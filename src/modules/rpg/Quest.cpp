#include "rpg/Quest.h"

#include "common/Json.h"

#include <algorithm>
#include <functional>
#include <unordered_set>

namespace eve::rpg {

using eve::json::Value;

namespace {
using Table = std::unordered_map<std::string, QuestDefinition>;
Table& table() {
    static Table value;
    return value;
}

bool valid(const QuestDefinition& def) {
    if (def.id.empty()) return false;
    std::unordered_set<std::string> ids;
    for (const auto& objective : def.objectives) {
        if (objective.id.empty() || objective.topic.empty() || objective.count <= 0 || !ids.insert(objective.id).second)
            return false;
    }
    return true;
}

bool hasCycleFrom(const Table& defs, const std::string& root) {
    std::unordered_set<std::string>         visiting;
    std::unordered_set<std::string>         visited;
    std::function<bool(const std::string&)> visit = [&](const std::string& id) {
        if (visiting.contains(id)) return true;
        if (visited.contains(id)) return false;
        auto it = defs.find(id);
        if (it == defs.end()) return false;
        visiting.insert(id);
        for (const auto& required : it->second.prerequisites)
            if (visit(required)) return true;
        visiting.erase(id);
        visited.insert(id);
        return false;
    };
    return visit(root);
}

QuestDefinition parse(Value value) {
    QuestDefinition def;
    if (!value.isObject()) return def;
    def.id             = value.getString("id");
    def.title          = value.getString("title");
    def.description    = value.getString("description");
    def.startPolicy    = value.getString("startPolicy", "manual");
    def.completePolicy = value.getString("completePolicy", "claim");
    if (def.startPolicy != "auto") def.startPolicy = "manual";
    if (def.completePolicy != "auto") def.completePolicy = "claim";
    def.prerequisites = value.getStringArray("requires");
    def.tags          = value.getStringArray("tags");
    def.extra         = value.getStringMap("extra");
    Value objectives  = value.get("objectives");
    for (size_t i = 0; i < objectives.size(); ++i) {
        Value          item = objectives.at(i);
        QuestObjective objective;
        objective.id     = item.getString("id");
        objective.topic  = item.getString("topic");
        objective.target = item.getString("target");
        objective.count  = item.getInt("count", 1);
        def.objectives.push_back(std::move(objective));
    }
    Value rewards = value.get("rewards");
    for (size_t i = 0; i < rewards.size(); ++i) {
        Value      item = rewards.at(i);
        RewardSpec reward;
        reward.type   = item.getString("type");
        reward.id     = item.getString("id");
        reward.amount = item.getDouble("amount", 1.0);
        if (!reward.type.empty()) def.rewards.push_back(std::move(reward));
    }
    return def;
}
}  // namespace

bool QuestDefinition::hasTag(const std::string& tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

bool QuestRegistry::registerQuest(const QuestDefinition& def) {
    if (!valid(def)) return false;
    Table candidate   = table();
    candidate[def.id] = def;
    if (hasCycleFrom(candidate, def.id)) return false;
    table()[def.id] = def;
    return true;
}

const QuestDefinition* QuestRegistry::find(const std::string& id) {
    auto it = table().find(id);
    return it == table().end() ? nullptr : &it->second;
}

bool QuestRegistry::remove(const std::string& id) { return table().erase(id) > 0; }
void QuestRegistry::clear() { table().clear(); }
int  QuestRegistry::count() { return int(table().size()); }

std::vector<std::string> QuestRegistry::ids() {
    std::vector<std::string> result;
    result.reserve(table().size());
    for (const auto& [id, unused] : table()) result.push_back(id);
    std::sort(result.begin(), result.end());
    return result;
}

int QuestRegistry::loadFromJson(const std::string& json, std::string* error) {
    std::string parseError;
    auto        doc = eve::json::Document::parse(json, &parseError);
    if (!doc.valid()) {
        if (error) *error = parseError.empty() ? "invalid json" : parseError;
        return 0;
    }
    std::vector<QuestDefinition> definitions;
    Value                        root = doc.root();
    if (root.isArray()) {
        for (size_t i = 0; i < root.size(); ++i) definitions.push_back(parse(root.at(i)));
    } else if (root.isObject()) {
        definitions.push_back(parse(root));
    }
    // Validate the whole batch before committing so both sides of A -> B -> A
    // are rejected, rather than leaving the first definition locked forever.
    Table candidate = table();
    for (const auto& definition : definitions)
        if (valid(definition)) candidate[definition.id] = definition;
    int registered = 0;
    for (const auto& definition : definitions) {
        if (!valid(definition) || hasCycleFrom(candidate, definition.id)) continue;
        table()[definition.id] = definition;
        ++registered;
    }
    return registered;
}

}  // namespace eve::rpg
