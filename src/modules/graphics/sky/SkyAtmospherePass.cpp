#include "graphics/sky/SkyAtmospherePass.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Shader.h"
#include "graphics/ShaderResources.h"
#include "graphics/ViewPreparation.h"
#include "graphics/sky/SkyAtmosphereLuts.h"
#include "graphics/sky/SkyAtmosphereSpv.h"
#include "graphics/sky/SkyOpticalCycle.h"
#include "graphics/sky/SkyViewLutSpv.h"
#include "graphics/sky/SkyWispsLayer.h"
#include "graphics/sky/SkyWispsSpv.h"

namespace eve::graphics {
namespace {
Diagnostic invalid(const char* message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, message, "sky.atmosphere");
}
Result<std::array<float, Shader::kMaxFloats>> viewFrame(const glm::mat4& viewProjection, const glm::vec3& eyeMetres,
                                                        const SkyCelestialLight& light) {
    const float determinant = glm::determinant(viewProjection);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-20f)
        return Result<std::array<float, Shader::kMaxFloats>>::failure(
            invalid("Sky camera transform must be finite and invertible"));
    const auto                            inverse = glm::inverse(viewProjection);
    std::array<float, Shader::kMaxFloats> data{};
    std::copy_n(glm::value_ptr(inverse), 16, data.begin());
    for (int axis = 0; axis < 3; ++axis) {
        data[16 + axis] = eyeMetres[axis];
        data[20 + axis] = light.direction[axis];
        data[24 + axis] = light.irradiance[axis];
    }
    const auto farCenter = inverse * glm::vec4(0, 0, 1, 1);
    const auto forward   = glm::vec3(farCenter) - eyeMetres * farCenter.w;
    if (!std::isfinite(glm::length(forward)) || glm::length(forward) < 1e-20f)
        return Result<std::array<float, Shader::kMaxFloats>>::failure(invalid("Sky camera forward must be nonzero"));
    data[28] = forward.x;
    data[29] = forward.y;
    data[30] = forward.z;
    if (!std::all_of(data.begin(), data.end(), [](float v) { return std::isfinite(v); }))
        return Result<std::array<float, Shader::kMaxFloats>>::failure(invalid("Sky camera parameters must be finite"));
    return Result<std::array<float, Shader::kMaxFloats>>::success(data);
}
}  // namespace

struct SkyAtmospherePass::Impl {
    Graphics*                             graphics = nullptr;
    std::weak_ptr<const void>             lifetime;
    Shader*                               shader      = nullptr;
    Mesh*                                 mesh        = nullptr;
    Shader*                               wispsShader = nullptr;
    Mesh*                                 wispsMesh   = nullptr;
    std::array<float, 3>                  moonForward{};
    float                                 wispsMorphPhase = 0;
    float                                 dayPhase        = 0;
    bool                                  daylightEnabled = false;
    SkyCelestialLight                     light;
    uint64_t                              forward = 0, capture = 0, preparation = 0;
    Shader*                               viewShader = nullptr;
    Canvas*                               viewCanvas = nullptr;  // Provider-owned, valid through Graphics shutdown.
    std::array<float, Shader::kMaxFloats> preparedFrame{};
    bool                                  viewReady = false;
};

SkyAtmospherePass::SkyAtmospherePass() : impl_(std::make_unique<Impl>()) {}
SkyAtmospherePass::~SkyAtmospherePass() {
    detach();
    if (!impl_->lifetime.expired()) {
        if (impl_->viewShader) {
            if (impl_->graphics->releaseShader(impl_->viewShader))
                delete impl_->viewShader;
            else
                std::fputs("SkyAtmospherePass: view shader release rejected\n", stderr);
        }
        if (impl_->wispsMesh) {
            if (impl_->graphics->releaseMesh(impl_->wispsMesh))
                delete impl_->wispsMesh;
            else
                std::fputs("SkyAtmospherePass: wisps mesh release rejected\n", stderr);
        }
        if (impl_->wispsShader) {
            if (impl_->graphics->releaseShader(impl_->wispsShader))
                delete impl_->wispsShader;
            else
                std::fputs("SkyAtmospherePass: wisps shader release rejected\n", stderr);
        }
        if (impl_->mesh) {
            if (impl_->graphics->releaseMesh(impl_->mesh))
                delete impl_->mesh;
            else
                std::fputs("SkyAtmospherePass: mesh release rejected\n", stderr);
        }
        if (impl_->shader) {
            if (impl_->graphics->releaseShader(impl_->shader))
                delete impl_->shader;
            else
                std::fputs("SkyAtmospherePass: shader release rejected\n", stderr);
        }
    }
}

