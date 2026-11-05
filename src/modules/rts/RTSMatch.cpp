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

void eliminate(Match& match, Match::Participants::Entry& entry, std::string reason, bool surrendered = false) {
    if (entry.eliminated) return;
    entry.eliminated = true;
    entry.surrendered = surrendered;
    entry.reason = reason;
    auto* faction = dynamic_cast<Faction*>(entry.faction.resolve());
    emit(match, "faction_eliminated", faction, entry.team, std::move(reason));
}

bool owns(const FactionLink& link, Faction& faction) { return link.resolve() == &faction; }

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
    if (rules.timeLimit.nanoseconds() < 0)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS match time limit cannot be negative", "rules.timeLimit", {},
            "rts.match"));
    if (rules.suddenDeath && rules.timeLimit.isZero())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "RTS sudden death requires a positive time limit", "rules.suddenDeath",
            {}, "rts.match"));
    if (rules.suddenDeath &&
        (rules.rule == VictoryRule::ScoreTarget || rules.rule == VictoryRule::ResourceTarget))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "RTS sudden death applies only to annihilation or headquarters victory", "rules.suddenDeath", {},
            "rts.match"));
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

    if (match.rules()->rule == VictoryRule::ScoreTarget && !match.state()->suddenDeathActive) {
        auto accrued = accrueControlPoints(match, step);
        if (!accrued) return Result<std::size_t>::failure(accrued.status());
        changed += accrued.value();
    }

    const bool timedOut = !match.rules()->timeLimit.isZero() &&
                          match.state()->elapsed >= match.rules()->timeLimit && !match.state()->suddenDeathActive;
    if (timedOut) {
        if (match.rules()->suddenDeath) {
            match.state()->suddenDeathActive = true;
            emit(match, "sudden_death", nullptr, -1, "time_expired");
            ++changed;
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
