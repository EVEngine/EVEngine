#pragma once
#include "common/Export.h"


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
class EVENGINE_API_WORLD WeaponSystem {
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

/**
 * @brief 声明 `IProjectileService` 的脚本绑定接缝。
 *
 * 接口由 weapon 模块拥有（它是提问方：谁问"这一发打出去以后怎么办"，就由谁负责编组，
 * `R-COHESION-6`），因此编组也由本模块做——把 `WeaponEntity` 的标识与 `AttackRequest`
 * 的字段**摊平成位置标量**，脚本只收稳定标识、方向与标量，收不到引擎内部借用指针。
 * 这正是 `ScriptProvider.h` 写明的立场（"marshalling is the caller's … not a
 * general-purpose reflection bridge"）；代价是脚本方法的实参个数等于摊平后的字段数。
 *
 * @remarks 幂等：`declareCapability` 替换上一对回调。脚本提供方占用的是 `cap::provide`
 *          的**单实现槽位**（消费方 `ProjectileLogic::fire` 用 `cap::query`），因此发布
 *          脚本提供方会顶掉已有的原生提供方，且卸载时**不会**把它装回来；顶掉这件事会
 *          记一条日志，避免"原生实现悄悄失效"。
 * @cost 组合期一次 map 插入；每次开火一次脚本调用，成本见 `ScriptProjectileService`。
 */
void declareScriptProjectileSeam();

/**
 * @brief 撤回本模块声明的脚本接缝，并卸载全部脚本提供方。
 *
 * 由 `Weapon` 的析构调用。适配器对象**保留而不销毁**：脚本可能在自己的回调里卸载自己，
 * 而正在进行的那次开火仍持有它的指针（此后查询会拿到 `StaleHandle` 而不是被再次调用）。
 *
 * @remarks 接缝状态是进程级的（能力表就是进程级的），所以第二个存活的 `Weapon` 实例
 *          （测试场景；生产侧经 `ModuleManager::requireInstance`）与它共享同一份状态。
 */
void withdrawScriptProjectileSeam();

}  // namespace eve::weapon
