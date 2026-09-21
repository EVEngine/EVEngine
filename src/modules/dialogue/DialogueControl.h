#pragma once

#include "common/GameplayControl.h"
#include "common/GameplayInstanceCatalog.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::dialogue {

class DialogueFlow;

/**
 * @brief Exposes the module's conversation runner through the shared gameplay protocol.
 *
 * A conversation that waits for a player choice is one of the few states an
 * unattended agent cannot diagnose from the outside: the runner is authoritative
 * and the game's HUD is only a projection of it. This adapter publishes that
 * runner so an agent can observe the current node, speaker, text and route
 * vocabulary, and then play the conversation with the module's own operations
 * (`startChecked`/`advanceChecked`/`select`) instead of calling script
 * internals.
 *
 * `DialogueFlow` runs exactly one conversation at a time, so the domain publishes
 * at most one instance: a second `publish` is refused with `Conflict` rather than
 * handing two identities the same runner.
 *
 * A conversation started through automation has no Squirrel call frame, so it
 * starts with an empty binding table. Conditions that read bindings therefore
 * resolve as absent, which is disclosed here and in the returned observation
 * rather than guessed.
 *
 * @ownership The flow is a borrowed authority owned by the module; the adapter
 *            keeps only the publication identity, its own revision and event
 *            ledger, and never owns conversation state.
 * @thread Owner-simulation thread only, like every gameplay provider.
 * @reentrancy `submitGameplay` invokes the module's own operations, which may
 *             synchronously run the game's configured command handlers; those
 *             must not re-enter this adapter.
 */
class DialogueControl final : public IGameplayControlProvider, public IGameplayInstanceCatalog {
public:
    /**
     * @brief Create an adapter for one borrowed conversation runner.
     * @param flow Borrowed module facade that must outlive this adapter.
     */
    explicit DialogueControl(DialogueFlow& flow);
    /** @brief Unpublish without touching the borrowed runner. */
    ~DialogueControl() override;

    DialogueControl(const DialogueControl&)            = delete;
    DialogueControl& operator=(const DialogueControl&) = delete;

    /**
     * @brief Publish the conversation runner under a stable instance identity.
     * @param instance Stable identity of the instance, a canonical persistent id.
     * @param owner Stable identity of the player/actor that controls it.
     * @return Success, or `Conflict` when the runner is already published.
     */
    [[nodiscard]] Result<void> publish(SubjectRef instance, SubjectRef owner);
    /** @brief Drop the publication; `NotFound` when that instance is not published. */
    [[nodiscard]] Result<void> unpublish(SubjectRef instance);
    /** @brief Drop every publication. */
    void clear();
    /** @brief Number of published instances (0 or 1). */
    [[nodiscard]] int count() const;
    /** @brief Published instance identities, in publication order. */
    [[nodiscard]] std::vector<SubjectRef> instances() const;

    /** @copydoc IGameplayControlProvider::gameplayDomain */
    [[nodiscard]] std::string_view gameplayDomain() const noexcept override;
    /** @copydoc IGameplayInstanceCatalog::gameplayInstances */
    [[nodiscard]] std::vector<SubjectRef> gameplayInstances() const override;
    /** @copydoc IGameplayControlProvider::observeGameplay */
    [[nodiscard]] Result<GameplayObservation> observeGameplay(const GameplaySession& session,
                                                              SubjectRef             instance) const override;
    /** @copydoc IGameplayControlProvider::availableGameplayActions */
    [[nodiscard]] Result<std::vector<GameplayActionDescriptor>> availableGameplayActions(
        const GameplaySession& session, SubjectRef instance, SubjectRef subject) const override;
    /** @copydoc IGameplayControlProvider::submitGameplay */
    [[nodiscard]] Result<GameplayCommandReceipt> submitGameplay(const GameplaySession& session, SubjectRef instance,
                                                                const GameplayCommand& command) override;
    /** @copydoc IGameplayControlProvider::advanceGameplay */
    [[nodiscard]] Result<GameplayObservation> advanceGameplay(const GameplaySession& session, SubjectRef instance,
                                                              const SimulationStep& step) override;
    /** @copydoc IGameplayControlProvider::gameplayEvents */
    [[nodiscard]] Result<std::vector<GameplayEvent>> gameplayEvents(const GameplaySession& session, SubjectRef instance,
                                                                    std::uint64_t afterSequence) const override;

private:
    /** @brief The single publication: identity, revision ledger and event buffer. */
    struct Entry {
        SubjectRef                 instance;
        SubjectRef                 owner;
        SimulationTick             tick              = SimulationTick::zero();
        std::uint64_t              revision          = 0;
        std::uint64_t              nextEventSequence = 1;
        std::vector<GameplayEvent> events;
    };

    /** @brief Whether this adapter currently holds a publication. */
    [[nodiscard]] bool published() const { return entry_.has_value(); }
    /** @brief Player-equivalent sessions must control the owner; other profiles pass. */
    [[nodiscard]] bool controls(const GameplaySession& session) const;
    /** @brief Owning observation of the runner's current node and route vocabulary. */
    [[nodiscard]] Value conversationState() const;
    /** @brief Append one event to the ledger and bump the revision. */
    void record(Entry& entry, const GameplayCommand& command, std::string type, std::string detail,
                std::int64_t quantity);

    DialogueFlow*        flow_ = nullptr;
    std::optional<Entry> entry_;
};

}  // namespace eve::dialogue
