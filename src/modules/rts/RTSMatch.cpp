#include "rts/RTSMatch.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace eve::rts {
namespace {

void emit(Match& match, std::string kind, Faction* faction, int team, std::string reason = {}) {
    auto state = match.state();
    match.events()->values.push_back({++state->updateSequence, std::move(kind),
                                      faction == nullptr ? SubjectRef{} : faction->identity()->subject,
                                      team, std::move(reason)});
}

Match::Participants::Entry* participant(Match& match, Faction& faction) {
    const auto found = std::find_if(match.participants()->entries.begin(), match.participants()->entries.end(),
                                    [&](auto& value) { return value.faction.resolve() == &faction; });
    return found == match.participants()->entries.end() ? nullptr : &*found;
}

const Match::Participants::Entry* participantByFactionSubject(Match& match, SubjectRef subject) {
    for (const auto& entry : match.participants()->entries) {
        auto* faction = dynamic_cast<Faction*>(entry.faction.resolve());
        if (faction != nullptr && faction->identity()->subject == subject) return &entry;
    }
    return nullptr;
}

bool teamIsLive(Match& match, int team) {
    for (const auto& entry : match.participants()->entries)
        if (!entry.eliminated && entry.team == team && entry.faction.resolve() != nullptr) return true;
    return false;
}

void eliminate(Match& match, Match::Participants::Entry& entry, std::string reason, bool surrendered = false) {
    if (entry.eliminated) return;
    entry.eliminated = true;
    entry.surrendered = surrendered;
    entry.reason = reason;
    auto* faction = dynamic_cast<Faction*>(entry.faction.resolve());
    emit(match, "faction_eliminated", faction, entry.team, std::move(reason));
}

bool owns(const FactionLink& link, Faction& faction) { return link.resolve() == &faction; }

bool factionHasPresence(Faction& faction) {
    auto units = ecs::View<Unit, Unit::Faction, Unit::Durability>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [owner, durability] = *it;
        if (owns(owner->link, faction) && durability->alive && durability->state.health > 0.0) return true;
    }
    auto buildings = ecs::View<Building, Building::Faction, Building::Integrity>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [owner, integrity] = *it;
        if (owns(owner->link, faction) && integrity->alive && integrity->state.health > 0.0) return true;
    }
    return false;
}

bool survives(Faction& faction, const Match::Rules& rules) {
    if (rules.rule == VictoryRule::DestroyHeadquarters) {
        auto buildings = ecs::View<Building, Building::Definition, Building::Faction, Building::Integrity,
                                   Building::Construction>();
        for (auto it = buildings.begin(); it != buildings.end(); ++it) {
            auto [definition, owner, integrity, construction] = *it;
            if (owns(owner->link, faction) && integrity->alive && integrity->state.health > 0.0 &&
                construction->progress >= 1.0f && definition->id.name() == rules.archetype)
                return true;
        }
        return false;
    }
    return factionHasPresence(faction);
}

bool teamHasPresence(Match& match, int team) {
    for (auto& entry : match.participants()->entries) {
        if (entry.eliminated || entry.team != team) continue;
        auto* faction = dynamic_cast<Faction*>(entry.faction.resolve());
        if (faction != nullptr && factionHasPresence(*faction)) return true;
    }
    return false;
}

void destroyFactionEntities(Faction& faction) {
    auto units = ecs::View<Unit, Unit::Faction, Unit::Durability>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [owner, durability] = *it;
        if (owns(owner->link, faction)) {
            durability->alive = false;
            durability->state.health = 0.0;
        }
    }
    auto buildings = ecs::View<Building, Building::Faction, Building::Integrity>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [owner, integrity] = *it;
        if (owns(owner->link, faction)) {
            integrity->alive = false;
            integrity->state.health = 0.0;
        }
    }
}

void settleTeams(Match& match, std::string reason = "last_team_standing") {
    std::set<int> teams;
    for (auto& entry : match.participants()->entries)
        if (!entry.eliminated && entry.faction.resolve() != nullptr) teams.insert(entry.team);
    if (teams.size() > 1) return;
    match.state()->phase = MatchPhase::Finished;
    match.state()->winningTeam = teams.empty() ? -1 : *teams.begin();
    emit(match, teams.empty() ? "match_draw" : "match_won", nullptr, match.state()->winningTeam,
         teams.empty() ? "no_survivors" : std::move(reason));
}

