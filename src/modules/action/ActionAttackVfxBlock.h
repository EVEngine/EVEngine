#pragma once
#include "common/Export.h"

/** @file ActionAttackVfxBlock.h @brief Typed AttackVfx action-block payload contract. */

#include "action/ActionSpatialBlock.h"
#include "common/Identity.h"
#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace eve::action {

/** @brief Authored AttackVfx block shape used to select lifetime validation. */
enum class ActionAttackVfxShape : std::uint8_t { Instant, State };

/** @brief One cue scheduled relative to AttackVfx play(). */
struct ActionAttackVfxCue {
    double      offsetSeconds = 0.0;
    std::string cue;

    /** @brief Operator <=>. */
    auto operator<=>(const ActionAttackVfxCue&) const = default;
};

/**
 * @brief Owning validated AttackVfx settings shared by runtime and editor preview.
 *
 * recipeId XOR uri: exactly one identification path is required. recipeId names
 * a pre-registered AttackVfxRuntime recipe; uri loads a JSON recipe document.
 */
struct EVENGINE_API_PLATFORM ActionAttackVfxBinding {
    LogicalId                       recipeId;
    std::string                     uri;
    std::optional<LogicalId>        skinId;
    ActionSpatialBinding            spatial;
    double                          lifetimeSeconds = 0.0;
    std::vector<ActionAttackVfxCue> cues;

    /** @brief Operator <=>. */
    auto operator<=>(const ActionAttackVfxBinding&) const = default;

    /**
     * @brief Decode one dynamic AttackVfx payload transactionally.
     * @param payload Owning payload borrowed only for this call.
     * @param shape Instant or state contract selected by the descriptor.
     * @return Validated owning settings or a field-path diagnostic.
     * @remarks Instant blocks require a positive finite lifetimeSeconds value.
     *          Exactly one of recipeId / uri must be non-empty.
     */
    [[nodiscard]] static Result<ActionAttackVfxBinding> fromPayload(const Value::Object& payload,
                                                                    ActionAttackVfxShape shape);
};

}  // namespace eve::action
