#include "graphics/Water.h"

#include "graphics/Graphics.h"
#include "graphics/GBuffer.h"
#include "graphics/Mesh.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/shaders/WaterWgsl.h"
#include "graphics/shaders/water_frag_spv.inc"
#include "graphics/shaders/water_vert_spv.inc"
#include "image/ImageData.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace eve::graphics {

namespace {

// Push-constant layout (data[32]):
//   0 time, 1 waveSpeed, 2 waveAmp, 3 waveScale, 4 rippleAmp,
//   5 rippleCount, 6 rippleInterval, 7 foamWidth, 8 foamSoftness,
//   9 foamStrength, 10..12 deepColor, 13..15 shallowColor,
//   16 depthDistance, 17..19 foamColor, 20 reflectionIntensity,
//   21..23 reflectionTint, 24 sunIntensity, 25 fresnelPower,
//   26 causticsStrength, 27 signedSsrStrength, 28 refractionStrength,
//   29 signedOpacityAndDepthAvailability, 30 causticsScale, 31 waveSharpness.

const char *kUniformNames[] = {
    "time",       "waveSpeed", "waveAmp",   "waveScale",   "rippleAmp",
    "rippleCnt",  "rippleInt", "foamWidth", "foamSoftness", "foamStrength",
    "deepColor",  "shallowColor", "depthDistance", "foamColor", "reflInten",
    "reflTint",   "sunInten", "fresnelPower", "causticsStrength", "ssrStrength",
    "refractionStrength", "opacityAndDepth", "causticsScale", "waveSharpness",
};
const int kUniformCount = int(sizeof(kUniformNames) / sizeof(kUniformNames[0]));

}  // namespace

namespace {
Result<glm::ivec2> waterPointCounts(const WaterMeshSettings& settings) {
    if ((settings.type != WaterMeshType::Plane && settings.type != WaterMeshType::Circle) ||
        !std::isfinite(settings.sizeX) || !std::isfinite(settings.sizeY) || !std::isfinite(settings.sizeZ) ||
        !std::isfinite(settings.densityX) || !std::isfinite(settings.densityY) ||
        !std::isfinite(settings.height) || settings.sizeX <= 0.0F || settings.sizeY <= 0.0F ||
        settings.sizeZ <= 0.0F || settings.densityX <= 0.0F || settings.densityY <= 0.0F) {
        return Result<glm::ivec2>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "water.mesh: finite positive size/density and a supported shape are required"));
    }
    const double scale = settings.type == WaterMeshType::Plane ? 1.0 : 0.5;
    const double rawX = double(settings.densityX) / 40.0 * double(settings.sizeX) * scale;
    const double rawY = double(settings.densityY) / 40.0 * double(settings.sizeZ) * scale;
    if (rawX > 4096.0 || rawY > 4096.0)
        return Result<glm::ivec2>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "water.mesh: generated axis point count exceeds 4096"));
    int x = static_cast<int>(std::nearbyint(rawX));
    int y = static_cast<int>(std::nearbyint(rawY));
    if (settings.type == WaterMeshType::Plane) {
        ++x;
        ++y;
        if (x < 2 || y < 2)
            return Result<glm::ivec2>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "water.mesh: plane requires at least one polygon"));
    } else if (x < 1 || y < 1) {
        return Result<glm::ivec2>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "water.mesh: circle requires at least one ring"));
    }
    return Result<glm::ivec2>::success({x, y});
}

int waterCirclePoint(int ring, int extent) {
    if (ring < 0) return 0;
    const int count = (ring + 1) * 6;
    extent %= count;
    if (extent < 0) extent += count;
    return 3 * ring * (ring + 1) + extent + 1;
}

