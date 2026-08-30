#pragma once

#include "common/Result.h"
#include "common/RuntimeHandle.h"
#include "npc_ai/NpcAi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace eve::npc_ai {

struct NavigationTicketTag {};
using NavigationTicket = RuntimeHandle<NavigationTicketTag>;

enum class NavigationPhase : std::uint8_t { Pending, Moving, Arrived, Unreachable, Cancelled };

struct NavigationRequest {
    AgentHandle           agent;
    std::array<double, 3> start{};
    std::array<double, 3> destination{};
    double                agentRadius      = 0.5;
    double                acceptanceRadius = 0.25;
    std::string           filter;
    std::uint64_t         requestedTick = 0;
};

struct NavigationProgress {
    NavigationPhase       phase = NavigationPhase::Pending;
    std::array<double, 3> desiredVelocity{};
    double                remainingDistance = 0.0;
};

/**
 * @brief Narrow asynchronous pathfinding and locomotion provider contract.
 * @thread Calls are simulation-thread-affine unless an implementation documents stronger guarantees.
 * @reentrancy Implementations must not call back into NpcAiWorld or NavigationTaskService.
 */
class INavigationProvider {
public:
    virtual ~INavigationProvider() = default;
    /** @brief Starts an owning backend request and returns its generation ticket. */
    [[nodiscard]] virtual Result<NavigationTicket> begin(const NavigationRequest& request) = 0;
    /** @brief Polls one ticket without blocking the simulation thread. */
    [[nodiscard]] virtual Result<NavigationProgress> poll(NavigationTicket ticket, std::uint64_t simulationTick) = 0;
    /**
     * @brief Unconditionally abandons local/backend work during task teardown.
     * @remarks This is a noexcept lifecycle primitive rather than a fallible gameplay operation;
     * after return the provider must never publish another result for the ticket.
     */
    virtual void abandon(NavigationTicket ticket) noexcept = 0;
};

/** @brief Builds a typed navigation request from an authored task and current agent projection. */
class INavigationRequestFactory {
public:
    virtual ~INavigationRequestFactory() = default;
    /** @brief Creates an owning request without mutating provider or world state. */
    [[nodiscard]] virtual Result<NavigationRequest> create(const TaskContext& context, const TaskSpec& task) const = 0;
};

/**
 * @brief `ITaskService` adapter for asynchronous move-to requests.
 * @remarks The adapter uniquely owns its provider and request factory. Active tickets
 * are keyed by agent, state and task identity. Destruction and stop abandon every
 * outstanding ticket, so provider results cannot target a later task incarnation.
 */
class NavigationTaskService final : public ITaskService {
public:
    /** @brief Validates and transfers both required owners into a task service. */
    [[nodiscard]] static Result<std::unique_ptr<NavigationTaskService>> create(
        std::unique_ptr<INavigationProvider> provider, std::unique_ptr<INavigationRequestFactory> requestFactory);
    ~NavigationTaskService() override;

    NavigationTaskService(const NavigationTaskService&)            = delete;
    NavigationTaskService& operator=(const NavigationTaskService&) = delete;

    /** @copydoc ITaskService::start */
    [[nodiscard]] Result<void> start(const TaskContext& context, const TaskSpec& spec,
                                     std::string& inOutMemoryJson) override;
    /** @copydoc ITaskService::tick */
    [[nodiscard]] Result<TaskStatus> tick(const TaskContext& context, const TaskSpec& spec,
                                          std::string& inOutMemoryJson) override;
    /** @copydoc ITaskService::stop */
    void stop(const TaskContext& context, const TaskSpec& spec, StopReason reason,
              std::string_view memoryJson) noexcept override;
    /** @brief Number of live backend tickets owned by this adapter. */
    [[nodiscard]] std::size_t outstandingCount() const noexcept { return active_.size(); }

private:
    struct TaskKey {
        AgentHandle agent;
        std::string state;
        std::string task;
        friend bool operator<(const TaskKey& left, const TaskKey& right) noexcept {
            return std::tuple{left.agent, left.state, left.task} < std::tuple{right.agent, right.state, right.task};
        }
    };

    NavigationTaskService(std::unique_ptr<INavigationProvider>       provider,
                          std::unique_ptr<INavigationRequestFactory> requestFactory);
    [[nodiscard]] static TaskKey key(const TaskContext& context, const TaskSpec& spec);

    std::unique_ptr<INavigationProvider>       provider_;
    std::unique_ptr<INavigationRequestFactory> requestFactory_;
    std::map<TaskKey, NavigationTicket>        active_;
};

}  // namespace eve::npc_ai
