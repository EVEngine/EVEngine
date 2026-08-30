#pragma once

/**
 * @file EquipmentSystem.h
 * @brief 装备加成：把已装备物品（inventory::EquipmentSet）的注册属性加成同步到
 * actor 的属性修改器上。
 *
 * 与 inventory 松耦合：EquipmentSet 只搬运物品，属性加成由本系统按槽位以
 * source="equip:<slot>" 的修改器应用到 RPGActor。物品→加成映射由本系统维护的
 * 进程级注册表提供（C++ 或 JSON）。每次 sync 先清除该槽位旧加成再重新施加，
 * 保证穿脱/换装后最终属性一致。
 */

#include "common/Result.h"

#include <string>
#include <vector>

namespace eve::inventory {
class EquipmentSet;
}

namespace eve::rpg {

class RPGActor;

/** @brief 一件物品的一条属性加成规格。 */
struct EquipmentStat {
    std::string attribute;
    std::string op = "add";
    double value = 0.0;
    int priority = 0;
};

/**
 * @brief 装备加成系统。
 * @thread 调用线程应与 RPGActor 的 ECS 线程一致。
 */
class EquipmentSystem {
public:
    /** @brief 为某物品 id 注册一条属性加成。 */
    static void registerItemStat(const std::string &itemId, const EquipmentStat &stat);
    /** @brief 批量注册某物品的属性加成；返回成功条数。 */
    static int registerItemStatsFromJson(const std::string &itemId, const std::string &json);
    /** @brief 清除某物品的加成。 */
    static void clearItemStats(const std::string &itemId);
    /** @brief 清除全部物品加成注册。 */
    static void clearAll();
    /** @brief 已注册加成的物品数量。 */
    static int statItemCount();

    /**
     * @brief 把装备栏当前装备同步为 actor 属性修改器（先清旧再施加）。
     * @return 本次实际施加的修改器条数。
     */
    static int syncEquipModifiers(RPGActor *actor, eve::inventory::EquipmentSet *equip);
};

}  // namespace eve::rpg