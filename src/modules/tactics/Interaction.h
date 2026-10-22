#pragma once

/**
 * @file Interaction.h
 * @brief Board interaction state machine that returns *intents* instead of mutating.
 *
 * The gap this closes: a turn-based UI normally owns its own selection/highlight state
 * on top of the simulation, so the same rule ("may this unit move there?") ends up
 * implemented twice — once for the highlight and once for the commit. Here the state
 * machine reads an immutable, caller-supplied projection of the battle and answers with a
 * value describing what the player asked for. It never touches the battle, so it is
 * headlessly replayable and cannot become a second authority over game state.
 *
 * This header has no rendering, input or ECS dependency by design: `tactics` stays
 * render-free, and a UI may run this machine on any thread that owns its own copy.
 */

#include "common/Result.h"
#include "tactics/TacticsTypes.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace eve::tactics {

/** @brief Where the interaction machine is, as far as the player is concerned. */
enum class InteractionState : std::uint8_t {
    /** @brief Not this controller's turn, or the battle is not accepting input at all. */
    Blocked,
    /** @brief Nothing selected yet; a click picks a destination or the active unit. */
    AwaitSelection,
    /** @brief The controlled unit is selected; the next click acts on a cell. */
    UnitSelected,
    /** @brief An ability is armed; the next click picks its target. */
    Targeting,
    /** @brief The caller has an uncommitted intent and must commit or cancel it. */
    Resolving,
    /** @brief The battle is over; only inspection remains. */
    Ended,
};

/** @brief What the player asked for. A value, not a mutation. */
enum class InteractionIntentKind : std::uint8_t {
    /** @brief Nothing to do: the click landed somewhere with no meaning in this state. */
    None,
    /** @brief Focus the controlled unit. */
    SelectUnit,
    /** @brief Move the controlled unit onto @ref InteractionIntent::cell. */
    MoveTo,
    /** @brief Declare @ref InteractionIntent::action against cell and optional unit. */
    UseAbilityOn,
    /** @brief Armed ability/selection should be dropped. */
    Cancel,
    /** @brief Submit whatever intent is pending. */
    Confirm,
    /** @brief End the controlled unit's activation. */
    EndTurn,
};

/** @brief Human-readable stable spelling of an interaction state (protocol text). */
[[nodiscard]] EVENGINE_API_DOMAINS std::string_view interactionStateName(InteractionState state) noexcept;
/** @brief Human-readable stable spelling of an intent kind (protocol text). */
[[nodiscard]] EVENGINE_API_DOMAINS std::string_view interactionIntentKindName(InteractionIntentKind kind) noexcept;

/**
 * @brief One player intention, expressed with stable identities only.
 *
 * @remarks The intent carries the revision it was decided against, exactly like a
 *          gameplay command, so a caller can detect that the world moved on between the
 *          click and the commit instead of committing against stale observations.
 */
struct InteractionIntent {
    InteractionIntentKind kind = InteractionIntentKind::None;
    /** @brief The unit the intent is about (the controlled unit when one is known). */
    SubjectRef actor;
    Cell       cell;
    /** @brief Armed ability for @ref InteractionIntentKind::UseAbilityOn. */
    LogicalId  action;
    /** @brief Unit occupying the target cell, when the intent targets a unit. */
    SubjectRef targetUnit;
    /** @brief The revision this intent was decided against. */
    Revision   expected;
};

/**
 * @brief Immutable projection of everything the interaction machine is allowed to know.
 *
 * @remarks This is a *value*, built by the caller from its own authoritative query
 *          results — `reachable`, `cellsInRange`, `observe`. The machine therefore has no
 *          way to reach into the battle: a UI cannot accidentally commit through it, and
 *          the same context can be replayed to reproduce a click sequence exactly.
 *          A context the caller cannot compute (for example reachability of a blocked
 *          unit) is expressed by leaving the corresponding set empty, which the machine
 *          reads as "no legal destination" rather than as an error.
 */
struct InteractionContext {
    BattleStatus status = BattleStatus::Setup;
    BattlePhase  phase  = BattlePhase::Setup;
    /** @brief The unit currently allowed to act, or an invalid subject when none is. */
    SubjectRef   activeUnit;
    /** @brief The unit this session drives. An invalid subject means "observe only". */
    SubjectRef   controlledUnit;
    /** @brief Cell of the controlled unit, used to recognise a click on the unit itself. */
    Cell         controlledCell;
    /** @brief Destinations the controlled unit may legally move to. */
    std::vector<Cell> reachableCells;
    /** @brief Cells where an armed ability may legally land. */
    std::vector<Cell> targetableCells;
    /** @brief Unit occupying each cell, for turning a targeted cell into a targeted unit. */
    std::map<Cell, SubjectRef> unitsByCell;
    /** @brief The revision the caller observed when it built this context. */
    Revision     revision;
};

