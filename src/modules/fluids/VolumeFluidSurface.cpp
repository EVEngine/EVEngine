#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include "common/Profile.h"
#include "fluids/FluidSurfaceRenderer.h"
#include "fluids/VolumeFluid.h"

namespace eve::fluids {
namespace {
constexpr size_t   kMaxGasParticles     = 65536;
constexpr uint64_t kMaxGasCoveredPixels = 4000000;
constexpr float    kBlurWeights[7]      = {.006f, .061f, .242f, .383f, .242f, .061f, .006f};
}  // namespace
void FluidSurfaceRenderer::renderVolume(const VolumeFluid& sim) { renderVolumeInternal(sim, false, 1.f); }

void FluidSurfaceRenderer::renderVolumeColorOnly(const VolumeFluid& sim) { renderVolumeInternal(sim, true, 1.f); }

Result<void> FluidSurfaceRenderer::renderVolumeInterpolated(const VolumeFluid& sim, float alpha) {
    if (!std::isfinite(alpha) || alpha < 0.f || alpha > 1.f)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid surface interpolation alpha", "fluids.surface.interpolation"));
    renderVolumeInternal(sim, false, alpha);
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::configureRefraction(float transparency, float absorption, float coefficient,
                                                       int downsample) {
    if (!std::isfinite(transparency) || transparency < 0.f || transparency > 1.f || !std::isfinite(absorption) ||
        absorption < 0.f || absorption > 30.f || !std::isfinite(coefficient) || coefficient < -.1f ||
        coefficient > .1f || downsample < 1 || downsample > 4)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Invalid fluid refraction controls",
                                                       "fluids.surface.configureRefraction"));
    refractionTransparency_ = transparency;
    refractionAbsorption_   = absorption;
    refractionCoefficient_  = coefficient;
    refractionDownsample_   = downsample;
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::configureRefractionEnabled(bool enabled) {
    refractionEnabled_ = enabled;
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::compositeConfiguredSceneRefraction(std::span<const uint8_t> sceneColor) {
    if (!refractionEnabled_) return Result<void>::success();
    const int    width = params_.width, height = params_.height;
    const size_t pixels = size_t(width) * size_t(height);
    if (!auxiliaryCurrent_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "Current color frame has no host-visible surface auxiliaries",
                                                       "fluids.surface.configuredRefraction"));
    if (sceneColor.size() != pixels * 4u)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Refraction scene dimensions are invalid",
                                                       "fluids.surface.configuredRefraction"));

    const int targetWidth  = std::max(1, (width + refractionDownsample_ - 1) / refractionDownsample_);
    const int targetHeight = std::max(1, (height + refractionDownsample_ - 1) / refractionDownsample_);
    refractionScratch_.resize(size_t(targetWidth) * size_t(targetHeight) * 4u);
    const auto sample = [](std::span<const uint8_t> source, int sourceWidth, int sourceHeight, float x, float y,
                           int channel) {
        x              = std::clamp(x, 0.f, float(sourceWidth - 1));
        y              = std::clamp(y, 0.f, float(sourceHeight - 1));
        const int   x0 = int(std::floor(x)), y0 = int(std::floor(y));
        const int   x1 = std::min(x0 + 1, sourceWidth - 1), y1 = std::min(y0 + 1, sourceHeight - 1);
        const float tx = x - float(x0), ty = y - float(y0);
        const auto  at = [&](int px, int py) {
            return float(source[(size_t(py) * size_t(sourceWidth) + size_t(px)) * 4u + size_t(channel)]);
        };
        return std::lerp(std::lerp(at(x0, y0), at(x1, y0), tx), std::lerp(at(x0, y1), at(x1, y1), tx), ty);
    };
    for (int y = 0; y < targetHeight; ++y)
        for (int x = 0; x < targetWidth; ++x) {
            const float  sourceX = (float(x) + .5f) * float(width) / float(targetWidth) - .5f;
            const float  sourceY = (float(y) + .5f) * float(height) / float(targetHeight) - .5f;
            const size_t out     = (size_t(y) * size_t(targetWidth) + size_t(x)) * 4u;
            for (int channel = 0; channel < 4; ++channel)
                refractionScratch_[out + size_t(channel)] = uint8_t(
                    std::clamp(std::lround(sample(sceneColor, width, height, sourceX, sourceY, channel)), 0l, 255l));
        }

    const std::span<const uint8_t> reduced(refractionScratch_);
    for (size_t i = 0; i < pixels; ++i) {
        const size_t rgba     = i * 4u;
        const float  coverage = float(color_[rgba + 3u]) / 255.f;
        if (coverage <= 0.f) {
            std::copy_n(sceneColor.data() + rgba, 4u, color_.data() + rgba);
            continue;
        }
        const int   x = int(i % size_t(width)), y = int(i / size_t(width));
        const float u  = (float(x) + .5f) / float(width) + normals_[i].x * thickness_[i] * refractionCoefficient_;
        const float v  = (float(y) + .5f) / float(height) - normals_[i].y * thickness_[i] * refractionCoefficient_;
        const float rx = u * float(targetWidth) - .5f;
        const float ry = v * float(targetHeight) - .5f;
        for (int channel = 0; channel < 3; ++channel) {
            const float liquid    = float(color_[rgba + size_t(channel)]) / 255.f;
            const float absorbed  = std::exp(-refractionAbsorption_ * (1.f - liquid) * std::max(thickness_[i], 0.f));
            const float refracted = sample(reduced, targetWidth, targetHeight, rx, ry, channel) * absorbed;
            const float fluid    = std::lerp(float(color_[rgba + size_t(channel)]), refracted, refractionTransparency_);
            const float composed = float(sceneColor[rgba + size_t(channel)]) * (1.f - coverage) + fluid * coverage;
            color_[rgba + size_t(channel)] = uint8_t(std::clamp(std::lround(composed), 0l, 255l));
        }
        color_[rgba + 3u] = 255u;
    }
    residentColorCurrent_ = false;
    return Result<void>::success();
}

