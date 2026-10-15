#include "rpg/Tracker.h"

#include "rpg/Quest.h"
#include "rpg/QuestSystem.h"
#include "rpg_tracker.pb.h"

#include <google/protobuf/util/json_util.h>

#include <algorithm>
#include <limits>
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

namespace {
constexpr char kTrackerSchema[] = "eve.rpg.quest-tracker";

eve::Result<void> captureTrackerMessage(const Tracker &tracker, proto::TrackerSave &save) {
    save.set_schema(kTrackerSchema);
    save.set_version(1);
    for (const auto &id : tracker.order) {
        const auto found = tracker.entries.find(id);
        if (found == tracker.entries.end())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvariantViolation, "quest tracker order references a missing entry", id));
        auto *entry = save.add_entries();
        entry->set_id(found->second.id);
        entry->set_state(found->second.state);
        for (const auto &objective : found->second.objectives) {
            auto *encoded = entry->add_objectives();
            encoded->set_id(objective.id);
            encoded->set_current(objective.current);
        }
    }
    return eve::Result<void>::success();
}

eve::Result<void> restoreTrackerMessage(Tracker &tracker, const proto::TrackerSave &save) {
    auto fail = [](eve::DiagnosticCode code, std::string message, std::string path) {
        return eve::Result<void>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
    };
    if (!save.has_schema() || save.schema() != kTrackerSchema)
        return fail(eve::DiagnosticCode::InvalidArgument, "snapshot does not belong to RPG Tracker", "$.schema");
    if (!save.has_version() || save.version() != 1)
        return fail(eve::DiagnosticCode::UnknownVersion, "unsupported RPG Tracker snapshot version", "$.version");

    Tracker candidate;
    if (static_cast<std::size_t>(save.entries_size()) != candidate.order.size())
        return fail(eve::DiagnosticCode::Conflict,
                    "quest tracker snapshot does not match the current quest registry", "$.entries");
    std::unordered_set<std::string> seenEntries;
    for (int index = 0; index < save.entries_size(); ++index) {
        const auto       &encoded = save.entries(index);
        const std::string path = "$.entries[" + std::to_string(index) + "]";
        if (!encoded.has_id() || !encoded.has_state())
            return fail(eve::DiagnosticCode::ParseError, "quest tracker entry fields are missing", path);
        if (!seenEntries.emplace(encoded.id()).second)
            return fail(eve::DiagnosticCode::Conflict, "quest tracker entry id is duplicated", path + ".id");
        if (encoded.id() != candidate.order[static_cast<std::size_t>(index)])
            return fail(eve::DiagnosticCode::Conflict,
                        "quest tracker entry order or id does not match the current registry", path + ".id");
        auto entryIt = candidate.entries.find(encoded.id());
        if (entryIt == candidate.entries.end())
            return fail(eve::DiagnosticCode::NotFound, "quest definition is not registered", path + ".id");
        const std::string &stateName = encoded.state();
        if (stateName != "locked" && stateName != "inactive" && stateName != "active" &&
            stateName != "ready" && stateName != "completed" && stateName != "failed")
            return fail(eve::DiagnosticCode::InvalidArgument, "quest tracker state is invalid", path + ".state");
        if (static_cast<std::size_t>(encoded.objectives_size()) != entryIt->second.objectives.size())
            return fail(eve::DiagnosticCode::Conflict,
                        "quest objective list does not match the current definition", path + ".objectives");
        bool allDone = true;
        for (int objectiveIndex = 0; objectiveIndex < encoded.objectives_size(); ++objectiveIndex) {
            const auto       &encodedObjective = encoded.objectives(objectiveIndex);
            const std::string objectivePath = path + ".objectives[" + std::to_string(objectiveIndex) + "]";
            auto             &runtime          = entryIt->second.objectives[static_cast<std::size_t>(objectiveIndex)];
            if (!encodedObjective.has_id() || !encodedObjective.has_current())
                return fail(eve::DiagnosticCode::ParseError, "quest objective fields are missing", objectivePath);
            if (encodedObjective.id() != runtime.id)
                return fail(eve::DiagnosticCode::Conflict,
                            "quest objective id or order does not match the current definition", objectivePath + ".id");
            if (encodedObjective.current() < 0 || encodedObjective.current() > runtime.count)
                return fail(eve::DiagnosticCode::InvalidArgument,
                            "quest objective progress is outside its definition bounds", objectivePath + ".current");
            runtime.current = encodedObjective.current();
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
    tracker.entries.swap(candidate.entries);
    tracker.order.swap(candidate.order);
    tracker.pending.swap(candidate.pending);
    tracker.polled.swap(candidate.polled);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}
}  // namespace

eve::Result<std::string> Tracker::snapshotBinary() const {
    proto::TrackerSave save;
    auto               captured = captureTrackerMessage(*this, save);
    if (!captured.ok()) return eve::Result<std::string>::failure(captured.status());
    std::string bytes;
    if (!save.SerializeToString(&bytes))
        return eve::Result<std::string>::failure(eve::Diagnostic::error(eve::DiagnosticCode::SerializationError,
                                                                        "quest tracker Protobuf serialization failed"));
    return eve::Result<std::string>::success(std::move(bytes));
}

eve::Result<std::string> Tracker::snapshotJson() const {
    proto::TrackerSave save;
    auto               captured = captureTrackerMessage(*this, save);
    if (!captured.ok()) return eve::Result<std::string>::failure(captured.status());
    std::string json;
    const auto  status = google::protobuf::util::MessageToJsonString(save, &json);
    if (!status.ok())
        return eve::Result<std::string>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::SerializationError, "quest tracker Protobuf JSON conversion failed"));
    return eve::Result<std::string>::success(std::move(json));
}

eve::Result<void> Tracker::restoreSnapshotBinary(std::string_view bytes) {
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "quest tracker snapshot is too large"));
    proto::TrackerSave save;
    if (!save.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "invalid quest tracker Protobuf bytes"));
    return restoreTrackerMessage(*this, save);
}

eve::Result<void> Tracker::restoreSnapshotJson(std::string_view json) {
    proto::TrackerSave                       save;
    google::protobuf::util::JsonParseOptions options;
    options.ignore_unknown_fields = true;
    const auto status             = google::protobuf::util::JsonStringToMessage(std::string(json), &save, options);
    if (!status.ok())
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "invalid quest tracker Protobuf JSON"));
    return restoreTrackerMessage(*this, save);
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
