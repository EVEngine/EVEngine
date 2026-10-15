#pragma once
#include "common/Export.h"

/**
 * @file RuleTypes.h
 * @brief Immutable emergence rule definitions and deferred activations.
 */

#include "common/Value.h"
#include "decision/Condition.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::emergence {

/** @brief When a watched rule may emit an activation. */
enum class FireMode : std::uint8_t {
    /** @brief Fire only on false→true edges. */
    Rising = 0,
    /** @brief Fire whenever woken while the condition currently passes. */
    Level = 1,
};

/** @brief One deferred side-effect descriptor; domains interpret `kind`. */
struct EmergenceAction {
    std::string kind;
    eve::Value  args = eve::Value::Object{};
};

/**
 * @brief One immutable rule definition owned by the catalogue.
 *
 * `watchKeys` must be non-empty after registration. When the author omits them,
 * the engine derives keys from the Condition AST via collectWatchKeys().
 */
struct RuleDefinition {
    std::string                  id;
    int                          priority      = 0;
    FireMode                     fireMode      = FireMode::Rising;
    bool                         once          = false;
    std::uint64_t                cooldownTicks = 0;
    decision::Condition          condition;
    std::vector<std::string>     watchKeys;
    std::vector<EmergenceAction> actions;
};

/** @brief One scheduled activation produced by drain(). */
struct Activation {
    std::uint64_t                sequence = 0;
    std::uint64_t                tick     = 0;
    std::string                  ruleId;
    int                          priority = 0;
    std::vector<EmergenceAction> actions;
};

}  // namespace eve::emergence
