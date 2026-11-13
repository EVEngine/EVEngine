#pragma once
#include "combat/Damage.h"
namespace eve::combat {
// The rule set is the immutable configuration authority; pipeline callbacks are only its derived cache.
struct DamageRuntime::SettlementState {
    settlement::SettlementRuleSet  rules;
    settlement::SettlementPipeline pipeline;
};
}  // namespace eve::combat
