#include "rpg/Class.h"

#include "common/Json.h"

#include <algorithm>

namespace eve::rpg {

using eve::json::Value;

bool ClassDefinition::hasTag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::string ClassDefinition::getExtra(const std::string &key, const std::string &fallback) const {
    auto it = extra.find(key);
    return it == extra.end() ? fallback : it->second;
}

namespace {
std::unordered_map<std::string, ClassDefinition> &classTable() {
    static std::unordered_map<std::string, ClassDefinition> t;
    return t;
}
}  // namespace

void ClassRegistry::registerClass(const ClassDefinition &def) {
    if (def.id.empty()) return;
    classTable()[def.id] = def;
}

const ClassDefinition *ClassRegistry::find(const std::string &id) {
    auto &t = classTable();
    auto it = t.find(id);
    return it == t.end() ? nullptr : &it->second;
}

bool ClassRegistry::remove(const std::string &id) { return classTable().erase(id) > 0; }

void ClassRegistry::clear() { classTable().clear(); }

int ClassRegistry::count() { return int(classTable().size()); }

int ClassRegistry::loadFromJson(const std::string &json, std::string *error) {
    std::string err;
    const eve::json::Document doc = eve::json::Document::parse(json, &err);
    if (!doc.valid()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return 0;
    }
    const Value root = doc.root();
    int n = 0;
    const auto parseOne = [&](Value o) {
        if (!o.isObject()) return;
        ClassDefinition def;
        def.id = o.getString("id");
        if (def.id.empty()) return;
        def.displayName = o.getString("displayName");
        def.tags = o.getStringArray("tags");
        def.extra = o.getStringMap("extra");
        def.traits = o.getStringArray("traits");
        const Value learn = o.get("learnSkills");
        for (size_t i = 0; i < learn.size(); ++i) {
            const Value e = learn.at(i);
            ClassLearnSkill ls;
            ls.skillId = e.getString("skillId");
            ls.level = e.getInt("level", 1);
            if (ls.level <= 0) ls.level = 1;
            if (ls.skillId.empty()) continue;
            def.learnSkills.push_back(std::move(ls));
        }
        classTable()[def.id] = std::move(def);
        ++n;
    };
    if (root.isArray()) {
        for (size_t i = 0; i < root.size(); ++i) parseOne(root.at(i));
    } else if (root.isObject()) {
        parseOne(root);
    }
    return n;
}

}  // namespace eve::rpg

// ClassSystem implementation (previously ClassSystem.cpp).
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
    ci.classId          = classId;
    ci.skillsSyncedUpTo = 0;
    // 施加新职业特征
    for (const auto &traitId : def->traits) {
        TraitSystem::apply(actor, traitId, "class:" + classId);
    }
    checkLevelSkills(actor);
    return true;
}

std::string ClassSystem::getClassId(RPGActor *actor) { return actor ? actor->classInfo()->classId : std::string{}; }

bool ClassSystem::hasClass(RPGActor *actor, const std::string &classId) {
    return actor && actor->classInfo()->classId == classId;
}

int ClassSystem::checkLevelSkills(RPGActor *actor) {
    if (!actor) return 0;
    const ClassDefinition *def = ClassRegistry::find(actor->classInfo()->classId);
    if (!def) return 0;
    auto     &ci      = *actor->classInfo();
    const int level   = LevelSystem::getLevel(actor);
    int       learned = 0;
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

// LevelSystem implementation (previously LevelSystem.cpp).
#include "rpg/LevelSystem.h"

#include "rpg/RPGActor.h"

#include <cmath>

namespace eve::rpg {

namespace {
std::vector<LevelUpEvent> &levelUpEvents() {
    static std::vector<LevelUpEvent> events;
    return events;
}
}  // namespace

int LevelSystem::getLevel(RPGActor *actor) { return actor ? actor->progression()->level : 1; }

double LevelSystem::getXp(RPGActor *actor) { return actor ? actor->progression()->xp : 0.0; }

double LevelSystem::getXpToNext(RPGActor *actor) { return actor ? actor->progression()->xpToNext : 100.0; }

void LevelSystem::setXpToNext(RPGActor *actor, double value) {
    if (!actor) return;
    actor->progression()->xpToNext = value > 0.0 ? value : 0.0;
}

void LevelSystem::setLevel(RPGActor *actor, int level) {
    if (!actor) return;
    actor->progression()->level = level > 0 ? level : 1;
}

eve::Result<void> LevelSystem::restoreProgression(RPGActor *actor, int level, double xp, double xpToNext) {
    if (!actor)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "progression restore requires an actor", "actor"));
    if (level < 1)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "progression level must be positive", "level"));
    if (!std::isfinite(xp) || xp < 0.0)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "progression xp must be finite and non-negative", "xp"));
    if (!std::isfinite(xpToNext) || xpToNext <= 0.0 || xp >= xpToNext)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "progression threshold must be finite, positive and greater than current xp", "xpToNext"));
    auto candidate        = *actor->progression();
    candidate.level       = level;
    candidate.xp          = xp;
    candidate.xpToNext    = xpToNext;
    *actor->progression() = candidate;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool LevelSystem::gainXp(RPGActor *actor, double amount, double xpGrowth) {
    auto prepared = prepareGainXp(actor, amount, xpGrowth);
    if (!prepared.ok()) return false;
    auto committed = commitGainXp(std::move(prepared).takeValue());
    return committed.ok() && std::move(committed).takeValue() > 0;
}