template <typename Stop, typename Value, typename Project>
Value sampleStops(const std::vector<Stop>& stops, float time, Project project) {
    if (time <= stops.front().time) return project(stops.front());
    if (time >= stops.back().time) return project(stops.back());
    for (std::size_t i = 1; i < stops.size(); ++i) if (time <= stops[i].time) {
        const auto& a = stops[i - 1]; const auto& b = stops[i];
        const float t = (time - a.time) / (b.time - a.time);
        return project(a) + (project(b) - project(a)) * t;
    }
    return project(stops.back());
}
}  // namespace

Result<void> WaterDepthGradient::addColorStop(float time, float red, float green, float blue) {
    if (!std::isfinite(time) || !std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue) ||
        time < 0.0F || time > 1.0F || red < 0.0F || red > 1.0F || green < 0.0F || green > 1.0F ||
        blue < 0.0F || blue > 1.0F)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "water.gradient: normalized finite RGB stop required"));
    auto candidate = colorStops; candidate.push_back({time, {red, green, blue}});
    std::stable_sort(candidate.begin(), candidate.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    colorStops = std::move(candidate); return Result<void>::success();
}

Result<void> WaterDepthGradient::addAlphaStop(float time, float alpha) {
    if (!std::isfinite(time) || !std::isfinite(alpha) || time < 0.0F || time > 1.0F || alpha < 0.0F || alpha > 1.0F)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "water.gradient: normalized finite alpha stop required"));
    auto candidate = alphaStops; candidate.push_back({time, alpha});
    std::stable_sort(candidate.begin(), candidate.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
    alphaStops = std::move(candidate); return Result<void>::success();
}

void WaterDepthGradient::clear() { colorStops.clear(); alphaStops.clear(); }

Result<int> calculateWaterMeshTriangles(const WaterMeshSettings& settings) {
    auto counts = waterPointCounts(settings);
    if (!counts) return Result<int>::failure(counts.status());
    const std::int64_t triangles = settings.type == WaterMeshType::Plane
                                       ? std::int64_t(counts.value().x - 1) * (counts.value().y - 1) * 2
                                       : std::int64_t(counts.value().x) * counts.value().x * 6;
    if (triangles <= 0 || triangles > std::numeric_limits<int>::max() / 3)
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "water.mesh: triangle count is not representable"));
    return Result<int>::success(static_cast<int>(triangles));
}

Shader *newWaterShader(Graphics *gfx) {
    // Use precompiled SPIR-V (not runtime glslc) so the water shader also works
    // on platforms without a runtime compiler (e.g. Windows).
    Shader *sh = nullptr;
    if (gfx->getBackendName() == "webgpu") {
        sh = gfx->newMeshShaderFromWgsl(shaders::kWaterVertWgsl, shaders::kWaterFragWgsl);
    } else {
        std::vector<uint32_t> vert(water_vert_spv, water_vert_spv + water_vert_spv_count);
        std::vector<uint32_t> frag(water_frag_spv, water_frag_spv + water_frag_spv_count);
        sh = gfx->newMeshShaderFromSpv(vert, frag);
    }
    gfx->configureMeshShaderSurface(*sh, BlendMode::Alpha, false, false)
        .expect("water requires alpha blending with depth writes disabled");
    for (int i = 0; i < kUniformCount; ++i) {
        const std::string name(kUniformNames[i]);
        if (name == "deepColor" || name == "shallowColor" || name == "foamColor" || name == "reflTint")
            sh->declareVec3(kUniformNames[i]);
        else
            sh->declareFloat(kUniformNames[i]);
    }
    return sh;
}

int Water::paramCount() { return kUniformCount; }

std::string Water::paramName(int index) {
    if (index < 0 || index >= kUniformCount) return {};
    return kUniformNames[index];
}

Water::Water(Graphics *gfx) : gfx_(gfx) {
    shader_ = newWaterShader(gfx);
    bindParams();
    captureDrawerToken_ = RenderSystem3D::addCaptureExtraDrawer(
        0xffffffffu,
        [this](Graphics &, const Camera3D::Data &, const glm::mat4 &, float, uint32_t mask) {
            if (!reflectionCaptureEnabled_ || (reflectionCaptureMask_ & mask) == 0u) return;
            drawReflectionCapture();
        });
}

