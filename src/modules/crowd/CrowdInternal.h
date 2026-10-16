#pragma once

#include "crowd/Crowd.h"
#include "crowd/CrowdSpatialIndex.h"
#include "crowd/CrowdWorkScheduler.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eve::crowd {
bool validAgentUpdate(const AgentUpdate& update);

/** @brief Copy validated goals into their unique canonical coordinates.
 * @return Owning coordinates or invalid input. @ownership Retains no caller span.
 * @thread Owner thread. @reentrancy No callbacks.
 * @cost Bounded sorting/copying per goal set; amortize per navigation request.
 */
[[nodiscard]] Result<std::vector<int>> canonicalFlowCoordinates(std::span<const FlowGoal> goals,
                                                                const CrowdField&         field);

/** @brief Observe exact containing-cell reachability, independent of interpolation across blocked cells.
 * @ownership Borrows immutable field only during call. @thread Owner thread. @reentrancy No callbacks.
 */
bool isFlowPointReachable(const CrowdField& field, float x, float y) noexcept;

/** @brief Compose public numeric identity/batch bindings without accessing private storage. */
void exposeAgentBatches(ssq::Class& cls);
/** @brief Register generation-qualified transient navigation request bindings. */
void exposeNavigationRequests(ssq::Class& cls);
/** @brief Resolve a checked request-owned lease without retaining script table references.
 * @return Owning pin copy or structured invalid/stale failure.
 * @ownership Copies only the lease. @thread Owner thread. @reentrancy No callbacks.
 */
[[nodiscard]] Result<FlowLease> resolveFlowRequestLease(Crowd& crowd, const ssq::Table& token);

/** @brief Shared exact-placement geometry predicate for creation and relocation transactions. */
bool isTerrainClear(const CrowdField& field, bool clamp, float x, float y, float radius);
/** @brief Normalize a compatibility filter into the canonical two-filter policy. */
InteractionPolicy canonicalInteraction(AgentInteraction policy);
/** @brief Check finite mobility and nonnegative filter masks without modifying state. */
bool validInteraction(InteractionPolicy policy);
/** @brief Decode a canonical policy at the script boundary; throws on missing or wrongly typed fields. */
InteractionPolicy parseInteractionPolicy(ssq::Table table);
/** @brief Decode exact integer movement identities; no floating-point identity coercion. */
MovementBinding parseMovementBinding(const ssq::Table& table);
/** @brief Hash UTF-8 identity bytes once at admission; collisions are rejected by the registry. */
std::uint64_t namedTieValue(std::string_view identity);

/**
 * @brief Private crowd runtime storage.
 * @cost Linear in agent count for SOA vectors and transient broadphase/avoidance work buffers; capacity is bounded
 * by maxAgents.
 */
struct Crowd::NavigationRequests {
    struct Entry {
        FlowBuildPlan            plan;
        std::optional<FlowLease> lease;
        std::uint32_t            generation = 1;
        bool                     occupied   = false;
    };
    std::vector<Entry> entries;
    /** @brief Resolve exact token identity without mutation.
     * @ownership Borrow registry entry only until structural mutation or provider destruction.
     * @lifetime Do not retain across VM calls or frames. @thread Owner thread. @reentrancy No callbacks.
     */
    [[nodiscard]] Result<std::reference_wrapper<Entry>> resolve(std::uint64_t owner, const ssq::Table& token);
};

struct Crowd::Impl {
    struct SchedulerFailure {
        Status status;
    };
    CrowdExecution                      execution = CrowdExecution::Serial;
    std::shared_ptr<CrowdWorkScheduler> workScheduler;
    void dispatchWork(std::size_t items, const std::function<void(std::size_t, std::size_t)>& body);

