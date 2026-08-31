#include "rts/RTSSystems.h"
#include "crowd/Crowd.h"
#include "sensing/Sensing.h"
#include "weapon/WeaponSystem.h"
#include "map/Pathfinder.h"
#include "map/Path.h"
#include "map/Fov.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace eve::rts {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

template <typename T>
Result<T> failureFrom(const Status& status) {
    return Result<T>::failure(status);
}

bool finitePosition(WorldPosition position) { return std::isfinite(position.x) && std::isfinite(position.y); }

SubjectRef stableSubject(ecs::Entity* entity) {
    if (auto* unit = dynamic_cast<Unit*>(entity)) return unit->identity()->subject;
    if (auto* building = dynamic_cast<Building*>(entity)) return building->identity()->subject;
    if (auto* faction = dynamic_cast<Faction*>(entity)) return faction->identity()->subject;
    return {};
}

float distanceSquared(float ax, float ay, float bx, float by) {
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

float weaponRangeDamageFactor(const weapon::WeaponDefinition& definition, float distance) {
    if (definition.minimumDamageFactor >= 1.0f || distance <= definition.falloffStart ||
        definition.range <= definition.falloffStart) return 1.0f;
    const float progress = std::clamp((distance - definition.falloffStart) /
                                          (definition.range - definition.falloffStart),
                                      0.0f, 1.0f);
    return 1.0f + (definition.minimumDamageFactor - 1.0f) * progress;
}

float weaponTargetPreference(const weapon::WeaponDefinition& definition, const TagSet* tags) {
    if (tags == nullptr) return 1.0f;
    return std::any_of(definition.preferredTargetTags.begin(), definition.preferredTargetTags.end(),
                       [&](const auto& tag) { return tags->contains(tag); })
               ? definition.preferredTargetBonus : 1.0f;
}

float deterministicShotRandom(SubjectRef subject, std::uint64_t sequence, std::uint64_t stream) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char character : subject.format()) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    hash ^= sequence + 0x9e3779b97f4a7c15ULL + (stream << 6U) + (stream >> 2U);
    hash ^= hash >> 30U; hash *= 0xbf58476d1ce4e5b9ULL;
    hash ^= hash >> 27U; hash *= 0x94d049bb133111ebULL;
    hash ^= hash >> 31U;
    return static_cast<float>((hash >> 40U) & 0xffffffU) / 16777216.0f;
}

struct ShotPlacement {
    bool missed = false;
    WorldPosition point;
};

ShotPlacement placeShot(SubjectRef subject, std::uint64_t sequence,
                        const weapon::WeaponDefinition& definition, WorldPosition intended) {
    ShotPlacement result{deterministicShotRandom(subject, sequence, 0) >= definition.accuracy, intended};
    if (!result.missed || definition.scatterRadius <= 0.0f) return result;
    const float angle = deterministicShotRandom(subject, sequence, 1) * 2.0f *
                        static_cast<float>(std::numbers::pi);
    const float radius = std::sqrt(deterministicShotRandom(subject, sequence, 2)) * definition.scatterRadius;
    result.point.x += std::cos(angle) * radius;
    result.point.y += std::sin(angle) * radius;
    return result;
}

bool movementOrder(OrderKind kind) {
    switch (kind) {
        case OrderKind::Move:
        case OrderKind::Attack:
        case OrderKind::AttackMove:
        case OrderKind::Gather:
        case OrderKind::ReturnCargo:
        case OrderKind::Patrol:
        case OrderKind::Repair:
        case OrderKind::Garrison:
        case OrderKind::BoardTransport:
        case OrderKind::Capture:
        case OrderKind::Resupply:
        case OrderKind::Escort:
        case OrderKind::SupplyRelay: return true;
        default: return false;
    }
}

bool sameHandle(const ecs::EntityHandle& left, const ecs::EntityHandle& right) {
    return left.table == right.table && left.type == right.type && left.id == right.id &&
           left.generation == right.generation;
}

std::optional<WorldPosition> entityPosition(const ecs::EntityHandle& handle) {
    if (auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle)))
        return WorldPosition{unit->motion()->x, unit->motion()->y};
    if (auto* building = dynamic_cast<Building*>(ecs::try_get(handle)))
        return WorldPosition{building->placement()->worldX, building->placement()->worldY};
    return std::nullopt;
}

std::string factionKey(const FactionLink& link) {
    auto* faction = dynamic_cast<Faction*>(link.resolve());
    return faction != nullptr && faction->identity()->subject.isValid() ? faction->identity()->subject.format()
                                                                       : std::string{};
}

Unit* unitBySubject(SubjectRef subject) {
    if (!subject.isValid()) return nullptr;
    auto units = ecs::View<Unit, Unit::Identity>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [identity] = *it;
        if (identity->subject == subject)
            return dynamic_cast<Unit*>(ecs::try_get(identity->self));
    }
    return nullptr;
}

bool hostileTo(Unit& source, const FactionLink& targetFaction) {
    return source.faction()->link.resolve() != nullptr && targetFaction.resolve() != nullptr &&
           !FactionRelationSystem::allied(source.faction()->link, targetFaction);
}

float visibleHostileThreatAt(Faction& viewer, WorldPosition point) {
    const auto visible = [&](SubjectRef subject) {
        if (!viewer.intel()->enabled) return true;
        const auto found = std::find_if(viewer.intel()->contacts.begin(), viewer.intel()->contacts.end(),
                                        [&](const auto& contact) { return contact.subject == subject; });
        return found != viewer.intel()->contacts.end() && found->visible && found->detected;
    };
    const auto contribution = [&](const weapon::WeaponDefinition& definition, WorldPosition source) {
        if (definition.range <= 0.0f) return 0.0f;
        const float distance = std::hypot(source.x - point.x, source.y - point.y);
        if (distance > definition.range) return 0.0f;
        return std::max(0.0f, definition.damage) / std::max(0.05f, definition.cooldown) *
               (0.1f + 1.0f - distance / definition.range);
    };
    float threat = 0.0f;
    auto units = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Faction, Unit::Weapon,
                           Unit::Durability, Unit::Containment>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [identity, motion, faction, weaponLink, durability, containment] = *it;
        if (!durability->alive || containment->container.isBound() ||
            FactionRelationSystem::allied(dynamic_cast<Faction*>(faction->link.resolve()), &viewer) ||
            !visible(identity->subject)) continue;
        auto* weaponEntity = dynamic_cast<weapon::WeaponEntity*>(weaponLink->link.resolve());
        const auto* definition = weaponEntity == nullptr ? nullptr : weaponEntity->definition()->def;
        if (definition != nullptr) threat += contribution(*definition, {motion->x, motion->y});
    }
    auto buildings = ecs::View<Building, Building::Identity, Building::Placement, Building::Faction,
                               Building::Weapon, Building::Integrity, Building::Construction>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [identity, placement, faction, weaponLink, integrity, construction] = *it;
        if (!integrity->alive || construction->progress < 1.0f ||
            FactionRelationSystem::allied(dynamic_cast<Faction*>(faction->link.resolve()), &viewer) ||
            !visible(identity->subject)) continue;
        auto* weaponEntity = dynamic_cast<weapon::WeaponEntity*>(weaponLink->link.resolve());
        const auto* definition = weaponEntity == nullptr ? nullptr : weaponEntity->definition()->def;
        if (definition != nullptr)
            threat += contribution(*definition, {placement->worldX, placement->worldY});
    }
    return threat;
}

Result<std::optional<OrderRecord>> readCurrent(OrderComponent& orders) {
    auto current = orders.current();
    if (current) return Result<std::optional<OrderRecord>>::success(std::move(current).takeValue());
    if (current.code() == StatusCode::NotFound) {
        current.ignore("RTS entity has no active order");
        return Result<std::optional<OrderRecord>>::success(std::nullopt, Status::success(StatusCode::NoOp));
    }
    return failureFrom<std::optional<OrderRecord>>(current.status());
}

Result<std::size_t> advanceEffects(RTSEffectComponent& effects, const SimulationStep& step) {
    auto advanced = effects.advance(step);
    if (!advanced) return failureFrom<std::size_t>(advanced.status());
    const auto result = std::move(advanced).takeValue();
    return Result<std::size_t>::success(result.settled,
                                        Status::success(result.settled == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace

Result<int> VeterancySystem::award(Unit& unit, float experience) {
    auto veterancy = unit.veterancy();
    auto durability = unit.durability();
    auto combatPolicy = unit.combat();
    if (!durability->alive || !std::isfinite(experience) || experience <= 0.0f)
        return failure<int>(DiagnosticCode::InvalidArgument,
                            "RTS veterancy award requires a live unit and positive finite experience",
                            "experience");
    if (veterancy->veteranThreshold <= 0.0f)
        return Result<int>::success(0, Status::success(StatusCode::NoOp));
    if (!std::isfinite(veterancy->experience) || veterancy->experience < 0.0f ||
        !std::isfinite(veterancy->veteranThreshold) || !std::isfinite(veterancy->eliteThreshold) ||
        veterancy->eliteThreshold <= veterancy->veteranThreshold ||
        !std::isfinite(veterancy->veteranDamageFactor) ||
        !std::isfinite(veterancy->eliteDamageFactor) ||
        !std::isfinite(veterancy->veteranHealthFactor) ||
        !std::isfinite(veterancy->eliteHealthFactor) ||
        veterancy->veteranDamageFactor < 1.0f ||
        veterancy->eliteDamageFactor < veterancy->veteranDamageFactor ||
        veterancy->veteranHealthFactor < 1.0f ||
        veterancy->eliteHealthFactor < veterancy->veteranHealthFactor ||
        veterancy->level < 0 || veterancy->level > 2)
        return failure<int>(DiagnosticCode::InvalidArgument,
                            "RTS veterancy thresholds and factors are inconsistent", "unit.veterancy");
    veterancy->experience += experience;
    const int oldLevel = veterancy->level;
    if (veterancy->eliteThreshold > 0.0f && veterancy->experience >= veterancy->eliteThreshold)
        veterancy->level = 2;
    else if (veterancy->experience >= veterancy->veteranThreshold)
        veterancy->level = 1;
    if (veterancy->level == oldLevel)
        return Result<int>::success(0, Status::success(StatusCode::Applied));
    const auto damageAt = [&](int level) {
        return level >= 2 ? veterancy->eliteDamageFactor
                          : level >= 1 ? veterancy->veteranDamageFactor : 1.0f;
    };
    const auto healthAt = [&](int level) {
        return level >= 2 ? veterancy->eliteHealthFactor
                          : level >= 1 ? veterancy->veteranHealthFactor : 1.0f;
    };
    combatPolicy->upgradeDamageFactor *= damageAt(veterancy->level) / damageAt(oldLevel);
    const double healthRatio = static_cast<double>(healthAt(veterancy->level) / healthAt(oldLevel));
    durability->state.maxHealth *= healthRatio;
    durability->state.health = std::min(durability->state.maxHealth,
                                        durability->state.health * healthRatio);
    return Result<int>::success(veterancy->level - oldLevel, Status::success(StatusCode::Applied));
}

bool FactionRelationSystem::allied(Faction* left, Faction* right) noexcept {
    if (left == nullptr || right == nullptr) return false;
    if (left == right) return true;
    auto matches = ecs::View<Match, Match::Participants>();
    for (auto it = matches.begin(); it != matches.end(); ++it) {
        auto [participants] = *it;
        const Match::Participants::Entry* leftEntry = nullptr;
        const Match::Participants::Entry* rightEntry = nullptr;
        for (const auto& entry : participants->entries) {
            if (entry.eliminated) continue;
            if (entry.faction.resolve() == left) leftEntry = &entry;
            if (entry.faction.resolve() == right) rightEntry = &entry;
        }
        if (leftEntry != nullptr && rightEntry != nullptr)
            return leftEntry->team == rightEntry->team;
    }
    return false;
}

bool FactionRelationSystem::allied(const FactionLink& left, const FactionLink& right) noexcept {
    return allied(dynamic_cast<Faction*>(left.resolve()), dynamic_cast<Faction*>(right.resolve()));
}

bool FactionIntelSystem::targetable(Faction* viewer, SubjectRef subject) noexcept {
    if (viewer == nullptr || !subject.isValid()) return false;
    const auto intel = viewer->intel();
    if (!intel->enabled) return true;
    const auto found = std::find_if(intel->contacts.begin(), intel->contacts.end(),
                                    [&](const auto& contact) { return contact.subject == subject; });
    return found != intel->contacts.end() && found->subject == subject &&
           found->visible && found->detected;
}

std::span<const SystemContract> systemContracts() noexcept {
    static constexpr SystemContract contracts[] = {
        {"rts.command_fan_out", "ecs::View<Unit, Unit::Identity, Unit::Orders>",
         "Unit::Identity; selection handles; formation input", "Unit::Orders",
         "none; use ecs::ScopedDefer if future code creates/removes entities/components",
         "none; caller owns command receipt/event publication", "input.command"},
        {"rts.motion", "ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders>", "Unit::Identity; Unit::Orders",
         "Unit::Motion", "none", "none", "simulation.movement"},
        {"rts.movement_order", "arrived Units with finite movement orders",
         "Unit::Motion; Unit::Navigation; Unit::Orders", "canonical order queue", "none", "none",
         "simulation.movement"},
        {"rts.command_state", "Units with Stop, HoldPosition, or AttackMove orders",
         "Unit::Orders; Unit::Motion", "Unit::Combat; Unit::Navigation; canonical order queue", "none",
         "none", "simulation.command"},
        {"rts.navigation", "moving Units with canonical map routes", "Unit::Orders; Unit::Motion; map Pathfinder",
         "Unit::Navigation", "none", "unreachable route notification", "simulation.navigation"},
        {"rts.patrol", "Units with active Patrol orders", "Unit::Motion; Unit::Orders",
         "Unit::Navigation patrol direction", "none", "none", "simulation.navigation"},
        {"rts.traffic_reservation", "moving Units approaching narrow canonical map cells",
         "Unit::Identity; Unit::Motion; Unit::Navigation; map Pathfinder", "Unit::Navigation::trafficWaiting",
         "none", "none", "simulation.navigation"},
        {"rts.fog", "vision-enabled Units/Buildings and Factions", "positions; factions; map FOV explored state",
         "map FOV revealers; Faction::Intel", "none", "none", "simulation.visibility"},
        {"rts.crowd_motion", "ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders, Unit::Crowd>",
         "Unit::Orders; Unit::Crowd; canonical crowd state", "Unit::Motion; canonical crowd agents", "none",
         "delegated to crowd::Crowd", "simulation.movement"},
        {"rts.worker_assignment", "Unit workers; ResourceNode stock/capacity", "positions; orders; stock; links",
         "Unit::Worker; Unit::Orders; ResourceNode::Harvest", "none", "none", "simulation.assignment"},
        {"rts.mining", "Unit workers; ResourceNode stock; Building dropoffs", "active orders; positions; stock",
         "Unit::Worker; Unit::Orders; ResourceNode::Stock", "none", "delegated resource credit receipt",
         "simulation.economy"},
        {"rts.construction", "Building construction; Unit builders", "orders; faction; motion; build rates",
         "Building::Construction; Unit::Orders", "none", "none", "simulation.construction"},
        {"rts.repair", "Building integrity; Unit repairers", "orders; faction; integrity; repair rates",
         "Building::Integrity; Unit::Orders", "none", "delegated resource debit receipt", "simulation.repair"},
        {"rts.capture", "capturable Buildings; capturing Units", "orders; faction; capture rates",
         "Building::Capture; Building::Faction; Unit::Orders", "none", "none", "simulation.capture"},
        {"rts.infrastructure", "live completed Buildings grouped by Faction",
         "Building::Faction; Construction; Integrity; Infrastructure",
         "Building::Infrastructure powered/income progress", "none", "delegated economy credit receipt",
         "simulation.economy"},
        {"rts.containment", "Units, transports, and garrison Buildings", "orders; faction; capacity; live handles",
         "Unit::Containment; transport/building occupants; Unit::Motion; Building::Capture", "none", "none",
         "simulation.containment"},
        {"rts.supply", "supplier Units/Buildings and armed recipient Units",
         "orders; factions; range; stock; canonical WeaponEntity resources",
         "Unit/Building::Supply; Unit::Orders; canonical weapon ammo", "none", "none", "simulation.logistics"},
        {"rts.supply_convoy", "Units sharing a live supply target",
         "canonical supply orders; positions; stable subjects", "Unit::Supply convoy projection",
         "none", "none", "simulation.movement"},
        {"rts.morale", "live uncontained Units", "positions; factions; suppression; morale auras",
         "Unit::Morale", "none", "none", "simulation.morale"},
        {"rts.shield", "live Units and Buildings with shields", "shield capacity, rate, delay and cooldown",
         "Unit/Building::Shield", "none", "none", "simulation.defense"},
        {"rts.command_network", "live Units and completed powered Buildings", "positions; factions; command policy",
         "Unit/Building::Command", "none", "none", "simulation.command"},
        {"rts.ability", "casting Units and live combat targets", "ability policy; factions; canonical effect state",
         "Unit::Abilities; target durability/shield/effects", "none", "delegated damage/economy receipts",
         "simulation.ability"},
        {"rts.projectile", "canonical pooled weapon projectiles and RTS combat targets",
         "weapon projectile trajectories; target positions/factions", "target durability/shield", "none",
         "delegated canonical damage outcomes", "simulation.projectile"},
        {"rts.artillery", "live uncontained indirect-fire Units", "motion; deployment policy",
         "Unit::Artillery", "none", "none", "simulation.artillery"},
        {"rts.fire_support", "friendly indirect-fire responders and exposed hostile artillery",
         "orders; factions; weapon ranges; last-fire positions", "Unit::Orders; Unit::Artillery", "none",
         "none", "simulation.fire_support"},
        {"rts.tactics", "escort and combat-group Units; live hostile candidates",
         "orders; factions; positions; weapon definitions; durability",
         "Unit::Tactics; Unit::Combat", "none", "none", "simulation.tactics"},
        {"rts.ai", "enabled Factions; friendly producers and Units; hostile Buildings",
         "Faction::Strategy; definitions; factions; orders; positions",
         "Faction::Strategy; Unit::Orders; Unit::Tactics", "none",
         "delegated production request; command fan-out receipt", "simulation.ai"},
        {"rts.combat_fire", "armed Units; live Unit/Building targets", "sensing facts; factions; orders; weapons",
         "Unit::Combat; Unit/Building durability; weapon runtime state", "none",
         "delegated weapon events, projectile spawn, and combat damage outcome", "simulation.combat"},
        {"rts.order_action", "ecs::View<Unit, Unit::Identity, Unit::Orders, Unit::Action>",
         "Unit::Identity; Unit::Action; active OrderRecord", "Unit::Orders; generic order lifecycle state", "none",
         "delegated to IRTSActionExecutor/ActionRuntime", "simulation.action"},
        {"rts.reinforcement_production_policy", "live production Buildings sharing a faction combat group",
         "Building::Rally; canonical production::WorkQueue tasks; live grouped Units",
         "Building::Rally policy bookkeeping; canonical task pause/resume/cancel state", "none",
         "delegated transactional enqueue and atomic cancel/refund receipts", "simulation.production"},
        {"rts.building_production", "ecs::View<Building, Building::Identity, Building::Production>",
         "Building::Identity; SimulationStep", "Building::Production", "none", "delegated to production::WorkQueue",
         "simulation.production"},
        {"rts.technology", "Factions, completed research tasks, and faction-owned entities",
         "canonical Definitions; canonical Production tasks; entity definitions",
         "Faction/Unit/Building::Technology; RTS-owned combat, motion, worker and durability projections",
         "none", "none", "simulation.technology"},
        {"rts.match", "Match participants and their faction-owned Units/Buildings",
         "match rules; typed faction links; durability; canonical economy query",
         "Match participants/state/events; surrender durability", "none", "match lifecycle events",
         "simulation.match"},
        {"rts.effects.unit", "ecs::View<Unit, Unit::Identity, Unit::Effects>", "Unit::Identity; SimulationStep",
         "Unit::Effects", "none", "delegated to effects::EffectContainer", "simulation.effects"},
        {"rts.effects.building", "ecs::View<Building, Building::Identity, Building::Effects>",
         "Building::Identity; SimulationStep", "Building::Effects", "none", "delegated to effects::EffectContainer",
         "simulation.effects"},
    };
    return {contracts, sizeof(contracts) / sizeof(contracts[0])};
}

Result<void> FormationSpec::validate() const {
    if (!std::isfinite(spacing) || spacing <= 0.0f)
        return failure<void>(DiagnosticCode::InvalidArgument, "formation spacing must be finite and positive",
                             "spacing");
    if (columns < 0)
        return failure<void>(DiagnosticCode::InvalidArgument, "formation columns must be non-negative", "columns");
    switch (kind) {
        case FormationKind::Line:
        case FormationKind::Grid:
        case FormationKind::Wedge: return Result<void>::success(Status::success(StatusCode::Applied));
    }
    return failure<void>(DiagnosticCode::InvalidArgument, "formation kind is invalid", "kind");
}

Result<std::vector<WorldPosition>> FormationPlanner::plan(std::size_t count, WorldPosition anchor,
                                                          const FormationSpec& spec) {
    auto valid = spec.validate();
    if (!valid) return failureFrom<std::vector<WorldPosition>>(valid.status());
    if (!finitePosition(anchor))
        return failure<std::vector<WorldPosition>>(DiagnosticCode::InvalidArgument, "formation anchor must be finite",
                                                   "anchor");

    std::vector<WorldPosition> result;
    result.reserve(count);
    if (count == 0)
        return Result<std::vector<WorldPosition>>::success(std::move(result), Status::success(StatusCode::NoOp));

    const float spacing = spec.spacing;
    switch (spec.kind) {
        case FormationKind::Line: {
            const float center = static_cast<float>(count - 1) * 0.5f;
            for (std::size_t index = 0; index < count; ++index) {
                result.push_back({anchor.x + (static_cast<float>(index) - center) * spacing, anchor.y});
            }
            break;
        }
        case FormationKind::Grid: {
            const std::size_t columns =
                spec.columns > 0 ? static_cast<std::size_t>(spec.columns)
                                 : static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
            if (columns == 0)
                return failure<std::vector<WorldPosition>>(DiagnosticCode::InvariantViolation,
                                                           "grid formation computed zero columns", "columns");
            const std::size_t rows         = (count + columns - 1) / columns;
            const float       columnCenter = static_cast<float>(columns - 1) * 0.5f;
            const float       rowCenter    = static_cast<float>(rows - 1) * 0.5f;
            for (std::size_t index = 0; index < count; ++index) {
                const std::size_t row    = index / columns;
                const std::size_t column = index % columns;
                result.push_back({anchor.x + (static_cast<float>(column) - columnCenter) * spacing,
                                  anchor.y + (static_cast<float>(row) - rowCenter) * spacing});
            }
            break;
        }
        case FormationKind::Wedge: {
            std::size_t row      = 0;
            std::size_t rowStart = 0;
            for (std::size_t index = 0; index < count; ++index) {
                while (index >= rowStart + row + 1) {
                    rowStart += row + 1;
                    ++row;
                }
                const std::size_t slot      = index - rowStart;
                const float       rowCenter = static_cast<float>(row) * 0.5f;
                result.push_back({anchor.x + (static_cast<float>(slot) - rowCenter) * spacing,
                                  anchor.y + static_cast<float>(row) * spacing});
            }
            break;
        }
    }
    return Result<std::vector<WorldPosition>>::success(std::move(result), Status::success(StatusCode::Applied));
}

Result<void> BuildInfluenceSystem::validate(Faction& faction, WorldPosition position,
                                            LogicalId definition, const PlacementValidation& placement,
                                            bool requireInfluence) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !definition.isValid() || !placement)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS building placement requires finite position, definition and provider",
                             "placement");
    if (requireInfluence) {
        bool covered = false;
        auto buildings = ecs::View<Building, Building::Placement, Building::Faction,
                                   Building::Construction, Building::Integrity,
                                   Building::Infrastructure>();
        for (auto it = buildings.begin(); it != buildings.end(); ++it) {
            auto [source, owner, construction, integrity, infrastructure] = *it;
            if (!source->placed || construction->progress < 1.0f || !integrity->alive ||
                !infrastructure->powered || infrastructure->buildInfluenceRadius <= 0.0f ||
                !FactionRelationSystem::allied(
                    dynamic_cast<Faction*>(owner->link.resolve()), &faction))
                continue;
            const float dx = source->worldX - position.x;
            const float dy = source->worldY - position.y;
            const float radius = infrastructure->buildInfluenceRadius;
            if (dx * dx + dy * dy <= radius * radius) {
                covered = true;
                break;
            }
        }
        if (!covered)
            return failure<void>(DiagnosticCode::Conflict,
                                 "RTS building position is outside powered allied build influence",
                                 "placement.influence");
    }
    return placement(position, std::move(definition));
}

Result<FanOutReceipt> CommandFanOutSystem::fanOut(std::span<const ecs::EntityHandle> unitHandles,
                                                  const CommandSpec& command, const FormationSpec& formation) {
    if (unitHandles.empty())
        return failure<FanOutReceipt>(DiagnosticCode::InvalidArgument, "RTS command fan-out requires at least one Unit",
                                      "selection.units");
    auto commandValid = command.validate();
    if (!commandValid) return failureFrom<FanOutReceipt>(commandValid.status());

    auto formationValid = formation.validate();
    if (!formationValid) return failureFrom<FanOutReceipt>(formationValid.status());
    auto targets = FormationPlanner::plan(unitHandles.size(), command.target, formation);
    if (!targets) return failureFrom<FanOutReceipt>(targets.status());
    auto plannedTargets = std::move(targets).takeValue();

    // The explicit View is the closure proof for this command boundary: a
    // Unit subclass is accepted by the Unit registry, while a Building root
    // cannot enter the selected set merely because it has an Identity field.
    std::vector<ecs::EntityHandle> visibleUnits;
    {
        auto view = ecs::View<Unit, Unit::Identity, Unit::Orders>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [identity, orders] = *it;
            (void)orders;
            Unit* unit = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
            if (unit != nullptr) visibleUnits.push_back(ecs::handle_of(unit));
        }
    }

    std::vector<Unit*> selected;
    selected.reserve(unitHandles.size());
    for (const auto& handle : unitHandles) {
        auto* entity = ecs::try_get(handle);
        auto* unit   = entity == nullptr ? nullptr : dynamic_cast<Unit*>(entity);
        if (unit == nullptr)
            return failure<FanOutReceipt>(DiagnosticCode::StaleHandle,
                                          "RTS fan-out selection contains a stale or non-Unit handle",
                                          "selection.units");
        const auto liveHandle = ecs::handle_of(unit);
        const bool inView = std::any_of(visibleUnits.begin(), visibleUnits.end(), [&liveHandle](const auto& candidate) {
            return sameHandle(candidate, liveHandle);
        });
        if (!inView)
            return failure<FanOutReceipt>(DiagnosticCode::InvariantViolation,
                                          "RTS Unit selection is outside the declared View closure", "selection.units");
        selected.push_back(unit);
    }

    struct AvailableSlot { WorldPosition position{}; int index = -1; };
    std::vector<AvailableSlot> available;
    available.reserve(plannedTargets.size());
    for (std::size_t index = 0; index < plannedTargets.size(); ++index)
        available.push_back({plannedTargets[index], static_cast<int>(index)});
    std::vector<std::size_t> assignmentOrder(selected.size());
    for (std::size_t index = 0; index < selected.size(); ++index) assignmentOrder[index] = index;
    std::sort(assignmentOrder.begin(), assignmentOrder.end(), [&](std::size_t left, std::size_t right) {
        const auto leftMotion = selected[left]->motion();
        const auto rightMotion = selected[right]->motion();
        const float leftDistance = distanceSquared(leftMotion->x, leftMotion->y, command.target.x, command.target.y);
        const float rightDistance = distanceSquared(rightMotion->x, rightMotion->y, command.target.x, command.target.y);
        if (leftDistance != rightDistance) return leftDistance > rightDistance;
        return selected[left]->identity()->self.id < selected[right]->identity()->self.id;
    });
    std::vector<AvailableSlot> assignedSlots(selected.size());
    for (const std::size_t selectedIndex : assignmentOrder) {
        const auto motion = selected[selectedIndex]->motion();
        auto best = std::min_element(available.begin(), available.end(), [&](const auto& left, const auto& right) {
            const float leftDistance = distanceSquared(motion->x, motion->y, left.position.x, left.position.y);
            const float rightDistance = distanceSquared(motion->x, motion->y, right.position.x, right.position.y);
            return leftDistance != rightDistance ? leftDistance < rightDistance : left.index < right.index;
        });
        assignedSlots[selectedIndex] = *best;
        available.erase(best);
    }

    FanOutReceipt receipt;
    receipt.requested = selected.size();
    receipt.orderIds.reserve(selected.size());
    std::vector<OrderComponent::Snapshot> previous;
    previous.reserve(selected.size());
    for (Unit* unit : selected) {
        auto snapshot = unit->orders()->values.snapshotState();
        if (!snapshot) return failureFrom<FanOutReceipt>(snapshot.status());
        previous.push_back(std::move(snapshot).takeValue());
    }
    for (std::size_t index = 0; index < selected.size(); ++index) {
        CommandSpec assigned = command;
        assigned.target = assignedSlots[index].position;
        auto order = assigned.append
                         ? selected[index]->orders()->values.enqueue(assigned, assignedSlots[index].index)
                         : selected[index]->orders()->values.replace(assigned, assignedSlots[index].index);
        if (!order) {
            const Status status = order.status();
            for (std::size_t rollback = 0; rollback < selected.size(); ++rollback)
                selected[rollback]->orders()->values.restoreState(previous[rollback])
                    .ignore("best-effort atomic fan-out rollback");
            return Result<FanOutReceipt>::failure(status);
        }
        receipt.orderIds.push_back(std::move(order).takeValue());
    }
    receipt.accepted = receipt.orderIds.size();
    return Result<FanOutReceipt>::success(std::move(receipt), Status::success(StatusCode::Applied));
}