Match::Scores::Entry* scoreEntry(Match& match, int team) {
    auto& entries = match.scores()->entries;
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.team == team; });
    return found == entries.end() ? nullptr : &*found;
}

Result<void> addScore(Match& match, int team, double amount) {
    if (!std::isfinite(amount) || amount == 0.0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "RTS match score credit must be a finite non-zero amount",
                                                       "score", {}, "rts.match"));
    auto* entry = scoreEntry(match, team);
    if (entry == nullptr) {
        match.scores()->entries.push_back({team, 0.0});
        std::sort(match.scores()->entries.begin(), match.scores()->entries.end(),
                  [](const auto& left, const auto& right) { return left.team < right.team; });
        entry = scoreEntry(match, team);
    }
    if (entry == nullptr)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation,
                                                       "RTS match score entry was not created", "score", {},
                                                       "rts.match"));
    const double next = entry->value + amount;
    if (!std::isfinite(next))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "RTS match score overflowed", "score", {}, "rts.match"));
    entry->value = next;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

std::map<int, double> liveTeamScores(Match& match) {
    std::map<int, double> scores;
    for (const auto& entry : match.participants()->entries) {
        if (entry.eliminated || entry.faction.resolve() == nullptr) continue;
        scores.emplace(entry.team, 0.0);
    }
    for (const auto& entry : match.scores()->entries) {
        auto found = scores.find(entry.team);
        if (found != scores.end()) found->second = entry.value;
    }
    return scores;
}

void finishWithTeam(Match& match, int team, std::string reason) {
    match.state()->phase = MatchPhase::Finished;
    match.state()->winningTeam = team;
    emit(match, team < 0 ? "match_draw" : "match_won", nullptr, team, std::move(reason));
}

void settleHighestScore(Match& match, const std::map<int, double>& scores, std::string reason) {
    if (scores.empty()) {
        finishWithTeam(match, -1, std::move(reason));
        return;
    }
    double best = -std::numeric_limits<double>::infinity();
    int winner = -1;
    bool tie = false;
    for (const auto& [team, value] : scores) {
        if (value > best) {
            best = value;
            winner = team;
            tie = false;
        } else if (value == best) {
            tie = true;
        }
    }
    finishWithTeam(match, tie ? -1 : winner, std::move(reason));
}

