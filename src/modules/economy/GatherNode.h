#pragma once

#include <string>

namespace eve::economy {

/**
 * @brief 地图上的资源节点：储量、再生、采集槽位。
 */
class GatherNode {
public:
    /**
     * @param type 资源类型 id（须已在注册表中）。
     * @param capacity 总储量。
     * @param workerSlots 同时采集槽位上限（>0）。
     * @param regenPerTick 每 tick 再生量（0=不再生）。
     */
    GatherNode(std::string type, int capacity, int workerSlots = 1, int regenPerTick = 0);

    /** @brief 资源类型 id。 */
    std::string resourceType() const;
    /** @brief 当前储量。 */
    int amount() const;
    /** @brief 总储量。 */
    int capacity() const;
    /** @brief 是否已采空。 */
    bool depleted() const;

    /** @brief 尝试取出 amount；返回实际取出量（不超过当前储量）。 */
    int extract(int amount);
    /** @brief 占用一个采集槽位；无空位返回 false。 */
    bool tryOccupySlot();
    /** @brief 释放一个采集槽位。 */
    void releaseSlot();
    /** @brief 剩余空槽数。 */
    int freeSlots() const;

    /** @brief 每 tick 再生（Growing/Renewable 模型）。 */
    void regenTick();

    /** @brief 脚本生命周期入口。 */
    void destroy();

private:
    std::string type_;
    int         capacity_       = 0;
    int         amount_         = 0;
    int         regenPerTick_   = 0;
    int         workerSlots_    = 1;
    int         occupiedSlots_  = 0;
};

}  // namespace eve::economy
