#pragma once
#include "common/Export.h"

/** @file SettlementModule.h @brief Script-facing factory for settlement rule + ledger runtimes. */

#include "common/Module.h"

namespace eve::settlement {

/**
 * @brief Factory module for script-owned settlement protocol runtimes.
 *
 * Each runtime owns a SettlementPipeline, an installed SettlementRuleSet
 * snapshot, and an optional script-owned resource ledger used by a narrow
 * damage/heal policy. It never writes RPG/Combat/RTS/Card domain state;
 * those modules keep their own configureSettlementRulesJson entry points.
 */
class EVENGINE_API_FOUNDATION Settlement final : public Module {
public:
    Module_REG(Settlement);
    /** @brief Settlement. */
    Settlement()           = default;
    /** @brief Settlement. */
    ~Settlement() override = default;
};

}  // namespace eve::settlement