Result<std::unique_ptr<SkyAtmospherePass>> SkyAtmospherePass::prepare(Graphics&                      graphics,
                                                                      const SkyAtmosphereParameters& p,
                                                                      const SkyWispsLayer*           wisps) {
    using Prepared = Result<std::unique_ptr<SkyAtmospherePass>>;
    auto valid     = p.validate();
    if (!valid) return Prepared::failure(valid.status());
    if (wisps) {
        const auto&      w = *wisps;
        const std::array scalars{w.domeScale, w.morphRate,    w.morphPeriod, w.morphAmount,
                                 w.opacity,   w.litIntensity, w.cloudTime};
        const auto       finite = [](const auto& values) {
            return std::all_of(values.begin(), values.end(), [](float v) { return std::isfinite(v); });
        };
        const auto bounded = [](const auto& values) {
            return std::all_of(values.begin(), values.end(),
                               [](float v) { return std::isfinite(v) && std::abs(v) <= 1e6f; });
        };
        if (!w.densityTexture || w.corners.empty() || w.corners.size() % 27 || w.corners.size() > 9000000 ||
            !finite(w.corners) || !bounded(scalars) || !bounded(w.color) || !bounded(w.gradient) ||
            !bounded(w.movement) || !bounded(w.moonForward) || !bounded(w.materialContrast) ||
            !bounded(w.sunDiskColor) || !bounded(w.sunDiskShape) ||
            *std::min_element(w.sunDiskColor.begin(), w.sunDiskColor.end()) < 0 || w.sunDiskShape[0] <= 0 ||
            w.sunDiskShape[0] > 3.141593f || w.sunDiskShape[1] <= 0 || w.sunDiskShape[2] < 0 ||
            w.materialContrast[1] <= 0 || w.materialContrast[2] < 0 || w.domeScale <= 0 || w.domeScale > 100000 ||
            w.morphPeriod < .0001f || w.morphAmount < 0 || w.opacity < 0 || w.opacity > 1 || w.litIntensity < 0)
            return Prepared::failure(invalid("Invalid authored wisps geometry, texture or material"));
        if (w.daylightEnabled) {
            auto checked = w.daylight.validate();
            if (!checked) return Prepared::failure(checked.status());
            if (!w.starsTexture || !w.starsNoiseTexture || w.starsTexture->dimension != ShaderImageDimension::Image2D ||
                w.starsNoiseTexture->dimension != ShaderImageDimension::Image2D)
                return Prepared::failure(invalid("Daylight layer requires two-dimensional star and noise textures"));
        }
        if (w.daylight.moonDiskColor[3] > 0 && (!w.daylightEnabled || !w.moonColorTexture || !w.moonNormalTexture ||
                                                w.moonColorTexture->dimension != ShaderImageDimension::Image2D ||
                                                w.moonNormalTexture->dimension != ShaderImageDimension::Image2D))
            return Prepared::failure(invalid("Lunar disk requires daylight and both two-dimensional textures"));
        if (w.sunDiskCurveEnabled) {
            for (size_t i = 0; i < w.sunDiskCurve.size(); ++i) {
                const auto& key = w.sunDiskCurve[i];
                if (!bounded(key) || key[0] < 0 || key[0] > 1 || key[1] < 0 ||
                    (i % 4 && key[0] <= w.sunDiskCurve[i - 1][0]) || (i % 4 == 3 && key[1] <= 0))
                    return Prepared::failure(invalid("Invalid solar elevation curve"));
            }
        }
        for (size_t i = 0; i < w.corners.size(); i += 9) {
            for (size_t axis = 0; axis < 3; ++axis)
                if (std::abs(w.corners[i + axis]) > 1e8f)
                    return Prepared::failure(invalid("Wisps geometry exceeds supported extent"));
            if (w.corners[i] == 0 && w.corners[i + 1] == 0 && w.corners[i + 2] == 0)
                return Prepared::failure(invalid("Wisps dome vertex must be away from its origin"));
            for (size_t channel = 5; channel < 9; ++channel)
                if (w.corners[i + channel] < 0 || w.corners[i + channel] > 1)
                    return Prepared::failure(invalid("Wisps vertex colors must be normalized"));
        }
    }
#ifdef EVENGINE_WEBGPU
    (void)graphics;
    return Prepared::failure(
        Diagnostic::error(DiagnosticCode::Unsupported, "Atmosphere resource program requires Vulkan"));
#else
    auto  pass     = std::unique_ptr<SkyAtmospherePass>(new SkyAtmospherePass());
    auto& state    = *pass->impl_;
    state.graphics = &graphics;
    state.lifetime = graphics.resourceLifetime();
    if (state.lifetime.expired()) return Prepared::failure(invalid("Graphics provider has retired"));
    try {
        auto baked = detail::bakeSkyAtmosphereLuts(p);
        if (!baked) return Prepared::failure(baked.status());
        auto       tables  = std::move(baked).takeValue();
        const bool optical = wisps && wisps->opticalCycleEnabled;
        if (optical && !wisps->daylightEnabled)
            return Prepared::failure(invalid("Optical cycle requires daylight material"));
        if (optical) {
            tables.transmittance.clear();
            tables.multiScattering.clear();
            for (unsigned layer = 0; layer < detail::OpticalLayers; ++layer) {
                const float height = detail::OpticalMinimum + (detail::OpticalMaximum - detail::OpticalMinimum) *
                                                                  float(layer) / float(detail::OpticalLayers - 1);
                auto sample = detail::bakeSkyAtmosphereLuts(detail::opticalSample(p, wisps->daylight, height));
                if (!sample) return Prepared::failure(sample.status());
                const auto& value = sample.value();
                tables.transmittance.insert(tables.transmittance.end(), value.transmittance.begin(),
                                            value.transmittance.end());
                tables.multiScattering.insert(tables.multiScattering.end(), value.multiScattering.begin(),
                                              value.multiScattering.end());
            }
        }
        state.shader = graphics.newMeshShaderFromSpv({}, {});
        if (!state.shader) return Prepared::failure(invalid("Graphics did not create an atmosphere shader"));
        const auto&        fog = p.heightFog;
        std::vector<float> constants{p.rayleigh[0],
                                     p.rayleigh[1],
                                     p.rayleigh[2],
                                     p.rayleighHeightKm,
                                     p.mieScattering[0],
                                     p.mieScattering[1],
                                     p.mieScattering[2],
                                     p.mieHeightKm,
                                     p.mieAbsorption[0],
                                     p.mieAbsorption[1],
                                     p.mieAbsorption[2],
                                     p.mieAnisotropy,
                                     p.ozoneAbsorption[0],
                                     p.ozoneAbsorption[1],
                                     p.ozoneAbsorption[2],
                                     p.ozoneCenterKm,
                                     p.groundRadiusKm,
                                     p.groundRadiusKm + p.atmosphereHeightKm,
                                     p.ozoneHalfWidthKm,
                                     fog.directionalElevationRange[0],
                                     fog.densityPerMetre,
                                     fog.heightFalloffPerMetre,
                                     fog.baseHeightMetres,
                                     fog.startDistanceMetres,
                                     fog.inscattering[0],
                                     fog.inscattering[1],
                                     fog.inscattering[2],
                                     fog.maximumOpacity,
                                     fog.directionalInscattering[0],
                                     fog.directionalInscattering[1],
                                     fog.directionalInscattering[2],
                                     fog.directionalExponent,
                                     fog.directionalStartDistanceMetres,
                                     fog.atmosphereContribution,
                                     fog.skyDistanceMetres,
                                     fog.directionalElevationRange[1]};
        constants.insert(constants.end(), {optical ? float(detail::OpticalLayers) : 1.f, detail::OpticalMinimum,
                                           detail::OpticalMaximum, 0});
        const SkyDaylightLayer neutral;
        const auto&            opticalValues = optical ? wisps->daylight : neutral;
        for (const auto* v : {&opticalValues.rayDay, &opticalValues.rayDusk, &opticalValues.rayNight,
                              &opticalValues.absorptionDay, &opticalValues.absorptionNight})
            constants.insert(constants.end(), v->begin(), v->end());
        std::array<ShaderImageInput, 2> images;
        for (size_t i = 0; i < images.size(); ++i) {
            images[i].binding         = static_cast<uint32_t>(i);
            images[i].format          = ShaderImageFormat::RGBA32Float;
            images[i].dimension       = ShaderImageDimension::Image3D;
            images[i].depth           = optical ? detail::OpticalLayers : 1;
            images[i].sampler         = TextureSampler::linear();
            images[i].sampler.repeatU = images[i].sampler.repeatV = false;
        }
        images[0].width        = tables.TransmittanceWidth;
        images[0].height       = tables.TransmittanceHeight;
        auto transBytes        = std::make_shared<const std::vector<float>>(std::move(tables.transmittance));
        auto multiBytes        = std::make_shared<const std::vector<float>>(std::move(tables.multiScattering));
        images[0].bytes        = std::as_bytes(std::span(*transBytes));
        images[0].contentOwner = transBytes;
        images[1].width        = tables.MultiWidth;
        images[1].height       = tables.MultiHeight;
        images[1].bytes        = std::as_bytes(std::span(*multiBytes));
        images[1].contentOwner = multiBytes;
        auto uploaded = graphics.replaceMeshShaderResources(*state.shader, sky_atmosphere_vert, sky_atmosphere_frag,
                                                            {images, std::as_bytes(std::span(constants)), {}});
        if (!uploaded) return Prepared::failure(uploaded.status());
        auto surface = graphics.configureMeshShaderSurface(*state.shader, BlendMode::Opaque, false, true);
        if (!surface) return Prepared::failure(surface.status());
        MeshShaderRasterState raster;
        raster.depthCompare = MeshDepthCompare::LessEqual;
        auto configured     = graphics.configureMeshShaderRaster(*state.shader, raster);
        if (!configured) return Prepared::failure(configured.status());
        const float    positions[]{-1, -1, 1, 3, -1, 1, -1, 3, 1};
        const uint32_t indices[]{0, 1, 2};
        state.mesh = graphics.newMeshFromArrays(positions, nullptr, nullptr, 3, indices, 3);
        if (!state.mesh) return Prepared::failure(invalid("Graphics did not create atmosphere geometry"));
        state.viewShader = graphics.newMeshShaderFromSpv({}, {});
        if (!state.viewShader) return Prepared::failure(invalid("Could not create sky-view producer"));
        std::vector<float> viewConstants(constants);
        viewConstants.resize(280, 0);
        auto viewUpload = graphics.replaceMeshShaderResources(*state.viewShader, sky_view_lut_vert, sky_view_lut_frag,
                                                              {images, std::as_bytes(std::span(viewConstants)), {}});
        if (!viewUpload) return Prepared::failure(viewUpload.status());
        auto viewSurface = graphics.configureMeshShaderSurface(*state.viewShader, BlendMode::Opaque, false, true);
        if (!viewSurface) return Prepared::failure(viewSurface.status());
        state.viewCanvas = graphics.newHDRCanvas(192, 104);
        if (!state.viewCanvas) return Prepared::failure(invalid("Could not allocate sky-view target"));
        if (wisps) {
            const auto&        w = *wisps;
            std::vector<float> cloudConstants(constants.begin(), constants.end());
            cloudConstants.insert(cloudConstants.end(), {w.color[0],
                                                         w.color[1],
                                                         w.color[2],
                                                         w.morphAmount,
                                                         w.gradient[0],
                                                         w.gradient[1],
                                                         w.gradient[2],
                                                         w.gradient[3],
                                                         w.movement[0],
                                                         w.movement[1],
                                                         w.morphRate,
                                                         w.morphPeriod,
                                                         w.litIntensity,
                                                         w.moonGradient ? 1.f : 0.f,
                                                         w.staticClouds ? 1.f : 0.f,
                                                         w.opacity,
                                                         w.materialContrast[0],
                                                         w.materialContrast[1],
                                                         w.materialContrast[2],
                                                         w.authoredSkyLighting ? 1.f : 0.f,
                                                         w.sunDiskColor[0],
                                                         w.sunDiskColor[1],
                                                         w.sunDiskColor[2],
                                                         w.sunDiskShape[0],
                                                         w.sunDiskShape[1],
                                                         w.sunDiskShape[2],
                                                         w.sunDiskCurveEnabled ? 1.f : 0.f,
                                                         w.daylightEnabled ? 1.f : 0.f});
            for (const auto& key : w.sunDiskCurve) cloudConstants.insert(cloudConstants.end(), key.begin(), key.end());
            for (const auto* v : {&w.daylight.sun, &w.daylight.moon, &w.daylight.moonOrbit, &w.daylight.dayTint,
                                  &w.daylight.duskTint, &w.daylight.nightTint, &w.daylight.glow, &w.daylight.controls,
                                  &w.daylight.stars, &w.daylight.starUv, &w.daylight.twinkle})
                cloudConstants.insert(cloudConstants.end(), v->begin(), v->end());
            for (const auto& key : w.daylight.scatteringCurve)
                cloudConstants.insert(cloudConstants.end(), key.begin(), key.end());
            for (const auto* v : {&w.daylight.moonDiskColor, &w.daylight.moonDiskShape, &w.daylight.moonDiskLighting,
                                  &w.daylight.moonDiskGlow})
                cloudConstants.insert(cloudConstants.end(), v->begin(), v->end());
            if (optical) {
                auto updatedView =
                    graphics.replaceMeshShaderResources(*state.viewShader, sky_view_lut_vert, sky_view_lut_frag,
                                                        {images, std::as_bytes(std::span(cloudConstants)), {}});
                if (!updatedView) return Prepared::failure(updatedView.status());
            }
            // Legacy programs never sample these bindings, but Vulkan requires valid descriptors.
            const std::array<std::byte, 4> inactivePixel{};
            ShaderImageInput               inactive;
            inactive.width = inactive.height = 1;
            inactive.format                  = ShaderImageFormat::RGBA8;
            inactive.bytes                   = inactivePixel;
            std::array<ShaderImageInput, 7> cloudImages{
                images[0],
                images[1],
                *w.densityTexture,
                w.daylightEnabled ? *w.starsTexture : inactive,
                w.daylightEnabled ? *w.starsNoiseTexture : inactive,
                w.daylight.moonDiskColor[3] > 0 ? *w.moonColorTexture : inactive,
                w.daylight.moonDiskColor[3] > 0 ? *w.moonNormalTexture : inactive};
            cloudImages[3].binding = 3;
            cloudImages[4].binding = 4;
            cloudImages[5].binding = 5;
            cloudImages[6].binding = 6;
            state.daylightEnabled  = w.daylightEnabled;
            cloudImages[2].binding = 2;
            if (cloudImages[2].dimension != ShaderImageDimension::Image2D)
                return Prepared::failure(invalid("Wisps density texture must be two dimensional"));
            state.wispsShader = graphics.newMeshShaderFromSpv({}, {});
            if (!state.wispsShader) return Prepared::failure(invalid("Could not create wisps shader"));
            auto upload =
                graphics.replaceMeshShaderResources(*state.wispsShader, sky_wisps_vert, sky_wisps_frag,
                                                    {cloudImages, std::as_bytes(std::span(cloudConstants)), {}});
            if (!upload) return Prepared::failure(upload.status());
            auto cloudSurface = graphics.configureMeshShaderSurface(*state.wispsShader, BlendMode::Opaque, false, true);
            if (!cloudSurface) return Prepared::failure(cloudSurface.status());
            auto cloudRaster = graphics.configureMeshShaderRaster(*state.wispsShader, raster);
            if (!cloudRaster) return Prepared::failure(cloudRaster.status());
            const size_t          count = w.corners.size() / 9;
            std::vector<float>    xyz(count * 3), attributes(count * 3), uv(count * 2);
            std::vector<uint32_t> corners(count);
            const float           scale = w.domeScale * .01f;
            for (size_t i = 0; i < count; ++i) {
                xyz[i * 3]        = w.corners[i * 9] * scale;
                xyz[i * 3 + 1]    = w.corners[i * 9 + 2] * scale;
                xyz[i * 3 + 2]    = w.corners[i * 9 + 1] * scale;
                attributes[i * 3] = w.corners[i * 9 + 5];
                uv[i * 2]         = w.corners[i * 9 + 3];
                uv[i * 2 + 1]     = w.corners[i * 9 + 4];
                corners[i]        = static_cast<uint32_t>(i);
            }
            state.wispsMesh =
                graphics.newMeshFromArrays(xyz.data(), attributes.data(), uv.data(), static_cast<int>(count),
                                           corners.data(), static_cast<int>(count));
            if (!state.wispsMesh) return Prepared::failure(invalid("Could not create wisps dome"));
            state.moonForward     = w.moonForward;
            const double phase    = std::fmod(double(w.cloudTime) * w.morphRate / w.morphPeriod, 1.0);
            state.wispsMorphPhase = static_cast<float>(phase < 0 ? phase + 1.0 : phase);
            if (state.wispsMorphPhase >= 1) state.wispsMorphPhase = 0;
        }
        return Prepared::success(std::move(pass));
    } catch (const std::exception& error) {
        return Prepared::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "sky.atmosphere"));
    }
