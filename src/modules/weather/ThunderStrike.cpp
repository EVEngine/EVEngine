#include "weather/ThunderStrike.h"

#include <algorithm>
#include <cmath>

namespace eve::weather {
namespace {
std::uint32_t next(std::uint32_t& state) { state = state * 1664525u + 1013904223u; return state; }
float unit(std::uint32_t& state) { return float(next(state) >> 8u) * (1.f / 16777216.f); }
float range(std::uint32_t& state, float a, float b) { return a + (b - a) * unit(state); }
template<class T> Result<T> fail(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "weather.thunderStrike"));
}
}

Result<ThunderStrikeReceipt> triggerThunderStrike(ThunderStrikeState& state,
    const ThunderStrikeSettings& settings, float px, float py, float pz, std::uint32_t seed) {
    if (state.playing) return fail<ThunderStrikeReceipt>(DiagnosticCode::Conflict, "thunder strike is already playing");
    if (!std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz) ||
        !std::isfinite(settings.intensity) || !std::isfinite(settings.radius) ||
        !std::isfinite(settings.volume) || settings.intensity < 0.f || settings.radius < 0.f ||
        settings.volume < 0.f || settings.audioClipCount < 0)
        return fail<ThunderStrikeReceipt>(DiagnosticCode::InvalidArgument, "thunder strike inputs must be finite and non-negative");
    std::uint32_t rng = seed;
    const bool positive = unit(rng) > 0.5f;
    ThunderStrikeReceipt receipt;
    receipt.x = range(rng, px, px + (positive ? 700.f : -700.f));
    receipt.y = range(rng, py + 50.f, py + 250.f);
    receipt.z = range(rng, pz, pz + (positive ? 700.f : -700.f));
    receipt.intensity = settings.intensity; receipt.radius = settings.radius; receipt.volume = settings.volume;
    if (settings.audioClipCount > 1)
        receipt.audioClipIndex = 1 + int(next(rng) % std::uint32_t(settings.audioClipCount - 1));
    state.playing = true;
    state.intensity = settings.intensity;
    return Result<ThunderStrikeReceipt>::success(receipt);
}

Result<void> advanceThunderStrike(ThunderStrikeState& state, float dt) {
    if (!std::isfinite(dt) || dt < 0.f)
        return fail<void>(DiagnosticCode::InvalidArgument, "thunder strike dt must be finite and non-negative");
    if (!state.playing) return Result<void>::success();
    state.intensity += (0.f - state.intensity) * std::clamp(dt * 2.f, 0.f, 1.f);
    if (state.intensity < 0.15f) { state.intensity = 0.f; state.playing = false; }
    return Result<void>::success();
}
}  // namespace eve::weather
