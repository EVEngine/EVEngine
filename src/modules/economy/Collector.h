#pragma once

#include <string>

namespace eve::economy {

class GatherNode;

/**
 * @brief 采集者：WorkerTrip 基线的状态机
 * （选目标→接近→采集→载荷判定→运输→卸货入账）。
 */
class Collector {
public:
    /** @brief 采集状态机的相位。 */
    enum class State { Idle, MovingToNode, Gathering, MovingToDrop, Depositing };

    /**
     * @param carryCapacity 单次最大携带量。
     * @param gatherRatePerTick 每 tick 入舱量。
     * @param travelTicks 单程移动耗时（去程/回程共用）。
     */
    Collector(int carryCapacity, int gatherRatePerTick, int travelTicks);

    /** @brief 指派目标节点并占用其采集槽位；无空位时保持待命并返回 false。 */
    bool assign(GatherNode* node);
    /** @brief 取消指派并释放槽位。 */
    void clearAssignment();

    /** @brief 推进一个 tick；卸货时入账到指定玩家的账本。 */
    void tick(int player);

    /** @brief 是否处于待命状态。 */
    bool isIdle() const;
    /** @brief 当前相位。 */
    State state() const;
    /** @brief 当前相位的字符串名（脚本友好）。 */
    std::string stateName() const;
    /** @brief 当前携带量。 */
    int cargo() const;
    /** @brief 单次最大携带量。 */
    int carryCapacity() const;
    /** @brief 正在采集的资源类型 id。 */
    std::string resourceType() const;
    /** @brief 累计入账量（不含浪费）。 */
    int totalGathered() const;
    /** @brief 完成的往返次数。 */
    int trips() const;

    /** @brief 脚本生命周期入口。 */
    void destroy();

private:
    void startMoveToNode();
    void startMoveToDrop();
    void deposit(int player);

    GatherNode* node_              = nullptr;
    State       state_             = State::Idle;
    int         carryCapacity_     = 0;
    int         gatherRatePerTick_ = 0;
    int         travelTicks_       = 0;
    int         travelRemaining_   = 0;
    int         cargo_             = 0;
    std::string type_;
    int         totalGathered_     = 0;
    int         trips_             = 0;
};

}  // namespace eve::economy
