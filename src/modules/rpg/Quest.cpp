#include "rpg/Quest.h"

#include "common/Json.h"

#include <algorithm>
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

}  // namespace eve::rpg