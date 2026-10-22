#pragma once
#include "common/Export.h"


#include "common/LifetimeWitness.h"
#include "common/Module.h"
#include "common/Time.h"
#include "crowd/CrowdField.h"
#include "crowd/CrowdTypes.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace eve::crowd {
class CrowdWorkScheduler;
enum class CrowdExecution;

/**
 * @brief 单个单位的状态快照（脚本 getAgentState 返回值）。
 * action: 0=idle, 1=flow, 2=seek, 3=boids。
 */
struct AgentState {
    float x = 0.f;       ///< 世界 X
    float y = 0.f;       ///< 世界 Y
    float heading = 0.f; ///< 朝向（弧度，0=+X）
    float speed = 0.f;   ///< 当前速度大小
    float vx = 0.f;      ///< 速度 X
    float vy = 0.f;      ///< 速度 Y
    int action = -1;     ///< 行动枚举（-1=非法 id）
    int data = 0;        ///< 游戏自定义标记
    int avoidancePriority = 0; ///< Higher-priority agents yield less during overlap resolution.
    std::uint64_t   flowGeneration    = 0;  ///< Selected lease generation; zero selects the compatibility default.
    MovementBinding movement;               ///< Owning movement selection, independent of dense storage.
};

/** @brief 流场采样结果（脚本 flowAtWorld / flowAtCell 返回值）。 */
struct FlowVec {
    float x = 0.f; ///< 方向 X
    float y = 0.f; ///< 方向 Y
};

/** @brief Reachability is distinct from gameplay arrival. */
enum class FlowSampleState { Reachable, Unreachable };
/** @brief Owning lease sample with explicit reachability. */
struct FlowSample {
    FlowSampleState state = FlowSampleState::Unreachable;
    FlowVec         direction;
};
/** @brief Owning navigation metadata; no arrays or pointers are borrowed. */
struct FieldObservation {
    std::uint64_t          owner = 0;  ///< In-process owner epoch; restore renews it.
    NavigationSurfaceStamp surface;
    FieldState             state    = FieldState::Unconfigured;
    std::uint64_t          revision = 0, builtRevision = 0;
    std::uint64_t          inputRevision = 0;  ///< Geometry/cost/rule epoch, independent of goal publications.
    int                    goals         = 0;
};
/** @brief Independent quality flags; complete is zero, truncation/residuals may coexist. */
enum class SolveQuality : std::uint32_t {
    Complete            = 0,
    PredictionTruncated = 1,
    ContactResidual     = 2,
    WallResidual        = 4
};
/** @brief Deterministic operation budgets; exhaustion rejects the entire step before publication. */
struct AdvanceBudget {
    int          maxSubsteps         = 1024;
    int          maxContactPasses    = 12;
    std::int64_t maxGridCells        = 4 * 1024 * 1024;  ///< Occupied sparse cells, independent of empty world extent.
    std::int64_t maxNeighborVisits   = std::numeric_limits<std::int64_t>::max();
    std::int64_t maxPredictionChecks = std::numeric_limits<std::int64_t>::max();
    std::int64_t maxContactChecks    = std::numeric_limits<std::int64_t>::max();
    std::int64_t maxSpatialVisits = std::numeric_limits<std::int64_t>::max();  ///< Query row/cell/binary-search work.
    std::int64_t maxContactPairs  = 1000000;  ///< Live candidate-pair entries per pass; never truncated.
    std::int64_t maxPairVisits =
        std::numeric_limits<std::int64_t>::max();  ///< Count/prefix/init/fill/sort work per advance.
    std::int64_t maxPreferredNeighbors =
        1000000;  ///< Complete preferred-neighbor entries per substep; never truncated.
    std::int64_t maxPreferredVisits =
        std::numeric_limits<std::int64_t>::max();  ///< Workspace/count/prefix/fill/force visits.
};
/** @brief Explicit owner step; positive duration consumes the next contiguous tick. Zero reconciles current tick. */
struct CrowdStep {
    SimulationTick tick;
    Duration       delta;
    AdvanceBudget  budget;
};
/** @brief Owning observations of one completed advance; simulation-thread only. */
struct StepReport {
    std::int64_t     preferredNeighborsPeak = 0;  ///< Largest complete preferred-neighbor buffer.
    std::int64_t     preferredVisits        = 0;  ///< Workspace/preparation/force entry work across substeps.
    FieldObservation navigation;
    int              navigationClassMismatches =
        0;  ///< Flow agents with stale leases, missing/wrong classes or insufficient clearance.
    SimulationTick tick;
    Duration       elapsed;
    SolveQuality   quality              = SolveQuality::Complete;
    std::int64_t   gridBuilds           = 0;    ///< Broadphase rebuilds, including final residual inspection.
    std::int64_t   gridCellsPeak        = 0;    ///< Largest occupied sparse-cell count during this advance.
    std::int64_t   cellsVisited         = 0;    ///< Occupied cells enumerated by all neighborhood queries.
    std::int64_t   gridEntriesPeak      = 0;    ///< Largest indexed agent membership count.
    std::int64_t   gridSortComparisons  = 0;    ///< Sorting comparisons across all broadphase rebuilds.
    std::int64_t   spatialVisits        = 0;    ///< Query row/cell enumeration and binary-search comparisons.
    std::int64_t   contactPairsPeak     = 0;    ///< Largest candidate-pair buffer in this advance.
    std::int64_t   pairVisits           = 0;    ///< Count/prefix/buffer-init/fill/sort work across contact passes.
    std::int64_t   candidatesVisited    = 0;    ///< Bucket entries inspected before radius or interaction filtering.
    std::int64_t   contactPairsChecked  = 0;    ///< Unique interacting pairs tested during contact iterations.
    std::int64_t   residualPairsChecked = 0;    ///< Unique interacting pairs inspected after projection.
    std::int64_t   contactPasses        = 0;    ///< Contact projection iterations across all substeps.
    std::int64_t   gridNanoseconds      = 0;    ///< Measured broadphase time; never controls simulation decisions.
    std::int64_t   motionNanoseconds    = 0;    ///< Preferred velocity, avoidance and integration time.
    std::int64_t   contactNanoseconds   = 0;    ///< Contact projection time, excluding grid and walls.
    std::int64_t   wallNanoseconds      = 0;    ///< Terrain projection time.
    std::int64_t   residualNanoseconds  = 0;    ///< Final quality inspection time, excluding its grid rebuild.
    std::int64_t avoidanceChecks      = 0;    ///< Candidate-neighbor predictions across all substeps.
    int          avoidanceTruncations = 0;    ///< Agent/substep queries exceeding the configured neighbor limit.
    int          substeps             = 0;    ///< Number of bounded integration intervals consumed.
    int          unresolvedContacts   = 0;    ///< Final overlapping pairs deeper than 0.001 world units.
    int          unresolvedWalls      = 0;    ///< Final agents violating terrain or field bounds.
    float        maxPenetration       = 0.f;  ///< Largest remaining pair penetration in world units.
};

