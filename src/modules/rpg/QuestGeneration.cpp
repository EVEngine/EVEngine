#include "rpg/QuestGeneration.h"

#include "common/Json.h"
#include "common/Value.h"
#include "rpg/Quest.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <random>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace eve::rpg {
namespace {

using eve::json::Value;

struct EntityFact {
    std::string                                  id;
    std::string                                  domain;
    double                                       urgency = 0.0;
    std::vector<std::string>                     tags;
    std::unordered_map<std::string, double>      numberVars;
    std::unordered_map<std::string, std::string> stringVars;
};

struct Candidate {
    const QuestGenerationArchetype *archetype = nullptr;
    const EntityFact               *entity    = nullptr;
    double                          score     = 0.0;
};

std::unordered_map<std::string, QuestGenerationArchetype> &archetypeTable() {
    static std::unordered_map<std::string, QuestGenerationArchetype> table;
    return table;
}

eve::Result<int> failInt(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<int>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "rpg.quest-generation"));
}

bool validContentId(const std::string &id) {
    if (id.empty() || id.size() > 256) return false;
    return std::none_of(id.begin(), id.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

bool stringArrayIsStrict(Value value) {
    if (!value.isArray()) return false;
    for (size_t i = 0; i < value.size(); ++i)
        if (!value.at(i).isString()) return false;
    return true;
}

bool stringMapIsStrict(Value value) {
    if (!value.isObject()) return false;
    for (const auto &key : value.keys())
        if (!value.get(key.c_str()).isString()) return false;
    return true;
}

std::string normalizeStart(const std::string &policy) { return policy == "auto" ? "auto" : "manual"; }

std::string normalizeComplete(const std::string &policy) { return policy == "claim" ? "claim" : "auto"; }

std::string normalizeObjectivePolicy(const std::string &policy) {
    if (policy == "any" || policy == "ordered") return policy;
    return "all";
}

bool entityHasAllTags(const EntityFact &entity, const std::vector<std::string> &required) {
    for (const auto &tag : required) {
        if (std::find(entity.tags.begin(), entity.tags.end(), tag) == entity.tags.end()) return false;
    }
    return true;
}

std::string substitute(std::string text, const QuestGenerationArchetype &archetype, const EntityFact &entity,
                       const std::string &giverId, int n) {
    auto replaceAll = [&](const std::string &token, const std::string &value) {
        for (;;) {
            const auto pos = text.find(token);
            if (pos == std::string::npos) break;
            text.replace(pos, token.size(), value);
        }
    };
    replaceAll("{archetype}", archetype.id);
    replaceAll("{entity}", entity.id);
    replaceAll("{giver}", giverId);
    replaceAll("{domain}", entity.domain);
    replaceAll("{n}", std::to_string(n));
    return text;
}

std::string resolveSlot(const std::string &slot, const EntityFact &entity, const std::string &giverId,
                        std::string *error) {
    if (slot.empty()) return {};
    if (slot == "entity") return entity.id;
    if (slot == "giver") return giverId;
    if (slot == "domain") return entity.domain;
    constexpr std::string_view kVarPrefix = "var:";
    if (slot.rfind(kVarPrefix.data(), 0) == 0) {
        const std::string key = slot.substr(kVarPrefix.size());
        auto              sit = entity.stringVars.find(key);
        if (sit != entity.stringVars.end()) return sit->second;
        auto nit = entity.numberVars.find(key);
        if (nit != entity.numberVars.end()) {
            std::ostringstream oss;
            oss << nit->second;
            return oss.str();
        }
        if (error) *error = "missing entity var '" + key + "'";
        return {};
    }
    if (error) *error = "unknown slot '" + slot + "'";
    return {};
}

eve::Result<QuestDefinition> instantiate(const QuestGenerationArchetype &archetype, const EntityFact &entity,
                                         const std::string &giverId, int n) {
    QuestDefinition   def;
    const std::string pattern = archetype.idPattern.empty() ? "gen.{archetype}.{entity}.{n}" : archetype.idPattern;
    def.id                    = substitute(pattern, archetype, entity, giverId, n);
    if (!validContentId(def.id))
        return eve::Result<QuestDefinition>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "generated quest id is not a stable content id", "idPattern",
            {{"id", def.id}}, "rpg.quest-generation"));

    def.startPolicy      = archetype.startPolicy;
    def.completePolicy   = archetype.completePolicy;
    def.objectivePolicy  = archetype.objectivePolicy;
    def.timeLimitSeconds = archetype.timeLimitSeconds;
    def.refreshKey       = archetype.refreshKey;
    def.tags             = archetype.tags;
    if (std::find(def.tags.begin(), def.tags.end(), "generated") == def.tags.end()) def.tags.push_back("generated");

    for (const auto &[key, value] : archetype.extra) def.extra[key] = substitute(value, archetype, entity, giverId, n);

    def.extra["generatedArchetype"] = archetype.id;
    def.extra["generatedEntity"]    = entity.id;
    if (!giverId.empty()) def.extra["generatedGiver"] = giverId;
    if (!entity.domain.empty()) def.extra["generatedDomain"] = entity.domain;

    for (size_t i = 0; i < archetype.objectives.size(); ++i) {
        const auto    &tmpl = archetype.objectives[i];
        QuestObjective obj;
        obj.id       = tmpl.id;
        obj.topic    = tmpl.topic;
        obj.optional = tmpl.optional;
        obj.count    = tmpl.count;
        if (!tmpl.countVar.empty()) {
            auto it = entity.numberVars.find(tmpl.countVar);
            if (it == entity.numberVars.end() || !std::isfinite(it->second) || it->second <= 0.0 ||
                it->second > static_cast<double>(INT_MAX) || std::floor(it->second) != it->second)
                return eve::Result<QuestDefinition>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "objective countVar must resolve to a positive integer",
                    "objectives[" + std::to_string(i) + "].countVar", {{"var", tmpl.countVar}},
                    "rpg.quest-generation"));
            obj.count = static_cast<int>(it->second);
        }
        if (obj.count <= 0) obj.count = 1;

        std::string slotError;
        if (!tmpl.targetSlot.empty()) {
            obj.target = resolveSlot(tmpl.targetSlot, entity, giverId, &slotError);
            if (!slotError.empty())
                return eve::Result<QuestDefinition>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, slotError, "objectives[" + std::to_string(i) + "].targetSlot",
                    {}, "rpg.quest-generation"));
        } else {
            obj.target = tmpl.target;
        }
        def.objectives.push_back(std::move(obj));
    }

    for (size_t i = 0; i < archetype.rewards.size(); ++i) {
        const auto &tmpl = archetype.rewards[i];
        RewardSpec  reward;
        reward.type = tmpl.type;
        std::string slotError;
        if (!tmpl.idSlot.empty()) {
            reward.id = resolveSlot(tmpl.idSlot, entity, giverId, &slotError);
            if (!slotError.empty())
                return eve::Result<QuestDefinition>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, slotError,
                                           "rewards[" + std::to_string(i) + "].idSlot", {}, "rpg.quest-generation"));
        } else {
            reward.id = tmpl.id;
        }
        reward.amount = tmpl.amount;
        if (!tmpl.amountVar.empty()) {
            auto it = entity.numberVars.find(tmpl.amountVar);
            if (it == entity.numberVars.end() || !std::isfinite(it->second) || it->second <= 0.0)
                return eve::Result<QuestDefinition>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "reward amountVar must resolve to a finite positive number",
                    "rewards[" + std::to_string(i) + "].amountVar", {{"var", tmpl.amountVar}}, "rpg.quest-generation"));
            reward.amount = it->second;
        }
        if (reward.id.empty() || !(reward.amount > 0.0) || !std::isfinite(reward.amount))
            return eve::Result<QuestDefinition>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "generated reward requires id and positive amount",
                "rewards[" + std::to_string(i) + "]", {}, "rpg.quest-generation"));
        def.rewards.push_back(std::move(reward));
    }

    return eve::Result<QuestDefinition>::success(std::move(def));
}