Result<std::size_t> TacticsSystem::step(const combat::DamageRuntime* damage, const SimulationStep& step) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                    "RTS tactics step delta must be non-negative", "step.delta");
    struct Candidate {
        ecs::EntityHandle handle{};
        FactionLink* faction = nullptr;
        WorldPosition position{};
        double health = 0.0;
        float shield = 0.0f;
        combat::CombatState* durability = nullptr;
        std::string key;
        ecs::EntityHandle activeTarget{};
        TagSet* tags = nullptr;
        bool airborne = false;
        bool cloaked = false;
    };
    std::vector<Candidate> candidates;
    auto units = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Faction, Unit::Combat, Unit::Durability,
                           Unit::Shield, Unit::Containment, Unit::Tags>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [identity, motion, faction, combat, durability, shield, containment, tags] = *it;
        if (!durability->alive || containment->container.isBound()) continue;
        candidates.push_back({identity->self, &faction->link, {motion->x, motion->y}, durability->state.health,
                              shield->value, &durability->state,
                              "u:" + std::to_string(identity->self.id) + ":" +
                                  std::to_string(identity->self.generation), combat->target, &tags->values,
                              motion->airborne});
    }
    auto buildings = ecs::View<Building, Building::Identity, Building::Placement, Building::Faction,
                               Building::Combat, Building::Integrity, Building::Shield, Building::Tags>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [identity, placement, faction, combat, integrity, shield, tags] = *it;
        if (!integrity->alive) continue;
        candidates.push_back({identity->self, &faction->link, {placement->worldX, placement->worldY},
                              integrity->state.health, shield->value, &integrity->state,
                              "b:" + std::to_string(identity->self.id) + ":" +
                                  std::to_string(identity->self.generation), combat->target, &tags->values, false});
    }

    std::size_t processed = 0;
    auto tacticalUnits = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders, Unit::Faction, Unit::Combat,
                                  Unit::Tactics, Unit::Weapon, Unit::Durability, Unit::Containment>();
    std::map<std::uint64_t, std::vector<Unit*>> groups;
    struct EscortAssignment {
        Unit* unit = nullptr;
        WorldPosition center{};
        ecs::EntityHandle protectedTarget{};
        std::vector<ecs::EntityHandle> protectedMembers;
        float protectionRange = 0.0f;
        WorldPosition travelDirection{};
        int screenSector = 0;
    };
    std::map<std::string, std::vector<EscortAssignment>> escortGroups;
    for (auto it = tacticalUnits.begin(); it != tacticalUnits.end(); ++it) {
        auto [identity, motion, orders, faction, combat, tactics, weaponLink, durability, containment] = *it;
        if (!durability->alive || containment->container.isBound()) continue;
        auto* self = dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (self == nullptr) continue;
        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto order = std::move(current).takeValue();
        if (!self->morale()->retreating || tactics->combatGroup == 0 || weaponLink->link.resolve() == nullptr ||
            (order && order->kind == OrderKind::Attack)) {
            tactics->retreatFireTeam = -1;
            tactics->retreatCoverElapsed = 0.0f;
            tactics->retreatCovering = false;
        }
        if (order && order->kind == OrderKind::Escort) {
            tactics->escortTarget = order->targetEntity;
            auto* protectedEntity = ecs::try_get(order->targetEntity);
            WorldPosition center{};
            FactionLink* protectedFaction = nullptr;
            if (auto* protectedUnit = dynamic_cast<Unit*>(protectedEntity)) {
                if (protectedUnit->durability()->alive && !protectedUnit->containment()->container.isBound()) {
                    center = {protectedUnit->motion()->x, protectedUnit->motion()->y};
                    protectedFaction = &protectedUnit->faction()->link;
                }
            } else if (auto* protectedBuilding = dynamic_cast<Building*>(protectedEntity)) {
                if (protectedBuilding->integrity()->alive) {
                    center = {protectedBuilding->placement()->worldX, protectedBuilding->placement()->worldY};
                    protectedFaction = &protectedBuilding->faction()->link;
                }
            }
            if (protectedFaction == nullptr ||
                !FactionRelationSystem::allied(*protectedFaction, faction->link)) {
                auto failed = orders->values.fail(order->id, "escort target is invalid or hostile");
                if (!failed) return failureFrom<std::size_t>(failed.status());
                tactics->escortTarget = {};
                tactics->guardSet = false;
                tactics->escortInterceptTarget = {};
                combat->target = {};
                continue;
            }
            std::vector<ecs::EntityHandle> protectedMembers{order->targetEntity};
            float convoyExtent = 0.0f;
            WorldPosition travelDirection{};
            if (auto* protectedUnit = dynamic_cast<Unit*>(protectedEntity);
                protectedUnit != nullptr && ecs::try_get(protectedUnit->supply()->convoyLeader) != nullptr) {
                const auto leader = protectedUnit->supply()->convoyLeader;
                std::vector<Unit*> convoy;
                auto convoyUnits = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Supply,
                                             Unit::Durability, Unit::Containment>();
                for (auto convoyIt = convoyUnits.begin(); convoyIt != convoyUnits.end(); ++convoyIt) {
                    auto [memberIdentity, memberMotion, memberSupply, memberDurability, memberContainment] = *convoyIt;
                    if (!memberDurability->alive || memberContainment->container.isBound() ||
                        !sameHandle(memberSupply->convoyLeader, leader)) continue;
                    if (auto* member = dynamic_cast<Unit*>(ecs::try_get(memberIdentity->self)); member != nullptr)
                        convoy.push_back(member);
                }
                if (convoy.size() > 1) {
                    center = {};
                    protectedMembers.clear();
                    for (Unit* member : convoy) {
                        center.x += member->motion()->x;
                        center.y += member->motion()->y;
                        protectedMembers.push_back(member->identity()->self);
                    }
                    center.x /= static_cast<float>(convoy.size());
                    center.y /= static_cast<float>(convoy.size());
                    for (Unit* member : convoy)
                        convoyExtent = std::max(convoyExtent,
                            std::hypot(member->motion()->x - center.x, member->motion()->y - center.y));
                    if (auto* convoyLeader = dynamic_cast<Unit*>(ecs::try_get(leader)); convoyLeader != nullptr) {
                        auto leaderOrder = readCurrent(convoyLeader->orders()->values);
                        if (!leaderOrder) return failureFrom<std::size_t>(leaderOrder.status());
                        if (leaderOrder.value()) {
                            travelDirection = {leaderOrder.value()->target.x - convoyLeader->motion()->x,
                                               leaderOrder.value()->target.y - convoyLeader->motion()->y};
                            const float length = std::hypot(travelDirection.x, travelDirection.y);
                            if (length > 1e-5f) {
                                travelDirection.x /= length;
                                travelDirection.y /= length;
                            }
                        }
                    }
                }
            }
            WorldPosition offset{tactics->escortOffsetX, tactics->escortOffsetY};
            if (std::hypot(travelDirection.x, travelDirection.y) > 1e-5f)
                offset = {travelDirection.x * tactics->escortOffsetX - travelDirection.y * tactics->escortOffsetY,
                          travelDirection.y * tactics->escortOffsetX + travelDirection.x * tactics->escortOffsetY};
            int screenSector = 0;
            if (std::hypot(travelDirection.x, travelDirection.y) > 1e-5f) {
                if (std::abs(tactics->escortOffsetX) >= std::abs(tactics->escortOffsetY))
                    screenSector = tactics->escortOffsetX >= 0.0f ? 2 : -2;
                else
                    screenSector = tactics->escortOffsetY >= 0.0f ? 1 : -1;
            }
            tactics->escortScreenSector = screenSector;
            tactics->escortSectorMatched = false;
            tactics->escortReinforcing = false;
            tactics->escortReinforcementSector = 0;
            tactics->escortRearGuard = false;
            tactics->guardX = center.x + offset.x;
            tactics->guardY = center.y + offset.y;
            tactics->guardSet = true;
            combat->guardX = center.x;
            combat->guardY = center.y;
            combat->guardSet = true;
            const float protectionRange = tactics->protectionRange + convoyExtent;
            combat->leashRange = std::max(combat->leashRange, protectionRange);
            escortGroups[std::to_string(order->targetEntity.id) + ":" +
                         std::to_string(order->targetEntity.generation)]
                .push_back({self, center, order->targetEntity, std::move(protectedMembers), protectionRange,
                            travelDirection, screenSector});
        } else if (tactics->combatGroup != 0 && weaponLink->link.resolve() != nullptr &&
                   (!order || order->kind != OrderKind::Attack)) {
            tactics->escortScreenSector = 0;
            tactics->escortSectorMatched = false;
            tactics->escortReinforcing = false;
            tactics->escortReinforcementSector = 0;
            tactics->escortRearGuard = false;
            tactics->escortInterceptTarget = {};
            groups[tactics->combatGroup].push_back(self);
        }
    }
    for (auto& [protectedKey, escorts] : escortGroups) {
        (void)protectedKey;
        std::sort(escorts.begin(), escorts.end(), [](const auto& left, const auto& right) {
            return left.unit->identity()->subject.format() < right.unit->identity()->subject.format();
        });
        auto* protectedUnit = escorts.empty() ? nullptr
            : dynamic_cast<Unit*>(ecs::try_get(escorts.front().protectedTarget));
        if (protectedUnit != nullptr && protectedUnit->morale()->retreating) {
            WorldPosition retreatDirection{
                protectedUnit->navigation()->plannedGoal.x - protectedUnit->motion()->x,
                protectedUnit->navigation()->plannedGoal.y - protectedUnit->motion()->y};
            float length = std::hypot(retreatDirection.x, retreatDirection.y);
            if (length > 1e-5f) {
                retreatDirection.x /= length;
                retreatDirection.y /= length;
                const WorldPosition side{-retreatDirection.y, retreatDirection.x};
                for (std::size_t index = 0; index < escorts.size(); ++index) {
                    auto tactics = escorts[index].unit->tactics();
                    const float slot = static_cast<float>(index) -
                                       (static_cast<float>(escorts.size()) - 1.0f) * 0.5f;
                    const float depth = std::max(1.0f,
                        std::hypot(tactics->escortOffsetX, tactics->escortOffsetY));
                    const float spacing = 1.5f;
                    tactics->guardX = escorts[index].center.x - retreatDirection.x * depth + side.x * slot * spacing;
                    tactics->guardY = escorts[index].center.y - retreatDirection.y * depth + side.y * slot * spacing;
                    tactics->escortRearGuard = true;
                }
            }
        }
        std::set<std::string> claimed;
        for (const auto& escort : escorts) {
            Candidate* best = nullptr;
            bool bestDirect = false;
            bool bestSector = false;
            int bestThreatSector = 0;
            float bestDistance = std::numeric_limits<float>::max();
            const auto faction = escort.unit->faction();
            const float range = escort.protectionRange;
            auto consider = [&](Candidate& candidate, bool allowClaimed) {
                if ((!allowClaimed && claimed.contains(candidate.key)) || candidate.faction == nullptr ||
                    FactionRelationSystem::allied(*candidate.faction, faction->link)) return;
                const float distance = distanceSquared(escort.center.x, escort.center.y,
                                                       candidate.position.x, candidate.position.y);
                if (distance > range * range) return;
                const bool direct = std::any_of(escort.protectedMembers.begin(), escort.protectedMembers.end(),
                    [&](ecs::EntityHandle member) { return sameHandle(candidate.activeTarget, member); });
                int threatSector = 0;
                const float forwardLength = std::hypot(escort.travelDirection.x, escort.travelDirection.y);
                if (forwardLength > 1e-5f) {
                    const float dx = candidate.position.x - escort.center.x;
                    const float dy = candidate.position.y - escort.center.y;
                    const float longitudinal = dx * escort.travelDirection.x + dy * escort.travelDirection.y;
                    const float lateral = dx * -escort.travelDirection.y + dy * escort.travelDirection.x;
                    threatSector = std::abs(longitudinal) >= std::abs(lateral)
                                       ? (longitudinal >= 0.0f ? 2 : -2)
                                       : (lateral >= 0.0f ? 1 : -1);
                }
                const bool sector = escort.screenSector == 0 || escort.screenSector == threatSector;
                const bool preferred = best == nullptr ||
                    (direct != bestDirect ? direct > bestDirect
                     : sector != bestSector ? sector > bestSector
                     : distance != bestDistance ? distance < bestDistance : candidate.key < best->key);
                if (preferred) {
                    best = &candidate;
                    bestDirect = direct;
                    bestSector = sector;
                    bestThreatSector = threatSector;
                    bestDistance = distance;
                }
            };
            for (Candidate& candidate : candidates) consider(candidate, false);
            if (best == nullptr)
                for (Candidate& candidate : candidates) consider(candidate, true);
            const SubjectRef nextIntercept = best == nullptr || best->durability == nullptr
                                                 ? SubjectRef{} : best->durability->subject;
            auto tactics = escort.unit->tactics();
            if (tactics->escortInterceptTarget.isValid() && nextIntercept.isValid() &&
                tactics->escortInterceptTarget != nextIntercept)
                ++tactics->escortHandoffCount;
            tactics->escortInterceptTarget = nextIntercept;
            escort.unit->combat()->target = best == nullptr ? ecs::EntityHandle{} : best->handle;
            tactics->escortSectorMatched = best != nullptr && bestSector;
            tactics->escortReinforcing = best != nullptr && escort.screenSector != 0 && !bestSector;
            tactics->escortReinforcementSector = tactics->escortReinforcing ? bestThreatSector : 0;
            if (best != nullptr) claimed.insert(best->key);
            ++processed;
        }
    }
    constexpr float retreatCoverInterval = 1.5f;
    for (auto& [groupId, members] : groups) {
        (void)groupId;
        std::sort(members.begin(), members.end(), [](Unit* left, Unit* right) {
            return left->identity()->subject.format() < right->identity()->subject.format();
        });
        std::vector<Unit*> retreating;
        float groupElapsed = 0.0f;
        for (Unit* member : members) {
            auto tactics = member->tactics();
            if (!member->morale()->retreating) {
                tactics->retreatFireTeam = -1;
                tactics->retreatCoverElapsed = 0.0f;
                tactics->retreatCovering = false;
                continue;
            }
            retreating.push_back(member);
            groupElapsed = std::max(groupElapsed, tactics->retreatCoverElapsed);
        }
        if (retreating.size() < 2) {
            for (Unit* member : retreating) {
                member->tactics()->retreatFireTeam = -1;
                member->tactics()->retreatCoverElapsed = 0.0f;
                member->tactics()->retreatCovering = false;
            }
            continue;
        }
        groupElapsed += static_cast<float>(step.delta.seconds());
        const int coveringTeam = static_cast<int>(std::floor(groupElapsed / retreatCoverInterval)) % 2;
        for (std::size_t index = 0; index < retreating.size(); ++index) {
            auto tactics = retreating[index]->tactics();
            tactics->retreatFireTeam = static_cast<int>(index % 2);
            tactics->retreatCoverElapsed = groupElapsed;
            tactics->retreatCovering = tactics->retreatFireTeam == coveringTeam;
        }
    }
    // Automatic groups share one deterministic commitment ledger for the tick.
    // This prevents separately numbered formations from independently budgeting
    // lethal volleys against the same target. Explicit Attack orders never enter
    // this path and therefore retain intentional focus-fire semantics.
    std::map<std::string, double> committed;
    auto commitShot = [&](SubjectRef source, ecs::Entity* ownFaction, WorldPosition origin,
                          const weapon::WeaponDefinition& definition, const Candidate& aim,
                          float damageFactor) -> Result<void> {
        const float launchDistance = std::hypot(aim.position.x - origin.x, aim.position.y - origin.y);
        const double baseDamage = static_cast<double>(definition.damage) *
                                  static_cast<double>(std::max(1, definition.projectile.pelletCount)) *
                                  static_cast<double>(damageFactor) *
                                  static_cast<double>(weaponRangeDamageFactor(definition, launchDistance));
        const bool splash = definition.projectile.speed > 0.0f && definition.projectile.aoe > 0.0f;
        for (Candidate& victim : candidates) {
            if ((!splash && !sameHandle(victim.handle, aim.handle)) || victim.faction == nullptr ||
                (!definition.friendlyFire && FactionRelationSystem::allied(
                    dynamic_cast<Faction*>(victim.faction->resolve()), dynamic_cast<Faction*>(ownFaction))) ||
                (victim.airborne && !definition.targetsAir) || (!victim.airborne && !definition.targetsGround) ||
                victim.tags == nullptr ||
                std::any_of(definition.requiredTargetTags.begin(), definition.requiredTargetTags.end(),
                    [&](const auto& tag) { return !victim.tags->contains(tag); }) ||
                std::any_of(definition.excludedTargetTags.begin(), definition.excludedTargetTags.end(),
                    [&](const auto& tag) { return victim.tags->contains(tag); })) continue;
            double radial = 1.0;
            if (splash) {
                const float distance = std::hypot(victim.position.x - aim.position.x,
                                                  victim.position.y - aim.position.y);
                if (distance > definition.projectile.aoe) continue;
                radial = std::max<double>(definition.splashMinimumDamageFactor,
                                          1.0 - distance / definition.projectile.aoe);
            }
            const double raw = baseDamage * radial;
            const double shieldDamage = std::min<double>(std::max(0.0f, victim.shield), raw);
            double healthDamage = raw - shieldDamage;
            if (damage != nullptr && victim.durability != nullptr && healthDamage > 0.0) {
                combat::DamageRequest request;
                request.source = source;
                request.target = victim.durability->subject;
                request.damageType = definition.damageType.empty() ? "damage.physical" : definition.damageType;
                request.healthDamage = healthDamage;
                auto previewed = damage->preview(*victim.durability, request);
                if (!previewed) return Result<void>::failure(previewed.status());
                healthDamage = previewed.value().healthDamage;
            }
            committed[victim.key] += shieldDamage + healthDamage;
        }
        return Result<void>::success(Status::success(StatusCode::Applied));
    };
    for (auto& [groupId, members] : groups) {
        (void)groupId;
        std::sort(members.begin(), members.end(), [](Unit* left, Unit* right) {
            return left->identity()->self.id < right->identity()->self.id;
        });
        const float volleyPhase = members.front()->tactics()->volleyReleaseRemaining;
        for (Unit* member : members)
            if (member->tactics()->coordinatedVolleyInterval > 0.0f)
                member->tactics()->volleyReleaseRemaining = volleyPhase;
        WorldPosition groupCenter{};
        for (Unit* member : members) {
            groupCenter.x += member->motion()->x;
            groupCenter.y += member->motion()->y;
        }
        groupCenter.x /= static_cast<float>(members.size());
        groupCenter.y /= static_cast<float>(members.size());
        for (Unit* shooter : members) {
            if (shooter->morale()->retreating && !shooter->tactics()->retreatCovering) {
                shooter->combat()->target = {};
                continue;
            }
            auto* weaponEntity = dynamic_cast<weapon::WeaponEntity*>(shooter->weapon()->link.resolve());
            if (weaponEntity == nullptr || weaponEntity->definition()->def == nullptr) continue;
            const auto& definition = *weaponEntity->definition()->def;
            Candidate* best = nullptr;
            float bestScore = std::numeric_limits<float>::max();
            bool bestSector = false;
            float bestPreference = -1.0f;
            float bestEffectiveness = -1.0f;
            for (Candidate& candidate : candidates) {
                if (candidate.faction == nullptr ||
                    FactionRelationSystem::allied(*candidate.faction, shooter->faction()->link) ||
                    sameHandle(candidate.handle, shooter->identity()->self) ||
                    committed[candidate.key] >= candidate.health + candidate.shield)
                    continue;
                const float distance = distanceSquared(shooter->motion()->x, shooter->motion()->y,
                                                       candidate.position.x, candidate.position.y);
                const float range = shooter->combat()->acquisitionRange > 0.0f
                                        ? shooter->combat()->acquisitionRange : definition.range;
                if (distance > range * range) continue;
                const float shooterX = shooter->motion()->x - groupCenter.x;
                const float shooterY = shooter->motion()->y - groupCenter.y;
                const float targetX = candidate.position.x - groupCenter.x;
                const float targetY = candidate.position.y - groupCenter.y;
                const bool sector = shooter->tactics()->threatSector == 0 ||
                                    shooterX * targetX + shooterY * targetY > 0.0f;
                const float preference = weaponTargetPreference(definition, candidate.tags);
                float effectiveness = 1.0f;
                if (damage != nullptr && candidate.durability != nullptr && definition.damage > 0.0f) {
                    combat::DamageRequest request;
                    request.source = shooter->identity()->subject;
                    request.target = candidate.durability->subject;
                    request.damageType = definition.damageType.empty() ? "damage.physical" : definition.damageType;
                    request.healthDamage = definition.damage;
                    auto previewed = damage->preview(*candidate.durability, request);
                    if (!previewed) return failureFrom<std::size_t>(previewed.status());
                    effectiveness = static_cast<float>(previewed.value().healthDamage / definition.damage);
                }
                const bool preferred = best == nullptr ||
                    (sector != bestSector ? sector > bestSector
                     : effectiveness != bestEffectiveness ? effectiveness > bestEffectiveness
                     : preference != bestPreference ? preference > bestPreference
                     : distance != bestScore ? distance < bestScore : candidate.key < best->key);
                if (preferred) {
                    best = &candidate;
                    bestScore = distance;
                    bestSector = sector;
                    bestPreference = preference;
                    bestEffectiveness = effectiveness;
                }
            }
            if (best == nullptr) continue;
            shooter->combat()->target = best->handle;
            shooter->tactics()->fireControlEffectiveness = bestEffectiveness < 0.0f ? 1.0f : bestEffectiveness;
            auto committedShot = commitShot(shooter->identity()->subject, shooter->faction()->link.resolve(),
                                            {shooter->motion()->x, shooter->motion()->y}, definition, *best, 1.0f);
            if (!committedShot) return failureFrom<std::size_t>(committedShot.status());
            ++processed;
        }
    }

    struct TurretNode {
        Building* building = nullptr;
        const weapon::WeaponDefinition* weapon = nullptr;
        std::string key;
    };
    std::vector<TurretNode> turrets;
    auto turretBuildings = ecs::View<Building, Building::Identity, Building::Orders, Building::Faction,
                                     Building::Placement, Building::Weapon, Building::Combat,
                                     Building::Integrity, Building::Construction, Building::Infrastructure,
                                     Building::Garrison>();
    for (auto it = turretBuildings.begin(); it != turretBuildings.end(); ++it) {
        auto [identity, orders, faction, placement, weaponLink, combat, integrity, construction,
              infrastructure, garrison] = *it;
        (void)orders; (void)faction; (void)placement; (void)garrison;
        auto* building = dynamic_cast<Building*>(ecs::try_get(identity->self));
        auto* weaponEntity = dynamic_cast<weapon::WeaponEntity*>(weaponLink->link.resolve());
        if (building == nullptr || !integrity->alive || construction->progress < 1.0f ||
            !infrastructure->powered || combat->airDefenseNetworkRange > 0.0f || weaponEntity == nullptr ||
            weaponEntity->definition()->def == nullptr) continue;
        turrets.push_back({building, weaponEntity->definition()->def, identity->subject.format()});
    }
    std::sort(turrets.begin(), turrets.end(), [](const auto& left, const auto& right) {
        return left.key < right.key;
    });
    for (const TurretNode& turret : turrets) {
        Building* building = turret.building;
        auto current = readCurrent(building->orders()->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        if (current.value() && current.value()->kind == OrderKind::Attack) continue;
        const auto& definition = *turret.weapon;
        Candidate* best = nullptr;
        float bestPreference = -1.0f;
        float bestEffectiveness = -1.0f;
        float bestDistance = std::numeric_limits<float>::max();
        for (Candidate& candidate : candidates) {
            if (candidate.faction == nullptr ||
                FactionRelationSystem::allied(*candidate.faction, building->faction()->link) ||
                sameHandle(candidate.handle, building->identity()->self) ||
                committed[candidate.key] >= candidate.health + candidate.shield ||
                (candidate.airborne && !definition.targetsAir) ||
                (!candidate.airborne && !definition.targetsGround) || candidate.tags == nullptr ||
                std::any_of(definition.requiredTargetTags.begin(), definition.requiredTargetTags.end(),
                    [&](const auto& tag) { return !candidate.tags->contains(tag); }) ||
                std::any_of(definition.excludedTargetTags.begin(), definition.excludedTargetTags.end(),
                    [&](const auto& tag) { return candidate.tags->contains(tag); })) continue;
            const float range = building->combat()->acquisitionRange > 0.0f
                                    ? building->combat()->acquisitionRange : definition.range;
            const float distance = distanceSquared(building->placement()->worldX,
                building->placement()->worldY, candidate.position.x, candidate.position.y);
            if (distance > range * range) continue;
            const float preference = weaponTargetPreference(definition, candidate.tags);
            float effectiveness = 1.0f;
            if (damage != nullptr && candidate.durability != nullptr && definition.damage > 0.0f) {
                combat::DamageRequest request;
                request.source = building->identity()->subject;
                request.target = candidate.durability->subject;
                request.damageType = definition.damageType.empty() ? "damage.physical" : definition.damageType;
                request.healthDamage = definition.damage;
                auto previewed = damage->preview(*candidate.durability, request);
                if (!previewed) return failureFrom<std::size_t>(previewed.status());
                effectiveness = static_cast<float>(previewed.value().healthDamage / definition.damage);
            }
            if (best == nullptr || effectiveness > bestEffectiveness ||
                (effectiveness == bestEffectiveness && (preference > bestPreference ||
                 (preference == bestPreference && (distance < bestDistance ||
                  (distance == bestDistance && candidate.key < best->key)))))) {
                best = &candidate;
                bestPreference = preference;
                bestEffectiveness = effectiveness;
                bestDistance = distance;
            }
        }
        building->combat()->target = best == nullptr ? ecs::EntityHandle{} : best->handle;
        if (best != nullptr) {
            const float garrisonFactor = 1.0f + building->garrison()->damageBonusPerOccupant *
                                                    static_cast<float>(building->garrison()->occupants.size());
            auto committedShot = commitShot(building->identity()->subject, building->faction()->link.resolve(),
                                            {building->placement()->worldX, building->placement()->worldY},
                                            definition, *best, garrisonFactor);
            if (!committedShot) return failureFrom<std::size_t>(committedShot.status());
        }
        ++processed;
    }

    struct DefenseNode {
        Building* building = nullptr;
        const weapon::WeaponDefinition* weapon = nullptr;
        std::string key;
    };
    std::vector<DefenseNode> defenses;
    auto defenseBuildings = ecs::View<Building, Building::Identity, Building::Placement, Building::Orders,
                                      Building::Faction, Building::Weapon, Building::Combat, Building::Integrity,
                                      Building::Construction, Building::Infrastructure>();
    for (auto it = defenseBuildings.begin(); it != defenseBuildings.end(); ++it) {
        auto [identity, placement, orders, faction, weaponLink, combat, integrity, construction, infrastructure] = *it;
        (void)placement; (void)orders; (void)faction;
        combat->airDefenseNetworkRoot = {};
        combat->airDefenseNetworkSize = 0;
        if (!std::isfinite(combat->airDefenseNetworkRange) || combat->airDefenseNetworkRange < 0.0f)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                        "RTS air-defense network range must be finite and non-negative",
                                        "building.combat.airDefenseNetworkRange");
        auto* building = dynamic_cast<Building*>(ecs::try_get(identity->self));
        auto* weaponEntity = dynamic_cast<weapon::WeaponEntity*>(weaponLink->link.resolve());
        if (building == nullptr || !integrity->alive || construction->progress < 1.0f ||
            !infrastructure->powered || combat->airDefenseNetworkRange <= 0.0f || weaponEntity == nullptr ||
            weaponEntity->definition()->def == nullptr || !weaponEntity->definition()->def->targetsAir)
            continue;
        defenses.push_back({building, weaponEntity->definition()->def, identity->subject.format()});
    }
    std::sort(defenses.begin(), defenses.end(), [](const auto& left, const auto& right) {
        return left.key < right.key;
    });
    std::vector<bool> visited(defenses.size(), false);
    for (std::size_t start = 0; start < defenses.size(); ++start) {
        if (visited[start]) continue;
        std::vector<std::size_t> component{start};
        visited[start] = true;
        for (std::size_t cursor = 0; cursor < component.size(); ++cursor) {
            const auto index = component[cursor];
            Building* current = defenses[index].building;
            for (std::size_t other = 0; other < defenses.size(); ++other) {
                if (visited[other] || !FactionRelationSystem::allied(
                        defenses[other].building->faction()->link,
                        current->faction()->link)) continue;
                Building* candidate = defenses[other].building;
                const float linkRange = std::max(current->combat()->airDefenseNetworkRange,
                                                 candidate->combat()->airDefenseNetworkRange);
                if (distanceSquared(current->placement()->worldX, current->placement()->worldY,
                                    candidate->placement()->worldX, candidate->placement()->worldY) <=
                    linkRange * linkRange) {
                    visited[other] = true;
                    component.push_back(other);
                }
            }
        }
        std::sort(component.begin(), component.end(), [&](auto left, auto right) {
            return defenses[left].key < defenses[right].key;
        });
        const auto root = defenses[component.front()].building->identity()->self;
        std::set<std::string> claimed;
        for (const auto index : component) {
            DefenseNode& defense = defenses[index];
            Building* building = defense.building;
            building->combat()->airDefenseNetworkRoot = root;
            building->combat()->airDefenseNetworkSize = component.size();
            auto current = readCurrent(building->orders()->values);
            if (!current) return failureFrom<std::size_t>(current.status());
            if (current.value() && current.value()->kind == OrderKind::Attack) continue;
            Candidate* best = nullptr;
            float bestPreference = -1.0f;
            float bestDistance = std::numeric_limits<float>::max();
            for (Candidate& candidate : candidates) {
                if (!candidate.airborne || candidate.faction == nullptr ||
                    FactionRelationSystem::allied(*candidate.faction, building->faction()->link) ||
                    claimed.contains(candidate.key)) continue;
                const auto& definition = *defense.weapon;
                if (candidate.tags == nullptr ||
                    std::any_of(definition.requiredTargetTags.begin(), definition.requiredTargetTags.end(),
                        [&](const auto& tag) { return !candidate.tags->contains(tag); }) ||
                    std::any_of(definition.excludedTargetTags.begin(), definition.excludedTargetTags.end(),
                        [&](const auto& tag) { return candidate.tags->contains(tag); })) continue;
                const float range = building->combat()->acquisitionRange > 0.0f
                                        ? building->combat()->acquisitionRange : definition.range;
                const float distance = distanceSquared(building->placement()->worldX,
                    building->placement()->worldY, candidate.position.x, candidate.position.y);
                if (distance > range * range) continue;
                const float preference = weaponTargetPreference(definition, candidate.tags);
                if (best == nullptr || preference > bestPreference ||
                    (preference == bestPreference && (distance < bestDistance ||
                     (distance == bestDistance && candidate.key < best->key)))) {
                    best = &candidate;
                    bestPreference = preference;
                    bestDistance = distance;
                }
            }
            building->combat()->target = best == nullptr ? ecs::EntityHandle{} : best->handle;
            if (best != nullptr) claimed.insert(best->key);
            ++processed;
        }
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> AISystem::step(const SimulationStep& step, const AIProductionRequest& requestProduction) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS AI step delta must be non-negative",
                                    "step.delta");
    std::size_t processed = 0;
    auto factions = ecs::View<Faction, Faction::Identity, Faction::Strategy>();
    for (auto it = factions.begin(); it != factions.end(); ++it) {
        auto [identity, strategy] = *it;
        auto* faction = dynamic_cast<Faction*>(ecs::try_get(identity->self));
        if (faction == nullptr || !strategy->enabled) continue;
        if (!std::isfinite(strategy->thinkInterval) || strategy->thinkInterval <= 0.0f ||
            strategy->desiredWorkers < 0 || strategy->attackThreshold <= 0 ||
            !std::isfinite(strategy->formationSpacing) || strategy->formationSpacing <= 0.0f)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS AI policy values are invalid",
                                        "faction.strategy");
        strategy->thinkAccumulator += static_cast<float>(step.delta.seconds());
        if (strategy->thinkAccumulator + 1e-6f < strategy->thinkInterval) continue;
        strategy->thinkAccumulator = std::fmod(strategy->thinkAccumulator, strategy->thinkInterval);

        int workerCount = 0;
        std::vector<ecs::EntityHandle> army;
        auto units = ecs::View<Unit, Unit::Identity, Unit::Definition, Unit::Faction, Unit::Orders,
                               Unit::Durability, Unit::Containment>();
        for (auto unitIt = units.begin(); unitIt != units.end(); ++unitIt) {
            auto [unitIdentity, definition, owner, orders, durability, containment] = *unitIt;
            if (!durability->alive || containment->container.isBound() ||
                owner->link.resolve() != faction) continue;
            if (strategy->workerDefinition.isValid() && definition->id == strategy->workerDefinition) ++workerCount;
            if (strategy->armyDefinition.isValid() && definition->id == strategy->armyDefinition)
                army.push_back(unitIdentity->self);
            (void)orders;
        }

        if (requestProduction) {
            const LogicalId& wanted = workerCount < strategy->desiredWorkers ? strategy->workerDefinition
                                                                             : strategy->armyDefinition;
            if (wanted.isValid()) {
                Building* producer = nullptr;
                auto buildings = ecs::View<Building, Building::Identity, Building::Faction,
                                           Building::Construction, Building::Integrity, Building::Production>();
                for (auto buildingIt = buildings.begin(); buildingIt != buildings.end(); ++buildingIt) {
                    auto [buildingIdentity, owner, construction, integrity, production] = *buildingIt;
                    (void)production;
                    if (!integrity->alive || construction->progress < 1.0f || owner->link.resolve() != faction)
                        continue;
                    auto* candidate = dynamic_cast<Building*>(ecs::try_get(buildingIdentity->self));
                    if (candidate != nullptr && (producer == nullptr ||
                        candidate->identity()->self.id < producer->identity()->self.id)) producer = candidate;
                }
                if (producer != nullptr) {
                    auto requested = requestProduction(*faction, *producer, wanted);
                    if (!requested) return failureFrom<std::size_t>(requested.status());
                    ++processed;
                }
            }
        }

        if (static_cast<int>(army.size()) < strategy->attackThreshold) continue;
        std::vector<ecs::EntityHandle> idleArmy;
        for (const auto& handle : army) {
            auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
            if (unit != nullptr && unit->orders()->values.empty()) idleArmy.push_back(handle);
        }
        if (idleArmy.empty()) continue;
        Building* target = nullptr;
        auto targets = ecs::View<Building, Building::Identity, Building::Definition, Building::Faction,
                                 Building::Placement, Building::Integrity>();
        for (auto targetIt = targets.begin(); targetIt != targets.end(); ++targetIt) {
            auto [targetIdentity, definition, owner, placement, integrity] = *targetIt;
            (void)placement;
            if (!integrity->alive || FactionRelationSystem::allied(
                    dynamic_cast<Faction*>(owner->link.resolve()), faction) ||
                (strategy->targetBuildingDefinition.isValid() &&
                 definition->id != strategy->targetBuildingDefinition)) continue;
            auto* candidate = dynamic_cast<Building*>(ecs::try_get(targetIdentity->self));
            if (candidate != nullptr && (target == nullptr ||
                candidate->identity()->self.id < target->identity()->self.id)) target = candidate;
        }
        if (target == nullptr && strategy->targetBuildingDefinition.isValid()) {
            for (auto targetIt = targets.begin(); targetIt != targets.end(); ++targetIt) {
                auto [targetIdentity, definition, owner, placement, integrity] = *targetIt;
                (void)definition; (void)placement;
                if (!integrity->alive || FactionRelationSystem::allied(
                        dynamic_cast<Faction*>(owner->link.resolve()), faction)) continue;
                auto* candidate = dynamic_cast<Building*>(ecs::try_get(targetIdentity->self));
                if (candidate != nullptr && (target == nullptr ||
                    candidate->identity()->self.id < target->identity()->self.id)) target = candidate;
            }
        }
        if (target == nullptr) continue;
        CommandSpec attackMove;
        attackMove.kind = OrderKind::AttackMove;
        attackMove.target = {target->placement()->worldX, target->placement()->worldY};
        FormationSpec formation;
        formation.kind = FormationKind::Grid;
        formation.spacing = strategy->formationSpacing;
        auto issued = CommandFanOutSystem::fanOut(idleArmy, attackMove, formation);
        if (!issued) return failureFrom<std::size_t>(issued.status());
        const std::uint64_t group = static_cast<std::uint64_t>(identity->self.id) + 1u;
        for (const auto& handle : idleArmy) {
            auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
            if (unit != nullptr) unit->tactics()->combatGroup = group;
        }
        processed += issued.value().accepted;
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> MotionSystem::step(const SimulationStep& step) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS motion step delta must be non-negative",
                                    "step.delta");
    const double deltaSeconds = step.delta.seconds();
    std::size_t  processed    = 0;
    auto view = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Navigation, Unit::Orders, Unit::Combat,
                          Unit::Containment, Unit::Supply, Unit::Morale, Unit::Tactics, Unit::Command,
                          Unit::Effects>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, motion, navigation, orders, combat, containment, supply, morale, tactics, command,
              effects] = *it;
        Unit* unit = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        if (unit->crowd()->link.isBound() || containment->container.isBound()) continue;
        if (!std::isfinite(motion->x) || !std::isfinite(motion->y) || !std::isfinite(motion->speed) ||
            !std::isfinite(motion->arrivalRadius) || motion->speed < 0.0f || motion->arrivalRadius < 0.0f)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                        "RTS motion state must contain finite non-negative values", "unit.motion");

        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        if (!record) continue;
        if (!movementOrder(record->kind)) {
            motion->arrived = true;
            ++processed;
            continue;
        }
        if (navigation->trafficWaiting || supply->convoyWaiting ||
            (morale->retreating && tactics->retreatCovering)) {
            motion->arrived = false;
            ++processed;
            continue;
        }
        if (record->kind == OrderKind::AttackMove && combat->engagementRange > 0.0f) {
            auto engaged = entityPosition(combat->target);
            if (engaged && distanceSquared(motion->x, motion->y, engaged->x, engaged->y) <=
                               combat->engagementRange * combat->engagementRange) {
                motion->arrived = false;
                ++processed;
                continue;
            }
        }

        WorldPosition target = record->target;
        if (record->kind == OrderKind::Attack || record->kind == OrderKind::Resupply ||
            record->kind == OrderKind::SupplyRelay) {
            auto liveTarget = entityPosition(record->targetEntity);
            if (liveTarget) target = *liveTarget;
            if (record->kind == OrderKind::SupplyRelay && supply->rendezvousActive)
                target = supply->rendezvousPoint;
        } else if (record->kind == OrderKind::Escort && tactics->guardSet) {
            target = {tactics->guardX, tactics->guardY};
        } else if (record->kind == OrderKind::Patrol && navigation->patrolInitialized &&
                   !navigation->patrolTowardTarget) {
            target = navigation->patrolOrigin;
        }
        if (navigation->plannedOrderId == record->id && !navigation->unreachable &&
            navigation->waypointIndex < navigation->waypoints.size())
            target = navigation->waypoints[navigation->waypointIndex];
        const float dx       = target.x - motion->x;
        const float dy       = target.y - motion->y;
        const float distance = std::hypot(dx, dy);
        if (!std::isfinite(distance))
            return failure<std::size_t>(DiagnosticCode::InvariantViolation, "RTS motion target distance is non-finite",
                                        "order.target");
        float arrivalRadius = motion->arrivalRadius;
        if (record->kind == OrderKind::Attack)
            arrivalRadius = std::max(arrivalRadius, combat->engagementRange);
        else if (record->kind == OrderKind::Resupply || record->kind == OrderKind::SupplyRelay)
            arrivalRadius = std::max(arrivalRadius, supply->range * 0.8f);
        if (distance <= arrivalRadius) {
            if (record->kind != OrderKind::Attack) {
                motion->x = target.x;
                motion->y = target.y;
            }
            motion->arrived = true;
        } else if (deltaSeconds > 0.0 && motion->speed > 0.0f) {
            const float moraleFactor = morale->active ? std::clamp(morale->suppressedSpeedFactor, 0.0f, 1.0f) : 1.0f;
            const float commandFactor = command->requiresCommand && !command->inCommand
                                            ? command->outOfCommandSpeedFactor : 1.0f;
            const float effectFactor = static_cast<float>(effects->values.multiplier("speedMultiplier"));
            const float speedFactor = moraleFactor * commandFactor * effectFactor;
            const double travel = static_cast<double>(motion->speed * speedFactor) * deltaSeconds;
            if (!std::isfinite(travel))
                return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS motion travel distance is non-finite",
                                            "unit.motion.speed");
            const float amount = static_cast<float>(std::min<double>(travel, distance));
            motion->x += dx / distance * amount;
            motion->y += dy / distance * amount;
            motion->arrived = static_cast<double>(amount) >= distance - arrivalRadius;
            if (motion->arrived) {
                if (record->kind != OrderKind::Attack) {
                    motion->x = target.x;
                    motion->y = target.y;
                }
            }
        } else {
            motion->arrived = false;
        }
        ++processed;
    }
    return Result<std::size_t>::success(processed, Status::success(StatusCode::Applied));
}

