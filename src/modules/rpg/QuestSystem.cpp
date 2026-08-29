#include "rpg/QuestSystem.h"

#include "rpg/Quest.h"
#include "rpg/Tracker.h"

#include <algorithm>
#include <string>
#include <vector>

namespace eve::rpg {

namespace {

bool objectivesFull(const QuestRuntime &rt) {
    for (const auto &o : rt.objectives) {
        if (!o.done) return false;
    }
    return true;
}

void pushEvent(Tracker *t, QuestEvent ev) { t->pending.push_back(std::move(ev)); }

void pushReject(Tracker *t, const std::string &entryId, const std::string &reason) {
    QuestEvent ev;
    ev.entryId = entryId;
    ev.action = "reject";
    ev.reason = reason;
    t->pending.push_back(std::move(ev));
}

void onObjectivesFull(Tracker *t, QuestRuntime &rt) {
    const QuestDefinition *def = QuestRegistry::find(rt.id);
    const std::string      policy = def ? def->completePolicy : "auto";
    if (policy == "claim") {
        if (rt.state == "ready" || rt.state == "completed") return;
        rt.state = "ready";
        pushEvent(t, {rt.id, "", "ready", "", "", 0, ""});
    } else {
        if (rt.state == "completed") return;
        rt.state = "completed";
        pushEvent(t, {rt.id, "", "complete", "", "", 0, ""});
        QuestSystem::syncAuto(t);
    }
}

}  // namespace

bool QuestSystem::hasMetRequirements(const Tracker &t, const QuestDefinition &def) {
    for (const auto &id : def.requiresIds) {
        auto it = t.entries.find(id);
        if (it == t.entries.end()) return false;
        if (it->second.state != "completed") return false;
    }
    return true;
}

std::string QuestSystem::stateForUnlocked(const QuestDefinition &def) {
    return def.startPolicy == "auto" ? "active" : "inactive";
}

void QuestSystem::syncAuto(Tracker *t) {
    if (!t) return;
    // 1. 补建 Registry 里尚未出现在本 Tracker 上的条目。
    for (const auto &id : QuestRegistry::ids()) {
        if (t->entries.count(id)) continue;
        const QuestDefinition *def = QuestRegistry::find(id);
        if (!def) continue;
        QuestRuntime rt;
        rt.id = id;
        rt.state = "locked";
        for (const auto &obj : def->objectives) {
            ObjectiveRuntime orun;
            orun.id = obj.id;
            orun.current = 0;
            orun.count = obj.count > 0 ? obj.count : 1;
            orun.done = false;
            rt.objectives.push_back(std::move(orun));
        }
        t->entries[id] = std::move(rt);
        t->order.push_back(id);
    }
    // 2. 反复解开前置已齐的 locked 条目。
    for (size_t pass = 0; pass <= t->entries.size(); ++pass) {
        bool changed = false;
        for (auto &[id, rt] : t->entries) {
            if (rt.state != "locked") continue;
            const QuestDefinition *def = QuestRegistry::find(id);
            if (!def) continue;
            if (!hasMetRequirements(*t, *def)) continue;
            const std::string newState = stateForUnlocked(*def);
            rt.state = newState;
            if (newState == "active") pushEvent(t, {id, "", "activate", "", "", 0, ""});
            if (newState == "active" && objectivesFull(rt)) onObjectivesFull(t, rt);
            changed = true;
        }
        if (!changed) break;
    }
}

std::string QuestSystem::canActivateReason(Tracker *t, const std::string &id) {
    if (!t) return "unknown";
    auto it = t->entries.find(id);
    if (it == t->entries.end()) return "unknown";
    const QuestDefinition *def = QuestRegistry::find(id);
    if (!def) return "unknown";
    const std::string &state = it->second.state;
    if (state == "locked") return "locked";
    if (state == "active") return "alreadyActive";
    if (state == "completed") return "alreadyCompleted";
    if (state == "failed") return "failed";
    if (state == "ready") return "ready";
    if (!hasMetRequirements(*t, *def)) return "locked";
    return "";
}

bool QuestSystem::canActivate(Tracker *t, const std::string &id, std::string *reason) {
    std::string r = canActivateReason(t, id);
    if (reason) *reason = r;
    return r.empty();
}

bool QuestSystem::activate(Tracker *t, const std::string &id, std::string *reason) {
    if (!t) {
        if (reason) *reason = "unknown";
        return false;
    }
    auto it = t->entries.find(id);
    if (it == t->entries.end()) {
        if (reason) *reason = "unknown";
        pushReject(t, id, "unknown");
        return false;
    }
    const std::string why = canActivateReason(t, id);
    if (!why.empty()) {
        if (reason) *reason = why;
        pushReject(t, id, why);
        return false;
    }
    it->second.state = "active";
    pushEvent(t, {id, "", "activate", "", "", 0, ""});
    if (objectivesFull(it->second)) onObjectivesFull(t, it->second);
    return true;
}

void QuestSystem::notify(Tracker *t, const std::string &topic, const std::string &target, int amount) {
    if (!t || amount <= 0) return;
    std::vector<std::string> fullIds;
    for (auto &[id, rt] : t->entries) {
        if (rt.state != "active") continue;
        const QuestDefinition *def = QuestRegistry::find(id);
        if (!def) continue;
        const size_t n = std::min(def->objectives.size(), rt.objectives.size());
        for (size_t i = 0; i < n; ++i) {
            const auto &objDef = def->objectives[i];
            auto &      rtObj = rt.objectives[i];
            if (rtObj.done) continue;
            if (rtObj.count <= 0) rtObj.count = 1;
            const bool match =
                objDef.topic == topic && (objDef.target.empty() || objDef.target == target);
            if (!match) continue;
            const int added = std::min(amount, rtObj.count - rtObj.current);
            rtObj.current += added;
            if (rtObj.current >= rtObj.count) rtObj.done = true;
            pushEvent(t, {id, rtObj.id, "progress", topic, target, added, ""});
        }
        if (objectivesFull(rt)) fullIds.push_back(id);
    }
    for (const auto &fid : fullIds) {
        auto it = t->entries.find(fid);
        if (it != t->entries.end()) onObjectivesFull(t, it->second);
    }
}

bool QuestSystem::claim(Tracker *t, const std::string &id, std::string *reason) {
    if (!t) {
        if (reason) *reason = "unknown";
        return false;
    }
    auto it = t->entries.find(id);
    if (it == t->entries.end()) {
        if (reason) *reason = "unknown";
        pushReject(t, id, "unknown");
        return false;
    }
    if (it->second.state != "ready") {
        if (reason) *reason = it->second.state;
        pushReject(t, id, it->second.state);
        return false;
    }
    it->second.state = "completed";
    pushEvent(t, {id, "", "complete", "", "", 0, ""});
    syncAuto(t);
    return true;
}

bool QuestSystem::reset(Tracker *t, const std::string &id) {
    if (!t) return false;
    auto it = t->entries.find(id);
    if (it == t->entries.end()) return false;  // 未知 id：false，无事件
    const QuestDefinition *def = QuestRegistry::find(id);
    QuestRuntime &         rt = it->second;
    rt.objectives.clear();
    if (def) {
        for (const auto &obj : def->objectives) {
            ObjectiveRuntime orun;
            orun.id = obj.id;
            orun.current = 0;
            orun.count = obj.count > 0 ? obj.count : 1;
            orun.done = false;
            rt.objectives.push_back(std::move(orun));
        }
    }
    rt.state = (def && hasMetRequirements(*t, *def)) ? stateForUnlocked(*def) : std::string("locked");
    pushEvent(t, {id, "", "reset", "", "", 0, ""});
    if (rt.state == "active" && objectivesFull(rt)) onObjectivesFull(t, rt);
    return true;
}

namespace {
bool failImpl(Tracker *t, const std::string &id, const std::string &reason) {
    if (!t) return false;
    auto it = t->entries.find(id);
    if (it == t->entries.end()) {
        pushReject(t, id, "unknown");
        return false;
    }
    const std::string &st = it->second.state;
    if (st != "inactive" && st != "active" && st != "ready") {
        pushReject(t, id, st);
        return false;
    }
    it->second.state = "failed";
    pushEvent(t, {id, "", "fail", "", "", 0, reason});
    return true;
}
}  // namespace

bool QuestSystem::abandon(Tracker *t, const std::string &id) { return failImpl(t, id, "abandon"); }

bool QuestSystem::fail(Tracker *t, const std::string &id, const std::string &reason) {
    return failImpl(t, id, reason);
}

void QuestSystem::pollEvents(Tracker *t, std::vector<QuestEvent> &out) {
    out.clear();
    if (!t) return;
    t->pollEvents();
    out = t->polled;
}

}  // namespace eve::rpg