eve::Value encodeQuest(const QuestDefinition &def) {
    eve::Value::Object root;
    root.emplace("id", eve::Value(def.id));
    root.emplace("startPolicy", eve::Value(def.startPolicy));
    root.emplace("completePolicy", eve::Value(def.completePolicy));
    root.emplace("objectivePolicy", eve::Value(def.objectivePolicy));
    if (def.timeLimitSeconds > 0.0) root.emplace("timeLimitSeconds", eve::Value(def.timeLimitSeconds));
    if (!def.refreshKey.empty()) root.emplace("refreshKey", eve::Value(def.refreshKey));

    eve::Value::Array requiresIds;
    for (const auto &id : def.requiresIds) requiresIds.emplace_back(eve::Value(id));
    root.emplace("requires", eve::Value(std::move(requiresIds)));

    eve::Value::Array objectives;
    for (const auto &obj : def.objectives) {
        eve::Value::Object item;
        item.emplace("id", eve::Value(obj.id));
        item.emplace("topic", eve::Value(obj.topic));
        item.emplace("target", eve::Value(obj.target));
        item.emplace("count", eve::Value(obj.count));
        if (obj.optional) item.emplace("optional", eve::Value(true));
        objectives.emplace_back(std::move(item));
    }
    root.emplace("objectives", eve::Value(std::move(objectives)));

    eve::Value::Array rewards;
    for (const auto &reward : def.rewards) {
        eve::Value::Object item;
        item.emplace("type", eve::Value(reward.type));
        item.emplace("id", eve::Value(reward.id));
        item.emplace("amount", eve::Value(reward.amount));
        rewards.emplace_back(std::move(item));
    }
    root.emplace("rewards", eve::Value(std::move(rewards)));

    eve::Value::Array tags;
    for (const auto &tag : def.tags) tags.emplace_back(eve::Value(tag));
    root.emplace("tags", eve::Value(std::move(tags)));

    eve::Value::Object extra;
    for (const auto &[key, value] : def.extra) extra.emplace(key, eve::Value(value));
    root.emplace("extra", eve::Value(std::move(extra)));
    return eve::Value(std::move(root));
}

