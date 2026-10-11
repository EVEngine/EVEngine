#include "graphics/fog/FogProfile.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>

namespace eve::graphics::fog {
namespace {

[[nodiscard]] bool finiteVec3(const glm::vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] float henyeyGreenstein(float g, float cosTheta) noexcept {
    const float g2 = g * g;
    const float denom = 1.f + g2 - 2.f * g * cosTheta;
    const float denom32 = denom * std::sqrt(std::max(denom, 1e-8f));
    return (1.f - g2) / (4.f * 3.14159265f * std::max(denom32, 1e-8f));
}

}  // namespace

Result<void> FogProfile::configure(float extinctionPerDensity, const glm::vec3& albedo,
                                   float ambientOcclusion, float ambientUp, float ambientDown,
                                   float anisotropy) {
    if (!std::isfinite(extinctionPerDensity) || extinctionPerDensity < 0.f) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "extinctionPerDensity must be finite and >= 0",
            "extinctionPerDensity", {}, "graphics.fog"));
    }
    if (!finiteVec3(albedo) || albedo.x < 0.f || albedo.y < 0.f || albedo.z < 0.f || albedo.x > 1.f ||
        albedo.y > 1.f || albedo.z > 1.f) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "albedo components must be finite and in [0,1]", "albedo",
            {}, "graphics.fog"));
    }
    auto in01 = [](float v) { return std::isfinite(v) && v >= 0.f && v <= 1.f; };
    if (!in01(ambientOcclusion) || !in01(ambientUp) || !in01(ambientDown)) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "ambient terms must be finite and in [0,1]", "ambient", {},
            "graphics.fog"));
    }
    if (!std::isfinite(anisotropy) || anisotropy < -0.99f || anisotropy > 0.99f) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "anisotropy must be finite and in [-0.99,0.99]",
            "anisotropy", {}, "graphics.fog"));
    }

    extinctionPerDensity_ = extinctionPerDensity;
    albedo_ = albedo;
    ambientOcclusion_ = ambientOcclusion;
    ambientUp_ = ambientUp;
    ambientDown_ = ambientDown;
    anisotropy_ = anisotropy;
    return Result<void>::success();
}

float FogProfile::extinctionAt(float density) const noexcept {
    return extinctionPerDensity_ * std::max(density, 0.f);
}

glm::vec3 FogProfile::scatteringAt(float density) const noexcept {
    return albedo_ * extinctionAt(density);
}

float FogProfile::phase(float cosTheta) const noexcept {
    const float c = std::clamp(cosTheta, -1.f, 1.f);
    const float g = anisotropy_;
    // Dual-lobe: forward HG + mild isotropic back lobe for ambient fill.
    return 0.85f * henyeyGreenstein(g, c) + 0.15f * henyeyGreenstein(-0.25f * g, c);
}

glm::vec3 FogProfile::ambientRadiance(const glm::vec3& skyUp, const glm::vec3& groundDown,
                                      float density) const noexcept {
    const float ao = 1.f - ambientOcclusion_ * std::clamp(density, 0.f, 1.f);
    return ao * (ambientUp_ * skyUp + ambientDown_ * groundDown);
}

}  // namespace eve::graphics::fog