Result<std::size_t> MovementOrderSystem::step() {
    std::size_t completedCount = 0;
    auto view = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Navigation, Unit::Orders,
                          Unit::Containment>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, motion, navigation, orders, containment] = *it;
        if (identity == nullptr)
            return failure<std::size_t>(DiagnosticCode::InvariantViolation,
                                        "RTS movement order candidate has no identity", "unit.identity");
        if (containment->container.isBound() || !motion->arrived || navigation->trafficWaiting) continue;
        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        if (!record || (record->kind != OrderKind::Move && record->kind != OrderKind::AttackMove)) continue;
        if (record->kind == OrderKind::AttackMove && orders->values.orderCount() <= 1) continue;
        const bool hasActivePath = navigation->plannedOrderId == record->id && !navigation->unreachable &&
                                   navigation->waypointIndex < navigation->waypoints.size();
        if (hasActivePath && navigation->waypointIndex + 1 < navigation->waypoints.size()) continue;
        auto completed = orders->values.complete(record->id);
        if (!completed) return failureFrom<std::size_t>(completed.status());
        navigation->waypoints.clear();
        navigation->waypointIndex = 0;
        navigation->plannedOrderId.clear();
        navigation->trafficWaiting = false;
        ++completedCount;
    }
    return Result<std::size_t>::success(completedCount,
        Status::success(completedCount == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> CommandStateSystem::step() {
    std::size_t processed = 0;
    auto view = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Navigation, Unit::Orders,
                          Unit::Combat, Unit::Containment>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, motion, navigation, orders, combat, containment] = *it;
        if (identity == nullptr)
            return failure<std::size_t>(DiagnosticCode::InvariantViolation,
                                        "RTS command-state candidate has no identity", "unit.identity");
        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        const bool holding = record && record->kind == OrderKind::HoldPosition;
        const bool attackMoving = record && record->kind == OrderKind::AttackMove;
        if (holding && !combat->holdPosition) {
            combat->guardX = motion->x;
            combat->guardY = motion->y;
            combat->guardSet = true;
        }
        combat->holdPosition = holding;
        combat->attackMove = attackMoving;
        if (!record || record->kind != OrderKind::Stop) continue;
        combat->target = {};
        combat->guardSet = false;
        navigation->waypoints.clear();
        navigation->waypointIndex = 0;
        navigation->plannedOrderId.clear();
        navigation->trafficWaiting = false;
        navigation->patrolInitialized = false;
        motion->arrived = true;
        auto completed = orders->values.complete(record->id);
        if (!completed) return failureFrom<std::size_t>(completed.status());
        ++processed;
        (void)containment;
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> NavigationSystem::step(map::Pathfinder& pathfinder, const NavigationGrid& grid,
                                           const NavigationEvent& unreachable) {
    if (!std::isfinite(grid.cellSize) || grid.cellSize <= 0.0f || !std::isfinite(grid.originX) ||
        !std::isfinite(grid.originY))
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                    "RTS navigation grid requires finite origin and positive cell size",
                                    "navigation.grid");
    const auto worldToCell = [&](float value, float origin) {
        return static_cast<int>(std::lround((value - origin) / grid.cellSize));
    };
    const auto cellToWorld = [&](int value, float origin) {
        return origin + static_cast<float>(value) * grid.cellSize;
    };
    std::size_t processed = 0;
    auto view = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Navigation, Unit::Orders,
                          Unit::Containment, Unit::Tactics, Unit::Supply, Unit::Faction>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, motion, navigation, orders, containment, tactics, supply, faction] = *it;
        auto* unit = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        if (containment->container.isBound()) continue;
        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        if (!record || !movementOrder(record->kind)) {
            navigation->waypoints.clear();
            navigation->waypointIndex = 0;
            navigation->plannedOrderId.clear();
            navigation->unreachable = false;
            navigation->unreachableReported = false;
            navigation->trafficWaiting = false;
            navigation->patrolInitialized = false;
            navigation->patrolTowardTarget = true;
            supply->routeThreat = 0.0f;
            supply->routeAvoidedThreat = false;
            continue;
        }
        WorldPosition goal = record->target;
        if (record->kind == OrderKind::Attack || record->kind == OrderKind::Resupply ||
            record->kind == OrderKind::SupplyRelay) {
            if (auto target = entityPosition(record->targetEntity)) goal = *target;
            if (record->kind == OrderKind::SupplyRelay && supply->rendezvousActive)
                goal = supply->rendezvousPoint;
        } else if (record->kind == OrderKind::Escort && tactics->guardSet) {
            goal = {tactics->guardX, tactics->guardY};
        } else if (record->kind == OrderKind::Patrol && navigation->patrolInitialized &&
                   !navigation->patrolTowardTarget) {
            goal = navigation->patrolOrigin;
        }
        const bool supplyMission = record->kind == OrderKind::Resupply ||
                                   record->kind == OrderKind::SupplyRelay;
        auto* viewer = dynamic_cast<Faction*>(faction->link.resolve());
        float currentRouteThreat = 0.0f;
        if (supplyMission && viewer != nullptr) {
            WorldPosition previous{motion->x, motion->y};
            for (std::size_t index = navigation->waypointIndex; index < navigation->waypoints.size(); ++index) {
                const WorldPosition point = navigation->waypoints[index];
                currentRouteThreat += visibleHostileThreatAt(*viewer, point) *
                                      std::hypot(point.x - previous.x, point.y - previous.y);
                previous = point;
            }
        }
        const bool threatChanged = supplyMission && navigation->plannedOrderId == record->id &&
                                   currentRouteThreat > supply->routeThreat + 1e-4f;
        const bool changed = navigation->plannedOrderId != record->id || threatChanged ||
                             distanceSquared(goal.x, goal.y, navigation->plannedGoal.x,
                                             navigation->plannedGoal.y) > grid.cellSize * grid.cellSize * 0.25f;
        if (changed) {
            navigation->waypoints.clear();
            navigation->waypointIndex = 0;
            navigation->plannedOrderId = record->id;
            navigation->plannedGoal = goal;
            navigation->unreachable = false;
            navigation->unreachableReported = false;
            const int startX = worldToCell(motion->x, grid.originX);
            const int startY = worldToCell(motion->y, grid.originY);
            const int goalX = worldToCell(goal.x, grid.originX);
            const int goalY = worldToCell(goal.y, grid.originY);
            std::unique_ptr<map::Path> baseline;
            std::unique_ptr<map::Path> path;
            if (supplyMission) {
                baseline.reset(pathfinder.findPath(startX, startY, goalX, goalY));
                path.reset(pathfinder.findPath(startX, startY, goalX, goalY,
                    [&](int x, int y) {
                        if (viewer == nullptr) return 0.0f;
                        const WorldPosition point{cellToWorld(x, grid.originX),
                                                  cellToWorld(y, grid.originY)};
                        return visibleHostileThreatAt(*viewer, point) * 0.5f;
                    }));
                const auto exposure = [&](const map::Path* candidate) {
                    if (candidate == nullptr || candidate->empty() || viewer == nullptr) return 0.0f;
                    float total = 0.0f;
                    WorldPosition previous{motion->x, motion->y};
                    for (int index = 1; index < candidate->getLength(); ++index) {
                        const WorldPosition point{cellToWorld(candidate->getX(index), grid.originX),
                                                  cellToWorld(candidate->getY(index), grid.originY)};
                        total += visibleHostileThreatAt(*viewer, point) *
                                 std::hypot(point.x - previous.x, point.y - previous.y);
                        previous = point;
                    }
                    return total;
                };
                supply->routeThreat = exposure(path.get());
                supply->routeAvoidedThreat = baseline != nullptr && !baseline->empty() &&
                    supply->routeThreat + 1e-4f < exposure(baseline.get());
            } else {
                path.reset(pathfinder.findPath(startX, startY, goalX, goalY));
                supply->routeThreat = 0.0f;
                supply->routeAvoidedThreat = false;
            }
            if (path == nullptr || path->empty()) {
                navigation->unreachable = true;
            } else {
                navigation->waypoints.reserve(static_cast<std::size_t>(path->getLength()));
                for (int index = 1; index < path->getLength(); ++index)
                    navigation->waypoints.push_back({cellToWorld(path->getX(index), grid.originX),
                                                     cellToWorld(path->getY(index), grid.originY)});
            }
        } else if (supplyMission) {
            supply->routeThreat = currentRouteThreat;
        }
        while (navigation->waypointIndex < navigation->waypoints.size()) {
            const auto waypoint = navigation->waypoints[navigation->waypointIndex];
            const float radius = std::max(motion->arrivalRadius, grid.cellSize * 0.1f);
            if (distanceSquared(motion->x, motion->y, waypoint.x, waypoint.y) > radius * radius) break;
            ++navigation->waypointIndex;
        }
        if (navigation->unreachable && !navigation->unreachableReported) {
            navigation->unreachableReported = true;
            if (unreachable) unreachable(*unit, *record);
        }
        ++processed;
    }
    return Result<std::size_t>::success(processed,
                                        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> PatrolSystem::step() {
    std::size_t processed = 0;
    auto view = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Navigation, Unit::Orders,
                          Unit::Containment>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, motion, navigation, orders, containment] = *it;
        if (identity == nullptr)
            return failure<std::size_t>(DiagnosticCode::InvariantViolation,
                                        "RTS patrol candidate has no identity", "unit.identity");
        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        if (!record || record->kind != OrderKind::Patrol || containment->container.isBound()) {
            navigation->patrolInitialized = false;
            navigation->patrolTowardTarget = true;
            continue;
        }
        if (!navigation->patrolInitialized) {
            navigation->patrolOrigin = {motion->x, motion->y};
            navigation->patrolInitialized = true;
            navigation->patrolTowardTarget = true;
        }
        const WorldPosition endpoint = navigation->patrolTowardTarget ? record->target : navigation->patrolOrigin;
        const float radius = std::max(motion->arrivalRadius, 0.01f);
        if (distanceSquared(motion->x, motion->y, endpoint.x, endpoint.y) <= radius * radius) {
            navigation->patrolTowardTarget = !navigation->patrolTowardTarget;
            navigation->plannedOrderId.clear();
            navigation->waypoints.clear();
            navigation->waypointIndex = 0;
            navigation->unreachable = false;
            navigation->unreachableReported = false;
            motion->arrived = false;
        }
        ++processed;
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> TrafficReservationSystem::step(const map::Pathfinder& pathfinder,
                                                    const NavigationGrid& grid) {
    if (!std::isfinite(grid.cellSize) || grid.cellSize <= 0.0f || !std::isfinite(grid.originX) ||
        !std::isfinite(grid.originY))
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                    "RTS traffic grid requires finite origin and positive cell size",
                                    "traffic.grid");
    struct Candidate {
        Unit::Navigation* navigation = nullptr;
        SubjectRef subject;
        int priority = 0;
    };
    std::map<std::pair<int, int>, std::vector<Candidate>> contenders;
    std::size_t processed = 0;
    auto view = ecs::View<Unit, Unit::Identity, Unit::Navigation, Unit::Orders, Unit::Containment>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, navigation, orders, containment] = *it;
        if (identity == nullptr)
            return failure<std::size_t>(DiagnosticCode::InvariantViolation,
                                        "RTS traffic candidate has no identity", "unit.identity");
        navigation->trafficWaiting = false;
        if (containment->container.isBound()) continue;
        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        if (!record || !movementOrder(record->kind) || navigation->unreachable ||
            navigation->plannedOrderId != record->id || navigation->waypointIndex >= navigation->waypoints.size())
            continue;
        ++processed;
        const auto waypoint = navigation->waypoints[navigation->waypointIndex];
        const int x = static_cast<int>(std::lround((waypoint.x - grid.originX) / grid.cellSize));
        const int y = static_cast<int>(std::lround((waypoint.y - grid.originY) / grid.cellSize));
        static constexpr int dx[] = {1, -1, 0, 0};
        static constexpr int dy[] = {0, 0, 1, -1};
        int exits = 0;
        for (int direction = 0; direction < 4; ++direction)
            if (pathfinder.isWalkable(x + dx[direction], y + dy[direction])) ++exits;
        if (exits <= 2)
            contenders[{x, y}].push_back({navigation, identity->subject, navigation->movementPriority});
    }
    for (auto& [cell, values] : contenders) {
        (void)cell;
        if (values.size() < 2) continue;
        std::sort(values.begin(), values.end(), [](const Candidate& left, const Candidate& right) {
            if (left.priority != right.priority) return left.priority > right.priority;
            return left.subject.format() < right.subject.format();
        });
        for (std::size_t index = 1; index < values.size(); ++index)
            values[index].navigation->trafficWaiting = true;
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

void FogOfWarSystem::clear(State& state) noexcept {
    for (const auto& binding : state.bindings)
        if (binding.provider != nullptr && binding.revealer >= 0)
            binding.provider->removeRevealer(binding.revealer);
    state.bindings.clear();
}

const Faction::Intel::Contact* FogOfWarSystem::contact(const Faction& faction, SubjectRef subject) noexcept {
    const auto& contacts = const_cast<Faction&>(faction).intel()->contacts;
    const auto it = std::find_if(contacts.begin(), contacts.end(),
                                 [&](const auto& value) { return value.subject == subject; });
    return it != contacts.end() && it->subject == subject ? &*it : nullptr;
}

Result<std::size_t> FogOfWarSystem::step(const SimulationStep& step, const NavigationGrid& grid, State& state,
                                         const FogProvider& provider) {
    if (!provider)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS fog provider is required", "fog.provider");
    if (step.delta.nanoseconds() < 0 || !std::isfinite(grid.cellSize) || grid.cellSize <= 0.0f)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS fog step/grid is invalid", "fog.grid");
    struct Source {
        ecs::EntityHandle handle{};
        ecs::EntityHandle faction{};
        map::Fov* fov = nullptr;
        WorldPosition position;
        float sight = 0.0f;
        float detectionRange = 0.0f;
        float detectionStrength = 0.0f;
        float radarRange = 0.0f;
        float radarResolution = 0.0f;
        float jammingRange = 0.0f;
    };
    std::vector<Source> sources;
    auto addSource = [&](ecs::EntityHandle handle, const FactionLink& factionLink, WorldPosition position,
                         float sight, float detectionRange, float detectionStrength, float radarRange,
                         float radarResolution, float jammingRange, bool enabled) -> Result<void> {
        if (!enabled || (sight <= 0.0f && radarRange <= 0.0f && jammingRange <= 0.0f))
            return Result<void>::success(Status::success(StatusCode::NoOp));
        if (!finitePosition(position) || !std::isfinite(sight) || !std::isfinite(detectionRange) ||
            !std::isfinite(detectionStrength) || !std::isfinite(radarRange) ||
            !std::isfinite(radarResolution) || !std::isfinite(jammingRange) || sight < 0.0f ||
            detectionRange < 0.0f || detectionStrength < 0.0f || radarRange < 0.0f ||
            radarResolution < 0.0f || jammingRange < 0.0f)
            return failure<void>(DiagnosticCode::InvalidArgument, "RTS vision values must be finite and non-negative",
                                 "vision");
        auto* faction = dynamic_cast<Faction*>(factionLink.resolve());
        if (faction == nullptr) return Result<void>::success(Status::success(StatusCode::NoOp));
        map::Fov* fov = provider(*faction);
        if (fov == nullptr) return Result<void>::success(Status::success(StatusCode::NoOp));
        sources.push_back({handle, ecs::handle_of(faction), fov, position, sight, detectionRange,
                           detectionStrength, radarRange, radarResolution, jammingRange});
        return Result<void>::success(Status::success(StatusCode::Applied));
    };
    auto units = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Faction, Unit::Vision, Unit::Containment>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [identity, motion, faction, vision, containment] = *it;
        if (containment->container.isBound()) continue;
        auto added = addSource(identity->self, faction->link, {motion->x, motion->y}, vision->sightRange,
                               vision->detectionRange, vision->detectionStrength, vision->radarRange,
                               vision->radarResolution, vision->jammingRange, vision->enabled);
        if (!added) return failureFrom<std::size_t>(added.status());
    }
    auto buildings = ecs::View<Building, Building::Identity, Building::Placement, Building::Faction,
                               Building::Vision, Building::Construction>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [identity, placement, faction, vision, construction] = *it;
        if (!placement->placed || construction->progress < 1.0f) continue;
        auto added = addSource(identity->self, faction->link, {placement->worldX, placement->worldY},
                               vision->sightRange, vision->detectionRange, vision->detectionStrength,
                               vision->radarRange, vision->radarResolution, vision->jammingRange,
                               vision->enabled);
        if (!added) return failureFrom<std::size_t>(added.status());
    }
    state.bindings.erase(std::remove_if(state.bindings.begin(), state.bindings.end(), [&](const auto& binding) {
        const bool retained = std::any_of(sources.begin(), sources.end(), [&](const Source& source) {
            return source.sight > 0.0f && sameHandle(source.handle, binding.source) && source.fov == binding.provider;
        });
        if (!retained && binding.provider != nullptr && binding.revealer >= 0)
            binding.provider->removeRevealer(binding.revealer);
        return !retained;
    }), state.bindings.end());
    const auto worldToCell = [&](float value, float origin) {
        return static_cast<int>(std::lround((value - origin) / grid.cellSize));
    };
    std::vector<map::Fov*> providers;
    for (const auto& source : sources) {
        if (source.sight <= 0.0f) continue;
        auto binding = std::find_if(state.bindings.begin(), state.bindings.end(), [&](const auto& value) {
            return sameHandle(value.source, source.handle) && value.provider == source.fov;
        });
        const int x = worldToCell(source.position.x, grid.originX);
        const int y = worldToCell(source.position.y, grid.originY);
        const int radius = std::max(0, static_cast<int>(std::ceil(source.sight / grid.cellSize)));
        if (binding == state.bindings.end()) {
            const int revealer = source.fov->addRevealer(x, y, radius);
            state.bindings.push_back({source.handle, source.faction, source.fov, revealer});
        } else {
            source.fov->setRevealerPosition(binding->revealer, x, y);
            source.fov->setRevealerRadius(binding->revealer, radius);
            binding->faction = source.faction;
        }
        if (std::find(providers.begin(), providers.end(), source.fov) == providers.end())
            providers.push_back(source.fov);
    }
    for (auto* fov : providers) fov->compute();

    auto factions = ecs::View<Faction, Faction::Identity, Faction::Intel>();
    for (auto fit = factions.begin(); fit != factions.end(); ++fit) {
        auto [identity, intel] = *fit;
        auto* viewer = dynamic_cast<Faction*>(ecs::try_get(identity->self));
        map::Fov* fov = viewer == nullptr ? nullptr : provider(*viewer);
        intel->enabled = fov != nullptr;
        for (auto& value : intel->contacts) {
            value.ageSeconds += step.delta.seconds();
            value.visible = false;
            value.detected = false;
        }
        if (fov == nullptr) continue;
        auto observe = [&](SubjectRef subject, std::string_view kind, ecs::EntityHandle targetFaction,
                           WorldPosition position, bool cloaked, float stealth) {
            auto* enemyFaction = dynamic_cast<Faction*>(ecs::try_get(targetFaction));
            if (enemyFaction == nullptr || enemyFaction == viewer || provider(*enemyFaction) == fov) return;
            const int x = worldToCell(position.x, grid.originX);
            const int y = worldToCell(position.y, grid.originY);
            bool detected = !cloaked;
            if (cloaked && fov->isVisible(x, y)) {
                for (const auto& source : sources) {
                    if (source.fov != fov || source.detectionRange <= 0.0f ||
                        source.detectionStrength < stealth) continue;
                    if (distanceSquared(source.position.x, source.position.y, position.x, position.y) <=
                        source.detectionRange * source.detectionRange) {
                        detected = true;
                        break;
                    }
                }
            }
            const bool visible = fov->isVisible(x, y) && detected;
            auto found = std::lower_bound(intel->contacts.begin(), intel->contacts.end(), subject,
                                          [](const auto& value, SubjectRef key) {
                                              return value.subject.format() < key.format();
                                          });
            if (!visible) {
                const bool jammed = std::any_of(sources.begin(), sources.end(), [&](const Source& jammer) {
                    if (jammer.jammingRange <= 0.0f || jammer.fov == fov) return false;
                    return distanceSquared(jammer.position.x, jammer.position.y, position.x, position.y) <=
                           jammer.jammingRange * jammer.jammingRange;
                });
                const Source* radar = nullptr;
                if (!jammed) {
                    for (const auto& candidate : sources) {
                        if (candidate.fov != fov || candidate.radarRange <= 0.0f) continue;
                        if (distanceSquared(candidate.position.x, candidate.position.y, position.x, position.y) <=
                            candidate.radarRange * candidate.radarRange) {
                            radar = &candidate;
                            break;
                        }
                    }
                }
                if (radar == nullptr) return;
                const float resolution = radar->radarResolution > 0.0f ? radar->radarResolution : grid.cellSize;
                const WorldPosition quantized{
                    grid.originX + (std::floor((position.x - grid.originX) / resolution) + 0.5f) * resolution,
                    grid.originY + (std::floor((position.y - grid.originY) / resolution) + 0.5f) * resolution};
                const std::string radarKind = kind == "unit" ? "radar_unit" : "radar_building";
                if (found == intel->contacts.end() || found->subject != subject)
                    intel->contacts.insert(found, {subject, radarKind, quantized, 0.0, false, false});
                else {
                    found->position = quantized;
                    found->ageSeconds = 0.0;
                    found->visible = false;
                    found->detected = false;
                    found->kind = radarKind;
                }
                return;
            }
            if (found == intel->contacts.end() || found->subject != subject)
                found = intel->contacts.insert(found, {subject, std::string(kind), position, 0.0, true, detected});
            else {
                found->position = position;
                found->ageSeconds = 0.0;
                found->visible = true;
                found->detected = detected;
                found->kind = kind;
            }
        };
        for (auto it = units.begin(); it != units.end(); ++it) {
            auto [targetIdentity, motion, faction, vision, containment] = *it;
            if (!containment->container.isBound())
                observe(targetIdentity->subject, "unit", faction->link.handle(), {motion->x, motion->y},
                        vision->cloaked, vision->stealth);
        }
        for (auto it = buildings.begin(); it != buildings.end(); ++it) {
            auto [targetIdentity, placement, faction, vision, construction] = *it;
            if (placement->placed)
                observe(targetIdentity->subject, "building", faction->link.handle(),
                        {placement->worldX, placement->worldY}, false, 0.0f);
        }
    }
    return Result<std::size_t>::success(sources.size(),
        Status::success(sources.empty() ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> CrowdMotionSystem::step(const SimulationStep& step, crowd::Crowd& crowd) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS crowd step delta must be non-negative",
                                    "step.delta");
    struct LinkedUnit {
        Unit* unit;
        Unit::Motion* motion;
        Unit::Navigation* navigation;
        Unit::Crowd* settings;
        Unit::Combat* combat;
        Unit::Supply* supply;
        Unit::Morale* morale;
        Unit::Tactics* tactics;
        Unit::Command* command;
        Unit::Effects* effects;
        OrderComponent* orders;
    };
    std::vector<LinkedUnit> linked;
    std::vector<std::string> keys;
    auto view = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Navigation, Unit::Orders, Unit::Crowd, Unit::Combat,
                          Unit::Containment, Unit::Supply, Unit::Morale, Unit::Tactics, Unit::Command,
                          Unit::Effects>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, motion, navigation, orders, settings, combat, containment, supply, morale, tactics,
              command, effects] = *it;
        auto* unit = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        if (!settings->link.isBound() || containment->container.isBound()) continue;
        const std::string& key = settings->link.key();
        if (std::find(keys.begin(), keys.end(), key) != keys.end())
            return failure<std::size_t>(DiagnosticCode::Conflict, "RTS crowd agent keys must be unique",
                                        "unit.crowd.link");
        keys.push_back(key);
        linked.push_back({unit, motion, navigation, settings, combat, supply, morale, tactics, command, effects,
                          &orders->values});
    }
    if (linked.empty())
        return Result<std::size_t>::success(0, Status::success(StatusCode::NoOp));

    for (const auto& entry : linked) {
        const std::string& key = entry.settings->link.key();
        int agent = crowd.getNamedAgentIndex(key);
        if (agent < 0) agent = crowd.addNamedAgent(key, entry.motion->x, entry.motion->y,
                                                  entry.settings->heading, entry.settings->radius);
        if (agent < 0)
            return failure<std::size_t>(DiagnosticCode::Failed, "canonical Crowd rejected an RTS agent",
                                        "unit.crowd.link");
        crowd.setAgentPosition(agent, entry.motion->x, entry.motion->y);
        crowd.setAgentRadius(agent, entry.settings->radius);
        crowd.setAgentAvoidancePriority(agent, entry.navigation->movementPriority);
        const float speedFactor = entry.morale->active
                                      ? std::clamp(entry.morale->suppressedSpeedFactor, 0.0f, 1.0f) : 1.0f;
        const float commandFactor = entry.command->requiresCommand && !entry.command->inCommand
                                        ? entry.command->outOfCommandSpeedFactor : 1.0f;
        const float effectFactor = static_cast<float>(entry.effects->values.multiplier("speedMultiplier"));
        crowd.setAgentSpeed(agent, entry.motion->speed * speedFactor * commandFactor * effectFactor);
        auto current = readCurrent(*entry.orders);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        bool attackMoveEngaged = false;
        if (record && record->kind == OrderKind::AttackMove && entry.combat->engagementRange > 0.0f) {
            auto engaged = entityPosition(entry.combat->target);
            attackMoveEngaged = engaged &&
                distanceSquared(entry.motion->x, entry.motion->y, engaged->x, engaged->y) <=
                    entry.combat->engagementRange * entry.combat->engagementRange;
        }
        if (record && movementOrder(record->kind) && !entry.navigation->trafficWaiting &&
            !(entry.morale->retreating && entry.tactics->retreatCovering) && !attackMoveEngaged) {
            WorldPosition target = record->target;
            if (record->kind == OrderKind::Attack || record->kind == OrderKind::Resupply ||
                record->kind == OrderKind::SupplyRelay) {
                auto liveTarget = entityPosition(record->targetEntity);
                if (liveTarget) target = *liveTarget;
            } else if (record->kind == OrderKind::Escort && entry.tactics->guardSet) {
                target = {entry.tactics->guardX, entry.tactics->guardY};
            } else if (record->kind == OrderKind::Patrol && entry.navigation->patrolInitialized &&
                       !entry.navigation->patrolTowardTarget) {
                target = entry.navigation->patrolOrigin;
            }
            if (entry.navigation->plannedOrderId == record->id && !entry.navigation->unreachable &&
                entry.navigation->waypointIndex < entry.navigation->waypoints.size())
                target = entry.navigation->waypoints[entry.navigation->waypointIndex];
            crowd.setAgentTarget(agent, target.x, target.y);
            crowd.setAgentAction(agent, "seek");
        } else {
            crowd.clearAgentTarget(agent);
            crowd.setAgentAction(agent, "idle");
        }
    }
    crowd.step(static_cast<float>(step.delta.seconds()));
    for (const auto& entry : linked) {
        const int agent = crowd.getNamedAgentIndex(entry.settings->link.key());
        const auto state = crowd.getAgentState(agent);
        if (state.action < 0)
            return failure<std::size_t>(DiagnosticCode::InvariantViolation, "canonical Crowd lost an RTS agent",
                                        "unit.crowd.link");
        entry.motion->x = state.x;
        entry.motion->y = state.y;
        entry.settings->heading = state.heading;
        auto current = readCurrent(*entry.orders);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        if (!record || !movementOrder(record->kind)) {
            entry.motion->arrived = true;
            continue;
        }
        WorldPosition target = record->target;
        if (record->kind == OrderKind::Attack || record->kind == OrderKind::Resupply ||
            record->kind == OrderKind::SupplyRelay) {
            auto liveTarget = entityPosition(record->targetEntity);
            if (liveTarget) target = *liveTarget;
        } else if (record->kind == OrderKind::Escort && entry.tactics->guardSet) {
            target = {entry.tactics->guardX, entry.tactics->guardY};
        }
        const float remaining = std::sqrt(distanceSquared(state.x, state.y, target.x, target.y));
        float arrivalRadius = entry.motion->arrivalRadius;
        if (record->kind == OrderKind::Attack)
            arrivalRadius = std::max(arrivalRadius, entry.combat->engagementRange);
        else if (record->kind == OrderKind::Resupply || record->kind == OrderKind::SupplyRelay)
            arrivalRadius = std::max(arrivalRadius, entry.supply->range * 0.8f);
        entry.motion->arrived = remaining <= arrivalRadius;
        if (entry.motion->arrived) {
            if (record->kind != OrderKind::Attack) {
                entry.motion->x = target.x;
                entry.motion->y = target.y;
            }
            crowd.setAgentPosition(agent, entry.motion->x, entry.motion->y);
            crowd.setAgentAction(agent, "idle");
        }
    }
    return Result<std::size_t>::success(linked.size(), Status::success(StatusCode::Applied));
}