#endif
}

Result<void> SkyAtmospherePass::setLight(const SkyCelestialLight& light) {
    return setFrame({light, impl_->wispsMorphPhase, impl_->dayPhase});
}
Result<void> SkyAtmospherePass::setFrame(const SkyAtmosphereFrame& frame) {
    if (impl_->lifetime.expired()) return Result<void>::failure(invalid("Sky graphics provider retired"));
    if (!std::isfinite(frame.wispsMorphPhase) || frame.wispsMorphPhase < 0 || frame.wispsMorphPhase >= 1)
        return Result<void>::failure(invalid("Wisps phase must be finite and normalized"));
    if (!std::isfinite(frame.dayPhase) || frame.dayPhase < 0 || frame.dayPhase >= 1)
        return Result<void>::failure(invalid("Day phase must be finite and normalized"));
    const auto& light = frame.sun;
    for (int axis = 0; axis < 3; ++axis)
        if (!std::isfinite(light.direction[axis]) || !std::isfinite(light.irradiance[axis]) ||
            light.irradiance[axis] < 0)
            return Result<void>::failure(invalid("Sky light direction and energy must be finite; energy nonnegative"));
    const float length = glm::length(light.direction);
    if (!std::isfinite(length) || length < 1e-6f)
        return Result<void>::failure(invalid("Sky light direction must be nonzero"));
    impl_->light = light;
    impl_->light.direction /= length;
    impl_->wispsMorphPhase = frame.wispsMorphPhase;
    impl_->dayPhase        = frame.dayPhase;
    return Result<void>::success();
}

