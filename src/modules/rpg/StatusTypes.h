#pragma once

// 状态（buff/debuff）系统的运行时数据类型。
//
// 术语区分：
//   Effect（效果，见 Effect.h）  — 数据驱动的"定义/模板"（伤害多少、持续多久、如何叠加……）
//   Status（状态，本文件）        — Effect 被施加到某个 RPGActor 后产生的"运行时实例"
//
// 一个 EffectDefinition 可以同时被多个 actor、多次实例化为独立的 StatusInstance。

#include <string>
#include <utility>
#include <vector>

namespace eve::rpg {

/** 状态实例：某个 Effect 施加到某个 actor 后的运行时记录。 */
struct StatusInstance {
    int instanceId = 0;
    std::string effectId;
    std::string source;  ///< 施加来源标签（施法者 id / 技能 id / 自定义），供查询与批量移除
    int stacks = 1;
    float remaining = -1.f;  ///< 剩余时间（秒）；-1 表示 infinite（永久，直到手动移除）
    float periodAccum = 0.f;  ///< 周期效果：距离上次 tick 的累积时间

    /** 若该效果在 apply 时直接写入了属性修改器，记录 (属性名, 修改器 id) 以便精确撤销。 */
    std::vector<std::pair<std::string, std::string>> appliedModifiers;
};

/**
 * 周期性状态触发的 tick 事件。周期效果（period > 0）不会自动修改属性，
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

}  // namespace eve::rpg