Result<std::size_t> WorkerAssignmentSystem::step() {
    struct Candidate {
        ResourceNode* node = nullptr;
        float distance = 0.0f;
    };
    std::size_t assigned = 0;
    auto workers = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders, Unit::Worker>();
    for (auto it = workers.begin(); it != workers.end(); ++it) {
        auto [identity, motion, orders, worker] = *it;
        auto* unit = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        if (!worker->autoAssign || worker->capacity <= 0.0f || worker->gatherRate <= 0.0f ||
            worker->resourceType.empty() || !orders->values.empty() || !worker->dropoff.isBound() ||
            worker->dropoff.isStale())
            continue;

        std::vector<Candidate> candidates;
        auto nodes = ecs::View<ResourceNode, ResourceNode::Identity, ResourceNode::Position, ResourceNode::Stock,
                               ResourceNode::Harvest>();
        for (auto nodeIt = nodes.begin(); nodeIt != nodes.end(); ++nodeIt) {
            auto [nodeIdentity, position, stock, harvest] = *nodeIt;
            auto* node = nodeIdentity == nullptr
                             ? nullptr
                             : dynamic_cast<ResourceNode*>(ecs::try_get(nodeIdentity->self));
            if (node == nullptr || stock->resourceType != worker->resourceType ||
                (!stock->infinite && stock->remaining <= 0.0f))
                continue;
            harvest->workers.erase(
                std::remove_if(harvest->workers.begin(), harvest->workers.end(),
                               [](const ecs::EntityHandle& handle) { return ecs::try_get(handle) == nullptr; }),
                harvest->workers.end());
            if (harvest->workers.size() >= harvest->capacity) continue;
            candidates.push_back({node, distanceSquared(motion->x, motion->y, position->x, position->y)});
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
            if (left.distance != right.distance) return left.distance < right.distance;
            return left.node->identity()->self.id < right.node->identity()->self.id;
        });
        if (candidates.empty()) continue;
        ResourceNode* node = candidates.front().node;
        auto link = ResourceNodeLink::bind(ecs::handle_of(node));
        if (!link) return failureFrom<std::size_t>(link.status());
        worker->resourceNode = std::move(link).takeValue();
        node->harvest()->workers.push_back(ecs::handle_of(unit));
        CommandSpec command;
        command.kind         = OrderKind::Gather;
        command.target       = {node->position()->x, node->position()->y};
        command.targetEntity = ecs::handle_of(node);
        auto queued = orders->values.enqueue(command);
        if (!queued) return failureFrom<std::size_t>(queued.status());
        std::move(queued).takeValue();
        ++assigned;
    }
    return Result<std::size_t>::success(assigned,
                                        Status::success(assigned == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> MiningSystem::step(const SimulationStep& step, const ResourceCredit& credit) {
    if (step.delta.nanoseconds() < 0 || !credit)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                    "RTS mining requires non-negative time and a resource credit callback", "mining");
    std::size_t processed = 0;
    auto workers = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders, Unit::Worker>();
    for (auto it = workers.begin(); it != workers.end(); ++it) {
        auto [identity, motion, orders, worker] = *it;
        auto* unit = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        if (!record) continue;

        if (record->kind == OrderKind::Gather && motion->arrived) {
            auto* node = dynamic_cast<ResourceNode*>(worker->resourceNode.resolve());
            if (node == nullptr) {
                auto failed = orders->values.fail(record->id, "resource node is stale");
                if (!failed) return failureFrom<std::size_t>(failed.status());
                worker->resourceNode.reset();
                continue;
            }
            const float room = std::max(0.0f, worker->capacity - worker->cargo);
            float amount = std::min(room, worker->gatherRate * static_cast<float>(step.delta.seconds()));
            if (!node->stock()->infinite) amount = std::min(amount, node->stock()->remaining);
            worker->cargo += amount;
            if (!node->stock()->infinite) node->stock()->remaining -= amount;
            if (worker->cargo >= worker->capacity || (!node->stock()->infinite && node->stock()->remaining <= 0.0f)) {
                auto* dropoff = dynamic_cast<Building*>(worker->dropoff.resolve());
                if (dropoff == nullptr) {
                    auto failed = orders->values.fail(record->id, "dropoff is stale");
                    if (!failed) return failureFrom<std::size_t>(failed.status());
                    continue;
                }
                auto completed = orders->values.complete(record->id);
                if (!completed) return failureFrom<std::size_t>(completed.status());
                CommandSpec returning;
                returning.kind         = OrderKind::ReturnCargo;
                returning.target       = {dropoff->placement()->worldX, dropoff->placement()->worldY};
                returning.targetEntity = ecs::handle_of(dropoff);
                auto queued = orders->values.enqueue(returning);
                if (!queued) return failureFrom<std::size_t>(queued.status());
                std::move(queued).takeValue();
            }
            ++processed;
        } else if (record->kind == OrderKind::ReturnCargo && motion->arrived && worker->cargo >= 1.0f) {
            const auto whole = static_cast<std::int64_t>(std::floor(worker->cargo));
            auto cost = resource::CostSpec::single(worker->resourceType, whole);
            if (!cost) return failureFrom<std::size_t>(cost.status());
            auto credited = credit(*unit, cost.value());
            if (!credited) return failureFrom<std::size_t>(credited.status());
            worker->cargo -= static_cast<float>(whole);
            auto completed = orders->values.complete(record->id);
            if (!completed) return failureFrom<std::size_t>(completed.status());
            auto* node = dynamic_cast<ResourceNode*>(worker->resourceNode.resolve());
            if (node != nullptr && (node->stock()->infinite || node->stock()->remaining > 0.0f)) {
                CommandSpec gather;
                gather.kind         = OrderKind::Gather;
                gather.target       = {node->position()->x, node->position()->y};
                gather.targetEntity = ecs::handle_of(node);
                auto queued = orders->values.enqueue(gather);
                if (!queued) return failureFrom<std::size_t>(queued.status());
                std::move(queued).takeValue();
            } else {
                worker->resourceNode.reset();
            }
            ++processed;
        }
    }
    return Result<std::size_t>::success(processed,
                                        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> ConstructionSystem::step(const SimulationStep& step, const LifecycleEventSink& events) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                    "RTS construction step delta must be non-negative", "step.delta");
    std::size_t processed = 0;
    auto buildings = ecs::View<Building, Building::Identity, Building::Construction, Building::Faction>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [identity, construction, faction] = *it;
        auto* building = identity == nullptr ? nullptr : dynamic_cast<Building*>(ecs::try_get(identity->self));
        if (building == nullptr || &*building->identity() != identity) continue;
        if (!std::isfinite(construction->progress) || !std::isfinite(construction->buildTimeSeconds) ||
            construction->buildTimeSeconds < 0.0f)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument, "invalid RTS construction state",
                                        "building.construction");
        if (construction->progress >= 1.0f || construction->paused) continue;

        construction->builders.clear();
        if (events)
            events({LifecycleEventKind::ConstructionCompleted, identity->subject, {},
                    building->definition()->id.format(), 1.0}, step.tick);
        float totalRate = 0.0f;
        auto units = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders, Unit::Worker, Unit::Faction>();
        for (auto unitIt = units.begin(); unitIt != units.end(); ++unitIt) {
            auto [unitIdentity, motion, orders, worker, unitFaction] = *unitIt;
            auto* unit = unitIdentity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(unitIdentity->self));
            if (unit == nullptr || !motion->arrived || worker->buildRate <= 0.0f ||
                unitFaction->link.resolve() != faction->link.resolve())
                continue;
            auto current = readCurrent(orders->values);
            if (!current) return failureFrom<std::size_t>(current.status());
            auto record = std::move(current).takeValue();
            if (!record || record->kind != OrderKind::Build || !sameHandle(record->targetEntity, identity->self))
                continue;
            construction->builders.push_back(unitIdentity->self);
            totalRate += worker->buildRate;
        }
        if (totalRate <= 0.0f) continue;
        if (construction->buildTimeSeconds == 0.0f) construction->progress = 1.0f;
        else construction->progress = std::min(1.0f, construction->progress +
            totalRate * static_cast<float>(step.delta.seconds()) / construction->buildTimeSeconds);
        ++processed;
        if (construction->progress < 1.0f) continue;
        for (const auto& builderHandle : construction->builders) {
            auto* unit = dynamic_cast<Unit*>(ecs::try_get(builderHandle));
            if (unit == nullptr) continue;
            auto current = readCurrent(unit->orders()->values);
            if (!current) return failureFrom<std::size_t>(current.status());
            auto record = std::move(current).takeValue();
            if (record && record->kind == OrderKind::Build && sameHandle(record->targetEntity, identity->self)) {
                auto completed = unit->orders()->values.complete(record->id);
                if (!completed) return failureFrom<std::size_t>(completed.status());
            }
        }
        construction->builders.clear();
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> WorkforceAssignmentSystem::step() {
    struct WorkerCandidate { Unit* unit; };
    struct Target { Building* building; OrderKind kind; std::size_t limit; };
    std::size_t processed = 0;
    auto factions = ecs::View<Faction, Faction::Identity, Faction::Workforce>();
    for (auto factionIt = factions.begin(); factionIt != factions.end(); ++factionIt) {
        auto [factionIdentity, policy] = *factionIt;
        if (!policy->autoConstruction && !policy->autoRepair) continue;
        if (policy->maxBuildersPerSite == 0 || policy->maxRepairersPerBuilding == 0)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                        "RTS workforce assignment limits must be positive", "faction.workforce");
        std::vector<WorkerCandidate> idle;
        auto units = ecs::View<Unit, Unit::Identity, Unit::Orders, Unit::Worker, Unit::Faction,
                               Unit::Containment, Unit::Durability>();
        for (auto it = units.begin(); it != units.end(); ++it) {
            auto [identity, orders, worker, faction, containment, durability] = *it;
            if (!durability->alive || containment->container.isBound() || !orders->values.empty() ||
                faction->link.resolve() != ecs::try_get(factionIdentity->self) ||
                (worker->buildRate <= 0.0f && worker->repairRate <= 0.0f)) continue;
            if (auto* unit = dynamic_cast<Unit*>(ecs::try_get(identity->self))) idle.push_back({unit});
        }
        std::size_t budget = idle.size() > policy->reserveWorkers ? idle.size() - policy->reserveWorkers : 0;
        if (budget == 0) continue;

        std::vector<Target> targets;
        auto buildings = ecs::View<Building, Building::Identity, Building::Faction, Building::Construction,
                                   Building::Integrity>();
        for (auto it = buildings.begin(); it != buildings.end(); ++it) {
            auto [identity, faction, construction, integrity] = *it;
            if (faction->link.resolve() != ecs::try_get(factionIdentity->self) || !integrity->alive) continue;
            auto* building = dynamic_cast<Building*>(ecs::try_get(identity->self));
            if (building == nullptr) continue;
            if (policy->autoConstruction && construction->progress < 1.0f && !construction->paused)
                targets.push_back({building, OrderKind::Build, policy->maxBuildersPerSite});
            else if (policy->autoRepair && construction->progress >= 1.0f &&
                     integrity->state.health < integrity->state.maxHealth)
                targets.push_back({building, OrderKind::Repair, policy->maxRepairersPerBuilding});
        }
        std::sort(targets.begin(), targets.end(), [](const Target& left, const Target& right) {
            return left.building->identity()->self.id < right.building->identity()->self.id;
        });
        for (const Target& target : targets) {
            if (budget == 0 || idle.empty()) break;
            std::size_t assigned = 0;
            auto assignedView = ecs::View<Unit, Unit::Orders, Unit::Faction>();
            for (auto it = assignedView.begin(); it != assignedView.end(); ++it) {
                auto [orders, faction] = *it;
                if (faction->link.resolve() != ecs::try_get(factionIdentity->self)) continue;
                auto current = readCurrent(orders->values);
                if (!current) return failureFrom<std::size_t>(current.status());
                auto record = std::move(current).takeValue();
                if (record && record->kind == target.kind &&
                    sameHandle(record->targetEntity, target.building->identity()->self)) ++assigned;
            }
            while (assigned < target.limit && budget > 0) {
                auto best = idle.end();
                float bestDistance = std::numeric_limits<float>::max();
                for (auto it = idle.begin(); it != idle.end(); ++it) {
                    auto* worker = it->unit;
                    if ((target.kind == OrderKind::Build && worker->worker()->buildRate <= 0.0f) ||
                        (target.kind == OrderKind::Repair && worker->worker()->repairRate <= 0.0f)) continue;
                    const float distance = distanceSquared(worker->motion()->x, worker->motion()->y,
                                                           target.building->placement()->worldX,
                                                           target.building->placement()->worldY);
                    if (best == idle.end() || distance < bestDistance ||
                        (distance == bestDistance && worker->identity()->self.id < best->unit->identity()->self.id)) {
                        best = it;
                        bestDistance = distance;
                    }
                }
                if (best == idle.end()) break;
                CommandSpec command;
                command.kind = target.kind;
                command.target = {target.building->placement()->worldX, target.building->placement()->worldY};
                command.targetEntity = target.building->identity()->self;
                auto queued = best->unit->orders()->values.replace(command);
                if (!queued) return failureFrom<std::size_t>(queued.status());
                std::move(queued).takeValue();
                idle.erase(best);
                --budget;
                ++assigned;
                ++processed;
            }
        }
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> RepairSystem::step(const SimulationStep& step, const RepairDebit& debit) {
    if (step.delta.nanoseconds() < 0 || !debit)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                    "RTS repair requires non-negative time and a debit callback", "repair");
    std::size_t processed = 0;
    auto units = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders, Unit::Worker, Unit::Faction>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [identity, motion, orders, worker, faction] = *it;
        auto* unit = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || !motion->arrived || worker->repairRate <= 0.0f) continue;
        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        if (!record || record->kind != OrderKind::Repair) continue;
        auto* building = dynamic_cast<Building*>(ecs::try_get(record->targetEntity));
        if (building == nullptr || building->faction()->link.resolve() != faction->link.resolve() ||
            building->construction()->progress < 1.0f) {
            auto failed = orders->values.fail(record->id, "repair target is invalid");
            if (!failed) return failureFrom<std::size_t>(failed.status());
            continue;
        }
        auto integrity = building->integrity();
        const float missing = static_cast<float>(std::max(0.0, integrity->state.maxHealth - integrity->state.health));
        if (missing <= 0.0f) {
            auto completed = orders->values.complete(record->id);
            if (!completed) return failureFrom<std::size_t>(completed.status());
            continue;
        }
        const float healed = std::min(missing, worker->repairRate * static_cast<float>(step.delta.seconds()));
        const float accumulatedCost = integrity->repairCostRemainder + healed * integrity->repairCostPerHealth;
        const auto wholeCost = static_cast<std::int64_t>(std::floor(accumulatedCost));
        if (wholeCost > 0) {
            auto cost = resource::CostSpec::single(integrity->repairResource, wholeCost);
            if (!cost) return failureFrom<std::size_t>(cost.status());
            auto paid = debit(*unit, *building, cost.value());
            if (!paid) return failureFrom<std::size_t>(paid.status());
        }
        integrity->repairCostRemainder = accumulatedCost - static_cast<float>(wholeCost);
        integrity->state.health += healed;
        ++processed;
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> CaptureSystem::step(const SimulationStep& step, const LifecycleEventSink& events) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                    "RTS capture step delta must be non-negative", "step.delta");
    struct Force { ecs::EntityHandle faction{}; float strength = 0.0f; std::vector<Unit*> units; };
    std::size_t processed = 0;
    auto buildings = ecs::View<Building, Building::Identity, Building::Capture, Building::Construction,
                               Building::Faction, Building::Rally>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [identity, capture, construction, owner, rally] = *it;
        auto* building = identity == nullptr ? nullptr : dynamic_cast<Building*>(ecs::try_get(identity->self));
        if (building == nullptr || !capture->capturable || capture->blockedByGarrison ||
            construction->progress < 1.0f)
            continue;
        if (!std::isfinite(capture->durationSeconds) || capture->durationSeconds <= 0.0f)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument, "capture duration must be positive",
                                        "building.capture.durationSeconds");
        std::vector<Force> forces;
        auto units = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders, Unit::Capture, Unit::Faction>();
        for (auto unitIt = units.begin(); unitIt != units.end(); ++unitIt) {
            auto [unitIdentity, motion, orders, contribution, faction] = *unitIt;
            auto* unit = unitIdentity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(unitIdentity->self));
            if (unit == nullptr || !motion->arrived || contribution->rate <= 0.0f ||
                faction->link.resolve() == nullptr ||
                FactionRelationSystem::allied(faction->link, owner->link)) continue;
            auto current = readCurrent(orders->values);
            if (!current) return failureFrom<std::size_t>(current.status());
            auto record = std::move(current).takeValue();
            if (!record || record->kind != OrderKind::Capture || !sameHandle(record->targetEntity, identity->self))
                continue;
            auto found = std::find_if(forces.begin(), forces.end(), [&](const Force& force) {
                return sameHandle(force.faction, faction->link.handle());
            });
            if (found == forces.end()) { forces.push_back({faction->link.handle(), 0.0f, {}}); found = forces.end() - 1; }
            found->strength += contribution->rate;
            found->units.push_back(unit);
        }
        const float dt = static_cast<float>(step.delta.seconds());
        if (forces.empty()) {
            capture->progress = std::max(0.0f, capture->progress - dt / capture->durationSeconds);
            if (capture->progress == 0.0f) capture->capturingFaction = {};
            continue;
        }
        if (forces.size() != 1) continue;
        Force& force = forces.front();
        if (ecs::try_get(capture->capturingFaction) != nullptr &&
            !sameHandle(capture->capturingFaction, force.faction)) {
            capture->progress = std::max(0.0f, capture->progress - force.strength * dt / capture->durationSeconds);
            if (capture->progress == 0.0f) capture->capturingFaction = force.faction;
            continue;
        }
        capture->capturingFaction = force.faction;
        capture->progress = std::min(1.0f, capture->progress + force.strength * dt / capture->durationSeconds);
        ++processed;
        if (capture->progress < 1.0f) continue;
        auto linked = FactionLink::bind(force.faction);
        if (!linked) return failureFrom<std::size_t>(linked.status());
        owner->link = std::move(linked).takeValue();
        if (rally->enabled && rally->combatGroup != 0) {
            rally->combatGroup = ((static_cast<std::uint64_t>(force.faction.id) + 1u) << 32u) |
                                 (static_cast<std::uint64_t>(identity->self.id) + 1u);
            rally->reinforcements.clear();
            rally->reinforcementCapped = false;
            rally->reinforcementPolicyPausedTask.clear();
            rally->reinforcementCappedSeconds = 0.0f;
        }
        capture->progress = 0.0f;
        capture->capturingFaction = {};
        if (events) {
            Unit* capturer = *std::min_element(force.units.begin(), force.units.end(),
                [](Unit* left, Unit* right) {
                    return left->identity()->subject.format() < right->identity()->subject.format();
                });
            events({LifecycleEventKind::BuildingCaptured, identity->subject,
                    capturer->identity()->subject, building->definition()->id.format(), 1.0}, step.tick);
        }
        for (Unit* unit : force.units) {
            auto current = readCurrent(unit->orders()->values);
            if (!current) return failureFrom<std::size_t>(current.status());
            auto record = std::move(current).takeValue();
            if (record) {
                auto completed = unit->orders()->values.complete(record->id);
                if (!completed) return failureFrom<std::size_t>(completed.status());
            }
        }
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> InfrastructureSystem::step(const SimulationStep& step,
                                               const PassiveIncomeCredit& credit) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                    "RTS infrastructure step delta must be non-negative", "step.delta");

    struct Group { std::vector<Building*> buildings; };
    std::map<std::string, Group> groups;
    auto view = ecs::View<Building, Building::Identity, Building::Faction, Building::Construction,
                          Building::Integrity, Building::Infrastructure>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, faction, construction, integrity, infrastructure] = *it;
        auto* building = identity == nullptr ? nullptr : dynamic_cast<Building*>(ecs::try_get(identity->self));
        if (building == nullptr || &*building->identity() != identity) continue;
        if (!std::isfinite(infrastructure->powerProduced) || infrastructure->powerProduced < 0.0f ||
            !std::isfinite(infrastructure->powerConsumed) || infrastructure->powerConsumed < 0.0f ||
            !std::isfinite(infrastructure->incomeRate) || infrastructure->incomeRate < 0.0f ||
            !std::isfinite(infrastructure->incomeProgress) || infrastructure->incomeProgress < 0.0f ||
            !std::isfinite(infrastructure->buildInfluenceRadius) || infrastructure->buildInfluenceRadius < 0.0f)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                        "RTS building infrastructure values must be finite and non-negative",
                                        "building.infrastructure");
        if (!integrity->alive || integrity->state.health <= 0.0 || construction->progress < 1.0f) {
            infrastructure->powered = false;
            continue;
        }
        auto* owner = dynamic_cast<Faction*>(faction->link.resolve());
        if (owner == nullptr || !owner->identity()->subject.isValid()) {
            infrastructure->powered = infrastructure->powerConsumed == 0.0f;
            continue;
        }
        groups[owner->identity()->subject.format()].buildings.push_back(building);
    }

    std::size_t processed = 0;
    const float dt = static_cast<float>(step.delta.seconds());
    for (auto& [_, group] : groups) {
        float available = 0.0f;
        for (Building* building : group.buildings)
            available += building->infrastructure()->powerProduced;
        std::sort(group.buildings.begin(), group.buildings.end(), [](Building* left, Building* right) {
            if (left->infrastructure()->powerPriority != right->infrastructure()->powerPriority)
                return left->infrastructure()->powerPriority > right->infrastructure()->powerPriority;
            return left->identity()->subject.format() < right->identity()->subject.format();
        });
        for (Building* building : group.buildings) {
            auto infrastructure = building->infrastructure();
            infrastructure->powered = infrastructure->powerConsumed <= available + 1e-6f;
            if (infrastructure->powered) available -= infrastructure->powerConsumed;
            ++processed;
            if (!infrastructure->powered || infrastructure->incomeRate == 0.0f ||
                infrastructure->incomeResource.empty()) continue;
            infrastructure->incomeProgress += infrastructure->incomeRate * dt;
            if (!credit) continue;
            const auto whole = static_cast<std::int64_t>(std::floor(infrastructure->incomeProgress));
            if (whole <= 0) continue;
            auto cost = resource::CostSpec::single(infrastructure->incomeResource, whole);
            if (!cost) return failureFrom<std::size_t>(cost.status());
            auto receipt = credit(*building, cost.value());
            if (!receipt) return failureFrom<std::size_t>(receipt.status());
            infrastructure->incomeProgress -= static_cast<float>(whole);
        }
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> ContainmentSystem::step() {
    std::size_t processed = 0;
    auto retainedBy = [](const ecs::EntityHandle& occupant, const ecs::EntityHandle& container) {
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(occupant));
        return unit != nullptr && unit->containment()->container.isBound() &&
               sameHandle(unit->containment()->container.handle(), container);
    };
    auto transports = ecs::View<Unit, Unit::Identity, Unit::Containment>();
    for (auto it = transports.begin(); it != transports.end(); ++it) {
        auto [identity, containment] = *it;
        std::erase_if(containment->occupants,
                      [&](const auto& occupant) { return !retainedBy(occupant, identity->self); });
    }
    auto garrisons = ecs::View<Building, Building::Identity, Building::Garrison, Building::Capture>();
    for (auto it = garrisons.begin(); it != garrisons.end(); ++it) {
        auto [identity, garrison, capture] = *it;
        std::erase_if(garrison->occupants,
                      [&](const auto& occupant) { return !retainedBy(occupant, identity->self); });
        capture->blockedByGarrison = !garrison->occupants.empty();
    }
    auto units = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders, Unit::Faction, Unit::Containment>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [identity, motion, orders, faction, containment] = *it;
        auto* unit = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;

        if (containment->container.isBound()) {
            ecs::Entity* container = containment->container.resolve();
            if (auto* transport = dynamic_cast<Unit*>(container)) {
                if (!transport->durability()->alive) {
                    containment->container.reset();
                    unit->durability()->alive = false;
                    continue;
                }
                motion->x = transport->motion()->x;
                motion->y = transport->motion()->y;
            } else if (auto* building = dynamic_cast<Building*>(container)) {
                if (!building->integrity()->alive) {
                    containment->container.reset();
                    unit->durability()->alive = false;
                    continue;
                }
                motion->x = building->placement()->worldX;
                motion->y = building->placement()->worldY;
            } else {
                containment->container.reset();
            }
            motion->arrived = true;
            ++processed;
            continue;
        }

        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        if (!record || (record->kind != OrderKind::Garrison && record->kind != OrderKind::BoardTransport) ||
            !motion->arrived)
            continue;

        ecs::Entity* target = ecs::try_get(record->targetEntity);
        std::vector<ecs::EntityHandle>* occupants = nullptr;
        std::size_t capacity = 0;
        FactionLink* owner = nullptr;
        WorldPosition position{};
        if (record->kind == OrderKind::BoardTransport) {
            auto* transport = dynamic_cast<Unit*>(target);
            if (transport != nullptr && transport != unit && transport->durability()->alive) {
                occupants = &transport->containment()->occupants;
                capacity = transport->containment()->capacity;
                owner = &transport->faction()->link;
                position = {transport->motion()->x, transport->motion()->y};
            }
        } else {
            auto* building = dynamic_cast<Building*>(target);
            if (building != nullptr && building->integrity()->alive && building->construction()->progress >= 1.0f) {
                occupants = &building->garrison()->occupants;
                capacity = building->garrison()->capacity;
                owner = &building->faction()->link;
                position = {building->placement()->worldX, building->placement()->worldY};
            }
        }
        if (occupants == nullptr || owner == nullptr ||
            !FactionRelationSystem::allied(*owner, faction->link) ||
            occupants->size() >= capacity) {
            auto failed = orders->values.fail(record->id, "container is invalid, hostile, or full");
            if (!failed) return failureFrom<std::size_t>(failed.status());
            continue;
        }
        auto link = ContainerLink::bind(record->targetEntity);
        if (!link) return failureFrom<std::size_t>(link.status());
        containment->container = std::move(link).takeValue();
        occupants->push_back(identity->self);
        std::sort(occupants->begin(), occupants->end(), [](const auto& left, const auto& right) {
            if (left.id != right.id) return left.id < right.id;
            return left.generation < right.generation;
        });
        motion->x = position.x;
        motion->y = position.y;
        motion->arrived = true;
        if (auto* building = dynamic_cast<Building*>(target)) building->capture()->blockedByGarrison = true;
        auto completed = orders->values.complete(record->id);
        if (!completed) return failureFrom<std::size_t>(completed.status());
        ++processed;
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

namespace {
Result<std::size_t> releaseOccupants(std::vector<ecs::EntityHandle>& occupants, WorldPosition destination) {
    if (!std::isfinite(destination.x) || !std::isfinite(destination.y))
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                    "RTS unload destination must be finite", "destination");
    const auto retained = occupants;
    occupants.clear();
    std::size_t released = 0;
    for (std::size_t index = 0; index < retained.size(); ++index) {
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(retained[index]));
        if (unit == nullptr || !unit->durability()->alive) continue;
        const float angle = static_cast<float>(index) * 2.39996323f;
        const float radius = 0.75f * std::sqrt(static_cast<float>(index + 1));
        const WorldPosition slot{destination.x + std::cos(angle) * radius,
                                 destination.y + std::sin(angle) * radius};
        unit->containment()->container = {};
        unit->motion()->x = slot.x;
        unit->motion()->y = slot.y;
        unit->motion()->arrived = true;
        CommandSpec move;
        move.kind = OrderKind::Move;
        move.target = slot;
        auto ordered = unit->orders()->values.replace(move);
        if (!ordered) return failureFrom<std::size_t>(ordered.status());
        std::move(ordered).takeValue();
        ++released;
    }
    return Result<std::size_t>::success(released,
        Status::success(released == 0 ? StatusCode::NoOp : StatusCode::Applied));
}
}  // namespace

Result<std::size_t> ContainmentSystem::unload(Unit& transport, WorldPosition destination) {
    return releaseOccupants(transport.containment()->occupants, destination);
}

Result<std::size_t> ContainmentSystem::evacuate(Building& building, WorldPosition destination) {
    auto result = releaseOccupants(building.garrison()->occupants, destination);
    if (result) building.capture()->blockedByGarrison = false;
    return result;
}

Result<SupplyRendezvousSelection> SupplyRendezvousSystem::select(
    Unit& supplier, Unit& relay, WorldPosition predicted,
    map::Pathfinder& pathfinder, const NavigationGrid& grid) {
    if (!finitePosition(predicted) || !std::isfinite(grid.cellSize) || grid.cellSize <= 0.0f ||
        !std::isfinite(grid.originX) || !std::isfinite(grid.originY))
        return failure<SupplyRendezvousSelection>(DiagnosticCode::InvalidArgument,
                                                  "RTS supply rendezvous requires a finite prediction and grid",
                                                  "supply.rendezvous");
    auto* faction = dynamic_cast<Faction*>(supplier.faction()->link.resolve());
    if (faction == nullptr || !FactionRelationSystem::allied(relay.faction()->link,
                                                              supplier.faction()->link))
        return failure<SupplyRendezvousSelection>(DiagnosticCode::StaleHandle,
                                                  "RTS supply rendezvous requires a shared live faction",
                                                  "supply.faction");
    WorldPosition forward{relay.motion()->x - supplier.motion()->x,
                          relay.motion()->y - supplier.motion()->y};
    auto relayOrder = readCurrent(relay.orders()->values);
    if (!relayOrder) return failureFrom<SupplyRendezvousSelection>(relayOrder.status());
    const auto active = std::move(relayOrder).takeValue();
    if (active && movementOrder(active->kind)) {
        WorldPosition goal = active->target;
        if (relay.navigation()->plannedOrderId == active->id && !relay.navigation()->unreachable)
            goal = relay.navigation()->plannedGoal;
        forward = {goal.x - relay.motion()->x, goal.y - relay.motion()->y};
    }
    const float forwardLength = std::hypot(forward.x, forward.y);
    if (forwardLength > 1e-5f) {
        forward.x /= forwardLength;
        forward.y /= forwardLength;
    } else {
        forward = {1.0f, 0.0f};
    }
    const WorldPosition side{-forward.y, forward.x};
    const float offset = std::max(4.0f, supplier.supply()->range * 1.5f);
    const std::array<WorldPosition, 6> candidates{{
        predicted,
        {predicted.x + side.x * offset, predicted.y + side.y * offset},
        {predicted.x - side.x * offset, predicted.y - side.y * offset},
        {predicted.x - forward.x * offset, predicted.y - forward.y * offset},
        {predicted.x - forward.x * offset * 0.5f + side.x * offset,
         predicted.y - forward.y * offset * 0.5f + side.y * offset},
        {predicted.x - forward.x * offset * 0.5f - side.x * offset,
         predicted.y - forward.y * offset * 0.5f - side.y * offset}}};
    const auto worldToCell = [&](float value, float origin) {
        return static_cast<int>(std::lround((value - origin) / grid.cellSize));
    };
    struct Candidate { WorldPosition target; float threat; float deviation; std::size_t index; };
    std::optional<Candidate> best;
    std::optional<Candidate> baseline;
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        const auto candidate = candidates[index];
        const int x = worldToCell(candidate.x, grid.originX);
        const int y = worldToCell(candidate.y, grid.originY);
        if (!pathfinder.isWalkable(x, y)) continue;
        std::unique_ptr<map::Path> path(pathfinder.findPath(
            worldToCell(supplier.motion()->x, grid.originX),
            worldToCell(supplier.motion()->y, grid.originY), x, y));
        if (path == nullptr || path->empty()) continue;
        Candidate evaluated{candidate, visibleHostileThreatAt(*faction, candidate),
                            distanceSquared(candidate.x, candidate.y, predicted.x, predicted.y), index};
        if (index == 0) baseline = evaluated;
        if (!best || evaluated.threat < best->threat - 1e-4f ||
            (std::abs(evaluated.threat - best->threat) <= 1e-4f &&
             (evaluated.deviation < best->deviation - 1e-4f ||
              (std::abs(evaluated.deviation - best->deviation) <= 1e-4f && evaluated.index < best->index))))
            best = evaluated;
    }
    if (!best)
        return failure<SupplyRendezvousSelection>(DiagnosticCode::NotFound,
                                                  "RTS supply rendezvous has no reachable candidate",
                                                  "supply.rendezvous");
    SupplyRendezvousSelection result{best->target, best->threat, false};
    if (baseline) result.avoidedThreat = best->threat < baseline->threat - 1e-4f;
    return Result<SupplyRendezvousSelection>::success(result, Status::success(StatusCode::Applied));
}