/** @brief Optional bounded velocity sampling for RTS local avoidance. */
struct AvoidanceSettings {
    bool  enabled      = false;  ///< Opt in independently of legacy Boids steering.
    float horizon      = 2.f;    ///< Positive prediction seconds, at most ten.
    float margin       = 0.05f;  ///< Non-negative extra pair clearance in world units.
    int   maxNeighbors = 32;     ///< Nearest interacting neighbors considered, in [1,128].
};

/** @brief Independent local interaction policy, copied into Crowd storage. */
struct AgentInteraction {
    float pushability  = 1.f;         ///< Relative contact mobility in [0,1]; zero cannot be pushed by agents.
    bool  holdPosition = false;       ///< Suppress locomotion and agent pushes; terrain constraints still apply.
    int   layer        = 1;           ///< Non-negative membership bit mask; zero disables agent interaction.
    int   mask         = 0x7fffffff;  ///< Both agents must accept the other's layer for interaction.
};

/** @brief Canonical independent predictive-steering and body-contact policy; both filters are symmetric. */
struct InteractionPolicy {
    float pushability    = 1.f;
    bool  holdPosition   = false;
    int   avoidanceLayer = 1, avoidanceMask = 0x7fffffff;
    int   contactLayer = 1, contactMask = 0x7fffffff;
};

/** @brief Strong locomotion intent; numeric values retain compatibility action codes. */
enum class AgentAction : std::int32_t { Idle = 0, Flow = 1, Seek = 2, Boids = 3 };
/** @brief Owning local steering request; does not retain navigation or gameplay objects. */
struct SteeringIntent {
    AgentAction action    = AgentAction::Idle;
    bool        hasTarget = false;
    float       targetX = 0.f, targetY = 0.f;
};
/** @brief Owned changes for one live agent. Absent fields keep their existing values. */
struct AgentUpdate {
    std::optional<FlowLease>         flow;
    AgentRef                         reference;
    std::optional<MovementBinding>   movement;
    std::optional<float>             maxSpeed, maxAcceleration, headingTurnRate, radius;
    std::optional<int>               avoidancePriority, data;
    std::optional<AgentInteraction>  interaction;
    std::optional<InteractionPolicy> policy;
    std::optional<bool>              holdPosition;
    std::optional<SteeringIntent>    intent;
};
/** @brief Caller-built atomic update batch; duplicate agent references are rejected. */
struct AgentUpdateBatch {
    std::vector<AgentUpdate> agents;
};
/** @brief Owned identities to remove atomically; duplicate and stale references are rejected. */
struct AgentRemovalBatch {
    std::vector<AgentRef> agents;
};
/** @brief Explicit relocation of one live agent; velocity resets while identity and intent remain. */
struct AgentRelocation {
    AgentRef reference;
    float    x = 0.f, y = 0.f;
};
/** @brief Atomic exact placement batch; rejects terrain/contact violations without pushing neighbors. */
struct AgentRelocationBatch {
    std::vector<AgentRelocation> agents;
    int                          maxChecks = 1000000;
};
/** @brief Owning receipt for a committed update or removal batch. */
struct AgentBatchReceipt {
    std::size_t applied = 0;
};
/** @brief Owning row for a reusable renderer-neutral output frame. */
struct AgentFrameEntry {
    AgentRef   reference;
    AgentState state;
};
/** @brief Caller-owned frame storage; no pointers into Crowd are retained. */
struct AgentFrame {
    std::vector<AgentFrameEntry> agents;
    double                       simulationSeconds = 0.0;
    SimulationTick               tick;
};

/** @brief Placement policy for an atomic spawn batch. */
enum class SpawnPolicy { RejectOverlap, NearestFree, PushNeighbors };

/** @brief One named agent to create; all fields are owned values. */
struct SpawnRequest {
    std::string      stableId;
    float            x = 0.f, y = 0.f, heading = 0.f, radius = 1.f;
    AgentInteraction interaction;
    std::optional<InteractionPolicy> policy =
        std::nullopt;  ///< Explicit canonical policy overrides compatibility fields.
    MovementBinding movement;
};

