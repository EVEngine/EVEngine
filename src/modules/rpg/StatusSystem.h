#pragma once
#include "common/Export.h"


// 状态（buff/debuff）系统：把 RPG EffectDefinition 适配到通用
// effects::EffectContainer，并由 RPG executor 驱动 modifier / 周期结算。
//
// 可插拔扩展点（与 inventory 模块同风格）：
//  - registerApplyCondition  — 免疫 / 抗性 / 互斥等"能否施加"判断
//  - registerStackPolicy     — 内置 none/refresh/extend/stack 之外的自定义叠加策略
//  - registerLifecycleHook   — 施加/刷新/叠层/移除/到期时的副作用（UI、成就、VFX）
//  - EffectDefinition::extra / StatusInstance::props — 任意自定义字段无需扩结构体

#include "common/Result.h"
#include "rpg/StatusTypes.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

class RPGActor;
struct EffectDefinition;

/** @brief EVENGINE_API_PLATFORM public API. */
class EVENGINE_API_PLATFORM StatusSystem {
public:
    /**
     * @brief 施加前条件：返回 false 表示拒绝施加。
     * 全部已注册条件按注册表遍历（AND）；任一失败则 apply 返回 -1 并产生 action="reject" 事件。
     */
    using ApplyCondition = std::function<bool(RPGActor *actor, const EffectDefinition &def,
                                               const std::string &source, std::string &outReason)>;

    /**
     * @brief 自定义叠加策略：在已有效果的临时 StatusInstance projection 上运行。
     *
     * 返回值约定与 apply() 相同（成功返回 adapter-owned 实例 id）。传入的
     * `existing` 永远是候选副本；策略不得通过 actor 直接修改状态或属性。
     * 只有回调成功且候选数据通过校验后，StatusSystem 才会一次性提交
     * EffectContainer、AttributeSet 和 executor metadata。
     */
    using StackPolicyFn = std::function<int(RPGActor *actor, StatusInstance &existing,
                                             const EffectDefinition &def, const std::string &source)>;

    /** @brief 生命周期钩子：每次产生 StatusChangeEvent 时同步回调（在事件入队之后）。 */
    using LifecycleHook = std::function<void(const StatusChangeEvent &ev)>;

    /** @brief Registers apply condition. */
    static void registerApplyCondition(const std::string &name, ApplyCondition fn);
    /** @brief Unregisters apply condition. */
    static void unregisterApplyCondition(const std::string &name);
    /** @brief True when apply condition. */
    static bool hasApplyCondition(const std::string &name);
    /** @brief Clears apply conditions. */
    static void clearApplyConditions();

    /** @brief Registers stack policy. */
    static void registerStackPolicy(const std::string &name, StackPolicyFn fn);
    /** @brief Unregisters stack policy. */
    static void unregisterStackPolicy(const std::string &name);
    /** @brief True when stack policy. */
    static bool hasStackPolicy(const std::string &name);
    /** @brief Clears stack policies. */
    static void clearStackPolicies();

    /** @brief Registers lifecycle hook. */
    static void registerLifecycleHook(const std::string &name, LifecycleHook fn);
    /** @brief Unregisters lifecycle hook. */
    static void unregisterLifecycleHook(const std::string &name);
    /** @brief True when lifecycle hook. */
    static bool hasLifecycleHook(const std::string &name);
    /** @brief Clears lifecycle hooks. */
    static void clearLifecycleHooks();

    /**
     * @brief Status application backed by the generic EffectContainer.
     * @return Success with the adapter-owned legacy integer id (zero for an
     *         instant effect), or a structured rejection. The string
     *         EffectInstance id remains the lifecycle identity.
     */
    [[nodiscard]] static eve::Result<int> apply(RPGActor *actor, const std::string &effectId,
                                                const std::string &source = "");

    /**
     * @brief Removal of one status by its legacy adapter id.
     * @return Applied when removed, or NotFound/Rejected with diagnostics.
     */
    [[nodiscard]] static eve::Result<void> remove(RPGActor *actor, int instanceId);

    /** @brief 移除该 actor 身上所有 effectId 匹配的实例；返回移除数量。 */
    static int removeByEffect(RPGActor *actor, const std::string &effectId);

    /** @brief 移除该 actor 身上所有 source 匹配的实例；返回移除数量。 */
    static int removeBySource(RPGActor *actor, const std::string &source);

    /** @brief 移除该 actor 身上所有 tag 匹配（效果定义带该 tag）的实例；返回移除数量。 */
    static int removeByTag(RPGActor *actor, const std::string &tag);

    /** @brief True when effect. */
    static bool hasEffect(RPGActor *actor, const std::string &effectId);
    /** @brief 是否存在任一效果定义带有该 tag 的活动实例。 */
    static bool hasTag(RPGActor *actor, const std::string &tag);
    /** @brief Returns the active count. */
    static int getActiveCount(RPGActor *actor);
    /** @brief Returns the active effect id. */
    static std::string getActiveEffectId(RPGActor *actor, int index);
    /** @brief Returns the active stacks. */
    static int getActiveStacks(RPGActor *actor, int index);
    /** @brief Returns the active remaining. */
    static float getActiveRemaining(RPGActor *actor, int index);
    /** @brief Returns the active instance id. */
    static int getActiveInstanceId(RPGActor *actor, int index);
    /** @brief Returns the active source. */
    static std::string getActiveSource(RPGActor *actor, int index);

    /** @brief 按实例 id 读写 props；找不到实例时 get 返回 fallback，set 返回 false。 */
    static std::string getProp(RPGActor *actor, int instanceId, const std::string &key,
                               const std::string &fallback = {});
    /** @brief Sets the prop. */
    static bool setProp(RPGActor *actor, int instanceId, const std::string &key,
                        const std::string &value);

    /**
     * @brief Advance all live RPG status executors with an injected simulation delta.
     * @return Expiry/tick summary; invalid deltas leave all actor state unchanged.
     */
    [[nodiscard]] static eve::Result<StatusUpdateSummary> update(double dt);

    /** @brief 取出并清空自上次调用以来累积的周期 tick 事件（push/poll 风格，见 event 模块）。 */
    static void pollTicks(std::vector<StatusTickEvent> &out);

    /** @brief 取出并清空自上次调用以来累积的生命周期变更事件。 */
    static void pollChanges(std::vector<StatusChangeEvent> &out);

private:
    static void emitChange(StatusChangeEvent ev);

    static std::unordered_map<std::string, ApplyCondition> &applyConditions();
    static std::unordered_map<std::string, StackPolicyFn> &stackPolicies();
    static std::unordered_map<std::string, LifecycleHook> &lifecycleHooks();
};

}  // namespace eve::rpg
