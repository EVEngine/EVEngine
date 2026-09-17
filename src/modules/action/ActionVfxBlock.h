#pragma once

/** @file ActionVfxBlock.h @brief Typed VFX action-block payload contract. */

#include "action/ActionSpatialBlock.h"

#include <cstdint>
#include <string>

namespace eve::action {

/** @brief Authored VFX block shape used to select lifetime validation. */
enum class ActionVfxShape : std::uint8_t { Instant, State };

/** @brief Behavior applied when a VFX block leaves its active interval. */
enum class ActionVfxStopBehavior : std::uint8_t { StopEmitting, ClearImmediately };

/** @brief Owning validated VFX settings shared by runtime and editor preview. */
struct ActionVfxBinding {
    std::string           uri;
    ActionSpatialBinding  spatial;
    ActionVfxStopBehavior stopBehavior       = ActionVfxStopBehavior::StopEmitting;
    bool                  playbackRateSynced = true;
    double                clipStartTime      = 0.0;
    double                clipEndTime        = 0.5;
    double                lifetimeSeconds    = 0.0;

    auto operator<=>(const ActionVfxBinding&) const = default;

    /**
     * @brief Decode one dynamic VFX payload transactionally.
     * @param payload Owning payload borrowed only for this call.
     * @param shape Instant or state contract selected by the descriptor.
     * @return Validated owning settings or a field-path diagnostic.
     * @remarks Clip bounds must be finite with end greater than start. Instant
     * blocks additionally require a positive finite lifetimeSeconds value.
     */
    [[nodiscard]] static Result<ActionVfxBinding> fromPayload(const Value::Object& payload,
                                                               ActionVfxShape shape);
};

}  // namespace eve::action
