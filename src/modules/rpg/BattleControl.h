#pragma once

/**
 * @file BattleControl.h
 * @brief Player-equivalent, host-neutral control adapter for one RPG Battle.
 */

#include "common/GameplayControl.h"

#include <unordered_map>

namespace eve::rpg {

class Battle;
class RPGActor;

/**
 * @brief Explicit stable-identity adapter over one caller-owned RPG battle.
 *
 * Battle remains the only mutable combat authority. The caller owns this adapter
 * and must destroy it before the borrowed Battle or any bound actor. Binding is
 * explicit because legacy RPGActor instances do not themselves own persistent IDs.
 * Registration and calls are confined to the battle's simulation thread.
 */
class BattleControl final : public IGameplayControlProvider {
public:
    /** @brief Construct an adapter over a borrowed battle and stable instance identity. */
    BattleControl(Battle& battle, SubjectRef instance);
    /** @brief Unpublish the capability without destroying the borrowed battle. */
    ~BattleControl() override;

    BattleControl(const BattleControl&) = delete;
    BattleControl& operator=(const BattleControl&) = delete;

    /** @brief Bind a stable subject to an existing battle participant. @return Applied or validation failure. */
    [[nodiscard]] Result<void> bindParticipant(SubjectRef subject, RPGActor* actor);

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
    [[nodiscard]] RPGActor* resolve(SubjectRef subject) const;
    [[nodiscard]] SubjectRef subjectOf(RPGActor* actor) const;
    [[nodiscard]] bool controls(const GameplaySession& session, SubjectRef subject) const;

    Battle& battle_;
    SubjectRef instance_;
    SimulationTick tick_ = SimulationTick::zero();
    std::uint64_t revision_ = 0;
    std::uint64_t nextEventSequence_ = 1;
    std::unordered_map<SubjectRef, RPGActor*> actors_;
    std::unordered_map<RPGActor*, std::string> lastCommandByActor_;
    std::vector<GameplayEvent> events_;
};

}  // namespace eve::rpg