Water::~Water() { RenderSystem3D::removeCaptureExtraDrawer(captureDrawerToken_); }

void Water::createPlane(float sizeX, float sizeZ, int segX, int segZ) {
    segX = std::max(1, segX);
    segZ = std::max(1, segZ);
    std::vector<float> pos, nrm, uv;
    std::vector<uint32_t> idx;
    for (int z = 0; z <= segZ; ++z) {
        for (int x = 0; x <= segX; ++x) {
            pos.push_back(-sizeX * 0.5f + sizeX * float(x) / segX);
            pos.push_back(0.f);
            pos.push_back(-sizeZ * 0.5f + sizeZ * float(z) / segZ);
            nrm.push_back(0.f);
            nrm.push_back(1.f);
            nrm.push_back(0.f);
            uv.push_back(float(x) / segX);
            uv.push_back(float(z) / segZ);
        }
    }
    for (int z = 0; z < segZ; ++z) {
        for (int x = 0; x < segX; ++x) {
            const uint32_t a = uint32_t(z * (segX + 1) + x);
            const uint32_t b = a + 1;
            const uint32_t c = a + uint32_t(segX + 1);
            const uint32_t d = c + 1;
            idx.push_back(a);
            idx.push_back(c);
            idx.push_back(b);
            idx.push_back(b);
            idx.push_back(c);
            idx.push_back(d);
        }
    }
    mesh_ = gfx_->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3),
                                    idx.data(), int(idx.size()));
}

Result<int> Water::createProceduralMesh(const WaterMeshSettings& settings) {
    auto counts = waterPointCounts(settings);
    if (!counts) return Result<int>::failure(counts.status());
    auto triangleCount = calculateWaterMeshTriangles(settings);
    if (!triangleCount) return Result<int>::failure(triangleCount.status());
    if (!gfx_)
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "water.mesh: graphics owner is unavailable"));

    std::vector<float> pos, nrm, uv;
    std::vector<uint32_t> idx;
    if (settings.type == WaterMeshType::Plane) {
        const int nx = counts.value().x, nz = counts.value().y;
        pos.reserve(std::size_t(nx) * nz * 3); nrm.reserve(std::size_t(nx) * nz * 3);
        uv.reserve(std::size_t(nx) * nz * 2); idx.reserve(std::size_t(triangleCount.value()) * 3);
        const float sx = settings.sizeX / float(nx - 1), sz = settings.sizeZ / float(nz - 1);
        for (int z = 0; z < nz; ++z) for (int x = 0; x < nx; ++x) {
            pos.insert(pos.end(), {(float(nx - 1) * 0.5F - x) * sx, settings.height * settings.sizeY,
                                   (float(nz - 1) * 0.5F - z) * sz});
            nrm.insert(nrm.end(), {0.0F, 1.0F, 0.0F});
            uv.insert(uv.end(), {(float(nx / 2) - x) / float(nx), (float(nz / 2) - z) / float(nz)});
        }
        for (int z = 0; z < nz - 1; ++z) for (int x = 0; x < nx - 1; ++x) {
            const uint32_t a = uint32_t(z * nx + x), b = a + 1, c = a + uint32_t(nx), d = c + 1;
            idx.insert(idx.end(), {a, c, b, c, d, b});
        }
    } else {
        const int rings = counts.value().x;
        const std::size_t vertexCount = 1u + 3u * std::size_t(rings) * std::size_t(rings + 1);
        pos.reserve(vertexCount * 3); nrm.reserve(vertexCount * 3); uv.reserve(vertexCount * 2);
        idx.reserve(std::size_t(triangleCount.value()) * 3);
        const float scaleX = (settings.sizeX * 0.5F) / float(rings);
        const float scaleZ = (settings.sizeZ * 0.5F) / float(counts.value().y);
        pos.insert(pos.end(), {0.0F, 0.0F, 0.0F}); nrm.insert(nrm.end(), {0.0F, 1.0F, 0.0F});
        uv.insert(uv.end(), {0.0F, 0.0F});
        for (int ring = 0; ring < rings; ++ring) {
            const int ringPoints = (ring + 1) * 6;
            const float step = 2.0F * 3.14159265358979323846F / float(ringPoints);
            for (int point = 0; point < ringPoints; ++point) {
                const float cx = std::cos(step * point) * float(ring + 1);
                const float cz = std::sin(-step * point) * float(ring + 1);
                pos.insert(pos.end(), {cx * scaleX, 0.0F, cz * scaleZ});
                nrm.insert(nrm.end(), {0.0F, 1.0F, 0.0F});
                uv.insert(uv.end(), {cx / settings.densityX, cz / settings.densityX});
            }
        }
        for (int ring = 0; ring < rings; ++ring) {
            int other = 0;
            for (int point = 0; point < (ring + 1) * 6; ++point) {
                if (point % (ring + 1) != 0) {
                    idx.insert(idx.end(), {uint32_t(waterCirclePoint(ring - 1, other + 1)),
                                           uint32_t(waterCirclePoint(ring - 1, other)),
                                           uint32_t(waterCirclePoint(ring, point)),
                                           uint32_t(waterCirclePoint(ring, point)),
                                           uint32_t(waterCirclePoint(ring, point + 1)),
                                           uint32_t(waterCirclePoint(ring - 1, other + 1))});
                    ++other;
                } else {
                    idx.insert(idx.end(), {uint32_t(waterCirclePoint(ring, point)),
                                           uint32_t(waterCirclePoint(ring, point + 1)),
                                           uint32_t(waterCirclePoint(ring - 1, other))});
                }
            }
        }
    }
    Mesh* candidate = gfx_->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3), idx.data(),
                                               int(idx.size()));
    if (!candidate)
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "water.mesh: graphics mesh allocation failed"));
    mesh_ = candidate;
    return Result<int>::success(int(pos.size() / 3));
}