Result<void> SkyAtmospherePass::prepareView(const glm::mat4& viewProjection, const glm::vec3& eyeMetres) {
    if (impl_->lifetime.expired()) return Result<void>::failure(invalid("Sky graphics provider retired"));
    auto frame = viewFrame(viewProjection, eyeMetres, impl_->light);
    if (!frame) return Result<void>::failure(frame.status());
    frame.value()[19] = impl_->dayPhase;
    if (impl_->viewReady && impl_->preparedFrame == frame.value()) return Result<void>::success();
    impl_->viewReady = false;
    bool opened      = false;
    try {
        impl_->viewShader->setPushConstantBlock(frame.value());
        impl_->graphics->begin3DFrameToCanvas(impl_->viewCanvas);
        opened = true;
        impl_->graphics->drawMeshShader(impl_->mesh, glm::mat4(1), nullptr, Color(1, 1, 1, 1), impl_->viewShader);
        impl_->graphics->end3DFrameToCanvas();
        opened               = false;
        impl_->preparedFrame = frame.value();
        impl_->viewReady     = true;
        return Result<void>::success();
    } catch (const std::exception& error) {
        if (opened) impl_->graphics->end3DFrameToCanvas();
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "sky.view"));
    }
}

Result<void> SkyAtmospherePass::draw(const glm::mat4& viewProjection, const glm::vec3& eyeMetres) {
    return drawView(viewProjection, eyeMetres, false);
}
Result<void> SkyAtmospherePass::drawView(const glm::mat4& viewProjection, const glm::vec3& eyeMetres, bool reflection) {
    if (impl_->lifetime.expired())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Sky graphics provider retired"));
    auto frame = viewFrame(viewProjection, eyeMetres, impl_->light);
    if (!frame) return Result<void>::failure(frame.status());
    auto data = frame.value();
    data[19]  = impl_->dayPhase;
    if (!impl_->viewReady || impl_->preparedFrame != data)
        return Result<void>::failure(invalid("Sky view must be prepared before its destination pass opens"));
    impl_->shader->setPushConstantBlock(data);
    try {
        impl_->graphics->drawMeshShader(impl_->mesh, glm::mat4(1), impl_->viewCanvas->getTexture(), Color(1, 1, 1, 1),
                                        impl_->shader);
        if (impl_->wispsMesh) {
            std::copy_n(glm::value_ptr(viewProjection), 16, data.begin());
            data[19] = impl_->daylightEnabled ? impl_->dayPhase : impl_->moonForward[0];
            data[23] = impl_->moonForward[1];
            data[27] = impl_->moonForward[2];
            // Phase is nonnegative: reserve its sign bit for capture without losing mantissa precision.
            data[31] =
                std::bit_cast<float>(std::bit_cast<uint32_t>(impl_->wispsMorphPhase) | (reflection ? 0x80000000u : 0u));
            impl_->wispsShader->setPushConstantBlock(data);
            impl_->graphics->drawMeshShader(impl_->wispsMesh, glm::mat4(1), impl_->viewCanvas->getTexture(),
                                            Color(1, 1, 1, 1), impl_->wispsShader);
        }
    } catch (const std::exception& error) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "sky.atmosphere"));
    }
    return Result<void>::success();
}