eve::Result<QuestGenerationArchetype> parseArchetype(Value object, const std::string &base) {
    if (!object.isObject())
        return eve::Result<QuestGenerationArchetype>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "archetype must be an object", base, {}, "rpg.quest-generation"));

    QuestGenerationArchetype archetype;
    const Value              idValue = object.get("id");
    if (!idValue.isString() || !validContentId(idValue.asString()))
        return eve::Result<QuestGenerationArchetype>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "archetype id must be a stable content id",
                                   base + ".id", {}, "rpg.quest-generation"));
    archetype.id = idValue.asString();

    const Value weight = object.get("weight");
    if (weight) {
        if (!weight.isNumber() || !std::isfinite(weight.asDouble()) || weight.asDouble() <= 0.0)
            return eve::Result<QuestGenerationArchetype>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "weight must be a finite positive number",
                                       base + ".weight", {}, "rpg.quest-generation"));
        archetype.weight = weight.asDouble();
    }

    const Value requiredTags = object.get("requiresEntityTags");
    if (requiredTags && !stringArrayIsStrict(requiredTags))
        return eve::Result<QuestGenerationArchetype>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "requiresEntityTags must be an array of strings",
            base + ".requiresEntityTags", {}, "rpg.quest-generation"));
    archetype.requiresEntityTags = requiredTags ? requiredTags.toStringArray() : std::vector<std::string>{};
    for (size_t i = 0; i < archetype.requiresEntityTags.size(); ++i)
        if (!validContentId(archetype.requiresEntityTags[i]))
            return eve::Result<QuestGenerationArchetype>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "requiresEntityTags entry must be a stable id",
                base + ".requiresEntityTags[" + std::to_string(i) + "]", {}, "rpg.quest-generation"));

    const Value startPolicy = object.get("startPolicy");
    if (startPolicy &&
        (!startPolicy.isString() || (startPolicy.asString() != "manual" && startPolicy.asString() != "auto")))
        return eve::Result<QuestGenerationArchetype>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "startPolicy must be 'manual' or 'auto'",
                                   base + ".startPolicy", {}, "rpg.quest-generation"));
    archetype.startPolicy = startPolicy ? startPolicy.asString() : "manual";

    const Value completePolicy = object.get("completePolicy");
    if (completePolicy &&
        (!completePolicy.isString() || (completePolicy.asString() != "auto" && completePolicy.asString() != "claim")))
        return eve::Result<QuestGenerationArchetype>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "completePolicy must be 'auto' or 'claim'",
                                   base + ".completePolicy", {}, "rpg.quest-generation"));
    archetype.completePolicy = completePolicy ? completePolicy.asString() : "claim";

    const Value objectivePolicy = object.get("objectivePolicy");
    if (objectivePolicy &&
        (!objectivePolicy.isString() || (objectivePolicy.asString() != "all" && objectivePolicy.asString() != "any" &&
                                         objectivePolicy.asString() != "ordered")))
        return eve::Result<QuestGenerationArchetype>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "objectivePolicy must be 'all', 'any', or 'ordered'",
            base + ".objectivePolicy", {}, "rpg.quest-generation"));
    archetype.objectivePolicy = objectivePolicy ? objectivePolicy.asString() : "all";

    const Value timeLimitSeconds = object.get("timeLimitSeconds");
    if (timeLimitSeconds) {
        if (!timeLimitSeconds.isNumber() || !std::isfinite(timeLimitSeconds.asDouble()) ||
            timeLimitSeconds.asDouble() < 0.0)
            return eve::Result<QuestGenerationArchetype>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "timeLimitSeconds must be a finite non-negative number",
                base + ".timeLimitSeconds", {}, "rpg.quest-generation"));
        archetype.timeLimitSeconds = timeLimitSeconds.asDouble();
    }

    const Value refreshKey = object.get("refreshKey");
    if (refreshKey) {
        if (!refreshKey.isString() || (!refreshKey.asString().empty() && !validContentId(refreshKey.asString())))
            return eve::Result<QuestGenerationArchetype>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "refreshKey must be empty or a stable id",
                                       base + ".refreshKey", {}, "rpg.quest-generation"));
        archetype.refreshKey = refreshKey.asString();
    }

    const Value idPattern = object.get("idPattern");
    if (idPattern) {
        if (!idPattern.isString())
            return eve::Result<QuestGenerationArchetype>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "idPattern must be a string",
                                       base + ".idPattern", {}, "rpg.quest-generation"));
        archetype.idPattern = idPattern.asString();
    }

    const Value objectives = object.get("objectives");
    if (!objectives || !objectives.isArray() || objectives.size() == 0)
        return eve::Result<QuestGenerationArchetype>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "archetype requires a non-empty objectives array",
            base + ".objectives", {}, "rpg.quest-generation"));
    std::unordered_set<std::string> seenObjectives;
    for (size_t i = 0; i < objectives.size(); ++i) {
        const Value item = objectives.at(i);
        const auto  path = base + ".objectives[" + std::to_string(i) + "]";
        if (!item.isObject())
            return eve::Result<QuestGenerationArchetype>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "objective template must be an object", path,
                                       {}, "rpg.quest-generation"));
        QuestGenerationObjectiveTemplate tmpl;
        const Value                      objectiveId = item.get("id");
        const Value                      topic       = item.get("topic");
        if (!objectiveId.isString() || !validContentId(objectiveId.asString()) ||
            !seenObjectives.insert(objectiveId.asString()).second)
            return eve::Result<QuestGenerationArchetype>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "objective id must be unique and stable",
                                       path + ".id", {}, "rpg.quest-generation"));
        if (!topic.isString() || !validContentId(topic.asString()))
            return eve::Result<QuestGenerationArchetype>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "objective topic must be a stable id",
                                       path + ".topic", {}, "rpg.quest-generation"));
        tmpl.id            = objectiveId.asString();
        tmpl.topic         = topic.asString();
        const Value target = item.get("target");
        if (target) {
            if (!target.isString() || (!target.asString().empty() && !validContentId(target.asString())))
                return eve::Result<QuestGenerationArchetype>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "objective target must be empty or a stable id",
                    path + ".target", {}, "rpg.quest-generation"));
            tmpl.target = target.asString();
        }
        const Value targetSlot = item.get("targetSlot");
        if (targetSlot) {
            if (!targetSlot.isString())
                return eve::Result<QuestGenerationArchetype>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "targetSlot must be a string",
                                           path + ".targetSlot", {}, "rpg.quest-generation"));
            tmpl.targetSlot = targetSlot.asString();
        }
        const Value count = item.get("count");
        if (count) {
            if (!count.isInt64() || count.asInt64() <= 0 || count.asInt64() > INT_MAX)
                return eve::Result<QuestGenerationArchetype>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "count must be a positive integer",
                                           path + ".count", {}, "rpg.quest-generation"));
            tmpl.count = static_cast<int>(count.asInt64());
        }
        const Value countVar = item.get("countVar");
        if (countVar) {
            if (!countVar.isString() || !validContentId(countVar.asString()))
                return eve::Result<QuestGenerationArchetype>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "countVar must be a stable id",
                                           path + ".countVar", {}, "rpg.quest-generation"));
            tmpl.countVar = countVar.asString();
        }
        const Value optional = item.get("optional");
        if (optional && !optional.isBool())
            return eve::Result<QuestGenerationArchetype>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "optional must be a boolean",
                                       path + ".optional", {}, "rpg.quest-generation"));
        tmpl.optional = optional ? optional.asBool() : false;
        archetype.objectives.push_back(std::move(tmpl));
    }

    const Value rewards = object.get("rewards");
    if (rewards) {
        if (!rewards.isArray())
            return eve::Result<QuestGenerationArchetype>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "rewards must be an array",
                                       base + ".rewards", {}, "rpg.quest-generation"));
        for (size_t i = 0; i < rewards.size(); ++i) {
            const Value item = rewards.at(i);
            const auto  path = base + ".rewards[" + std::to_string(i) + "]";
            if (!item.isObject())
                return eve::Result<QuestGenerationArchetype>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "reward template must be an object", path,
                                           {}, "rpg.quest-generation"));
            QuestGenerationRewardTemplate tmpl;
            const Value                   type = item.get("type");
            if (!type.isString() || !validContentId(type.asString()))
                return eve::Result<QuestGenerationArchetype>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "reward type must be a stable id",
                                           path + ".type", {}, "rpg.quest-generation"));
            tmpl.type          = type.asString();
            const Value id     = item.get("id");
            const Value idSlot = item.get("idSlot");
            if (idSlot) {
                if (!idSlot.isString())
                    return eve::Result<QuestGenerationArchetype>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "idSlot must be a string",
                                               path + ".idSlot", {}, "rpg.quest-generation"));
                tmpl.idSlot = idSlot.asString();
            }
            if (id) {
                if (!id.isString() || !validContentId(id.asString()))
                    return eve::Result<QuestGenerationArchetype>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "reward id must be a stable id",
                                               path + ".id", {}, "rpg.quest-generation"));
                tmpl.id = id.asString();
            } else if (tmpl.idSlot.empty()) {
                return eve::Result<QuestGenerationArchetype>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "reward requires id or idSlot", path,
                                           {}, "rpg.quest-generation"));
            }
            const Value amount = item.get("amount");
            if (amount) {
                if (!amount.isNumber() || !std::isfinite(amount.asDouble()) || amount.asDouble() <= 0.0)
                    return eve::Result<QuestGenerationArchetype>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "amount must be finite and positive", path + ".amount",
                        {}, "rpg.quest-generation"));
                tmpl.amount = amount.asDouble();
            }
            const Value amountVar = item.get("amountVar");
            if (amountVar) {
                if (!amountVar.isString() || !validContentId(amountVar.asString()))
                    return eve::Result<QuestGenerationArchetype>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "amountVar must be a stable id",
                                               path + ".amountVar", {}, "rpg.quest-generation"));
                tmpl.amountVar = amountVar.asString();
            }
            if (!(tmpl.amount > 0.0) && tmpl.amountVar.empty())
                return eve::Result<QuestGenerationArchetype>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "reward requires amount or amountVar",
                                           path, {}, "rpg.quest-generation"));
            archetype.rewards.push_back(std::move(tmpl));
        }
    }

    const Value tags = object.get("tags");
    if (tags && !stringArrayIsStrict(tags))
        return eve::Result<QuestGenerationArchetype>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "tags must be an array of strings",
                                   base + ".tags", {}, "rpg.quest-generation"));
    archetype.tags = tags ? tags.toStringArray() : std::vector<std::string>{};
    for (size_t i = 0; i < archetype.tags.size(); ++i)
        if (!validContentId(archetype.tags[i]))
            return eve::Result<QuestGenerationArchetype>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "tag must be a stable id",
                                       base + ".tags[" + std::to_string(i) + "]", {}, "rpg.quest-generation"));

    const Value extra = object.get("extra");
    if (extra && !stringMapIsStrict(extra))
        return eve::Result<QuestGenerationArchetype>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "extra must be an object of string values",
                                   base + ".extra", {}, "rpg.quest-generation"));
    if (extra) {
        for (const auto &key : extra.keys()) archetype.extra[key] = extra.get(key.c_str()).asString();
    }

    return eve::Result<QuestGenerationArchetype>::success(std::move(archetype));
}