Result<int> Water::setDepthGradient(const WaterDepthGradient& gradient, int resolution) {
    if (!gfx_ || resolution < 2 || resolution > 4096 || gradient.colorStops.empty() || gradient.alphaStops.empty())
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "water.gradient: graphics, color/alpha keys and resolution 2..4096 required"));
    for (std::size_t i = 1; i < gradient.colorStops.size(); ++i)
        if (gradient.colorStops[i - 1].time == gradient.colorStops[i].time)
            return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "water.gradient: duplicate color times are ambiguous"));
    for (std::size_t i = 1; i < gradient.alphaStops.size(); ++i)
        if (gradient.alphaStops[i - 1].time == gradient.alphaStops[i].time)
            return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "water.gradient: duplicate alpha times are ambiguous"));
    image::ImageData pixels(resolution, resolution, "RGBA8");
    for (int x = 0; x < resolution; ++x) {
        const float t = float(x) / float(resolution);
        const glm::vec3 color = sampleStops<WaterGradientColorStop, glm::vec3>(
            gradient.colorStops, t, [](const auto& stop) { return stop.color; });
        const float alpha = sampleStops<WaterGradientAlphaStop, float>(
            gradient.alphaStops, t, [](const auto& stop) { return stop.alpha; });
        for (int y = 0; y < resolution; ++y)
            pixels.setPixel(x, y, image::ImageData::Colorf{color.r, color.g, color.b, alpha});
    }
    Texture* candidate = gfx_->newTextureFromImageData(&pixels, false, false);
    if (!candidate)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                       "water.gradient: texture allocation failed"));
    depthGradientTexture_ = candidate;
    return Result<int>::success(resolution * resolution);
}

void Water::update(float dt) {
    time_ += dt;
    bindParams();
}

void Water::setTime(float seconds) {
    time_ = seconds;
    bindParams();
}

