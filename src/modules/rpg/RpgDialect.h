#pragma once

/**
 * @file RpgDialect.h
 * @brief RPG vocabulary for the `.dnut` sequence language, plus its catalogue and session.
 */

#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eve::dnut {
class StepKindRegistry;
}

namespace eve::inventory {
class Bag;
class EquipmentSet;
}  // namespace eve::inventory

namespace eve::rpg {

class GameState;
class Party;
class RPGActor;

/** @brief Terminal state of one requested actor move. */
enum class StoryMoveStatus : std::uint8_t {
    /** @brief The actor is already at the destination, so the step completes inline. */
    Arrived,
    /** @brief The move was accepted and is still running; the host acknowledges the step when it ends. */
    Travelling,
    /** @brief The destination cannot be reached, so the `move` step fails. */
    Unreachable,
};

/**
 * @brief One `move` step request, expressed in the host's own movement space.
 *
 * `x` and `y` carry whatever the game's movement model uses — tile indices for a
 * grid or tactics game, world units for a continuous one, lanes for a rail
 * sequence. The engine never interprets them and never assumes a path model, so
 * one authored `.dnut` story is not tied to one kind of game; the meaning is
 * settled by whoever installs the handler.
 *
 * @thread Filled and consumed synchronously inside one step dispatch.
 */
struct StoryMoveRequest {
    /** @brief Authored `actor=<id>` verbatim; the host's own subject space. */
    std::string actorId;
    /**
     * @brief Actor resolved through the binding, or nullptr.
     *
     * A convenience for hosts whose subjects are party members: it is non-null
     * exactly when `RpgStoryBinding::resolveActor` or `party` resolved
     * `actorId`. It is visibly null when the id belongs to another subject space
     * (a map object, a camera, a squad), so a host must not read a null here as
     * "the actor does not exist" — use `actorId` and resolve it yourself.
     */
    RPGActor* actor = nullptr;
    /** @brief Requested destination. */
    double x = 0.0;
    double y = 0.0;
    /** @brief Whether the authored step carried a `duration` field at all. */
    bool hasDuration = false;
    /** @brief Authored travel time in seconds; only meaningful when `hasDuration`. */
    double duration = 0.0;
};

/**
 * @brief Host movement controller invoked by the `move` step.
 *
 * @ownership Borrowed callback; the binding keeps it alive for the whole session.
 * @thread Invoked on the session's owner thread, synchronously.
 * @reentrancy Must not re-enter the session that invoked it.
 *
 * Returning `Arrived` completes the step inline, so a host whose movement is
 * instantaneous — teleport, snap, an already-satisfied target — needs no host
 * loop at all. Returning `Travelling` suspends the story until the host
 * acknowledges the step. Returning `Unreachable` fails the step with a
 * diagnostic naming the actor and the destination, and the session keeps its
 * cursor so the caller can decide whether to retry or abandon. A failed
 * `Result` is reported verbatim instead, so a reason such as "the navigation
 * service is not loaded" is never swallowed.
 */
using StoryMoveHandler = std::function<eve::Result<StoryMoveStatus>(const StoryMoveRequest&)>;

/** @brief Terminal state of one requested animation playback. */
enum class StoryAnimationStatus : std::uint8_t {
    /** @brief The clip was applied instantaneously, so the step completes inline. */
    Finished,
    /** @brief Playback was accepted and is still running; the host acknowledges the step when it ends. */
    Playing,
    /** @brief The target or the clip cannot be played, so the `animation` step fails. */
    Unavailable,
};

/**
 * @brief One `animation` step request, expressed in the host's own animation space.
 *
 * `clip` is an opaque locator owned by the host — a skeletal clip id, a sprite
 * sequence name, a Spine animation, a montage section. The engine never resolves
 * it, never loads it and never assumes a rig or a clip format, so one authored
 * `.dnut` story is not tied to one animation stack; the meaning is settled by
 * whoever installs the handler.
 *
 * `loop` and `hold` are carried verbatim as authored and are never interpreted
 * here. `loop` asks for indefinite repetition; `hold` asks playback to stay on
 * its final pose once the clip ends. A host whose animation stack has no such
 * concept simply ignores the flags it does not implement.
 *
 * @thread Filled and consumed synchronously inside one step dispatch.
 */
struct StoryAnimationRequest {
    /** @brief Authored `target=<id>` verbatim; the host's own subject space. */
    std::string targetId;
    /**
     * @brief Target resolved through the binding, or nullptr.
     *
     * The same convenience projection as `StoryMoveRequest::actor`: it is
     * non-null exactly when `RpgStoryBinding::resolveActor` or `party` resolved
     * `targetId`, and visibly null when the id belongs to another subject space
     * (a map prop, a camera, a UI element). A host must not read a null here as
     * "the target does not exist" — use `targetId` and resolve it yourself.
     */
    RPGActor* actor = nullptr;
    /** @brief Authored `clip=<uri>` verbatim; opaque to the engine. */
    std::string clip;
    /** @brief Authored `loop` flag; false when the step omits it. */
    bool loop = false;
    /** @brief Authored `hold` flag; false when the step omits it. */
    bool hold = false;
};

/**
 * @brief Host animation controller invoked by the `animation` step.
 *
 * @ownership Borrowed callback; the binding keeps it alive for the whole session.
 * @thread Invoked on the session's owner thread, synchronously.
 * @reentrancy Must not re-enter the session that invoked it.
 *
 * Returning `Finished` completes the step inline, so a host whose playback is
 * instantaneous — an applied pose, a zero-length clip, a purely cosmetic flicker
 * — needs no host loop at all. Returning `Playing` suspends the story until the
 * host acknowledges the step. Returning `Unavailable` fails the step with a
 * diagnostic naming the target and the clip, and the session keeps its cursor so
 * the caller can decide whether to retry or abandon. A failed `Result` is
 * reported verbatim instead, so a reason such as "the clip library is still
 * streaming" is never swallowed.
 */
using StoryAnimationHandler = std::function<eve::Result<StoryAnimationStatus>(const StoryAnimationRequest&)>;

/**
 * @brief Borrowed domain objects an RPG story step runs against.
 *
 * Every pointer is borrowed: the caller keeps the objects alive for as long as a
 * session may step. A null pointer makes the steps that need it fail with an
 * explicit diagnostic instead of silently doing nothing.
 *
 * @thread Configured and used on the owning simulation thread.
 */
struct RpgStoryBinding {
    /** @brief Owner of switches, variables and per-story completion facts. */
    GameState* gameState = nullptr;
    /** @brief Party used to resolve `actor=<memberId>` when no custom resolver is set. */
    Party* party = nullptr;
    /** @brief Bag used by `item` and `equipment` steps, or nullptr when unavailable. */
    eve::inventory::Bag* bag = nullptr;
    /** @brief Equipment set used by `equipment` steps, or nullptr when unavailable. */
    eve::inventory::EquipmentSet* equipment = nullptr;
    /**
     * @brief Optional actor resolver that overrides `party` member lookup.
     *
     * Returning nullptr makes the step fail, which keeps a typo in authored
     * content observable instead of silently skipping the effect.
     */
    std::function<RPGActor*(const std::string& actorId)> resolveActor;
    /**
     * @brief Optional movement controller for `move` steps.
     *
     * When this is empty the `move` step keeps its host-presented contract: the
     * story suspends, the host reads the payload and calls
     * `RpgStorySession::advance()`. That is the escape hatch for hosts whose
     * movement cannot be started by one call — for example a host that wants to
     * route the move through its own command queue.
     *
     * When it is set, the handler is the single place that knows how actors
     * move, which is what keeps the dialect free of any game-mode assumption.
     *
     * @ownership Borrowed; the session stores a pointer to `RpgStoryBinding` and
     *            never copies the callback.
     */
    StoryMoveHandler moveActor;
    /**
     * @brief Optional animation controller for `animation` steps.
     *
     * When this is empty the `animation` step keeps its host-presented contract:
     * the story suspends, the host reads the payload and calls
     * `RpgStorySession::advance()`. That is the escape hatch for hosts whose
     * playback cannot be started by one call — for example a host that wants to
     * blend the request into a state machine on its own schedule.
     *
     * When it is set, the handler is the single place that knows how clips play,
     * which is what keeps the dialect free of any animation-stack assumption.
     *
     * @ownership Borrowed; the session stores a pointer to `RpgStoryBinding` and
     *            never copies the callback.
     */
    StoryAnimationHandler playAnimation;
};

/**
 * @brief Process-local step vocabulary for the RPG story dialect.
 *
 * The registry owns the descriptors (field schema and domain invariants) and the
 * handlers for every domain step. Handlers read their per-run domain objects from
 * `StepContext::host`, which must point at an `RpgStoryBinding`.
 *
 * @return Borrowed registry owned by this module for the process lifetime.
 * @ownership Borrowed; never destroy or move it.
 * @thread First call initializes the registry; call it on one thread before others use it.
 */
[[nodiscard]] eve::dnut::StepKindRegistry& rpgStoryStepRegistry();

/**
 * @brief Strict process-local catalogue of `.dnut` story assets.
 *
 * Replacement is atomic: a document with any error leaves the previous
 * catalogue untouched, so a broken hot reload cannot take down live content.
 *
 * @thread Owner thread only; no synchronization is performed.
 */
class RpgStoryCatalogue {
public:
    /**
     * @brief Compile and atomically publish every `story` block of one document.
     * @param source Full `.dnut` text; it is not retained.
     * @param path Source identity reported in diagnostics.
     * @return Published story count, or a `ParseError` diagnostic listing the
     *         first problem; the previous catalogue is preserved on failure.
     * @cost Linear in the document size.
     */
    [[nodiscard]] static eve::Result<int> replaceFromDnutStrict(const std::string& source, const std::string& path);
    /** @brief Discard every published story. */
    static void clear();
    /** @brief Return the published story count. */
    static int count();
    /** @brief Return whether an exact story id is published. */
    static bool contains(const std::string& storyId);
    /** @brief Return every published story id in lexical order. */
    [[nodiscard]] static std::vector<std::string> storyIds();
};

/**
 * @brief Caller-owned cursor over one published `.dnut` story.
 *
 * The session snapshots the whole catalogue when it begins, so a later hot
 * replacement cannot invalidate an active run. Persistent facts live in
 * `RpgStoryBinding::gameState`: a non-repeatable story records completion under
 * the self-variable scope `story.<id>` and refuses to restart once complete.
 * Full cursor persistence is available through `captureState` / `restoreState`.
 *
 * @thread Owner thread only. @reentrancy No callbacks into the session.
 */
class RpgStorySession {
public:
    RpgStorySession();
    ~RpgStorySession();
    RpgStorySession(RpgStorySession&&) noexcept;
    RpgStorySession& operator=(RpgStorySession&&) noexcept;
    RpgStorySession(const RpgStorySession&) = delete;
    RpgStorySession& operator=(const RpgStorySession&) = delete;

