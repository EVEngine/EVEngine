#pragma once

/** @file ActionSpatialBlock.h @brief Typed spatial payload shared by presentation action blocks. */

#include "common/Result.h"
#include "common/Value.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace eve::action {

/** @brief How a spawned presentation object follows its resolved anchor. */
enum class ActionSpatialAttachmentMode : std::uint8_t {
    /** @brief Re-evaluate position and rotation from the anchor every update. */
    FollowTarget,
    /** @brief Re-evaluate position only and retain the spawn rotation. */
    FollowPositionOnly,
    /** @brief Capture the complete world transform once on Enter. */
    WorldTransformAtStart,
};

/** @brief Which handle in ActionNotifyContext supplies the spatial anchor. */
enum class ActionSpatialTarget : std::uint8_t { Source, Target };

/** @brief Owning three-component value used at the action/presentation boundary. */
struct ActionSpatialVector3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    auto operator<=>(const ActionSpatialVector3&) const = default;
};

/**
 * @brief Validated spatial settings decoded from a notify payload.
 *
 * The action module owns only these backend-neutral values. Scene, animation,
 * audio and VFX adapters resolve entity handles and bone names synchronously;
 * this value never stores a scene node, component pointer or bone pointer.
 */
struct ActionSpatialBinding {
    ActionSpatialAttachmentMode mode = ActionSpatialAttachmentMode::FollowTarget;
    ActionSpatialTarget         target = ActionSpatialTarget::Source;
    std::size_t                 targetIndex = 0;
    std::string                 bone;
    ActionSpatialVector3        positionOffset;
    ActionSpatialVector3        rotationOffsetDegrees;
    ActionSpatialVector3        scale{1.0, 1.0, 1.0};

    auto operator<=>(const ActionSpatialBinding&) const = default;

    /**
     * @brief Decode and validate the common spatial fields in an owning payload.
     * @param payload Notify payload; unrelated provider-specific fields are ignored.
     * @return Typed settings or a stable field-path diagnostic without mutation.
     * @remarks Accepted vector fields are three-element numeric arrays. All
     *          components must be finite and scale components must be positive.
     */
    [[nodiscard]] static Result<ActionSpatialBinding> fromPayload(const Value::Object& payload);
};

/** @brief Return the stable payload spelling for an attachment mode. */
[[nodiscard]] std::string_view actionSpatialAttachmentModeName(ActionSpatialAttachmentMode mode) noexcept;

}  // namespace eve::action
