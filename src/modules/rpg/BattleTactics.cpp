#include "rpg/BattleTactics.h"

#include "common/Json.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"

#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace eve::rpg {
namespace {

std::unordered_map<std::string, BattleTacticsDefinition> &definitions() {
    static std::unordered_map<std::string, BattleTacticsDefinition> value;
    return value;
}

template <typename T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "rpg.battle-tactics"));
}

bool validId(const std::string &value, bool allowEmpty = false) {
    if (value.empty()) return allowEmpty;
    if (value.size() > 256) return false;
    for (unsigned char ch : value)
        if (ch < 0x20 || ch == 0x7f) return false;
    return true;
}

bool parsePolicy(const std::string &value, BattleTargetPolicy &policy) {
    if (value == "auto") policy = BattleTargetPolicy::Auto;
    else if (value == "self") policy = BattleTargetPolicy::Self;
    else if (value == "lowestHealthAlly") policy = BattleTargetPolicy::LowestHealthAlly;
    else if (value == "lowestHealthEnemy") policy = BattleTargetPolicy::LowestHealthEnemy;
    else return false;
    return true;
}

bool policyMatchesTargetType(BattleTargetPolicy policy, const std::string &targetType) {
    if (policy == BattleTargetPolicy::Auto) return true;
    if (targetType == "self") return policy == BattleTargetPolicy::Self;
    if (targetType == "allySingle") return policy == BattleTargetPolicy::LowestHealthAlly;
    return policy == BattleTargetPolicy::LowestHealthEnemy;
}

}  // namespace

eve::Result<int> BattleTacticsCatalogue::replaceFromJsonStrict(const std::string &json) {
    std::string parseError;
    const auto document = eve::json::Document::parse(json, &parseError);
    if (!document.valid())
        return failure<int>(eve::DiagnosticCode::ParseError,
                            parseError.empty() ? "invalid JSON" : parseError, "$");
    const auto root = document.root();
    if (!root.isArray() || root.size() == 0)
        return failure<int>(eve::DiagnosticCode::InvalidArgument,
                            "battle tactics catalogue must be a non-empty array", "$");
    const std::unordered_set<std::string> definitionFields = {"id", "rules"};
    const std::unordered_set<std::string> ruleFields = {
        "skillId", "targetPolicy", "conditionResource", "belowRatio"};
    std::unordered_map<std::string, BattleTacticsDefinition> proposed;
    for (std::size_t index = 0; index < root.size(); ++index) {
        const auto object = root.at(index);
        const std::string path = "$[" + std::to_string(index) + "]";
        if (!object.isObject())
            return failure<int>(eve::DiagnosticCode::InvalidArgument,
                                "battle tactics definition must be an object", path);
        for (const auto &key : object.keys())
            if (!definitionFields.contains(key))
                return failure<int>(eve::DiagnosticCode::InvalidArgument,
                                    "battle tactics definition contains an unknown field", path + "." + key);
        const auto id = object.get("id");
        const auto rules = object.get("rules");
        if (!id.isString() || !validId(id.asString()))
            return failure<int>(eve::DiagnosticCode::InvalidArgument,
                                "battle tactics id must be a stable non-empty id", path + ".id");
        if (proposed.contains(id.asString()))
            return failure<int>(eve::DiagnosticCode::AlreadyExists,
                                "duplicate battle tactics id", path + ".id");
        if (!rules.isArray() || rules.size() == 0 || rules.size() > 16)
            return failure<int>(eve::DiagnosticCode::InvalidArgument,
                                "rules must contain between 1 and 16 entries", path + ".rules");
        BattleTacticsDefinition definition;
        definition.id = id.asString();
        for (std::size_t ruleIndex = 0; ruleIndex < rules.size(); ++ruleIndex) {
            const auto rule = rules.at(ruleIndex);
            const std::string rulePath = path + ".rules[" + std::to_string(ruleIndex) + "]";
            if (!rule.isObject())
                return failure<int>(eve::DiagnosticCode::InvalidArgument,
                                    "battle tactic rule must be an object", rulePath);
            for (const auto &key : rule.keys())
                if (!ruleFields.contains(key))
                    return failure<int>(eve::DiagnosticCode::InvalidArgument,
                                        "battle tactic rule contains an unknown field", rulePath + "." + key);
            const auto skillId = rule.get("skillId");
            const auto targetPolicy = rule.get("targetPolicy");
            const auto conditionResource = rule.get("conditionResource");
            const auto belowRatio = rule.get("belowRatio");
            if (!skillId.isString() || !validId(skillId.asString(), true) ||
                (!skillId.asString().empty() && !SkillRegistry::find(skillId.asString())))
                return failure<int>(eve::DiagnosticCode::NotFound,
                                    "skillId must be empty or reference a registered skill",
                                    rulePath + ".skillId");
            BattleTargetPolicy parsedPolicy = BattleTargetPolicy::Auto;
            if (!targetPolicy.isString() || !parsePolicy(targetPolicy.asString(), parsedPolicy))
                return failure<int>(eve::DiagnosticCode::InvalidArgument,
                                    "targetPolicy is invalid", rulePath + ".targetPolicy");
            const std::string targetType = skillId.asString().empty()
                                               ? "enemySingle"
                                               : SkillRegistry::find(skillId.asString())->targetType;
            if (!policyMatchesTargetType(parsedPolicy, targetType))
                return failure<int>(eve::DiagnosticCode::InvalidArgument,
                                    "targetPolicy does not match the skill target type",
                                    rulePath + ".targetPolicy");
            if (!conditionResource.isString() || !validId(conditionResource.asString(), true) ||
                !belowRatio.isNumber() || !std::isfinite(belowRatio.asDouble()) ||
                belowRatio.asDouble() < 0.0 || belowRatio.asDouble() > 1.0)
                return failure<int>(eve::DiagnosticCode::InvalidArgument,
                                    "conditionResource and belowRatio are invalid", rulePath);
            if (parsedPolicy == BattleTargetPolicy::Auto && !conditionResource.asString().empty())
                return failure<int>(eve::DiagnosticCode::InvalidArgument,
                                    "conditional rules require an explicit target policy", rulePath);
            BattleTacticRule parsed;
            parsed.skillId = skillId.asString();
            parsed.targetPolicy = parsedPolicy;
            parsed.conditionResource = conditionResource.asString();
            parsed.belowRatio = belowRatio.asDouble();
            definition.rules.push_back(std::move(parsed));
        }
        if (!definition.rules.back().conditionResource.empty())
            return failure<int>(eve::DiagnosticCode::InvalidArgument,
                                "the final battle tactic rule must be unconditional", path + ".rules");
        proposed.emplace(definition.id, std::move(definition));
    }
    definitions() = std::move(proposed);
    return eve::Result<int>::success(static_cast<int>(definitions().size()));
}

