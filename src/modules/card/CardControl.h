#pragma once

#include "common/GameplayControl.h"
#include "common/GameplayInstanceCatalog.h"
#include "common/ResourceAccount.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eve::card {

class Card;
class Hand;

/**
 * @brief Exposes published card hands through the shared gameplay protocol.
 *
 * The card module owns definitions, ECS-backed cards, hands, zones, decks and the
 * transactional play path, but none of it was reachable from `eve_gameplay`: an
 * unattended agent could only poke script internals. The vocabulary here is the
 * module's own — `card:draw` calls the module's deck-to-hand draw, `card:play`
 * goes through `Card::play` (condition, container and payment transaction), and
 * `card:set-attribute` is exposed as a cheat for the test-driver and
 * developer-cheat profiles because it rewrites canonical combat attributes.
 *
 * Playing a card needs the game's authoritative money account, which the adapter
 * cannot invent: a publication may bind a borrowed `IResourceAccount`. Without
 * one, `card:play` is neither advertised nor accepted (`Unsupported`), and the
 * observation reports `"payment":"unbound"` so the gap is visible rather than
 * silently missing.
 *
 * One adapter serves every published hand (the shared router requires exactly one
 * provider per domain), so several players can be observed at once.
 *
 * @ownership Published hands and accounts are borrowed authorities that must
 *            outlive their publication; the adapter keeps only identity,
 *            revision and its own event ledger.
 * @thread Owner-simulation thread only, like every gameplay provider.
 * @reentrancy No method invokes callbacks; `submitGameplay` calls the module's
 *             own synchronous operations and appends to its event ledger.
 */
class CardControl final : public IGameplayControlProvider, public IGameplayInstanceCatalog {
public:
    /**
     * @brief Create an unpublished adapter.
     * @param module Borrowed card facade that must outlive this adapter.
     */
    explicit CardControl(Card& module);
    /** @brief Unpublish every hand without touching the borrowed authorities. */
    ~CardControl() override;

    CardControl(const CardControl&)            = delete;
    CardControl& operator=(const CardControl&) = delete;

    /**
     * @brief Publish one borrowed hand under a stable instance identity.
     * @param instance Stable identity of the instance, a canonical persistent id.
     * @param owner Stable identity of the player/actor controlling it.
     * @param hand Borrowed hand observed and mutated by this adapter.
     * @param account Borrowed payment account, or null when the game has none.
     * @return Success, or `Conflict` when that instance is already published.
     */
    [[nodiscard]] Result<void> publish(SubjectRef instance, SubjectRef owner, Hand& hand,
                                       eve::resource::IResourceAccount* account);
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
        SubjectRef                       instance;
        SubjectRef                       owner;
        Hand*                            hand              = nullptr;
        eve::resource::IResourceAccount* account           = nullptr;
        SimulationTick                   tick              = SimulationTick::zero();
        std::uint64_t                    revision          = 0;
        std::uint64_t                    nextEventSequence = 1;
        std::vector<GameplayEvent>       events;
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
    /** @brief Owning observation of the hand, deck size and payment binding. */
    [[nodiscard]] Value handState(const Entry& entry) const;

    Card*              module_ = nullptr;
    std::vector<Entry> entries_;
};

}  // namespace eve::card