/**
 * @brief Caller-built transaction; positive budgets bound search and relaxation.
 * @cost Linear in requested agents plus bounded candidate/contact checks; callers amortize by batching spawns.
 */
struct SpawnBatch {
    std::vector<SpawnRequest> agents;
    SpawnPolicy               policy        = SpawnPolicy::PushNeighbors;
    float                     maxDistance   = 64.f;  ///< Maximum relocation from each requested/original position.
    float                     searchSpacing = 1.f;   ///< NearestFree samples concentric rings at this spacing.
    int                       maxPasses     = 64;    ///< Contact relaxation limit, at most 256.
    int                       maxChecks = 1000000;   ///< Combined candidate/neighbor work budget, at most ten million.
};

/** @brief Canonical creation batch; reuses the existing atomic placement policies and budgets. */
using AgentCreateBatch = SpawnBatch;

/** @brief Owning placement result, independent of compact slots and subsequent removal. */
struct SpawnPlacement {
    std::string stableId;
    float       x = 0.f, y = 0.f;
    AgentRef    reference;  ///< Owner-qualified identity of the committed agent.
};

/**
 * @brief Observations from a committed spawn transaction.
 * @cost Linear in created placement count; returned by value once per spawn batch.
 */
struct SpawnReceipt {
    std::vector<SpawnPlacement> created;
    int                         displacedAgents = 0;
    int                         checks          = 0;
};

/**
 * @brief 群体行为模块：连续流场寻路 + 海量单位移动/转向/行动 + Boids 鸟群。
 *
 * Script: `crowd <- eve.Crowd();`
 *
 * 纯 CPU 仿真，与渲染解耦：每帧调用 step(dt) 推进；渲染/游戏逻辑通过
 * getPositions / getHeadings 批量读取（脚本预分配数组），万级单位无需逐单位回调。
 *
 * 行动模式：
 *   idle  —— 不主动移动（分离力仍生效）；
 *   flow  —— 沿流场方向行军（Boids 分离力防扎堆）；
 *   seek  —— 向世界目标点移动，进入 arriveRadius 后线性减速；
 *   boids —— 鸟群：分离 + 对齐 + 聚合，可叠加目标偏置与 wander。
 */
class EVENGINE_API_FOUNDATION Crowd : public Module {
    struct Impl;

public:
    /** @brief Move-only prepared terrain/navigation publication. */
    class EVENGINE_API_FOUNDATION SurfacePlan {
        friend class Crowd;
        std::unique_ptr<CrowdField> field_;
        NavigationSurfaceStamp      stamp_;
        std::uint64_t               owner_ = 0, revision_ = 0;
        bool                        unchanged_ = false;

    public:
        /** @brief Construct an empty, noncommittable plan. */
        SurfacePlan();
        /** @brief Release unpublished buffers. */
        ~SurfacePlan();
        /** @brief Transfer one-use publication ownership. */
        SurfacePlan(SurfacePlan&&) noexcept;
        /** @brief Transfer one-use publication ownership. */
        SurfacePlan& operator=(SurfacePlan&&) noexcept;
        SurfacePlan(const SurfacePlan&)            = delete;
        SurfacePlan& operator=(const SurfacePlan&) = delete;
    };
    /** @brief Validate and copy an immutable surface projection without publishing it.
     * @return Owned plan; conflicting provenance or invalid cost/geometry leaves current state unchanged.
     * @ownership Does not retain input arrays. @thread Owner thread. @reentrancy No callbacks.
     * @cost Linear validation and grid copies; amortize per explicit surface revision boundary.
     */
    [[nodiscard]] Result<SurfacePlan> prepareNavigationSurface(const NavigationSurfaceSnapshot& snapshot) const;
    /** @brief Publish terrain and navigation projection together without allocation.
     * @return Checked result; stale/foreign plans preserve the current field.
     * @thread Owner thread. @reentrancy No callbacks.
     */
    [[nodiscard]] Result<void> commitNavigationSurface(SurfacePlan&& plan);
    /** @brief Lifecycle of an owning prepared navigation publication. */
    enum class FlowBuildState { Ready, Cancelled, Committed, Pending, Failed, Unreachable };
    /** @brief Deterministic work consumed by a navigation advancement. */
    struct FlowBuildProgress {
        FlowBuildState state    = FlowBuildState::Cancelled;
        int            heapPops = 0, directionCells = 0;
    };
    /** @brief Move-only navigation workstate; contains no borrowed provider pointers. */
    class EVENGINE_API_FOUNDATION FlowBuildPlan {
        friend class Crowd;
        std::unique_ptr<CrowdField> field_;
        std::uint64_t               owner_ = 0, revision_ = 0;
        FlowBuildState              state_ = FlowBuildState::Cancelled;

