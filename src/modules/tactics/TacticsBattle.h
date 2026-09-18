#pragma once

/** @file TacticsBattle.h @brief Checked battle setup and deterministic phase systems. */

#include "tactics/TacticsTypes.h"
#include "tactics/TacticsPath.h"

#include <cstddef>
#include <vector>

namespace eve::tactics {

/** @brief Committed tactical movement and remaining resource state. */
struct MoveReceipt {
    SubjectRef        actor;
    std::vector<Cell> path;
    int               cost                = 0;
    int               remainingMovePoints = 0;
};

/**
 * @brief One declared ability activation and the resource state it left behind.
 *
 * This is what a caller needs to hand the declaration to whoever owns the effect:
 * which actor, which ability, which target, and how many action points remain.
 */
struct AbilityReceipt {
    SubjectRef actor;
    LogicalId  action;
    /** @brief The targeted unit, or an invalid subject when the ability targets a cell only. */
    SubjectRef targetUnit;
    Cell       targetCell;
    /** @brief Remaining action points after the activation was paid for. */
    int        remainingActionPoints = 0;
};

/**
 * @brief Stateless systems over the tactics short-root ECS family.
 *
 * Entity scope: Battle, TacticalSide and TacticalUnit roots linked to the
 * supplied Battle. Read set: identities, membership, turn resources and board
 * facts. Write set: Battle turn/events/board and TacticalUnit membership,
 * position and acted flag. Structural mutation: none. Events are appended to
 * Battle::Events. Services: none. Phase: owner simulation thread only.
 * Determinism: bit-exact for equal setup, handles, SubjectRefs and steps.
 */
class BattleSystem final {
public:
    /** @brief Register a side during setup using a generation-checked handle. */
    [[nodiscard]] static Result<void> addSide(Battle& battle, ecs::EntityHandle side);

    /**
     * @brief Register and place a unit during setup atomically.
     * @return Applied, or failure with board/unit/battle state unchanged.
     */
    [[nodiscard]] static Result<void> addUnit(Battle& battle, ecs::EntityHandle unit, ecs::EntityHandle side,
                                              Cell cell);

    /** @brief Start a configured battle and emit battle.started. */
    [[nodiscard]] static Result<void> start(Battle& battle, std::string policyId);

    /**
     * @brief Advance exactly one observable automatic phase transition.
     * @param battle Owner-thread-affine authoritative battle.
     * @param step Strictly increasing injected simulation tick and non-negative duration.
     */
    [[nodiscard]] static Result<BattlePhase> advance(Battle& battle, const SimulationStep& step);

    /** @brief End the currently acting unit's turn and enter TurnEnd. */
    [[nodiscard]] static Result<void> endTurn(Battle& battle, SubjectRef actor);

    /**
     * @brief Validate and atomically commit movement for the active unit.
     * @return Owning receipt; failure leaves board, position and resources unchanged.
     */
    [[nodiscard]] static Result<MoveReceipt> moveUnit(Battle& battle, SubjectRef actor, Cell destination);
    /** @brief Commit movement at an explicit non-decreasing command tick (ActionRuntime adapter path). */
    [[nodiscard]] static Result<MoveReceipt> moveUnit(Battle& battle, SubjectRef actor, Cell destination,
                                                      SimulationTick commandTick);

    /** @brief Change the active unit's logical facing without consuming movement points. */
    [[nodiscard]] static Result<void> faceUnit(Battle& battle, SubjectRef actor, int facing);
    /** @brief Commit facing at an explicit non-decreasing command tick. */
    [[nodiscard]] static Result<void> faceUnit(Battle& battle, SubjectRef actor, int facing,
                                               SimulationTick commandTick);
    /** @brief Preview a facing action with the exact validator used by commit. */
    [[nodiscard]] static Result<void> previewFace(Battle& battle, SubjectRef actor, int facing);

