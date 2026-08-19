#pragma once

// 状态（buff/debuff）系统：把 EffectDefinition 施加/移除为具体 actor 身上的
// StatusInstance，并驱动持续时间 / 周期 tick。
//
// 可插拔扩展点（与 inventory 模块同风格）：
//  - registerApplyCondition  — 免疫 / 抗性 / 互斥等"能否施加"判断
//  - registerStackPolicy     — 内置 none/refresh/extend/stack 之外的自定义叠加策略
//  - registerLifecycleHook   — 施加/刷新/叠层/移除/到期时的副作用（UI、成就、VFX）
//  - EffectDefinition::extra / StatusInstance::props — 任意自定义字段无需扩结构体

#include "rpg/StatusTypes.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

class RPGActor;
struct EffectDefinition;

class StatusSystem {
public:
    /**
     * @brief 施加前条件：返回 false 表示拒绝施加。
     * 全部已注册条件按注册表遍历（AND）；任一失败则 apply 返回 -1 并产生 action="reject" 事件。
     */
    using ApplyCondition = std::function<bool(RPGActor *actor, const EffectDefinition &def,
                                               const std::string &source, std::string &outReason)>;

    /**
     * @brief 自定义叠加策略：当已存在同 effectId 实例且 stackPolicy 名命中本注册表时调用。
     * 返回值约定与 apply() 相同（-1 拒绝 / >0 实例 id）。策略内部可改写 stacks/remaining，
     * 并应在需要时自行调用 AttributeSystem 或依赖 StatusSystem 后续的 modifier 刷新。
     * 返回后若实例仍在列表中且 period<=0，StatusSystem 会按新 stacks 重写属性修改器。
     */
    using StackPolicyFn = std::function<int(RPGActor *actor, StatusInstance &existing,
                                             const EffectDefinition &def, const std::string &source)>;

    /** @brief 生命周期钩子：每次产生 StatusChangeEvent 时同步回调（在事件入队之后）。 */
    using LifecycleHook = std::function<void(const StatusChangeEvent &ev)>;

    static void registerApplyCondition(const std::string &name, ApplyCondition fn);
    static void unregisterApplyCondition(const std::string &name);
    static bool hasApplyCondition(const std::string &name);
    static void clearApplyConditions();

    static void registerStackPolicy(const std::string &name, StackPolicyFn fn);
    static void unregisterStackPolicy(const std::string &name);
    static bool hasStackPolicy(const std::string &name);
    static void clearStackPolicies();

    static void registerLifecycleHook(const std::string &name, LifecycleHook fn);
    static void unregisterLifecycleHook(const std::string &name);
    static bool hasLifecycleHook(const std::string &name);
    static void clearLifecycleHooks();

    /**
     * @brief 施加一个效果。返回值：
     *   -1  — 失败（actor 为空 / 效果不存在 / 条件拒绝 / stackPolicy 拒绝重复）
     *    0  — durationPolicy=="instant"：已立即生效，无需跟踪的状态实例
     *   >0  — 新建或更新后的状态实例 id
     */
    static int apply(RPGActor *actor, const std::string &effectId, const std::string &source = "");

    /** @brief 按实例 id 精确移除（撤销其属性修改器）；返回是否命中。 */
    static bool remove(RPGActor *actor, int instanceId);

    /** @brief 移除该 actor 身上所有 effectId 匹配的实例；返回移除数量。 */
    static int removeByEffect(RPGActor *actor, const std::string &effectId);

    /** @brief 移除该 actor 身上所有 source 匹配的实例；返回移除数量。 */
    static int removeBySource(RPGActor *actor, const std::string &source);

    /** @brief 移除该 actor 身上所有 tag 匹配（效果定义带该 tag）的实例；返回移除数量。 */
    static int removeByTag(RPGActor *actor, const std::string &tag);

    static bool hasEffect(RPGActor *actor, const std::string &effectId);
    /** @brief 是否存在任一效果定义带有该 tag 的活动实例。 */
    static bool hasTag(RPGActor *actor, const std::string &tag);
    static int getActiveCount(RPGActor *actor);
    static std::string getActiveEffectId(RPGActor *actor, int index);
    static int getActiveStacks(RPGActor *actor, int index);
    static float getActiveRemaining(RPGActor *actor, int index);
    static int getActiveInstanceId(RPGActor *actor, int index);
    static std::string getActiveSource(RPGActor *actor, int index);

    /** @brief 按实例 id 读写 props；找不到实例时 get 返回 fallback，set 返回 false。 */
    static std::string getProp(RPGActor *actor, int instanceId, const std::string &key,
                               const std::string &fallback = {});
    static bool setProp(RPGActor *actor, int instanceId, const std::string &key,
                        const std::string &value);

    /** @brief 遍历 RPGActor::liveActors()：倒计时长、到期移除、周期效果产生 tick 事件。 */
    static void update(float dt);

    /** @brief 取出并清空自上次调用以来累积的周期 tick 事件（push/poll 风格，见 event 模块）。 */
    static void pollTicks(std::vector<StatusTickEvent> &out);

    /** @brief 取出并清空自上次调用以来累积的生命周期变更事件。 */
    static void pollChanges(std::vector<StatusChangeEvent> &out);

private:
    static void emitChange(StatusChangeEvent ev);
    static StatusInstance *findByInstanceId(RPGActor *actor, int instanceId);

    static std::unordered_map<std::string, ApplyCondition> &applyConditions();
    static std::unordered_map<std::string, StackPolicyFn> &stackPolicies();
    static std::unordered_map<std::string, LifecycleHook> &lifecycleHooks();
};

}  // namespace eve::rpg
