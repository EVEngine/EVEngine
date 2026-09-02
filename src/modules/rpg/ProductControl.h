#pragma once

/**
 * @file ProductControl.h
 * @brief Player-equivalent control adapter for persistent RPG product loops.
 */

#include "common/GameplayControl.h"

namespace eve::inventory {
class Bag;
}

namespace eve::rpg {

class GameState;
class Tracker;

/**
 * @brief Host-neutral adapter joining shop, quest-reward and world-loot transactions.
 *
 * GameState, Tracker and Bag remain independent authoritative owners. This
 * caller-owned adapter retains only borrowed pointers, an optimistic observation
 * fingerprint and owning event projections. Destroy it before any participant.
 */
class ProductControl final : public IGameplayControlProvider {
public:
    /** @brief Construct over borrowed product-loop authorities and a stable instance identity. */
    ProductControl(SubjectRef instance, GameState& gameState, Tracker& tracker,
                   inventory::Bag& bag);
    /** @brief Unpublish the capability without destroying borrowed authorities. */
    ~ProductControl() override;

    ProductControl(const ProductControl&) = delete;
    ProductControl& operator=(const ProductControl&) = delete;

    /** @copydoc IGameplayControlProvider::gameplayDomain */
    [[nodiscard]] std::string_view gameplayDomain() const noexcept override;
    /** @copydoc IGameplayControlProvider::observeGameplay */
    [[nodiscard]] Result<GameplayObservation> observeGameplay(const GameplaySession& session,
                                                               SubjectRef instance) const override;
    /** @copydoc IGameplayControlProvider::availableGameplayActions */
    [[nodiscard]] Result<std::vector<GameplayActionDescriptor>> availableGameplayActions(
        const GameplaySession& session, SubjectRef instance, SubjectRef subject) const override;
    /** @copydoc IGameplayControlProvider::submitGameplay */
    [[nodiscard]] Result<GameplayCommandReceipt> submitGameplay(const GameplaySession& session,
                                                                 SubjectRef instance,
                                                                 const GameplayCommand& command) override;
    /** @copydoc IGameplayControlProvider::advanceGameplay */
    [[nodiscard]] Result<GameplayObservation> advanceGameplay(const GameplaySession& session,
                                                               SubjectRef instance,
                                                               const SimulationStep& step) override;
    /** @copydoc IGameplayControlProvider::gameplayEvents */
    [[nodiscard]] Result<std::vector<GameplayEvent>> gameplayEvents(const GameplaySession& session,
                                                                     SubjectRef instance,
                                                                     std::uint64_t afterSequence) const override;

private:
    [[nodiscard]] bool controls(const GameplaySession& session) const;
    [[nodiscard]] Result<Value> stateProjection() const;
    [[nodiscard]] Result<std::uint64_t> refreshRevision() const;
    void publishEvent(std::string type, const GameplayCommand& command, Value payload);

    SubjectRef instance_;
    GameState* gameState_ = nullptr;
    Tracker* tracker_ = nullptr;
    inventory::Bag* bag_ = nullptr;
    SimulationTick tick_ = SimulationTick::zero();
    mutable std::uint64_t revision_ = 0;
    mutable std::string stateFingerprint_;
    std::uint64_t nextEventSequence_ = 1;
    std::vector<GameplayEvent> events_;
};

}  // namespace eve::rpg
