#pragma once

/** @file ActionPreview.h @brief Presentation-neutral action preview frame contracts. */

#include "action/ActionTimeline.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eve::action {

/** @brief Reason a preview frame is being presented. */
enum class ActionPreviewReason : std::uint8_t { Refresh, Seek, Advance };

/** @brief Semantic presentation channel selected for one timeline boundary. */
enum class ActionPreviewCueKind : std::uint8_t { Gameplay, Vfx, Audio, Camera, StateEnter, StateExit };

/** @brief Explicit availability of root-motion preview data. */
enum class RootMotionPreviewState : std::uint8_t { Unavailable, Available };

/** @brief One owning 3D point in an animation root-motion trajectory. */
struct ActionPreviewPoint3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

/** @brief Owning presentation cue derived from a canonical timeline event. */
struct ActionPreviewCue {
    ActionPreviewCueKind kind = ActionPreviewCueKind::Gameplay;
    LogicalId            itemId;
    LogicalId            type;
    Duration             time;
    Value::Object        payload;
};

/** @brief Complete frame atomically prepared and then presented by a preview host. */
struct ActionPreviewFrame {
    ActionPreviewReason              reason = ActionPreviewReason::Refresh;
    std::string                      animationUri;
    Duration                         previous;
    Duration                         current;
    std::vector<ActionPreviewCue>    cues;
    RootMotionPreviewState           rootMotionState = RootMotionPreviewState::Unavailable;
    std::vector<ActionPreviewPoint3> rootMotionPath;
};

/**
 * @brief Host boundary for 3D animation, VFX, audio and camera preview.
 *
 * prepare must not publish visible state. After it succeeds, present is a
 * non-failing commit point. No method may retain references to the frame.
 */
class IActionPreviewSink {
public:
    virtual ~IActionPreviewSink() = default;
    /** @brief Validate and stage every resource required by a frame. */
    [[nodiscard]] virtual Result<void> prepare(const ActionPreviewFrame& frame) = 0;
    /** @brief Publish one successfully prepared frame without failure or callbacks. */
    virtual void present(const ActionPreviewFrame& frame) noexcept = 0;
    /** @brief Discard staged resources when the authoritative transport commit fails. */
    virtual void discardPrepared() noexcept = 0;
};

/** @brief Optional animation provider that samples an owning root-motion polyline. */
class IActionRootMotionSource {
public:
    virtual ~IActionRootMotionSource() = default;
    /** @brief Sample animation root motion from zero through duration, including both endpoints. */
    [[nodiscard]] virtual Result<std::vector<ActionPreviewPoint3>> sampleRootMotion(
        std::string_view animationUri, Duration duration, std::uint32_t sampleCount) const = 0;
};

}  // namespace eve::action
