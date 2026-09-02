#include "rpg/EncounterCatalogue.h"

#include "common/Json.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"

#include <climits>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace eve::rpg {
namespace {

std::unordered_map<std::string, EncounterDefinition> &definitions() {
    static std::unordered_map<std::string, EncounterDefinition> value;
    return value;
}

bool validId(const std::string &value, bool allowEmpty = false) {
    if (value.empty()) return allowEmpty;
    if (value.size() > 256) return false;
    for (unsigned char ch : value)
        if (ch < 0x20 || ch == 0x7f) return false;
    return true;
}

eve::Result<int> failure(std::string message, std::string path) {
    return eve::Result<int>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path), {},
        "rpg.encounter-catalogue"));
}

const std::unordered_set<std::string> &knownFields() {
    static const std::unordered_set<std::string> fields = {
        "id", "displayName", "members", "xpReward", "xpGrowth", "goldReward",
        "requiredQuestId", "notifyTopic", "notifyTarget",
        "notifyAmount", "defeatCounterId", "defeatCounterAmount", "levelPointAttributeId",
        "pointsPerLevel"};
    return fields;
}

const std::unordered_set<std::string> &knownMemberFields() {
    static const std::unordered_set<std::string> fields = {
        "id", "displayName", "skillId", "attack", "defense", "maxHp", "speed"};
    return fields;
}

bool finiteNumber(const eve::json::Value &value, double minimum, bool strictMinimum = false) {
    if (!value.isNumber() || !std::isfinite(value.asDouble())) return false;
    return strictMinimum ? value.asDouble() > minimum : value.asDouble() >= minimum;
}

bool validPositiveInt(const eve::json::Value &value, bool allowZero = false) {
    return value.isInt64() && value.asInt64() <= INT_MAX &&
           (allowZero ? value.asInt64() >= 0 : value.asInt64() > 0);
}

}  // namespace

