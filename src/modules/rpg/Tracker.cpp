#include "rpg/Tracker.h"

#include "rpg/Quest.h"
#include "rpg/QuestSystem.h"
#include "common/Value.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace eve::rpg {

Tracker::Tracker() { QuestSystem::syncAuto(this); }

void Tracker::syncAuto() { QuestSystem::syncAuto(this); }

bool Tracker::activate(const std::string &id) { return QuestSystem::activate(this, id); }

bool Tracker::canActivate(const std::string &id) { return QuestSystem::canActivate(this, id); }

std::string Tracker::canActivateReason(const std::string &id) {
    return QuestSystem::canActivateReason(this, id);
}

void Tracker::notify(const std::string &topic, const std::string &target, int amount) {
    QuestSystem::notify(this, topic, target, amount);
}

bool Tracker::claim(const std::string &id) { return QuestSystem::claim(this, id); }

bool Tracker::reset(const std::string &id) { return QuestSystem::reset(this, id); }

bool Tracker::abandon(const std::string &id) { return QuestSystem::abandon(this, id); }

bool Tracker::fail(const std::string &id, const std::string &reason) {
    return QuestSystem::fail(this, id, reason);
}

void Tracker::pollEvents() {
    polled = std::move(pending);
    pending.clear();
}

eve::Result<std::string> Tracker::snapshotJson() const {
    eve::Value::Array encodedEntries;
    encodedEntries.reserve(order.size());
    for (const auto &id : order) {
        const auto found = entries.find(id);
        if (found == entries.end())
            return eve::Result<std::string>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvariantViolation, "quest tracker order references a missing entry", id));
        eve::Value::Array objectives;
        objectives.reserve(found->second.objectives.size());
        for (const auto &objective : found->second.objectives) {
            eve::Value::Object encodedObjective;
            encodedObjective.emplace("current", eve::Value(objective.current));
            encodedObjective.emplace("id", eve::Value(objective.id));
            objectives.emplace_back(std::move(encodedObjective));
        }
        eve::Value::Object encodedEntry;
        encodedEntry.emplace("id", eve::Value(found->second.id));
        encodedEntry.emplace("objectives", eve::Value(std::move(objectives)));
        encodedEntry.emplace("state", eve::Value(found->second.state));
        encodedEntries.emplace_back(std::move(encodedEntry));
    }
    eve::Value::Object root;
    root.emplace("entries", eve::Value(std::move(encodedEntries)));
    root.emplace("schema", eve::Value("eve.rpg.quest-tracker"));
    root.emplace("version", eve::Value(1));
    return eve::Value(std::move(root)).toJson();
}