    public:
        /** @brief Construct an empty cancelled plan. */
        FlowBuildPlan();
        /** @brief Release unpublished storage. */
        ~FlowBuildPlan();
        /** @brief Transfer publication ownership. */
        FlowBuildPlan(FlowBuildPlan&&) noexcept;
        /** @brief Transfer publication ownership. */
        FlowBuildPlan& operator=(FlowBuildPlan&&) noexcept;
        FlowBuildPlan(const FlowBuildPlan&)            = delete;
        FlowBuildPlan& operator=(const FlowBuildPlan&) = delete;
        /** @brief Observe lifecycle; a moved-from plan cannot be committed. */
        [[nodiscard]] FlowBuildState state() const noexcept { return state_; }
        /** @brief Cancel without publishing; repeated cancellation is a no-op.
         * @thread Owner thread only. @reentrancy No callbacks.
         */
        [[nodiscard]] Result<void> cancel();
    };
    /** @brief Prepare owned navigation workstate for one goal without changing the live field.
     * @return Move-only plan bound to this Crowd owner and current navigation input revision.
     * @ownership Plan owns all storage and retains no provider pointers.
     * @thread Owner thread only. @reentrancy No callbacks.
     * @cost Copies grid storage and seeds goal work; amortize per navigation request. Graph work is advanced
     * separately.
     */
    [[nodiscard]] Result<FlowBuildPlan> prepareFlowBuild(int gx, int gy) const;
    /** @brief Prepare one shared field for a canonical, nonempty set of at most 1024 destinations.
     * @return Owned plan; invalid cells reject before changing live state. Blocked goals are allowed.
     * @ownership Copies, sorts and deduplicates caller coordinates; retains no input spans.
     * @thread Simulation owner thread only. @reentrancy No callbacks.
     * @cost Copies grid-sized workstate and sorts the goal set; amortize per shared navigation request.
     * Graph advancement and atomic publication use the same kernel as single-goal compatibility calls.
     */
    [[nodiscard]] Result<FlowBuildPlan> prepareFlowBuild(std::span<const FlowGoal> goals) const;
    /** @brief Spend at most maxWork heap-pop/direction-cell units on owned request state.
     * @return Progress; stale requests are cancelled before work, failures never publish.
     * @thread Owner thread only. @reentrancy No callbacks.
     * @cost Bounded local graph/gradient work; amortize across navigation update phases.
     */
    [[nodiscard]] Result<FlowBuildProgress> advanceFlowBuild(FlowBuildPlan& plan, int maxWork);
    /** @brief Publish a prepared field without allocation; reject stale, foreign or cancelled plans.
     * @return Structured outcome; failure leaves the current field unchanged.
     * @thread Owner thread only. @reentrancy No callbacks.
     */
    [[nodiscard]] Result<void> commitFlowBuild(FlowBuildPlan&& plan);
    /** @brief Acquire an existing field for a canonical goal set without graph work.
     * @return Lease or NotFound; invalid input rejects without cache mutation.
     * @ownership Copies pin immutable storage, not this provider. @thread Owner thread. @reentrancy No callbacks.
     * @cost Sorts goals and compares complete bounded keys; amortize per navigation request.
     */
    [[nodiscard]] Result<FlowLease> acquireFlowField(std::span<const FlowGoal> goals);
    /** @brief Publish a completed build into shared cache without selecting the default field.
     * @return Lease or stale/pressure failure; failure preserves cache and plan.
     * @ownership Consumes plan only on success; lease copies pin storage, not this provider.
     * @thread Owner thread only. @reentrancy No callbacks.
     * @cost Bounded cache validation/eviction and ownership transfer after explicit graph advancement.
     */
    [[nodiscard]] Result<FlowLease> acquireFlowField(FlowBuildPlan&& plan);
    /** @brief Set retained-storage budget, evicting only unleased fields with atomic pressure failure.
     * @return Applied or pressure/invalid failure. @thread Owner thread. @reentrancy No callbacks.
     * @cost Stages bounded metadata for at most 10000 entries; amortize at navigation configuration boundaries.
     */
    [[nodiscard]] Result<void> configureFlowCache(std::uint64_t budgetBytes);
    /** @brief Read retained-storage accounting without copying fields; owner thread only. */
    [[nodiscard]] FlowCacheObservation getFlowCacheStatus() const noexcept;
    /** @brief Sample a current lease; stale/foreign leases fail, unreachable locations return zero.
     * @ownership Borrows lease during call only. @thread Owner thread. @reentrancy No callbacks.
     */
    [[nodiscard]] Result<FlowSample> sampleFlowField(const FlowLease& lease, float x, float y) const;

    /** @brief Immutable in-process checkpoint; not a persistent or cross-process format. */
    class Snapshot {
        friend class Crowd;
        std::shared_ptr<const Impl> state_;
    };
    /** @brief Prepared restore workspace; owns allocations until commit or destruction. */
    class EVENGINE_API_FOUNDATION RestorePlan {
        friend class Crowd;
        std::unique_ptr<Impl> state_;
        std::uint64_t         targetOwner_ = 0;

