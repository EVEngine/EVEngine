#pragma once
/** @file SkillTypes.h
 * @brief rpg module-interface: skill clocks and explicit table-local cast target links.
 */

// 技能系统的运行时数据类型（每个 actor 已学会技能的冷却 / 施法状态）。
// 技能的静态定义见 Skill.h 的 SkillDefinition / SkillRegistry。

#include <optional>
#include <string>
#include "common/ECS.h"
#include "common/SubjectRef.h"
#include "rpg/ActorEventRef.h"

namespace eve::rpg {

/** @brief 单个已学会技能的运行时状态（冷却）。 */
struct SkillRuntime {
    float cooldownRemaining = 0.f;
};

/** @brief Explicit default-self intent, a same-world link, or observed target retirement. */
enum class CastTargetIntent { DefaultSelf, Linked, Retired };
/** @brief Non-owning RPG-family cast target within the caster's physical ECS Table.
 * @ownership Contains intent and slot/type/generation only; never keeps an actor or world alive.
 * @remarks DefaultSelf requires an empty link. Linked requires a live same-Table RPGActor at resolution.
 * Retirement/reuse cancels resolution; the update discards the retired native handle before advancing its timer.
 * The Table owner outlives the synchronous update; caster destruction retires its state and world destruction
 * retires both. World restore must explicitly rebuild links; no process authority is serialized here.
 */
struct CastingTarget {
    CastTargetIntent  intent = CastTargetIntent::DefaultSelf;
    ecs::EntityHandle entity;
};

/** @brief Coordinator-admitted identity and current incarnation of one checkpoint target.
 * @ownership Non-owning synchronous binding. The coordinator proves subject belongs to this exact entity;
 * RPG validates the saved subject against ActorIdentity, same physical Table, generation and RPG family
 * before publication. Referenced actors must be registered before admitting a persistent target binding.
 * Neither this binding nor a native handle is serialized. Table owners outlive the synchronous call.
 */
struct CastTargetBinding {
    SubjectRef        subject;
    ecs::EntityHandle entity;
};

/** @brief 施法状态：同一时间每个 actor 只有一个进行中的引导/读条（可按需扩展为多槽）。 */
struct CastingState {
    bool active = false;
    std::string skillId;
    float remaining = 0.f;
    float totalCastTime = 0.f;
    CastingTarget target;
};

/** @brief 技能释放结算事件：由 SkillSystem 在读条完成 / 瞬发技能释放时产生。 */
struct SkillCastEvent {
    ActorEventRef caster;
    ActorEventRef target;  ///< None denotes default-self/no-target intent; explicit self retains its reference.
    std::string skillId;
};

}  // namespace eve::rpg
