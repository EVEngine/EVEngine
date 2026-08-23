#pragma once

// 连续流场：Dijkstra 积分场 + 梯度方向场 + 双线性采样。
// 与 map/FlowField 的区别：map 的流场是格级 next 指针，本类是连续方向向量，
// 供单位在格子内平滑移动、平滑转向。纯数学，零模块依赖。

#include <cstdint>
#include <vector>

namespace eve::crowd {

/**
 * @brief 连续流场（flow field）。
 *
 * build() 从所有目标格做一次 8 邻域 Dijkstra，得到到目标的积分代价
 * （integration cost）；随后由中心差分求梯度，生成每格的单位方向向量
 * （沿代价下降方向）。flowAtWorld() 用双线性插值给出任意世界坐标的方向，
 * 供单位逐帧采样跟随。
 */
class CrowdField {
public:
    /** @brief 不可达/阻挡的积分代价。 */
    static constexpr float kUnreachable = 3.4e38f;

    CrowdField() = default;

    /**
     * @brief 配置网格（保留原有 cost/goals，若尺寸变化则重置）。
     * @param width 列数（>0）
     * @param height 行数（>0）
     * @param cellSize 每格世界尺寸（>0）
     * @param originX 世界原点 X（格 (0,0) 中心）
     * @param originY 世界原点 Y
     */
    void resize(int width, int height, float cellSize, float originX, float originY);

    /** @brief 清空全部数据（回到无效状态）。 */
    void clear();

    /** @brief 是否配置了有效网格。 */
    bool valid() const { return width_ > 0 && height_ > 0 && cellSize_ > 0.f; }

    /** @brief 网格尺寸访问器。 */
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    float getCellSize() const { return cellSize_; }
    float getOriginX() const { return originX_; }
    float getOriginY() const { return originY_; }

    /**
     * @brief 设置/清除某格阻挡。
     * @param cx 列
     * @param cy 行
     * @param blocked true=阻挡（cost 记为 0）
     */
    void setBlocked(int cx, int cy, bool blocked);

    /** @brief 查询某格是否阻挡。 */
    bool isBlocked(int cx, int cy) const;

    /**
     * @brief 设置某格地形代价（进入该格的移动成本）。
     * @param cost 0=阻挡，>=1 可走（越大越难走）
     */
    void setCellCost(int cx, int cy, float cost);

    /** @brief 查询地形代价（越界/阻挡返回 0）。 */
    float getCellCost(int cx, int cy) const;

    /** @brief 把全部格子设为同一地形代价（通常 build 前重置用）。 */
    void setAllCellCost(float cost);

    /**
     * @brief 设定唯一目标格（等价 clearGoals + addGoal）。
     * @param gx 目标列
     * @param gy 目标行
     */
    void setGoal(int gx, int gy);

    /** @brief 追加一个目标格（多目标支持）。 */
    void addGoal(int gx, int gy);
    /** @brief 清空目标列表。 */
    void clearGoals();
    /** @brief 目标个数。 */
    int goalCount() const { return int(goals_.size() / 2); }

    /** @brief 是否已 build 过（build 后或 resize 后为 true，改动地形后仍为 true）。 */
    bool isBuilt() const { return built_; }

    /** @brief 从目标格做 Dijkstra，生成积分场与方向场。 */
    void build();

    /** @brief 查询格级积分代价（未 build 返回 kUnreachable）。 */
    float costAtCell(int cx, int cy) const;

    /** @brief 某格是否可达（积分代价有限且非负）。 */
    bool isReachable(int cx, int cy) const;

    /**
     * @brief 查询格级方向向量（单位长度；目标格/阻挡/不可达为 0）。
     * @param cx 列
     * @param cy 行
     * @param[out] dx 方向 X
     * @param[out] dy 方向 Y
     */
    void flowAtCell(int cx, int cy, float &dx, float &dy) const;

    /**
     * @brief 查询世界坐标处的积分代价（双线性插值；场外返回 kUnreachable）。
     * @param wx 世界 X
     * @param wy 世界 Y
     * @return 插值后的积分代价
     */
    float costAtWorld(float wx, float wy) const;

    /**
     * @brief 查询世界坐标处的跟随方向（双线性插值后归一化）。
     * @param wx 世界 X
     * @param wy 世界 Y
     * @param[out] dx 方向 X（场外/全不可达为 0）
     * @param[out] dy 方向 Y
     */
    void flowAtWorld(float wx, float wy, float &dx, float &dy) const;

    /**
     * @brief 圆 vs 阻挡格碰撞消解：把圆心从覆盖到的阻挡格中推出来。
     *
     * 按单位半径展开 AABB，检查覆盖到的格；对每个阻挡格沿穿透最小的轴推出，
     * 使单位圆不再与任何阻挡格重叠。墙角处可能需迭代 2 次。
     *
     * @param[in,out] wx 圆心世界 X
     * @param[in,out] wy 圆心世界 Y
     * @param radius 单位半径
     * @return 是否发生了推挤
     */
    bool resolvePenetration(float &wx, float &wy, float radius) const;

    /** @brief 只读访问积分场（调试渲染用）。 */
    const std::vector<float> &integration() const { return integ_; }
    /** @brief 只读访问方向场 X 分量（调试渲染用）。 */
    const std::vector<float> &flowX() const { return flowX_; }
    /** @brief 只读访问方向场 Y 分量（调试渲染用）。 */
    const std::vector<float> &flowY() const { return flowY_; }

private:
    bool inBounds(int cx, int cy) const { return cx >= 0 && cy >= 0 && cx < width_ && cy < height_; }
    int index(int cx, int cy) const { return cy * width_ + cx; }
    void worldToCell(float wx, float wy, float &fx, float &fy) const;
    void rebuildFlow();

    int width_ = 0;
    int height_ = 0;
    float cellSize_ = 0.f;
    float originX_ = 0.f;
    float originY_ = 0.f;
    std::vector<float> cost_;     // 地形代价（0=阻挡，>=1 可走）
    std::vector<float> integ_;    // Dijkstra 积分场
    std::vector<float> flowX_;    // 每格方向 X
    std::vector<float> flowY_;    // 每格方向 Y
    std::vector<int> goals_;      // 目标格 (x,y) 对
    bool built_ = false;
};

}  // namespace eve::crowd