Result<std::size_t> SupplySystem::step(const SimulationStep& step, const AmmoProductionPurchase& purchase,
                                       map::Pathfinder* pathfinder, const NavigationGrid& grid,
                                       const LifecycleEventSink& events) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS supply step delta must be non-negative",
                                    "step.delta");
    std::size_t processed = 0;
    auto producers = ecs::View<Building, Building::Identity, Building::Construction, Building::Integrity,
                               Building::Infrastructure, Building::Supply>();
    for (auto it = producers.begin(); it != producers.end(); ++it) {
        auto [identity, construction, integrity, infrastructure, supply] = *it;
        auto* building = dynamic_cast<Building*>(ecs::try_get(identity->self));
        if (building == nullptr || &*building->identity() != identity) continue;
        if (!std::isfinite(supply->stock) || !std::isfinite(supply->capacity) ||
            !std::isfinite(supply->productionRate) || !std::isfinite(supply->productionProgress) ||
            supply->stock < 0.0f || supply->capacity < 0.0f || supply->productionRate < 0.0f ||
            supply->productionProgress < 0.0f || supply->productionCostPerRound < 0)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                "RTS building ammunition production values must be finite and non-negative", "building.supply");
        const float remaining = std::max(0.0f, supply->capacity - supply->stock);
        if (!integrity->alive || construction->progress < 1.0f || !infrastructure->powered ||
            supply->productionResource.empty() || supply->productionRate <= 0.0f || remaining < 1.0f) {
            if (remaining < 1.0f) supply->productionProgress = std::min(supply->productionProgress, 0.999f);
            continue;
        }
        supply->productionProgress +=
            supply->productionRate * static_cast<float>(step.delta.seconds());
        const double readyValue = std::min<double>(
            std::floor(supply->productionProgress), std::floor(remaining));
        if (readyValue > static_cast<double>(std::numeric_limits<std::size_t>::max()))
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                "RTS ammunition production batch exceeds addressable size", "building.supply.productionProgress");
        const auto ready = static_cast<std::size_t>(readyValue);
        if (ready == 0) continue;
        std::size_t purchased = ready;
        if (supply->productionCostPerRound > 0) {
            if (!purchase) continue;
            auto paid = purchase(*building, supply->productionResource,
                                 supply->productionCostPerRound, ready);
            if (!paid) return failureFrom<std::size_t>(paid.status());
            purchased = std::move(paid).takeValue();
            if (purchased > ready)
                return failure<std::size_t>(DiagnosticCode::InvariantViolation,
                    "RTS ammunition purchase returned more rounds than requested", "purchase");
        }
        supply->stock += static_cast<float>(purchased);
        supply->productionProgress -= static_cast<float>(purchased);
        if (purchased == 0) supply->productionProgress = std::min(supply->productionProgress, 1.0f);
        if (events && purchased > 0)
            events({LifecycleEventKind::AmmoProduced, identity->subject, {}, supply->productionResource,
                    static_cast<double>(purchased)}, step.tick);
        processed += purchased;
    }

    auto ammunition = [](Unit& unit) -> std::pair<int, int> {
        auto* weaponEntity = dynamic_cast<weapon::WeaponEntity*>(unit.weapon()->link.resolve());
        if (weaponEntity == nullptr || weaponEntity->definition()->def == nullptr) return {0, 0};
        const auto& resource = weaponEntity->state()->resource;
        if (resource.kind != weapon::ResourceKind::Ammo || resource.infinite) return {0, 0};
        int carried = std::max(0, static_cast<int>(std::floor(resource.value)));
        int capacity = std::max(0, static_cast<int>(std::floor(resource.max)));
        if (auto* pool = weaponEntity->state()->ammoPool) {
            carried += std::max(0, pool->state()->count);
            capacity += pool->state()->max < 0 ? std::max(0, pool->state()->count) : pool->state()->max;
        } else if (resource.reserve >= 0) {
            carried += resource.reserve;
            capacity += std::max(resource.reserve, weaponEntity->definition()->def->reserveSize);
        }
        return {carried, capacity};
    };
    auto transfer = [&](Unit& recipient, int rounds) -> int {
        auto* weaponEntity = dynamic_cast<weapon::WeaponEntity*>(recipient.weapon()->link.resolve());
        if (weaponEntity == nullptr || rounds <= 0) return 0;
        auto& resource = weaponEntity->state()->resource;
        if (resource.kind != weapon::ResourceKind::Ammo || resource.infinite) return 0;
        int accepted = 0;
        const int magazineSpace = std::max(0, static_cast<int>(std::floor(resource.max - resource.value)));
        const int magazineRounds = std::min(rounds, magazineSpace);
        resource.value += static_cast<float>(magazineRounds);
        accepted += magazineRounds;
        rounds -= magazineRounds;
        if (magazineRounds > 0) weapon::WeaponSystem::cancelReload(*weaponEntity);
        if (rounds <= 0) return accepted;
        if (auto* pool = weaponEntity->state()->ammoPool) {
            const int space = pool->state()->max < 0 ? rounds : std::max(0, pool->state()->max - pool->state()->count);
            const int pooled = std::min(rounds, space);
            pool->state()->count += pooled;
            return accepted + pooled;
        }
        if (resource.reserve < 0) return accepted;
        const int reserveCapacity = std::max(resource.reserve, weaponEntity->definition()->def == nullptr
                                                                   ? resource.reserve
                                                                   : weaponEntity->definition()->def->reserveSize);
        const int reserved = std::min(rounds, std::max(0, reserveCapacity - resource.reserve));
        resource.reserve += reserved;
        return accepted + reserved;
    };

    struct Recipient {
        Unit* unit = nullptr;
        int deficit = 0;
        float ratio = 1.0f;
    };
    std::vector<Recipient> recipients;
    auto recipientView = ecs::View<Unit, Unit::Identity, Unit::Weapon, Unit::Faction, Unit::Containment,
                                   Unit::Durability, Unit::Supply>();
    for (auto it = recipientView.begin(); it != recipientView.end(); ++it) {
        auto [identity, weaponLink, faction, containment, durability, supply] = *it;
        (void)weaponLink; (void)faction; (void)supply;
        if (!durability->alive || containment->container.isBound()) continue;
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        const auto [carried, capacity] = ammunition(*unit);
        if (capacity > carried) recipients.push_back({unit, capacity - carried, float(carried) / float(capacity)});
    }
    std::sort(recipients.begin(), recipients.end(), [](const Recipient& left, const Recipient& right) {
        const float leftScore = (1.0f - left.ratio) * left.unit->supply()->priority;
        const float rightScore = (1.0f - right.ratio) * right.unit->supply()->priority;
        if (leftScore != rightScore) return leftScore > rightScore;
        return left.unit->identity()->self.id < right.unit->identity()->self.id;
    });

    std::vector<Unit*> suppliers;
    auto supplierView = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders, Unit::Faction, Unit::Supply,
                                   Unit::Containment, Unit::Durability>();
    for (auto it = supplierView.begin(); it != supplierView.end(); ++it) {
        auto [identity, motion, orders, faction, supply, containment, durability] = *it;
        (void)motion; (void)orders; (void)faction; (void)supply; (void)containment; (void)durability;
        auto* supplier = dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (supplier != nullptr) suppliers.push_back(supplier);
    }
    std::sort(suppliers.begin(), suppliers.end(), [](Unit* left, Unit* right) {
        return left->identity()->subject.format() < right->identity()->subject.format();
    });
    std::map<std::string, float> recipientReservations;
    for (Unit* supplier : suppliers) {
        if (!supplier->durability()->alive || supplier->containment()->container.isBound()) continue;
        auto* target = dynamic_cast<Unit*>(ecs::try_get(supplier->supply()->assignedTarget));
        if (target != nullptr && supplier->supply()->reservedStock > 0.0f)
            recipientReservations[target->identity()->subject.format()] += supplier->supply()->reservedStock;
    }
    for (Unit* supplier : suppliers) {
        auto identity = supplier->identity();
        auto motion = supplier->motion();
        auto orders = supplier->orders();
        auto faction = supplier->faction();
        auto supply = supplier->supply();
        auto containment = supplier->containment();
        auto durability = supplier->durability();
        if (!durability->alive || containment->container.isBound() || supply->range <= 0.0f ||
            supply->transferRate <= 0.0f)
            continue;
        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto order = std::move(current).takeValue();
        if ((!order || (order->kind != OrderKind::Resupply && order->kind != OrderKind::SupplyRelay)) &&
            supply->assignedTarget.table != nullptr) {
            if (auto* previous = dynamic_cast<Unit*>(ecs::try_get(supply->assignedTarget))) {
                float& reserved = recipientReservations[previous->identity()->subject.format()];
                reserved = std::max(0.0f, reserved - supply->reservedStock);
            }
            supply->assignedTarget = {};
            supply->reservedStock = 0.0f;
            supply->transferProgress = 0.0f;
            supply->returning = false;
            supply->rendezvousActive = false;
            supply->rendezvousThreat = 0.0f;
            supply->rendezvousAvoidedThreat = false;
        }
        if (!order && supply->returning) {
            if (distanceSquared(motion->x, motion->y, supply->returnPoint.x, supply->returnPoint.y) <= 0.01f) {
                supply->returning = false;
                if (events)
                    events({LifecycleEventKind::SupplyReturned, identity->subject, {}, {}, 0.0}, step.tick);
            }
            continue;
        }
        if (!order && supply->autoDispatch && supply->stock >= 1.0f) {
            for (const Recipient& candidate : recipients) {
                if (candidate.unit == supplier ||
                    !FactionRelationSystem::allied(candidate.unit->faction()->link, faction->link)) continue;
                const auto [liveCarried, liveCapacity] = ammunition(*candidate.unit);
                const float liveRatio = liveCapacity <= 0 ? 1.0f
                                                          : static_cast<float>(liveCarried) / liveCapacity;
                if (liveRatio > supply->autoThreshold) continue;
                const std::string recipientKey = candidate.unit->identity()->subject.format();
                const float remainingDeficit = std::max(
                    0.0f, static_cast<float>(std::max(0, liveCapacity - liveCarried)) -
                              recipientReservations[recipientKey]);
                if (remainingDeficit < 1.0f) continue;
                CommandSpec command;
                command.kind = OrderKind::Resupply;
                command.target = {candidate.unit->motion()->x, candidate.unit->motion()->y};
                command.targetEntity = candidate.unit->identity()->self;
                auto queued = orders->values.enqueue(command);
                if (!queued) return failureFrom<std::size_t>(queued.status());
                std::move(queued).takeValue();
                supply->assignedTarget = candidate.unit->identity()->self;
                supply->returnPoint = {motion->x, motion->y};
                supply->reservedStock = std::min<float>(remainingDeficit, std::floor(supply->stock));
                recipientReservations[recipientKey] += supply->reservedStock;
                supply->returning = false;
                auto assigned = readCurrent(orders->values);
                if (!assigned) return failureFrom<std::size_t>(assigned.status());
                order = std::move(assigned).takeValue();
                if (events)
                    events({LifecycleEventKind::SupplyDispatched, identity->subject,
                            candidate.unit->identity()->subject, {}, supply->reservedStock}, step.tick);
                ++processed;
                break;
            }
        }
        if (!order && supply->autoDispatch && supply->stock >= 1.0f) {
            Unit* relayTarget = nullptr;
            float relayRatio = 1.0f;
            float relayDistance = std::numeric_limits<float>::max();
            for (Unit* candidate : suppliers) {
                if (candidate == supplier || !candidate->durability()->alive ||
                    candidate->containment()->container.isBound() || !candidate->supply()->relayEnabled ||
                    candidate->supply()->capacity <= 0.0f ||
                    candidate->supply()->capacity >= supply->capacity ||
                    candidate->supply()->stock >= candidate->supply()->capacity ||
                    !FactionRelationSystem::allied(candidate->faction()->link, faction->link)) continue;
                const float ratio = candidate->supply()->stock / candidate->supply()->capacity;
                if (ratio > supply->autoThreshold) continue;
                const float distance = distanceSquared(motion->x, motion->y,
                                                       candidate->motion()->x, candidate->motion()->y);
                if (relayTarget == nullptr || ratio < relayRatio ||
                    (ratio == relayRatio && (distance < relayDistance ||
                     (distance == relayDistance && candidate->identity()->subject.format() <
                                                   relayTarget->identity()->subject.format())))) {
                    relayTarget = candidate;
                    relayRatio = ratio;
                    relayDistance = distance;
                }
            }
            if (relayTarget != nullptr) {
                const std::string relayKey = relayTarget->identity()->subject.format();
                const float remaining = std::max(0.0f, relayTarget->supply()->capacity -
                    relayTarget->supply()->stock - recipientReservations[relayKey]);
                if (remaining >= 1.0f) {
                    CommandSpec command;
                    command.kind = OrderKind::SupplyRelay;
                    command.target = {relayTarget->motion()->x, relayTarget->motion()->y};
                    command.targetEntity = relayTarget->identity()->self;
                    auto queued = orders->values.enqueue(command);
                    if (!queued) return failureFrom<std::size_t>(queued.status());
                    std::move(queued).takeValue();
                    supply->assignedTarget = relayTarget->identity()->self;
                    supply->returnPoint = {motion->x, motion->y};
                    supply->reservedStock = std::min<float>(remaining, std::floor(supply->stock));
                    recipientReservations[relayKey] += supply->reservedStock;
                    supply->returning = false;
                    auto assigned = readCurrent(orders->values);
                    if (!assigned) return failureFrom<std::size_t>(assigned.status());
                    order = std::move(assigned).takeValue();
                    if (events)
                        events({LifecycleEventKind::SupplyRelayDispatched, identity->subject,
                                relayTarget->identity()->subject, {}, supply->reservedStock}, step.tick);
                    ++processed;
                }
            }
        }
        if (!order || (order->kind != OrderKind::Resupply && order->kind != OrderKind::SupplyRelay)) continue;
        auto* target = dynamic_cast<Unit*>(ecs::try_get(order->targetEntity));
        if (target == nullptr || !target->durability()->alive || target->containment()->container.isBound() ||
            !FactionRelationSystem::allied(target->faction()->link, faction->link)) {
            auto failed = orders->values.fail(order->id, "supply target is invalid or hostile");
            if (!failed) return failureFrom<std::size_t>(failed.status());
            if (target != nullptr) {
                float& reserved = recipientReservations[target->identity()->subject.format()];
                reserved = std::max(0.0f, reserved - supply->reservedStock);
            }
            supply->assignedTarget = {};
            supply->reservedStock = 0.0f;
            supply->rendezvousActive = false;
            supply->rendezvousThreat = 0.0f;
            supply->rendezvousAvoidedThreat = false;
            continue;
        }
        if (supply->assignedTarget.table == nullptr) {
            supply->assignedTarget = target->identity()->self;
            supply->returnPoint = {motion->x, motion->y};
            const auto [carried, capacity] = ammunition(*target);
            const float requested = order->kind == OrderKind::SupplyRelay
                                        ? std::max(0.0f, target->supply()->capacity - target->supply()->stock)
                                        : static_cast<float>(std::max(0, capacity - carried));
            supply->reservedStock = std::min(requested, std::floor(supply->stock));
            supply->returning = false;
        }
        if (order->kind == OrderKind::SupplyRelay &&
            (!target->supply()->relayEnabled || target->supply()->capacity >= supply->capacity)) {
            auto failed = orders->values.fail(order->id, "supply relay requires a smaller-capacity relay target");
            if (!failed) return failureFrom<std::size_t>(failed.status());
            supply->assignedTarget = {};
            supply->reservedStock = 0.0f;
            supply->rendezvousActive = false;
            supply->rendezvousThreat = 0.0f;
            supply->rendezvousAvoidedThreat = false;
            continue;
        }
        if (order->kind == OrderKind::SupplyRelay) {
            WorldPosition rendezvous{target->motion()->x, target->motion()->y};
            auto targetOrder = readCurrent(target->orders()->values);
            if (!targetOrder) return failureFrom<std::size_t>(targetOrder.status());
            const auto moving = std::move(targetOrder).takeValue();
            if (moving && movementOrder(moving->kind)) {
                WorldPosition goal = moving->target;
                if (target->navigation()->plannedOrderId == moving->id &&
                    !target->navigation()->unreachable)
                    goal = target->navigation()->plannedGoal;
                const float dx = goal.x - target->motion()->x;
                const float dy = goal.y - target->motion()->y;
                const float remaining = std::hypot(dx, dy);
                if (remaining > 1e-5f && target->motion()->speed > 0.0f) {
                    const float travel = std::hypot(target->motion()->x - motion->x,
                                                    target->motion()->y - motion->y) /
                                         std::max(0.1f, motion->speed);
                    const float lead = std::min(remaining,
                        target->motion()->speed * std::clamp(travel, 0.0f, 4.0f));
                    rendezvous.x += dx / remaining * lead;
                    rendezvous.y += dy / remaining * lead;
                }
            }
            supply->rendezvousPoint = rendezvous;
            supply->rendezvousActive = true;
            supply->rendezvousThreat = 0.0f;
            supply->rendezvousAvoidedThreat = false;
            if (pathfinder != nullptr) {
                auto safe = SupplyRendezvousSystem::select(*supplier, *target, rendezvous, *pathfinder, grid);
                if (safe) {
                    const auto selection = std::move(safe).takeValue();
                    supply->rendezvousPoint = selection.target;
                    supply->rendezvousThreat = selection.threat;
                    supply->rendezvousAvoidedThreat = selection.avoidedThreat;
                } else if (safe.code() != StatusCode::NotFound) {
                    return failureFrom<std::size_t>(safe.status());
                } else {
                    safe.ignore("supply relay falls back when no safe map candidate is reachable");
                }
            }
        } else {
            supply->rendezvousActive = false;
            supply->rendezvousThreat = 0.0f;
            supply->rendezvousAvoidedThreat = false;
        }
        const float distance = distanceSquared(motion->x, motion->y, target->motion()->x, target->motion()->y);
        if (distance > supply->range * supply->range) continue;
        supply->transferProgress += supply->transferRate * static_cast<float>(step.delta.seconds());
        int ready = std::min({static_cast<int>(std::floor(supply->transferProgress)),
                              static_cast<int>(std::floor(supply->stock)),
                              static_cast<int>(std::floor(supply->reservedStock))});
        int accepted = 0;
        if (order->kind == OrderKind::SupplyRelay && target->supply()->relayEnabled) {
            ready = std::min(ready, static_cast<int>(std::floor(
                std::max(0.0f, target->supply()->capacity - target->supply()->stock))));
            target->supply()->stock += static_cast<float>(ready);
            accepted = ready;
        } else {
            accepted = transfer(*target, ready);
        }
        supply->stock -= static_cast<float>(accepted);
        supply->reservedStock -= static_cast<float>(accepted);
        supply->transferProgress -= static_cast<float>(accepted);
        if (accepted > 0) {
            float& reserved = recipientReservations[target->identity()->subject.format()];
            reserved = std::max(0.0f, reserved - static_cast<float>(accepted));
            if (events)
                events({order->kind == OrderKind::SupplyRelay
                            ? LifecycleEventKind::SupplyRelayTransferred
                            : LifecycleEventKind::AmmoResupplied,
                        identity->subject, target->identity()->subject, {}, static_cast<double>(accepted)},
                       step.tick);
        }
        if (accepted > 0) ++processed;
        const auto [carried, capacity] = ammunition(*target);
        if ((order->kind == OrderKind::Resupply && carried >= capacity) ||
            (order->kind == OrderKind::SupplyRelay && target->supply()->stock >= target->supply()->capacity) ||
            supply->stock < 1.0f) {
            auto completed = orders->values.complete(order->id);
            if (!completed) return failureFrom<std::size_t>(completed.status());
            float& reserved = recipientReservations[target->identity()->subject.format()];
            reserved = std::max(0.0f, reserved - supply->reservedStock);
            supply->assignedTarget = {};
            supply->reservedStock = 0.0f;
            supply->rendezvousActive = false;
            supply->rendezvousThreat = 0.0f;
            supply->rendezvousAvoidedThreat = false;
            supply->returning = true;
            if (events)
                events({LifecycleEventKind::SupplyReturning, identity->subject,
                        target->identity()->subject, {}, 0.0}, step.tick);
            CommandSpec returnCommand;
            returnCommand.kind = OrderKind::Move;
            returnCommand.target = supply->returnPoint;
            auto queued = orders->values.enqueue(returnCommand);
            if (!queued) return failureFrom<std::size_t>(queued.status());
            std::move(queued).takeValue();
        }
    }

    auto buildings = ecs::View<Building, Building::Identity, Building::Placement, Building::Faction, Building::Construction,
                               Building::Integrity, Building::Supply>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [identity, placement, faction, construction, integrity, supply] = *it;
        if (!integrity->alive || construction->progress < 1.0f || supply->stock < 1.0f || supply->range <= 0.0f ||
            supply->transferRate <= 0.0f) continue;
        for (Recipient& candidate : recipients) {
            if (!FactionRelationSystem::allied(candidate.unit->faction()->link, faction->link) ||
                candidate.deficit <= 0 ||
                distanceSquared(placement->worldX, placement->worldY, candidate.unit->motion()->x,
                                candidate.unit->motion()->y) > supply->range * supply->range) continue;
            supply->transferProgress += supply->transferRate * static_cast<float>(step.delta.seconds());
            const int ready = std::min({static_cast<int>(std::floor(supply->transferProgress)),
                                        static_cast<int>(std::floor(supply->stock)), candidate.deficit});
            const int accepted = transfer(*candidate.unit, ready);
            supply->stock -= static_cast<float>(accepted);
            supply->transferProgress -= static_cast<float>(accepted);
            candidate.deficit -= accepted;
            if (accepted > 0) {
                if (events)
                    events({LifecycleEventKind::AmmoResupplied, identity->subject,
                            candidate.unit->identity()->subject, {}, static_cast<double>(accepted)}, step.tick);
                ++processed;
            }
            break;
        }
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> SupplyConvoySystem::step() {
    struct Member {
        Unit* unit = nullptr;
        WorldPosition destination;
    };
    std::map<std::string, std::vector<Member>> groups;
    auto view = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders, Unit::Faction, Unit::Supply,
                          Unit::Containment, Unit::Durability>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, motion, orders, faction, supply, containment, durability] = *it;
        supply->convoyLeader = {};
        supply->convoyIndex = 0;
        supply->convoyWaiting = false;
        if (!durability->alive || containment->container.isBound() || supply->returning) continue;
        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        const auto order = std::move(current).takeValue();
        if (!order || (order->kind != OrderKind::Resupply && order->kind != OrderKind::SupplyRelay)) continue;
        auto* target = dynamic_cast<Unit*>(ecs::try_get(order->targetEntity));
        if (target == nullptr) continue;
        WorldPosition destination{target->motion()->x, target->motion()->y};
        if (order->kind == OrderKind::SupplyRelay && supply->rendezvousActive)
            destination = supply->rendezvousPoint;
        const std::string key = factionKey(faction->link) + "\n" +
                                std::to_string(static_cast<int>(order->kind)) + "\n" +
                                target->identity()->subject.format();
        groups[key].push_back({dynamic_cast<Unit*>(ecs::try_get(identity->self)), destination});
        (void)motion;
    }
    std::size_t processed = 0;
    for (auto& [key, members] : groups) {
        (void)key;
        members.erase(std::remove_if(members.begin(), members.end(),
                                     [](const Member& member) { return member.unit == nullptr; }), members.end());
        if (members.empty()) continue;
        std::sort(members.begin(), members.end(), [](const Member& left, const Member& right) {
            const float leftDistance = distanceSquared(left.unit->motion()->x, left.unit->motion()->y,
                                                       left.destination.x, left.destination.y);
            const float rightDistance = distanceSquared(right.unit->motion()->x, right.unit->motion()->y,
                                                        right.destination.x, right.destination.y);
            if (leftDistance != rightDistance) return leftDistance < rightDistance;
            return left.unit->identity()->subject.format() < right.unit->identity()->subject.format();
        });
        Unit* leader = members.front().unit;
        for (std::size_t index = 0; index < members.size(); ++index) {
            auto supply = members[index].unit->supply();
            supply->convoyLeader = leader->identity()->self;
            supply->convoyIndex = index;
            ++processed;
        }
        if (members.size() < 2) continue;
        for (std::size_t index = 1; index < members.size(); ++index) {
            Unit* follower = members[index].unit;
            const float spacing = leader->crowd()->radius + follower->crowd()->radius + 1.0f;
            const float allowed = spacing * 2.5f * static_cast<float>(index);
            if (distanceSquared(leader->motion()->x, leader->motion()->y,
                                follower->motion()->x, follower->motion()->y) > allowed * allowed) {
                leader->supply()->convoyWaiting = true;
                break;
            }
        }
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> MoraleSystem::step(const SimulationStep& step, const LifecycleEventSink& events) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS morale step delta must be non-negative",
                                    "step.delta");
    std::size_t processed = 0;
    auto units = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Faction, Unit::Morale, Unit::Containment,
                           Unit::Durability>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [identity, motion, faction, morale, containment, durability] = *it;
        if (!durability->alive || containment->container.isBound() || morale->capacity <= 0.0f) continue;
        if (!std::isfinite(morale->suppression) || !std::isfinite(morale->capacity) || morale->capacity < 0.0f ||
            !std::isfinite(morale->recoveryRate) || morale->recoveryRate < 0.0f)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS morale values must be finite",
                                        "unit.morale");
        float recoveryBonus = 0.0f;
        auto sources = ecs::View<Unit, Unit::Motion, Unit::Faction, Unit::Morale, Unit::Containment,
                                 Unit::Durability>();
        for (auto sourceIt = sources.begin(); sourceIt != sources.end(); ++sourceIt) {
            auto [sourceMotion, sourceFaction, aura, sourceContainment, sourceDurability] = *sourceIt;
            if (!sourceDurability->alive || sourceContainment->container.isBound() || aura->auraRange <= 0.0f ||
                !FactionRelationSystem::allied(sourceFaction->link, faction->link)) continue;
            if (distanceSquared(motion->x, motion->y, sourceMotion->x, sourceMotion->y) <=
                aura->auraRange * aura->auraRange)
                recoveryBonus = std::max(recoveryBonus, aura->auraRecoveryBonus);
        }
        const float before = morale->suppression;
        morale->suppression = std::max(0.0f, morale->suppression -
            (morale->recoveryRate + recoveryBonus) * static_cast<float>(step.delta.seconds()));
        if (morale->active && morale->suppression < morale->capacity * 0.5f) {
            morale->active = false;
            morale->retreating = false;
            if (events)
                events({LifecycleEventKind::SuppressionRecovered, identity->subject, {}, {},
                        morale->suppression}, step.tick);
        }
        if (morale->suppression != before) ++processed;
        (void)identity;
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> ShieldSystem::step(const SimulationStep& step, const LifecycleEventSink& events) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS shield step delta must be non-negative",
                                    "step.delta");
    const float dt = static_cast<float>(step.delta.seconds());
    std::size_t processed = 0;
    auto advance = [&](auto* shield, bool alive, SubjectRef subject) -> Result<void> {
        if (!std::isfinite(shield->value) || !std::isfinite(shield->capacity) ||
            !std::isfinite(shield->regenRate) || !std::isfinite(shield->regenDelay) ||
            !std::isfinite(shield->cooldown) || shield->capacity < 0.0f || shield->value < 0.0f ||
            shield->value > shield->capacity || shield->regenRate < 0.0f || shield->regenDelay < 0.0f ||
            shield->cooldown < 0.0f)
            return failure<void>(DiagnosticCode::InvalidArgument,
                                 "RTS shield values must be finite and within configured ranges", "shield");
        if (!alive || shield->capacity == 0.0f) return Result<void>::success();
        const float beforeValue = shield->value;
        const float beforeCooldown = shield->cooldown;
        shield->cooldown = std::max(0.0f, shield->cooldown - dt);
        if (shield->cooldown == 0.0f && shield->value < shield->capacity)
            shield->value = std::min(shield->capacity, shield->value + shield->regenRate * dt);
        if (beforeValue < shield->capacity && shield->value >= shield->capacity && events)
            events({LifecycleEventKind::ShieldRecharged, subject, {}, {}, shield->value}, step.tick);
        if (shield->value != beforeValue || shield->cooldown != beforeCooldown) ++processed;
        return Result<void>::success();
    };
    auto units = ecs::View<Unit, Unit::Identity, Unit::Shield, Unit::Durability>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [identity, shield, durability] = *it;
        auto result = advance(shield, durability->alive && durability->state.health > 0.0,
                              identity->subject);
        if (!result) return failureFrom<std::size_t>(result.status());
    }
    auto buildings = ecs::View<Building, Building::Identity, Building::Shield, Building::Integrity>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [identity, shield, integrity] = *it;
        auto result = advance(shield, integrity->alive && integrity->state.health > 0.0,
                              identity->subject);
        if (!result) return failureFrom<std::size_t>(result.status());
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> CommandNetworkSystem::step() {
    struct Jammer { ecs::Entity* faction = nullptr; WorldPosition position; float range = 0.0f; };
    std::vector<Jammer> jammers;
    auto unitJammers = ecs::View<Unit, Unit::Motion, Unit::Faction, Unit::Command, Unit::Durability,
                                 Unit::Containment>();
    for (auto it = unitJammers.begin(); it != unitJammers.end(); ++it) {
        auto [motion, faction, command, durability, containment] = *it;
        if (durability->alive && durability->state.health > 0.0 && !containment->container.isBound() &&
            command->jammingRange > 0.0f)
            jammers.push_back({faction->link.resolve(), {motion->x, motion->y}, command->jammingRange});
    }
    auto buildingJammers = ecs::View<Building, Building::Placement, Building::Faction, Building::Command,
                                     Building::Integrity, Building::Construction, Building::Infrastructure>();
    for (auto it = buildingJammers.begin(); it != buildingJammers.end(); ++it) {
        auto [placement, faction, command, integrity, construction, infrastructure] = *it;
        if (integrity->alive && integrity->state.health > 0.0 && construction->progress >= 1.0f &&
            infrastructure->powered && command->jammingRange > 0.0f)
            jammers.push_back({faction->link.resolve(), {placement->worldX, placement->worldY},
                               command->jammingRange});
    }
    auto isJammed = [&](ecs::Entity* faction, WorldPosition position) {
        return std::any_of(jammers.begin(), jammers.end(), [&](const Jammer& jammer) {
            return jammer.faction != nullptr && faction != nullptr &&
                   !FactionRelationSystem::allied(dynamic_cast<Faction*>(jammer.faction),
                                                   dynamic_cast<Faction*>(faction)) &&
                   distanceSquared(position.x, position.y, jammer.position.x, jammer.position.y) <=
                       jammer.range * jammer.range;
        });
    };

    struct Source {
        ecs::EntityHandle handle{};
        ecs::Entity* faction = nullptr;
        WorldPosition position;
        float range = 0.0f;
        int capacity = 0;
        int* load = nullptr;
        std::string stableId;
    };
    std::vector<Source> active;
    std::vector<Unit*> relays;
    std::vector<Unit*> recipients;
    auto units = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Faction, Unit::Command, Unit::Durability,
                           Unit::Containment>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [identity, motion, faction, command, durability, containment] = *it;
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        if (!std::isfinite(command->range) || command->range < 0.0f || command->capacity < 0 ||
            command->cost <= 0 || !std::isfinite(command->jammingRange) || command->jammingRange < 0.0f ||
            !std::isfinite(command->outOfCommandSpeedFactor) || command->outOfCommandSpeedFactor < 0.0f ||
            command->outOfCommandSpeedFactor > 1.0f || !std::isfinite(command->outOfCommandDamageFactor) ||
            command->outOfCommandDamageFactor < 0.0f || command->outOfCommandDamageFactor > 1.0f)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                        "RTS unit command policy is invalid", "unit.command");
        command->load = 0;
        command->source = {};
        command->uplink = {};
        command->relayActive = false;
        const bool live = durability->alive && durability->state.health > 0.0 &&
                          !containment->container.isBound();
        command->jammed = live && isJammed(faction->link.resolve(), {motion->x, motion->y});
        command->inCommand = !command->requiresCommand;
        if (!live || command->jammed) continue;
        if (command->range > 0.0f) {
            if (command->relayRequiresUplink) relays.push_back(unit);
            else {
                command->relayActive = true;
                active.push_back({identity->self, faction->link.resolve(), {motion->x, motion->y}, command->range,
                                  command->capacity, &command->load, identity->subject.format()});
            }
        }
        if (command->requiresCommand && !(command->range > 0.0f && command->relayRequiresUplink))
            recipients.push_back(unit);
    }
    auto buildings = ecs::View<Building, Building::Identity, Building::Placement, Building::Faction,
                               Building::Command, Building::Integrity, Building::Construction,
                               Building::Infrastructure>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [identity, placement, faction, command, integrity, construction, infrastructure] = *it;
        command->load = 0;
        command->active = false;
        if (!std::isfinite(command->range) || command->range < 0.0f || command->capacity < 0 ||
            !std::isfinite(command->jammingRange) || command->jammingRange < 0.0f)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                        "RTS building command policy is invalid", "building.command");
        const bool live = integrity->alive && integrity->state.health > 0.0 && construction->progress >= 1.0f &&
                          infrastructure->powered;
        command->jammed = live && isJammed(faction->link.resolve(), {placement->worldX, placement->worldY});
        if (!live || command->jammed || command->range <= 0.0f) continue;
        command->active = true;
        active.push_back({identity->self, faction->link.resolve(), {placement->worldX, placement->worldY},
                          command->range, command->capacity, &command->load, identity->subject.format()});
    }

    auto orderUnits = [](Unit* left, Unit* right) {
        if (left->command()->priority != right->command()->priority)
            return left->command()->priority > right->command()->priority;
        return left->identity()->subject.format() < right->identity()->subject.format();
    };
    std::sort(relays.begin(), relays.end(), orderUnits);
    std::sort(recipients.begin(), recipients.end(), orderUnits);
    auto cover = [&](Unit& unit, bool reserve) -> Source* {
        Source* best = nullptr;
        float bestDistance = std::numeric_limits<float>::max();
        for (auto& source : active) {
            if (sameHandle(source.handle, unit.identity()->self) ||
                !FactionRelationSystem::allied(dynamic_cast<Faction*>(source.faction),
                                                dynamic_cast<Faction*>(unit.faction()->link.resolve())) ||
                (source.capacity > 0 && *source.load + unit.command()->cost > source.capacity)) continue;
            const float candidate = distanceSquared(source.position.x, source.position.y,
                                                    unit.motion()->x, unit.motion()->y);
            if (candidate > source.range * source.range) continue;
            if (best == nullptr || candidate < bestDistance ||
                (candidate == bestDistance && source.stableId < best->stableId)) {
                best = &source;
                bestDistance = candidate;
            }
        }
        if (best != nullptr && reserve) *best->load += unit.command()->cost;
        return best;
    };
    bool changed = true;
    while (changed) {
        changed = false;
        for (Unit* relay : relays) {
            if (relay->command()->relayActive) continue;
            Source* uplink = cover(*relay, true);
            if (uplink == nullptr) continue;
            relay->command()->uplink = uplink->handle;
            relay->command()->source = uplink->handle;
            relay->command()->inCommand = true;
            relay->command()->relayActive = true;
            active.push_back({relay->identity()->self, relay->faction()->link.resolve(),
                              {relay->motion()->x, relay->motion()->y}, relay->command()->range,
                              relay->command()->capacity, &relay->command()->load,
                              relay->identity()->subject.format()});
            changed = true;
        }
    }
    for (Unit* unit : recipients) {
        Source* source = cover(*unit, true);
        if (source == nullptr) continue;
        unit->command()->source = source->handle;
        unit->command()->inCommand = true;
    }
    return Result<std::size_t>::success(active.size() + recipients.size(), Status::success(StatusCode::Applied));
}

namespace {

Result<void> settleAbility(Unit& caster, const AbilitySpec& spec, ecs::EntityHandle targetHandle,
                           WorldPosition point, combat::DamageRuntime& damage,
                           const DamageEventSink& damageEvents, SimulationTick tick,
                           const LifecycleEventSink& events) {
    auto affect = [&](ecs::Entity* entity) -> Result<void> {
        if (entity == nullptr) return failure<void>(DiagnosticCode::StaleHandle, "ability target is stale", "target");
        combat::CombatState* state = nullptr;
        bool* alive = nullptr;
        FactionLink* faction = nullptr;
        RTSEffectComponent* effects = nullptr;
        float* shield = nullptr;
        float* shieldCooldown = nullptr;
        float shieldDelay = 0.0f;
        SubjectRef subject;
        if (auto* unit = dynamic_cast<Unit*>(entity)) {
            state = &unit->durability()->state; alive = &unit->durability()->alive;
            faction = &unit->faction()->link; effects = &unit->effects()->values;
            shield = &unit->shield()->value; shieldCooldown = &unit->shield()->cooldown;
            shieldDelay = unit->shield()->regenDelay; subject = unit->identity()->subject;
        } else if (auto* building = dynamic_cast<Building*>(entity)) {
            state = &building->integrity()->state; alive = &building->integrity()->alive;
            faction = &building->faction()->link; effects = &building->effects()->values;
            shield = &building->shield()->value; shieldCooldown = &building->shield()->cooldown;
            shieldDelay = building->shield()->regenDelay; subject = building->identity()->subject;
        }
        if (state == nullptr || alive == nullptr || !*alive || !subject.isValid())
            return failure<void>(DiagnosticCode::InvalidArgument, "ability target is not a live RTS combat subject",
                                 "target");
        const bool allied = faction != nullptr &&
            FactionRelationSystem::allied(*faction, caster.faction()->link);
        if ((spec.target == AbilityTarget::Enemy && allied) ||
            (spec.target == AbilityTarget::Ally && !allied) ||
            (spec.target == AbilityTarget::Self && entity != &caster))
            return failure<void>(DiagnosticCode::Conflict, "ability target relationship is invalid", "target");
        if (spec.damage > 0.0f) {
            combat::DamageRequest request;
            request.source = caster.identity()->subject;
            request.target = subject;
            request.damageType = spec.damageType;
            request.healthDamage = spec.damage * effects->multiplier("incomingDamageMultiplier");
            if (shield != nullptr && *shield > 0.0f) {
                const float absorbed = std::min(*shield, static_cast<float>(request.healthDamage));
                *shield -= absorbed;
                request.healthDamage -= absorbed;
                if (shieldCooldown != nullptr) *shieldCooldown = shieldDelay;
            }
            auto outcome = damage.apply(*state, request);
            if (!outcome) return Result<void>::failure(outcome.status());
            if (damageEvents) damageEvents(request, outcome.value(), tick, DamageChannel::Ability);
            if (outcome.value().reaction == combat::HitReaction::Death) {
                if (faction != nullptr && hostileTo(caster, *faction)) {
                    auto awarded = VeterancySystem::award(caster,
                        static_cast<float>(std::max(1.0, state->maxHealth)));
                    if (!awarded) return Result<void>::failure(awarded.status());
                    std::move(awarded).takeValue();
                }
                *alive = false;
            }
        }
        if (spec.healing > 0.0f) state->health = std::min(state->maxHealth, state->health + spec.healing);
        if (spec.appliesEffect && effects != nullptr) {
            auto applied = effects->apply(spec.effect);
            if (!applied) return Result<void>::failure(applied.status());
            std::move(applied).takeValue();
            if (events)
                events({LifecycleEventKind::StatusApplied, caster.identity()->subject, subject,
                        spec.effect.id, spec.effect.duration}, tick);
        }
        return Result<void>::success(Status::success(StatusCode::Applied));
    };

    if (spec.target == AbilityTarget::Self) return affect(&caster);
    if (spec.target != AbilityTarget::Point) return affect(ecs::try_get(targetHandle));
    const float radius = std::max(0.0f, spec.radius);
    std::vector<ecs::Entity*> targets;
    auto units = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Durability>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [identity, motion, durability] = *it;
        if (durability->alive && distanceSquared(point.x, point.y, motion->x, motion->y) <= radius * radius)
            targets.push_back(ecs::try_get(identity->self));
    }
    auto buildings = ecs::View<Building, Building::Identity, Building::Placement, Building::Integrity>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [identity, placement, integrity] = *it;
        if (integrity->alive && distanceSquared(point.x, point.y, placement->worldX, placement->worldY) <=
                                    radius * radius)
            targets.push_back(ecs::try_get(identity->self));
    }
    for (ecs::Entity* entity : targets) {
        if (entity == nullptr || entity == &caster) continue;
        FactionLink* faction = dynamic_cast<Unit*>(entity) ? &dynamic_cast<Unit*>(entity)->faction()->link
                                                           : &dynamic_cast<Building*>(entity)->faction()->link;
        if (FactionRelationSystem::allied(*faction, caster.faction()->link)) continue;
        auto result = affect(entity);
        if (!result) return result;
    }
    return Result<void>::success(Status::success(targets.empty() ? StatusCode::NoOp : StatusCode::Applied));
}

