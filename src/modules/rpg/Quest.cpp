#include "rpg/Quest.h"

#include "common/Json.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>
#include <unordered_set>

namespace eve::rpg {

using eve::json::Value;

bool QuestDefinition::hasTag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::string QuestDefinition::getExtra(const std::string &key, const std::string &fallback) const {
    auto it = extra.find(key);
    return it == extra.end() ? fallback : it->second;
}

std::unordered_map<std::string, QuestDefinition> &questTable() {
    static std::unordered_map<std::string, QuestDefinition> t;
    return t;
}

namespace {

std::string normalizeStart(const std::string &policy) {
    return policy == "auto" ? "auto" : "manual";
}

std::string normalizeComplete(const std::string &policy) {
    return policy == "claim" ? "claim" : "auto";
}

// 环检测：图的边是 id -> requires[i]；未在表中的前置 id 不算环。自依赖是环。
bool idOnCycle(const std::unordered_map<std::string, QuestDefinition> &table, const std::string &start) {
    std::unordered_set<std::string> visiting;
    std::unordered_set<std::string> visited;
    std::function<bool(const std::string &)> dfs = [&](const std::string &id) -> bool {
        if (visiting.count(id)) return true;
        if (visited.count(id)) return false;
        auto it = table.find(id);
        if (it == table.end()) return false;
        visiting.insert(id);
        for (const auto &req : it->second.requiresIds) {
            if (dfs(req)) return true;
        }
        visiting.erase(id);
        visited.insert(id);
        return false;
    };
    return dfs(start);
}

QuestObjective parseObjective(Value o) {
    QuestObjective obj;
    if (!o.isObject()) return obj;
    obj.id = o.getString("id");
    obj.topic = o.getString("topic");
    obj.target = o.getString("target");
    obj.count = o.getInt("count", 1);
    if (obj.count <= 0) obj.count = 1;
    return obj;
}

RewardSpec parseReward(Value o) {
    RewardSpec reward;
    if (!o.isObject()) return reward;
    reward.type = o.getString("type");
    reward.id = o.getString("id");
    reward.amount = o.getDouble("amount", 0.0);
    return reward;
}

// 返回 id 为空的定义表示该 JSON 对象被拒绝（缺 id / 缺目标 id / 目标 id 重复）。
QuestDefinition parseQuestObject(Value o) {
    QuestDefinition def;
    if (!o.isObject()) return def;
    def.id = o.getString("id");
    def.startPolicy = normalizeStart(o.getString("startPolicy", "manual"));
    def.completePolicy = normalizeComplete(o.getString("completePolicy", "auto"));
    def.requiresIds = o.getStringArray("requires");
    def.tags = o.getStringArray("tags");
    def.extra = o.getStringMap("extra");

    const Value objectives = o.get("objectives");
    std::unordered_set<std::string> seen;
    bool invalid = false;
    for (size_t i = 0; i < objectives.size(); ++i) {
        QuestObjective obj = parseObjective(objectives.at(i));
        if (obj.id.empty() || !seen.insert(obj.id).second) {
            invalid = true;
            break;
        }
        def.objectives.push_back(std::move(obj));
    }
    if (invalid) {
        def.id.clear();  // 拒绝整条定义
        return def;
    }

    const Value rewards = o.get("rewards");
    for (size_t i = 0; i < rewards.size(); ++i) {
        RewardSpec reward = parseReward(rewards.at(i));
        def.rewards.push_back(std::move(reward));
    }
    return def;
}

eve::Result<int> strictFailure(std::string message, std::string path) {
    return eve::Result<int>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::ParseError, std::move(message), std::move(path), {}, "rpg.quest-content"));
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

}  // namespace

void QuestRegistry::registerQuest(const QuestDefinition &def) {
    if (def.id.empty()) return;
    auto &t = questTable();
    auto  old = t.find(def.id);
    auto  saved = old != t.end() ? old->second : QuestDefinition{};
    bool  hadOld = old != t.end();

    QuestDefinition normalized = def;
    normalized.startPolicy = normalizeStart(def.startPolicy);
    normalized.completePolicy = normalizeComplete(def.completePolicy);
    t[def.id] = normalized;
    if (idOnCycle(t, def.id)) {
        if (hadOld) {
            t[def.id] = saved;
        } else {
            t.erase(def.id);
        }
    }
}

