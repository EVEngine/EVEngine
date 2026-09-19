#pragma once
#include "common/Export.h"


#include "common/Result.h"

#include <cstdint>

namespace eve::weather {

/** @brief Pcg ThunderStrike parameters independent of renderer and audio providers. */
struct ThunderStrikeSettings {
    float intensity = 1.f;
    float radius = 300.f;
    float volume = 1.f;
    int audioClipCount = 0;
};

/** @brief Caller-owned thunder flash state. */
struct ThunderStrikeState {
    bool playing = false;
    float intensity = 0.f;
};

/** @brief Deterministic strike command consumed by scene, light and audio providers. */
struct ThunderStrikeReceipt {
    float x = 0.f, y = 0.f, z = 0.f;
    float intensity = 0.f, radius = 0.f, volume = 0.f;
    int audioClipIndex = -1;
};

/**
 * @brief Start a Pcg-style strike around the supplied player position.
 * @param state Caller-owned state; an active strike returns Conflict without mutation.
 * @param settings Finite non-negative light/audio parameters and available clip count.
 * @param playerX Player world position.
 * @param playerY Player world position.
 * @param playerZ Player world position.
 * @param seed Explicit seed for the named thunder-strike RNG stream.
 * @return Position/light/audio command. Pcg deliberately excludes audio clip zero when two or more clips exist.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<ThunderStrikeReceipt> triggerThunderStrike(ThunderStrikeState& state,
    const ThunderStrikeSettings& settings, float playerX, float playerY, float playerZ, std::uint32_t seed);

/** @brief Apply Pcg's clamped Lerp(intensity,0,dt*2) decay and 0.15 stop threshold. */
[[nodiscard]] EVENGINE_API_WORLD Result<void> advanceThunderStrike(ThunderStrikeState& state, float dt);

}  // namespace eve::weather