    public:
        /** @brief Construct an empty, noncommittable plan. */
        RestorePlan();
        /** @brief Release staged state. */
        ~RestorePlan();
        /** @brief Transfer one-use restore ownership. */
        RestorePlan(RestorePlan&&) noexcept;
        /** @brief Transfer one-use restore ownership. */
        RestorePlan& operator=(RestorePlan&&) noexcept;
        RestorePlan(const RestorePlan&)            = delete;
        RestorePlan& operator=(const RestorePlan&) = delete;
    };
    /** @brief Copy authoritative runtime state and clock into an owning checkpoint.
     * @return Immutable in-process state; no borrowed arrays or pointers.
     * @thread Simulation owner thread. @reentrancy No callbacks.
     * @cost Copies all agent/field storage; amortize at explicit checkpoint boundaries.
     */
    [[nodiscard]] Result<Snapshot> captureSnapshot() const;
    /** @brief Allocate complete restored state and fresh handle ownership before publication.
     * @return One-use plan targeted at this Crowd; previous AgentRef values become stale on commit.
     * @thread Simulation owner thread. @reentrancy No callbacks.
     * @cost Copies snapshot storage once; prepare before cross-domain observable mutation.
     */
    [[nodiscard]] Result<RestorePlan> prepareRestore(const Snapshot& snapshot) const;
    /** @brief Publish a prepared restore without allocation; consumes plan on success.
     * @return Failure for empty, consumed or foreign plans, preserving current state.
     * @thread Simulation owner thread. @reentrancy No callbacks.
     */
    [[nodiscard]] Result<void> commitRestore(RestorePlan&& plan);
    Module_REG(Crowd);
    /** @brief Crowd. */
    Crowd();
    /** @brief Crowd. */
    ~Crowd() override;
    /** @brief Select serial execution or inject a shared synchronous scheduler lease.
     * @param execution Explicit mode; Parallel requires a nonempty lease.
     * @param scheduler Owned shared adapter lease, not ownership of its worker pool.
     * @return InvalidArgument for missing provider, Conflict during advancement.
     * @ownership Serial drops the old lease; snapshot restore preserves current execution configuration.
     * @thread Crowd owner only. @reentrancy Must not be called by scheduler callbacks.
     */
    [[nodiscard]] Result<void> setWorkScheduler(CrowdExecution                      execution,
                                                std::shared_ptr<CrowdWorkScheduler> scheduler);
    /** @brief Return a non-owning witness for this provider instance, independent of restored agent state.
     * @return Opaque weak witness; expiration means the provider cannot be dereferenced.
     * @thread Owner thread only; does not authorize concurrent destruction or keep Crowd alive.
     * @reentrancy No callbacks. Witness remains valid across agent clear and checkpoint restoration.
     */
    [[nodiscard]] LifetimeWitness lifetimeWitness() const noexcept { return lifetime_.witness(); }

    // --- 流场（内部持有一个 CrowdField） ---
    /** @brief 配置流场网格（世界单位/格）。 */
    void resizeField(int width, int height, float cellSize, float originX, float originY);
    /** @brief 设置/清除某格阻挡。 */
    void setBlocked(int cx, int cy, bool blocked);
    /** @brief 设置地形代价（0=阻挡，>=1 可走）。 */
    void setCellCost(int cx, int cy, float cost);
    /** @brief 查询地形代价。 */
    float getCellCost(int cx, int cy) const;
    /** @brief Compatibility-only synchronous single-goal build through prepareFlowBuild/commitFlowBuild.
     * @throws eve::Exception A rejected request leaves the published field unchanged.
     * @cost Full field copy and Dijkstra; use explicit preparation/publication at navigation boundaries.
     */
    void buildFlowField(int gx, int gy);
    /** @brief Observe current field readiness and owner-local revision without rebuilding.
     * @return Owning metadata; stale differs from unbuilt and ready-but-unreachable.
     * @thread Simulation owner thread. @reentrancy No callbacks or retained pointers.
     */
    [[nodiscard]] FieldObservation getFieldStatus() const noexcept;
    /** @brief 追加目标格（多目标）。 */
    void addFlowGoal(int gx, int gy);
    /** @brief 清空目标列表。 */
    void clearFlowGoals();
    /** @brief 执行 Dijkstra 建场。 */
    void build();
    /** @brief 是否已建场。 */
    bool isFieldBuilt() const;
    /** @brief 某格是否可达。 */
    bool isReachable(int cx, int cy) const;
    /** @brief 网格信息访问器（调试渲染用）。 */
    int getFieldWidth() const;
    /** @brief Returns the field height. */
    int getFieldHeight() const;
    /** @brief Returns the cell size. */
    float getCellSize() const;
    /** @brief Returns the field origin x. */
    float getFieldOriginX() const;
    /** @brief Returns the field origin y. */
    float getFieldOriginY() const;

    /** @brief 世界坐标流场方向（双线性插值；场外返回零向量）。 */
    FlowVec flowAtWorld(float wx, float wy) const;
    /** @brief 世界坐标积分代价（场外返回 kUnreachable）。 */
    float costAtWorld(float wx, float wy) const;
    /** @brief 格级流场方向。 */
    FlowVec flowAtCell(int cx, int cy) const;

