#pragma once

#include "common/GameplayControl.h"
#include "common/GameplayInstanceCatalog.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eve::economy {

/**
 * @brief Exposes published player resource ledgers through the shared gameplay protocol.
 *
 * The economy module already owned the authoritative ledger (`EconomySystem`), but
 * nothing bridged it to the gameplay protocol, so an unattended agent had to call
 * `eve_eval` on module internals to read or change a balance. The action vocabulary
 * here is the module's own: `economy:debit` is an ordinary spend, and
 * `economy:credit` is a grant, refused for `GameplayAccess::PlayerEquivalent` and
 * accepted only for the test-driver and developer-cheat profiles.
 *
 * Gathering stays with the game: `GatherNode`/`Collector` drive extraction over
 * simulated ticks, and re-exposing a cheaper "harvest now" shortcut here would be a
 * second write path for the same fact.
 *
 * One adapter serves every published instance (the shared router requires exactly
 * one provider per domain), so several players can be observed at once.
 *
 * @ownership The adapter stores only the integer player id, its own revision and
 *            event ledger, and the borrowed publication identity; the ledger itself
 *            stays owned by `EconomySystem`.
 * @thread Owner-simulation thread only, like every gameplay provider.
 * @reentrancy No method invokes callbacks; `submitGameplay` credits/debits the
 *             module ledger directly and appends to its own event ledger.
 */
class EconomyControl final : public IGameplayControlProvider, public IGameplayInstanceCatalog {
public:
    /** @brief Create an unpublished adapter and register it as the economy domain. */
    EconomyControl();
    /** @brief Unpublish every instance. */
    ~EconomyControl() override;

    EconomyControl(const EconomyControl&)            = delete;
    EconomyControl& operator=(const EconomyControl&) = delete;

    /**
     * @brief Publish one player ledger under a stable instance identity.
     * @param instance Stable identity of the instance, a canonical persistent id.
     * @param owner Stable identity of the player/actor controlling it.
     * @param player Module-level player id whose ledger this instance observes.
     * @return Success, or `Conflict` when that instance is already published.
     */
    [[nodiscard]] Result<void> publish(SubjectRef instance, SubjectRef owner, int player);
    /** @brief Drop one publication; `NotFound` when that instance is not published. */
    [[nodiscard]] Result<void> unpublish(SubjectRef instance);
    /** @brief Drop every publication. */
    void clear();
    /** @brief Number of published instances. */
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
    /** @brief Per-instance authority, revision ledger and event buffer. */
    struct Entry {
        SubjectRef                 instance;
        SubjectRef                 owner;
        int                        player            = 0;
        SimulationTick             tick              = SimulationTick::zero();
        std::uint64_t              revision          = 0;
        std::uint64_t              nextEventSequence = 1;
        std::vector<GameplayEvent> events;
    };

    /**
     * @brief Find one published instance record.
     * @param instance Instance identity to look up.
     * @return Borrowed entry owned by this adapter, or null when unpublished; the
     *         pointer is only valid until the next publish/unpublish/clear call.
     * @lifetime Never retained by the caller across a publication change.
     */
    [[nodiscard]] Entry* find(SubjectRef instance);
    /** @copydoc find — const overload returns a borrowed read-only record. */
    [[nodiscard]] const Entry* find(SubjectRef instance) const;
    /** @brief Player-equivalent sessions must control the owner; other profiles pass. */
    [[nodiscard]] bool controls(const GameplaySession& session, const Entry& entry) const;
    /** @brief Owning observation of every balance the player ledger knows about. */
    [[nodiscard]] Value ledgerState(const Entry& entry) const;

    std::vector<Entry> entries_;
};

}  // namespace eve::economy