eve::Result<EntityFact> parseEntity(Value object, const std::string &base) {
    if (!object.isObject())
        return eve::Result<EntityFact>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "entity must be an object", base, {}, "rpg.quest-generation"));
    EntityFact  entity;
    const Value id = object.get("id");
    if (!id.isString() || !validContentId(id.asString()))
        return eve::Result<EntityFact>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                       "entity id must be a stable content id",
                                                                       base + ".id", {}, "rpg.quest-generation"));
    entity.id = id.asString();

    const Value domain = object.get("domain");
    if (domain) {
        if (!domain.isString() || (!domain.asString().empty() && !validContentId(domain.asString())))
            return eve::Result<EntityFact>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "domain must be empty or a stable id",
                                       base + ".domain", {}, "rpg.quest-generation"));
        entity.domain = domain.asString();
    }

    const Value urgency = object.get("urgency");
    if (urgency) {
        if (!urgency.isNumber() || !std::isfinite(urgency.asDouble()) || urgency.asDouble() < 0.0)
            return eve::Result<EntityFact>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "urgency must be a finite non-negative number", base + ".urgency",
                {}, "rpg.quest-generation"));
        entity.urgency = urgency.asDouble();
    }

    const Value tags = object.get("tags");
    if (tags && !stringArrayIsStrict(tags))
        return eve::Result<EntityFact>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                       "tags must be an array of strings",
                                                                       base + ".tags", {}, "rpg.quest-generation"));
    entity.tags = tags ? tags.toStringArray() : std::vector<std::string>{};
    for (size_t i = 0; i < entity.tags.size(); ++i)
        if (!validContentId(entity.tags[i]))
            return eve::Result<EntityFact>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "tag must be a stable id",
                                       base + ".tags[" + std::to_string(i) + "]", {}, "rpg.quest-generation"));

    const Value vars = object.get("vars");
    if (vars) {
        if (!vars.isObject())
            return eve::Result<EntityFact>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                           "vars must be an object", base + ".vars", {},
                                                                           "rpg.quest-generation"));
        for (const auto &key : vars.keys()) {
            if (!validContentId(key))
                return eve::Result<EntityFact>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "var key must be a stable id",
                                           base + ".vars", {{"key", key}}, "rpg.quest-generation"));
            const Value value = vars.get(key.c_str());
            if (value.isNumber()) {
                if (!std::isfinite(value.asDouble()))
                    return eve::Result<EntityFact>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "numeric var must be finite",
                                               base + ".vars." + key, {}, "rpg.quest-generation"));
                entity.numberVars[key] = value.asDouble();
            } else if (value.isString()) {
                entity.stringVars[key] = value.asString();
            } else {
                return eve::Result<EntityFact>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "var values must be numbers or strings",
                    base + ".vars." + key, {}, "rpg.quest-generation"));
            }
        }
    }
    return eve::Result<EntityFact>::success(std::move(entity));
}

