#pragma once

/** @file ActionCameraBlock.h @brief Typed camera-cue action block contract. */

#include "action/ActionNotifyRegistry.h"

#include <cstdint>

namespace eve::action {

/** @brief Owning validated camera impulse shared by action runtime and camera targets. */
struct ActionCameraCueBinding {
    /** @brief Stable project-defined cue identity used for target selection. */
    LogicalId cue;
    /** @brief Positional shake amplitude in world units. */
    double positionAmplitude = 0.15;
    /** @brief Rotational shake amplitude in degrees. */
    double rotationAmplitude = 1.5;
    /** @brief Additive vertical field-of-view impulse in degrees. */
    double fovAmplitude = 0.0;
    /** @brief Positive duration of the damped impulse. */
    Duration duration = Duration::fromNanoseconds(250000000);
    /** @brief Stable deterministic noise seed. */
    std::uint32_t seed = 0;

    auto operator<=>(const ActionCameraCueBinding&) const = default;

    /**
     * @brief Decode a camera cue transactionally.
     * @param payload Owning dynamic payload borrowed only for this call.
     * @return Validated owning cue or a field-path diagnostic.
     */
    [[nodiscard]] static Result<ActionCameraCueBinding> fromPayload(const Value::Object& payload);
};

/**
 * @brief Optional open target for instantaneous action camera cues.
 *
 * Implementations own camera state. Calls are synchronous and owner-thread-only;
 * implementations must not retain the borrowed binding or context.
 */
class IActionCameraCueSink {
public:
    static constexpr const char* capabilityName = "eve.action.camera-cue-sink";
    virtual ~IActionCameraCueSink() = default;

    /** @brief Whether this sink owns the supplied stable cue identity. */
    [[nodiscard]] virtual bool supports(const LogicalId& cue) const noexcept = 0;
    /** @brief Trigger one already validated camera cue. */
    [[nodiscard]] virtual Result<void> trigger(const ActionCameraCueBinding& binding,
                                               const ActionNotifyContext& context) = 0;
};

}  // namespace eve::action