void Water::setWaveSpeed(float v) { config_.waveSpeed = std::clamp(v, -20.0F, 20.0F); }
void Water::setWaveAmplitude(float v) { config_.waveAmplitude = std::clamp(v, 0.0F, 4.0F); }
void Water::setRippleAmplitude(float v) { config_.rippleAmplitude = std::clamp(v, 0.0F, 4.0F); }
void Water::setEdgeFalloff(float v) { config_.foamWidth = std::clamp(v, 0.001F, 1000.0F); }
void Water::setRippleCount(int v) { config_.rippleCount = std::clamp(v, 0, 8); }
void Water::setRippleInterval(float v) { config_.rippleInterval = std::clamp(v, 0.05F, 60.0F); }
void Water::setWaveScale(float v) { config_.waveScale = std::clamp(v, 0.01F, 256.0F); }
Result<void> Water::setWaveDirectionAngle(float degrees) {
    if (!std::isfinite(degrees))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "water.direction: finite degrees required"));
    waveDirectionAngle_ = std::fmod(degrees, 360.0F);
    if (waveDirectionAngle_ < 0.0F) waveDirectionAngle_ += 360.0F;
    return Result<void>::success();
}
void Water::setWaterColor(float r, float g, float b) {
    config_.deepColor = glm::max(glm::vec3(r, g, b), glm::vec3(0.0F));
}
void Water::setReflectionTint(float r, float g, float b) {
    config_.reflectionTint = glm::max(glm::vec3(r, g, b), glm::vec3(0.0F));
}
void Water::setReflectionIntensity(float v) { config_.reflectionIntensity = std::clamp(v, 0.0F, 4.0F); }
void Water::setSunIntensity(float v) { config_.sunIntensity = std::clamp(v, 0.0F, 8.0F); }
void Water::setScreenSpaceReflection(bool enabled, float strength) {
    config_.screenSpaceReflection = enabled;
    config_.screenSpaceReflectionStrength = std::clamp(strength, 0.0F, 4.0F);
}
void Water::setViewport(float w, float h) {
    viewportW_ = std::max(0.f, w);
    viewportH_ = std::max(0.f, h);
}

void Water::bindParams() {
    if (!shader_) return;
    shader_->sendFloat("time", time_);
    shader_->sendFloat("waveSpeed", config_.waveSpeed);
    shader_->sendFloat("waveAmp", config_.waveAmplitude);
    shader_->sendFloat("waveScale", config_.waveScale);
    shader_->sendFloat("rippleAmp", config_.rippleAmplitude);
    // Preserve the 128-byte portable parameter block: the integer part is ripple count and
    // [0,0.5) stores PWS_WaterSystem.directionAngle / 720. Shaders decode both independently.
    shader_->sendFloat("rippleCnt", float(config_.rippleCount) + waveDirectionAngle_ / 720.0F);
    shader_->sendFloat("rippleInt", config_.rippleInterval);
    shader_->sendFloat("foamWidth", config_.foamWidth);
    shader_->sendFloat("foamSoftness", config_.foamSoftness);
    shader_->sendFloat("foamStrength", config_.foamStrength);
    shader_->sendVec3("deepColor", config_.deepColor.x, config_.deepColor.y, config_.deepColor.z);
    shader_->sendVec3("shallowColor", config_.shallowColor.x, config_.shallowColor.y, config_.shallowColor.z);
    shader_->sendFloat("depthDistance", depthGradientTexture_ ? -config_.depthDistance : config_.depthDistance);
    shader_->sendVec3("foamColor", config_.foamColor.x, config_.foamColor.y, config_.foamColor.z);
    shader_->sendFloat("reflInten", config_.reflectionIntensity);
    shader_->sendVec3("reflTint", config_.reflectionTint.x, config_.reflectionTint.y, config_.reflectionTint.z);
    shader_->sendFloat("sunInten", config_.sunIntensity);
    shader_->sendFloat("fresnelPower", config_.fresnelPower);
    shader_->sendFloat("causticsStrength", config_.causticsStrength);
    shader_->sendFloat("ssrStrength", config_.screenSpaceReflection ? config_.screenSpaceReflectionStrength : -1.0F);
    shader_->sendFloat("refractionStrength", config_.refractionStrength);
    shader_->sendFloat("opacityAndDepth", -(config_.opacity + 1.0F));
    shader_->sendFloat("causticsScale", config_.causticsScale);
    shader_->sendFloat("waveSharpness", config_.waveSharpness);
}