eve::Result<int> EncounterCatalogue::replaceFromJsonStrict(const std::string &json) {
    std::string parseError;
    const auto document = eve::json::Document::parse(json, &parseError);
    if (!document.valid()) return failure(parseError.empty() ? "invalid JSON" : parseError, "$");
    const auto root = document.root();
    if (!root.isArray() || root.size() == 0)
        return failure("encounter catalogue must be a non-empty array", "$");

    std::unordered_map<std::string, EncounterDefinition> proposed;
    for (std::size_t index = 0; index < root.size(); ++index) {
        const auto object = root.at(index);
        const std::string path = "$[" + std::to_string(index) + "]";
        if (!object.isObject()) return failure("encounter definition must be an object", path);
        for (const auto &key : object.keys())
            if (knownFields().count(key) == 0)
                return failure("encounter definition contains an unknown field", path + "." + key);
        const auto id = object.get("id");
        const auto displayName = object.get("displayName");
        if (!id.isString() || !validId(id.asString()))
            return failure("encounter id must be a stable non-empty id", path + ".id");
        if (proposed.count(id.asString()) != 0) return failure("duplicate encounter id", path + ".id");
        if (!displayName.isString() || displayName.asString().empty() || displayName.asString().size() > 512)
            return failure("displayName must be a non-empty string of at most 512 bytes",
                           path + ".displayName");
        const auto members = object.get("members");
        if (!members.isArray() || members.size() == 0 || members.size() > 32)
            return failure("members must be a non-empty array with at most 32 entries",
                           path + ".members");
        const auto xpReward = object.get("xpReward");
        const auto xpGrowth = object.get("xpGrowth");
        const auto goldReward = object.get("goldReward");
        if (!finiteNumber(xpReward, 0.0, true) || !finiteNumber(xpGrowth, 1.0) ||
            !finiteNumber(goldReward, 0.0))
            return failure("rewards must be finite and within their valid ranges", path);
        const auto requiredQuestId = object.get("requiredQuestId");
        const auto notifyTopic = object.get("notifyTopic");
        const auto notifyTarget = object.get("notifyTarget");
        const auto defeatCounterId = object.get("defeatCounterId");
        const auto levelPointAttributeId = object.get("levelPointAttributeId");
        if (!requiredQuestId.isString() || !validId(requiredQuestId.asString()) ||
            !QuestRegistry::find(requiredQuestId.asString()))
            return failure("requiredQuestId must reference a registered quest", path + ".requiredQuestId");
        if (!notifyTopic.isString() || !validId(notifyTopic.asString()) ||
            !notifyTarget.isString() || !validId(notifyTarget.asString()) ||
            !defeatCounterId.isString() || !validId(defeatCounterId.asString()) ||
            !levelPointAttributeId.isString() || !validId(levelPointAttributeId.asString()))
            return failure("notification and counter ids must be stable non-empty ids", path);
        const auto notifyAmount = object.get("notifyAmount");
        const auto defeatCounterAmount = object.get("defeatCounterAmount");
        const auto pointsPerLevel = object.get("pointsPerLevel");
        if (!validPositiveInt(notifyAmount) || !validPositiveInt(defeatCounterAmount) ||
            !validPositiveInt(pointsPerLevel, true))
            return failure("notification/counter amounts must be positive and pointsPerLevel non-negative", path);

        EncounterDefinition definition;
        definition.id = id.asString();
        definition.displayName = displayName.asString();
        std::unordered_set<std::string> memberIds;
        for (std::size_t memberIndex = 0; memberIndex < members.size(); ++memberIndex) {
            const auto member = members.at(memberIndex);
            const std::string memberPath = path + ".members[" + std::to_string(memberIndex) + "]";
            if (!member.isObject()) return failure("encounter member must be an object", memberPath);
            for (const auto &key : member.keys())
                if (knownMemberFields().count(key) == 0)
                    return failure("encounter member contains an unknown field", memberPath + "." + key);
            const auto memberId = member.get("id");
            const auto memberName = member.get("displayName");
            const auto memberSkill = member.get("skillId");
            const auto attack = member.get("attack");
            const auto defense = member.get("defense");
            const auto maxHp = member.get("maxHp");
            const auto speed = member.get("speed");
            if (!memberId.isString() || !validId(memberId.asString()) ||
                !memberIds.insert(memberId.asString()).second)
                return failure("member id must be stable and unique within the encounter",
                               memberPath + ".id");
            if (!memberName.isString() || memberName.asString().empty() ||
                memberName.asString().size() > 512)
                return failure("member displayName must be a non-empty string of at most 512 bytes",
                               memberPath + ".displayName");
            if (!memberSkill.isString() || !validId(memberSkill.asString()) ||
                !SkillRegistry::find(memberSkill.asString()))
                return failure("member skillId must reference a registered skill",
                               memberPath + ".skillId");
            if (!finiteNumber(attack, 0.0) || !finiteNumber(defense, 0.0) ||
                !finiteNumber(maxHp, 0.0, true) || !finiteNumber(speed, 0.0))
                return failure("member combat stats must be finite and within their valid ranges",
                               memberPath);
            EncounterMemberDefinition parsedMember;
            parsedMember.id = memberId.asString();
            parsedMember.displayName = memberName.asString();
            parsedMember.skillId = memberSkill.asString();
            parsedMember.attack = attack.asDouble();
            parsedMember.defense = defense.asDouble();
            parsedMember.maxHp = maxHp.asDouble();
            parsedMember.speed = speed.asDouble();
            definition.members.push_back(std::move(parsedMember));
        }
        definition.xpReward = xpReward.asDouble();
        definition.xpGrowth = xpGrowth.asDouble();
        definition.goldReward = goldReward.asDouble();
        definition.requiredQuestId = requiredQuestId.asString();
        definition.notifyTopic = notifyTopic.asString();
        definition.notifyTarget = notifyTarget.asString();
        definition.notifyAmount = static_cast<int>(notifyAmount.asInt64());
        definition.defeatCounterId = defeatCounterId.asString();
        definition.defeatCounterAmount = static_cast<int>(defeatCounterAmount.asInt64());
        definition.levelPointAttributeId = levelPointAttributeId.asString();
        definition.pointsPerLevel = static_cast<int>(pointsPerLevel.asInt64());
        proposed.emplace(definition.id, std::move(definition));
    }
    definitions() = std::move(proposed);
    return eve::Result<int>::success(static_cast<int>(definitions().size()));
}

void EncounterCatalogue::clear() { definitions().clear(); }
int EncounterCatalogue::count() { return static_cast<int>(definitions().size()); }

const EncounterDefinition *EncounterCatalogue::find(const std::string &id) {
    const auto found = definitions().find(id);
    return found == definitions().end() ? nullptr : &found->second;
}

int EncounterCatalogue::memberCount(const std::string &id) {
    const auto *definition = find(id);
    return definition ? static_cast<int>(definition->members.size()) : 0;
}

RPGActor *EncounterCatalogue::createActor(const std::string &id) {
    return createMemberActor(id, 0);
}

RPGActor *EncounterCatalogue::createMemberActor(const std::string &id, int memberIndex) {
    const auto *definition = find(id);
    if (!definition || memberIndex < 0 ||
        static_cast<std::size_t>(memberIndex) >= definition->members.size())
        return nullptr;
    const auto &member = definition->members[static_cast<std::size_t>(memberIndex)];
    RPGActor *actor = RPGActor::createActor();
    if (!actor) return nullptr;
    actor->setBaseAttribute("attack", member.attack);
    actor->setBaseAttribute("defense", member.defense);
    actor->setBaseAttribute("hp", member.maxHp);
    actor->setBaseAttribute("speed", member.speed);
    actor->setCurrent("hp", member.maxHp);
    actor->learnSkill(member.skillId);
    return actor;
}

}  // namespace eve::rpg
