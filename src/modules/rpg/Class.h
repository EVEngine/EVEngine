#pragma once

/**
 * @file Class.h
 * @brief 职业（Class）定义：可学技能（升级解锁）与职业基础特征，进程级注册表。
 *
 * 与 RPG Maker 的职业模型对齐：职业决定"升到 N 级可学某技能"，并可携带
 * 一组职业基础特征（由 ClassSystem 以 source="class:<classId>" 施加）。
 */

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

/** @brief 一条"升到 level 级可学 skillId"的规格。 */
struct ClassLearnSkill {
    std::string skillId;
    int level = 1;
};

/** @brief 数据驱动的职业模板。 */
struct ClassDefinition {
    std::string id;
    std::string displayName;
    std::vector<ClassLearnSkill> learnSkills;
    std::vector<std::string> traits;  ///< 职业基础特征 id（施加于 actor）
    std::vector<std::string> tags;
    std::unordered_map<std::string, std::string> extra;

    bool hasTag(const std::string &tag) const;
    std::string getExtra(const std::string &key, const std::string &fallback = {}) const;
};

/** @brief 进程级职业定义注册表。 */
class ClassRegistry {
public:
    static void registerClass(const ClassDefinition &def);
    /**
     * @brief Find a registered class definition by id.
     * @return Borrowed nullable definition owned by the process-local registry.
     * @ownership ClassRegistry owns the definition; callers must not delete or mutate it.
     * @lifetime Valid until remove(), clear(), or replacement of the same id; copy data before registry mutation.
     */
    static const ClassDefinition *find(const std::string &id);
    /** @brief Remove a class by id (compatibility facade returning whether it was present). */
    static bool remove(const std::string &id);
    static void clear();
    static int count();

    /** @brief 从 JSON 数组/对象批量注册。元素形如：
     * { "id":"warrior","displayName":"战士",
     *   "traits":["mighty"],
     *   "learnSkills":[{"skillId":"power_strike","level":2}],
     *   "tags":["physical"] } */
    static int loadFromJson(const std::string &json, std::string *error = nullptr);
};

}  // namespace eve::rpg