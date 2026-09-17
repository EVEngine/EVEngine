#pragma once

/**
 * @file RpgDialect.h
 * @brief RPG vocabulary for the `.dnut` sequence language, plus its catalogue and session.
 */

#include "common/Result.h"
#include "common/Value.h"

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
