#pragma once

// 技能系统：数据驱动的 SkillDefinition（消耗、冷却、读条时间、命中后授予的效果……）。
//
// 灵活性/可定制点：
//  - targetType 是自由字符串（"self"/"single"/"area"/... 或游戏自定义值），
//    引擎不解释其含义——具体的目标选取逻辑完全由脚本/游戏代码实现，
//    SkillSystem 只负责冷却/消耗/读条与授予效果的通用流程。
//  - extra 字段是任意 string->string 的自定义数据（动画名、投射物速度……），
//    不需要为每个新技能特性修改引擎结构体。
//  - 消耗使用 common/resource 的 CostSpec，可以是内建的 "mana"，也可以是
//    任何自定义资源；RPG 不再维护一份私有 Cost 类型。

#include "common/ResourceAccount.h"
#include "decision/Condition.h"
#include "definitions/Definitions.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

struct SkillDefinition {
    std::string id;
    float cooldown = 0.f;
    float castTime = 0.f;  ///< 0 = 瞬发
    std::string targetType = "self";
    /** @brief Canonical resource cost; AttributeSet is an adapter, not a second cost model. */
    std::optional<eve::resource::CostSpec> cost;
    std::vector<std::string> grantedEffects;  ///< 命中/释放后施加给目标的效果 id 列表
    std::vector<std::string> tags;
    /** @brief Side-effect-free condition tree required before this skill can cast. */
    eve::decision::Condition                     castCondition;
    std::unordered_map<std::string, std::string> extra;  ///< 游戏自定义附加数据

    bool hasTag(const std::string &tag) const;
};

class SkillRegistry {
public:
    /**
     * @brief Return the common registry that owns canonical skill JSON.
     * @remarks The registry is process-local and thread-affine, matching the
     * legacy static SkillRegistry contract. Typed runtime adapters bind to it
     * through a generation-qualified DefinitionRef.
     */
    [[nodiscard]] static eve::definitions::DefinitionRegistry &definitionRegistry();

    static void registerSkill(const SkillDefinition &def);
    /**
     * @brief Find a registered skill definition by id.
     * @return Borrowed nullable definition owned by the process-local registry.
     * @ownership SkillRegistry owns the definition; callers must not delete or mutate it.
     * @lifetime Valid until remove(), clear(), or replacement of the same id; copy data before registry mutation.
     * @thread Call on the RPG registry thread.
     * @reentrancy The lookup does not invoke callbacks and is not valid across re-entrant registry mutation.
     */
    static const SkillDefinition *find(const std::string &id);
    static bool remove(const std::string &id);
    static void clear();
    static int count();

    /**
     * @brief 从 JSON 数组批量注册技能定义，返回成功注册数量。元素形如：
     * {
     *   "id": "fireball", "cooldown": 4, "castTime": 1.2, "targetType": "single",
     *   "costs": [{"attribute":"mana","amount":20}],
     *   "grantedEffects": ["burning"], "tags": ["fire"],
     *   "extra": {"projectile": "fireball_vfx"}
     * }
     */
    static int loadFromJson(const std::string &json, std::string *error = nullptr);
};

}  // namespace eve::rpg