    void applyAgentUpdate(const AgentUpdate& update, std::size_t slot);
    struct AdvanceGuard;
    Impl();
    struct RegistryEntry {
        std::uint32_t generation = 1;
        int           denseIndex = -1;
    };
    std::uint64_t              owner;
    std::vector<RegistryEntry> registry;
    std::vector<std::uint32_t> denseToRegistry, freeRegistry;
    void                       reserveAgentAppend();
    void                       appendAgentIdentity(int denseIndex);
    void                       eraseAgentIdentity(int denseIndex);
    void                       clearAgentIdentities();
    int                        resolveAgent(AgentRef reference) const noexcept;
    CrowdField                 field;
    NavigationSurfaceStamp     surfaceStamp;
    struct FlowCacheEntry {
        std::shared_ptr<const CrowdField> field;
        std::weak_ptr<const void>         pins;
        NavigationSurfaceStamp            surface;
        std::uint64_t                     input = 0, generation = 0, bytes = 0;
        SimulationTick                    access;
        std::uint32_t                     algorithm = flowBuildAlgorithmVersion;
    };
    static std::uint64_t retainedFlowBytes(const CrowdField& value);
    static bool          isFlowKeyBefore(const FlowCacheEntry& a, const FlowCacheEntry& b);
    /** @brief Stage cache metadata and deterministic unleased evictions without live mutation.
     * @ownership Returns owning metadata, retaining immutable fields. @thread Owner thread only.
     * @cost Bounded scan/copy of at most 10000 entries, amortized per cache publication/configuration.
     */
    [[nodiscard]] Result<std::vector<FlowCacheEntry>> stageFlowCache(std::uint64_t budget, std::uint64_t bytes,
                                                                     size_t count) const;
    FlowLease                                         leaseFor(FlowCacheEntry& entry);
    std::vector<FlowCacheEntry>                       flowCache;
    std::uint64_t                                     flowCacheBudget = 64 * 1024 * 1024, nextFlowGeneration = 1;
    bool                                              isFlowLeaseCurrent(const FlowLease& lease) const noexcept {
        return lease.field_ && lease.owner_ == owner && lease.input_ == field.inputRevision() &&
               lease.field_->isBuilt() && lease.algorithm_ == flowBuildAlgorithmVersion;
    }

    struct MotionBackup {
        std::vector<float> xs, ys, headings, vxs, vys, speeds, wanderPhases;
    } rollback;
    SimulationTick                tick;
    Duration                      elapsed;
    std::optional<ClockAuthority> clock;
    AdvanceBudget                 activeBudget;
    bool                          budgetActive = false;

    float              defaultSpeed    = 120.f;
    float              defaultRadius   = 6.f;
    float              defaultTurnRate = 6.f;
    float              arriveRadius    = 64.f;
    float              sepRadius       = 28.f;
    float              perceiveRadius  = 64.f;
    float              sepWeight       = 1.f;
    float              alignWeight     = 0.f;
    float              cohesionWeight  = 0.f;
    float              wanderWeight    = 0.f;
    float              goalWeight      = 1.f;
    bool               resolveOverlaps = true;
    bool               clampToField    = true;
    int                maxAgents       = 100000;
    float              simTime         = 0.f;
    AvoidanceSettings  avoidance;
    std::int64_t       avoidanceChecks      = 0;
    int                avoidanceTruncations = 0;
    mutable StepReport workReport;
    float              avoidanceMaxSpeed = 0.f, avoidanceMaxRadius = 0.f;
    struct AvoidanceNeighbor {
        size_t slot;
        double distance2;
    };
    /**
     * @brief Per-step avoidance neighbor scratch space.
     * @cost Linear in checked neighbors and bounded by avoidance work limits.
     */
    std::vector<std::vector<AvoidanceNeighbor>> predictionNeighbors;

    /**
     * @brief SOA agent storage, indexed by compact slot id.
     * @cost Linear in agent count; mutation keeps all owned vectors aligned.
     */
    // SOA 单位存储（id = 槽位索引）。
    std::vector<float>             xs, ys, headings, vxs, vys, speeds;
    std::vector<float>             nextVxs, nextVys, correctionXs, correctionYs;
    std::vector<float>             preferredVxs, preferredVys, arriveFactors;
    std::vector<float>             radii, maxSpeeds, maxAccels, turnRates;
    std::vector<int32_t>           actions, datas, avoidancePriorities;
    std::vector<uint8_t>           hasTargets;
    std::vector<InteractionPolicy> interactions;
    std::vector<MovementBinding>   movements;
    std::vector<FlowLease>         flows;
    std::vector<float>             targetXs, targetYs;
    std::vector<float>             wanderPhases;
    std::vector<std::string>       stableIds;
    using TieKey = CrowdTieKey;
    std::vector<TieKey>                    tieKeys;
    std::unordered_map<std::uint64_t, int> namedTieKeys;
    std::uint64_t                          nextAnonymousTieKey = 1;
    std::unordered_map<std::string, int>   namedAgents;

