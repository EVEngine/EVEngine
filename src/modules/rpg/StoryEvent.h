#pragma once

/** @file StoryEvent.h @brief Versioned, resumable presentation-event sequencing for RPG games. */

#include "common/Result.h"

#include <string>
#include <vector>

namespace eve::rpg {

class GameState;

/** @brief Closed set of UI-neutral presentation commands emitted by a story event. */
enum class StoryEventStepKind { Dialogue, Message, Wait, Move, Camera };

/** @brief One immutable presentation command owned by a story-event definition. */
struct StoryEventStep {
    StoryEventStepKind kind = StoryEventStepKind::Message;
    std::string        reference;
    std::string        actorId;
    double             x        = 0.0;
    double             y        = 0.0;
    double             duration = 0.0;
};

/** @brief One immutable, ordered and resumable story-event definition. */
struct StoryEventDefinition {
    std::string                 id;
    bool                        repeatable = false;
    std::vector<StoryEventStep> steps;
};

/** @brief Strict process-local catalogue for versioned story-event content. */
class StoryEventCatalogue {
public:
    /**
     * @brief Validate and atomically replace every story-event definition.
     * @param json UTF-8 `eve.rpg.story-events` version 1 document.
     * @return Committed event count, or a structured failure preserving the prior catalogue.
     * @remarks Unknown fields and invalid command shapes are rejected before publication.
     * @thread Call on the owning content/simulation thread; no synchronization is performed.
     * @reentrancy No callbacks or scripts are invoked.
     */
    [[nodiscard]] static eve::Result<int> replaceFromJsonStrict(const std::string& json);
    /** @brief Clear all definitions on the owning content thread without invoking callbacks. */
    static void clear();
    /** @brief Return the committed definition count on the owning content thread. */
    static int count();
    /** @brief Return whether an exact stable event id exists in the current catalogue. */
    static bool contains(const std::string& eventId);
    /**
     * @brief Find one definition for immediate synchronous inspection.
     * @return Borrowed nullable definition owned by the catalogue.
     * @lifetime Invalidated by clear() or successful replacement; do not retain across either call.
     * @thread Query on the owning content/simulation thread.
     */
    static const StoryEventDefinition* find(const std::string& eventId);
};

/**
 * @brief Caller-owned runtime cursor over a copied story-event definition.
 * @remarks GameState remains the sole owner of persistent cursor/completion facts. The session owns
 * its definition copy so catalogue hot replacement cannot invalidate an active event.
 */
class StoryEventSession {
public:
    /**
     * @brief Begin or resume an event from its authoritative GameState cursor.
     * @param eventId Exact registered event id.
     * @param gameState Borrowed persistent state owner used only for this synchronous call.
     * @return Applied/unchanged success, or a structured validation/conflict failure.
     * @remarks Non-repeatable completed events reject restart. Failed begin leaves this session and
     * GameState unchanged.
     * @thread Call on the GameState owning simulation thread.
     * @reentrancy No callbacks or scripts are invoked.
     */
    [[nodiscard]] eve::Result<void> begin(const std::string& eventId, GameState* gameState);

    /**
     * @brief Acknowledge the current presentation command and persist the next cursor atomically.
     * @param gameState Borrowed owner used to persist progress; must be the same logical save.
     * @return Remaining step count, or structured failure without cursor mutation.
     * @remarks Acknowledging the final command persists completion before the session becomes inactive.
     * @thread Call on the GameState owning simulation thread.
     * @reentrancy No callbacks or scripts are invoked.
     */
    [[nodiscard]] eve::Result<int> advance(GameState* gameState);

    /** @brief Return whether a current step is awaiting acknowledgement. */
    bool isActive() const;
    /** @brief Return whether this session acknowledged its final step. */
    bool isFinished() const;
    /** @brief Return the zero-based current cursor, or the step count after completion. */
    int getStepIndex() const;
    /** @brief Return the copied definition's total step count. */
    int getStepCount() const;
    /** @brief Return an owning copy of the current event id. */
    std::string getEventId() const;
    /** @brief Return the current command kind, or an empty string while inactive. */
    std::string getStepKind() const;
    /** @brief Return an owning copy of the current dialogue/message reference. */
    std::string getReference() const;
    /** @brief Return an owning copy of the current move actor id. */
    std::string getActorId() const;
    /** @brief Return the current move/camera X coordinate, or zero while inactive. */
    double getX() const;
    /** @brief Return the current move/camera Y coordinate, or zero while inactive. */
    double getY() const;
    /** @brief Return the current wait/motion duration in seconds, or zero while inactive. */
    double getDuration() const;

private:
    /**
     * @brief Borrow the current step from the session-owned definition.
     * @ownership Borrowed. @lifetime Invalidated by begin, advance, or session destruction.
     */
    const StoryEventStep* current() const noexcept;
    StoryEventDefinition  definition_;
    int                   cursor_   = 0;
    bool                  active_   = false;
    bool                  finished_ = false;
};

}  // namespace eve::rpg