Result<void> validateAbility(Unit& caster, const AbilitySpec& spec, ecs::EntityHandle target,
                             WorldPosition point) {
    if (spec.id.empty() || !std::isfinite(spec.range) || spec.range < 0.0f || !std::isfinite(spec.radius) ||
        spec.radius < 0.0f || !std::isfinite(spec.cooldown) || spec.cooldown < 0.0f ||
        !std::isfinite(spec.damage) || spec.damage < 0.0f || !std::isfinite(spec.healing) || spec.healing < 0.0f ||
        !std::isfinite(spec.castTime) || spec.castTime < 0.0f || !std::isfinite(spec.channelTickInterval) ||
        spec.channelTickInterval < 0.0f || spec.resourceCost < 0 ||
        (spec.resourceCost > 0 && spec.resourceType.empty()) ||
        (spec.channelTickInterval > 0.0f && spec.castTime <= 0.0f) ||
        !finitePosition(point))
        return failure<void>(DiagnosticCode::InvalidArgument, "RTS ability definition or target point is invalid",
                             "ability");
    if (spec.casterDefinition.isValid() && caster.definition()->id != spec.casterDefinition)
        return failure<void>(DiagnosticCode::Conflict, "unit definition cannot cast this ability", "caster");
    WorldPosition destination = point;
    if (spec.target == AbilityTarget::Self) destination = {caster.motion()->x, caster.motion()->y};
    else if (spec.target != AbilityTarget::Point) {
        auto position = entityPosition(target);
        if (!position) return failure<void>(DiagnosticCode::StaleHandle, "ability target is stale", "target");
        destination = *position;
        if (spec.target == AbilityTarget::Enemy && !FactionIntelSystem::targetable(
                dynamic_cast<Faction*>(caster.faction()->link.resolve()),
                stableSubject(ecs::try_get(target))))
            return failure<void>(DiagnosticCode::Conflict,
                                 "enemy ability target is not currently visible and detected", "target");
    }
    if (distanceSquared(caster.motion()->x, caster.motion()->y, destination.x, destination.y) >
        spec.range * spec.range)
        return failure<void>(DiagnosticCode::Conflict, "ability target is outside cast range", "target");
    return Result<void>::success();
}

}  // namespace

Result<void> AbilitySystem::cast(Unit& caster, const AbilitySpec& spec, ecs::EntityHandle target,
                                 WorldPosition point, combat::DamageRuntime& damage,
                                 const AbilityResourceDebit& debit,
                                 const DamageEventSink& damageEvents, SimulationTick tick,
                                 const LifecycleEventSink& events) {
    auto valid = validateAbility(caster, spec, target, point);
    if (!valid) return valid;
    if (caster.abilities()->channel)
        return failure<void>(DiagnosticCode::Conflict, "unit is already casting an ability", "ability.channel");
    auto found = std::find_if(caster.abilities()->cooldowns.begin(), caster.abilities()->cooldowns.end(),
                              [&](const auto& cooldown) { return cooldown.id == spec.id; });
    if (found != caster.abilities()->cooldowns.end() && found->remaining > 0.0f)
        return failure<void>(DiagnosticCode::Conflict, "ability is on cooldown", "ability.cooldown");
    if (spec.resourceCost > 0) {
        if (!debit) return failure<void>(DiagnosticCode::Unsupported, "ability resource debit provider is absent");
        auto cost = resource::CostSpec::single(spec.resourceType, spec.resourceCost);
        if (!cost) return Result<void>::failure(cost.status());
        auto paid = debit(caster, cost.value());
        if (!paid) return Result<void>::failure(paid.status());
    }
    if (found == caster.abilities()->cooldowns.end())
        caster.abilities()->cooldowns.push_back({spec.id, spec.cooldown});
    else found->remaining = spec.cooldown;
    if (spec.castTime == 0.0f) {
        auto settled = settleAbility(caster, spec, target, point, damage, damageEvents, tick, events);
        if (settled && events)
            events({LifecycleEventKind::AbilityCast, caster.identity()->subject,
                    stableSubject(ecs::try_get(target)), spec.id, 1.0}, tick);
        return settled;
    }
    caster.abilities()->channel = Unit::Abilities::Channel{spec, target, point,
        caster.durability()->state.health, spec.castTime,
        spec.channelTickInterval > 0.0f ? spec.channelTickInterval : spec.castTime};
    if (events)
        events({LifecycleEventKind::AbilityChannelStarted, caster.identity()->subject,
                stableSubject(ecs::try_get(target)), spec.id, spec.castTime}, tick);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::size_t> AbilitySystem::step(const SimulationStep& step, combat::DamageRuntime& damage,
                                        const DamageEventSink& damageEvents,
                                        const LifecycleEventSink& events) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS ability delta must be non-negative",
                                    "step.delta");
    const float dt = static_cast<float>(step.delta.seconds());
    std::size_t processed = 0;
    auto units = ecs::View<Unit, Unit::Identity, Unit::Abilities, Unit::Durability>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [identity, abilities, durability] = *it;
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        for (auto& cooldown : abilities->cooldowns) cooldown.remaining = std::max(0.0f, cooldown.remaining - dt);
        if (!abilities->channel) continue;
        auto& channel = *abilities->channel;
        if (!durability->alive ||
            (channel.spec.interruptOnDamage && durability->state.health < channel.startingHealth)) {
            if (events)
                events({LifecycleEventKind::AbilityInterrupted, identity->subject,
                        stableSubject(ecs::try_get(channel.target)), channel.spec.id,
                        channel.remaining}, step.tick);
            abilities->channel.reset();
            ++processed;
            continue;
        }
        const float elapsed = std::min(dt, channel.remaining);
        channel.remaining = std::max(0.0f, channel.remaining - dt);
        channel.tickRemaining -= elapsed;
        if (channel.spec.channelTickInterval > 0.0f) {
            while (channel.tickRemaining <= 1e-6f) {
                auto settled = settleAbility(*unit, channel.spec, channel.target, channel.point,
                                              damage, damageEvents, step.tick, events);
                if (!settled) return failureFrom<std::size_t>(settled.status());
                if (events)
                    events({LifecycleEventKind::AbilityChannelTick, identity->subject,
                            stableSubject(ecs::try_get(channel.target)), channel.spec.id,
                            channel.remaining}, step.tick);
                channel.tickRemaining += channel.spec.channelTickInterval;
                ++processed;
            }
        }
        if (channel.remaining == 0.0f) {
            const AbilitySpec completedSpec = channel.spec;
            const SubjectRef completedTarget = stableSubject(ecs::try_get(channel.target));
            if (channel.spec.channelTickInterval == 0.0f) {
                auto settled = settleAbility(*unit, channel.spec, channel.target, channel.point,
                                              damage, damageEvents, step.tick, events);
                if (!settled) return failureFrom<std::size_t>(settled.status());
                if (events)
                    events({LifecycleEventKind::AbilityCast, identity->subject, completedTarget,
                            completedSpec.id, 1.0}, step.tick);
            }
            abilities->channel.reset();
            if (events)
                events({LifecycleEventKind::AbilityChannelCompleted, identity->subject,
                        completedTarget, completedSpec.id, 0.0}, step.tick);
            ++processed;
        }
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> ArtillerySystem::step(const SimulationStep& step) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS artillery step delta must be non-negative",
                                    "step.delta");
    std::size_t processed = 0;
    auto units = ecs::View<Unit, Unit::Motion, Unit::Artillery, Unit::Containment, Unit::Durability,
                           Unit::Orders>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [motion, artillery, containment, durability, orders] = *it;
        if (!durability->alive || containment->container.isBound()) continue;
        if (!std::isfinite(artillery->deployTime) || artillery->deployTime < 0.0f ||
            !std::isfinite(artillery->deployRemaining) || artillery->deployRemaining < 0.0f)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                        "RTS artillery deployment values must be finite and non-negative",
                                        "unit.artillery");
        if (!artillery->positionInitialized) {
            artillery->previousX = motion->x;
            artillery->previousY = motion->y;
            artillery->positionInitialized = true;
            artillery->deployRemaining = artillery->deployTime;
            ++processed;
            continue;
        }
        const bool moved = distanceSquared(motion->x, motion->y, artillery->previousX, artillery->previousY) > 1e-8f;
        artillery->previousX = motion->x;
        artillery->previousY = motion->y;
        if (artillery->relocating) {
            auto current = readCurrent(orders->values);
            if (!current) return failureFrom<std::size_t>(current.status());
            const auto record = std::move(current).takeValue();
            if (!record || record->kind != OrderKind::Move) artillery->relocating = false;
        }
        if (moved) {
            artillery->deployRemaining = artillery->deployTime;
            ++processed;
        } else if (artillery->deployRemaining > 0.0f) {
            artillery->deployRemaining = std::max(0.0f, artillery->deployRemaining -
                static_cast<float>(step.delta.seconds()));
            ++processed;
        }
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<ArtilleryRelocationSelection> ArtilleryRelocationSystem::select(
    Unit& unit, WorldPosition target, float distance, float weaponRange,
    map::Pathfinder& pathfinder, const NavigationGrid& grid) {
    if (!finitePosition(target) || !std::isfinite(distance) || distance <= 0.0f ||
        !std::isfinite(weaponRange) || weaponRange < 0.0f || !std::isfinite(grid.cellSize) ||
        grid.cellSize <= 0.0f || !std::isfinite(grid.originX) || !std::isfinite(grid.originY))
        return failure<ArtilleryRelocationSelection>(
            DiagnosticCode::InvalidArgument,
            "RTS artillery relocation requires finite target, range and navigation grid values",
            "artillery.relocation");

    auto* ownFaction = dynamic_cast<Faction*>(unit.faction()->link.resolve());
    if (ownFaction == nullptr)
        return failure<ArtilleryRelocationSelection>(DiagnosticCode::StaleHandle,
                                                      "RTS artillery faction link is stale",
                                                      "unit.faction");
    const auto visibleToFaction = [&](SubjectRef subject) {
        if (ownFaction->intel()->contacts.empty()) return true;
        const auto found = std::find_if(ownFaction->intel()->contacts.begin(), ownFaction->intel()->contacts.end(),
                                        [&](const auto& contact) { return contact.subject == subject; });
        return found != ownFaction->intel()->contacts.end() && found->visible && found->detected;
    };
    const auto hostileThreat = [&](WorldPosition candidate) {
        float threat = 0.0f;
        auto units = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Faction, Unit::Weapon,
                               Unit::Durability, Unit::Containment>();
        for (auto it = units.begin(); it != units.end(); ++it) {
            auto [identity, motion, faction, weaponLink, durability, containment] = *it;
            if (!durability->alive || containment->container.isBound() ||
                FactionRelationSystem::allied(
                    dynamic_cast<Faction*>(faction->link.resolve()), ownFaction) ||
                !visibleToFaction(identity->subject)) continue;
            auto* weaponEntity = dynamic_cast<weapon::WeaponEntity*>(weaponLink->link.resolve());
            const auto* definition = weaponEntity == nullptr ? nullptr : weaponEntity->definition()->def;
            if (definition == nullptr || definition->range <= 0.0f) continue;
            const float d2 = distanceSquared(candidate.x, candidate.y, motion->x, motion->y);
            if (d2 <= definition->range * definition->range)
                threat += std::max(0.0f, definition->damage);
        }
        auto buildings = ecs::View<Building, Building::Identity, Building::Placement, Building::Faction,
                                   Building::Weapon, Building::Integrity, Building::Construction>();
        for (auto it = buildings.begin(); it != buildings.end(); ++it) {
            auto [identity, placement, faction, weaponLink, integrity, construction] = *it;
            if (!integrity->alive || construction->progress < 1.0f ||
                FactionRelationSystem::allied(
                    dynamic_cast<Faction*>(faction->link.resolve()), ownFaction) ||
                !visibleToFaction(identity->subject)) continue;
            auto* weaponEntity = dynamic_cast<weapon::WeaponEntity*>(weaponLink->link.resolve());
            const auto* definition = weaponEntity == nullptr ? nullptr : weaponEntity->definition()->def;
            if (definition == nullptr || definition->range <= 0.0f) continue;
            const float d2 = distanceSquared(candidate.x, candidate.y,
                                             placement->worldX, placement->worldY);
            if (d2 <= definition->range * definition->range)
                threat += std::max(0.0f, definition->damage);
        }
        return threat;
    };
    const float separation = std::max(unit.crowd()->radius * 2.0f, distance * 0.75f);
    const auto conflictCount = [&](WorldPosition candidate) {
        int conflicts = 0;
        auto allies = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Faction, Unit::Weapon,
                                Unit::Artillery, Unit::Durability, Unit::Containment>();
        for (auto it = allies.begin(); it != allies.end(); ++it) {
            auto [identity, motion, faction, weaponLink, artillery, durability, containment] = *it;
            if (sameHandle(identity->self, unit.identity()->self) || !durability->alive ||
                containment->container.isBound() ||
                !FactionRelationSystem::allied(
                    dynamic_cast<Faction*>(faction->link.resolve()), ownFaction)) continue;
            auto* weaponEntity = dynamic_cast<weapon::WeaponEntity*>(weaponLink->link.resolve());
            const auto* definition = weaponEntity == nullptr ? nullptr : weaponEntity->definition()->def;
            if (definition == nullptr ||
                (definition->projectile.gravity <= 0.0f && artillery->deployTime <= 0.0f)) continue;
            const WorldPosition position = artillery->relocating ? artillery->relocationTarget
                                                                  : WorldPosition{motion->x, motion->y};
            if (distanceSquared(candidate.x, candidate.y, position.x, position.y) < separation * separation)
                ++conflicts;
        }
        return conflicts;
    };

    const float dx = target.x - unit.motion()->x;
    const float dy = target.y - unit.motion()->y;
    const float length = std::hypot(dx, dy);
    const float forwardX = length > 1e-5f ? dx / length : 1.0f;
    const float forwardY = length > 1e-5f ? dy / length : 0.0f;
    const float sideX = -forwardY;
    const float sideY = forwardX;
    const float preferred = deterministicShotRandom(unit.identity()->subject, 0, 17) >= 0.5f ? 1.0f : -1.0f;
    const std::array<WorldPosition, 8> directions{{
        {sideX * preferred - forwardX * 0.25f, sideY * preferred - forwardY * 0.25f},
        {-sideX * preferred - forwardX * 0.25f, -sideY * preferred - forwardY * 0.25f},
        {-forwardX + sideX * 0.5f * preferred, -forwardY + sideY * 0.5f * preferred},
        {-forwardX - sideX * 0.5f * preferred, -forwardY - sideY * 0.5f * preferred},
        {sideX * preferred + forwardX * 0.35f, sideY * preferred + forwardY * 0.35f},
        {-sideX * preferred + forwardX * 0.35f, -sideY * preferred + forwardY * 0.35f},
        {-forwardX, -forwardY}, {forwardX, forwardY}}};
    const auto worldToCell = [&](float value, float origin) {
        return static_cast<int>(std::lround((value - origin) / grid.cellSize));
    };
    struct Candidate {
        WorldPosition target;
        float threat;
        float rangePenalty;
        int conflicts;
        bool revisit;
        std::size_t index;
    };
    std::optional<Candidate> best;
    std::optional<Candidate> legacy;
    for (std::size_t index = 0; index < directions.size(); ++index) {
        const float directionLength = std::hypot(directions[index].x, directions[index].y);
        const WorldPosition candidate{unit.motion()->x + directions[index].x / directionLength * distance,
                                      unit.motion()->y + directions[index].y / directionLength * distance};
        const int goalX = worldToCell(candidate.x, grid.originX);
        const int goalY = worldToCell(candidate.y, grid.originY);
        if (!pathfinder.isWalkable(goalX, goalY)) continue;
        std::unique_ptr<map::Path> path(pathfinder.findPath(worldToCell(unit.motion()->x, grid.originX),
                                                            worldToCell(unit.motion()->y, grid.originY),
                                                            goalX, goalY));
        if (path == nullptr || path->empty()) continue;
        Candidate evaluated{candidate, hostileThreat(candidate),
                            std::max(0.0f, std::hypot(candidate.x - target.x, candidate.y - target.y) - weaponRange),
                            conflictCount(candidate),
                            unit.artillery()->hasDepartedPosition &&
                                distanceSquared(candidate.x, candidate.y,
                                                unit.artillery()->departedPosition.x,
                                                unit.artillery()->departedPosition.y) < distance * distance * 0.5625f,
                            index};
        if (index == 0) legacy = evaluated;
        const bool better = !best || evaluated.threat < best->threat - 1e-4f ||
            (std::abs(evaluated.threat - best->threat) <= 1e-4f &&
             (evaluated.rangePenalty < best->rangePenalty - 1e-4f ||
              (std::abs(evaluated.rangePenalty - best->rangePenalty) <= 1e-4f &&
               (evaluated.conflicts < best->conflicts ||
                (evaluated.conflicts == best->conflicts &&
                 ((best->revisit && !evaluated.revisit) ||
                  (best->revisit == evaluated.revisit && evaluated.index < best->index)))))));
        if (better) best = evaluated;
    }
    if (!best)
        return failure<ArtilleryRelocationSelection>(DiagnosticCode::NotFound,
                                                      "RTS artillery has no reachable relocation candidate",
                                                      "artillery.relocation");
    ArtilleryRelocationSelection selection;
    selection.target = best->target;
    selection.threat = best->threat;
    selection.conflicts = best->conflicts;
    selection.avoidedDepartedPosition = unit.artillery()->hasDepartedPosition && !best->revisit;
    if (legacy) {
        selection.avoidedThreat = best->threat < legacy->threat - 1e-4f;
        selection.deconflicted = best->conflicts < legacy->conflicts;
    }
    return Result<ArtilleryRelocationSelection>::success(selection,
                                                          Status::success(StatusCode::Applied));
}