    // --- 单位 ---
    /**
     * @brief 添加单位；返回 id（=槽位索引；删除后 id 不稳定）。
     * @param x 世界 X
     * @param y 世界 Y
     * @param heading 初始朝向（弧度）
     * @param radius 半径（用于分离/重叠消解）
     * @return 单位 id，达到上限返回 -1
     */
    int addAgent(float x, float y, float heading, float radius);
    /** @brief Add an agent with an editor/game-stable logical identifier.
     * @param stableId Non-empty logical identifier unique within this Crowd.
     * @param x Initial world X coordinate.
     * @param y Initial world Y coordinate.
     * @param heading Initial heading in radians.
     * @param radius Agent collision radius.
     * @return Current compact slot, or -1 when the identifier or capacity is invalid.
     */
    int addNamedAgent(const std::string &stableId, float x, float y, float heading, float radius);
    /** @brief Return whether a stable logical agent exists.
     * @param stableId Logical identifier to query.
     * @return True when the identifier is currently mapped.
     */
    bool hasNamedAgent(const std::string &stableId) const;
    /** @brief Resolve a stable logical identifier to the current compact slot.
     * @param stableId Logical identifier to resolve.
     * @return Current compact slot, or -1 when missing.
     */
    int getNamedAgentIndex(const std::string &stableId) const;
    /** @brief Return the stable logical identifier for a compact slot.
     * @param index Current compact slot.
     * @return Stable identifier, or an empty string for invalid or anonymous slots.
     */
    std::string getAgentStableId(int index) const;
    /** @brief Remove an agent by stable logical identifier.
     * @param stableId Logical identifier to remove.
     * @return True when an existing agent was removed.
     */
    bool removeNamedAgent(const std::string &stableId);
    /** @brief 删除单位（swap-pop O(1)）。 */
    bool removeAgent(int id);
    /** @brief 清空全部单位。 */
    void clearAgents();
    /** @brief 当前单位数。 */
    int getAgentCount() const;
    /** @brief Resolve a compatibility dense slot to an owning identity value.
     * @return Agent reference or NotFound. @ownership Returns a value; does not retain the Crowd.
     * @lifetime The reference survives unrelated swap-pop; invalidate on removal or clear.
     * @thread Simulation thread only. @reentrancy No callbacks.
     */
    [[nodiscard]] Result<AgentRef> agentRef(int slot) const;
    /** @brief Resolve a logical name once at binding boundaries, without rebuilding any state.
     * @return Agent reference or NotFound. @ownership Returns a value; no input retained.
     * @lifetime Invalid after agent removal, clear or owner destruction.
     * @thread Simulation thread only. @reentrancy No callbacks.
     */
    [[nodiscard]] Result<AgentRef> resolveNamedAgent(const std::string& stableId) const;
    /** @brief Whether the owner and generation still identify a live agent.
     * @thread Simulation thread only; the caller must separately keep the Crowd alive.
     */
    [[nodiscard]] bool isAgentAlive(AgentRef reference) const noexcept;
    /** @brief Read an owning state snapshot by owner-qualified identity.
     * @return State or NotFound, including wrong-owner and stale-generation references.
     * @ownership Returns a value. @lifetime Snapshot survives subsequent mutation.
     * @thread Simulation thread only. @reentrancy No callbacks.
     */
    [[nodiscard]] Result<AgentState> readAgent(AgentRef reference) const;
    /** @brief Move-only owning preparation token; contains values and owner identity, never a Crowd pointer.
     * @ownership Holds immutable flow leases when present; provider-first destruction is safe.
     * @lifetime Single successful commit; restore or stale references reject atomically.
     * @thread Owner thread only. @reentrancy No callbacks. Not persistent or cross-process data.
     */
    class EVENGINE_API_FOUNDATION AgentUpdatePlan {
    public:
        /** @brief Construct an empty, uncommittable token. */
        AgentUpdatePlan();
        /** @brief Release prepared values without changing agents. */
        ~AgentUpdatePlan();
        /** @brief Transfer token ownership. */
        AgentUpdatePlan(AgentUpdatePlan&&) noexcept;
        /** @brief Transfer token ownership. */
        AgentUpdatePlan& operator=(AgentUpdatePlan&&) noexcept;
        /** @brief Tokens cannot be copied. */
        AgentUpdatePlan(const AgentUpdatePlan&) = delete;
        /** @brief Tokens cannot be copied. */
        AgentUpdatePlan& operator=(const AgentUpdatePlan&) = delete;

    private:
        friend class Crowd;
        struct Data;
        std::unique_ptr<Data> data_;
    };
    /** @brief Canonically validate and own a batch before a later nonallocating commit.
     * @ownership Copies values; retains no caller span or provider pointer.
     * @lifetime Token may span motion steps; identity/flow validity is rechecked at commit.
     * @thread Owner thread. @reentrancy No callbacks.
     * @cost Linear copying plus identity sorting per batch; amortize preparation before a commit phase.
     */
    [[nodiscard]] Result<AgentUpdatePlan> prepareAgentUpdates(const AgentUpdateBatch& batch) const;
    /** @brief Validate all identities then apply selected prepared updates without allocating success-path workspace.
     * @param plan Owned token; consumed on success, preserved on rejection.
     * @param selection Empty means all; otherwise one zero/one byte per prepared update, borrowed during call.
     * @return Selected count or failure before any mutation, including stale unselected references.
     * @ownership Resolves numeric slots at commit; never retains registry pointers across steps.
     * @thread Owner thread; unavailable during advancement. @reentrancy No callbacks.
     * @cost Linear validation/application per prepared batch; no sorting or output allocation on success.
     */
    [[nodiscard]] Result<AgentBatchReceipt> commitAgentUpdates(AgentUpdatePlan&&             plan,
                                                               std::span<const std::uint8_t> selection);
    /** @brief Compatibility-only one-call facade over canonical prepareAgentUpdates/commitAgentUpdates.
     * @ownership Borrows input for the call. @thread Owner thread. @reentrancy No callbacks.
     * @cost Allocates/copies and sorts per batch; new transaction consumers prepare once before publication.
     */
    [[nodiscard]] Result<AgentBatchReceipt> updateAgents(const AgentUpdateBatch& batch);
    /** @brief Atomically remove live references; every reference is checked before any removal.
     * @ownership Borrows input during the call only. @thread Simulation thread only.
     * @reentrancy No callbacks. @cost Linear removals plus sorting identities once per batch.
     */
    [[nodiscard]] Result<AgentBatchReceipt> removeAgents(const AgentRemovalBatch& batch);
    /** @brief Atomically relocate live agents to exact clear positions; failure preserves all motion state.
     * @ownership Borrows owned requests during this call. @lifetime References and intents remain unchanged.
     * @thread Simulation thread only. @reentrancy No callbacks.
     * @cost Copies position columns once per batch, then bounded candidate/pair geometry checks;
     * amortize teleport requests per phase. This exact-placement operation has no nearest-free fallback.
     */
    [[nodiscard]] Result<AgentBatchReceipt> relocateAgents(const AgentRelocationBatch& batch);
    /** @brief Copy one owning frame into reusable caller storage; allocation failure preserves the output.
     * @ownership Caller owns output; retains no internal view. @lifetime Survives all subsequent mutations.
     * @thread Simulation thread only. @reentrancy No callbacks.
     * @cost Linear in live agents per call; reuse capacity and copy once per consumer phase.
     */
    [[nodiscard]] Result<AgentBatchReceipt> copyAgentFrame(AgentFrame& destination) const;
    /** @brief Copy a selected owning frame in request order; stale input preserves the destination.
     * @ownership Borrows references only during the call; caller owns the copied frame.
     * @lifetime Output survives all mutations. @thread Simulation thread only. @reentrancy No callbacks.
     * @cost Linear in requested agents; reuse destination capacity once per consumer phase.
     */
    [[nodiscard]] Result<AgentBatchReceipt> copyAgentStates(std::span<const AgentRef> references,
                                                            AgentFrame&               destination) const;
    /** @brief 单位容量上限（默认 100000）。 */
    void setMaxAgents(int maxAgents);
    /** @brief Returns the max agents. */
    int getMaxAgents() const;

