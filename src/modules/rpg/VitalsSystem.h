#pragma once

/**
 * @file VitalsSystem.h
 * @brief 当前资源（hp/mana...）的受伤 / 治疗 / 复活 / 死亡。
 *
 * 上限（max）取 actor 同名最终属性（getFinalAttribute），当前值存
 * RPGActor::Vitals。受伤、治疗、死亡、复活都会产生 VitalsEvent。
 * 伤害/治疗数值可以由 Settlement 流水线结算后调用 takeDamage/heal 落账。
 */

#include "common/Result.h"

#include <string>
#include <vector>

namespace eve::rpg {

class RPGActor;

/** @brief 生命等资源事件：damage | heal | death | revive。 */
struct VitalsEvent {
    class RPGActor *actor = nullptr;
    std::string resource;
    std::string action;
    double amount = 0.0;
    std::string source;
    double current = 0.0;
    double max = 0.0;
};

/**
 * @brief 当前资源系统：读写 RPGActor::Vitals 并发出事件。
 * @thread 调用线程应与 RPGActor 的 ECS 线程一致。
 */
class VitalsSystem {
public:
    /** @brief 当前值（无 vitals/资源时 0）。 */
    static double getCurrent(RPGActor *actor, const std::string &resource);
    /** @brief 上限：取 actor 同名最终属性（getFinalAttribute），无则 0。 */
    static double getMax(RPGActor *actor, const std::string &resource);
    /** @brief 直接设定当前值，夹在 [0, max]。 */
    static void setCurrent(RPGActor *actor, const std::string &resource, double value);

    /**
     * @brief 造成伤害：current -= amount，下限 0；归零时发 death 事件。
     * @return 实际生效的伤害量（夹在 [0, currentBefore]）。
     */
    static double takeDamage(RPGActor *actor, const std::string &resource, double amount,
                             const std::string &source = "");
    /** @brief 治疗：current += amount，上限 max。返回实际治疗量。 */
    static double heal(RPGActor *actor, const std::string &resource, double amount);
    /** @brief 复活：把资源设为 amount（默认当前上限），并清掉死亡状态。 */
    static void revive(RPGActor *actor, const std::string &resource, double amount = -1.0);
    /** @brief 是否死亡：max > 0 且 current <= 0。 */
    static bool isDead(RPGActor *actor, const std::string &resource);

    /** @brief 取出并清空自上次调用以来累积的资源事件（push/poll 风格）。 */
    static void pollEvents(std::vector<VitalsEvent> &out);
};

}  // namespace eve::rpg