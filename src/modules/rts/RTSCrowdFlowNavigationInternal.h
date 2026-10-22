#pragma once
#include <optional>
#include "crowd/Crowd.h"
#include "rts/RTS.h"
namespace eve::rts {
/** @brief Owner-thread derived shared static-goal navigation service; retains no ECS pointers. */
class CrowdFlowNavigation {
public:
    enum class InputChange { Unchanged, Changed };
    enum class DriveState { Pending, Ready, Unreachable, Unavailable };
    /** @brief Transient group member binding; RTS remains the membership authority. */
    struct GroupMember {
        ecs::EntityHandle unit;
        std::string       orderId;
    };
    /** @brief Owning group projection prepared once before the motion phase. */
    struct GroupInput {
        std::vector<GroupMember>   members;
        std::vector<WorldPosition> targets;
    };
    /** @brief Opaque owning steering selection, valid only for the captured Crowd input epoch. */
    class Route {
        friend class CrowdFlowNavigation;
        std::shared_ptr<const std::vector<crowd::FlowGoal>> sharedGoals_;
        crowd::FlowGoal                                     exactGoal_;
        WorldPosition                                       target_;
        std::uint64_t                                       owner_ = 0, input_ = 0;
    };
    /** @brief Atomically replace derived group bindings after validating the complete projection.
     * @ownership Consumes owning input, retains handles and immutable keys, no ECS pointers.
     * @thread Owner thread. @reentrancy No callbacks.
     * @cost Sorting per group and member lookup table, amortized once per group phase.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<void> replaceGroups(crowd::Crowd& crowd, std::vector<GroupInput> groups);
    /** @brief Collect the applicable group or exact-terminal request for one current order.
     * @ownership Borrows inputs only for this call; returned route owns its immutable key.
     * @thread Owner thread. @reentrancy No callbacks.
     * @cost Logarithmic member lookup and bounded cell lookup; no repeated group sorting.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<Route> requestRoute(crowd::Crowd& crowd, ecs::EntityHandle unit,
                                                                  std::string_view orderId, WorldPosition position,
                                                                  WorldPosition target, SimulationTick tick);
    /** @brief Steer a previously collected route without repeating target-set conversion.
     * @ownership Borrows route for this call; update receives an owning lease.
     * @thread Owner thread. @reentrancy No callbacks. @cost Bounded canonical job lookup per unit.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<DriveState> steerRoute(crowd::Crowd& crowd, WorldPosition position,
                                                                     const Route& route, float radius,
                                                                     crowd::MovementBinding movement,
                                                                     crowd::AgentUpdate&    update);
    /** @brief Store cheap owning quotas without preparing graph work. */
    explicit CrowdFlowNavigation(CrowdFlowNavigationSettings settings) : settings_(settings) {}
    /** @brief Observe policy enablement; owner thread, no callbacks or provider access. */
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    /** @brief Change cheap quotas without losing jobs or the owner/tick spending latch.
     * @ownership Stores values only. @thread Owner thread. @reentrancy No callbacks.
     * @return Applied/NoOp; caller validates quota ranges before entry.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<void> configure(CrowdFlowNavigationSettings settings) noexcept;
    /** @brief Disable policy and release jobs/leases while retaining the current spending boundary.
     * @thread Owner thread. @reentrancy No callbacks. No provider pointer is retained.
     */
    EVENGINE_API_DOMAINS void disable() noexcept;
    /** @brief Reconcile owner/input epochs and retire unused goals; owner thread, no callbacks. */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<InputChange> begin(crowd::Crowd& crowd, const SimulationStep& step);
    [[nodiscard]] EVENGINE_API_DOMAINS Result<void> request(crowd::Crowd& crowd, WorldPosition target,
                                                            SimulationTick tick);
    [[nodiscard]] EVENGINE_API_DOMAINS Result<void> pump(crowd::Crowd& crowd, const SimulationStep& step);
    [[nodiscard]] EVENGINE_API_DOMAINS Result<DriveState> steer(crowd::Crowd& crowd, WorldPosition position,
                                                                WorldPosition target, float radius,
                                                                crowd::MovementBinding movement,
                                                                crowd::AgentUpdate&    update);
    /** @brief Collect a canonical multi-slot target set into the same quota service.
     * @ownership Copies input values, retaining no spans. @thread Owner thread. @reentrancy No callbacks.
     * @cost Bounded goal conversion/sort; amortize per shared group request.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<void> request(crowd::Crowd& crowd, std::span<const WorldPosition> targets,
                                                            SimulationTick tick);
    /** @brief Select a multi-slot shared field while preserving this unit's exact destination.
     * @ownership Borrows inputs for this call; only an owning lease enters update.
     * @thread Owner thread. @reentrancy No callbacks.
     * @cost Bounded canonical key lookup; amortize at shared steering boundaries.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<DriveState> steer(crowd::Crowd& crowd, WorldPosition position,
                                                                std::span<const WorldPosition> targets,
                                                                WorldPosition exactTarget, float radius,
                                                                crowd::MovementBinding movement,
                                                                crowd::AgentUpdate&    update);
    /** @brief Observe bounded work metadata without field copies; owner thread only. */
    [[nodiscard]] EVENGINE_API_DOMAINS CrowdFlowNavigationObservation observation() const noexcept;
    /** @brief Owning deferred notification; stale handles are resolved at delivery. */
    struct Notice {
        ecs::EntityHandle unit;
        OrderRecord       order;
    };
    /** @brief Append an owning post-commit event; failure leaves this notice unpublished. */
    void queueNotice(ecs::EntityHandle unit, const OrderRecord& order) { notices_.push_back({unit, order}); }
    /** @brief Transfer committed notices to caller before invoking unknown callbacks.
     * @ownership Caller owns moved records/handles and resolves liveness before delivery.
     * @cost Constant-time buffer ownership transfer, amortized per motion phase; delivery is separate.
     */
    [[nodiscard]] std::vector<Notice> takeNotices() { return std::exchange(notices_, {}); }


private:
    struct GroupBinding {
        GroupMember                                         member;
        std::shared_ptr<const std::vector<crowd::FlowGoal>> goals;
        bool                                                reform = false;
        std::uint64_t                                       owner = 0, input = 0;
    };
    std::vector<GroupBinding> groups_;
    struct Job {
        std::uint64_t                sequence = 0;
        std::vector<crowd::FlowGoal> goals;
        SimulationTick               used;
        crowd::Crowd::FlowBuildPlan  plan;
        crowd::FlowLease             lease;
    };
    [[nodiscard]] EVENGINE_API_DOMAINS Result<crowd::FlowGoal> goalCell(crowd::Crowd& crowd,
                                                                        WorldPosition target) const;
    /** @brief Own normalized slot coordinates; rejects invalid input before changing work state.
     * @ownership Returns copied coordinates. @thread Owner thread. @reentrancy No callbacks.
     * @cost Bounded conversion and sorting per target set, amortized per shared request.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<crowd::FlowGoal>> goalSet(
        crowd::Crowd& crowd, std::span<const WorldPosition> targets) const;
    [[nodiscard]] EVENGINE_API_DOMAINS Result<void> requestGoals(std::span<const crowd::FlowGoal> goals,
                                                                 SimulationTick                   tick);
    [[nodiscard]] EVENGINE_API_DOMAINS Result<DriveState> steerGoals(crowd::Crowd& crowd, WorldPosition position,
                                                                     std::span<const crowd::FlowGoal> goals,
                                                                     WorldPosition target, float radius,
                                                                     crowd::MovementBinding movement,
                                                                     crowd::AgentUpdate&    update);
    CrowdFlowNavigationSettings                           settings_;
    bool                                                  enabled_ = true;
    std::vector<Job>                                      jobs_;
    std::vector<Notice>                                   notices_;
    std::optional<SimulationTick>                         frame_, pumped_;
    std::uint64_t                                         owner_ = 0, input_ = 0;
    std::uint64_t                                         nextSequence_ = 1, workAfter_ = 0, roundThrough_ = 0;
    CrowdFlowNavigationObservation                        stats_;
};
}  // namespace eve::rts