    /**
     * @brief Start a published story and run it to its first suspension point.
     * @param storyId Exact published story id.
     * @param binding Borrowed domain objects; may be null for presentation-only stories.
     * @return Success, or a structured failure that leaves this session stopped.
     */
    [[nodiscard]] eve::Result<void> begin(const std::string& storyId, RpgStoryBinding* binding);

    /**
     * @brief Start a story against the standard RPG objects.
     *
     * Convenience overload for hosts that do not need a custom actor resolver.
     * The session keeps its own `RpgStoryBinding` filled from these arguments, so
     * the caller only has to keep the domain objects themselves alive.
     *
     * @param storyId Exact published story id.
     * @param gameState Switches, variables and completion facts; may be null.
     * @param party Actor lookup for `actor=<memberId>`; may be null.
     * @param bag Bag used by `item` / `equipment`; may be null.
     * @param equipment Equipment set used by `equipment`; may be null.
     */
    [[nodiscard]] eve::Result<void> begin(const std::string& storyId, GameState* gameState, Party* party,
                                          eve::inventory::Bag* bag, eve::inventory::EquipmentSet* equipment);

    /**
     * @brief Acknowledge the presented `move` / `animation` / `select` / `dialogue`
     *        / `message` / `camera` / `wait` step and continue.
     * @remarks A `move` step is acknowledged the same way whether it was
     *          host-presented or suspended by `RpgStoryBinding::moveActor`
     *          reporting `StoryMoveStatus::Travelling`: call `advance` once the
     *          actor has actually arrived. A handler that reports `Arrived`
     *          never suspends the story at all.
     */
    [[nodiscard]] eve::Result<void> advance();