Result<std::size_t> FireSupportSystem::request(Unit& requester, WorldPosition center, float radius,
                                               int shotsPerResponder, std::size_t maxResponders) {
    if (!finitePosition(center) || !std::isfinite(radius) || radius <= 0.0f || shotsPerResponder <= 0 ||
        !requester.durability()->alive || requester.containment()->container.isBound() ||
        requester.faction()->link.resolve() == nullptr)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                    "RTS fire support request is invalid", "fireSupport");
    struct Candidate { Unit* unit = nullptr; float distance = 0.0f; std::string id; };
    std::vector<Candidate> candidates;
    auto view = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Faction, Unit::Orders, Unit::Weapon,
                          Unit::Artillery, Unit::Durability, Unit::Containment, Unit::Morale>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, motion, faction, orders, weaponLink, artillery, durability, containment, morale] = *it;
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(identity->self));
        auto* weaponEntity = dynamic_cast<weapon::WeaponEntity*>(weaponLink->link.resolve());
        if (unit == nullptr || unit == &requester || !durability->alive || containment->container.isBound() ||
            morale->retreating || !FactionRelationSystem::allied(faction->link, requester.faction()->link) ||
            weaponEntity == nullptr || weaponEntity->definition()->def == nullptr) continue;
        const auto& definition = *weaponEntity->definition()->def;
        if (definition.projectile.speed <= 0.0f ||
            (definition.projectile.gravity <= 0.0f && artillery->deployTime <= 0.0f)) continue;
        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        if (record && record->kind != OrderKind::HoldPosition && record->kind != OrderKind::AttackMove &&
            record->kind != OrderKind::Patrol) continue;
        const float distance = distanceSquared(motion->x, motion->y, center.x, center.y);
        if (distance > definition.range * definition.range) continue;
        candidates.push_back({unit, distance, identity->subject.format()});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        return left.distance != right.distance ? left.distance < right.distance : left.id < right.id;
    });
    if (maxResponders > 0 && candidates.size() > maxResponders) candidates.resize(maxResponders);
    float dx = center.x - requester.motion()->x, dy = center.y - requester.motion()->y;
    const float length = std::hypot(dx, dy);
    if (length <= 1e-5f) { dx = 1.0f; dy = 0.0f; }
    else { dx /= length; dy /= length; }
    const WorldPosition lateral{-dy * radius, dx * radius};
    for (const Candidate& candidate : candidates) {
        CommandSpec command;
        command.kind = OrderKind::SuppressArea;
        command.target = {center.x - lateral.x, center.y - lateral.y};
        command.secondaryTarget = {center.x + lateral.x, center.y + lateral.y};
        command.radius = radius;
        auto assigned = candidate.unit->orders()->values.replace(command);
        if (!assigned) return failureFrom<std::size_t>(assigned.status());
        std::move(assigned).takeValue();
        candidate.unit->artillery()->suppressionShotsRemaining = shotsPerResponder;
        candidate.unit->artillery()->fireSupportRequester = requester.identity()->self;
    }
    return Result<std::size_t>::success(candidates.size(),
        Status::success(candidates.empty() ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> FireSupportSystem::cancel(Unit& requester) {
    std::size_t cancelled = 0;
    auto view = ecs::View<Unit, Unit::Identity, Unit::Orders, Unit::Artillery>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, orders, artillery] = *it;
        if (!sameHandle(artillery->fireSupportRequester, requester.identity()->self)) continue;
        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        if (record && record->kind == OrderKind::SuppressArea) {
            auto stopped = orders->values.cancel(record->id, "fire support cancelled");
            if (!stopped) return failureFrom<std::size_t>(stopped.status());
            ++cancelled;
        }
        artillery->fireSupportRequester = {};
        artillery->suppressionShotsRemaining = 0;
        (void)identity;
    }
    return Result<std::size_t>::success(cancelled,
        Status::success(cancelled == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> FireSupportSystem::step(const SimulationStep& step) {
    struct Exposure { ecs::EntityHandle handle{}; ecs::Entity* faction = nullptr; WorldPosition position; };
    std::vector<Exposure> exposures;
    auto exposedUnits = ecs::View<Unit, Unit::Identity, Unit::Faction, Unit::Artillery, Unit::Durability>();
    for (auto it = exposedUnits.begin(); it != exposedUnits.end(); ++it) {
        auto [identity, faction, artillery, durability] = *it;
        if (durability->alive && artillery->lastFireTick.value() > 0 &&
            step.tick.value() >= artillery->lastFireTick.value())
            exposures.push_back({identity->self, faction->link.resolve(), artillery->lastFirePosition});
    }
    auto exposedBuildings = ecs::View<Building, Building::Identity, Building::Faction, Building::IndirectFire,
                                      Building::Integrity>();
    for (auto it = exposedBuildings.begin(); it != exposedBuildings.end(); ++it) {
        auto [identity, faction, indirect, integrity] = *it;
        if (integrity->alive && indirect->lastFireTick.value() > 0 &&
            step.tick.value() >= indirect->lastFireTick.value())
            exposures.push_back({identity->self, faction->link.resolve(), indirect->lastFirePosition});
    }
    std::size_t processed = 0;
    auto responders = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Faction, Unit::Orders, Unit::Weapon,
                                Unit::Artillery, Unit::Durability, Unit::Containment>();
    for (auto it = responders.begin(); it != responders.end(); ++it) {
        auto [identity, motion, faction, orders, weaponLink, artillery, durability, containment] = *it;
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        if (artillery->fireSupportRequester.table != nullptr &&
            ecs::try_get(artillery->fireSupportRequester) == nullptr) {
            auto current = readCurrent(orders->values);
            if (!current) return failureFrom<std::size_t>(current.status());
            auto record = std::move(current).takeValue();
            if (record && record->kind == OrderKind::SuppressArea) {
                auto stopped = orders->values.cancel(record->id, "fire support requester lost");
                if (!stopped) return failureFrom<std::size_t>(stopped.status());
            }
            artillery->fireSupportRequester = {};
            ++processed;
        }
        if (!artillery->autoCounterBattery || artillery->counterBatteryWindowTicks == 0 ||
            !durability->alive || containment->container.isBound()) continue;
        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        if (record && record->kind != OrderKind::HoldPosition) continue;
        auto* weaponEntity = dynamic_cast<weapon::WeaponEntity*>(weaponLink->link.resolve());
        if (weaponEntity == nullptr || weaponEntity->definition()->def == nullptr) continue;
        const auto& definition = *weaponEntity->definition()->def;
        const Exposure* best = nullptr;
        float bestDistance = std::numeric_limits<float>::max();
        for (const Exposure& exposure : exposures) {
            if (exposure.faction == nullptr || FactionRelationSystem::allied(
                    dynamic_cast<Faction*>(exposure.faction),
                    dynamic_cast<Faction*>(faction->link.resolve()))) continue;
            auto* hostileUnit = dynamic_cast<Unit*>(ecs::try_get(exposure.handle));
            auto* hostileBuilding = dynamic_cast<Building*>(ecs::try_get(exposure.handle));
            const auto firedTick = hostileUnit != nullptr ? hostileUnit->artillery()->lastFireTick.value()
                                  : hostileBuilding != nullptr ? hostileBuilding->indirectFire()->lastFireTick.value() : 0;
            if (step.tick.value() - firedTick > artillery->counterBatteryWindowTicks) continue;
            const float distance = distanceSquared(motion->x, motion->y, exposure.position.x, exposure.position.y);
            if (distance > definition.range * definition.range || distance >= bestDistance) continue;
            best = &exposure; bestDistance = distance;
        }
        if (best == nullptr) continue;
        CommandSpec counter;
        counter.kind = OrderKind::AttackGround;
        counter.target = best->position;
        auto assigned = orders->values.replace(counter);
        if (!assigned) return failureFrom<std::size_t>(assigned.status());
        std::move(assigned).takeValue();
        ++processed;
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

std::uint64_t RTSProjectileSystem::key(weapon::ProjectileHandle handle) noexcept {
    return (static_cast<std::uint64_t>(handle.generation) << 32u) | handle.slot;
}

RTSProjectileSystemSnapshot RTSProjectileSystem::snapshot() const {
    RTSProjectileSystemSnapshot result;
    result.runtime = runtime_.snapshot();
    for (auto& slot : result.runtime.slots)
        if (slot.state) slot.state->target.reset();
    result.payloads.reserve(payloads_.size());
    for (const auto& [payloadKey, payload] : payloads_) {
        result.payloads.push_back({payloadKey, payload.source, stableSubject(ecs::try_get(payload.faction)),
            stableSubject(ecs::try_get(payload.target)), stableSubject(ecs::try_get(payload.observer)),
            payload.targetPoint, payload.targetHeight, payload.damageType, payload.damage,
            payload.radius, payload.splashMinimumDamageFactor, payload.targetsGround, payload.targetsAir,
            payload.friendlyFire, payload.blockedByObstacles, payload.requiredTargetTags,
            payload.excludedTargetTags});
    }
    return result;
}

Result<void> RTSProjectileSystem::restore(const RTSProjectileSystemSnapshot& snapshot,
                                          const ProjectileSubjectResolver& resolver) {
    if (!resolver)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS projectile restore requires a stable subject resolver", "projectiles.resolver");
    weapon::ProjectileRuntime stagedRuntime;
    auto runtimeRestored = stagedRuntime.restore(snapshot.runtime);
    if (!runtimeRestored) return runtimeRestored;
    std::set<std::uint64_t> liveKeys;
    for (const auto& state : stagedRuntime.states()) liveKeys.insert(key(state.handle));
    if (liveKeys.size() != snapshot.payloads.size())
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS projectile snapshot payload count does not match live trajectories",
                             "projectiles.payloads");
    std::map<std::uint64_t, Payload> stagedPayloads;
    for (const auto& value : snapshot.payloads) {
        if (!liveKeys.contains(value.key) || stagedPayloads.contains(value.key) || !value.source.isValid() ||
            !value.faction.isValid() || !finitePosition(value.targetPoint) || !std::isfinite(value.targetHeight) ||
            value.damageType.empty() ||
            !std::isfinite(value.damage) || value.damage < 0.0 || !std::isfinite(value.radius) || value.radius < 0.0f ||
            !std::isfinite(value.splashMinimumDamageFactor) || value.splashMinimumDamageFactor < 0.0f ||
            value.splashMinimumDamageFactor > 1.0f || (!value.targetsGround && !value.targetsAir))
            return failure<void>(DiagnosticCode::InvalidArgument,
                                 "RTS projectile snapshot contains an invalid payload", "projectiles.payloads");
        auto* faction = dynamic_cast<Faction*>(resolver(value.faction));
        ecs::Entity* target = value.target.isValid() ? resolver(value.target) : nullptr;
        ecs::Entity* observer = value.observer.isValid() ? resolver(value.observer) : nullptr;
        if (faction == nullptr || (value.target.isValid() && dynamic_cast<Unit*>(target) == nullptr &&
                                   dynamic_cast<Building*>(target) == nullptr) ||
            (value.observer.isValid() && dynamic_cast<Unit*>(observer) == nullptr &&
             dynamic_cast<Building*>(observer) == nullptr))
            return failure<void>(DiagnosticCode::NotFound,
                                 "RTS projectile snapshot relationship cannot be resolved", "projectiles.payloads");
        Payload payload;
        payload.source = value.source;
        payload.faction = ecs::handle_of(faction);
        payload.target = target == nullptr ? ecs::EntityHandle{} : ecs::handle_of(target);
        payload.observer = observer == nullptr ? ecs::EntityHandle{} : ecs::handle_of(observer);
        payload.targetPoint = value.targetPoint;
        payload.targetHeight = value.targetHeight;
        payload.damageType = value.damageType;
        payload.damage = value.damage;
        payload.radius = value.radius;
        payload.splashMinimumDamageFactor = value.splashMinimumDamageFactor;
        payload.targetsGround = value.targetsGround;
        payload.targetsAir = value.targetsAir;
        payload.friendlyFire = value.friendlyFire;
        payload.blockedByObstacles = value.blockedByObstacles;
        payload.requiredTargetTags = value.requiredTargetTags;
        payload.excludedTargetTags = value.excludedTargetTags;
        stagedPayloads.emplace(value.key, std::move(payload));
    }
    runtime_ = std::move(stagedRuntime);
    payloads_ = std::move(stagedPayloads);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<weapon::ProjectilePoint> RTSProjectileSystem::position(ecs::EntityHandle target) const {
    auto value = entityPosition(target);
    if (!value)
        return failure<weapon::ProjectilePoint>(DiagnosticCode::StaleHandle,
                                                "RTS projectile homing target is stale", "target");
    return Result<weapon::ProjectilePoint>::success({value->x, 0.0, value->y});
}

Result<void> RTSProjectileSystem::launch(SubjectRef source, ecs::EntityHandle faction, WorldPosition origin,
                                         ecs::EntityHandle target, WorldPosition targetPoint,
                                         const weapon::WeaponDefinition& definition, double damageFactor,
                                         ecs::EntityHandle observer, float originHeight, float targetHeight) {
    if (!source.isValid() || definition.projectile.speed <= 0.0f || !std::isfinite(damageFactor) ||
        damageFactor < 0.0)
        return failure<void>(DiagnosticCode::InvalidArgument,
                             "RTS projectile requires a source and positive canonical weapon speed", "projectile");
    auto id = LogicalId::fromParts("projectile", definition.id.empty() ? "weapon" : definition.id);
    if (!id) return failure<void>(DiagnosticCode::InvalidArgument, "weapon id cannot identify a projectile");
    weapon::ProjectileDefinition projectile;
    projectile.id = *id;
    projectile.speed = definition.projectile.speed;
    projectile.gravity = std::max(0.0f, definition.projectile.gravity);
    projectile.mode = projectile.gravity > 0.0 ? weapon::ProjectileMode::Ballistic
                                                : weapon::ProjectileMode::Linear;
    const double flight = std::max(1.0, static_cast<double>(std::max(1.0f, definition.range)) /
                                           static_cast<double>(definition.projectile.speed) * 2.0);
    auto lifetime = Duration::fromSeconds(flight);
    if (!lifetime) return Result<void>::failure(lifetime.status());
    projectile.lifetime = lifetime.value();
    WorldPosition destination = targetPoint;
    if (auto live = entityPosition(target)) destination = *live;
    weapon::ProjectileSpawnRequest request;
    request.position = {origin.x, originHeight, origin.y};
    const double dx = destination.x - origin.x;
    const double dz = destination.y - origin.y;
    double dy = static_cast<double>(targetHeight - originHeight);
    if (projectile.mode == weapon::ProjectileMode::Ballistic) {
        const double horizontal = std::hypot(dx, dz);
        const double speed2 = static_cast<double>(definition.projectile.speed) *
                              static_cast<double>(definition.projectile.speed);
        const double gravity = static_cast<double>(definition.projectile.gravity);
        const double discriminant = speed2 * speed2 - gravity *
            (gravity * horizontal * horizontal + 2.0 * dy * speed2);
        if (horizontal > 1e-6 && discriminant >= 0.0)
            dy = horizontal * (speed2 - std::sqrt(discriminant)) / (gravity * horizontal);
    }
    request.direction = {dx, dy, dz};
    auto spawned = runtime_.spawn(projectile, request);
    if (!spawned) return Result<void>::failure(spawned.status());
    Payload payload;
    payload.source = source;
    payload.faction = faction;
    payload.target = target;
    payload.observer = observer;
    payload.targetPoint = destination;
    payload.targetHeight = targetHeight;
    payload.damageType = definition.damageType.empty() ? "damage.physical" : definition.damageType;
    const float launchDistance = std::hypot(destination.x - origin.x, destination.y - origin.y);
    payload.damage = static_cast<double>(definition.damage) * std::max(1, definition.projectile.pelletCount) *
                     damageFactor * weaponRangeDamageFactor(definition, launchDistance);
    payload.radius = std::max(0.0f, definition.projectile.aoe);
    payload.splashMinimumDamageFactor = definition.splashMinimumDamageFactor;
    payload.targetsGround = definition.targetsGround;
    payload.targetsAir = definition.targetsAir;
    payload.friendlyFire = definition.friendlyFire;
    payload.blockedByObstacles = definition.blockedByObstacles;
    payload.requiredTargetTags = definition.requiredTargetTags;
    payload.excludedTargetTags = definition.excludedTargetTags;
    payloads_[key(spawned.value())] = std::move(payload);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<std::size_t> RTSProjectileSystem::step(const SimulationStep& step, combat::DamageRuntime& damage,
                                              const ProjectileCollisionQuery& collision,
                                              const DamageEventSink& damageEvents) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS projectile delta must be non-negative",
                                    "step.delta");
    auto observerSees = [](ecs::EntityHandle observerHandle, ecs::EntityHandle targetHandle) {
        const auto targetPosition = entityPosition(targetHandle);
        if (!targetPosition) return false;
        bool cloaked = false;
        if (auto* targetUnit = dynamic_cast<Unit*>(ecs::try_get(targetHandle)))
            cloaked = targetUnit->vision()->cloaked;
        if (auto* observer = dynamic_cast<Unit*>(ecs::try_get(observerHandle))) {
            if (!observer->durability()->alive || observer->containment()->container.isBound() ||
                !observer->vision()->enabled) return false;
            const float range = cloaked ? observer->vision()->detectionRange : observer->vision()->sightRange;
            return range > 0.0f && distanceSquared(observer->motion()->x, observer->motion()->y,
                targetPosition->x, targetPosition->y) <= range * range;
        }
        if (auto* observer = dynamic_cast<Building*>(ecs::try_get(observerHandle))) {
            if (!observer->integrity()->alive || observer->construction()->progress < 1.0f ||
                !observer->infrastructure()->powered || !observer->vision()->enabled || cloaked) return false;
            const float range = observer->vision()->sightRange;
            return range > 0.0f && distanceSquared(observer->placement()->worldX, observer->placement()->worldY,
                targetPosition->x, targetPosition->y) <= range * range;
        }
        return false;
    };
    for (auto& [payloadKey, payload] : payloads_) {
        (void)payloadKey;
        if (payload.observer.table == nullptr) continue;
        if (observerSees(payload.observer, payload.target)) {
            if (const auto live = entityPosition(payload.target)) payload.targetPoint = *live;
        } else {
            payload.target = {};
            payload.observer = {};
        }
    }
    std::map<std::uint64_t, weapon::ProjectileState> before;
    for (const auto& state : runtime_.states()) before.emplace(key(state.handle), state);
    auto updated = runtime_.update(step.delta, this);
    if (!updated) return failureFrom<std::size_t>(updated.status());
    for (const auto& released : updated.value().released) payloads_.erase(key(released));
    std::size_t impacts = 0;
    for (const auto& handle : updated.value().advanced) {
        const auto old = before.find(key(handle));
        const auto current = runtime_.find(handle);
        const auto payloadIt = payloads_.find(key(handle));
        if (old == before.end() || !current || payloadIt == payloads_.end()) continue;
        Payload& payload = payloadIt->second;
        WorldPosition destination = payload.targetPoint;
        if (auto live = entityPosition(payload.target)) destination = *live;
        const double ax = old->second.position.x, ay = old->second.position.z;
        const double bx = current->position.x, by = current->position.z;
        ecs::EntityHandle impactEntity = payload.target;
        bool collided = false;
        if (payload.blockedByObstacles && collision) {
            auto queried = collision({static_cast<float>(ax), static_cast<float>(ay)},
                                     static_cast<float>(old->second.position.y),
                                     {static_cast<float>(bx), static_cast<float>(by)},
                                     static_cast<float>(current->position.y),
                                     payload.source, payload.target);
            if (!queried) return failureFrom<std::size_t>(queried.status());
            if (queried.value()) {
                destination = queried.value()->position;
                impactEntity = queried.value()->entity;
                collided = true;
            }
        }
        const double az = old->second.position.y, bz = current->position.y;
        const double vx = bx - ax, vy = by - ay, vz = bz - az;
        const double wx = destination.x - ax, wy = destination.y - ay, wz = payload.targetHeight - az;
        const double length2 = vx * vx + vy * vy + vz * vz;
        const double t = length2 <= 1e-12 ? 0.0 :
            std::clamp((wx * vx + wy * vy + wz * vz) / length2, 0.0, 1.0);
        const double dx = ax + vx * t - destination.x;
        const double dy = ay + vy * t - destination.y;
        const double dz = az + vz * t - payload.targetHeight;
        if (!collided && dx * dx + dy * dy + dz * dz > 0.25) continue;

        auto apply = [&](ecs::Entity* entity, double scale) -> Result<void> {
            combat::CombatState* state = nullptr; bool* alive = nullptr; FactionLink* faction = nullptr;
            RTSEffectComponent* effects = nullptr;
            float* shield = nullptr; float* cooldown = nullptr; float delay = 0.0f; SubjectRef subject;
            TagSet* tags = nullptr; bool airborne = false;
            if (auto* unit = dynamic_cast<Unit*>(entity)) {
                state = &unit->durability()->state; alive = &unit->durability()->alive;
                faction = &unit->faction()->link; shield = &unit->shield()->value;
                effects = &unit->effects()->values;
                cooldown = &unit->shield()->cooldown; delay = unit->shield()->regenDelay;
                subject = unit->identity()->subject; tags = &unit->tags()->values;
                airborne = unit->motion()->airborne;
            } else if (auto* building = dynamic_cast<Building*>(entity)) {
                state = &building->integrity()->state; alive = &building->integrity()->alive;
                faction = &building->faction()->link; shield = &building->shield()->value;
                effects = &building->effects()->values;
                cooldown = &building->shield()->cooldown; delay = building->shield()->regenDelay;
                subject = building->identity()->subject; tags = &building->tags()->values;
            }
            if (state == nullptr || alive == nullptr || !*alive || faction == nullptr ||
                (!payload.friendlyFire && FactionRelationSystem::allied(
                    dynamic_cast<Faction*>(faction->resolve()),
                    dynamic_cast<Faction*>(ecs::try_get(payload.faction)))) ||
                !subject.isValid() || (airborne && !payload.targetsAir) ||
                (!airborne && !payload.targetsGround) || tags == nullptr ||
                std::any_of(payload.requiredTargetTags.begin(), payload.requiredTargetTags.end(),
                    [&](const auto& tag) { return !tags->contains(tag); }) ||
                std::any_of(payload.excludedTargetTags.begin(), payload.excludedTargetTags.end(),
                    [&](const auto& tag) { return tags->contains(tag); }))
                return Result<void>::success(Status::success(StatusCode::NoOp));
            combat::DamageRequest request;
            request.source = payload.source; request.target = subject; request.damageType = payload.damageType;
            request.healthDamage = payload.damage * scale * effects->multiplier("incomingDamageMultiplier");
            if (shield != nullptr && *shield > 0.0f) {
                const float absorbed = std::min(*shield, static_cast<float>(request.healthDamage));
                *shield -= absorbed; request.healthDamage -= absorbed;
                if (cooldown != nullptr) *cooldown = delay;
            }
            auto outcome = damage.apply(*state, request);
            if (!outcome) return Result<void>::failure(outcome.status());
            if (damageEvents) damageEvents(request, outcome.value(), step.tick, DamageChannel::Projectile);
            if (outcome.value().reaction == combat::HitReaction::Death) {
                if (Unit* source = unitBySubject(payload.source);
                    source != nullptr && hostileTo(*source, *faction)) {
                    auto awarded = VeterancySystem::award(*source,
                        static_cast<float>(std::max(1.0, state->maxHealth)));
                    if (!awarded) return Result<void>::failure(awarded.status());
                    std::move(awarded).takeValue();
                }
                *alive = false;
            }
            return Result<void>::success(Status::success(StatusCode::Applied));
        };
        if (payload.radius <= 0.0f) {
            auto result = apply(ecs::try_get(impactEntity), 1.0);
            if (!result) return failureFrom<std::size_t>(result.status());
        } else {
            auto units = ecs::View<Unit, Unit::Identity, Unit::Motion>();
            for (auto it = units.begin(); it != units.end(); ++it) {
                auto [identity, motion] = *it;
                const float distance = std::hypot(motion->x - destination.x, motion->y - destination.y);
                if (distance > payload.radius) continue;
                const double radial = 1.0 - distance / payload.radius;
                auto result = apply(ecs::try_get(identity->self),
                    std::max<double>(payload.splashMinimumDamageFactor, radial));
                if (!result) return failureFrom<std::size_t>(result.status());
            }
            auto buildings = ecs::View<Building, Building::Identity, Building::Placement>();
            for (auto it = buildings.begin(); it != buildings.end(); ++it) {
                auto [identity, placement] = *it;
                const float distance = std::hypot(placement->worldX - destination.x,
                                                  placement->worldY - destination.y);
                if (distance > payload.radius) continue;
                const double radial = 1.0 - distance / payload.radius;
                auto result = apply(ecs::try_get(identity->self),
                    std::max<double>(payload.splashMinimumDamageFactor, radial));
                if (!result) return failureFrom<std::size_t>(result.status());
            }
        }
        auto released = runtime_.release(handle);
        if (!released) return failureFrom<std::size_t>(released.status());
        payloads_.erase(key(handle));
        ++impacts;
    }
    return Result<std::size_t>::success(impacts,
        Status::success(impacts == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> CombatFireSystem::step(const SimulationStep& step, State& state,
                                           sensing::SensingWorld& sensing, combat::DamageRuntime& damage,
                                           RTSProjectileSystem* projectiles, const FireLineQuery& fireLine,
                                           map::Pathfinder* pathfinder, const NavigationGrid& navigationGrid,
                                           const CombatHeightQuery& heightQuery,
                                           const DamageEventSink& damageEvents,
                                           const CombatFireEventSink& fireEvents) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS combat step delta must be non-negative",
                                    "step.delta");
    const auto launchHeights = [&](WorldPosition origin, WorldPosition target,
                                    ecs::EntityHandle source, ecs::EntityHandle targetEntity) {
        return heightQuery ? heightQuery(origin, target, source, targetEntity) : CombatHeightProfile{};
    };
    const auto publishShot = [&](weapon::WeaponEntity& weaponEntity, SubjectRef source,
                                 SubjectRef target, WorldPosition point, bool projectile, bool missed) {
        if (!fireEvents) return;
        fireEvents({projectile ? CombatFireEventKind::ProjectileFired
                               : CombatFireEventKind::WeaponFired,
                    source, target, point}, step.tick);
        if (missed)
            fireEvents({CombatFireEventKind::ShotMissed, source, target, point}, step.tick);
        const auto& resource = weaponEntity.state()->resource;
        if (resource.kind == weapon::ResourceKind::Ammo && !resource.infinite && resource.value <= 0.0f)
            fireEvents({CombatFireEventKind::WeaponDry, source, target, point}, step.tick);
    };
    std::set<std::string> blockedThisStep;
    const auto publishBlocked = [&](SubjectRef source, SubjectRef target, WorldPosition point) {
        const std::string key = source.format();
        blockedThisStep.insert(key);
        if (fireEvents && !state.blockedSubjects.contains(key))
            fireEvents({CombatFireEventKind::FireBlocked, source, target, point}, step.tick);
    };
    const auto updateWeapon = [&](weapon::WeaponEntity& weaponEntity, SubjectRef source,
                                  WorldPosition position) {
        const auto* definition = weaponEntity.definition()->def;
        auto weaponState = weaponEntity.state();
        const auto& resource = weaponState->resource;
        const float dt = static_cast<float>(step.delta.seconds());
        const bool hasReserve = weaponState->ammoPool != nullptr
                                    ? weaponState->ammoPool->state()->count > 0
                                    : definition->reserveSize != 0 && resource.reserve > 0;
        const bool startsReload = definition->kind == weapon::WeaponKind::Ranged &&
                                  !resource.reloading && resource.value <= 0.0f &&
                                  weaponState->cooldown <= dt && !weaponState->jammed && hasReserve;
        const bool completesReload = definition->kind == weapon::WeaponKind::Ranged &&
                                     resource.reloading &&
                                     resource.reloadProgress + dt >= definition->reloadTime;
        weapon::WeaponSystem::update(weaponEntity, dt);
        if (startsReload && fireEvents)
            fireEvents({CombatFireEventKind::ReloadStarted, source, {}, position}, step.tick);
        if (completesReload && fireEvents)
            fireEvents({CombatFireEventKind::ReloadCompleted, source, {}, position}, step.tick);
    };
    struct Target {
        ecs::EntityHandle handle{};
        SubjectRef subject;
        FactionLink* faction = nullptr;
        WorldPosition position{};
        combat::CombatState* durability = nullptr;
        bool* alive = nullptr;
        Unit::Morale* morale = nullptr;
        float* shield = nullptr;
        float* shieldCooldown = nullptr;
        float shieldDelay = 0.0f;
        RTSEffectComponent* effects = nullptr;
        TagSet* tags = nullptr;
        bool airborne = false;
        std::string definition;
        bool cloaked = false;
    };
    std::map<std::string, Target> targets;
    for (const auto& id : state.mirroredSubjects) {
        auto removed = sensing.remove(id);
        if (!removed && removed.code() != StatusCode::NotFound) return failureFrom<std::size_t>(removed.status());
        if (!removed) removed.ignore("RTS sensing mirror was already absent");
    }
    state.mirroredSubjects.clear();
    auto mirror = [&](std::string id, Target target, std::string_view tags) -> Result<void> {
        const std::string faction = target.faction == nullptr ? std::string{} : factionKey(*target.faction);
        auto inserted = sensing.upsert(id, target.position.x, target.position.y, faction, tags, "");
        if (!inserted) return Result<void>::failure(inserted.status());
        state.mirroredSubjects.insert(id);
        targets.emplace(std::move(id), target);
        return Result<void>::success(Status::success(StatusCode::Applied));
    };
    {
        auto units = ecs::View<Unit, Unit::Identity, Unit::Definition, Unit::Motion, Unit::Faction, Unit::Durability,
                               Unit::Containment, Unit::Morale, Unit::Shield, Unit::Effects, Unit::Tags, Unit::Vision>();
        for (auto it = units.begin(); it != units.end(); ++it) {
            auto [identity, unitDefinition, motion, faction, durability, containment, morale, shield, effects, tags,
                  vision] = *it;
            if (!identity->subject.isValid() || !durability->alive || durability->state.health <= 0.0 ||
                containment->container.isBound()) continue;
            auto result = mirror(identity->subject.format(),
                                 {identity->self, identity->subject, &faction->link, {motion->x, motion->y},
                                  &durability->state, &durability->alive, morale, &shield->value,
                                  &shield->cooldown, shield->regenDelay, &effects->values, &tags->values, motion->airborne,
                                  unitDefinition->id.format(), vision->cloaked},
                                 "combat-target,unit");
            if (!result) return failureFrom<std::size_t>(result.status());
        }
    }
    {
        auto buildings = ecs::View<Building, Building::Identity, Building::Definition, Building::Placement,
                                   Building::Faction, Building::Integrity, Building::Shield, Building::Effects,
                                   Building::Tags>();
        for (auto it = buildings.begin(); it != buildings.end(); ++it) {
            auto [identity, buildingDefinition, placement, faction, integrity, shield, effects, tags] = *it;
            if (!identity->subject.isValid() || !integrity->alive || integrity->state.health <= 0.0) continue;
            auto result = mirror(identity->subject.format(),
                                 {identity->self, identity->subject, &faction->link,
                                  {placement->worldX, placement->worldY}, &integrity->state, &integrity->alive,
                                  nullptr, &shield->value, &shield->cooldown, shield->regenDelay,
                                  &effects->values, &tags->values, false, buildingDefinition->id.format(), false},
                                 "building,combat-target");
            if (!result) return failureFrom<std::size_t>(result.status());
        }
    }

    std::size_t fired = 0;
    auto observedFireSpotter = [&](ecs::Entity* ownFaction, const Target& target) -> ecs::EntityHandle {
        ecs::EntityHandle best{};
        std::string bestKey;
        auto consider = [&](ecs::EntityHandle handle, ecs::Entity* observerFaction, WorldPosition position,
                            float sightRange, float detectionRange, bool enabled) {
            const float range = target.cloaked ? detectionRange : sightRange;
            const SubjectRef subject = stableSubject(ecs::try_get(handle));
            if (!enabled || !FactionRelationSystem::allied(
                    dynamic_cast<Faction*>(observerFaction), dynamic_cast<Faction*>(ownFaction)) ||
                range <= 0.0f || !subject.isValid() ||
                distanceSquared(position.x, position.y, target.position.x, target.position.y) > range * range)
                return;
            const std::string key = subject.format();
            if (best.table == nullptr || key < bestKey) { best = handle; bestKey = key; }
        };
        auto unitObservers = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Faction, Unit::Vision,
                                       Unit::Durability, Unit::Containment>();
        for (auto it = unitObservers.begin(); it != unitObservers.end(); ++it) {
            auto [identity, motion, faction, vision, durability, containment] = *it;
            if (!durability->alive || containment->container.isBound()) continue;
            consider(identity->self, faction->link.resolve(), {motion->x, motion->y}, vision->sightRange,
                     vision->detectionRange, vision->enabled);
        }
        auto buildingObservers = ecs::View<Building, Building::Identity, Building::Placement, Building::Faction,
                                           Building::Vision, Building::Integrity, Building::Construction,
                                           Building::Infrastructure>();
        for (auto it = buildingObservers.begin(); it != buildingObservers.end(); ++it) {
            auto [identity, placement, faction, vision, integrity, construction, infrastructure] = *it;
            if (!integrity->alive || construction->progress < 1.0f || !infrastructure->powered) continue;
            consider(identity->self, faction->link.resolve(), {placement->worldX, placement->worldY},
                     vision->sightRange, target.cloaked ? 0.0f : vision->detectionRange, vision->enabled);
        }
        return best;
    };
    auto attackers = ecs::View<Unit, Unit::Identity, Unit::Motion, Unit::Orders, Unit::Faction, Unit::Weapon,
                               Unit::Combat, Unit::Durability, Unit::Containment, Unit::Morale, Unit::Artillery,
                               Unit::Veterancy, Unit::Command, Unit::Tactics, Unit::Vision, Unit::Effects>();
    for (auto it = attackers.begin(); it != attackers.end(); ++it) {
        auto [identity, motion, orders, faction, weaponLink, policy, durability, containment, morale, artillery,
              veterancy, command, tactics, vision, effects] = *it;
        auto* unit = dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        if (!durability->alive || durability->state.health <= 0.0 || containment->container.isBound()) continue;
        if (!std::isfinite(veterancy->experience) || veterancy->experience < 0.0f ||
            !std::isfinite(veterancy->veteranThreshold) || !std::isfinite(veterancy->eliteThreshold) ||
            veterancy->veteranThreshold < 0.0f || veterancy->eliteThreshold < 0.0f ||
            (veterancy->veteranThreshold > 0.0f && veterancy->eliteThreshold <= veterancy->veteranThreshold) ||
            !std::isfinite(veterancy->veteranDamageFactor) || !std::isfinite(veterancy->eliteDamageFactor) ||
            !std::isfinite(veterancy->veteranHealthFactor) || !std::isfinite(veterancy->eliteHealthFactor) ||
            veterancy->veteranDamageFactor < 1.0f ||
            veterancy->eliteDamageFactor < veterancy->veteranDamageFactor ||
            veterancy->veteranHealthFactor < 1.0f ||
            veterancy->eliteHealthFactor < veterancy->veteranHealthFactor || veterancy->level < 0 ||
            veterancy->level > 2)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                        "RTS veterancy thresholds and factors are inconsistent",
                                        "unit.veterancy");
        auto* weaponEntity = dynamic_cast<weapon::WeaponEntity*>(weaponLink->link.resolve());
        if (weaponEntity == nullptr || weaponEntity->definition()->def == nullptr) continue;
        updateWeapon(*weaponEntity, identity->subject, {motion->x, motion->y});
        const weapon::WeaponDefinition& definition = *weaponEntity->definition()->def;
        if (!std::isfinite(definition.preferredTargetBonus) || definition.preferredTargetBonus < 1.0f)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                        "RTS weapon preferred target bonus must be finite and at least one",
                                        "weapon.preferredTargetBonus");
        policy->engagementRange = std::max(0.0f, definition.range);
        if (std::any_of(policy->targetPriorities.begin(), policy->targetPriorities.end(), [](const auto& entry) {
                return entry.first.empty() || !std::isfinite(entry.second) || entry.second < 0.0f;
            }))
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                        "RTS target priority entries require non-empty ids and finite weights",
                                        "unit.combat.targetPriorities");
        if (!std::isfinite(policy->turnRateDegrees) || policy->turnRateDegrees < 0.0f ||
            !std::isfinite(policy->aimToleranceDegrees) || policy->aimToleranceDegrees < 0.0f)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                        "RTS turret turn rate and aim tolerance must be finite and non-negative",
                                        "unit.combat.aim");

        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        const bool explicitAttack = record && record->kind == OrderKind::Attack;
        if (explicitAttack) policy->target = record->targetEntity;

        if (!std::isfinite(tactics->coordinatedVolleyInterval) ||
            tactics->coordinatedVolleyInterval < 0.0f ||
            !std::isfinite(tactics->volleyReleaseRemaining) || tactics->volleyReleaseRemaining < 0.0f)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                        "RTS coordinated volley timing must be finite and non-negative",
                                        "unit.tactics.coordinatedVolley");
        bool volleyReleased = true;
        if (!explicitAttack && tactics->combatGroup != 0 && tactics->coordinatedVolleyInterval > 0.0f) {
            if (tactics->volleyReleaseRemaining <= 1e-5f)
                tactics->volleyReleaseRemaining = tactics->coordinatedVolleyInterval;
            tactics->volleyReleaseRemaining = std::max(
                0.0f, tactics->volleyReleaseRemaining - static_cast<float>(step.delta.seconds()));
            volleyReleased = tactics->volleyReleaseRemaining <= 1e-5f;
            tactics->volleyHolding = !volleyReleased;
            if (volleyReleased) tactics->volleyReleaseRemaining = tactics->coordinatedVolleyInterval;
        } else {
            tactics->volleyHolding = false;
        }

        const bool groundFire = record && (record->kind == OrderKind::AttackGround ||
                                            record->kind == OrderKind::SuppressArea);
        if (groundFire) {
            if (artillery->deployRemaining > 0.0f) continue;
            WorldPosition aimPoint = record->target;
            if (record->kind == OrderKind::SuppressArea) {
                const float lineX = record->secondaryTarget.x - record->target.x;
                const float lineY = record->secondaryTarget.y - record->target.y;
                const float lineLength = std::hypot(lineX, lineY);
                if (lineLength <= 1e-5f) {
                    auto failed = orders->values.fail(record->id, "suppression line must have non-zero length");
                    if (!failed) return failureFrom<std::size_t>(failed.status());
                    continue;
                }
                const std::uint32_t sequence = artillery->shotSequence++;
                const float t = (static_cast<float>((sequence * 37u) % 101u) + 0.5f) / 101.0f;
                const float lateral = (static_cast<float>((sequence * 53u) % 101u) / 100.0f - 0.5f) *
                                      2.0f * record->radius;
                aimPoint.x = record->target.x + lineX * t - lineY / lineLength * lateral;
                aimPoint.y = record->target.y + lineY * t + lineX / lineLength * lateral;
            }
            const float range = std::max(0.0f, definition.range);
            if (distanceSquared(motion->x, motion->y, aimPoint.x, aimPoint.y) > range * range) continue;
            if (fireLine && definition.blockedByObstacles && definition.projectile.gravity <= 0.0f) {
                auto clear = fireLine({motion->x, motion->y}, aimPoint, identity->self, {}, definition);
                if (!clear) return failureFrom<std::size_t>(clear.status());
                if (!clear.value()) {
                    publishBlocked(identity->subject, {}, aimPoint);
                    continue;
                }
            }
            const float yaw = std::atan2(aimPoint.y - motion->y, aimPoint.x - motion->x) * 180.0f /
                              static_cast<float>(std::numbers::pi);
            weaponEntity->aim()->yaw = yaw;
            weaponEntity->aim()->desiredYaw = yaw;
            weapon::AttackRequest attack;
            attack.targetX = aimPoint.x;
            attack.targetY = aimPoint.y;
            attack.hasTarget = true;
            attack.muzzleX = motion->x;
            attack.muzzleY = motion->y;
            attack.shooterId = static_cast<int>(identity->self.id);
            if (!weapon::WeaponSystem::tryFire(*weaponEntity, attack)) continue;
            const ShotPlacement shot = placeShot(identity->subject, policy->shotSequence++, definition, aimPoint);
            if (definition.projectile.speed > 0.0f &&
                (definition.projectile.gravity > 0.0f || artillery->deployTime > 0.0f)) {
                artillery->lastFireTick = step.tick;
                artillery->lastFirePosition = {motion->x, motion->y};
            }
            if (definition.projectile.speed > 0.0f && projectiles != nullptr) {
                const auto heights = launchHeights({motion->x, motion->y}, shot.point, identity->self, {});
                const float moraleFactor = morale->active
                                               ? std::clamp(morale->suppressedDamageFactor, 0.0f, 1.0f) : 1.0f;
                const float commandFactor = command->requiresCommand && !command->inCommand
                                                ? command->outOfCommandDamageFactor : 1.0f;
                auto launched = projectiles->launch(identity->subject, faction->link.handle(),
                                                     {motion->x, motion->y}, {}, shot.point, definition,
                                                     moraleFactor * commandFactor * policy->upgradeDamageFactor *
                                                     static_cast<float>(effects->values.multiplier("damageMultiplier")),
                                                     {}, heights.source, heights.target);
                if (!launched) return failureFrom<std::size_t>(launched.status());
            }
            publishShot(*weaponEntity, identity->subject, {}, shot.point,
                        definition.projectile.speed > 0.0f, shot.missed);
            ++fired;
            bool finished = record->kind == OrderKind::AttackGround;
            if (record->kind == OrderKind::SuppressArea && artillery->suppressionShotsRemaining > 0) {
                --artillery->suppressionShotsRemaining;
                finished = artillery->suppressionShotsRemaining == 0;
                if (finished) artillery->fireSupportRequester = {};
            }
            if (artillery->shootAndScootDistance > 0.0f) {
                const float dx = motion->x - aimPoint.x;
                const float dy = motion->y - aimPoint.y;
                const float length = std::hypot(dx, dy);
                const float awayX = length > 1e-5f ? dx / length : 1.0f;
                const float awayY = length > 1e-5f ? dy / length : 0.0f;
                WorldPosition relocation{motion->x + awayX * artillery->shootAndScootDistance,
                                         motion->y + awayY * artillery->shootAndScootDistance};
                if (pathfinder != nullptr) {
                    auto selected = ArtilleryRelocationSystem::select(*unit, aimPoint,
                                                                      artillery->shootAndScootDistance,
                                                                      definition.range, *pathfinder,
                                                                      navigationGrid);
                    if (selected) {
                        const auto choice = std::move(selected).takeValue();
                        relocation = choice.target;
                        artillery->relocationThreat = choice.threat;
                        artillery->relocationConflictCount = choice.conflicts;
                    } else if (selected.code() != StatusCode::NotFound) {
                        return failureFrom<std::size_t>(selected.status());
                    } else {
                        selected.ignore("artillery falls back when no canonical-map candidate is reachable");
                    }
                }
                auto completed = orders->values.complete(record->id);
                if (!completed) return failureFrom<std::size_t>(completed.status());
                CommandSpec relocate;
                relocate.kind = OrderKind::Move;
                relocate.target = relocation;
                auto moveOrder = orders->values.enqueue(relocate);
                if (!moveOrder) return failureFrom<std::size_t>(moveOrder.status());
                std::move(moveOrder).takeValue();
                if (!finished && record->kind == OrderKind::SuppressArea) {
                    CommandSpec resume;
                    resume.kind = OrderKind::SuppressArea;
                    resume.target = record->target;
                    resume.secondaryTarget = record->secondaryTarget;
                    resume.radius = record->radius;
                    auto resumed = orders->values.enqueue(resume);
                    if (!resumed) return failureFrom<std::size_t>(resumed.status());
                    std::move(resumed).takeValue();
                }
                artillery->departedPosition = {motion->x, motion->y};
                artillery->hasDepartedPosition = true;
                artillery->relocationTarget = relocation;
                artillery->relocating = true;
            } else if (finished) {
                auto completed = orders->values.complete(record->id);
                if (!completed) return failureFrom<std::size_t>(completed.status());
            }
            continue;
        }

        auto validTarget = [&](const ecs::EntityHandle& handle, bool applyLeash) -> Target* {
            auto* entity = ecs::try_get(handle);
            if (entity == nullptr || sameHandle(handle, identity->self)) return nullptr;
            auto found = std::find_if(targets.begin(), targets.end(), [&](auto& entry) {
                return sameHandle(entry.second.handle, handle);
            });
            if (found == targets.end() || found->second.alive == nullptr || !*found->second.alive ||
                found->second.faction == nullptr ||
                (!definition.friendlyFire &&
                 FactionRelationSystem::allied(*found->second.faction, faction->link)))
                return nullptr;
            if ((found->second.airborne && !definition.targetsAir) ||
                (!found->second.airborne && !definition.targetsGround)) return nullptr;
            if (found->second.tags == nullptr ||
                std::any_of(definition.requiredTargetTags.begin(), definition.requiredTargetTags.end(),
                    [&](const auto& tag) { return !found->second.tags->contains(tag); }) ||
                std::any_of(definition.excludedTargetTags.begin(), definition.excludedTargetTags.end(),
                    [&](const auto& tag) { return found->second.tags->contains(tag); })) return nullptr;
            const float distance = distanceSquared(motion->x, motion->y, found->second.position.x,
                                                   found->second.position.y);
            const bool indirect = definition.projectile.speed > 0.0f && definition.projectile.gravity > 0.0f;
            if (indirect && distance > vision->sightRange * vision->sightRange &&
                observedFireSpotter(faction->link.resolve(), found->second).table == nullptr)
                return nullptr;
            if (!explicitAttack && distance > policy->acquisitionRange * policy->acquisitionRange) return nullptr;
            if (explicitAttack && !FactionIntelSystem::targetable(
                    dynamic_cast<Faction*>(faction->link.resolve()), found->second.subject)) return nullptr;
            if (applyLeash && policy->stance != CombatStance::Aggressive &&
                policy->leashRange > 0.0f && policy->guardSet &&
                distanceSquared(policy->guardX, policy->guardY, found->second.position.x,
                                found->second.position.y) > policy->leashRange * policy->leashRange)
                return nullptr;
            return &found->second;
        };

        Target* target = validTarget(policy->target, !explicitAttack);
        if (explicitAttack && target == nullptr) {
            auto failed = orders->values.fail(record->id, "attack target is stale, allied, or destroyed");
            if (!failed) return failureFrom<std::size_t>(failed.status());
            policy->target = {};
            continue;
        }
        if (target == nullptr && policy->stance != CombatStance::Passive &&
            policy->acquisitionRange > 0.0f) {
            if (!policy->guardSet) {
                policy->guardX = motion->x;
                policy->guardY = motion->y;
                policy->guardSet = true;
            }
            const std::string ownFaction = factionKey(faction->link);
            auto queried = sensing.circle(motion->x, motion->y, policy->acquisitionRange, "combat-target", "",
                                          "", ownFaction, "", static_cast<int>(targets.size()));
            if (!queried) return failureFrom<std::size_t>(queried.status());
            float bestPriority = -1.0f;
            for (int index = 0; index < queried.value(); ++index) {
                auto candidate = sensing.resultAt(index);
                if (!candidate) continue;
                auto found = targets.find(candidate->get().id);
                if (found == targets.end()) continue;
                Target* accepted = validTarget(found->second.handle, true);
                if (accepted != nullptr) {
                    auto preferred = policy->targetPriorities.find(accepted->definition);
                    const float policyPriority = preferred == policy->targetPriorities.end() ? 1.0f : preferred->second;
                    const float priority = policyPriority * weaponTargetPreference(definition, accepted->tags);
                    if (target == nullptr || priority > bestPriority) {
                        target = accepted;
                        bestPriority = priority;
                    }
                }
            }
            if (target != nullptr) policy->target = target->handle;
        }
        if (target == nullptr) {
            policy->target = {};
            artillery->usingObservedFire = false;
            artillery->observedFireSpotter = {};
            continue;
        }
        const bool indirect = definition.projectile.speed > 0.0f && definition.projectile.gravity > 0.0f;
        const float targetDistance = distanceSquared(motion->x, motion->y, target->position.x, target->position.y);
        if (indirect && targetDistance > vision->sightRange * vision->sightRange) {
            artillery->observedFireSpotter = observedFireSpotter(faction->link.resolve(), *target);
            artillery->usingObservedFire = artillery->observedFireSpotter.table != nullptr;
        } else {
            artillery->observedFireSpotter = {};
            artillery->usingObservedFire = false;
        }
        if (!volleyReleased) continue;
        const float range = std::max(0.0f, definition.range);
        if (distanceSquared(motion->x, motion->y, target->position.x, target->position.y) > range * range) continue;
        if (fireLine && definition.blockedByObstacles && definition.projectile.gravity <= 0.0f) {
            auto clear = fireLine({motion->x, motion->y}, target->position, identity->self,
                                  target->handle, definition);
            if (!clear) return failureFrom<std::size_t>(clear.status());
            if (!clear.value()) {
                publishBlocked(identity->subject, target->subject, target->position);
                continue;
            }
        }

        const float yaw = std::atan2(target->position.y - motion->y, target->position.x - motion->x) *
                          180.0f / static_cast<float>(std::numbers::pi);
        weaponEntity->aim()->desiredYaw = yaw;
        weaponEntity->aim()->turnSpeed = policy->turnRateDegrees;
        weapon::WeaponSystem::updateAim(*weaponEntity, static_cast<float>(step.delta.seconds()));
        if (std::fabs(std::remainder(weaponEntity->aim()->desiredYaw - weaponEntity->aim()->yaw, 360.0f)) >
            policy->aimToleranceDegrees) continue;
        weapon::AttackRequest attack;
        attack.targetX = target->position.x;
        attack.targetY = target->position.y;
        attack.hasTarget = true;
        attack.targetHandle = target->handle;
        attack.muzzleX = motion->x;
        attack.muzzleY = motion->y;
        attack.shooterId = static_cast<int>(identity->self.id);
        if (!weapon::WeaponSystem::tryFire(*weaponEntity, attack)) continue;
        const ShotPlacement shot = placeShot(identity->subject, policy->shotSequence++, definition,
                                             target->position);
        if (definition.projectile.speed > 0.0f &&
            (definition.projectile.gravity > 0.0f || artillery->deployTime > 0.0f)) {
            artillery->lastFireTick = step.tick;
            artillery->lastFirePosition = {motion->x, motion->y};
        }
        if (definition.projectile.speed > 0.0f && projectiles != nullptr) {
            const auto heights = launchHeights({motion->x, motion->y}, shot.point,
                                               identity->self, target->handle);
            const float moraleFactor = morale->active ? std::clamp(morale->suppressedDamageFactor, 0.0f, 1.0f)
                                                       : 1.0f;
            const float commandFactor = command->requiresCommand && !command->inCommand
                                            ? command->outOfCommandDamageFactor : 1.0f;
            auto launched = projectiles->launch(identity->subject, faction->link.handle(),
                                                 {motion->x, motion->y}, shot.missed ? ecs::EntityHandle{} : target->handle,
                                                 shot.point,
                                                 definition, moraleFactor * commandFactor *
                                                                 policy->upgradeDamageFactor *
                                                                 static_cast<float>(effects->values.multiplier("damageMultiplier")),
                                                 artillery->observedFireSpotter, heights.source, heights.target);
            if (!launched) return failureFrom<std::size_t>(launched.status());
        }
        publishShot(*weaponEntity, identity->subject, target->subject, shot.point,
                    definition.projectile.speed > 0.0f, shot.missed);
        ++fired;

        // Projectile services own delayed impact. Hitscan/melee settles through
        // the canonical combat runtime immediately at the weapon's Active edge.
        if (definition.projectile.speed <= 0.0f && !shot.missed && target->durability != nullptr) {
            combat::DamageRequest request;
            request.source = identity->subject;
            request.target = target->subject;
            request.damageType = definition.damageType.empty() ? "damage.physical" : definition.damageType;
            const float damageFactor = morale->active ? std::clamp(morale->suppressedDamageFactor, 0.0f, 1.0f) : 1.0f;
            const float commandFactor = command->requiresCommand && !command->inCommand
                                            ? command->outOfCommandDamageFactor : 1.0f;
            const float rangeFactor = weaponRangeDamageFactor(definition,
                std::hypot(target->position.x - motion->x, target->position.y - motion->y));
            request.healthDamage = static_cast<double>(definition.damage * damageFactor * commandFactor *
                                                       policy->upgradeDamageFactor * rangeFactor *
                                                       static_cast<float>(effects->values.multiplier("damageMultiplier"))) *
                                   static_cast<double>(std::max(1, definition.projectile.pelletCount));
            request.healthDamage *= target->effects->multiplier("incomingDamageMultiplier");
            if (target->shield != nullptr && *target->shield > 0.0f && request.healthDamage > 0.0) {
                const float absorbed = std::min(*target->shield, static_cast<float>(request.healthDamage));
                *target->shield -= absorbed;
                request.healthDamage -= absorbed;
                if (target->shieldCooldown != nullptr) *target->shieldCooldown = target->shieldDelay;
            }
            auto outcome = damage.apply(*target->durability, request);
            if (!outcome) return failureFrom<std::size_t>(outcome.status());
            if (damageEvents) damageEvents(request, outcome.value(), step.tick, DamageChannel::Weapon);
            if (target->morale != nullptr && target->morale->capacity > 0.0f && policy->suppressionPerShot > 0.0f) {
                float auraFactor = 1.0f;
                auto allies = ecs::View<Unit, Unit::Motion, Unit::Faction, Unit::Morale, Unit::Containment,
                                        Unit::Durability>();
                for (auto allyIt = allies.begin(); allyIt != allies.end(); ++allyIt) {
                    auto [allyMotion, allyFaction, aura, allyContainment, allyDurability] = *allyIt;
                    if (!allyDurability->alive || allyContainment->container.isBound() || aura->auraRange <= 0.0f ||
                        !FactionRelationSystem::allied(allyFaction->link, *target->faction)) continue;
                    if (distanceSquared(target->position.x, target->position.y, allyMotion->x, allyMotion->y) <=
                        aura->auraRange * aura->auraRange)
                        auraFactor = std::min(auraFactor, std::clamp(aura->auraSuppressionFactor, 0.0f, 1.0f));
                }
                target->morale->suppression = std::min(target->morale->capacity,
                    target->morale->suppression + policy->suppressionPerShot * auraFactor * rangeFactor);
                if (target->morale->suppression >= target->morale->capacity * 0.5f) target->morale->active = true;
                if (target->morale->retreatEnabled && !target->morale->retreating &&
                    target->morale->suppression >= target->morale->capacity * target->morale->retreatThreshold) {
                    auto* targetUnit = dynamic_cast<Unit*>(ecs::try_get(target->handle));
                    if (targetUnit != nullptr) {
                        float awayX = target->position.x - motion->x;
                        float awayY = target->position.y - motion->y;
                        const float length = std::hypot(awayX, awayY);
                        if (length <= 1e-5f) { awayX = 1.0f; awayY = 0.0f; }
                        else { awayX /= length; awayY /= length; }
                        CommandSpec retreat;
                        retreat.kind = OrderKind::Move;
                        retreat.target = {target->position.x + awayX * target->morale->retreatDistance,
                                          target->position.y + awayY * target->morale->retreatDistance};
                        auto replaced = targetUnit->orders()->values.replace(retreat);
                        if (!replaced) return failureFrom<std::size_t>(replaced.status());
                        std::move(replaced).takeValue();
                        target->morale->retreating = true;
                    }
                }
            }
            if (outcome.value().reaction == combat::HitReaction::Death) {
                *target->alive = false;
                auto awarded = VeterancySystem::award(
                    *dynamic_cast<Unit*>(ecs::try_get(identity->self)),
                    static_cast<float>(std::max(1.0, target->durability->maxHealth)));
                if (!awarded) return failureFrom<std::size_t>(awarded.status());
                std::move(awarded).takeValue();
                policy->target = {};
                if (explicitAttack) {
                    auto completed = orders->values.complete(record->id);
                    if (!completed) return failureFrom<std::size_t>(completed.status());
                }
            }
        }
    }
    auto armedBuildings = ecs::View<Building, Building::Identity, Building::Placement, Building::Orders,
                                    Building::Faction, Building::Weapon, Building::Combat, Building::Integrity,
                                    Building::Construction, Building::Garrison>();
    for (auto it = armedBuildings.begin(); it != armedBuildings.end(); ++it) {
        auto [identity, placement, orders, faction, weaponLink, policy, integrity, construction, garrison] = *it;
        if (!integrity->alive || integrity->state.health <= 0.0 || construction->progress < 1.0f) continue;
        auto* weaponEntity = dynamic_cast<weapon::WeaponEntity*>(weaponLink->link.resolve());
        if (weaponEntity == nullptr || weaponEntity->definition()->def == nullptr) continue;
        updateWeapon(*weaponEntity, identity->subject, {placement->worldX, placement->worldY});
        const weapon::WeaponDefinition& definition = *weaponEntity->definition()->def;
        if (!std::isfinite(definition.preferredTargetBonus) || definition.preferredTargetBonus < 1.0f)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                        "RTS weapon preferred target bonus must be finite and at least one",
                                        "weapon.preferredTargetBonus");
        policy->engagementRange = std::max(0.0f, definition.range);
        if (std::any_of(policy->targetPriorities.begin(), policy->targetPriorities.end(), [](const auto& entry) {
                return entry.first.empty() || !std::isfinite(entry.second) || entry.second < 0.0f;
            }))
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                        "RTS target priority entries require non-empty ids and finite weights",
                                        "building.combat.targetPriorities");
        if (!std::isfinite(policy->turnRateDegrees) || policy->turnRateDegrees < 0.0f ||
            !std::isfinite(policy->aimToleranceDegrees) || policy->aimToleranceDegrees < 0.0f)
            return failure<std::size_t>(DiagnosticCode::InvalidArgument,
                                        "RTS turret turn rate and aim tolerance must be finite and non-negative",
                                        "building.combat.aim");
        const float acquisitionRange = policy->acquisitionRange > 0.0f ? policy->acquisitionRange
                                                                       : policy->engagementRange;

        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        const bool explicitAttack = record && record->kind == OrderKind::Attack;
        if (explicitAttack) policy->target = record->targetEntity;
        auto resolveTarget = [&](const ecs::EntityHandle& handle) -> Target* {
            auto found = std::find_if(targets.begin(), targets.end(), [&](auto& entry) {
                return sameHandle(entry.second.handle, handle);
            });
            if (found == targets.end() || found->second.alive == nullptr || !*found->second.alive ||
                found->second.faction == nullptr ||
                (!definition.friendlyFire &&
                 FactionRelationSystem::allied(*found->second.faction, faction->link)) ||
                sameHandle(found->second.handle, identity->self)) return nullptr;
            if ((found->second.airborne && !definition.targetsAir) ||
                (!found->second.airborne && !definition.targetsGround)) return nullptr;
            if (found->second.tags == nullptr ||
                std::any_of(definition.requiredTargetTags.begin(), definition.requiredTargetTags.end(),
                    [&](const auto& tag) { return !found->second.tags->contains(tag); }) ||
                std::any_of(definition.excludedTargetTags.begin(), definition.excludedTargetTags.end(),
                    [&](const auto& tag) { return found->second.tags->contains(tag); })) return nullptr;
            if (explicitAttack && !FactionIntelSystem::targetable(
                    dynamic_cast<Faction*>(faction->link.resolve()), found->second.subject)) return nullptr;
            return &found->second;
        };
        Target* target = resolveTarget(policy->target);
        if (explicitAttack && target == nullptr) {
            auto failed = orders->values.fail(record->id, "building attack target is invalid or destroyed");
            if (!failed) return failureFrom<std::size_t>(failed.status());
            policy->target = {};
            continue;
        }
        if (target == nullptr && acquisitionRange > 0.0f) {
            const std::string ownFaction = factionKey(faction->link);
            auto queried = sensing.circle(placement->worldX, placement->worldY, acquisitionRange,
                                          "combat-target", "", "", ownFaction, "",
                                          static_cast<int>(targets.size()));
            if (!queried) return failureFrom<std::size_t>(queried.status());
            float bestPriority = -1.0f;
            for (int index = 0; index < queried.value(); ++index) {
                auto candidate = sensing.resultAt(index);
                if (!candidate) continue;
                auto found = targets.find(candidate->get().id);
                if (found == targets.end()) continue;
                Target* accepted = resolveTarget(found->second.handle);
                if (accepted != nullptr) {
                    auto preferred = policy->targetPriorities.find(accepted->definition);
                    const float policyPriority = preferred == policy->targetPriorities.end() ? 1.0f : preferred->second;
                    const float priority = policyPriority * weaponTargetPreference(definition, accepted->tags);
                    if (target == nullptr || priority > bestPriority) {
                        target = accepted;
                        bestPriority = priority;
                    }
                }
            }
            if (target != nullptr) policy->target = target->handle;
        }
        if (target == nullptr) { policy->target = {}; continue; }
        const float range = std::max(0.0f, definition.range);
        if (distanceSquared(placement->worldX, placement->worldY, target->position.x, target->position.y) >
            range * range) continue;
        if (fireLine && definition.blockedByObstacles && definition.projectile.gravity <= 0.0f) {
            auto clear = fireLine({placement->worldX, placement->worldY}, target->position, identity->self,
                                  target->handle, definition);
            if (!clear) return failureFrom<std::size_t>(clear.status());
            if (!clear.value()) {
                publishBlocked(identity->subject, target->subject, target->position);
                continue;
            }
        }
        const float yaw = std::atan2(target->position.y - placement->worldY,
                                     target->position.x - placement->worldX) * 180.0f /
                          static_cast<float>(std::numbers::pi);
        weaponEntity->aim()->desiredYaw = yaw;
        weaponEntity->aim()->turnSpeed = policy->turnRateDegrees;
        weapon::WeaponSystem::updateAim(*weaponEntity, static_cast<float>(step.delta.seconds()));
        if (std::fabs(std::remainder(weaponEntity->aim()->desiredYaw - weaponEntity->aim()->yaw, 360.0f)) >
            policy->aimToleranceDegrees) continue;
        weapon::AttackRequest attack;
        attack.targetX = target->position.x;
        attack.targetY = target->position.y;
        attack.hasTarget = true;
        attack.targetHandle = target->handle;
        attack.muzzleX = placement->worldX;
        attack.muzzleY = placement->worldY;
        attack.shooterId = static_cast<int>(identity->self.id);
        if (!weapon::WeaponSystem::tryFire(*weaponEntity, attack)) continue;
        const ShotPlacement shot = placeShot(identity->subject, policy->shotSequence++, definition,
                                             target->position);
        if (definition.projectile.speed > 0.0f && definition.projectile.gravity > 0.0f) {
            auto* firingBuilding = dynamic_cast<Building*>(ecs::try_get(identity->self));
            if (firingBuilding != nullptr) {
                firingBuilding->indirectFire()->lastFireTick = step.tick;
                firingBuilding->indirectFire()->lastFirePosition = {placement->worldX, placement->worldY};
            }
        }
        if (definition.projectile.speed > 0.0f && projectiles != nullptr) {
            const auto heights = launchHeights({placement->worldX, placement->worldY}, shot.point,
                                               identity->self, target->handle);
            const float garrisonFactor = 1.0f + garrison->damageBonusPerOccupant *
                                                    static_cast<float>(garrison->occupants.size());
            auto launched = projectiles->launch(identity->subject, faction->link.handle(),
                                                 {placement->worldX, placement->worldY},
                                                 shot.missed ? ecs::EntityHandle{} : target->handle,
                                                 shot.point, definition, garrisonFactor, {},
                                                 heights.source, heights.target);
            if (!launched) return failureFrom<std::size_t>(launched.status());
        }
        publishShot(*weaponEntity, identity->subject, target->subject, shot.point,
                    definition.projectile.speed > 0.0f, shot.missed);
        ++fired;
        if (definition.projectile.speed <= 0.0f && !shot.missed && target->durability != nullptr) {
            combat::DamageRequest request;
            request.source = identity->subject;
            request.target = target->subject;
            request.damageType = definition.damageType.empty() ? "damage.physical" : definition.damageType;
            const float garrisonFactor = 1.0f + garrison->damageBonusPerOccupant *
                                                    static_cast<float>(garrison->occupants.size());
            const float rangeFactor = weaponRangeDamageFactor(definition,
                std::hypot(target->position.x - placement->worldX,
                           target->position.y - placement->worldY));
            request.healthDamage = static_cast<double>(definition.damage * garrisonFactor * rangeFactor) *
                                   static_cast<double>(std::max(1, definition.projectile.pelletCount));
            request.healthDamage *= target->effects->multiplier("incomingDamageMultiplier");
            if (target->shield != nullptr && *target->shield > 0.0f && request.healthDamage > 0.0) {
                const float absorbed = std::min(*target->shield, static_cast<float>(request.healthDamage));
                *target->shield -= absorbed;
                request.healthDamage -= absorbed;
                if (target->shieldCooldown != nullptr) *target->shieldCooldown = target->shieldDelay;
            }
            auto outcome = damage.apply(*target->durability, request);
            if (!outcome) return failureFrom<std::size_t>(outcome.status());
            if (damageEvents) damageEvents(request, outcome.value(), step.tick, DamageChannel::Weapon);
            if (outcome.value().reaction == combat::HitReaction::Death) {
                *target->alive = false;
                policy->target = {};
                if (explicitAttack) {
                    auto completed = orders->values.complete(record->id);
                    if (!completed) return failureFrom<std::size_t>(completed.status());
                }
            }
        }
    }
    state.blockedSubjects = std::move(blockedThisStep);
    return Result<std::size_t>::success(fired,
        Status::success(fired == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> OrderActionSystem::step(const SimulationStep& step, IRTSActionExecutor& executor) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS action step delta must be non-negative",
                                    "step.delta");
    std::size_t processed = 0;
    auto        view      = ecs::View<Unit, Unit::Identity, Unit::Orders, Unit::Action>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, orders, action] = *it;
        (void)action;
        Unit* unit = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
        if (unit == nullptr || &*unit->identity() != identity) continue;
        auto current = readCurrent(orders->values);
        if (!current) return failureFrom<std::size_t>(current.status());
        auto record = std::move(current).takeValue();
        if (!record) continue;
        if (record->kind == OrderKind::Gather || record->kind == OrderKind::ReturnCargo ||
            record->kind == OrderKind::Build || record->kind == OrderKind::Repair ||
            record->kind == OrderKind::Capture || record->kind == OrderKind::Attack ||
            record->kind == OrderKind::Move || record->kind == OrderKind::AttackMove ||
            record->kind == OrderKind::Patrol || record->kind == OrderKind::HoldPosition ||
            record->kind == OrderKind::Stop ||
            record->kind == OrderKind::Garrison || record->kind == OrderKind::BoardTransport ||
            record->kind == OrderKind::AttackGround || record->kind == OrderKind::SuppressArea ||
            record->kind == OrderKind::Resupply || record->kind == OrderKind::SupplyRelay ||
            record->kind == OrderKind::Escort)
            continue;

        auto executed = executor.execute(*unit, *record, step);
        if (!executed) return failureFrom<std::size_t>(executed.status());
        const auto outcome = std::move(executed).takeValue();
        if (outcome.disposition == ActionDisposition::Completed) {
            auto completed = orders->values.complete(record->id);
            if (!completed) return failureFrom<std::size_t>(completed.status());
        }
        ++processed;
    }
    return Result<std::size_t>::success(processed, Status::success(StatusCode::Applied));
}