/**
 * @brief A disposable, non-authoritative interaction session.
 *
 * The session holds only the player-facing state (what is selected, what is armed). The
 * authoritative battle owns nothing here, so a session may be dropped and rebuilt from a
 * fresh context at any time — which is what makes it safe for a UI to keep one per client.
 *
 * @thread Affine to the caller; it has no internal synchronization and touches no shared
 *         state, so two clients may each drive their own copy of the same context.
 * @reentrancy Handlers do not invoke callbacks.
 */
class EVENGINE_API_DOMAINS InteractionSession final {
public:
    /**
     * @brief Start a session from an immutable context.
     *
     * @return The session, whose initial state is derived from the context and never
     *         stored twice. A battle that is not running or not in `acting` starts
     *         `Blocked`; a finished battle starts `Ended`; a session that drives a unit
     *         other than the active one also starts `Blocked`. A session that cannot act
     *         reports it through @ref state rather than through a failing factory, so a UI
     *         can hold one session across turns instead of rebuilding it defensively.
     */
    [[nodiscard]] static InteractionSession create(InteractionContext context);

    /** @brief Current player-facing state. */
    [[nodiscard]] InteractionState state() const noexcept;
    /** @brief The context this session was created from. */
    [[nodiscard]] const InteractionContext& context() const noexcept { return context_; }
    /** @brief The ability currently armed, or an invalid id when none is. */
    [[nodiscard]] const LogicalId& armedAction() const noexcept { return armedAction_; }

    /**
     * @brief Resolve a click on one cell into an intention.
     * @return The intent, or a structured refusal when the machine cannot accept input at
     *         all: blocked, or ended. A click with no meaning in the current state yields
     *         @ref InteractionIntentKind::None rather than a failure: "the player clicked
     *         an empty hex" is normal, not an error.
     */
    [[nodiscard]] Result<InteractionIntent> onCellClicked(Cell cell);

    /**
     * @brief Arm an ability so the next click selects its target.
     * @param action Ability identity the caller's rule set reported as usable.
     * @param targetableCells Cells that ability may land on, already filtered by the
     *        caller's own rules (range, line of sight, resources).
     * @return Applied, or a refusal when the session cannot accept input or the ability id
     *         is invalid. `targetableCells` may be empty: arming is allowed, and every
     *         click then resolves to `None`, which is how a UI shows "this ability has no
     *         legal target right now" without inventing a failure.
     */
    [[nodiscard]] Result<void> armAbility(const LogicalId& action, std::vector<Cell> targetableCells);

    /** @brief Drop any selection or armed ability, reporting it as a Cancel intent. */
    [[nodiscard]] Result<InteractionIntent> onCancel();

    /** @brief Ask to end the controlled unit's activation. */
    [[nodiscard]] Result<InteractionIntent> onEndTurn();

    /**
     * @brief Record that the caller is committing the intent it just received.
     *
     * @return Applied (state `Resolving`) or a refusal when there is no pending intent.
     * @remarks The session stays non-authoritative: this only tells the machine that the
     *          intent is in flight, so a second click is refused instead of queueing a
     *          contradictory intention behind a commit that may yet fail.
     */
    [[nodiscard]] Result<void> markResolving();

    /** @brief Return to `AwaitSelection` after a commit succeeded, failed or was dropped. */
    [[nodiscard]] Result<void> resolve();

private:
    explicit InteractionSession(InteractionContext context);

    [[nodiscard]] bool acceptsInput() const noexcept;
    [[nodiscard]] bool cellInReachable(Cell cell) const;
    [[nodiscard]] bool cellInTargetable(Cell cell) const;
    [[nodiscard]] InteractionIntent makeIntent(InteractionIntentKind kind, Cell cell) const;

    InteractionContext      context_;
    InteractionState        state_ = InteractionState::Blocked;
    bool                    unitSelected_ = false;
    LogicalId               armedAction_;
    std::vector<Cell>       armedTargets_;
    InteractionIntentKind   pending_ = InteractionIntentKind::None;
};

}  // namespace eve::tactics