    /** @brief 设置行动："idle" | "flow" | "seek" | "boids"。 */
    bool setAgentAction(int id, const std::string &action);
    /** @brief 查询行动名。 */
    std::string getAgentAction(int id) const;
    /** @brief 设置世界目标点（seek 直接寻点，boids 作迁移偏置）。 */
    bool setAgentTarget(int id, float tx, float ty);
    /** @brief 清除目标点。 */
    bool clearAgentTarget(int id);
    /** @brief 设置最大速度（世界单位/秒）。 */
    bool setAgentSpeed(int id, float speed);
    /** @brief 设置加速度上限（默认 maxSpeed×2）。 */
    bool setAgentAccel(int id, float accel);
    /** @brief 设置转向速率上限（弧度/秒）。 */
    bool setAgentTurnRate(int id, float radPerSec);
    /** @brief 设置半径。 */
    bool setAgentRadius(int id, float radius);
    /** @brief 设置游戏自定义标记。 */
    bool setAgentData(int id, int data);
    /** @brief Returns the agent data. */
    int getAgentData(int id) const;
    /** @brief Set overlap-resolution priority; higher values yield less. @return Applied, or NotFound for an invalid
     * slot. */
    [[nodiscard]] Result<void> setAgentAvoidancePriority(int id, int priority);
    /** @brief Return overlap-resolution priority, or zero for an invalid id. */
    int getAgentAvoidancePriority(int id) const;
    /** @brief Atomically replace the local interaction policy for a current compact slot.
     * @param id Current
     * slot; resolve stable identity again after any removal.
     * @param policy Copied policy with finite pushability
     * in [0,1] and non-negative masks.
     * @return Applied, InvalidArgument or NotFound; failure leaves the policy
     * unchanged.
     * @ownership Crowd owns a copy. @thread Simulation thread only.
     * @reentrancy No callbacks
     * or reentrant mutation.
     */
    [[nodiscard]] Result<void> setAgentInteraction(int id, AgentInteraction policy);
    /** @brief Read an owning policy snapshot, or NotFound for an invalid compact slot.
     * @thread Simulation thread
     * only. @lifetime The copy survives subsequent mutations.
     */
    [[nodiscard]] Result<AgentInteraction> getAgentInteraction(int id) const;
    /** @brief Atomically replace independent steering/contact rules by owner-qualified identity.
     * @return Applied, InvalidArgument or NotFound; failure changes no state.
     * @ownership Copies the policy. @thread Simulation thread only. @reentrancy No callbacks.
     * @lifetime Stored until replaced or agent removal. Terrain constraints are never filtered.
     */
    [[nodiscard]] Result<void> setInteractionPolicy(AgentRef reference, InteractionPolicy policy);
    /** @brief Read an owning canonical policy snapshot; stale or wrong-owner references return NotFound.
     * @ownership Returns a value. @lifetime Survives mutation. @thread Simulation thread only.
     * @reentrancy No callbacks.
     */
    [[nodiscard]] Result<InteractionPolicy> readInteractionPolicy(AgentRef reference) const;
    /** @brief Apply a caller-built spawn batch atomically, including neighbor displacement.
     * @param batch Up to
     * 1024 uniquely named agents and explicit placement/work budgets.
     * @return Owning receipt, or a diagnostic
     * without any world mutation on failure.
     * @ownership Copies the batch; retains no caller references. Existing
     * targets and velocities are preserved.
     * @thread Simulation thread only. @reentrancy No callbacks or
     * reentry.
     * @cost Copies current simulation/field storage once per batch, then performs bounded
     * candidate/contact work.
     * Amortize by batching spawns; NearestFree chooses the first clear deterministic
     * ring sample, not a continuous optimum.
     * @details PushNeighbors anchors all new agents, respects existing
     * hold/pushability and interaction masks,
     * and only moves the connected contact region. No simulation time is
     * advanced. Terrain and distance limits
     * are enforced before commit. Budget exhaustion is a visible failure,
     * never a partial creation.
     */
    [[nodiscard]] Result<SpawnReceipt> applySpawnBatch(const SpawnBatch &batch);
    /** @brief Canonical atomic agent creation with placement policies and work budgets.
     * @ownership Borrows the owning batch for this call; returns an owning receipt.
     * @thread Simulation thread only. @reentrancy No callbacks or reentry.
     * @cost One simulation snapshot per creation batch plus explicitly bounded placement work;
     * amortize by batching creation. Failure publishes neither agents nor displacement.
     * @lifetime Returned references invalidate on agent removal, clear, restore or owner destruction.
     */
    [[nodiscard]] Result<SpawnReceipt> createAgents(const AgentCreateBatch& batch);
    /** @brief 直接放置单位。 */
    bool setAgentPosition(int id, float x, float y);
    /** @brief 读取单位状态快照（非法 id 返回 action=-1）。 */
    AgentState getAgentState(int id) const;

