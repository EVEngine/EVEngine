#pragma once

/**
 * @brief 武器行为静态系统：逻辑注册表 + 每帧推进 + 开火/装填。
 * 与 rpg 的 *System 静态类同一设计语言；事件通过事件汇（sink）
 * 交给模块/游戏侧缓存。
 */

#include "weapon/WeaponTypes.h"

#include <functional>
#include <string>

namespace eve::weapon {

class IWeaponLogic;

/** @brief 事件汇：由 Weapon 模块（或测试）注册，把事件写入自己的队列。 */
using WeaponEventSink = std::function<void(const WeaponEvent&)>;

/** @brief 武器系统：逻辑注册 + 更新 + 开火 + 装填。 */
class WeaponSystem {
public:
    /** @brief 注册武器逻辑实现；同名替换。 */
    static void registerLogic(IWeaponLogic* logic);
    /**
     * @brief 按名字取逻辑；未注册返回 nullptr。
     * @return Borrowed nullable implementation owned by the registered provider.
     * @ownership WeaponSystem retains the registry entry but does not own the implementation.
     * @lifetime Valid while registered and until replacement/unregister; do not retain across registry mutation.
     * @thread Call on the weapon registry thread.
     * @reentrancy The lookup invokes no callbacks and does not mutate the registry.
     */
    static IWeaponLogic* findLogic(const std::string& name);
    /** @brief 已注册逻辑数量。 */
    static int logicCount();

    /** @brief 注册事件汇（替换旧的；传 nullptr 清空）。 */
    static void setEventSink(WeaponEventSink sink);

    /** @brief 向事件汇推一条事件（模块/脚本便捷入口）。 */
    static void emitEvent(const WeaponEvent& e);

    /** @brief 每帧推进：冷却 / 连发 / 装填 / 炮口旋转 / 逻辑自身。 */
    static void update(WeaponEntity& w, float dt);

    /** @brief 尝试开火；满足条件时扣弹药、调逻辑、推事件并返回 true。 */
    static bool tryFire(WeaponEntity& w, const FireRequest& req);

    /** @brief 是否可开火（未装填/未卡壳/冷却完毕/有弹药/逻辑放行）。 */
    static bool canFire(WeaponEntity& w);

    /** @brief 开始装填（有备用弹药且弹匣不满时生效）。 */
    static void startReload(WeaponEntity& w);

    /** @brief 打断装填。 */
    static void cancelReload(WeaponEntity& w);

    /** @brief 炮口朝目标旋转；返回本帧是否到位。 */
    static bool updateAim(WeaponEntity& w, float dt);
};

}  // namespace eve::weapon
