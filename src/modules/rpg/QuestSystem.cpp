#include "rpg/QuestSystem.h"

#include "rpg/Quest.h"
#include "rpg/Tracker.h"

#include <algorithm>

namespace eve::rpg {

void QuestSystem::emit(Tracker& tracker, const std::string& id, const std::string& action, const std::string& objective,
                       const std::string& topic, const std::string& target, int amount, const std::string& reason) {
    tracker.pending_.push_back({id, objective, action, topic, target, reason, amount});
}

bool QuestSystem::requiresMet(const Tracker& tracker, const QuestDefinition& definition) {
    for (const auto& required : definition.prerequisites) {
        auto it = tracker.entries_.find(required);
        if (it == tracker.entries_.end() || it->second.state != "completed") return false;
    }
    return true;
}

void QuestSystem::createRuntime(Tracker& tracker, const QuestDefinition& definition) {
    QuestRuntime runtime;
    runtime.id = definition.id;
    runtime.state =
        requiresMet(tracker, definition) ? (definition.startPolicy == "auto" ? "active" : "inactive") : "locked";
    for (const auto& objective : definition.objectives)
        runtime.objectives.push_back({objective.id, 0, objective.count, false});
    tracker.entries_[definition.id] = std::move(runtime);
    tracker.order_.push_back(definition.id);
    auto& created = tracker.entries_.at(definition.id);
    if (created.state == "active") {
        emit(tracker, definition.id, "activate");
        finishIfReady(tracker, created, definition);
    }
}

void QuestSystem::syncAuto(Tracker& tracker) {
    for (const auto& id : QuestRegistry::ids()) {
        if (!tracker.entries_.contains(id)) createRuntime(tracker, *QuestRegistry::find(id));
    }
    unlock(tracker);
}

std::string QuestSystem::canActivateReason(const Tracker& tracker, const std::string& id) {
    auto it = tracker.entries_.find(id);
    if (it == tracker.entries_.end()) return "unknown";
    const auto& state = it->second.state;
    if (state == "inactive") return {};
    if (state == "locked") return "locked";
    if (state == "active") return "alreadyActive";
    if (state == "completed") return "alreadyCompleted";
    if (state == "ready") return "ready";
    return "failed";
}

bool QuestSystem::activate(Tracker& tracker, const std::string& id) {
    if (!canActivateReason(tracker, id).empty()) return false;
    auto* definition = QuestRegistry::find(id);
    if (!definition) return false;
    auto& runtime = tracker.entries_.at(id);
    runtime.state = "active";
    emit(tracker, id, "activate");
    finishIfReady(tracker, runtime, *definition);
    return true;
}

void QuestSystem::finishIfReady(Tracker& tracker, QuestRuntime& runtime, const QuestDefinition& definition) {
    if (runtime.state != "active") return;
    bool complete = std::all_of(runtime.objectives.begin(), runtime.objectives.end(),
                                [](const auto& objective) { return objective.done; });
    if (!complete) return;
    runtime.state = definition.completePolicy == "auto" ? "completed" : "ready";
    emit(tracker, runtime.id, runtime.state == "completed" ? "complete" : "ready");
    if (runtime.state == "completed") unlock(tracker);
}

void QuestSystem::notify(Tracker& tracker, const std::string& topic, const std::string& target, int amount) {
    if (topic.empty() || amount <= 0) return;
    for (const auto& id : tracker.order_) {
        auto& runtime = tracker.entries_.at(id);
        if (runtime.state != "active") continue;
        const auto* definition = QuestRegistry::find(id);
        if (!definition) continue;
        for (size_t i = 0; i < definition->objectives.size(); ++i) {
            const auto& spec     = definition->objectives[i];
            auto&       progress = runtime.objectives[i];
            if (progress.done || spec.topic != topic || (!spec.target.empty() && spec.target != target)) continue;
            int before       = progress.current;
            progress.current = std::min(progress.count, progress.current + amount);
            progress.done    = progress.current >= progress.count;
            emit(tracker, id, "progress", progress.id, topic, target, progress.current - before);
        }
        finishIfReady(tracker, runtime, *definition);
    }
}

bool QuestSystem::claim(Tracker& tracker, const std::string& id) {
    auto it = tracker.entries_.find(id);
    if (it == tracker.entries_.end() || it->second.state != "ready") return false;
    it->second.state = "completed";
    emit(tracker, id, "complete");
    unlock(tracker);
    return true;
}

bool QuestSystem::fail(Tracker& tracker, const std::string& id, const std::string& reason) {
    auto it = tracker.entries_.find(id);
    if (it == tracker.entries_.end() ||
        (it->second.state != "inactive" && it->second.state != "active" && it->second.state != "ready"))
        return false;
    it->second.state = "failed";
    emit(tracker, id, "fail", {}, {}, {}, 0, reason);
    return true;
}

bool QuestSystem::reset(Tracker& tracker, const std::string& id) {
    auto        it         = tracker.entries_.find(id);
    const auto* definition = QuestRegistry::find(id);
    if (it == tracker.entries_.end() || !definition) return false;
    for (auto& objective : it->second.objectives) {
        objective.current = 0;
        objective.done    = false;
    }
    it->second.state =
        requiresMet(tracker, *definition) ? (definition->startPolicy == "auto" ? "active" : "inactive") : "locked";
    emit(tracker, id, "reset");
    if (it->second.state == "active") {
        emit(tracker, id, "activate");
        finishIfReady(tracker, it->second, *definition);
    }
    return true;
}

void QuestSystem::unlock(Tracker& tracker) {
    bool changed;
    do {
        changed = false;
        for (const auto& id : tracker.order_) {
            auto&       runtime    = tracker.entries_.at(id);
            const auto* definition = QuestRegistry::find(id);
            if (!definition || runtime.state != "locked" || !requiresMet(tracker, *definition)) continue;
            runtime.state = definition->startPolicy == "auto" ? "active" : "inactive";
            emit(tracker, id, "unlock");
            if (runtime.state == "active") {
                emit(tracker, id, "activate");
                finishIfReady(tracker, runtime, *definition);
            }
            changed = true;
        }
    } while (changed);
}

}  // namespace eve::rpg
