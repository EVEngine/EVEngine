#include "rpg/ClassSystem.h"

#include "rpg/Class.h"
#include "rpg/LevelSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/SkillSystem.h"
#include "rpg/TraitSystem.h"

#include <algorithm>

namespace eve::rpg {

bool ClassSystem::setClass(RPGActor *actor, const std::string &classId) {
    if (!actor) return false;
    const ClassDefinition *def = ClassRegistry::find(classId);
    if (!def) return false;
    auto &ci = *actor->classInfo();
    // 撤销旧职业特征
    if (!ci.classId.empty()) {
        TraitSystem::removeBySource(actor, "class:" + ci.classId);
    }
    ci.classId = classId;
    ci.skillsSyncedUpTo = 0;
    // 施加新职业特征
    for (const auto &traitId : def->traits) {
        TraitSystem::apply(actor, traitId, "class:" + classId);
    }
    checkLevelSkills(actor);
    return true;
}

std::string ClassSystem::getClassId(RPGActor *actor) {
    return actor ? actor->classInfo()->classId : std::string{};
}

bool ClassSystem::hasClass(RPGActor *actor, const std::string &classId) {
    return actor && actor->classInfo()->classId == classId;
}

int ClassSystem::checkLevelSkills(RPGActor *actor) {
    if (!actor) return 0;
    const ClassDefinition *def = ClassRegistry::find(actor->classInfo()->classId);
    if (!def) return 0;
    auto &ci = *actor->classInfo();
    const int level = LevelSystem::getLevel(actor);
    int learned = 0;
    for (const auto &ls : def->learnSkills) {
        if (ls.level <= ci.skillsSyncedUpTo) continue;  // 已处理
        if (ls.level <= level) {
            if (!SkillSystem::knows(actor, ls.skillId)) {
                SkillSystem::learn(actor, ls.skillId);
                ++learned;
            }
        }
    }
    ci.skillsSyncedUpTo = std::max(ci.skillsSyncedUpTo, level);
    return learned;
}

int ClassSystem::getLearnCount(RPGActor *actor) {
    if (!actor) return 0;
    const ClassDefinition *def = ClassRegistry::find(actor->classInfo()->classId);
    return def ? int(def->learnSkills.size()) : 0;
}

std::string ClassSystem::getLearnSkillIdAt(RPGActor *actor, int index) {
    if (!actor) return {};
    const ClassDefinition *def = ClassRegistry::find(actor->classInfo()->classId);
    if (!def || index < 0 || size_t(index) >= def->learnSkills.size()) return {};
    return def->learnSkills[size_t(index)].skillId;
}

int ClassSystem::getLearnLevelAt(RPGActor *actor, int index) {
    if (!actor) return 0;
    const ClassDefinition *def = ClassRegistry::find(actor->classInfo()->classId);
    if (!def || index < 0 || size_t(index) >= def->learnSkills.size()) return 0;
    return def->learnSkills[size_t(index)].level;
}

}  // namespace eve::rpg