void FluidSurfaceRenderer::renderVolumeInternal(const VolumeFluid& sim, bool colorOnly, float alpha) {
    EV_PROFILE_MODULE("fluids", "FluidSurfaceRenderer::renderVolume");
    residentColorCurrent_ = false;
    const auto  copied  = anisotropyEnabled_
                              ? sim.copyInterpolatedSurfaceRenderData(alpha, positions_, volumeColors_, particleRadii_,
                                                                      particleOrientations_)
                              : sim.copyInterpolatedRenderData(alpha, positions_, volumeColors_);
    const float spacing = copied.value();
    const float radius  = spacing * 0.9f;
    anisotropicFrame_   = anisotropyEnabled_ && !positions_.empty();
    if (anisotropicFrame_) buildAnisotropicSplats();
    const bool uniform           = !particleBlendConfigured_ && !volumeColors_.empty() &&
                                   std::all_of(volumeColors_.begin() + 1, volumeColors_.end(),
                                               [&](const glm::vec4& color) { return color == volumeColors_.front(); });
    uniformVolumeColor_          = uniform ? volumeColors_.front() : glm::vec4(0.f);
    const bool applyDownsampling = !colorOnly;
    uniformVolumeGpuShade_       = colorOnly && uniform && preferGpu_;
    multicolorVolumeGpuShade_    = colorOnly && !uniform && !particleBlendConfigured_ && preferGpu_;
    if (applyDownsampling && surfaceDownsample_ > 1) {
        ensureReducedRenderer();
        reducedRenderer_->positions_        = positions_;
        reducedRenderer_->anisotropicFrame_ = anisotropicFrame_;
        if (anisotropicFrame_) {
            reducedRenderer_->particleRadii_        = particleRadii_;
            reducedRenderer_->particleOrientations_ = particleOrientations_;
            reducedRenderer_->buildAnisotropicSplats();
        }
        reducedRenderer_->renderInternal(reducedRenderer_->positions_, radius);
        expandReducedOutputs(false);
    } else {
        renderInternal(positions_, radius);
    }
    if (applyDownsampling && thicknessDownsample_ != surfaceDownsample_ && !positions_.empty()) {
        ensureThicknessRenderer();
        thicknessRenderer_->positions_        = positions_;
        thicknessRenderer_->anisotropicFrame_ = anisotropicFrame_;
        if (anisotropicFrame_) {
            thicknessRenderer_->particleRadii_        = particleRadii_;
            thicknessRenderer_->particleOrientations_ = particleOrientations_;
            thicknessRenderer_->buildAnisotropicSplats();
        }
        thicknessRenderer_->renderInternal(thicknessRenderer_->positions_, radius);
        replaceThicknessFromReduced();
    }
    const bool gpuShadedVolume = (uniformVolumeGpuShade_ || multicolorVolumeGpuShade_) && gpuOk_;
    uniformVolumeGpuShade_     = false;
    multicolorVolumeGpuShade_  = false;
    if (gpuShadedVolume) auxiliaryCurrent_ = false;
    if (positions_.empty() || gpuShadedVolume) return;
    const int   width = params_.width, height = params_.height;
    const auto  view = glm::lookAtRH(params_.eye, params_.target, params_.up);
    const float projectionScale =
        params_.orthographic ? params_.orthographicSize : std::tan(glm::radians(params_.fovYDeg) * 0.5f);
    const float  aspect     = float(width) / float(height);
    const size_t pixelCount = size_t(width) * size_t(height);

    // Most liquid actors use one material color. Avoid re-splatting every particle
    // over the CPU image after the SSF pass in that common case: the reconstructed
    // depth already identifies the covered pixels, so tinting is linear in output
    // resolution instead of particle count times projected disc area.
    const glm::vec4 uniformColor = volumeColors_.front();
    if (uniform) {
        const auto lightDirection = glm::normalize(glm::vec3(0.35f, 0.65f, 0.55f));
        for (size_t i = 0; i < pixelCount; ++i) {
            if (color_[i * 4u + 3u] == 0 || thickness_[i] * 10.f < thicknessCutoff_) {
                color_[i * 4u + 0u] = color_[i * 4u + 1u] = color_[i * 4u + 2u] = color_[i * 4u + 3u] = 0;
                continue;
            }
            const auto  n    = normals_[i];
            const float diff = std::max(0.f, glm::dot(n, lightDirection));
            const float light =
                lighting_ ? std::clamp(ambientMultiplier_ + (1.f - std::min(ambientMultiplier_, 1.f)) * diff, 0.f, 6.f)
                          : 1.f;
            const float     exponent  = 4.f + 124.f * smoothness_;
            const float     spec      = lighting_ ? std::pow(std::max(n.z, 0.f), exponent) * smoothness_ * .56f : 0.f;
            const float     fresnel   = .04f + .96f * std::pow(1.f - std::max(n.z, 0.f), 5.f);
            const glm::vec3 reflected = glm::mix(reflectionColor_, glm::vec3(uniformColor), metalness_);
            const float     reflectionStrength = reflectionEnabled_ ? reflection_ : 0.f;
            const glm::vec3 shaded =
                glm::vec3(uniformColor) * light + reflected * fresnel * reflectionStrength + glm::vec3(spec);
            for (int c = 0; c < 3; ++c) color_[i * 4u + size_t(c)] = uint8_t(255.f * std::clamp(shaded[c], 0.f, 1.f));
            color_[i * 4u + 3u] = uint8_t(255.f * std::clamp(thickness_[i] * opacity_, 0.f, 1.f) * uniformColor.a);
        }
        return;
    }

    volumeTint_.assign(pixelCount, particleBlendConfigured_ ? glm::vec4(1.f) : glm::vec4(0.f));
    volumeTintWeights_.assign(pixelCount, 0.f);
    for (size_t particle = 0; particle < positions_.size(); ++particle) {
        const auto  v = view * glm::vec4(positions_[particle], 1.f);
        const float z = -v.z;
        if (z <= params_.nearZ || z >= params_.farZ) continue;
        const float divisor = params_.orthographic ? 1.f : z;
        const float cx      = (0.5f + 0.5f * v.x / (divisor * projectionScale * aspect)) * float(width);
        const float cy      = (0.5f - 0.5f * v.y / (divisor * projectionScale)) * float(height);
        const float r       = std::max(1.f, radius * float(height) / (2.f * divisor * projectionScale));
        const int   x0 = std::max(0, int(std::floor(cx - r))), x1 = std::min(width - 1, int(std::ceil(cx + r)));
        const int   y0 = std::max(0, int(std::floor(cy - r))), y1 = std::min(height - 1, int(std::ceil(cy + r)));
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                const float dx = (float(x) + 0.5f - cx) / r, dy = (float(y) + 0.5f - cy) / r;
                const float q = 1.f - dx * dx - dy * dy;
                if (q <= 0.f) continue;
                const size_t at            = size_t(y) * size_t(width) + size_t(x);
                const float  fragmentDepth = z - radius * std::sqrt(q);
                if (fragmentDepth > depth_[at] + radius) continue;
                if (particleBlendConfigured_) {
                    if (particleDepthWrite_ && volumeTintWeights_[at] > 0.f && fragmentDepth >= volumeTintWeights_[at])
                        continue;
                    const glm::vec4 source      = volumeColors_[particle];
                    const glm::vec4 destination = volumeTint_[at];
                    glm::vec4       blended =
                        glm::clamp(source * blendFluidColor(particleBlendSource_, source, destination) +
                                       destination * blendFluidColor(particleBlendDestination_, source, destination),
                                   glm::vec4(0.f), glm::vec4(1.f));
                    blended.a              = destination.a;  // Fluid3D's FluidColorsBlend pass uses ColorMask RGB.
                    volumeTint_[at]        = blended;
                    volumeTintWeights_[at] = particleDepthWrite_ ? fragmentDepth : 1.f;
                } else {
                    volumeTint_[at] += volumeColors_[particle] * q;
                    volumeTintWeights_[at] += q;
                }
            }
    }
    for (size_t i = 0; i < volumeTint_.size(); ++i) {
        if (volumeTintWeights_[i] <= 0.f || color_[i * 4u + 3u] == 0 || thickness_[i] * 10.f < thicknessCutoff_) {
            if (thickness_[i] * 10.f < thicknessCutoff_)
                color_[i * 4u + 0u] = color_[i * 4u + 1u] = color_[i * 4u + 2u] = color_[i * 4u + 3u] = 0;
            continue;
        }
        const auto  base = particleBlendConfigured_
                               ? volumeTint_[i]
                               : glm::clamp(volumeTint_[i] / volumeTintWeights_[i], glm::vec4(0.f), glm::vec4(1.f));
        const auto  n    = normals_[i];
        const float diff = std::max(0.f, glm::dot(n, glm::normalize(glm::vec3(.35f, .65f, .55f))));
        const float light =
            lighting_ ? std::clamp(ambientMultiplier_ + (1.f - std::min(ambientMultiplier_, 1.f)) * diff, 0.f, 6.f)
                      : 1.f;
        const float     exponent  = 4.f + 124.f * smoothness_;
        const float     spec      = lighting_ ? std::pow(std::max(n.z, 0.f), exponent) * smoothness_ * .56f : 0.f;
        const float     fresnel   = .04f + .96f * std::pow(1.f - std::max(n.z, 0.f), 5.f);
        const glm::vec3 reflected = glm::mix(reflectionColor_, glm::vec3(base), metalness_);
        const float     reflectionStrength = reflectionEnabled_ ? reflection_ : 0.f;
        const glm::vec3 shaded = glm::vec3(base) * light + reflected * fresnel * reflectionStrength + glm::vec3(spec);
        for (int c = 0; c < 3; ++c) color_[i * 4u + size_t(c)] = uint8_t(255.f * std::clamp(shaded[c], 0.f, 1.f));
        color_[i * 4u + 3u] = uint8_t(255.f * std::clamp(thickness_[i] * opacity_, 0.f, 1.f) * base.a);
    }
}