Result<void> SkyAtmospherePass::attach() {
    if (impl_->lifetime.expired()) return Result<void>::failure(invalid("Sky graphics provider retired"));
    if (impl_->forward) return Result<void>::success();
    auto render = [this](Graphics& graphics, const Camera3D::Data& camera, const glm::mat4& vp, float) {
        if (impl_->lifetime.expired() || &graphics != impl_->graphics) return;
        auto result = draw(vp, {camera.eyeX, camera.eyeY, camera.eyeZ});
        if (!result) std::fprintf(stderr, "SkyAtmospherePass: %s\n", result.status().describe().c_str());
    };
    try {
        auto preparation = addViewPreparation([this](Graphics& graphics, const glm::mat4& vp, const glm::vec3& eye) {
            if (&graphics != impl_->graphics) return Result<void>::success();
            return prepareView(vp, eye);
        });
        if (!preparation) return Result<void>::failure(preparation.status());
        impl_->preparation = preparation.value();
        impl_->forward     = RenderSystem3D::addForwardExtraDrawer(render);
        impl_->capture     = RenderSystem3D::addCaptureExtraDrawer(
            ~uint32_t(0),
            [this](Graphics& graphics, const Camera3D::Data& camera, const glm::mat4& vp, float, uint32_t) {
                if (impl_->lifetime.expired() || &graphics != impl_->graphics) return;
                auto result = drawView(vp, {camera.eyeX, camera.eyeY, camera.eyeZ}, true);
                if (!result) std::fprintf(stderr, "SkyAtmospherePass: %s\n", result.status().describe().c_str());
            });
        if (!impl_->forward || !impl_->capture) {
            detach();
            return Result<void>::failure(invalid("Sky contributor registration failed"));
        }
        return Result<void>::success();
    } catch (const std::exception& error) {
        detach();
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "sky.atmosphere"));
    }
}
void SkyAtmospherePass::detach() noexcept {
    removeViewPreparation(impl_->preparation);
    impl_->preparation = 0;
    RenderSystem3D::removeForwardExtraDrawer(impl_->forward);
    RenderSystem3D::removeCaptureExtraDrawer(impl_->capture);
    impl_->forward = impl_->capture = 0;
}
}  // namespace eve::graphics