Result<void> settleTimedResource(Match& match, const MatchResourceQuery& resources) {
    if (!resources)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "resource victory requires an authoritative economy query",
            "resources", {}, "rts.match"));
    std::map<int, double> totals;
    for (auto& entry : match.participants()->entries) {
        if (entry.eliminated) continue;
        auto* faction = dynamic_cast<Faction*>(entry.faction.resolve());
        if (faction == nullptr) continue;
        auto balance = resources(*faction, match.rules()->archetype);
        if (!balance) return Result<void>::failure(balance.status());
        totals[entry.team] += balance.value();
    }
    settleHighestScore(match, totals, "time_expired");
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::size_t> accrueControlPoints(Match& match, const SimulationStep& step) {
    if (step.delta.isZero()) return Result<std::size_t>::success(0, Status::success(StatusCode::NoOp));
    const double seconds = step.delta.seconds();
    if (!(seconds > 0.0) || !std::isfinite(seconds))
        return Result<std::size_t>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS match control-point accrual requires a positive finite delta",
            "step.delta", {}, "rts.match"));
    std::map<Faction*, int> factionTeams;
    for (auto& entry : match.participants()->entries) {
        if (entry.eliminated) continue;
        auto* faction = dynamic_cast<Faction*>(entry.faction.resolve());
        if (faction == nullptr) continue;
        factionTeams.emplace(faction, entry.team);
    }
    std::size_t credited = 0;
    auto buildings =
        ecs::View<Building, Building::Faction, Building::Capture, Building::Integrity, Building::Construction>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [owner, capture, integrity, construction] = *it;
        if (!std::isfinite(capture->scorePerSecond) || capture->scorePerSecond <= 0.0f) continue;
        if (!integrity->alive || integrity->state.health <= 0.0 || construction->progress < 1.0f) continue;
        auto* faction = dynamic_cast<Faction*>(owner->link.resolve());
        if (faction == nullptr) continue;
        const auto team = factionTeams.find(faction);
        if (team == factionTeams.end()) continue;
        const double amount = static_cast<double>(capture->scorePerSecond) * seconds;
        auto creditedScore = addScore(match, team->second, amount);
        if (!creditedScore) return Result<std::size_t>::failure(creditedScore.status());
        ++credited;
    }
    return Result<std::size_t>::success(credited,
                                        Status::success(credited == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

bool needsScoreAccrual(Match& match) {
    if (match.rules()->rule == VictoryRule::ScoreTarget) return true;
    for (const auto& objective : match.objectives()->values)
        if (objective.spec.kind == MissionObjectiveKind::ReachScore) return true;
    return false;
}

bool subjectAlive(SubjectRef subject) {
    if (!subject.isValid()) return false;
    auto units = ecs::View<Unit, Unit::Identity, Unit::Durability>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [identity, durability] = *it;
        if (identity->subject == subject) return durability->alive && durability->state.health > 0.0;
    }
    auto buildings = ecs::View<Building, Building::Identity, Building::Integrity>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [identity, integrity] = *it;
        if (identity->subject == subject) return integrity->alive && integrity->state.health > 0.0;
    }
    return false;
}

bool factionOnTeam(Match& match, Faction* faction, int team) {
    if (faction == nullptr) return false;
    for (const auto& entry : match.participants()->entries) {
        if (entry.eliminated || entry.team != team) continue;
        if (entry.faction.resolve() == faction) return true;
    }
    return false;
}

enum class ObjectiveProgress : std::uint8_t { Pending, Completed, Failed };