std::vector<Candidate> buildCandidates(const std::vector<EntityFact> &entities) {
    std::vector<Candidate> candidates;
    for (const auto &entity : entities) {
        for (const auto &id : QuestGenerationCatalogue::ids()) {
            const QuestGenerationArchetype *archetype = QuestGenerationCatalogue::find(id);
            if (!archetype) continue;
            if (!entityHasAllTags(entity, archetype->requiresEntityTags)) continue;
            Candidate candidate;
            candidate.archetype = archetype;
            candidate.entity    = &entity;
            candidate.score     = std::max(0.0, entity.urgency) * std::max(0.0, archetype->weight);
            if (candidate.score <= 0.0) continue;
            candidates.push_back(candidate);
        }
    }
    return candidates;
}

std::vector<Candidate> selectCandidates(std::vector<Candidate> candidates, const std::string &selection, int maxQuests,
                                        std::mt19937 &rng) {
    std::vector<Candidate> chosen;
    if (candidates.empty() || maxQuests <= 0) return chosen;

    auto takeOne = [&](size_t index) {
        chosen.push_back(candidates[index]);
        const std::string entityId = candidates[index].entity->id;
        candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                        [&](const Candidate &c) { return c.entity->id == entityId; }),
                         candidates.end());
    };

    while (!candidates.empty() && static_cast<int>(chosen.size()) < maxQuests) {
        if (selection == "equal") {
            std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
            takeOne(dist(rng));
            continue;
        }
        if (selection == "weighted") {
            double total = 0.0;
            for (const auto &c : candidates) total += c.score;
            if (!(total > 0.0)) break;
            std::uniform_real_distribution<double> dist(0.0, total);
            double                                 pick  = dist(rng);
            size_t                                 index = 0;
            for (; index + 1 < candidates.size(); ++index) {
                pick -= candidates[index].score;
                if (pick <= 0.0) break;
            }
            takeOne(index);
            continue;
        }
        // mostUrgent (default)
        const auto best = std::max_element(candidates.begin(), candidates.end(),
                                           [](const Candidate &a, const Candidate &b) { return a.score < b.score; });
        takeOne(static_cast<size_t>(best - candidates.begin()));
    }
    return chosen;
}

