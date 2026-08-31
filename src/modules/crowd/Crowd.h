#pragma once

#include "common/Module.h"
#include "crowd/CrowdField.h"

#include <cstdint>
#include <memory>
#include <string>

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
class Crowd : public Module {
public:
    Module_REG(Crowd);
    Crowd();
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
    int getFieldHeight() const;
    float getCellSize() const;
    float getFieldOriginX() const;
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
    int getAgentData(int id) const;
    /** @brief Set overlap-resolution priority; higher values yield less. */
    bool setAgentAvoidancePriority(int id, int priority);
    /** @brief Return overlap-resolution priority, or zero for an invalid id. */
    int getAgentAvoidancePriority(int id) const;
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

    /** @brief 推进一帧仿真。 */
    void step(float dt);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::crowd