Result<ObjectiveProgress> evaluateObjective(Match& match, const MissionObjectiveSpec& spec,
                                            const MatchResourceQuery& resources) {
    switch (spec.kind) {
        case MissionObjectiveKind::EliminateFaction: {
            const auto* target = participantByFactionSubject(match, spec.targetFaction);
            if (target == nullptr)
                return Result<ObjectiveProgress>::success(ObjectiveProgress::Completed);
            auto* faction = dynamic_cast<Faction*>(target->faction.resolve());
            if (faction == nullptr || target->eliminated || !factionHasPresence(*faction))
                return Result<ObjectiveProgress>::success(ObjectiveProgress::Completed);
            return Result<ObjectiveProgress>::success(ObjectiveProgress::Pending);
        }
        case MissionObjectiveKind::DestroyBuilding: {
            if (spec.targetSubject.isValid()) {
                return Result<ObjectiveProgress>::success(subjectAlive(spec.targetSubject) ? ObjectiveProgress::Pending
                                                                                           : ObjectiveProgress::Completed);
            }
            const auto* target = participantByFactionSubject(match, spec.targetFaction);
            if (target == nullptr)
                return Result<ObjectiveProgress>::success(ObjectiveProgress::Completed);
            auto* faction = dynamic_cast<Faction*>(target->faction.resolve());
            if (faction == nullptr)
                return Result<ObjectiveProgress>::success(ObjectiveProgress::Completed);
            auto buildings = ecs::View<Building, Building::Definition, Building::Faction, Building::Integrity,
                                       Building::Construction>();
            for (auto it = buildings.begin(); it != buildings.end(); ++it) {
                auto [definition, owner, integrity, construction] = *it;
                if (!owns(owner->link, *faction)) continue;
                if (!integrity->alive || integrity->state.health <= 0.0 || construction->progress < 1.0f) continue;
                if (definition->id.name() == spec.archetype)
                    return Result<ObjectiveProgress>::success(ObjectiveProgress::Pending);
            }
            return Result<ObjectiveProgress>::success(ObjectiveProgress::Completed);
        }
        case MissionObjectiveKind::SurviveDuration: {
            if (!teamIsLive(match, spec.beneficiaryTeam) || !teamHasPresence(match, spec.beneficiaryTeam))
                return Result<ObjectiveProgress>::success(ObjectiveProgress::Failed);
            if (match.state()->elapsed >= spec.requiredDuration)
                return Result<ObjectiveProgress>::success(ObjectiveProgress::Completed);
            return Result<ObjectiveProgress>::success(ObjectiveProgress::Pending);
        }
        case MissionObjectiveKind::ProtectSubjects: {
            for (const auto& subject : spec.requiredSubjects)
                if (!subjectAlive(subject))
                    return Result<ObjectiveProgress>::success(ObjectiveProgress::Failed);
            if (!spec.requiredDuration.isZero() && match.state()->elapsed >= spec.requiredDuration)
                return Result<ObjectiveProgress>::success(ObjectiveProgress::Completed);
            if (spec.requiredDuration.isZero())
                return Result<ObjectiveProgress>::success(ObjectiveProgress::Pending);
            return Result<ObjectiveProgress>::success(ObjectiveProgress::Pending);
        }
        case MissionObjectiveKind::OccupyBuildings: {
            if (spec.requiredSubjects.empty()) {
                auto buildings =
                    ecs::View<Building, Building::Definition, Building::Faction, Building::Integrity,
                              Building::Construction, Building::Capture>();
                for (auto it = buildings.begin(); it != buildings.end(); ++it) {
                    auto [definition, owner, integrity, construction, capture] = *it;
                    if (!spec.archetype.empty() && definition->id.name() != spec.archetype) continue;
                    if (spec.archetype.empty() &&
                        (!std::isfinite(capture->scorePerSecond) || capture->scorePerSecond <= 0.0f))
                        continue;
                    if (!integrity->alive || integrity->state.health <= 0.0 || construction->progress < 1.0f) continue;
                    if (factionOnTeam(match, dynamic_cast<Faction*>(owner->link.resolve()), spec.beneficiaryTeam))
                        return Result<ObjectiveProgress>::success(ObjectiveProgress::Completed);
                }
                return Result<ObjectiveProgress>::success(ObjectiveProgress::Pending);
            }
            for (const auto& subject : spec.requiredSubjects) {
                bool owned = false;
                auto buildings =
                    ecs::View<Building, Building::Identity, Building::Faction, Building::Integrity,
                              Building::Construction>();
                for (auto it = buildings.begin(); it != buildings.end(); ++it) {
                    auto [identity, owner, integrity, construction] = *it;
                    if (identity->subject != subject) continue;
                    if (!integrity->alive || integrity->state.health <= 0.0 || construction->progress < 1.0f)
                        return Result<ObjectiveProgress>::success(ObjectiveProgress::Pending);
                    owned = factionOnTeam(match, dynamic_cast<Faction*>(owner->link.resolve()),
                                          spec.beneficiaryTeam);
                    break;
                }
                if (!owned) return Result<ObjectiveProgress>::success(ObjectiveProgress::Pending);
            }
            return Result<ObjectiveProgress>::success(ObjectiveProgress::Completed);
        }
        case MissionObjectiveKind::ReachScore: {
            const auto scores = liveTeamScores(match);
            const auto found = scores.find(spec.beneficiaryTeam);
            const double value = found == scores.end() ? 0.0 : found->second;
            if (value >= spec.targetValue)
                return Result<ObjectiveProgress>::success(ObjectiveProgress::Completed);
            return Result<ObjectiveProgress>::success(ObjectiveProgress::Pending);
        }
        case MissionObjectiveKind::ReachResource: {
            if (!resources)
                return Result<ObjectiveProgress>::failure(Diagnostic::error(
                    DiagnosticCode::PreconditionViolation,
                    "resource mission objective requires an authoritative economy query", "resources", {},
                    "rts.match"));
            for (auto& entry : match.participants()->entries) {
                if (entry.eliminated || entry.team != spec.beneficiaryTeam) continue;
                auto* faction = dynamic_cast<Faction*>(entry.faction.resolve());
                if (faction == nullptr) continue;
                auto balance = resources(*faction, spec.resourceType);
                if (!balance) return Result<ObjectiveProgress>::failure(balance.status());
                if (balance.value() >= spec.targetValue)
                    return Result<ObjectiveProgress>::success(ObjectiveProgress::Completed);
            }
            return Result<ObjectiveProgress>::success(ObjectiveProgress::Pending);
        }
    }
    return Result<ObjectiveProgress>::success(ObjectiveProgress::Pending);
}