    /** @brief Answer a blocked `choice` step by its authored label. */
    [[nodiscard]] eve::Result<void> select(const std::string& routeLabel);

    /** @brief Resume a step whose handler suspended the story. */
    [[nodiscard]] eve::Result<void> resume(eve::Value result);

    /** @brief Stop and clear the session. */
    void stop();

    /** @brief Whether a story is loaded. */
    [[nodiscard]] bool isActive() const;
    /** @brief Whether the story is suspended and awaiting acknowledgement. */
    [[nodiscard]] bool isBlocked() const;
    /** @brief Whether the suspension expects `resume` rather than `advance`. */
    [[nodiscard]] bool isWaitingStep() const;
    /** @brief Owning copy of the running story id, or an empty string. */
    [[nodiscard]] std::string getStoryId() const;
    /** @brief Current step type (`move`, `choice`, `wait`, …), or an empty string. */
    [[nodiscard]] std::string getStepKind() const;
    /**
     * @brief Current step payload.
     * @return Borrowed reference; an empty object while no step is current.
     * @lifetime Invalidated by the next session transition.
     */
    [[nodiscard]] const eve::Value& getStepPayload() const;
    /** @brief Authored labels of the current `choice` step, empty for other step types. */
    [[nodiscard]] std::vector<std::string> getChoiceLabels() const;

    /** @brief Capture the complete cursor for persistence. */
    [[nodiscard]] eve::Result<void> captureState(eve::Value& out) const;
    /**
     * @brief Restore a captured cursor into a fresh session.
     * @param storyId Root story id the cursor was captured from.
     * @param in Value previously produced by `captureState`.
     * @param binding Borrowed domain objects; the runtime does not retain them across turns.
     * @return Success, or a structured failure that leaves this session stopped.
     */
    [[nodiscard]] eve::Result<void> restoreState(const std::string& storyId, const eve::Value& in,
                                                 RpgStoryBinding* binding);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::rpg
