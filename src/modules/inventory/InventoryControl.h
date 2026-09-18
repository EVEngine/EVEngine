#pragma once

#include "common/GameplayControl.h"
#include "common/GameplayInstanceCatalog.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eve::inventory {

/**
 * @brief Exposes the module's published player inventories through the shared
 *        gameplay command protocol.
 *
 * Until now the inventory domain had no player-equivalent agent surface at all:
 * `eve_gameplay` could not reach it, so an unattended agent had to fall back to
 * `eve_eval` against bag internals. The action vocabulary here is the module's
 * own vocabulary; add/remove/split/equip/unequip map one-to-one onto `Bag` and
 * `EquipmentSet` operations, not onto a new write path invented by the adapter.
 *
 * One adapter serves every published instance, because the shared router requires
 * exactly one provider per domain: a per-instance listener would make the second
 * published inventory unusable for everyone.
 *
 * Authority: `inventory:add-item` is a grant, so it is refused for
 * `GameplayAccess::PlayerEquivalent` and only accepted for the test-driver and
 * developer-cheat profiles. Every other action is player-equivalent and requires
 * the session to control the instance owner.
 *
 * Partial effects stay observable: `inventory:remove-item` keeps the `Bag`
 * take-up-to-N contract, so a request for more than the container holds applies
 * what it can and reports the applied amount as `quantity` in the receipt and
 * event instead of silently claiming the full requested amount.
 *
 * @ownership Published bags and equipment sets are borrowed authorities that must
 *            outlive their publication; the adapter copies no inventory state and
 *            keeps only per-instance revision and event ledgers. `clear()` drops
 *            the publications without touching the containers.
 * @thread Owner-simulation thread only, like every gameplay provider.
 * @reentrancy No method invokes callbacks; `submitGameplay` mutates the borrowed
 *             bag/equipment directly and appends to its own event ledger.
 */
class InventoryControl final : public IGameplayControlProvider, public IGameplayInstanceCatalog {
public:
    /** @brief Create an unpublished adapter and register it as the inventory domain. */
    InventoryControl();
    /** @brief Unpublish every instance without destroying borrowed authorities. */
    ~InventoryControl() override;

    InventoryControl(const InventoryControl&)            = delete;
    InventoryControl& operator=(const InventoryControl&) = delete;

    /**
     * @brief Publish one borrowed inventory instance under a stable identity.
     * @param instance Stable identity of the instance, a canonical persistent id.
     * @param owner Stable identity of the player/actor controlling it.
     * @param bag Borrowed container observed and mutated by this adapter.
     * @param equipment Borrowed equipment set, or null when the instance has none.
     * @return Success, or `Conflict` when that instance is already published.
     * @ownership The publication does not take ownership of `bag` or `equipment`.
     */
    [[nodiscard]] Result<void> publish(SubjectRef instance, SubjectRef owner, Bag& bag, EquipmentSet* equipment);
    /** @brief Drop one publication; `NotFound` when that instance is not published. */
    [[nodiscard]] Result<void> unpublish(SubjectRef instance);
    /** @brief Drop every publication without destroying borrowed containers. */
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
        Bag*                       bag               = nullptr;
        EquipmentSet*              equipment         = nullptr;
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
    [[nodiscard]] Entry*       find(SubjectRef instance);
    /** @copydoc find — const overload returns a borrowed read-only record. */
    [[nodiscard]] const Entry* find(SubjectRef instance) const;
    /** @brief Player-equivalent sessions must control the owner; other profiles pass. */
    [[nodiscard]] bool controls(const GameplaySession& session, const Entry& entry) const;
    /** @brief Owning observation of the bag contents and capacity. */
    [[nodiscard]] Value bagState(const Entry& entry) const;
    /** @brief Owning observation of the equipment slots, empty without a set. */
    [[nodiscard]] Value equipmentState(const Entry& entry) const;

    std::vector<Entry> entries_;
};

}  // namespace eve::inventory
