#include "rts/RTSMatch.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <utility>

namespace eve::rts {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {}, "rts.match"));
}

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

void settleTeams(Match& match) {
    std::set<int> teams;
    for (auto& entry : match.participants()->entries)
        if (!entry.eliminated && entry.faction.resolve() != nullptr) teams.insert(entry.team);
    if (teams.size() > 1) return;
    match.state()->phase = MatchPhase::Finished;
    match.state()->winningTeam = teams.empty() ? -1 : *teams.begin();
    emit(match, teams.empty() ? "match_draw" : "match_won", nullptr, match.state()->winningTeam,
         teams.empty() ? "no_survivors" : "last_team_standing");
}

}  // namespace

Result<void> MatchSystem::addParticipant(Match& match, Faction& faction, int team) {
    if (match.state()->phase != MatchPhase::Setup)
        return failure<void>(DiagnosticCode::Conflict, "cannot add an RTS participant after match start", "match");
    if (participant(match, faction) != nullptr)
        return failure<void>(DiagnosticCode::Conflict, "RTS faction is already a participant", "faction");
    auto link = FactionLink::bind(ecs::handle_of(&faction));
    if (!link) return Result<void>::failure(link.status());
    match.participants()->entries.push_back({std::move(link).takeValue(), team, false, false, {}});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MatchSystem::start(Match& match) {
    if (match.state()->phase != MatchPhase::Setup)
        return failure<void>(DiagnosticCode::Conflict, "RTS match is not in setup", "match.phase");
    if (match.participants()->entries.size() < 2)
        return failure<void>(DiagnosticCode::PreconditionViolation, "RTS match requires at least two factions",
                             "participants");
    for (const auto& entry : match.participants()->entries)
        if (entry.faction.resolve() == nullptr)
            return failure<void>(DiagnosticCode::StaleHandle, "RTS match contains a stale faction", "participants");
    const auto& rules = *match.rules();
    if (rules.rule == VictoryRule::DestroyHeadquarters && rules.archetype.empty())
        return failure<void>(DiagnosticCode::InvalidArgument, "headquarters victory requires an archetype", "rules");
    if (rules.rule == VictoryRule::ResourceTarget &&
        (rules.archetype.empty() || rules.targetValue <= 0.0 || !std::isfinite(rules.targetValue)))
        return failure<void>(DiagnosticCode::InvalidArgument, "resource victory requires a positive target", "rules");
    match.state()->phase = MatchPhase::Running;
    match.state()->winningTeam = -1;
    emit(match, "match_started", nullptr, -1);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> MatchSystem::surrender(Match& match, Faction& faction) {
    if (match.state()->phase != MatchPhase::Running)
        return failure<void>(DiagnosticCode::Conflict, "RTS match is not running", "match.phase");
    auto* entry = participant(match, faction);
    if (entry == nullptr || entry->eliminated)
        return failure<void>(DiagnosticCode::NotFound, "RTS faction is not an active participant", "faction");
    destroyFactionEntities(faction);
    eliminate(match, *entry, "surrender", true);
    settleTeams(match);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::size_t> MatchSystem::step(Match& match, const MatchResourceQuery& resources) {
    if (match.state()->phase != MatchPhase::Running)
        return Result<std::size_t>::success(0, Status::success(StatusCode::NoOp));
    std::size_t eliminated = 0;
    if (match.rules()->rule == VictoryRule::ResourceTarget) {
        if (!resources)
            return failure<std::size_t>(DiagnosticCode::PreconditionViolation,
                                        "resource victory requires an authoritative economy query", "resources");
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
                return Result<std::size_t>::success(eliminated + 1, Status::success(StatusCode::Applied));
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
    return Result<std::size_t>::success(eliminated,
        Status::success(eliminated == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace eve::rts
