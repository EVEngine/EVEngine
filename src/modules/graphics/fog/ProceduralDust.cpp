#include "graphics/fog/ProceduralDust.h"

#include "common/Diagnostic.h"

#include <cmath>

#include <glm/common.hpp>

namespace eve::graphics::fog {
namespace {

[[nodiscard]] float hash11(std::uint32_t n) noexcept {
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return static_cast<float>(n & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

[[nodiscard]] std::uint32_t mix(std::uint32_t a, std::uint32_t b) noexcept {
    return a * 747796405u + b * 2891336453u;
}

}  // namespace

Result<std::vector<DustParticle>> ProceduralDust::generate(const FogWorldBounds& volume, int count,
                                                           float timeSeconds, float turbulenceMeters,
                                                           std::uint32_t seed) const {
    if (count < 1 || count > 4096) {
        return Result<std::vector<DustParticle>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "dust count must be in [1,4096]", "count", {},
            "graphics.fog"));
    }
    if (!std::isfinite(timeSeconds) || !std::isfinite(turbulenceMeters) || turbulenceMeters < 0.f) {
        return Result<std::vector<DustParticle>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "time/turbulence must be finite; turbulence >= 0",
            "turbulence", {}, "graphics.fog"));
    }
    const glm::vec3 size = volume.size();
    if (!(size.x > 0.f && size.y > 0.f && size.z > 0.f)) {
        return Result<std::vector<DustParticle>>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "volume bounds must have positive extent", "volume", {},
            "graphics.fog"));
    }

    std::vector<DustParticle> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::uint32_t h0 = mix(seed, static_cast<std::uint32_t>(i) * 3u + 1u);
        const std::uint32_t h1 = mix(seed, static_cast<std::uint32_t>(i) * 3u + 2u);
        const std::uint32_t h2 = mix(seed, static_cast<std::uint32_t>(i) * 3u + 3u);
        const float ux = hash11(h0);
        const float uy = hash11(h1);
        const float uz = hash11(h2);

        DustParticle p;
        p.band = (i & 1) ? DustParticle::Band::Mid : DustParticle::Band::Fine;
        p.position = volume.minimum + glm::vec3(ux, uy, uz) * size;

        // Continuous, camera-independent turbulence — no persistent buffer.
        const float phase = timeSeconds * (p.band == DustParticle::Band::Fine ? 0.7f : 0.35f);
        const float amp =
            turbulenceMeters * (p.band == DustParticle::Band::Fine ? 0.55f : 1.f);
        p.position += glm::vec3(amp * std::sin(phase + ux * 6.2831853f),
                                amp * 0.45f * std::sin(phase * 1.3f + uy * 6.2831853f),
                                amp * std::cos(phase * 0.9f + uz * 6.2831853f));
        p.position = glm::clamp(p.position, volume.minimum, volume.maximum);
        p.size = p.band == DustParticle::Band::Fine ? 0.015f : 0.04f;
        p.brightness = 0.55f + 0.45f * hash11(mix(h0, h2));
        out.push_back(p);
    }
    return Result<std::vector<DustParticle>>::success(std::move(out));
}

}  // namespace eve::graphics::fog