const QuestDefinition *QuestRegistry::find(const std::string &id) {
    auto &t = questTable();
    auto it = t.find(id);
    return it == t.end() ? nullptr : &it->second;
}

bool QuestRegistry::remove(const std::string &id) { return questTable().erase(id) > 0; }

void QuestRegistry::clear() { questTable().clear(); }

int QuestRegistry::count() { return int(questTable().size()); }

std::vector<std::string> QuestRegistry::ids() {
    std::vector<std::string> out;
    for (const auto &[id, unused] : questTable()) {
        (void)unused;
        out.push_back(id);
    }
    return out;
}

bool QuestRegistry::contains(const std::string &id) { return questTable().count(id) != 0; }

int QuestRegistry::loadFromJson(const std::string &json, std::string *error) {
    std::string err;
    const eve::json::Document doc = eve::json::Document::parse(json, &err);
    if (!doc.valid()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return 0;
    }

    const Value root = doc.root();
    std::vector<Value> objects;
    if (root.isArray()) {
        for (size_t i = 0; i < root.size(); ++i) objects.push_back(root.at(i));
    } else if (root.isObject()) {
        objects.push_back(root);
    }

    auto &t = questTable();
    std::vector<QuestDefinition> parsed;
    std::vector<std::string>     candidateIds;
    for (const auto &o : objects) {
        QuestDefinition def = parseQuestObject(o);
        if (def.id.empty()) continue;
        parsed.push_back(def);
        candidateIds.push_back(def.id);
    }

    // proposed = 当前表 + 本批候选；对本批候选在 proposed 上做环检测。
    auto proposed = t;
    for (const auto &def : parsed) proposed[def.id] = def;
    std::unordered_set<std::string> rejected;
    for (const auto &id : candidateIds) {
        if (idOnCycle(proposed, id)) rejected.insert(id);
    }

    int n = 0;
    for (const auto &def : parsed) {
        if (rejected.count(def.id)) continue;
        t[def.id] = def;
        ++n;
    }
    return n;
}

