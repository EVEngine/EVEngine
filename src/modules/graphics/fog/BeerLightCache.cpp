#include "graphics/fog/BeerLightCache.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>

namespace eve::graphics::fog {

void BeerLightCache::invalidate() {
    valid_ = false;
    ++key_.version;
}

bool BeerLightCache::matches(std::uint64_t densityRevision, const glm::vec3& lightDir,
                             float intensity) const noexcept {
    if (!valid_) return false;
    if (key_.densityRevision != densityRevision) return false;
    if (std::fabs(key_.intensity - intensity) > 1e-5f) return false;
    const glm::vec3 d = key_.direction - lightDir;
    return glm::dot(d, d) < 1e-6f;
}

Result<void> BeerLightCache::rebuild(const FogDensityField& field, const FogProfile& profile,
                                     const glm::vec3& lightDir, const glm::vec3& lightColor,
                                     float intensity, int samplesPerCell) {
    if (field.width() < 2) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                       "density field is empty", "rebuild", {},
                                                       "graphics.fog"));
    }
    if (!std::isfinite(intensity) || intensity < 0.f || samplesPerCell < 1 || samplesPerCell > 64) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "intensity/samples out of range", "rebuild", {},
            "graphics.fog"));
    }
    const float len = glm::length(lightDir);
    if (!(len > 1e-6f) || !std::isfinite(len)) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "light direction must be non-zero and finite",
            "lightDir", {}, "graphics.fog"));
    }
    const glm::vec3 dir = lightDir / len;

    width_ = field.width();
    height_ = field.height();
    depth_ = field.depth();
    transmittance_.assign(static_cast<std::size_t>(width_) * height_ * depth_, 1.f);

    const glm::vec3 cell = field.cellSize();
    const float stepLen = 0.5f * std::min({cell.x, cell.y, cell.z});
    const FogWorldBounds& bounds = field.bounds();

    for (int z = 0; z < depth_; ++z) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                glm::vec3 p = bounds.minimum + glm::vec3((x + 0.5f) * cell.x, (y + 0.5f) * cell.y,
                                                         (z + 0.5f) * cell.z);
                float optical = 0.f;
                for (int s = 0; s < samplesPerCell; ++s) {
                    // March toward the light (opposite the "direction toward the surface").
                p -= dir * stepLen;
                    if (!bounds.contains(p)) break;
                    optical += profile.extinctionAt(field.sampleDensity(p)) * stepLen;
                }
                const std::size_t idx =
                    (static_cast<std::size_t>(z) * height_ + y) * width_ + static_cast<std::size_t>(x);
                transmittance_[idx] = std::exp(-optical);
            }
        }
    }

    key_.direction = dir;
    key_.color = lightColor;
    key_.intensity = intensity;
    key_.densityRevision = field.revision();
    ++key_.version;
    valid_ = true;
    return Result<void>::success();
}

float BeerLightCache::sampleTransmittance(const FogDensityField& field,
                                          const glm::vec3& world) const noexcept {
    if (!valid_ || transmittance_.empty()) return 1.f;
    const FogWorldBounds& bounds = field.bounds();
    const glm::vec3 cell = field.cellSize();
    const glm::vec3 local = (world - bounds.minimum) / cell;
    const int x = std::clamp(static_cast<int>(local.x), 0, width_ - 1);
    const int y = std::clamp(static_cast<int>(local.y), 0, height_ - 1);
    const int z = std::clamp(static_cast<int>(local.z), 0, depth_ - 1);
    const std::size_t idx =
        (static_cast<std::size_t>(z) * height_ + y) * width_ + static_cast<std::size_t>(x);
    return transmittance_[idx];
}

}  // namespace eve::graphics::fog