struct GeneratePlan {
    std::vector<QuestDefinition> quests;
    std::vector<std::string>     ids;
};

eve::Result<GeneratePlan> generatePlan(const std::string &requestJson) {
    std::string               parseError;
    const eve::json::Document doc = eve::json::Document::parse(requestJson, &parseError);
    if (!doc.valid())
        return eve::Result<GeneratePlan>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, parseError.empty() ? "invalid JSON" : parseError,
                                   "$", {}, "rpg.quest-generation"));
    const Value root = doc.root();
    if (!root.isObject())
        return eve::Result<GeneratePlan>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError,
                                                                         "quest generate request must be an object",
                                                                         "$", {}, "rpg.quest-generation"));

    const Value schema = root.get("schema");
    if (schema && (!schema.isString() || schema.asString() != "eve.rpg.quest-generate"))
        return eve::Result<GeneratePlan>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                         "schema must be eve.rpg.quest-generate",
                                                                         "$.schema", {}, "rpg.quest-generation"));
    const Value version = root.get("version");
    if (version && (!version.isInt64() || version.asInt64() != 1))
        return eve::Result<GeneratePlan>::failure(eve::Diagnostic::error(eve::DiagnosticCode::UnknownVersion,
                                                                         "unsupported quest-generate version",
                                                                         "$.version", {}, "rpg.quest-generation"));

    if (QuestGenerationCatalogue::count() == 0)
        return eve::Result<GeneratePlan>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "no quest generation archetypes are registered", "archetypes",
            {}, "rpg.quest-generation"));

    std::uint64_t seed      = 0;
    const Value   seedValue = root.get("seed");
    if (seedValue) {
        if (!seedValue.isInt64() || seedValue.asInt64() < 0)
            return eve::Result<GeneratePlan>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                             "seed must be a non-negative integer",
                                                                             "$.seed", {}, "rpg.quest-generation"));
        seed = static_cast<std::uint64_t>(seedValue.asInt64());
    }

    int         maxQuests = 1;
    const Value maxValue  = root.get("maxQuests");
    if (maxValue) {
        if (!maxValue.isInt64() || maxValue.asInt64() <= 0 || maxValue.asInt64() > 64)
            return eve::Result<GeneratePlan>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "maxQuests must be an integer in [1, 64]",
                                       "$.maxQuests", {}, "rpg.quest-generation"));
        maxQuests = static_cast<int>(maxValue.asInt64());
    }

    std::string selection      = "mostUrgent";
    const Value selectionValue = root.get("selection");
    if (selectionValue) {
        if (!selectionValue.isString() ||
            (selectionValue.asString() != "mostUrgent" && selectionValue.asString() != "weighted" &&
             selectionValue.asString() != "equal"))
            return eve::Result<GeneratePlan>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "selection must be mostUrgent, weighted, or equal", "$.selection",
                {}, "rpg.quest-generation"));
        selection = selectionValue.asString();
    }

    std::string giverId;
    const Value giver = root.get("giverId");
    if (giver) {
        if (!giver.isString() || (!giver.asString().empty() && !validContentId(giver.asString())))
            return eve::Result<GeneratePlan>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                             "giverId must be empty or a stable id",
                                                                             "$.giverId", {}, "rpg.quest-generation"));
        giverId = giver.asString();
    }

    bool        excludeExisting = true;
    const Value exclude         = root.get("excludeExisting");
    if (exclude) {
        if (!exclude.isBool())
            return eve::Result<GeneratePlan>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "excludeExisting must be a boolean",
                                       "$.excludeExisting", {}, "rpg.quest-generation"));
        excludeExisting = exclude.asBool();
    }

    const Value entitiesValue = root.get("entities");
    if (!entitiesValue || !entitiesValue.isArray() || entitiesValue.size() == 0)
        return eve::Result<GeneratePlan>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                         "entities must be a non-empty array",
                                                                         "$.entities", {}, "rpg.quest-generation"));

    std::vector<EntityFact>         entities;
    std::unordered_set<std::string> seenEntities;
    entities.reserve(entitiesValue.size());
    for (size_t i = 0; i < entitiesValue.size(); ++i) {
        auto parsed = parseEntity(entitiesValue.at(i), "$.entities[" + std::to_string(i) + "]");
        if (!parsed.ok()) return eve::Result<GeneratePlan>::failure(parsed.status());
        EntityFact entity = std::move(parsed).takeValue();
        if (!seenEntities.insert(entity.id).second)
            return eve::Result<GeneratePlan>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "duplicate entity id in generate request",
                                       "$.entities[" + std::to_string(i) + "].id", {}, "rpg.quest-generation"));
        entities.push_back(std::move(entity));
    }

    auto candidates = buildCandidates(entities);
    if (candidates.empty())
        return eve::Result<GeneratePlan>::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound,
                                                                         "no entity matched any generation archetype",
                                                                         "$.entities", {}, "rpg.quest-generation"));

    std::mt19937 rng(static_cast<unsigned>(seed & 0xffffffffu) ^ static_cast<unsigned>((seed >> 32) & 0xffffffffu));
    std::vector<Candidate> selected = selectCandidates(std::move(candidates), selection, maxQuests, rng);

    GeneratePlan                    plan;
    std::unordered_set<std::string> usedIds;
    int                             n = 1;
    for (const auto &candidate : selected) {
        auto built = instantiate(*candidate.archetype, *candidate.entity, giverId, n);
        if (!built.ok()) return eve::Result<GeneratePlan>::failure(built.status());
        QuestDefinition def = std::move(built).takeValue();
        if (!usedIds.insert(def.id).second)
            return eve::Result<GeneratePlan>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "generated quest id collided within this batch",
                                       "idPattern", {{"id", def.id}}, "rpg.quest-generation"));
        if (excludeExisting && QuestRegistry::contains(def.id)) continue;
        plan.ids.push_back(def.id);
        plan.quests.push_back(std::move(def));
        ++n;
    }
    if (plan.quests.empty())
        return eve::Result<GeneratePlan>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "every generated quest id already exists in the registry", "ids", {},
            "rpg.quest-generation"));
    return eve::Result<GeneratePlan>::success(std::move(plan));
}

}  // namespace

