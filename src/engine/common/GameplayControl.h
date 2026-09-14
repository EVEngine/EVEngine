#pragma once

/**
 * @file GameplayControl.h
 * @brief Host-neutral player-equivalent command and observation protocol.
 */

#include "common/Identity.h"
#include "common/Result.h"
#include "common/SubjectRef.h"
#include "common/Time.h"
#include "common/Value.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eve {

/** @brief Authority profile applied by a gameplay provider before domain validation. */
enum class GameplayAccess : std::uint8_t { PlayerEquivalent, TestDriver, DeveloperCheat };

/**
 * @brief Owning authorization context shared by player, script and automation adapters.
 *
 * The host owns this value. Providers retain no reference to it. Controlled subjects
 * are stable identities, not ECS handles; every provider resolves them at each call.
 */
struct GameplaySession {
    std::string             id;
    GameplayAccess          access = GameplayAccess::PlayerEquivalent;
    std::vector<SubjectRef> controlledSubjects;
};

/** @brief One discoverable domain action and its owning schema projection. */
struct GameplayActionDescriptor {
    LogicalId id;
    Value     parameterSchema;
};

/** @brief Immutable owning observation of one authoritative gameplay instance. */
struct GameplayObservation {
    LogicalId       domain;
    SubjectRef      instance;
    SimulationTick tick = SimulationTick::zero();
    std::uint64_t   revision = 0;
    Value           state;
};

/** @brief One player-semantic command submitted to a domain authority. */
struct GameplayCommand {
    std::string     id;
    LogicalId       action;
    SubjectRef      subject;
    SimulationTick observedTick = SimulationTick::zero();
    std::uint64_t   expectedRevision = 0;
    Value           parameters;
};

/** @brief Owning evidence that a gameplay authority accepted one command. */
struct GameplayCommandReceipt {
    std::string     commandId;
    std::string     executionId;
    SimulationTick acceptedTick = SimulationTick::zero();
    std::uint64_t   resultingRevision = 0;
    Value           details;
};

/** @brief Owning event projection safe across frames and process boundaries. */
struct GameplayEvent {
    std::uint64_t   sequence = 0;
    SimulationTick tick = SimulationTick::zero();
    std::string     type;
    SubjectRef      subject;
    std::string     causationCommandId;
    std::string     correlationId;
    Value           payload;
};

/**
 * @brief Multi-provider gameplay boundary consumed by input, scripts and automation.
 *
 * Implementations are registered as Capability listeners because several gameplay
 * domains may coexist. Registration and every method are owner-simulation-thread
 * affine. Implementations must not retain argument references or invoke unknown
 * callbacks. Domain objects remain the only mutable authorities; returned Values are
 * owning projections. Missing providers are observable through an empty listener set.
 */
class IGameplayControlProvider {
public:
    static constexpr const char* capabilityName = "IGameplayControlProvider";
    virtual ~IGameplayControlProvider() = default;

    /** @brief Stable provider domain used for discovery and routing. */
    [[nodiscard]] virtual std::string_view gameplayDomain() const noexcept = 0;
    /** @brief Observe one provider-owned gameplay instance without mutation. */
    [[nodiscard]] virtual Result<GameplayObservation> observeGameplay(const GameplaySession& session,
                                                                       SubjectRef instance) const = 0;
    /** @brief Discover currently legal player-semantic actions for one subject. */
    [[nodiscard]] virtual Result<std::vector<GameplayActionDescriptor>> availableGameplayActions(
        const GameplaySession& session, SubjectRef instance, SubjectRef subject) const = 0;
    /** @brief Validate authority and atomically submit one domain command. */
    [[nodiscard]] virtual Result<GameplayCommandReceipt> submitGameplay(const GameplaySession& session,
                                                                         SubjectRef instance,
                                                                         const GameplayCommand& command) = 0;
    /** @brief Advance one provider-owned instance using injected deterministic time. */
    [[nodiscard]] virtual Result<GameplayObservation> advanceGameplay(const GameplaySession& session,
                                                                       SubjectRef instance,
                                                                       const SimulationStep& step) = 0;
    /** @brief Return events strictly newer than an instance-local sequence cursor. */
    [[nodiscard]] virtual Result<std::vector<GameplayEvent>> gameplayEvents(const GameplaySession& session,
                                                                             SubjectRef instance,
                                                                             std::uint64_t afterSequence) const = 0;
};

}  // namespace eve