Result<ReinforcementRequestReceipt> ReinforcementProductionPolicySystem::request(
    Building& building, std::string preferredProduct, const ReinforcementEnqueue& enqueue) {
    if (preferredProduct.empty() || !enqueue)
        return failure<ReinforcementRequestReceipt>(DiagnosticCode::InvalidArgument,
            "RTS reinforcement request requires a preferred product and enqueue boundary", "reinforcement");
    std::set<std::string> visited;
    std::string candidate = preferredProduct;
    Status lastFailure = Status::success(StatusCode::NotFound);
    while (visited.insert(candidate).second) {
        auto queued = enqueue(building, candidate);
        if (queued) return Result<ReinforcementRequestReceipt>::success(
            {std::move(preferredProduct), candidate, std::move(queued).takeValue()},
            Status::success(StatusCode::Applied));
        lastFailure = queued.status();
        queued.ignore("reinforcement fallback continues after rejected candidate");
        const auto fallback = building.rally()->reinforcementFallbacks.find(candidate);
        if (fallback == building.rally()->reinforcementFallbacks.end() || fallback->second.empty())
            return Result<ReinforcementRequestReceipt>::failure(lastFailure);
        candidate = fallback->second;
    }
    return failure<ReinforcementRequestReceipt>(DiagnosticCode::InvalidArgument,
        "RTS reinforcement fallback chain contains a cycle", "building.rally.reinforcementFallbacks");
}

Result<std::size_t> ReinforcementProductionPolicySystem::step(
    const SimulationStep& step, const ReinforcementCancel& cancel) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument,
            "RTS reinforcement policy delta must be non-negative", "step.delta");
    struct Demand {
        Building* building = nullptr;
        production::ProductionTask* task = nullptr;
        std::string product;
        int priority = 0;
    };
    struct Group {
        std::vector<Demand> demands;
        std::map<std::string, std::size_t> livingByType;
        std::size_t living = 0;
        std::size_t limit = 0;
        std::map<std::string, std::size_t> typeLimits;
    };
    std::map<std::string, Group> groups;
    auto units = ecs::View<Unit, Unit::Definition, Unit::Faction, Unit::Tactics,
                           Unit::Durability, Unit::Containment>();
    for (auto it = units.begin(); it != units.end(); ++it) {
        auto [definition, faction, tactics, durability, containment] = *it;
        if (!durability->alive || containment->container.isBound() || tactics->combatGroup == 0) continue;
        auto& group = groups[factionKey(faction->link) + "\n" + std::to_string(tactics->combatGroup)];
        ++group.living;
        ++group.livingByType[definition->id.format()];
    }
    auto buildings = ecs::View<Building, Building::Identity, Building::Faction, Building::Production,
                               Building::Rally, Building::Integrity>();
    for (auto it = buildings.begin(); it != buildings.end(); ++it) {
        auto [identity, faction, production, rally, integrity] = *it;
        if (!integrity->alive || !rally->enabled || rally->combatGroup == 0) continue;
        auto* building = dynamic_cast<Building*>(ecs::try_get(identity->self));
        if (building == nullptr || &*building->identity() != identity) continue;
        production::ProductionTask* active = nullptr;
        for (int index = 0; index < production->values.taskCount(); ++index) {
            auto task = production->values.taskAt(index);
            if (!task || task->get().kind != "unit") continue;
            const auto state = task->get().state;
            if (state == production::TaskState::Queued || state == production::TaskState::Running ||
                task->get().id == rally->reinforcementPolicyPausedTask) {
                active = &task->get();
                break;
            }
        }
        if (active == nullptr) {
            rally->reinforcementCapped = false;
            rally->reinforcementPolicyPausedTask.clear();
            continue;
        }
        auto& group = groups[factionKey(faction->link) + "\n" + std::to_string(rally->combatGroup)];
        if (group.limit == 0 || (rally->reinforcementLimit > 0 && rally->reinforcementLimit < group.limit))
            group.limit = rally->reinforcementLimit;
        for (const auto& [product, limit] : rally->reinforcementTypeLimits) {
            auto found = group.typeLimits.find(product);
            if (found == group.typeLimits.end() || limit < found->second) group.typeLimits[product] = limit;
        }
        const auto priority = rally->reinforcementTypePriorities.find(active->product);
        group.demands.push_back({building, active, active->product,
            priority == rally->reinforcementTypePriorities.end() ? 0 : priority->second});
    }

    std::size_t processed = 0;
    for (auto& [key, group] : groups) {
        (void)key;
        std::sort(group.demands.begin(), group.demands.end(), [](const Demand& left, const Demand& right) {
            if (left.priority != right.priority) return left.priority > right.priority;
            return left.building->identity()->subject.format() < right.building->identity()->subject.format();
        });
        std::size_t reserved = 0;
        std::map<std::string, std::size_t> reservedByType;
        for (Demand& demand : group.demands) {
            auto rally = demand.building->rally();
            const auto typeLimit = group.typeLimits.find(demand.product);
            const bool totalCapped = group.limit > 0 && group.living + reserved >= group.limit;
            const bool typeCapped = typeLimit != group.typeLimits.end() &&
                group.livingByType[demand.product] + reservedByType[demand.product] >= typeLimit->second;
            const bool capped = totalCapped || typeCapped;
            if (capped && demand.task->state != production::TaskState::Paused) {
                auto paused = demand.building->production()->values.pause(demand.task->id);
                if (!paused) return failureFrom<std::size_t>(paused.status());
                rally->reinforcementPolicyPausedTask = demand.task->id;
                ++processed;
            } else if (!capped && demand.task->state == production::TaskState::Paused &&
                       rally->reinforcementPolicyPausedTask == demand.task->id) {
                auto resumed = demand.building->production()->values.resume(demand.task->id);
                if (!resumed) return failureFrom<std::size_t>(resumed.status());
                rally->reinforcementPolicyPausedTask.clear();
                ++processed;
            }
            rally->reinforcementCapped = capped;
            if (capped) {
                rally->reinforcementCappedSeconds += static_cast<float>(step.delta.seconds());
                if (rally->reinforcementAutoCancelDelay > 0.0f &&
                    rally->reinforcementCappedSeconds + 1e-5f >= rally->reinforcementAutoCancelDelay && cancel) {
                    const std::string taskId = demand.task->id;
                    auto cancelled = cancel(*demand.building, taskId);
                    if (!cancelled) return failureFrom<std::size_t>(cancelled.status());
                    rally->reinforcementPolicyPausedTask.clear();
                    rally->reinforcementCappedSeconds = 0.0f;
                    rally->reinforcementCapped = false;
                    ++processed;
                    continue;
                }
            } else {
                rally->reinforcementCappedSeconds = 0.0f;
            }
            if (!capped) {
                ++reserved;
                ++reservedByType[demand.product];
            }
        }
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> BuildingProductionSystem::step(const SimulationStep& step, const ProductionSpawn& spawn,
                                                    const ProductionSpawnPosition& position,
                                                    const LifecycleEventSink& events) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS production step delta must be non-negative",
                                    "step.delta");
    std::size_t processed = 0;
    struct Settlement { ecs::EntityHandle building; production::ProductionTask task; };
    std::vector<Settlement> settlements;
    auto        view      = ecs::View<Building, Building::Identity, Building::Production>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, production] = *it;
        Building* building = identity == nullptr ? nullptr : dynamic_cast<Building*>(ecs::try_get(identity->self));
        if (building == nullptr || &*building->identity() != identity) continue;
        auto advanced = production->values.advance(step);
        if (!advanced) return failureFrom<std::size_t>(advanced.status());
        advanced.value();
        ++processed;
        if (!spawn) continue;
        const auto completed = production->values.completed("unit");
        if (building->rally()->productionSpawnBlocked &&
            std::none_of(completed.begin(), completed.end(), [&](const auto& task) {
                return task.id == building->rally()->blockedProductionTask;
            })) {
            building->rally()->productionSpawnBlocked = false;
            building->rally()->blockedProductionTask.clear();
            if (events)
                events({LifecycleEventKind::ProductionSpawnCleared, identity->subject, {}, {}, 0.0}, step.tick);
        }
        for (const auto& task : completed) {
            auto& settled = building->rally()->settledProductionTasks;
            if (std::find(settled.begin(), settled.end(), task.id) != settled.end()) continue;
            settlements.push_back({identity->self, task});
        }
    }
    for (const auto& settlement : settlements) {
            auto* building = dynamic_cast<Building*>(ecs::try_get(settlement.building));
            if (building == nullptr)
                return failure<std::size_t>(DiagnosticCode::StaleHandle,
                                            "RTS producer disappeared during production settlement", "production");
            auto& settled = building->rally()->settledProductionTasks;
            if (std::find(settled.begin(), settled.end(), settlement.task.id) != settled.end()) continue;
            std::optional<WorldPosition> spawnPosition;
            if (position) {
                auto available = position(*building, settlement.task);
                if (!available) return failureFrom<std::size_t>(available.status());
                spawnPosition = std::move(available).takeValue();
                if (!spawnPosition) {
                    const bool newlyBlocked = !building->rally()->productionSpawnBlocked;
                    building->rally()->productionSpawnBlocked = true;
                    building->rally()->blockedProductionTask = settlement.task.id;
                    if (newlyBlocked && events)
                        events({LifecycleEventKind::ProductionSpawnBlocked,
                                building->identity()->subject, {}, settlement.task.product, 0.0}, step.tick);
                    continue;
                }
            }
            auto created = spawn(*building, settlement.task);
            if (!created) return failureFrom<std::size_t>(created.status());
            Unit* unit = std::move(created).takeValue();
            if (unit == nullptr)
                return failure<std::size_t>(DiagnosticCode::Failed,
                                            "RTS production factory returned a null unit", "production.spawn");
            unit->motion()->x = spawnPosition ? spawnPosition->x : building->placement()->worldX;
            unit->motion()->y = spawnPosition ? spawnPosition->y : building->placement()->worldY;
            const bool wasBlocked = building->rally()->productionSpawnBlocked;
            building->rally()->productionSpawnBlocked = false;
            building->rally()->blockedProductionTask.clear();
            if (wasBlocked && events)
                events({LifecycleEventKind::ProductionSpawnCleared,
                        building->identity()->subject, unit->identity()->subject,
                        settlement.task.product, 0.0}, step.tick);
            unit->tactics()->combatGroup = building->rally()->combatGroup;
            building->rally()->reinforcements.push_back(unit->identity()->self);

            bool boarded = false;
            auto* transport = dynamic_cast<Unit*>(ecs::try_get(building->rally()->transport));
            if (transport != nullptr && transport->durability()->alive && transport->containment()->capacity > 0 &&
                transport->containment()->occupants.size() < transport->containment()->capacity &&
                FactionRelationSystem::allied(transport->faction()->link, building->faction()->link)) {
                auto link = ContainerLink::bind(transport->identity()->self);
                if (!link) return failureFrom<std::size_t>(link.status());
                unit->containment()->container = std::move(link).takeValue();
                transport->containment()->occupants.push_back(unit->identity()->self);
                boarded = true;
            }
            if (!boarded && building->rally()->enabled) {
                auto queued = unit->orders()->values.replace(building->rally()->command);
                if (!queued) return failureFrom<std::size_t>(queued.status());
                std::move(queued).takeValue();
            }
            settled.push_back(settlement.task.id);
            if (events)
                events({LifecycleEventKind::UnitProduced, building->identity()->subject,
                        unit->identity()->subject, settlement.task.product, 1.0}, step.tick);
            ++processed;
    }
    return Result<std::size_t>::success(processed, Status::success(StatusCode::Applied));
}

Result<std::size_t> ReinforcementSystem::step() {
    std::size_t processed = 0;
    auto view = ecs::View<Building, Building::Identity, Building::Faction, Building::Rally>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [identity, faction, rally] = *it;
        (void)identity;
        if (!rally->enabled || rally->transport.table == nullptr) continue;
        auto* transport = dynamic_cast<Unit*>(ecs::try_get(rally->transport));
        if (transport == nullptr || !transport->durability()->alive ||
            !FactionRelationSystem::allied(transport->faction()->link, faction->link)) {
            rally->transport = {};
            rally->transportActive = false;
            continue;
        }
        auto& occupants = transport->containment()->occupants;
        occupants.erase(std::remove_if(occupants.begin(), occupants.end(), [](const auto& handle) {
                            return dynamic_cast<Unit*>(ecs::try_get(handle)) == nullptr;
                        }), occupants.end());
        if (!rally->transportActive) {
            if (occupants.size() < std::max<std::size_t>(1, rally->minimumTransportLoad)) continue;
            auto dispatched = transport->orders()->values.replace(rally->command);
            if (!dispatched) return failureFrom<std::size_t>(dispatched.status());
            std::move(dispatched).takeValue();
            transport->tactics()->combatGroup = rally->combatGroup;
            rally->transportActive = true;
            ++processed;
            continue;
        }
        if (!transport->motion()->arrived) continue;
        const auto passengers = occupants;
        occupants.clear();
        for (const auto& handle : passengers) {
            auto* passenger = dynamic_cast<Unit*>(ecs::try_get(handle));
            if (passenger == nullptr) continue;
            passenger->containment()->container = {};
            passenger->motion()->x = transport->motion()->x;
            passenger->motion()->y = transport->motion()->y;
            passenger->tactics()->combatGroup = rally->combatGroup;
            auto ordered = passenger->orders()->values.replace(rally->command);
            if (!ordered) return failureFrom<std::size_t>(ordered.status());
            std::move(ordered).takeValue();
            ++processed;
        }
        rally->transportActive = false;
    }
    return Result<std::size_t>::success(processed,
        Status::success(processed == 0 ? StatusCode::NoOp : StatusCode::Applied));
}

Result<std::size_t> EffectSystem::step(const SimulationStep& step, const LifecycleEventSink& events) {
    if (step.delta.nanoseconds() < 0)
        return failure<std::size_t>(DiagnosticCode::InvalidArgument, "RTS effects step delta must be non-negative",
                                    "step.delta");
    std::size_t processed = 0;
    {
        auto view = ecs::View<Unit, Unit::Identity, Unit::Effects, Unit::Durability>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [identity, effects, durability] = *it;
            Unit* unit = identity == nullptr ? nullptr : dynamic_cast<Unit*>(ecs::try_get(identity->self));
            if (unit == nullptr || &*unit->identity() != identity || &*unit->effects() != effects) continue;
            const double healing = effects->values.additive("healingPerSecond") * step.delta.seconds();
            if (durability->alive && healing > 0.0)
                durability->state.health = std::min(durability->state.maxHealth,
                                                     durability->state.health + healing);
            const auto before = effects->values.snapshot();
            auto advanced = advanceEffects(effects->values, step);
            if (!advanced) return failureFrom<std::size_t>(advanced.status());
            if (events && advanced.value() > 0) {
                const auto after = effects->values.snapshot();
                for (int index = 0; index < before.effects.effectCount(); ++index) {
                    const auto* instance = before.effects.effectAt(index);
                    if (instance != nullptr && after.effects.find(instance->id) == nullptr)
                        events({LifecycleEventKind::StatusExpired, identity->subject, {},
                                instance->type, 0.0}, step.tick);
                }
            }
            processed += std::move(advanced).takeValue();
        }
    }
    {
        auto view = ecs::View<Building, Building::Identity, Building::Effects>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [identity, effects] = *it;
            Building* building = identity == nullptr ? nullptr : dynamic_cast<Building*>(ecs::try_get(identity->self));
            if (building == nullptr || &*building->identity() != identity || &*building->effects() != effects) continue;
            const auto before = effects->values.snapshot();
            auto advanced = advanceEffects(effects->values, step);
            if (!advanced) return failureFrom<std::size_t>(advanced.status());
            if (events && advanced.value() > 0) {
                const auto after = effects->values.snapshot();
                for (int index = 0; index < before.effects.effectCount(); ++index) {
                    const auto* instance = before.effects.effectAt(index);
                    if (instance != nullptr && after.effects.find(instance->id) == nullptr)
                        events({LifecycleEventKind::StatusExpired, identity->subject, {},
                                instance->type, 0.0}, step.tick);
                }
            }
            processed += std::move(advanced).takeValue();
        }
    }
    return Result<std::size_t>::success(processed, Status::success(StatusCode::Applied));
}

}  // namespace eve::rts
