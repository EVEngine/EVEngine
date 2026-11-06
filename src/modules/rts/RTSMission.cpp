/**
 * @file RTSMission.cpp
 * @brief Match campaign trigger host evaluation.
 */

#include "rts/RTSMission.h"

#include "rts/RTSScriptInternal.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::rts {
namespace {

MissionTrigger* findTrigger(Match& match, std::string_view id) {
    for (auto& trigger : match.campaign()->triggers)
        if (trigger.id == id) return &trigger;
    return nullptr;
}

MissionRegionMonitor* findMonitor(Match& match, std::string_view id) {
    for (auto& monitor : match.campaign()->monitors)
        if (monitor.id == id) return &monitor;
    return nullptr;
}

MissionTimer* findTimer(Match& match, std::string_view id) {
    for (auto& timer : match.campaign()->timers)
        if (timer.id == id) return &timer;
    return nullptr;
}

MissionVariable* findVariable(Match& match, std::string_view id) {
    for (auto& variable : match.campaign()->variables)
        if (variable.id == id) return &variable;
    return nullptr;
}

Result<void> requireNonEmptyId(std::string_view id, const char* field) {
    if (id.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "RTS campaign entry requires a non-empty id", field, {},
                                                       "rts.mission"));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

bool compareVariable(double left, MissionVariableCompare op, double right) {
    switch (op) {
        case MissionVariableCompare::Eq: return left == right;
        case MissionVariableCompare::Ne: return left != right;
        case MissionVariableCompare::Lt: return left < right;
        case MissionVariableCompare::Le: return left <= right;
        case MissionVariableCompare::Gt: return left > right;
        case MissionVariableCompare::Ge: return left >= right;
    }
    return false;
}

bool conditionsPass(Match& match, const MissionTrigger& trigger) {
    for (const auto& condition : trigger.conditions) {
        const auto* variable = findVariable(match, condition.variable);
        const double value = variable == nullptr ? 0.0 : variable->value;
        if (!compareVariable(value, condition.op, condition.value)) return false;
    }
    return true;
}

bool eventMatches(const MissionPendingEvent& event, const MissionTriggerEventFilter& filter) {
    if (event.kind != filter.kind) return false;
    if (!filter.id.empty() && event.id != filter.id) return false;
    if (filter.subject.isValid() && event.subject != filter.subject) return false;
    if (filter.faction.isValid() && event.faction != filter.faction) return false;
    if (!filter.archetype.empty() && event.archetype != filter.archetype) return false;
    return true;
}

bool triggerWantsEvent(const MissionTrigger& trigger, const MissionPendingEvent& event) {
    if (trigger.events.empty()) return false;
    for (const auto& filter : trigger.events)
        if (eventMatches(event, filter)) return true;
    return false;
}

bool factionParticipates(Match& match, Faction* faction) {
    if (faction == nullptr) return false;
    for (const auto& entry : match.participants()->entries) {
        if (entry.eliminated) continue;
        if (entry.faction.resolve() == faction) return true;
    }
    return false;
}

bool monitorAccepts(Match& match, const MissionRegionMonitor& monitor, SubjectRef subject, Faction* owner) {
    if (!monitor.subjects.empty()) {
        const auto found =
            std::find(monitor.subjects.begin(), monitor.subjects.end(), subject) != monitor.subjects.end();
        if (!found) return false;
    }
    if (monitor.faction.isValid()) {
        if (owner == nullptr || owner->identity()->subject != monitor.faction) return false;
    } else if (!factionParticipates(match, owner)) {
        return false;
    }
    return true;
}

bool insideRegion(const MissionRegionMonitor& monitor, float x, float y) {
    const float minX = std::min(monitor.minX, monitor.maxX);
    const float maxX = std::max(monitor.minX, monitor.maxX);
    const float minY = std::min(monitor.minY, monitor.maxY);
    const float maxY = std::max(monitor.minY, monitor.maxY);
    return x >= minX && x <= maxX && y >= minY && y <= maxY;
}

Result<void> enqueue(Match& match, MissionPendingEvent event) {
    match.campaign()->pending.push_back(std::move(event));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

const char* actionKindName(MissionActionKind kind) {
    switch (kind) {
        case MissionActionKind::SetVariable: return "set_variable";
        case MissionActionKind::AddVariable: return "add_variable";
        case MissionActionKind::StartTimer: return "start_timer";
        case MissionActionKind::StopTimer: return "stop_timer";
        case MissionActionKind::ResetTimer: return "reset_timer";
        case MissionActionKind::EnableTrigger: return "enable_trigger";
        case MissionActionKind::DisableTrigger: return "disable_trigger";
        case MissionActionKind::EnableMonitor: return "enable_monitor";
        case MissionActionKind::DisableMonitor: return "disable_monitor";
        case MissionActionKind::EmitCustom: return "emit_custom";
        case MissionActionKind::Script: return "script";
    }
    return "script";
}

Result<void> applyAction(Match& match, const MissionTrigger& trigger, const MissionAction& action,
                         std::uint64_t tick) {
    auto& campaign = *match.campaign();
    switch (action.kind) {
        case MissionActionKind::SetVariable: {
            auto set = MissionScriptSystem::setVariable(match, action.targetId, action.numeric);
            if (!set) return set;
            break;
        }
        case MissionActionKind::AddVariable: {
            const auto* existing = findVariable(match, action.targetId);
            const double next = (existing == nullptr ? 0.0 : existing->value) + action.numeric;
            auto set = MissionScriptSystem::setVariable(match, action.targetId, next);
            if (!set) return set;
            break;
        }
        case MissionActionKind::StartTimer: {
            auto* timer = findTimer(match, action.targetId);
            if (timer == nullptr)
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                               "RTS campaign timer was not found", "timer", {},
                                                               "rts.mission"));
            timer->running = true;
            break;
        }
        case MissionActionKind::StopTimer: {
            auto* timer = findTimer(match, action.targetId);
            if (timer == nullptr)
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                               "RTS campaign timer was not found", "timer", {},
                                                               "rts.mission"));
            timer->running = false;
            break;
        }
        case MissionActionKind::ResetTimer: {
            auto* timer = findTimer(match, action.targetId);
            if (timer == nullptr)
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                               "RTS campaign timer was not found", "timer", {},
                                                               "rts.mission"));
            timer->elapsed = Duration::zero();
            break;
        }
        case MissionActionKind::EnableTrigger:
        case MissionActionKind::DisableTrigger: {
            auto* target = findTrigger(match, action.targetId);
            if (target == nullptr)
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                               "RTS campaign trigger was not found", "trigger", {},
                                                               "rts.mission"));
            target->enabled = action.kind == MissionActionKind::EnableTrigger;
            if (action.kind == MissionActionKind::EnableTrigger) target->fired = false;
            break;
        }
        case MissionActionKind::EnableMonitor:
        case MissionActionKind::DisableMonitor: {
            auto* target = findMonitor(match, action.targetId);
            if (target == nullptr)
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                               "RTS campaign monitor was not found", "monitor", {},
                                                               "rts.mission"));
            target->enabled = action.kind == MissionActionKind::EnableMonitor;
            if (!target->enabled) target->occupants.clear();
            break;
        }
        case MissionActionKind::EmitCustom: {
            auto pushed = enqueue(match, MissionPendingEvent{MissionTriggerEventKind::Custom, action.targetId, {},
                                                             {}, {}, -1});
            if (!pushed) return pushed;
            break;
        }
        case MissionActionKind::Script:
            break;
    }
    if (action.kind == MissionActionKind::Script || action.kind == MissionActionKind::EmitCustom ||
        action.kind == MissionActionKind::SetVariable || action.kind == MissionActionKind::AddVariable ||
        action.kind == MissionActionKind::StartTimer || action.kind == MissionActionKind::StopTimer ||
        action.kind == MissionActionKind::ResetTimer || action.kind == MissionActionKind::EnableTrigger ||
        action.kind == MissionActionKind::DisableTrigger || action.kind == MissionActionKind::EnableMonitor ||
        action.kind == MissionActionKind::DisableMonitor) {
        campaign.actions.push_back(MissionActionLogEntry{++campaign.actionSequence, tick, trigger.id,
                                                         actionKindName(action.kind), action.targetId, action.numeric,
                                                         action.args});
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::size_t> fireTrigger(Match& match, MissionTrigger& trigger, std::uint64_t tick) {
    if (!conditionsPass(match, trigger))
        return Result<std::size_t>::success(0, Status::success(StatusCode::NoOp));
    std::size_t applied = 0;
    for (const auto& action : trigger.actions) {
        auto done = applyAction(match, trigger, action, tick);
        if (!done) return Result<std::size_t>::failure(done.status());
        ++applied;
    }
    if (trigger.once) {
        trigger.fired = true;
        trigger.enabled = false;
    }
    return Result<std::size_t>::success(applied, Status::success(StatusCode::Applied));
}

Result<void> bridgeMatchEvents(Match& match) {
    auto& campaign = *match.campaign();
    for (const auto& event : match.events()->values) {
        if (event.sequence <= campaign.bridgedEventSequence) continue;
        campaign.bridgedEventSequence = event.sequence;
        MissionPendingEvent pending;
        pending.id = event.reason;
        pending.faction = event.faction;
        pending.team = event.team;
        if (event.kind == "match_started")
            pending.kind = MissionTriggerEventKind::MatchStarted;
        else if (event.kind == "match_won" || event.kind == "match_draw")
            pending.kind = MissionTriggerEventKind::MatchFinished;
        else if (event.kind == "objective_completed")
            pending.kind = MissionTriggerEventKind::ObjectiveCompleted;
        else if (event.kind == "objective_failed")
            pending.kind = MissionTriggerEventKind::ObjectiveFailed;
        else
            continue;
        auto pushed = enqueue(match, std::move(pending));
        if (!pushed) return pushed;
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::size_t> advanceTimers(Match& match, const SimulationStep& step) {
    if (step.delta.isZero()) return Result<std::size_t>::success(0, Status::success(StatusCode::NoOp));
    std::size_t changed = 0;
    for (auto& timer : match.campaign()->timers) {
        if (!timer.running || timer.duration.isZero()) continue;
        auto next = timer.elapsed.tryAdd(step.delta);
        if (!next) return Result<std::size_t>::failure(next.status());
        timer.elapsed = std::move(next).takeValue();
        ++changed;
        while (timer.running && !timer.duration.isZero() && timer.elapsed >= timer.duration) {
            auto pushed =
                enqueue(match, MissionPendingEvent{MissionTriggerEventKind::TimerElapsed, timer.id, {}, {}, {}, -1});
            if (!pushed) return Result<std::size_t>::failure(pushed.status());
            if (timer.repeating) {
                const auto leftover = timer.elapsed.nanoseconds() - timer.duration.nanoseconds();
                if (leftover < 0)
                    return Result<std::size_t>::failure(Diagnostic::error(
                        DiagnosticCode::Conflict, "RTS campaign timer underflowed while repeating", "timer", {},
                        "rts.mission"));
                timer.elapsed = Duration::fromNanoseconds(leftover);
            } else {
                timer.elapsed = timer.duration;
                timer.running = false;
                break;
            }
        }
    }
    return Result<std::size_t>::success(changed,
                                        Status::success(changed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

SubjectRef ownerFactionSubject(Faction* faction) {
    return faction == nullptr ? SubjectRef{} : faction->identity()->subject;
}

Result<std::size_t> sampleMonitors(Match& match) {
    std::size_t changed = 0;
    for (auto& monitor : match.campaign()->monitors) {
        if (!monitor.enabled) continue;
        struct Occupant {
            SubjectRef subject;
            SubjectRef faction;
        };
        std::vector<Occupant> nowInside;
        auto units = ecs::View<Unit, Unit::Identity, Unit::Faction, Unit::Motion, Unit::Durability>();
        for (auto it = units.begin(); it != units.end(); ++it) {
            auto [identity, owner, motion, durability] = *it;
            if (!durability->alive || durability->state.health <= 0.0) continue;
            auto* faction = dynamic_cast<Faction*>(owner->link.resolve());
            if (!monitorAccepts(match, monitor, identity->subject, faction)) continue;
            if (!insideRegion(monitor, motion->x, motion->y)) continue;
            nowInside.push_back({identity->subject, ownerFactionSubject(faction)});
        }
        auto buildings =
            ecs::View<Building, Building::Identity, Building::Faction, Building::Placement, Building::Integrity>();
        for (auto it = buildings.begin(); it != buildings.end(); ++it) {
            auto [identity, owner, placement, integrity] = *it;
            if (!integrity->alive || integrity->state.health <= 0.0) continue;
            auto* faction = dynamic_cast<Faction*>(owner->link.resolve());
            if (!monitorAccepts(match, monitor, identity->subject, faction)) continue;
            if (!insideRegion(monitor, placement->worldX, placement->worldY)) continue;
            nowInside.push_back({identity->subject, ownerFactionSubject(faction)});
        }
        std::sort(nowInside.begin(), nowInside.end(), [](const Occupant& left, const Occupant& right) {
            return left.subject.format() < right.subject.format();
        });
        nowInside.erase(std::unique(nowInside.begin(), nowInside.end(),
                                    [](const Occupant& left, const Occupant& right) {
                                        return left.subject == right.subject;
                                    }),
                        nowInside.end());

        std::vector<SubjectRef> nowSubjects;
        nowSubjects.reserve(nowInside.size());
        for (const auto& occupant : nowInside) {
            nowSubjects.push_back(occupant.subject);
            const bool wasInside =
                std::find(monitor.occupants.begin(), monitor.occupants.end(), occupant.subject) !=
                monitor.occupants.end();
            if (wasInside) continue;
            auto pushed =
                enqueue(match, MissionPendingEvent{MissionTriggerEventKind::RegionEntered, monitor.id,
                                                   occupant.subject, occupant.faction, {}, -1});
            if (!pushed) return Result<std::size_t>::failure(pushed.status());
            ++changed;
        }
        for (const auto& subject : monitor.occupants) {
            const bool stillInside = std::find(nowSubjects.begin(), nowSubjects.end(), subject) != nowSubjects.end();
            if (stillInside) continue;
            auto pushed = enqueue(match, MissionPendingEvent{MissionTriggerEventKind::RegionExited, monitor.id,
                                                             subject, {}, {}, -1});
            if (!pushed) return Result<std::size_t>::failure(pushed.status());
            ++changed;
        }
        monitor.occupants = std::move(nowSubjects);
    }
    return Result<std::size_t>::success(changed,
                                        Status::success(changed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> sampleDeaths(Match& match) {
    std::vector<SubjectRef> aliveNow;
    std::map<std::string, MissionPendingEvent> deaths;
    auto units = ecs::View<Unit, Unit::Identity, Unit::Faction, Unit::Durability, Unit::Definition>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [identity, owner, durability, definition] = *it;
        auto* faction = dynamic_cast<Faction*>(owner->link.resolve());
        if (!factionParticipates(match, faction)) continue;
        if (durability->alive && durability->state.health > 0.0) {
            aliveNow.push_back(identity->subject);
            continue;
        }
        MissionPendingEvent event;
        event.kind = MissionTriggerEventKind::UnitDied;
        event.subject = identity->subject;
        if (faction != nullptr) event.faction = faction->identity()->subject;
        event.archetype = definition->id.name();
        deaths.emplace(identity->subject.format(), std::move(event));
    }
    auto buildings =
        ecs::View<Building, Building::Identity, Building::Faction, Building::Integrity, Building::Definition>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [identity, owner, integrity, definition] = *it;
        auto* faction = dynamic_cast<Faction*>(owner->link.resolve());
        if (!factionParticipates(match, faction)) continue;
        if (integrity->alive && integrity->state.health > 0.0) {
            aliveNow.push_back(identity->subject);
            continue;
        }
        MissionPendingEvent event;
        event.kind = MissionTriggerEventKind::BuildingDestroyed;
        event.subject = identity->subject;
        if (faction != nullptr) event.faction = faction->identity()->subject;
        event.archetype = definition->id.name();
        deaths.emplace(identity->subject.format(), std::move(event));
    }
    std::sort(aliveNow.begin(), aliveNow.end(),
              [](const SubjectRef& left, const SubjectRef& right) { return left.format() < right.format(); });
    aliveNow.erase(std::unique(aliveNow.begin(), aliveNow.end()), aliveNow.end());

    std::size_t changed = 0;
    auto& tracked = match.campaign()->trackedAlive;
    if (!tracked.empty()) {
        for (const auto& previous : tracked) {
            const bool stillAlive = std::find(aliveNow.begin(), aliveNow.end(), previous) != aliveNow.end();
            if (stillAlive) continue;
            auto found = deaths.find(previous.format());
            if (found == deaths.end()) {
                MissionPendingEvent event;
                event.kind = MissionTriggerEventKind::UnitDied;
                event.subject = previous;
                auto pushed = enqueue(match, std::move(event));
                if (!pushed) return Result<std::size_t>::failure(pushed.status());
            } else {
                auto pushed = enqueue(match, found->second);
                if (!pushed) return Result<std::size_t>::failure(pushed.status());
            }
            ++changed;
        }
    }
    tracked = std::move(aliveNow);
    return Result<std::size_t>::success(changed,
                                        Status::success(changed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> drainPending(Match& match, std::uint64_t tick) {
    std::size_t applied = 0;
    // Drain in waves so EmitCustom actions can wake later triggers in the same step.
    for (int wave = 0; wave < 8; ++wave) {
        if (match.campaign()->pending.empty()) break;
        auto batch = std::move(match.campaign()->pending);
        match.campaign()->pending.clear();
        for (const auto& event : batch) {
            std::vector<MissionTrigger*> woken;
            for (auto& trigger : match.campaign()->triggers) {
                if (!trigger.enabled || (trigger.once && trigger.fired)) continue;
                if (!triggerWantsEvent(trigger, event)) continue;
                woken.push_back(&trigger);
            }
            std::stable_sort(woken.begin(), woken.end(), [](const MissionTrigger* left, const MissionTrigger* right) {
                if (left->priority != right->priority) return left->priority > right->priority;
                return left->id < right->id;
            });
            for (auto* trigger : woken) {
                if (!trigger->enabled || (trigger->once && trigger->fired)) continue;
                auto fired = fireTrigger(match, *trigger, tick);
                if (!fired) return Result<std::size_t>::failure(fired.status());
                applied += fired.value();
            }
        }
    }
    return Result<std::size_t>::success(applied,
                                        Status::success(applied == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace

Result<void> MissionScriptSystem::addTrigger(Match& match, MissionTrigger trigger) {
    auto id = requireNonEmptyId(trigger.id, "trigger.id");
    if (!id) return id;
    if (trigger.events.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "RTS campaign trigger requires at least one event",
                                                       "trigger.events", {}, "rts.mission"));
    if (findTrigger(match, trigger.id) != nullptr)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                       "RTS campaign trigger id already exists", "trigger.id", {},
                                                       "rts.mission"));
    match.campaign()->triggers.push_back(std::move(trigger));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MissionScriptSystem::addMonitor(Match& match, MissionRegionMonitor monitor) {
    auto id = requireNonEmptyId(monitor.id, "monitor.id");
    if (!id) return id;
    if (!(std::isfinite(monitor.minX) && std::isfinite(monitor.minY) && std::isfinite(monitor.maxX) &&
          std::isfinite(monitor.maxY)))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "RTS campaign monitor bounds must be finite", "monitor", {},
                                                       "rts.mission"));
    if (findMonitor(match, monitor.id) != nullptr)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                       "RTS campaign monitor id already exists", "monitor.id", {},
                                                       "rts.mission"));
    std::sort(monitor.subjects.begin(), monitor.subjects.end(),
              [](const SubjectRef& left, const SubjectRef& right) { return left.format() < right.format(); });
    monitor.subjects.erase(std::unique(monitor.subjects.begin(), monitor.subjects.end()), monitor.subjects.end());
    monitor.occupants.clear();
    match.campaign()->monitors.push_back(std::move(monitor));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MissionScriptSystem::addTimer(Match& match, MissionTimer timer) {
    auto id = requireNonEmptyId(timer.id, "timer.id");
    if (!id) return id;
    if (timer.duration.isZero() || timer.duration.nanoseconds() < 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "RTS campaign timer requires a positive duration",
                                                       "timer.duration", {}, "rts.mission"));
    if (findTimer(match, timer.id) != nullptr)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                       "RTS campaign timer id already exists", "timer.id", {},
                                                       "rts.mission"));
    timer.elapsed = Duration::zero();
    match.campaign()->timers.push_back(std::move(timer));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MissionScriptSystem::setVariable(Match& match, std::string id, double value) {
    auto valid = requireNonEmptyId(id, "variable.id");
    if (!valid) return valid;
    if (!std::isfinite(value))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "RTS campaign variable must be finite", "variable.value", {},
                                                       "rts.mission"));
    if (auto* existing = findVariable(match, id)) {
        existing->value = value;
        return Result<void>::success(Status::success(StatusCode::Applied));
    }
    match.campaign()->variables.push_back(MissionVariable{std::move(id), value});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<double> MissionScriptSystem::variable(Match& match, std::string_view id) noexcept {
    const auto* existing = findVariable(match, id);
    return Result<double>::success(existing == nullptr ? 0.0 : existing->value, Status::success(StatusCode::Applied));
}

Result<void> MissionScriptSystem::setTriggerEnabled(Match& match, std::string_view id, bool enabled) {
    auto* trigger = findTrigger(match, id);
    if (trigger == nullptr)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                       "RTS campaign trigger was not found", "trigger", {},
                                                       "rts.mission"));
    trigger->enabled = enabled;
    if (enabled) trigger->fired = false;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MissionScriptSystem::setMonitorEnabled(Match& match, std::string_view id, bool enabled) {
    auto* monitor = findMonitor(match, id);
    if (monitor == nullptr)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                       "RTS campaign monitor was not found", "monitor", {},
                                                       "rts.mission"));
    monitor->enabled = enabled;
    if (!enabled) monitor->occupants.clear();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MissionScriptSystem::controlTimer(Match& match, std::string_view id, bool running, bool reset) {
    auto* timer = findTimer(match, id);
    if (timer == nullptr)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                       "RTS campaign timer was not found", "timer", {},
                                                       "rts.mission"));
    if (reset) timer->elapsed = Duration::zero();
    timer->running = running;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MissionScriptSystem::pushEvent(Match& match, MissionPendingEvent event) {
    return enqueue(match, std::move(event));
}

Result<std::size_t> MissionScriptSystem::step(Match& match, const SimulationStep& step) {
    if (match.state()->phase == MatchPhase::Setup)
        return Result<std::size_t>::success(0, Status::success(StatusCode::NoOp));
    std::size_t changed = 0;
    auto bridged = bridgeMatchEvents(match);
    if (!bridged) return Result<std::size_t>::failure(bridged.status());
    auto timers = advanceTimers(match, step);
    if (!timers) return Result<std::size_t>::failure(timers.status());
    changed += timers.value();
    auto monitors = sampleMonitors(match);
    if (!monitors) return Result<std::size_t>::failure(monitors.status());
    changed += monitors.value();
    auto deaths = sampleDeaths(match);
    if (!deaths) return Result<std::size_t>::failure(deaths.status());
    changed += deaths.value();
    auto drained = drainPending(match, step.tick.value());
    if (!drained) return Result<std::size_t>::failure(drained.status());
    changed += drained.value();
    return Result<std::size_t>::success(changed,
                                        Status::success(changed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<void> MissionScriptSystem::clearActionLog(Match& match) noexcept {
    match.campaign()->actions.clear();
    return Result<void>::success(Status::success(StatusCode::Applied));
}

namespace {

const Value::Object* asObject(const Value& value) { return value.getIf<Value::Object>(); }

Result<std::string> requireObjectString(const Value::Object& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end() || found->second.getIf<std::string>() == nullptr)
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "RTS campaign JSON field missing", key, {},
                              "rts.mission"));
    return Result<std::string>::success(*found->second.getIf<std::string>());
}

std::string optionalObjectString(const Value::Object& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end() || found->second.getIf<std::string>() == nullptr) return {};
    return *found->second.getIf<std::string>();
}

bool optionalObjectBool(const Value::Object& object, const char* key, bool fallback) {
    const auto found = object.find(key);
    if (found == object.end() || found->second.getIf<bool>() == nullptr) return fallback;
    return *found->second.getIf<bool>();
}

double optionalObjectNumber(const Value::Object& object, const char* key, double fallback) {
    const auto found = object.find(key);
    if (found == object.end()) return fallback;
    if (const auto* asDouble = found->second.getIf<double>()) return *asDouble;
    if (const auto* asInt = found->second.getIf<std::int64_t>()) return static_cast<double>(*asInt);
    return fallback;
}

Result<MissionTriggerEventKind> parseEventKind(std::string_view text) {
    if (text == "match_started") return Result<MissionTriggerEventKind>::success(MissionTriggerEventKind::MatchStarted);
    if (text == "match_finished")
        return Result<MissionTriggerEventKind>::success(MissionTriggerEventKind::MatchFinished);
    if (text == "objective_completed")
        return Result<MissionTriggerEventKind>::success(MissionTriggerEventKind::ObjectiveCompleted);
    if (text == "objective_failed")
        return Result<MissionTriggerEventKind>::success(MissionTriggerEventKind::ObjectiveFailed);
    if (text == "timer_elapsed") return Result<MissionTriggerEventKind>::success(MissionTriggerEventKind::TimerElapsed);
    if (text == "region_entered")
        return Result<MissionTriggerEventKind>::success(MissionTriggerEventKind::RegionEntered);
    if (text == "region_exited") return Result<MissionTriggerEventKind>::success(MissionTriggerEventKind::RegionExited);
    if (text == "unit_died") return Result<MissionTriggerEventKind>::success(MissionTriggerEventKind::UnitDied);
    if (text == "building_destroyed")
        return Result<MissionTriggerEventKind>::success(MissionTriggerEventKind::BuildingDestroyed);
    if (text == "custom") return Result<MissionTriggerEventKind>::success(MissionTriggerEventKind::Custom);
    return Result<MissionTriggerEventKind>::failure(Diagnostic::error(
        DiagnosticCode::InvalidArgument, "RTS campaign trigger event kind is invalid", "events.kind", {},
        "rts.mission"));
}

Result<MissionActionKind> parseActionKind(std::string_view text) {
    if (text == "set_variable") return Result<MissionActionKind>::success(MissionActionKind::SetVariable);
    if (text == "add_variable") return Result<MissionActionKind>::success(MissionActionKind::AddVariable);
    if (text == "start_timer") return Result<MissionActionKind>::success(MissionActionKind::StartTimer);
    if (text == "stop_timer") return Result<MissionActionKind>::success(MissionActionKind::StopTimer);
    if (text == "reset_timer") return Result<MissionActionKind>::success(MissionActionKind::ResetTimer);
    if (text == "enable_trigger") return Result<MissionActionKind>::success(MissionActionKind::EnableTrigger);
    if (text == "disable_trigger") return Result<MissionActionKind>::success(MissionActionKind::DisableTrigger);
    if (text == "enable_monitor") return Result<MissionActionKind>::success(MissionActionKind::EnableMonitor);
    if (text == "disable_monitor") return Result<MissionActionKind>::success(MissionActionKind::DisableMonitor);
    if (text == "emit_custom") return Result<MissionActionKind>::success(MissionActionKind::EmitCustom);
    if (text == "script") return Result<MissionActionKind>::success(MissionActionKind::Script);
    return Result<MissionActionKind>::failure(Diagnostic::error(
        DiagnosticCode::InvalidArgument, "RTS campaign action kind is invalid", "actions.kind", {}, "rts.mission"));
}

Result<MissionVariableCompare> parseCompare(std::string_view text) {
    if (text == "eq") return Result<MissionVariableCompare>::success(MissionVariableCompare::Eq);
    if (text == "ne") return Result<MissionVariableCompare>::success(MissionVariableCompare::Ne);
    if (text == "lt") return Result<MissionVariableCompare>::success(MissionVariableCompare::Lt);
    if (text == "le") return Result<MissionVariableCompare>::success(MissionVariableCompare::Le);
    if (text == "gt") return Result<MissionVariableCompare>::success(MissionVariableCompare::Gt);
    if (text == "ge") return Result<MissionVariableCompare>::success(MissionVariableCompare::Ge);
    return Result<MissionVariableCompare>::failure(Diagnostic::error(
        DiagnosticCode::InvalidArgument, "RTS campaign condition operator is invalid", "conditions.op", {},
        "rts.mission"));
}

}  // namespace

Result<MissionTrigger> MissionScriptSystem::parseTriggerJson(std::string_view json) {
    auto parsed = Value::fromJson(json);
    if (!parsed) return Result<MissionTrigger>::failure(parsed.status());
    const auto* object = asObject(parsed.value());
    if (object == nullptr)
        return Result<MissionTrigger>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS campaign trigger JSON must be an object", "trigger", {},
            "rts.mission"));
    auto id = requireObjectString(*object, "id");
    if (!id) return Result<MissionTrigger>::failure(id.status());
    MissionTrigger trigger;
    trigger.id = std::move(id).takeValue();
    trigger.enabled = optionalObjectBool(*object, "enabled", true);
    trigger.once = optionalObjectBool(*object, "once", true);
    trigger.priority = static_cast<int>(optionalObjectNumber(*object, "priority", 0.0));
    const auto eventsFound = object->find("events");
    if (eventsFound == object->end() || eventsFound->second.getIf<Value::Array>() == nullptr)
        return Result<MissionTrigger>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS campaign trigger requires an events array", "events", {},
            "rts.mission"));
    for (const auto& entry : *eventsFound->second.getIf<Value::Array>()) {
        const auto* eventObject = entry.getIf<Value::Object>();
        if (eventObject == nullptr)
            return Result<MissionTrigger>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "RTS campaign trigger events must be objects", "events", {},
                "rts.mission"));
        auto kindText = requireObjectString(*eventObject, "kind");
        if (!kindText) return Result<MissionTrigger>::failure(kindText.status());
        auto kind = parseEventKind(kindText.value());
        if (!kind) return Result<MissionTrigger>::failure(kind.status());
        MissionTriggerEventFilter filter;
        filter.kind = kind.value();
        filter.id = optionalObjectString(*eventObject, "id");
        filter.archetype = optionalObjectString(*eventObject, "archetype");
        if (const auto subjectText = optionalObjectString(*eventObject, "subject"); !subjectText.empty()) {
            auto subject = script_internal::parseScriptSubject(subjectText, "events.subject");
            if (!subject) return Result<MissionTrigger>::failure(subject.status());
            filter.subject = std::move(subject).takeValue();
        }
        if (const auto factionText = optionalObjectString(*eventObject, "faction"); !factionText.empty()) {
            auto faction = script_internal::parseScriptSubject(factionText, "events.faction");
            if (!faction) return Result<MissionTrigger>::failure(faction.status());
            filter.faction = std::move(faction).takeValue();
        }
        trigger.events.push_back(std::move(filter));
    }
    if (const auto conditionsFound = object->find("conditions");
        conditionsFound != object->end() && conditionsFound->second.getIf<Value::Array>() != nullptr) {
        for (const auto& entry : *conditionsFound->second.getIf<Value::Array>()) {
            const auto* conditionObject = entry.getIf<Value::Object>();
            if (conditionObject == nullptr)
                return Result<MissionTrigger>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "RTS campaign conditions must be objects", "conditions", {},
                    "rts.mission"));
            auto variable = requireObjectString(*conditionObject, "variable");
            if (!variable) return Result<MissionTrigger>::failure(variable.status());
            auto opText = optionalObjectString(*conditionObject, "op");
            if (opText.empty()) opText = "eq";
            auto op = parseCompare(opText);
            if (!op) return Result<MissionTrigger>::failure(op.status());
            MissionTriggerCondition condition;
            condition.variable = std::move(variable).takeValue();
            condition.op = op.value();
            condition.value = optionalObjectNumber(*conditionObject, "value", 0.0);
            trigger.conditions.push_back(std::move(condition));
        }
    }
    if (const auto actionsFound = object->find("actions");
        actionsFound != object->end() && actionsFound->second.getIf<Value::Array>() != nullptr) {
        for (const auto& entry : *actionsFound->second.getIf<Value::Array>()) {
            const auto* actionObject = entry.getIf<Value::Object>();
            if (actionObject == nullptr)
                return Result<MissionTrigger>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "RTS campaign actions must be objects", "actions", {},
                    "rts.mission"));
            auto kindText = requireObjectString(*actionObject, "kind");
            if (!kindText) return Result<MissionTrigger>::failure(kindText.status());
            auto kind = parseActionKind(kindText.value());
            if (!kind) return Result<MissionTrigger>::failure(kind.status());
            MissionAction action;
            action.kind = kind.value();
            action.targetId = optionalObjectString(*actionObject, "targetId");
            action.numeric = optionalObjectNumber(*actionObject, "numeric", 0.0);
            if (const auto argsFound = actionObject->find("args"); argsFound != actionObject->end())
                action.args = argsFound->second;
            trigger.actions.push_back(std::move(action));
        }
    }
    return Result<MissionTrigger>::success(std::move(trigger), Status::success(StatusCode::Applied));
}

