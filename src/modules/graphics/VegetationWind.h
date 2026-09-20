#pragma once
#include <array>
#include <glm/glm.hpp>
#include "common/Result.h"
namespace eve::graphics {
/** @brief Caller-owned wind globals; no hidden clock, singleton or retained scene link. */
struct VegetationWindState {
    glm::vec3 direction{0};
    float     strength = 0;
    /** @brief Phase currently published to shaders. */
    float phase = 0;
    /** @brief WindManager's private update accumulator; InstantWindApply deliberately preserves it. */
    float updatePhase = 0;
};

/** @brief Caller-owned state for Pcg WindManager's looping wind-audio volume controller. */
struct VegetationWindAudioState {
    float currentWindSpeed = 0;
    float anchorVolume = 0;
    float volume = 0;
    bool  processing = false;
    bool  playing = false;
};

/**
 * @brief Advance Pcg WindManager CheckWindVolume and ProcessWindAudio as a pure state transition.
 * @param state Exclusively borrowed audio controller state, published only after validation.
 * @param windStrength Finite WindZone main value; the audio target is clamped to [0,1].
 * @param transitionTime Finite positive source transition duration.
 * @param dt Finite nonnegative explicit frame delta.
 * @param enabled False skips both source checks exactly like m_useWindAudio.
 * @param clipAvailable False retains processing state but skips playback and volume mutation.
 * @return Success or InvalidArgument; failure preserves state.
 * @thread Synchronous caller-owned access; no audio object, callback, clock or reference is retained.
 * The source's currentWindSpeed is intentionally not updated: a nonzero WindZone re-arms processing each frame.
 * Lerp clamps dt/transitionTime. Increasing volume snaps within 0.05 below target; the source's asymmetric
 * decreasing comparison is retained and generally approaches zero without snapping.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> advanceVegetationWindAudio(VegetationWindAudioState& state, float windStrength,
                                                       float transitionTime, float dt, bool enabled,
                                                       bool clipAvailable);
/** @brief Caller-owned material wind controls; billboard suppresses source branch and leaf motion.
 * AlphaTest
 * controls leaf motion only, independently of the renderer's alpha discard.
 */
struct VegetationWindProfile {
    glm::vec3 flex{0.8F, 1.15F, 0.1F}, frequency{0.25F, 0.5F, 1.3F};
    float     maximumDistance = 100;
    bool      enabled = false, billboard = false, alphaTest = true;
};
/** @brief Pack a wind snapshot into the grass shader's final 14 float slots.
 * @param state Borrowed immutable direction, strength and phase.
 * @param profile Borrowed material controls; distance must be positive even when disabled.
 * @param seconds Finite explicit time used for full and quarter-frequency sine values.
 * @return Complete packet or InvalidArgument, without mutation or GPU access.
 * @ownership Retains no references. Caller supplies immutable snapshots on its owner thread.
 * Layout is direction, strength, phase, signed distance, flex, frequency and two time sines.
 * Zero distance disables wind. Negative distance selects billboard motion. No alphaTest zeros leaf flex.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<std::array<float, 14>> packVegetationWind(const VegetationWindState&   state,
                                                               const VegetationWindProfile& profile, double seconds);
/** @brief Explicit source PW_GeneralWind inputs in object/world coordinates. */
struct VegetationWindInput {
    glm::vec3 position{0}, worldOffset{0}, cameraPosition{0};
    glm::mat3 objectToWorld{1};
    glm::vec2 widthHeight{1};
    glm::vec3 flex{0.8F, 1.15F, 0.1F}, frequency{0.25F, 0.5F, 1.3F};
    float     maximumDistance = 100;
    /** @brief Inject Unity-equivalent sin(time/4) and sin(time) values. */
    float sinTimeQuarter = 0, sinTimeFull = 0;
    bool  enabled = true, billboard = false, alphaTest = true;
};
/** @brief Apply Pcg WindManager.InstantWindApply semantics without smoothing.
 * @param state Mutable caller-owned
 * state, published only after complete validation.
 * @param forward Finite source WindZone forward direction. Y
 * becomes the source forced downward value.
 * @param strength Finite WindZone main strength, clamped to [0, 1.2].
 *
 * @return Success or InvalidArgument; invalid/nonrepresentable input preserves state.
 * The phase becomes
 * std::pow(strength / 2 + 0.5, 3) / 10 exactly as the immediate shader upload path.
 * @ownership Retains no references
 * and invokes no callbacks. Caller serializes its owner-thread state.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> initializeVegetationWind(VegetationWindState& state, glm::vec3 forward, float strength);
/** @brief Advance source WindManager smoothing and phase using explicit nonnegative dt.
 * Direction Y follows
 * -max(forward.y, length(forward.xz)*0.5); strength clamps to [0,1.2]. Lerp factor clamps dt*0.25; phase uses the
 * smoothed strength and subtracts 100 once if >100.
 * @return Success or InvalidArgument; invalid/nonrepresentable input preserves state atomically.
 * Caller serializes state on its owner thread. No borrowed pointer, callback, wall clock or RNG.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> advanceVegetationWind(VegetationWindState& state, glm::vec3 forward, float strength,
                                                 float dt);
/** @brief Evaluate the source main/branch/leaf deformation for one vertex without mutation.
 * Requires finite inputs, invertible object transform, positive dimensions/distance, unit sine inputs
 * and finite wind globals. Source branch/leaf compile switches are explicit flags.
 * Resolves the source root 0/0 volume normalization as zero displacement; other nonrepresentable results fail.
 * @return Deformed object-space vertex or InvalidArgument. Double intermediates, float output; numerical
 * tolerance rather than GPU bit parity. Caller provides an immutable snapshot on its owning thread.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<glm::vec3> evaluateVegetationWind(const VegetationWindInput& input,
                                                       const VegetationWindState& state);
}  // namespace eve::graphics
