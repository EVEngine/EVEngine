#pragma once

// 状态（buff/debuff）系统的运行时数据类型。
//
// 术语区分：
//   Effect（效果，见 Effect.h）  — 数据驱动的"定义/模板"（伤害多少、持续多久、如何叠加……）
//   Status（状态，本文件）        — Effect 被施加到某个 RPGActor 后产生的"运行时实例"
//   Buff                          — 与 Status 同义的游戏侧称呼；API 同时提供 applyBuff 等别名
//
// 一个 EffectDefinition 可以同时被多个 actor、多次实例化为独立的 StatusInstance。

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve::rpg {

/** @brief 状态实例：某个 Effect 施加到某个 actor 后的运行时记录。 */
struct StatusInstance {
    int instanceId = 0;
    std::string effectId;
    std::string source;  ///< 施加来源标签（施法者 id / 技能 id / 自定义），供查询与批量移除
    int stacks = 1;
    float remaining = -1.f;  ///< 剩余时间（秒）；-1 表示 infinite（永久，直到手动移除）
    float periodAccum = 0.f;  ///< 周期效果：距离上次 tick 的累积时间

    /** @brief 若该效果在 apply 时直接写入了属性修改器，记录 (属性名, 修改器 id) 以便精确撤销。 */
    std::vector<std::pair<std::string, std::string>> appliedModifiers;

    /**
     * @brief 运行时自定义键值（图标路径、UI 着色、脚本标记……）。
     * 与 EffectDefinition::extra（定义侧）互补：extra 是模板数据，props 是实例数据。
     */
    std::unordered_map<std::string, std::string> props;
};

/**
 * @brief 周期性状态触发的 tick 事件。周期效果（period > 0）不会自动修改属性，
 * 而是每个周期产生一个 tick 事件交给上层（脚本或 C++ 结算系统）处理——
 * 这样伤害/治疗类周期效果可以完整地走 Settlement 流水线（护甲、抗性、暴击……），
 * 而不是被引擎硬编码为直接扣血。
 */
struct StatusTickEvent {
    class RPGActor *actor = nullptr;
    int instanceId = 0;
    std::string effectId;
    std::string source;
    int stacks = 1;
};

/**
 * @brief 状态生命周期变更事件（施加 / 刷新 / 叠层 / 延长 / 移除 / 到期 / 拒绝）。
 * 与 StatusTickEvent 分开：tick 是周期性数值触发，change 是实例结构变化。
 *
 * action 取值（字符串，便于脚本与扩展）：
 *   "apply" | "refresh" | "extend" | "stack" | "remove" | "expire" | "reject"
 */
struct StatusChangeEvent {
    class RPGActor *actor = nullptr;
    int instanceId = 0;
    std::string effectId;
    std::string source;
    std::string action;
    int stacks = 0;
    std::string reason;  ///< reject / 自定义条件失败时的说明；其它情况可空
};

}  // namespace eve::rpg