Result<MissionRegionMonitor> MissionScriptSystem::parseMonitorJson(std::string_view json) {
    auto parsed = Value::fromJson(json);
    if (!parsed) return Result<MissionRegionMonitor>::failure(parsed.status());
    const auto* object = asObject(parsed.value());
    if (object == nullptr)
        return Result<MissionRegionMonitor>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS campaign monitor JSON must be an object", "monitor", {},
            "rts.mission"));
    auto id = requireObjectString(*object, "id");
    if (!id) return Result<MissionRegionMonitor>::failure(id.status());
    MissionRegionMonitor monitor;
    monitor.id = std::move(id).takeValue();
    monitor.enabled = optionalObjectBool(*object, "enabled", true);
    monitor.minX = static_cast<float>(optionalObjectNumber(*object, "minX", 0.0));
    monitor.minY = static_cast<float>(optionalObjectNumber(*object, "minY", 0.0));
    monitor.maxX = static_cast<float>(optionalObjectNumber(*object, "maxX", 0.0));
    monitor.maxY = static_cast<float>(optionalObjectNumber(*object, "maxY", 0.0));
    if (const auto factionText = optionalObjectString(*object, "faction"); !factionText.empty()) {
        auto faction = script_internal::parseScriptSubject(factionText, "monitor.faction");
        if (!faction) return Result<MissionRegionMonitor>::failure(faction.status());
        monitor.faction = std::move(faction).takeValue();
    }
    if (const auto subjectsFound = object->find("subjects");
        subjectsFound != object->end() && subjectsFound->second.getIf<Value::Array>() != nullptr) {
        for (const auto& entry : *subjectsFound->second.getIf<Value::Array>()) {
            const auto* text = entry.getIf<std::string>();
            if (text == nullptr)
                return Result<MissionRegionMonitor>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "RTS campaign monitor subjects must be strings", "subjects", {},
                    "rts.mission"));
            auto subject = script_internal::parseScriptSubject(*text, "monitor.subjects");
            if (!subject) return Result<MissionRegionMonitor>::failure(subject.status());
            monitor.subjects.push_back(std::move(subject).takeValue());
        }
    }
    return Result<MissionRegionMonitor>::success(std::move(monitor), Status::success(StatusCode::Applied));
}

}  // namespace eve::rts