struct ObjectiveSettlement {
    bool changed = false;
    bool ended = false;
};

Result<ObjectiveSettlement> evaluateObjectives(Match& match, const MatchResourceQuery& resources, bool atTimeout) {
    ObjectiveSettlement settlement;
    for (auto& objective : match.objectives()->values) {
        if (objective.status != MissionObjectiveStatus::Pending) continue;
        auto progress = evaluateObjective(match, objective.spec, resources);
        if (!progress) return Result<ObjectiveSettlement>::failure(progress.status());
        if (progress.value() == ObjectiveProgress::Pending && atTimeout && objective.spec.failsMatch &&
            objective.spec.endsMatch && objective.spec.kind != MissionObjectiveKind::SurviveDuration &&
            objective.spec.kind != MissionObjectiveKind::ProtectSubjects) {
            progress = Result<ObjectiveProgress>::success(ObjectiveProgress::Failed);
        }
        if (progress.value() == ObjectiveProgress::Pending) continue;
        settlement.changed = true;
        if (progress.value() == ObjectiveProgress::Completed) {
            objective.status = MissionObjectiveStatus::Completed;
            objective.completedSequence = match.state()->updateSequence + 1;
            emit(match, "objective_completed", nullptr, objective.spec.beneficiaryTeam, objective.spec.id);
            if (objective.spec.endsMatch) {
                finishWithTeam(match, objective.spec.beneficiaryTeam, "objective:" + objective.spec.id);
                settlement.ended = true;
                return Result<ObjectiveSettlement>::success(settlement, Status::success(StatusCode::Applied));
            }
        } else {
            objective.status = MissionObjectiveStatus::Failed;
            objective.completedSequence = match.state()->updateSequence + 1;
            emit(match, "objective_failed", nullptr, objective.spec.beneficiaryTeam, objective.spec.id);
            if (objective.spec.failsMatch) {
                finishWithTeam(match, -1, "objective_failed:" + objective.spec.id);
                settlement.ended = true;
                return Result<ObjectiveSettlement>::success(settlement, Status::success(StatusCode::Applied));
            }
        }
    }
    return Result<ObjectiveSettlement>::success(
        settlement, Status::success(settlement.changed ? StatusCode::Applied : StatusCode::NoOp));
}

Result<void> validateObjective(Match& match, const MissionObjectiveSpec& objective) {
    if (objective.id.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "RTS mission objective requires a non-empty id", "objective.id",
                                                       {}, "rts.match"));
    bool knownTeam = false;
    for (const auto& entry : match.participants()->entries)
        if (entry.team == objective.beneficiaryTeam) knownTeam = true;
    if (!knownTeam)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "RTS mission objective beneficiary team is not a participant team",
            "objective.beneficiaryTeam", {}, "rts.match"));
    switch (objective.kind) {
        case MissionObjectiveKind::EliminateFaction:
            if (!objective.targetFaction.isValid() || participantByFactionSubject(match, objective.targetFaction) == nullptr)
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::NotFound, "RTS eliminate objective requires a participating target faction",
                    "objective.targetFaction", {}, "rts.match"));
            break;
        case MissionObjectiveKind::DestroyBuilding:
            if (!objective.targetSubject.isValid() &&
                (objective.archetype.empty() || !objective.targetFaction.isValid() ||
                 participantByFactionSubject(match, objective.targetFaction) == nullptr))
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument,
                    "RTS destroy-building objective requires a subject or archetype+target faction", "objective", {},
                    "rts.match"));
            break;
        case MissionObjectiveKind::SurviveDuration:
            if (objective.requiredDuration.isZero() || objective.requiredDuration.nanoseconds() < 0)
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "RTS survive objective requires a positive duration",
                    "objective.requiredDuration", {}, "rts.match"));
            break;
        case MissionObjectiveKind::ProtectSubjects:
            if (objective.requiredSubjects.empty())
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "RTS protect objective requires subjects",
                    "objective.requiredSubjects", {}, "rts.match"));
            break;
        case MissionObjectiveKind::OccupyBuildings:
            if (objective.requiredSubjects.empty() && objective.archetype.empty())
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument,
                    "RTS occupy objective requires building subjects or an archetype", "objective", {}, "rts.match"));
            break;
        case MissionObjectiveKind::ReachScore:
            if (!(objective.targetValue > 0.0) || !std::isfinite(objective.targetValue))
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "RTS reach-score objective requires a positive target",
                    "objective.targetValue", {}, "rts.match"));
            break;
        case MissionObjectiveKind::ReachResource:
            if (objective.resourceType.empty() || !(objective.targetValue > 0.0) ||
                !std::isfinite(objective.targetValue))
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument,
                    "RTS reach-resource objective requires a resource and positive target", "objective", {},
                    "rts.match"));
            break;
    }
    return Result<void>::success(Status::success(StatusCode::Applied));
}

}  // namespace

