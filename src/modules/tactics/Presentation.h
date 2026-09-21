#pragma once

/**
 * @file Presentation.h
 * @brief Presentation intents derived from the battle event stream, with an explicit undo.
 *
 * The gap this closes: a turn-based framework normally drives highlights and animations by
 * mutating view objects (`MarkAsSelected`, `MarkAsAttacking`, `MarkAsDestroyed`, …) with no
 * statement of when that state stops being true. The result is the classic stale highlight:
 * a unit stays "selected" or "targetable" after the world moved on, because nothing owned
 * the transition back.
 *
 * Here presentation is *data*: a projection of the authoritative event stream into
 * `PresentationIntent` values, each carrying the revision it came from, a tick after which
 * it is stale, and a @ref PresentationRevert that says — explicitly — how and when to take
 * it back. Nothing in this header touches `graphics`, so the tactics module stays
 * render-free and a renderer (or a test) is just another consumer.
 */

#include "common/Result.h"
#include "tactics/TacticsTypes.h"

#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

namespace eve::tactics {

/**
 * @brief How a consumer should draw one unit.
 *
 * The set is deliberately small and player-facing: it is the vocabulary a UI needs, not a
 * description of the model. `Idle` is the resting state every transient state returns to.
 */
enum class UnitVisualState : std::uint8_t {
    /** @brief Resting, nothing special. */
    Idle,
    /** @brief Controlled by the viewing session. */
    Friendly,
    /** @brief Currently selected or acting. */
    Selected,
    /** @brief Already finished this round. */
    Finished,
    /** @brief A legal target for the armed ability. */
    Targetable,
    /** @brief Currently declaring an ability. */
    Attacking,
    /** @brief Reacting to an incoming effect. */
    Defending,
    /** @brief In transit between cells. */
    Moving,
    /** @brief Defeated; terminal. */
    Destroyed,
};

/**
 * @brief What makes a presentation intent stop being true.
 *
 * This is the part a view-object API cannot express, and the reason stale highlights exist.
 */
enum class PresentationRevertTrigger : std::uint8_t {
    /** @brief Durable state (defeat, end of round): never taken back. */
    Never,
    /** @brief Clear when `expiresAtTick` has passed. */
    OnExpiry,
    /** @brief Clear when any unit's next turn starts. */
    OnNextTurn,
    /** @brief Clear at the next round boundary. */
    OnRoundStart,
};

/** @brief Human-readable stable spelling of a visual state (protocol text). */
[[nodiscard]] EVENGINE_API_DOMAINS std::string_view unitVisualStateName(UnitVisualState state) noexcept;
/** @brief Human-readable stable spelling of a revert trigger (protocol text). */
[[nodiscard]] EVENGINE_API_DOMAINS std::string_view presentationRevertTriggerName(
    PresentationRevertTrigger trigger) noexcept;

/**
 * @brief One presentation instruction: what to show, where, and until when.
 *
 * @remarks Presentation is not authoritative and must be droppable: `revision` lets a
 *          consumer discard intents older than the state it is drawing, and
 *          `expiresAtTick` bounds every transient one. Geometry that the event stream does
 *          not carry (`from`, `to`, `path`) is filled from the accepted command the event
 *          points at, so there is still exactly one authority for it.
 */
struct PresentationIntent {
    /** @brief Battle event sequence this intent was projected from. */
    std::uint64_t sequence = 0;
    /** @brief The unit the intent is about. */
    SubjectRef    subject;
    /** @brief The other unit involved (ability target, attacker), when there is one. */
    SubjectRef    other;
    UnitVisualState state = UnitVisualState::Idle;
    /** @brief Origin cell, when the intent has geometry. */
    Cell          from;
    /** @brief Destination or target cell, when the intent has geometry. */
    Cell          to;
    /** @brief Traversal the consumer may animate; empty when not applicable. */
    std::vector<Cell> path;
    /** @brief Battle revision this intent was projected from. */
    Revision      revision;
    /** @brief Battle tick this intent was projected from. */
    SimulationTick tick = SimulationTick::zero();
    /**
     * @brief Tick after which the intent is stale.
     *
     * Zero means "no expiry": only durable states use it, and they are exactly the ones
     * whose revert trigger is @ref PresentationRevertTrigger::Never.
     */
    SimulationTick expiresAtTick = SimulationTick::zero();
};

/**
 * @brief The explicit undo contract for one intent.
 *
 * @remarks A consumer that applies an intent must either keep it until its trigger fires
 *          and then apply `restingState`, or drop it. `revertable` is false for durable
 *          states, and asking to revert those is a refusal rather than a silent no-op.
 */
struct PresentationRevert {
    /** @brief The state to restore when the trigger fires. */
    UnitVisualState          restingState = UnitVisualState::Idle;
    PresentationRevertTrigger trigger = PresentationRevertTrigger::Never;
    /** @brief Event sequence the revert belongs to, for correlation. */
    std::uint64_t            sequence = 0;

    /** @brief Whether a consumer may undo the intent at all. */
    [[nodiscard]] constexpr bool isRevertable() const noexcept {
        return trigger != PresentationRevertTrigger::Never;
    }
};

/** @brief One intent together with the way to take it back. */
struct PresentationCommand {
    PresentationIntent intent;
    PresentationRevert revert;
};

/**
 * @brief What the projector knows about the battle beyond the event itself.
 *
 * @remarks A value, like every other cross-boundary input in this module: the projector
 *          cannot call back into the battle, so a projection can be recomputed from a
 *          snapshot and compared byte for byte.
 */
struct PresentationFrame {
    /** @brief Battle revision the frame was observed at. */
    Revision revision;
    /** @brief Resting state of each subject, used as the revert target. */
    std::map<std::string, UnitVisualState> restingState;
    /** @brief How long a transient state stays valid, in ticks. */
    std::uint64_t transientTicks = 20;
};

/**
 * @brief Projects authoritative events into bounded, revertible presentation intents.
 *
 * @thread Stateless apart from its configuration; it holds no battle reference and can be
 *         shared or copied freely.
 */
class EVENGINE_API_DOMAINS PresentationProjector final {
public:
    /** @brief Configure the projector; the default lifetime is used unless overridden. */
    explicit PresentationProjector(std::uint64_t transientTicks = 20) : transientTicks_(transientTicks) {}

    /**
     * @brief Project one authoritative event into presentation commands.
     * @param event The battle event, which owns the sequence, tick and type.
     * @param command The accepted command the event points at, when the caller can supply
     *        it; it is the authority for the geometry (`cell`, `targetUnit`) the event does
     *        not carry. Pass `nullptr` when unavailable, in which case intents are emitted
     *        without geometry instead of guessing.
     * @param frame The revision and resting states the projection is anchored to.
     * @return Zero or more commands. An event with no presentation meaning (a random draw,
     *         an objective evaluation) projects to an empty list; that is a normal answer.
     */
    [[nodiscard]] Result<std::vector<PresentationCommand>> project(const BattleEvent& event,
                                                                   const BattleCommand* command,
                                                                   const PresentationFrame& frame) const;

    /**
     * @brief Build the inverse intent that undoes @p intent.
     *
     * @return The inverse intent, or `Unsupported` when the intent is durable
     *         (@ref PresentationRevert::revertable is false). Refusing loudly is the point:
     *         a caller cannot "revert" a defeat and leave the view lying about the battle.
     */
    [[nodiscard]] Result<PresentationIntent> revert(const PresentationIntent& intent,
                                                    const PresentationRevert& spec) const;

private:
    std::uint64_t transientTicks_ = 20;
};

}  // namespace eve::tactics