    /** @brief Commit a wait action and enter TurnEnd for the active unit. */
    [[nodiscard]] static Result<void> waitUnit(Battle& battle, SubjectRef actor);
    /** @brief Commit wait at an explicit non-decreasing command tick. */
    [[nodiscard]] static Result<void> waitUnit(Battle& battle, SubjectRef actor, SimulationTick commandTick);
    /** @brief Preview a wait action with the exact validator used by commit. */
    [[nodiscard]] static Result<void> previewWait(Battle& battle, SubjectRef actor);

    /**
     * @brief Declare an ability activation by the active unit.
     * @param action Ability identity; it must be a valid logical id.
     * @param targetCell Target cell; it must exist and share the actor's layer.
     * @param targetUnit Optional targeted unit; when valid it must be a living unit of
     *        this battle. It is kept independent of @p targetCell, because an ability may
     *        name a primary unit inside an area the caller centres elsewhere.
     * @param payload Opaque caller-owned effect parameters, stored and replayed verbatim;
     *        it must not exceed `kMaxAbilityPayloadBytes`.
     * @return Owning receipt, or a structured refusal that leaves resources, events and
     *         the command log unchanged.
     * @remarks Tactics owns the *activation opportunity*, not the effect: it enforces
     *          the phase and the actor, spends one action point, emits
     *          `action.declared` and records the command for replay. Damage, status
     *          and hit resolution stay with the RPG or game adapter, which reports
     *          the tactical outcome back through {@link defeatUnit}. Ending the turn
     *          is the caller's decision, exactly as for a move.
     * @ownership The returned receipt owns its values; nothing borrows from the battle.
     */
    [[nodiscard]] static Result<AbilityReceipt> useAbility(Battle& battle, SubjectRef actor, const LogicalId& action,
                                                           Cell targetCell, SubjectRef targetUnit = {},
                                                           std::string payload = {});

    /**
     * @brief Preview an ability declaration with the exact validator used by commit.
     * @return The receipt the commit would produce, computed without mutation: no action
     *         point is spent, no event is emitted and no command is recorded.
     * @remarks Sharing one validator is what lets a UI show "this is legal and costs one
     *          point" and then commit it without a second, divergent rule set.
     */
    [[nodiscard]] static Result<AbilityReceipt> previewAbility(Battle& battle, SubjectRef actor,
                                                               const LogicalId& action, Cell targetCell,
                                                               SubjectRef targetUnit = {},
                                                               std::string_view payload = {});

    /** @brief Preview movement with the exact validator used by commit, without mutation. */
    [[nodiscard]] static Result<MoveReceipt> previewMove(Battle& battle, SubjectRef actor, Cell destination);

    /** @brief End a running battle and enter BattleEnd. */
    [[nodiscard]] static Result<void> finish(Battle& battle);

    /** @brief Open a sorted reaction window for an existing battle event sequence. */
    [[nodiscard]] static Result<std::size_t> openReaction(Battle& battle, std::uint64_t triggerSequence,
                                                          std::vector<ReactionCandidate> candidates);
    /** @brief Accept one eligible reaction, consume one reaction point and close the top window. */
    [[nodiscard]] static Result<ReactionReceipt> acceptReaction(Battle& battle, SubjectRef reactor,
                                                                 const LogicalId& action);
    /** @brief Decline and close the top reaction window without consuming resources. */
    [[nodiscard]] static Result<void> declineReaction(Battle& battle);

    /** @brief Register a validated built-in objective during setup. */
    [[nodiscard]] static Result<void> addObjective(Battle& battle, ObjectiveSpec objective);

    /** @brief Apply a tactical defeat outcome and evaluate affected objectives atomically. */
    [[nodiscard]] static Result<void> defeatUnit(Battle& battle, SubjectRef unit);

    /**
     * @brief Consume one value from a battle-owned named deterministic stream.
     * @remarks Only commit/resolver paths may call this; previews and queries must not advance streams.
     */
    [[nodiscard]] static Result<std::uint64_t> roll(Battle& battle, const LogicalId& stream);

private:
    [[nodiscard]] static Result<std::vector<ecs::EntityHandle>> orderedUnits(Battle& battle);
};

}  // namespace eve::tactics
