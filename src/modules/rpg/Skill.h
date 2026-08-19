#pragma once

// 技能系统：数据驱动的 SkillDefinition（消耗、冷却、读条时间、命中后授予的效果……）。
//
// 灵活性/可定制点：
//  - targetType 是自由字符串（"self"/"single"/"area"/... 或游戏自定义值），
//    引擎不解释其含义——具体的目标选取逻辑完全由脚本/游戏代码实现，
//    SkillSystem 只负责冷却/消耗/读条与授予效果的通用流程。
//  - extra 字段是任意 string->string 的自定义数据（动画名、投射物速度……），
//    不需要为每个新技能特性修改引擎结构体。
//  - 消耗的属性同样是字符串，可以是内建的 "mana"，也可以是任何自定义资源。

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

struct SkillCostSpec {
    std::string attribute;
    double amount = 0.0;
};

struct SkillDefinition {
    std::string id;
    float cooldown = 0.f;
    float castTime = 0.f;  ///< 0 = 瞬发
    std::string targetType = "self";
    std::vector<SkillCostSpec> costs;
    std::vector<std::string> grantedEffects;  ///< 命中/释放后施加给目标的效果 id 列表
    std::vector<std::string> tags;
    std::unordered_map<std::string, std::string> extra;  ///< 游戏自定义附加数据

    bool hasTag(const std::string &tag) const;
};

class SkillRegistry {
public:
    static void registerSkill(const SkillDefinition &def);
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