    // --- 群体参数 ---
    /** @brief 新单位默认速度。 */
    void setDefaultSpeed(float speed);
    /** @brief 新单位默认半径。 */
    void setDefaultRadius(float radius);
    /** @brief 新单位默认转向速率（弧度/秒）。 */
    void setDefaultTurnRate(float radPerSec);
    /** @brief seek 到达减速半径。 */
    void setArriveRadius(float radius);
    /** @brief 分离/邻居查询半径。 */
    void setSeparationRadius(float radius);
    /** @brief 对齐/聚合感知半径（Boids；默认 64，可大于分离半径）。 */
    void setPerceptionRadius(float radius);
    /** @brief Boids 分离力权重。 */
    void setSeparationWeight(float weight);
    /** @brief Boids 对齐力权重。 */
    void setAlignmentWeight(float weight);
    /** @brief Boids 聚合力权重。 */
    void setCohesionWeight(float weight);
    /** @brief Boids wander 权重。 */
    void setWanderWeight(float weight);
    /** @brief Boids 目标偏置权重。 */
    void setGoalWeight(float weight);
    /** @brief 是否做位置重叠消解 pass（默认开）。 */
    void setResolveOverlaps(bool enable);
    /** @brief 是否把单位钳制在流场边界内（默认开）。 */
    void setClampToField(bool enable);
    /** @brief Configure predictive velocity sampling; invalid input leaves settings unchanged.
     * @return Applied
     * or InvalidArgument. @ownership Copies the settings.
     * @thread Simulation thread only. @reentrancy No
     * callbacks or reentry.
     * @details Sampling respects acceleration limits and prefers passing on the right.

     * * It is a local heuristic, not a collision-free or deadlock-free guarantee; inspect advance reports.
     */
    [[nodiscard]] Result<void> configureAvoidance(AvoidanceSettings settings);
    /** @brief Return an owning settings snapshot. @thread Simulation thread only. */
    AvoidanceSettings getAvoidanceSettings() const;

    /** @brief Advance using bounded simultaneous integration and contact projection.
     * @param dt Finite,
     * non-negative caller-supplied simulation seconds; zero is a no-op.
     * @return Owning residual-contact report,
     * or InvalidArgument/PreconditionViolation
     * before mutation for invalid time or more than 1024 required
     * substeps.
     * @ownership Crowd retains agents; the report owns its observations.
     * @lifetime Report
     * values remain valid independently of subsequent mutations.
     * @thread Simulation thread only; no concurrent
     * access to this Crowd.
     * @reentrancy No callbacks and no reentrant mutation.
     * @cost Per tick, linear
     * storage work plus local neighbor work per substep/contact
     * iteration; dense clusters can be quadratic.
     * Reuse one report per game tick.
     * @details Substeps are at most 1/60 second and limit travel relative to
     * positive
     * agent radii and terrain cell size. Call with a fixed dt sequence for repeatability
     * on one
     * build; cross-platform bit-exact replay is not guaranteed. Residual
     * contacts are reported rather than
     * pretending an impossible packing was solved.
     */
    [[nodiscard]] Result<StepReport> advance(float dt);
    /** @brief Advance the next owner tick using explicit time and deterministic operation budgets.
     * @return Owning report, or structured failure preserving all motion, wander and clock state.
     * @ownership Borrows the step during the call. @thread Simulation thread only. @reentrancy No callbacks.
     * @cost Copies motion columns into reusable rollback storage once per tick, then bounded integration work;
     * amortize calls at the fixed simulation cadence. Clock authority cannot mix with positive legacy stepping.
     * @lifetime The report owns its observations. Timing counters never select simulation behavior.
     */
    [[nodiscard]] Result<StepReport> advance(const CrowdStep& step);
    /** @brief Validate external clock admission before a consumer publishes its input batch.
     * @return Checked clock/duration admission only; geometry and solve budgets are checked by advance.
     * @ownership Borrows step; retains nothing. @thread Simulation owner thread only.
     * @reentrancy No callbacks. No reservation: intervening advances invalidate this observation.
     */
    [[nodiscard]] Result<void> checkAdvanceClock(const CrowdStep& step) const;

    /** @brief Compatibility-only stepping facade; delegates to advance and throws on
     * rejected input. Use advance
     * to inspect residual crowding and handle failure.
     * @thread Simulation thread only. @reentrancy No callbacks
     * or reentry.
     */
    void step(float dt);

private:
    friend void              exposeNavigationRequests(ssq::Class& cls);
    friend Result<FlowLease> resolveFlowRequestLease(Crowd& crowd, const ssq::Table& token);
    struct NavigationRequests;
    std::unique_ptr<NavigationRequests> navigationRequests_;
    void                                clearNavigationRequests() noexcept;
    enum class ClockAuthority { Compatibility, External };
    [[nodiscard]] Result<void>       checkClock(const CrowdStep& step, ClockAuthority authority) const;
    [[nodiscard]] Result<StepReport> advanceInternal(const CrowdStep& step, ClockAuthority authority);
    [[nodiscard]] Result<StepReport> advanceWorking(float dt, const AdvanceBudget& budget);
    std::unique_ptr<Impl> impl_;
    LifetimeOwner                    lifetime_;
};

}  // namespace eve::crowd