Result<void> FluidSurfaceRenderer::renderGasVolume(const VolumeFluid& sim, float absorption) {
    return renderVolumeCloud(sim, absorption, true);
}

Result<void> FluidSurfaceRenderer::renderVolumeWithoutSurface(const VolumeFluid& sim, float absorption) {
    return renderVolumeCloud(sim, absorption, false);
}

Result<void> FluidSurfaceRenderer::renderConfiguredVolume(const VolumeFluid& sim) {
    if (!surfaceEnabled_) return renderVolumeWithoutSurface(sim, refractionAbsorption_);
    renderVolumeInternal(sim, false, 1.f);
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::renderVolumeCloud(const VolumeFluid& sim, float absorption, bool gasOnly) {
    residentColorCurrent_ = false;
    if (!std::isfinite(absorption) || absorption < .01f || absorption > 30.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Volume absorption must be finite and within [0.01,30]",
                                                       "fluids.surface.volume-cloud"));

    const auto particles     = sim.particleView();
    size_t     selectedCount = particles.size();
    if (gasOnly)
        selectedCount =
            size_t(std::count_if(particles.begin(), particles.end(), [](const VolumeFluidParticle& particle) {
                return particle.material.phase == VolumeFluidPhase::Gas;
            }));
    if (selectedCount > kMaxGasParticles)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "Volume frame exceeds the 65536-particle render budget",
                                                       "fluids.surface.volume-cloud"));
    positions_.clear();
    volumeColors_.clear();
    positions_.reserve(selectedCount);
    volumeColors_.reserve(selectedCount);
    for (const auto& particle : particles) {
        if (gasOnly && particle.material.phase != VolumeFluidPhase::Gas) continue;
        positions_.push_back(particle.position);
        volumeColors_.push_back(particle.color);
    }

    const int width = params_.width, height = params_.height;
    // Fluid3D permits thicknessDownsample up to 4. Use that performance-safe level
    // for the CPU reference, then expand once into the public output buffer.
    const int    renderWidth      = std::max(8, (width + 3) / 4);
    const int    renderHeight     = std::max(8, (height + 3) / 4);
    const size_t renderPixelCount = size_t(renderWidth) * size_t(renderHeight);
    const size_t pixelCount       = size_t(width) * size_t(height);
    const float  radius           = sim.spacing() * .9f;
    const float  projectionScale =
        params_.orthographic ? params_.orthographicSize : std::tan(glm::radians(params_.fovYDeg) * .5f);
    const float aspect = float(renderWidth) / float(renderHeight);
    const auto  view   = glm::lookAtRH(params_.eye, params_.target, params_.up);
    gasSplats_.clear();
    gasSplats_.reserve(positions_.size());
    uint64_t coveredPixels = 0;
    for (const auto& position : positions_) {
        const auto  v = view * glm::vec4(position, 1.f);
        const float z = -v.z;
        if (z <= params_.nearZ || z >= params_.farZ) {
            gasSplats_.push_back({});
            continue;
        }
        GasSplat splat;
        splat.z             = z;
        const float divisor = params_.orthographic ? 1.f : z;
        splat.cx            = (.5f + .5f * v.x / (divisor * projectionScale * aspect)) * float(renderWidth);
        splat.cy            = (.5f - .5f * v.y / (divisor * projectionScale)) * float(renderHeight);
        splat.r             = std::max(1.f, radius * float(renderHeight) / (2.f * divisor * projectionScale));
        splat.x0            = std::max(0, int(std::floor(splat.cx - splat.r)));
        splat.x1            = std::min(renderWidth - 1, int(std::ceil(splat.cx + splat.r)));
        splat.y0            = std::max(0, int(std::floor(splat.cy - splat.r)));
        splat.y1            = std::min(renderHeight - 1, int(std::ceil(splat.cy + splat.r)));
        if (splat.x1 >= splat.x0 && splat.y1 >= splat.y0)
            coveredPixels += uint64_t(splat.x1 - splat.x0 + 1) * uint64_t(splat.y1 - splat.y0 + 1);
        if (coveredPixels > kMaxGasCoveredPixels)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                           "Volume frame exceeds the four-million-pixel splat budget",
                                                           "fluids.surface.volume-cloud"));
        gasSplats_.push_back(splat);
    }

    depthScratch_.assign(renderPixelCount, 1e30f);
    gasDensityScratch_.assign(renderPixelCount, 0.f);
    gasTintScratch_.assign(renderPixelCount, glm::vec4(0.f));
    for (size_t particle = 0; particle < gasSplats_.size(); ++particle) {
        const auto& splat = gasSplats_[particle];
        if (splat.x1 < splat.x0 || splat.y1 < splat.y0) continue;
        const float r2 = splat.r * splat.r;
        for (int y = splat.y0; y <= splat.y1; ++y)
            for (int x = splat.x0; x <= splat.x1; ++x) {
                const float dx = float(x) + .5f - splat.cx, dy = float(y) + .5f - splat.cy;
                const float q = 1.f - (dx * dx + dy * dy) / r2;
                if (q <= 0.f) continue;
                const size_t at    = size_t(y) * size_t(renderWidth) + size_t(x);
                const float  chord = 2.f * radius * std::sqrt(q) * params_.thicknessScale;
                gasDensityScratch_[at] += chord;
                gasTintScratch_[at] += volumeColors_[particle] * chord;
                depthScratch_[at] = std::min(depthScratch_[at], splat.z - radius * std::sqrt(q));
            }
    }

    thickness_.resize(renderPixelCount);
    volumeTint_.resize(renderPixelCount);
    auto blur = [&](bool horizontal, const std::vector<float>& srcDensity, const std::vector<glm::vec4>& srcTint,
                    std::vector<float>& dstDensity, std::vector<glm::vec4>& dstTint) {
        for (int y = 0; y < renderHeight; ++y)
            for (int x = 0; x < renderWidth; ++x) {
                float     density = 0.f;
                glm::vec4 tint(0.f);
                for (int tap = -3; tap <= 3; ++tap) {
                    const int    sx     = std::clamp(x + (horizontal ? tap : 0), 0, renderWidth - 1);
                    const int    sy     = std::clamp(y + (horizontal ? 0 : tap), 0, renderHeight - 1);
                    const size_t at     = size_t(sy) * size_t(renderWidth) + size_t(sx);
                    const float  weight = kBlurWeights[tap + 3];
                    density += srcDensity[at] * weight;
                    tint += srcTint[at] * weight;
                }
                const size_t at = size_t(y) * size_t(renderWidth) + size_t(x);
                dstDensity[at]  = density;
                dstTint[at]     = tint;
            }
    };
    blur(true, gasDensityScratch_, gasTintScratch_, thickness_, volumeTint_);
    blur(false, thickness_, volumeTint_, gasDensityScratch_, gasTintScratch_);

    depth_.resize(pixelCount);
    normals_.assign(pixelCount, glm::vec3(0.f));
    thickness_.resize(pixelCount);
    volumeTint_.resize(pixelCount);
    color_.resize(pixelCount * 4u);
    for (size_t i = 0; i < pixelCount; ++i) {
        const int    x = int(i % size_t(width)), y = int(i / size_t(width));
        const int    sx     = std::min(renderWidth - 1, x * renderWidth / width);
        const int    sy     = std::min(renderHeight - 1, y * renderHeight / height);
        const size_t source = size_t(sy) * size_t(renderWidth) + size_t(sx);
        depth_[i]           = depthScratch_[source];
        thickness_[i]       = gasDensityScratch_[source];
        volumeTint_[i]      = gasTintScratch_[source];
        const float density = thickness_[i];
        if (density <= 1e-7f) {
            color_[i * 4u + 0] = color_[i * 4u + 1] = color_[i * 4u + 2] = color_[i * 4u + 3] = 0;
            continue;
        }
        const glm::vec4 tint  = glm::clamp(volumeTint_[i] / density, glm::vec4(0.f), glm::vec4(1.f));
        const float     alpha = (1.f - std::exp(-absorption * density)) * tint.a;
        const float     glow  = .72f + .28f * std::clamp(density * absorption, 0.f, 1.f);
        for (size_t channel = 0; channel < 3; ++channel)
            color_[i * 4u + channel] = uint8_t(255.f * std::clamp(tint[channel] * glow, 0.f, 1.f));
        color_[i * 4u + 3] = uint8_t(255.f * std::clamp(alpha, 0.f, 1.f));
    }
    auxiliaryCurrent_ = true;
    return Result<void>::success();
}
}  // namespace eve::fluids
