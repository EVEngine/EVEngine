#pragma once
#include "common/Export.h"


#include "common/Module.h"
#include "crowd/CrowdField.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::crowd {

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
};

/** @brief 流场采样结果（脚本 flowAtWorld / flowAtCell 返回值）。 */
struct FlowVec {
    float x = 0.f; ///< 方向 X
    float y = 0.f; ///< 方向 Y
};

/** @brief Owning observations of one completed advance; simulation-thread only. */
struct StepReport {
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

/** @brief Placement policy for an atomic spawn batch. */
enum class SpawnPolicy { RejectOverlap, NearestFree, PushNeighbors };

/** @brief One named agent to create; all fields are owned values. */
struct SpawnRequest {
    std::string      stableId;
    float            x = 0.f, y = 0.f, heading = 0.f, radius = 1.f;
    AgentInteraction interaction;
};

/** @brief Caller-built transaction; positive budgets bound search and relaxation. */
struct SpawnBatch {
    std::vector<SpawnRequest> agents;
    SpawnPolicy               policy        = SpawnPolicy::PushNeighbors;
    float                     maxDistance   = 64.f;  ///< Maximum relocation from each requested/original position.
    float                     searchSpacing = 1.f;   ///< NearestFree samples concentric rings at this spacing.
    int                       maxPasses     = 64;    ///< Contact relaxation limit, at most 256.
    int                       maxChecks = 1000000;   ///< Combined candidate/neighbor work budget, at most ten million.
};

/** @brief Owning placement result, independent of compact slots and subsequent removal. */
struct SpawnPlacement {
    std::string stableId;
    float       x = 0.f, y = 0.f;
};

/** @brief Observations from a committed spawn transaction. */
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
public:
    Module_REG(Crowd);
    /** @brief Crowd. */
    Crowd();
    /** @brief Crowd. */
    ~Crowd() override;

    // --- 流场（内部持有一个 CrowdField） ---
    /** @brief 配置流场网格（世界单位/格）。 */
    void resizeField(int width, int height, float cellSize, float originX, float originY);
    /** @brief 设置/清除某格阻挡。 */
    void setBlocked(int cx, int cy, bool blocked);
    /** @brief 设置地形代价（0=阻挡，>=1 可走）。 */
    void setCellCost(int cx, int cy, float cost);
    /** @brief 查询地形代价。 */
    float getCellCost(int cx, int cy) const;
    /** @brief 单目标快捷建场（clearGoals + addGoal + build）。 */
    void buildFlowField(int gx, int gy);
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

    /** @brief Compatibility-only stepping facade; delegates to advance and throws on
     * rejected input. Use advance
     * to inspect residual crowding and handle failure.
     * @thread Simulation thread only. @reentrancy No callbacks
     * or reentry.
     */
    void step(float dt);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::crowd
