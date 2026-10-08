#pragma once
#include "common/Export.h"

/** @file CombatCancelResolver.h @brief Cancel buffer + windows + combo graph resolution. */

#include "action/input/ActionCancelWindowState.h"
#include "action/input/ActionComboWindowState.h"
#include "action/input/ActionInputBuffer.h"
#include "combat/ComboGraph.h"
#include "common/Result.h"

#include <optional>

namespace eve::combat {

/** @brief Inputs for one deterministic cancel/combo resolution. */
struct CombatCancelResolveRequest {
    SubjectRef     subject;
    LogicalId      currentAbility;
    SimulationTick tick = SimulationTick::zero();
};

/** @brief Owning audit of one resolved cancel or combo transition. */
struct CombatCancelResolution {
    action::input::BufferedInput                   consumed;
    action::input::ActionCancelMatch               cancel;
    std::optional<action::input::ActionComboMatch> comboWindow;
    std::optional<ComboGraphMatch>                 graph;
};

/**
 * @brief Compose buffered input, cancel/combo windows and ComboGraph without activating abilities.
 *
 * Callers submit the resulting graph target as an AbilityIntent. Missing matches are NotFound.
 */
class EVENGINE_API_BACKENDS CombatCancelResolver {
public:
    void setBuffer(action::input::ActionInputBuffer& buffer) noexcept { buffer_ = &buffer; }
    void setCancels(const action::input::ActionCancelWindowState& cancels) noexcept { cancels_ = &cancels; }
    void setCombos(const action::input::ActionComboWindowState* combos) noexcept { combos_ = combos; }
    void setGraph(const ComboGraph* graph) noexcept { graph_ = graph; }

    /**
     * @brief Expire the buffer, consume the best allowed input, and optionally match the combo graph.
     * @remarks Consumes the buffered input even when the graph has no edge (NoOp graph).
     */
    [[nodiscard]] Result<CombatCancelResolution> resolve(const CombatCancelResolveRequest& request);

private:
    action::input::ActionInputBuffer*             buffer_  = nullptr;
    const action::input::ActionCancelWindowState* cancels_ = nullptr;
    const action::input::ActionComboWindowState*  combos_  = nullptr;
    const ComboGraph*                             graph_   = nullptr;
};

}  // namespace eve::combat