eve::Result<int> QuestGenerationCatalogue::replaceFromJsonStrict(const std::string &json) {
    std::string               parseError;
    const eve::json::Document doc = eve::json::Document::parse(json, &parseError);
    if (!doc.valid())
        return failInt(eve::DiagnosticCode::ParseError, parseError.empty() ? "invalid JSON" : parseError, "$");

    const Value        root = doc.root();
    std::vector<Value> objects;
    if (root.isArray()) {
        if (root.size() == 0)
            return failInt(eve::DiagnosticCode::InvalidArgument, "archetype catalogue must not be empty", "$");
        for (size_t i = 0; i < root.size(); ++i) objects.push_back(root.at(i));
    } else if (root.isObject()) {
        objects.push_back(root);
    } else {
        return failInt(eve::DiagnosticCode::ParseError, "archetype catalogue must be an object or array", "$");
    }

    std::unordered_map<std::string, QuestGenerationArchetype> proposed;
    for (size_t i = 0; i < objects.size(); ++i) {
        auto parsed = parseArchetype(objects[i], "$[" + std::to_string(i) + "]");
        if (!parsed.ok()) return eve::Result<int>::failure(parsed.status());
        QuestGenerationArchetype archetype = std::move(parsed).takeValue();
        if (proposed.count(archetype.id) != 0)
            return failInt(eve::DiagnosticCode::Conflict, "duplicate archetype id", "$[" + std::to_string(i) + "].id");
        proposed.emplace(archetype.id, std::move(archetype));
    }
    archetypeTable() = std::move(proposed);
    return eve::Result<int>::success(static_cast<int>(archetypeTable().size()));
}