eve::Result<void> Tracker::restoreSnapshotJson(std::string_view json) {
    auto parsed = eve::Value::fromJson(json);
    if (!parsed.ok()) return eve::Result<void>::failure(parsed.status());
    const eve::Value &root = parsed.value();
    auto fail = [](eve::DiagnosticCode code, std::string message, std::string path) {
        return eve::Result<void>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
    };
    if (!root.isObject())
        return fail(eve::DiagnosticCode::ParseError, "quest tracker root must be an object", "$");
    const eve::Value *schema = root.find("schema");
    if (!schema || !schema->isString() || schema->asString() != "eve.rpg.quest-tracker")
        return fail(eve::DiagnosticCode::InvalidArgument, "snapshot does not belong to RPG Tracker", "$.schema");
    const eve::Value *version = root.find("version");
    if (!version || !version->isInt64() || version->asInt() != 1)
        return fail(eve::DiagnosticCode::UnknownVersion, "unsupported RPG Tracker snapshot version", "$.version");
    const eve::Value *encodedEntries = root.find("entries");
    if (!encodedEntries || !encodedEntries->isArray())
        return fail(eve::DiagnosticCode::ParseError, "quest tracker entries must be an array", "$.entries");

    Tracker candidate;
    if (encodedEntries->arraySize() != candidate.order.size())
        return fail(eve::DiagnosticCode::Conflict,
                    "quest tracker snapshot does not match the current quest registry", "$.entries");
    std::unordered_set<std::string> seenEntries;
    for (std::size_t index = 0; index < candidate.order.size(); ++index) {
        const eve::Value &encoded = encodedEntries->at(index);
        const std::string path = "$.entries[" + std::to_string(index) + "]";
        if (!encoded.isObject())
            return fail(eve::DiagnosticCode::ParseError, "quest tracker entry must be an object", path);
        const eve::Value *id = encoded.find("id");
        const eve::Value *state = encoded.find("state");
        const eve::Value *objectives = encoded.find("objectives");
        if (!id || !id->isString() || !state || !state->isString() || !objectives || !objectives->isArray())
            return fail(eve::DiagnosticCode::ParseError, "quest tracker entry fields have invalid types", path);
        if (!seenEntries.emplace(id->asString()).second)
            return fail(eve::DiagnosticCode::Conflict, "quest tracker entry id is duplicated", path + ".id");
        if (id->asString() != candidate.order[index])
            return fail(eve::DiagnosticCode::Conflict,
                        "quest tracker entry order or id does not match the current registry", path + ".id");
        auto entryIt = candidate.entries.find(id->asString());
        if (entryIt == candidate.entries.end())
            return fail(eve::DiagnosticCode::NotFound, "quest definition is not registered", path + ".id");
        const std::string &stateName = state->asString();
        if (stateName != "locked" && stateName != "inactive" && stateName != "active" &&
            stateName != "ready" && stateName != "completed" && stateName != "failed")
            return fail(eve::DiagnosticCode::InvalidArgument, "quest tracker state is invalid", path + ".state");
        if (objectives->arraySize() != entryIt->second.objectives.size())
            return fail(eve::DiagnosticCode::Conflict,
                        "quest objective list does not match the current definition", path + ".objectives");
        bool allDone = true;
        for (std::size_t objectiveIndex = 0; objectiveIndex < entryIt->second.objectives.size(); ++objectiveIndex) {
            const eve::Value &encodedObjective = objectives->at(objectiveIndex);
            const std::string objectivePath = path + ".objectives[" + std::to_string(objectiveIndex) + "]";
            if (!encodedObjective.isObject())
                return fail(eve::DiagnosticCode::ParseError, "quest objective must be an object", objectivePath);
            const eve::Value *objectiveId = encodedObjective.find("id");
            const eve::Value *current = encodedObjective.find("current");
            auto &runtime = entryIt->second.objectives[objectiveIndex];
            if (!objectiveId || !objectiveId->isString() || !current || !current->isInt64())
                return fail(eve::DiagnosticCode::ParseError, "quest objective fields have invalid types", objectivePath);
            if (objectiveId->asString() != runtime.id)
                return fail(eve::DiagnosticCode::Conflict,
                            "quest objective id or order does not match the current definition", objectivePath + ".id");
            if (current->asInt() < 0 || current->asInt() > runtime.count)
                return fail(eve::DiagnosticCode::InvalidArgument,
                            "quest objective progress is outside its definition bounds", objectivePath + ".current");
            runtime.current = static_cast<int>(current->asInt());
            runtime.done = runtime.current >= runtime.count;
            allDone = allDone && runtime.done;
        }
        if ((stateName == "ready" || stateName == "completed") && !allDone)
            return fail(eve::DiagnosticCode::InvariantViolation,
                        "ready or completed quest must have every objective complete", path + ".state");
        if ((stateName == "locked" || stateName == "inactive") &&
            std::any_of(entryIt->second.objectives.begin(), entryIt->second.objectives.end(),
                        [](const ObjectiveRuntime &objective) { return objective.current != 0; }))
            return fail(eve::DiagnosticCode::InvariantViolation,
                        "locked or inactive quest cannot contain progress", path + ".state");
        if (allDone && stateName != "ready" && stateName != "completed" && stateName != "failed")
            return fail(eve::DiagnosticCode::InvariantViolation,
                        "fully progressed quest must be ready, completed, or failed", path + ".state");
        entryIt->second.state = stateName;
    }
    candidate.pending.clear();
    candidate.polled.clear();
    entries.swap(candidate.entries);
    order.swap(candidate.order);
    pending.swap(candidate.pending);
    polled.swap(candidate.polled);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

int Tracker::getCount() const { return int(entries.size()); }

std::string Tracker::getId(int index) const {
    if (index < 0 || size_t(index) >= order.size()) return {};
    return order[size_t(index)];
}

std::string Tracker::getState(const std::string &id) const {
    auto it = entries.find(id);
    return it == entries.end() ? std::string{} : it->second.state;
}

bool Tracker::hasTag(const std::string &id, const std::string &tag) const {
    const QuestDefinition *def = QuestRegistry::find(id);
    return def && def->hasTag(tag);
}

std::string Tracker::getExtra(const std::string &id, const std::string &key) const {
    const QuestDefinition *def = QuestRegistry::find(id);
    return def ? def->getExtra(key, "") : std::string{};
}

int Tracker::getObjectiveCount(const std::string &id) const {
    auto it = entries.find(id);
    return it == entries.end() ? 0 : int(it->second.objectives.size());
}

std::string Tracker::getObjectiveId(const std::string &id, int index) const {
    auto it = entries.find(id);
    if (it == entries.end()) return {};
    if (index < 0 || size_t(index) >= it->second.objectives.size()) return {};
    return it->second.objectives[size_t(index)].id;
}

int Tracker::getObjectiveCurrent(const std::string &id, int index) const {
    auto it = entries.find(id);
    if (it == entries.end()) return 0;
    if (index < 0 || size_t(index) >= it->second.objectives.size()) return 0;
    return it->second.objectives[size_t(index)].current;
}

int Tracker::getObjectiveCountRequired(const std::string &id, int index) const {
    auto it = entries.find(id);
    if (it == entries.end()) return 0;
    if (index < 0 || size_t(index) >= it->second.objectives.size()) return 0;
    return it->second.objectives[size_t(index)].count;
}

bool Tracker::isObjectiveDone(const std::string &id, int index) const {
    auto it = entries.find(id);
    if (it == entries.end()) return false;
    if (index < 0 || size_t(index) >= it->second.objectives.size()) return false;
    return it->second.objectives[size_t(index)].done;
}

int Tracker::getRewardCount(const std::string &id) const {
    const QuestDefinition *def = QuestRegistry::find(id);
    return def ? int(def->rewards.size()) : 0;
}

std::string Tracker::getRewardType(const std::string &id, int i) const {
    const QuestDefinition *def = QuestRegistry::find(id);
    if (!def || i < 0 || size_t(i) >= def->rewards.size()) return {};
    return def->rewards[size_t(i)].type;
}

std::string Tracker::getRewardId(const std::string &id, int i) const {
    const QuestDefinition *def = QuestRegistry::find(id);
    if (!def || i < 0 || size_t(i) >= def->rewards.size()) return {};
    return def->rewards[size_t(i)].id;
}

double Tracker::getRewardAmount(const std::string &id, int i) const {
    const QuestDefinition *def = QuestRegistry::find(id);
    if (!def || i < 0 || size_t(i) >= def->rewards.size()) return 0.0;
    return def->rewards[size_t(i)].amount;
}

int Tracker::getEventCount() const { return int(polled.size()); }

std::string Tracker::getEventEntryId(int i) const {
    if (i < 0 || size_t(i) >= polled.size()) return {};
    return polled[size_t(i)].entryId;
}

std::string Tracker::getEventObjectiveId(int i) const {
    if (i < 0 || size_t(i) >= polled.size()) return {};
    return polled[size_t(i)].objectiveId;
}

std::string Tracker::getEventAction(int i) const {
    if (i < 0 || size_t(i) >= polled.size()) return {};
    return polled[size_t(i)].action;
}

std::string Tracker::getEventTopic(int i) const {
    if (i < 0 || size_t(i) >= polled.size()) return {};
    return polled[size_t(i)].topic;
}

std::string Tracker::getEventTarget(int i) const {
    if (i < 0 || size_t(i) >= polled.size()) return {};
    return polled[size_t(i)].target;
}

int Tracker::getEventAmount(int i) const {
    if (i < 0 || size_t(i) >= polled.size()) return 0;
    return polled[size_t(i)].amount;
}

std::string Tracker::getEventReason(int i) const {
    if (i < 0 || size_t(i) >= polled.size()) return {};
    return polled[size_t(i)].reason;
}

}  // namespace eve::rpg
