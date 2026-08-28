#pragma once

/** @file TacticsAction.h @brief Tactics movement adapter for shared ActionRuntime. */

#include "action/Action.h"
#include "tactics/TacticsBattle.h"

#include <memory>

namespace eve::tactics {

/**
 * @brief Active-effect provider that commits move, face and wait through ActionRuntime.
 *
 * The bound Battle is borrowed and must outlive this provider and every
 * ActionRuntime using it. Preparation validates movement without mutation;
 * ActionRuntime calls commit synchronously on the same simulation thread. The
 * provider retains no unit pointer and invokes no script or external callback.
 */
class TacticsActionExecutor final : public action::IActionEffectExecutor {
public:
    /** @brief Bind one module-owned running battle. */
    explicit TacticsActionExecutor(Battle& battle) noexcept : battle_(battle) {}

    /** @copydoc action::IActionEffectExecutor::prepare */
    [[nodiscard]] Result<std::unique_ptr<action::IActionEffectOperation>> prepare(
        const action::ActionDefinition& definition, const action::ActionRequest& request,
        const sensing::TargetSet* targets, SimulationTick tick) override;

private:
    Battle& battle_;
};

/** @brief Compatibility spelling retained for the original movement-only adapter name. */
using MoveActionExecutor = TacticsActionExecutor;

/** @brief Build the canonical zero-duration tactics move action definition. */
[[nodiscard]] action::ActionDefinition moveActionDefinition();
/** @brief Build the canonical zero-duration tactics facing action definition. */
[[nodiscard]] action::ActionDefinition faceActionDefinition();
/** @brief Build the canonical zero-duration tactics wait action definition. */
[[nodiscard]] action::ActionDefinition waitActionDefinition();

/**
 * @brief Build a checked move request using canonical integer cell parameters.
 * @param unit Generation-checked tactical unit source.
 * @param destination Logical target cell.
 * @param tick Deterministic request tick.
 */
[[nodiscard]] Result<action::ActionRequest> makeMoveRequest(ecs::EntityHandle unit, Cell destination,
                                                            SimulationTick tick);
/** @brief Build a checked facing request with optimistic battle revision. */
[[nodiscard]] Result<action::ActionRequest> makeFaceRequest(ecs::EntityHandle unit, int facing,
                                                            SimulationTick tick);
/** @brief Build a checked wait request with optimistic battle revision. */
[[nodiscard]] Result<action::ActionRequest> makeWaitRequest(ecs::EntityHandle unit, SimulationTick tick);

}  // namespace eve::tactics