Result<void> MatchSystem::addParticipant(Match& match, Faction& faction, int team) {
    if (match.state()->phase != MatchPhase::Setup)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "cannot add an RTS participant after match start", "match", {}, "rts.match"));
    if (participant(match, faction) != nullptr)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, "RTS faction is already a participant",
                                                       "faction", {}, "rts.match"));
    auto link = FactionLink::bind(ecs::handle_of(&faction));
    if (!link) return Result<void>::failure(link.status());
    match.participants()->entries.push_back({std::move(link).takeValue(), team, false, false, {}});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MatchSystem::addObjective(Match& match, MissionObjectiveSpec objective) {
    if (match.state()->phase != MatchPhase::Setup)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "cannot add an RTS mission objective after match start", "match.phase", {},
            "rts.match"));
    auto validated = validateObjective(match, objective);
    if (!validated) return validated;
    const auto duplicate = std::find_if(match.objectives()->values.begin(), match.objectives()->values.end(),
                                        [&](const auto& value) { return value.spec.id == objective.id; });
    if (duplicate != match.objectives()->values.end())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                       "RTS mission objective id already exists", "objective.id", {},
                                                       "rts.match"));
    std::sort(objective.requiredSubjects.begin(), objective.requiredSubjects.end(),
              [](const SubjectRef& left, const SubjectRef& right) { return left.format() < right.format(); });
    objective.requiredSubjects.erase(
        std::unique(objective.requiredSubjects.begin(), objective.requiredSubjects.end(),
                    [](const SubjectRef& left, const SubjectRef& right) { return left == right; }),
        objective.requiredSubjects.end());
    match.objectives()->values.push_back({std::move(objective), MissionObjectiveStatus::Pending, 0});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MatchSystem::start(Match& match) {
    if (match.state()->phase != MatchPhase::Setup)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "RTS match is not in setup", "match.phase", {}, "rts.match"));
    if (match.participants()->entries.size() < 2)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "RTS match requires at least two factions", "participants", {},
                                                       "rts.match"));
    for (const auto& entry : match.participants()->entries)
        if (entry.faction.resolve() == nullptr)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::StaleHandle, "RTS match contains a stale faction", "participants", {}, "rts.match"));
    const auto& rules = *match.rules();
    if (rules.rule == VictoryRule::DestroyHeadquarters && rules.archetype.empty())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "headquarters victory requires an archetype", "rules", {}, "rts.match"));
    if (rules.rule == VictoryRule::ResourceTarget &&
        (rules.archetype.empty() || rules.targetValue <= 0.0 || !std::isfinite(rules.targetValue)))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "resource victory requires a positive target", "rules", {}, "rts.match"));
    if (rules.rule == VictoryRule::ScoreTarget && (rules.targetValue <= 0.0 || !std::isfinite(rules.targetValue)))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "score victory requires a positive target", "rules", {}, "rts.match"));
    if (rules.rule == VictoryRule::Mission) {
        const bool hasTerminal = std::any_of(match.objectives()->values.begin(), match.objectives()->values.end(),
                                             [](const auto& value) { return value.spec.endsMatch || value.spec.failsMatch; });
        if (!hasTerminal)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::PreconditionViolation,
                "RTS mission victory requires at least one terminal objective", "objectives", {}, "rts.match"));
    }
    if (rules.timeLimit.nanoseconds() < 0)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS match time limit cannot be negative", "rules.timeLimit", {},
            "rts.match"));
    if (rules.suddenDeath && rules.timeLimit.isZero())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS sudden death requires a positive time limit", "rules.suddenDeath",
            {}, "rts.match"));
    if (rules.suddenDeath && (rules.rule == VictoryRule::ScoreTarget || rules.rule == VictoryRule::ResourceTarget ||
                              rules.rule == VictoryRule::Mission))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "RTS sudden death applies only to annihilation or headquarters victory", "rules.suddenDeath", {},
            "rts.match"));
    for (const auto& objective : match.objectives()->values) {
        auto validated = validateObjective(match, objective.spec);
        if (!validated) return validated;
    }
    match.state()->phase = MatchPhase::Running;
    match.state()->winningTeam = -1;
    match.state()->elapsed = Duration::zero();
    match.state()->suddenDeathActive = false;
    emit(match, "match_started", nullptr, -1);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MatchSystem::surrender(Match& match, Faction& faction) {
    if (match.state()->phase != MatchPhase::Running)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "RTS match is not running", "match.phase", {}, "rts.match"));
    auto* entry = participant(match, faction);
    if (entry == nullptr || entry->eliminated)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "RTS faction is not an active participant", "faction", {}, "rts.match"));
    destroyFactionEntities(faction);
    eliminate(match, *entry, "surrender", true);
    settleTeams(match);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MatchSystem::creditScore(Match& match, int team, double amount) {
    if (match.state()->phase != MatchPhase::Running)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "RTS match is not running", "match.phase", {}, "rts.match"));
    bool knownTeam = false;
    for (const auto& entry : match.participants()->entries) {
        if (!entry.eliminated && entry.team == team) {
            knownTeam = true;
            break;
        }
    }
    if (!knownTeam)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "RTS match score team is not an active participant team", "team", {},
            "rts.match"));
    return addScore(match, team, amount);
}