eve::Result<PreparedProgressionGain> LevelSystem::prepareGainXp(RPGActor *actor, double amount, double xpGrowth) {
    if (!actor)
        return eve::Result<PreparedProgressionGain>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "XP gain requires an actor", "actor"));
    if (!std::isfinite(amount) || amount <= 0.0)
        return eve::Result<PreparedProgressionGain>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "XP gain must be finite and positive", "amount"));
    if (!std::isfinite(xpGrowth) || xpGrowth < 1.0)
        return eve::Result<PreparedProgressionGain>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "XP growth must be finite and at least one", "xpGrowth"));
    const auto &current = *actor->progression();
    if (current.level < 1 || !std::isfinite(current.xp) || current.xp < 0.0 || !std::isfinite(current.xpToNext) ||
        current.xpToNext <= 0.0 || current.xp >= current.xpToNext)
        return eve::Result<PreparedProgressionGain>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "actor progression is not valid for XP gain", "progression"));

    PreparedProgressionGain prepared;
    prepared.actor_            = actor;
    prepared.previousLevel_    = current.level;
    prepared.previousXp_       = current.xp;
    prepared.previousXpToNext_ = current.xpToNext;
    prepared.nextLevel_        = current.level;
    prepared.nextXp_           = current.xp + amount;
    prepared.nextXpToNext_     = current.xpToNext;
    if (!std::isfinite(prepared.nextXp_))
        return eve::Result<PreparedProgressionGain>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "XP gain would overflow progression", "amount"));
    constexpr int maxLevelsPerGain = 10000;
    int           levels           = 0;
    while (prepared.nextXp_ >= prepared.nextXpToNext_) {
        if (++levels > maxLevelsPerGain)
            return eve::Result<PreparedProgressionGain>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                       "XP gain exceeds the maximum levels allowed in one transaction", "amount"));
        prepared.nextXp_ -= prepared.nextXpToNext_;
        ++prepared.nextLevel_;
        prepared.nextXpToNext_ *= xpGrowth;
        if (!std::isfinite(prepared.nextXpToNext_) || prepared.nextXpToNext_ <= 0.0)
            return eve::Result<PreparedProgressionGain>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "XP growth would overflow progression", "xpGrowth"));
    }
    return eve::Result<PreparedProgressionGain>::success(std::move(prepared),
                                                         eve::Status::success(eve::StatusCode::Pending));
}

eve::Result<int> LevelSystem::commitGainXp(PreparedProgressionGain prepared) {
    if (!prepared.actor_)
        return eve::Result<int>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "prepared XP gain is empty", "prepared"));
    auto &current = *prepared.actor_->progression();
    if (current.level != prepared.previousLevel_ || current.xp != prepared.previousXp_ ||
        current.xpToNext != prepared.previousXpToNext_)
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "actor progression changed after XP preparation", "progression"));
    current.level    = prepared.nextLevel_;
    current.xp       = prepared.nextXp_;
    current.xpToNext = prepared.nextXpToNext_;
    const int levels = prepared.nextLevel_ - prepared.previousLevel_;
    for (int offset = 0; offset < levels; ++offset) {
        LevelUpEvent event;
        event.actor         = prepared.actor_;
        event.previousLevel = prepared.previousLevel_ + offset;
        event.newLevel      = event.previousLevel + 1;
        levelUpEvents().push_back(std::move(event));
    }
    return eve::Result<int>::success(levels, eve::Status::success(eve::StatusCode::Applied));
}

void LevelSystem::pollLevelUps(std::vector<LevelUpEvent> &out) {
    out.clear();
    out = std::move(levelUpEvents());
    levelUpEvents().clear();
}

}  // namespace eve::rpg