Result<void> Water::applyConfigJson(const std::string& json) {
    auto candidate = WaterStyleConfig::fromJson(json);
    if (!candidate) return Result<void>::failure(candidate.status());
    config_ = std::move(candidate).takeValue();
    bindParams();
    return Result<void>::success();
}

Result<std::string> Water::configJson() const { return config_.toJson(); }

void Water::draw() { drawWithReflection(nullptr, 0.0F); }

void Water::drawWithPlanarReflection(Texture* planarReflection, float strength) {
    drawWithReflection(planarReflection, std::clamp(strength, 0.0F, 4.0F));
}

void Water::drawWithReflection(Texture* requestedReflection, float requestedStrength) {
    if (!gfx_ || !mesh_ || !shader_) return;
    bindParams();
    Texture *reflection = requestedReflection;
    float reflectionStrength = requestedReflection ? requestedStrength : -1.0F;
    RenderControl *rc = gfx_->getRenderControl();
    if (!reflection && config_.screenSpaceReflection && rc && rc->isEnabled("ssr")) {
        ScreenSpaceReflection *ssr = gfx_->pipelineScreenSpaceReflection();
        if (ssr->hasValidHistory()) {
            reflection = ssr->getReflectionTexture();
            reflectionStrength = config_.screenSpaceReflectionStrength;
        }
    }
    shader_->sendFloat("ssrStrength", reflection ? reflectionStrength : -1.0F);
    Texture *depth = rc && rc->getGBuffer() ? rc->getGBuffer()->getDepthTexture() : nullptr;
    Texture *sceneColor = gfx_->getSceneColorTexture();
    shader_->sendFloat("opacityAndDepth", depth ? config_.opacity : -(config_.opacity + 1.0F));
    shader_->sendFloat("refractionStrength", sceneColor ? config_.refractionStrength : 0.0F);
    gfx_->setMesh3DHeightTexture(reflection);
    gfx_->setMesh3DNormalTexture(depthGradientTexture_);
    gfx_->setMesh3DSceneDepth(depth);
    gfx_->drawMeshShader(mesh_, glm::mat4(1.f), sceneColor, glm::vec4(1.f), shader_);
    gfx_->setMesh3DHeightTexture(nullptr);
    gfx_->setMesh3DNormalTexture(nullptr);
    gfx_->setMesh3DSceneDepth(nullptr);
}

void Water::drawReflectionCapture() {
    if (!gfx_ || !mesh_ || !shader_) return;
    bindParams();
    // Probe captures must never sample the main-view SSR history: that creates
    // view-dependent feedback and recursively bakes an old reflection into the cube.
    shader_->sendFloat("ssrStrength", -1.0F);
    shader_->sendFloat("opacityAndDepth", -(config_.opacity + 1.0F));
    shader_->sendFloat("refractionStrength", 0.0F);
    gfx_->setMesh3DHeightTexture(nullptr);
    gfx_->setMesh3DNormalTexture(depthGradientTexture_);
    gfx_->drawMeshShader(mesh_, glm::mat4(1.f), nullptr, glm::vec4(1.f), shader_);
    gfx_->setMesh3DNormalTexture(nullptr);
    // Restore the authored state for a subsequent main-view draw in the same frame.
    shader_->sendFloat("ssrStrength", config_.screenSpaceReflection ? config_.screenSpaceReflectionStrength : -1.0F);
    shader_->sendFloat("refractionStrength", config_.refractionStrength);
}

}  // namespace eve::graphics
