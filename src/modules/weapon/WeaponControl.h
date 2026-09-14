#pragma once

/**
 * @file WeaponControl.h
 * @brief Player-equivalent control adapter for one authoritative weapon action.
 */

#include "common/GameplayControl.h"
#include "weapon/WeaponTypes.h"

namespace eve::resource {
class IResourceAccount;
}
namespace eve::transaction {
class ITransactionParticipant;
}

namespace eve::weapon {

/**
 * @brief Exposes one weapon through the shared gameplay command protocol.
 *
 * The definition is copied at construction. The resource account and effect
 * participant remain authoritative borrowed collaborators and must outlive this
 * owner-thread-affine adapter. Firing is delegated to WeaponActionAdapter.
 */
class WeaponControl final : public IGameplayControlProvider {
public:
    /**
     * @brief Construct a control adapter for one copied definition and borrowed effect authorities.
     * @param instance Stable identity of this weapon-control instance.
     * @param wielder Stable player-controllable subject identity.
     * @param definition Owning weapon definition copied into the adapter.
     * @param account Borrowed authoritative resource account.
     * @param effect Borrowed authoritative weapon-effect transaction participant.
     */
    WeaponControl(SubjectRef instance, SubjectRef wielder, WeaponDefinition definition,
                  resource::IResourceAccount& account,
                  transaction::ITransactionParticipant& effect);
    /** @brief Unpublish the capability without destroying borrowed authorities. */
    ~WeaponControl() override;

    WeaponControl(const WeaponControl&) = delete;
    WeaponControl& operator=(const WeaponControl&) = delete;

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

    SubjectRef instance_;
    SubjectRef wielder_;
    WeaponDefinition definition_;
    resource::IResourceAccount* account_ = nullptr;
    transaction::ITransactionParticipant* effect_ = nullptr;
    SimulationTick tick_ = SimulationTick::zero();
    std::uint64_t revision_ = 1;
    std::uint64_t nextEventSequence_ = 1;
    std::vector<GameplayEvent> events_;
};

}  // namespace eve::weapon