    CrowdSpatialIndex spatialIndex;
    struct ContactPair {
        int left, right;
    };
    struct ContactDelta {
        float leftX = 0.f, leftY = 0.f, rightX = 0.f, rightY = 0.f;
        float penetration = 0.f;
    };
    std::vector<ContactPair>  contactPairs;
    std::vector<ContactDelta> contactDeltas;
    std::vector<std::size_t>  incidentStarts, incidentPairs;
    std::vector<std::size_t>  pairCounts, pairStarts;

    bool validId(int id) const { return id >= 0 && id < int(actions.size()); }
    bool isMovementCompatible(size_t index) const {
        if (flows[index].field_ && !isFlowLeaseCurrent(flows[index])) return false;
        const auto& binding       = movements[index];
        const bool  explicitClass = binding.movement.value() != 0;
        if (field.navigationCost_.empty()) return !explicitClass;
        return radii[index] <= surfaceStamp.clearanceRadius &&
               (!explicitClass ||
                (binding.movement == surfaceStamp.movement && binding.revision == surfaceStamp.movementRevision));
    }

    int navigationMismatchCount() const noexcept {
        int count = 0;
        for (size_t index = 0; index < actions.size(); ++index)
            if (actions[index] == static_cast<int>(AgentAction::Flow) && !isMovementCompatible(index)) ++count;
        return count;
    }

    /** @brief Observe the selected compatible built field, or null when unavailable.
     * @ownership Borrowed read-only pointer; never delete or retain across frames/tasks.
     * @lifetime Valid only in the current owner-thread phase until lease/default-field mutation or Crowd destruction.
     * @thread Owner thread only. @reentrancy No callbacks.
     */
    const CrowdField* navigationField(size_t index) const noexcept {
        if (!isMovementCompatible(index)) return nullptr;
        if (flows[index].field_) return flows[index].field_.get();
        return field.isBuilt() ? &field : nullptr;
    }

    bool canContact(size_t a, size_t b) const {
        return (interactions[a].contactLayer & interactions[b].contactMask) != 0 &&
               (interactions[b].contactLayer & interactions[a].contactMask) != 0;
    }
    bool canAvoid(size_t a, size_t b) const {
        return (interactions[a].avoidanceLayer & interactions[b].avoidanceMask) != 0 &&
               (interactions[b].avoidanceLayer & interactions[a].avoidanceMask) != 0;
    }

    std::vector<std::size_t> preferredCounts, preferredStarts;
    std::vector<int>         preferredNeighbors;
    struct PreferredFlow {
        float x = 0.f, y = 0.f, arrive = 1.f;
    };
    std::vector<PreferredFlow> preferredFlows;
    void                       spendPreferredWork(std::int64_t amount);
    void                       preparePreferredNeighbors();
    void                       preparePreferredVelocities(float dt);
    void rebuildGrid();

    template <typename Fn>
    void forEachNeighbor(float qx, float qy, float radius, Fn&& fn, StepReport* partitionReport = nullptr) const {
        const double r2             = double(radius) * radius;
        StepReport&  counters       = partitionReport == nullptr ? workReport : *partitionReport;
        const auto   baseSpatial    = partitionReport == nullptr ? 0 : workReport.spatialVisits;
        const auto   baseCandidates = partitionReport == nullptr ? 0 : workReport.candidatesVisited;
        spatialIndex.visit(
            qx, qy, radius,
            budgetActive ? activeBudget.maxSpatialVisits - baseSpatial : std::numeric_limits<std::int64_t>::max(),
            counters.spatialVisits, counters.cellsVisited, [&](int j) {
                if (budgetActive && counters.candidatesVisited >= activeBudget.maxNeighborVisits - baseCandidates)
                    throw WorkLimit{"Crowd neighbor visit budget exhausted"};
                ++counters.candidatesVisited;
                const double dx = double(xs[size_t(j)]) - qx, dy = double(ys[size_t(j)]) - qy;
                if (dx * dx + dy * dy <= r2) fn(j);
            });
    }

    void stepAgents(float dt);
    void prepareAvoidanceNeighbors();
    void selectAvoidanceVelocity(size_t index, float dt, float& vx, float& vy);

    float resolveOverlapsPass();

    void resolveWalls();
};

}  // namespace eve::crowd
