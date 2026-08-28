#pragma once

/** @file CombatAttributes.h @brief Tag-addressed combat attributes, regeneration and change events. */

#include "attributes/AttributeSet.h"
#include "common/Result.h"
#include "common/Time.h"

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eve::combat {

/** @brief Data-driven bounds and deterministic regeneration for one combat attribute. */
struct CombatAttributeDefinition {
    std::string tag;
    double      initialValue         = 0.0;
    double      minimumValue         = 0.0;
    double      maximumValue         = 0.0;
    double      regenerationPerSecond = 0.0;

    /** @brief Validate the gameplay-tag identity, finite bounds and regeneration rate. */
    [[nodiscard]] Result<void> validate() const;
};

/** @brief Observable reason for a committed combat-attribute change. */
enum class CombatAttributeEventKind : std::uint8_t { Gain, Loss, Depleted };

/** @brief Owning event emitted after one attribute mutation commits. */
struct CombatAttributeEvent {
    std::string              tag;
    CombatAttributeEventKind kind = CombatAttributeEventKind::Gain;
    double                   previousValue = 0.0;
    double                   value         = 0.0;
    double                   delta         = 0.0;
};

/** @brief Owning result of a deterministic regeneration step. */
struct CombatAttributeAdvance {
    std::vector<CombatAttributeEvent> events;
};

/**
 * @brief Owner-thread authoritative combat attribute coordinator.
 *
 * Definitions and the underlying AttributeSet are owned by this runtime. All
 * changes clamp before one base-value commit and return owning events; no
 * callback is invoked. advance consumes injected Duration and no wall clock.
 */
class CombatAttributeRuntime {
public:
    /** @brief Construct an empty runtime for a stable owning subject id. */
    explicit CombatAttributeRuntime(std::string subject = {}) : values_(std::move(subject)) {}

    /** @brief Register one tag-addressed definition and initialize its value atomically. */
    [[nodiscard]] Result<void> registerAttribute(CombatAttributeDefinition definition);
    /** @brief Return the current base value, or NotFound for an unknown tag. */
    [[nodiscard]] Result<double> value(std::string_view tag) const;
    /** @brief Replace a value after clamping and return its owning gain/loss/depletion event, if changed. */
    [[nodiscard]] Result<std::vector<CombatAttributeEvent>> setValue(std::string_view tag, double value);
    /** @brief Add a signed delta after clamping and return owning change events. */
    [[nodiscard]] Result<std::vector<CombatAttributeEvent>> modify(std::string_view tag, double delta);
    /** @brief Regenerate all attributes in lexical tag order using injected deterministic time. */
    [[nodiscard]] Result<CombatAttributeAdvance> advance(Duration delta);
    /** @brief Return owning registered definitions in lexical tag order. */
    [[nodiscard]] std::vector<CombatAttributeDefinition> definitions() const;

private:
    [[nodiscard]] Result<std::vector<CombatAttributeEvent>> commit(const CombatAttributeDefinition& definition,
                                                                   double newValue);

    attributes::AttributeSet                       values_;
    std::map<std::string, CombatAttributeDefinition, std::less<>> definitions_;
};

}  // namespace eve::combat