Result<double> MatchSystem::teamScore(Match& match, int team) noexcept {
    const auto* entry = scoreEntry(match, team);
    return Result<double>::success(entry == nullptr ? 0.0 : entry->value, Status::success(StatusCode::Applied));
}

Result<std::size_t> MatchSystem::step(Match& match, const MatchResourceQuery& resources) {
    return step(match, SimulationStep{}, resources);
}

Result<std::size_t> MatchSystem::step(Match& match, const SimulationStep& step, const MatchResourceQuery& resources) {
    if (match.state()->phase != MatchPhase::Running)
        return Result<std::size_t>::success(0, Status::success(StatusCode::NoOp));

    std::size_t changed = 0;
    if (!step.delta.isZero()) {
        auto nextElapsed = match.state()->elapsed.tryAdd(step.delta);
        if (!nextElapsed) return Result<std::size_t>::failure(nextElapsed.status());
        match.state()->elapsed = std::move(nextElapsed).takeValue();
        ++changed;
    }

    if (needsScoreAccrual(match) && !match.state()->suddenDeathActive) {
        auto accrued = accrueControlPoints(match, step);
        if (!accrued) return Result<std::size_t>::failure(accrued.status());
        changed += accrued.value();
    }

    auto objectives = evaluateObjectives(match, resources, false);
    if (!objectives) return Result<std::size_t>::failure(objectives.status());
    if (objectives.value().changed) ++changed;
    if (objectives.value().ended)
        return Result<std::size_t>::success(changed, Status::success(StatusCode::Applied));

    const bool timedOut = !match.rules()->timeLimit.isZero() &&
                          match.state()->elapsed >= match.rules()->timeLimit && !match.state()->suddenDeathActive;
    if (timedOut) {
        if (match.rules()->suddenDeath) {
            match.state()->suddenDeathActive = true;
            emit(match, "sudden_death", nullptr, -1, "time_expired");
            ++changed;
        } else if (match.rules()->rule == VictoryRule::Mission || !match.objectives()->values.empty()) {
            auto atTimeout = evaluateObjectives(match, resources, true);
            if (!atTimeout) return Result<std::size_t>::failure(atTimeout.status());
            if (atTimeout.value().ended)
                return Result<std::size_t>::success(changed + 1, Status::success(StatusCode::Applied));
            if (match.rules()->rule == VictoryRule::Mission) {
                finishWithTeam(match, -1, "time_expired");
                return Result<std::size_t>::success(changed + 1, Status::success(StatusCode::Applied));
            }
        } else if (match.rules()->rule == VictoryRule::ScoreTarget) {
            settleHighestScore(match, liveTeamScores(match), "time_expired");
            return Result<std::size_t>::success(changed + 1, Status::success(StatusCode::Applied));
        } else if (match.rules()->rule == VictoryRule::ResourceTarget) {
            auto settled = settleTimedResource(match, resources);
            if (!settled) return Result<std::size_t>::failure(settled.status());
            return Result<std::size_t>::success(changed + 1, Status::success(StatusCode::Applied));
        } else {
            std::set<int> teams;
            for (const auto& entry : match.participants()->entries)
                if (!entry.eliminated && entry.faction.resolve() != nullptr) teams.insert(entry.team);
            finishWithTeam(match, teams.size() == 1 ? *teams.begin() : -1, "time_expired");
            return Result<std::size_t>::success(changed + 1, Status::success(StatusCode::Applied));
        }
    }

    if (match.rules()->rule == VictoryRule::Mission)
        return Result<std::size_t>::success(changed,
                                            Status::success(changed == 0 ? StatusCode::NoOp : StatusCode::Applied));

    if (match.rules()->rule == VictoryRule::ScoreTarget) {
        const auto scores = liveTeamScores(match);
        for (const auto& [team, value] : scores) {
            if (value >= match.rules()->targetValue) {
                finishWithTeam(match, team, "score_target");
                return Result<std::size_t>::success(changed + 1, Status::success(StatusCode::Applied));
            }
        }
        return Result<std::size_t>::success(changed,
                                            Status::success(changed == 0 ? StatusCode::NoOp : StatusCode::Applied));
    }

    std::size_t eliminated = 0;
    if (match.rules()->rule == VictoryRule::ResourceTarget) {
        if (!resources)
            return Result<std::size_t>::failure(Diagnostic::error(
                DiagnosticCode::PreconditionViolation, "resource victory requires an authoritative economy query",
                "resources", {}, "rts.match"));
        for (auto& entry : match.participants()->entries) {
            if (entry.eliminated) continue;
            auto* faction = dynamic_cast<Faction*>(entry.faction.resolve());
            if (faction == nullptr) {
                eliminate(match, entry, "stale_faction");
                ++eliminated;
                continue;
            }
            auto balance = resources(*faction, match.rules()->archetype);
            if (!balance) return Result<std::size_t>::failure(balance.status());
            if (balance.value() >= match.rules()->targetValue) {
                match.state()->phase = MatchPhase::Finished;
                match.state()->winningTeam = entry.team;
                emit(match, "match_won", faction, entry.team, "resource_target");
                return Result<std::size_t>::success(changed + eliminated + 1, Status::success(StatusCode::Applied));
            }
        }
    } else {
        for (auto& entry : match.participants()->entries) {
            if (entry.eliminated) continue;
            auto* faction = dynamic_cast<Faction*>(entry.faction.resolve());
            if (faction == nullptr || !survives(*faction, *match.rules())) {
                eliminate(match, entry, faction == nullptr ? "stale_faction" :
                          match.rules()->rule == VictoryRule::DestroyHeadquarters ? "headquarters_destroyed" :
                                                                                   "annihilated");
                ++eliminated;
            }
        }
    }
    settleTeams(match);
    const std::size_t total = changed + eliminated;
    return Result<std::size_t>::success(total, Status::success(total == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace eve::rts
