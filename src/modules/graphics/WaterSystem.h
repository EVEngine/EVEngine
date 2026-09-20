#pragma once

#include "common/Result.h"

#include <cstdint>

#include <glm/glm.hpp>

namespace ssq {
class Table;
}

namespace eve::graphics {

/** @brief Reflection refresh policy used by the caller-owned water controller. */
enum class WaterAutoUpdateMode : std::uint8_t { Interval = 0, SceneConditions = 1 };

/** @brief Immutable scene inputs used to decide whether water reflections are stale. */
struct WaterSceneConditions {
    glm::vec3 sunColor{1.0F};
    glm::vec3 sunDirection{0.0F};
    float     sunIntensity = 1.0F;
    int       hour = 12;
    int       minute = 0;
    bool      sunAvailable = true;
    bool      timeAvailable = false;
};

/** @brief Authored policy for Pcg-compatible water placement and reflection refresh. */
struct WaterSystemSettings {
    WaterAutoUpdateMode autoUpdateMode = WaterAutoUpdateMode::Interval;
    float refreshRate = 0.5F;
    bool  infiniteMode = true;
    bool  autoRefresh = true;
    bool  ignoreSceneConditions = true;
};

/** @brief Caller-owned water-system state; contains no scene pointers or hidden clock. */
struct WaterSystemState {
    WaterSceneConditions observed{};
    float seaLevel = 25.0F;
    float positionX = 0.0F;
    float positionY = 25.0F;
    float positionZ = 0.0F;
    float refreshRemaining = 0.5F;
    int   sceneCheckFrames = 0;
    std::uint64_t refreshRevision = 0;
    bool initialized = false;
};

/**
 * @brief Initialize a water controller from an authored policy and current scene snapshot.
 * @param state Exclusively borrowed state, published only after complete validation.
 * @param settings Immutable policy. refreshRate must be finite and positive.
 * @param scene Immutable finite scene snapshot; hour is 0..23 and minute is 0..59 when time is available.
 * @param initialSceneCheckFrames Nonnegative injected delay before SceneConditions evaluation.
 * @return Success or InvalidArgument; failure preserves state.
 * @thread Synchronous caller-owned access. Retains no pointer and invokes no callback.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> initializeWaterSystem(WaterSystemState& state, const WaterSystemSettings& settings,
                                                 const WaterSceneConditions& scene, int initialSceneCheckFrames);

/**
 * @brief Advance infinite placement and Pcg-compatible reflection refresh scheduling.
 * @param state Exclusively borrowed initialized state.
 * @param settings Immutable current policy.
 * @param scene Current immutable scene snapshot.
 * @param playerX Finite player/camera world X used only in infinite mode.
 * @param playerZ Finite player/camera world Z used only in infinite mode.
 * @param dt Finite nonnegative caller-provided simulation delta.
 * @param nextSceneCheckFrames Positive injected replacement for Pcg's random 5..14-frame delay after refresh.
 * @return Whether this step requests reflection regeneration. Failure preserves state.
 * @thread Synchronous caller-owned access. Deterministic for the supplied inputs and injected delay.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<bool> advanceWaterSystem(WaterSystemState& state, const WaterSystemSettings& settings,
                                               const WaterSceneConditions& scene, float playerX, float playerZ,
                                               float dt, int nextSceneCheckFrames);

/**
 * @brief Atomically move the water surface to a new sea level.
 * @param state Exclusively borrowed initialized state.
 * @param seaLevel Finite world-space Y value.
 * @param regenerateReflections Whether a changed level increments refreshRevision.
 * @return Whether the sea level changed. Failure preserves state.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<bool> updateWaterSeaLevel(WaterSystemState& state, float seaLevel,
                                               bool regenerateReflections);

/** @brief Register water-system value types and checked operations with the VM owner thread. */
void exposeWaterSystemBindings(ssq::Table& table);

}  // namespace eve::graphics