eve::Result<int> QuestRegistry::replaceFromJsonStrict(const std::string &json) {
    std::string               parseError;
    const eve::json::Document doc = eve::json::Document::parse(json, &parseError);
    if (!doc.valid()) return strictFailure(parseError.empty() ? "invalid JSON" : parseError, "$");

    const Value root = doc.root();
    if (!root.isArray() && !root.isObject())
        return strictFailure("quest catalogue must be an object or array", "$");

    std::vector<Value> objects;
    if (root.isArray()) {
        if (root.size() == 0) return strictFailure("quest catalogue must not be empty", "$");
        for (size_t i = 0; i < root.size(); ++i) objects.push_back(root.at(i));
    } else {
        objects.push_back(root);
    }

    std::unordered_map<std::string, QuestDefinition> proposed;
    for (size_t questIndex = 0; questIndex < objects.size(); ++questIndex) {
        const Value       object = objects[questIndex];
        const std::string base = "$[" + std::to_string(questIndex) + "]";
        if (!object.isObject()) return strictFailure("quest entry must be an object", base);

        const Value idValue = object.get("id");
        if (!idValue.isString() || !validContentId(idValue.asString()))
            return strictFailure("quest id must be a non-empty stable id of at most 256 bytes", base + ".id");

        QuestDefinition definition;
        definition.id = idValue.asString();
        if (proposed.count(definition.id) != 0)
            return strictFailure("duplicate quest id", base + ".id");

        const Value startPolicy = object.get("startPolicy");
        if (startPolicy && (!startPolicy.isString() ||
                            (startPolicy.asString() != "manual" && startPolicy.asString() != "auto")))
            return strictFailure("startPolicy must be 'manual' or 'auto'", base + ".startPolicy");
        definition.startPolicy = startPolicy ? startPolicy.asString() : "manual";

        const Value completePolicy = object.get("completePolicy");
        if (completePolicy && (!completePolicy.isString() ||
                               (completePolicy.asString() != "auto" && completePolicy.asString() != "claim")))
            return strictFailure("completePolicy must be 'auto' or 'claim'", base + ".completePolicy");
        definition.completePolicy = completePolicy ? completePolicy.asString() : "auto";

        const Value prerequisites = object.get("requires");
        if (prerequisites && !stringArrayIsStrict(prerequisites))
            return strictFailure("requires must be an array of quest ids", base + ".requires");
        std::unordered_set<std::string> seenRequirements;
        definition.requiresIds = prerequisites ? prerequisites.toStringArray() : std::vector<std::string>{};
        for (size_t i = 0; i < definition.requiresIds.size(); ++i) {
            const auto &required = definition.requiresIds[i];
            if (!validContentId(required) || !seenRequirements.insert(required).second)
                return strictFailure("requires contains an invalid or duplicate quest id",
                                     base + ".requires[" + std::to_string(i) + "]");
        }

        const Value objectives = object.get("objectives");
        if (objectives && !objectives.isArray())
            return strictFailure("objectives must be an array", base + ".objectives");
        std::unordered_set<std::string> seenObjectives;
        for (size_t i = 0; i < objectives.size(); ++i) {
            const Value item = objectives.at(i);
            const auto  path = base + ".objectives[" + std::to_string(i) + "]";
            if (!item.isObject()) return strictFailure("objective must be an object", path);
            const Value objectiveId = item.get("id");
            const Value topic = item.get("topic");
            if (!objectiveId.isString() || !validContentId(objectiveId.asString()) ||
                !seenObjectives.insert(objectiveId.asString()).second)
                return strictFailure("objective id must be unique and stable", path + ".id");
            if (!topic.isString() || !validContentId(topic.asString()))
                return strictFailure("objective topic must be a stable id", path + ".topic");
            const Value target = item.get("target");
            if (target && (!target.isString() || (!target.asString().empty() && !validContentId(target.asString()))))
                return strictFailure("objective target must be empty or a stable id", path + ".target");
            const Value count = item.get("count");
            if (count && (!count.isInt64() || count.asInt64() <= 0 || count.asInt64() > INT_MAX))
                return strictFailure("objective count must be a positive integer", path + ".count");
            definition.objectives.push_back(
                {objectiveId.asString(), topic.asString(), target ? target.asString() : std::string{},
                 count ? int(count.asInt64()) : 1});
        }

        const Value rewards = object.get("rewards");
        if (rewards && !rewards.isArray()) return strictFailure("rewards must be an array", base + ".rewards");
        for (size_t i = 0; i < rewards.size(); ++i) {
            const Value item = rewards.at(i);
            const auto  path = base + ".rewards[" + std::to_string(i) + "]";
            if (!item.isObject()) return strictFailure("reward must be an object", path);
            const Value type = item.get("type");
            const Value id = item.get("id");
            const Value amount = item.get("amount");
            if (!type.isString() || !validContentId(type.asString()))
                return strictFailure("reward type must be a stable id", path + ".type");
            if (!id.isString() || !validContentId(id.asString()))
                return strictFailure("reward id must be a stable id", path + ".id");
            if (!amount.isNumber() || !std::isfinite(amount.asDouble()) || amount.asDouble() <= 0.0)
                return strictFailure("reward amount must be finite and positive", path + ".amount");
            definition.rewards.push_back({type.asString(), id.asString(), amount.asDouble()});
        }

        const Value tags = object.get("tags");
        if (tags && !stringArrayIsStrict(tags))
            return strictFailure("tags must be an array of strings", base + ".tags");
        definition.tags = tags ? tags.toStringArray() : std::vector<std::string>{};
        for (size_t i = 0; i < definition.tags.size(); ++i)
            if (!validContentId(definition.tags[i]))
                return strictFailure("tag must be a stable id", base + ".tags[" + std::to_string(i) + "]");

        const Value extra = object.get("extra");
        if (extra && !stringMapIsStrict(extra))
            return strictFailure("extra must be an object containing only string values", base + ".extra");
        if (extra) {
            for (const auto &key : extra.keys()) definition.extra[key] = extra.get(key.c_str()).asString();
        }
        proposed.emplace(definition.id, std::move(definition));
    }

    for (const auto &[id, definition] : proposed) {
        for (const auto &required : definition.requiresIds)
            if (proposed.count(required) == 0)
                return strictFailure("required quest is missing from replacement catalogue", id + ".requires");
        if (idOnCycle(proposed, id)) return strictFailure("quest prerequisite cycle detected", id + ".requires");
    }

    questTable() = std::move(proposed);
    return eve::Result<int>::success(int(questTable().size()));
}

}  // namespace eve::rpg