void BattleTacticsCatalogue::clear() { definitions().clear(); }
int BattleTacticsCatalogue::count() { return static_cast<int>(definitions().size()); }

eve::Result<std::string> BattleTacticsCatalogue::queueAction(Battle *battle, RPGActor *actor,
                                                              const std::string &tacticsId) {
    if (!battle || !actor)
        return failure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                    "battle and actor must not be null", "battle");
    const auto found = definitions().find(tacticsId);
    if (found == definitions().end())
        return failure<std::string>(eve::DiagnosticCode::NotFound,
                                    "battle tactics id is not registered", "tacticsId");
    for (const auto &rule : found->second.rules) {
        if (!rule.conditionResource.empty()) {
            const int side = battle->sideOf(actor);
            RPGActor *target = rule.targetPolicy == BattleTargetPolicy::Self
                                   ? actor
                                   : battle->lowestHealthTarget(
                                         side, rule.targetPolicy == BattleTargetPolicy::LowestHealthAlly);
            if (!target) continue;
            const double maximum = target->getMax(rule.conditionResource);
            if (maximum <= 0.0 || target->getCurrent(rule.conditionResource) / maximum >= rule.belowRatio)
                continue;
        }
        auto queued = battle->setActionByPolicyChecked(actor, rule.skillId, rule.targetPolicy);
        if (!queued.ok()) return eve::Result<std::string>::failure(queued.status());
        return eve::Result<std::string>::success(rule.skillId);
    }
    return failure<std::string>(eve::DiagnosticCode::PreconditionViolation,
                                "battle tactics has no applicable default rule", "rules");
}

}  // namespace eve::rpg