void QuestGenerationCatalogue::clear() { archetypeTable().clear(); }

int QuestGenerationCatalogue::count() { return static_cast<int>(archetypeTable().size()); }

const QuestGenerationArchetype *QuestGenerationCatalogue::find(const std::string &id) {
    auto &table = archetypeTable();
    auto  it    = table.find(id);
    return it == table.end() ? nullptr : &it->second;
}

std::vector<std::string> QuestGenerationCatalogue::ids() {
    std::vector<std::string> out;
    for (const auto &[id, unused] : archetypeTable()) {
        (void)unused;
        out.push_back(id);
    }
    std::sort(out.begin(), out.end());
    return out;
}

eve::Result<std::string> QuestGenerator::generateJson(const std::string &requestJson) {
    auto plan = generatePlan(requestJson);
    if (!plan.ok()) return eve::Result<std::string>::failure(plan.status());
    GeneratePlan value = std::move(plan).takeValue();

    eve::Value::Array ids;
    eve::Value::Array quests;
    for (size_t i = 0; i < value.quests.size(); ++i) {
        ids.emplace_back(eve::Value(value.ids[i]));
        quests.emplace_back(encodeQuest(value.quests[i]));
    }
    eve::Value::Object root;
    root.emplace("ids", eve::Value(std::move(ids)));
    root.emplace("quests", eve::Value(std::move(quests)));
    root.emplace("schema", eve::Value("eve.rpg.quest-generate-result"));
    root.emplace("version", eve::Value(1));
    return eve::Value(std::move(root)).toJson();
}

eve::Result<int> QuestGenerator::generateAndRegister(const std::string &requestJson) {
    std::string               parseError;
    const eve::json::Document doc = eve::json::Document::parse(requestJson, &parseError);
    if (!doc.valid())
        return failInt(eve::DiagnosticCode::ParseError, parseError.empty() ? "invalid JSON" : parseError, "$");
    bool replaceGenerated = true;
    if (doc.root().isObject()) {
        const Value flag = doc.root().get("replaceGenerated");
        if (flag) {
            if (!flag.isBool())
                return failInt(eve::DiagnosticCode::InvalidArgument, "replaceGenerated must be a boolean",
                               "$.replaceGenerated");
            replaceGenerated = flag.asBool();
        }
    }

    // Remove previous generated board before planning so excludeExisting only sees hand-authored ids.
    if (replaceGenerated) {
        const auto existing = QuestRegistry::ids();
        for (const auto &id : existing) {
            const QuestDefinition *def = QuestRegistry::find(id);
            if (def && def->hasTag("generated")) QuestRegistry::remove(id);
        }
    }

    auto plan = generatePlan(requestJson);
    if (!plan.ok()) return eve::Result<int>::failure(plan.status());
    GeneratePlan value = std::move(plan).takeValue();

    int registered = 0;
    for (const auto &def : value.quests) {
        const bool existed = QuestRegistry::contains(def.id);
        QuestRegistry::registerQuest(def);
        if (!QuestRegistry::contains(def.id))
            return failInt(eve::DiagnosticCode::Conflict,
                           "generated quest was rejected by the registry (likely a requires cycle)", def.id);
        if (!existed) ++registered;
    }
    return eve::Result<int>::success(registered);
}

}  // namespace eve::rpg
