#pragma once

/**
 * @file ClimbingControl.h
 * @brief Player-equivalent control adapter for one authoritative climbing runtime.
 */

#include "climbing/Climbing.h"
#include "common/GameplayControl.h"

namespace eve::climbing {

/**
 * @brief Exposes server-probed climbing actions without accepting authored world targets.
 *
 * Runtime, physics world and pose remain borrowed authoritative collaborators.
 * They must outlive this owner-thread-affine adapter. Commands carry only
 * player semantics; begin always re-probes the borrowed authoritative world.
 */
class ClimbingControl final : public IGameplayControlProvider {
public:
    /**
     * @brief Construct an adapter over one runtime, world and authoritative character pose.
     * @param instance Stable identity of this climbing-control instance.
     * @param character Stable player-controllable character identity.
     * @param runtime Borrowed authoritative climbing lifecycle.
     * @param world Borrowed physics world used synchronously for probing and movement.
     * @param pose Borrowed server-owned pose used synchronously for every new probe.
     */
    ClimbingControl(SubjectRef instance, SubjectRef character, ClimbingRuntime& runtime,
                    physics::World3D& world, const ClimbingPose& pose);
    /** @brief Unpublish the capability without destroying borrowed authorities. */
    ~ClimbingControl() override;

    ClimbingControl(const ClimbingControl&) = delete;
    ClimbingControl& operator=(const ClimbingControl&) = delete;

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
    [[nodiscard]] Result<std::uint64_t> refreshRevision() const;
    [[nodiscard]] Result<void> captureEvents(std::string_view commandId);

    SubjectRef instance_;
    SubjectRef character_;
    ClimbingRuntime* runtime_ = nullptr;
    physics::World3D* world_ = nullptr;
    const ClimbingPose* pose_ = nullptr;
    SimulationTick tick_ = SimulationTick::zero();
    mutable std::uint64_t revision_ = 0;
    mutable std::string stateFingerprint_;
    std::uint64_t nextEventSequence_ = 1;
    std::vector<GameplayEvent> events_;
};

}  // namespace eve::climbing
