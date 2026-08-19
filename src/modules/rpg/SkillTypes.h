#pragma once

// 技能系统的运行时数据类型（每个 actor 已学会技能的冷却 / 施法状态）。
// 技能的静态定义见 Skill.h 的 SkillDefinition / SkillRegistry。

#include <string>

namespace eve::rpg {

/** 单个已学会技能的运行时状态（冷却）。 */
struct SkillRuntime {
    float cooldownRemaining = 0.f;
};

/** 施法状态：同一时间每个 actor 只有一个进行中的引导/读条（可按需扩展为多槽）。 */
struct CastingState {
    bool active = false;
    std::string skillId;
    float remaining = 0.f;
    float totalCastTime = 0.f;
    class RPGActor *target = nullptr;
};

/** 技能释放结算事件：由 SkillSystem 在读条完成 / 瞬发技能释放时产生。 */
struct SkillCastEvent {
    class RPGActor *caster = nullptr;
    class RPGActor *target = nullptr;  ///< 可能为空（自身或无目标技能）
    std::string skillId;
};

}  // namespace eve::rpg
