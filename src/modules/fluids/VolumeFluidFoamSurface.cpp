#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include "fluids/FluidSurfaceRenderer.h"
#include "fluids/VolumeFluidDiffuse.h"

namespace eve::fluids {
Result<void> FluidSurfaceRenderer::configureFoam(bool enabled, int downsample) {
    if (downsample < 1 || downsample > 4)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Foam downsample must be in [1,4]", "fluids.surface.configureFoam"));
    foamEnabled_    = enabled;
    foamDownsample_ = downsample;
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::compositeDiffuse(const VolumeFluidDiffuse& pool, float radius, float opacity,
                                                    float fadeSeconds) {
    const auto fail = [](const char* message) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.surface.diffuse"));
    };
    if (!std::isfinite(radius) || radius <= 0.f || radius > 1.f || !std::isfinite(opacity) || opacity < 0.f ||
        opacity > 1.f || !std::isfinite(fadeSeconds) || fadeSeconds <= 0.f || fadeSeconds > 86400.f)
        return fail("Invalid diffuse rendering settings");
    if (!foamEnabled_ || pool.particleCount() == 0 || opacity == 0.f) return Result<void>::success();
    const int width = params_.width, height = params_.height;
    if (width <= 0 || height <= 0 || depth_.size() != size_t(width) * size_t(height) ||
        color_.size() != depth_.size() * 4)
        return fail("Invalid reconstructed frame size");
    const int   renderWidth  = std::max(1, (width + foamDownsample_ - 1) / foamDownsample_);
    const int   renderHeight = std::max(1, (height + foamDownsample_ - 1) / foamDownsample_);
    const float tangent      = std::tan(glm::radians(params_.fovYDeg) * .5f);
    if (!std::isfinite(tangent) || tangent <= 0.f) return fail("Invalid diffuse camera projection");
    const auto view = glm::lookAtRH(params_.eye, params_.target, params_.up);
    pool.copyRenderData(diffuseParticles_);
    diffuseSplats_.clear();
    diffuseSplats_.reserve(diffuseParticles_.size());
    uint64_t visits = 0;
    for (const auto p : diffuseParticles_) {
        const auto v = view * glm::vec4(glm::vec3(p), 1.f);
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
            return fail("Invalid diffuse camera transform");
        DiffuseSplat s;
        s.z = -v.z;
        if (s.z <= params_.nearZ || s.z >= params_.farZ) continue;
        s.r = std::max(1.f, radius * float(renderHeight) / (2.f * s.z * tangent));
        s.x = (.5f + .5f * v.x / (s.z * tangent * float(renderWidth) / float(renderHeight))) * float(renderWidth);
        s.y = (.5f - .5f * v.y / (s.z * tangent)) * float(renderHeight);
        if (!std::isfinite(s.r) || !std::isfinite(s.x) || !std::isfinite(s.y))
            return fail("Invalid diffuse projection");
        if (s.x + s.r < 0.f || s.y + s.r < 0.f || s.x - s.r >= float(renderWidth) || s.y - s.r >= float(renderHeight))
            continue;
        // Clamp in floating point before converting extreme off-screen coordinates.
        s.x0    = int(std::floor(std::clamp(s.x - s.r, 0.f, float(renderWidth - 1))));
        s.x1    = int(std::ceil(std::clamp(s.x + s.r, 0.f, float(renderWidth - 1))));
        s.y0    = int(std::floor(std::clamp(s.y - s.r, 0.f, float(renderHeight - 1))));
        s.y1    = int(std::ceil(std::clamp(s.y + s.r, 0.f, float(renderHeight - 1))));
        s.alpha = opacity * std::min(1.f, p.w / fadeSeconds);
        visits += uint64_t(s.x1 - s.x0 + 1) * uint64_t(s.y1 - s.y0 + 1);
        if (visits > 4000000) return fail("Diffuse rendering exceeds 4M pixel visits");
        diffuseSplats_.push_back(s);
    }
    if (foamDownsample_ > 1) foamCoverageScratch_.assign(size_t(renderWidth) * size_t(renderHeight), 0.f);
    for (const auto& s : diffuseSplats_) {
        for (int y = s.y0; y <= s.y1; ++y)
            for (int x = s.x0; x <= s.x1; ++x) {
                const float dx = (float(x) + .5f - s.x) / s.r, dy = (float(y) + .5f - s.y) / s.r;
                const float q = 1.f - dx * dx - dy * dy;
                if (q <= 0.f) continue;
                const size_t reducedAt  = size_t(y) * size_t(renderWidth) + size_t(x);
                const int    fullX      = std::min(width - 1, x * width / renderWidth);
                const int    fullY      = std::min(height - 1, y * height / renderHeight);
                const size_t at         = size_t(fullY) * size_t(width) + size_t(fullX);
                const float  front      = s.z - radius * std::sqrt(q);
                const float  visibility = std::clamp(1.f + (depth_[at] - front) / radius, 0.f, 1.f);
                const float  alpha      = s.alpha * q * q * visibility;
                if (alpha <= 0.f) continue;
                if (foamDownsample_ > 1) {
                    foamCoverageScratch_[reducedAt] = alpha + foamCoverageScratch_[reducedAt] * (1.f - alpha);
                    continue;
                }
                const float oldAlpha = float(color_[at * 4 + 3]) / 255.f;
                const float retained = oldAlpha * (1.f - alpha), combined = alpha + retained;
                for (size_t c = 0; c < 3; ++c) {
                    const float color  = (alpha + float(color_[at * 4 + c]) / 255.f * retained) / combined;
                    color_[at * 4 + c] = uint8_t(std::clamp(color, 0.f, 1.f) * 255.f + .5f);
                }
                color_[at * 4 + 3] = uint8_t(std::clamp(combined, 0.f, 1.f) * 255.f + .5f);
            }
    }
    if (foamDownsample_ > 1) {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const int   sx    = std::min(renderWidth - 1, x * renderWidth / width);
                const int   sy    = std::min(renderHeight - 1, y * renderHeight / height);
                const float alpha = foamCoverageScratch_[size_t(sy) * size_t(renderWidth) + size_t(sx)];
                if (alpha <= 0.f) continue;
                const size_t at       = size_t(y) * size_t(width) + size_t(x);
                const float  oldAlpha = float(color_[at * 4u + 3u]) / 255.f;
                const float  retained = oldAlpha * (1.f - alpha), combined = alpha + retained;
                for (size_t c = 0; c < 3; ++c) {
                    const float value   = (alpha + float(color_[at * 4u + c]) / 255.f * retained) / combined;
                    color_[at * 4u + c] = uint8_t(std::clamp(value, 0.f, 1.f) * 255.f + .5f);
                }
                color_[at * 4u + 3u] = uint8_t(std::clamp(combined, 0.f, 1.f) * 255.f + .5f);
            }
    }
    residentColorCurrent_ = false;
    return Result<void>::success();
}
}  // namespace eve::fluids
