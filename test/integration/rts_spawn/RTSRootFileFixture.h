#pragma once
#include "RTSSpawnRestoreFixture.h"
#include "rts/RTSAttributeCapture.h"
#include "rts/RTSMatch.h"
namespace test_rts_root_file {
using namespace eve;
using namespace eve::rts;
inline PersistentId epoch() { return *PersistentId::parse("01900000-0000-7000-8000-000000000e71"); }
inline SubjectRef   subject(std::string_view role) { return SubjectRef::fromPersistentId(epoch().child(role)); }
struct Fixture {
    std::shared_ptr<ecs::Table> world = std::make_shared<ecs::Table>();
    RTS                         domain{world};
    Fixture() {
        ecs::ScopedTable scope(*world);
        auto*            own   = domain.newFaction(subject("own")).expect("own faction");
        auto*            enemy = domain.newFaction(subject("enemy")).expect("enemy faction");
        for (const auto* role : {"mover", "ally", "passenger", "enemy-unit"}) {
            auto* u            = domain.newUnit(subject(role)).expect("unit");
            auto* f            = std::string_view(role) == "enemy-unit" ? enemy : own;
            u->faction()->link = FactionLink::bind(ecs::handle_of(f)).expect("unit owner");
            f->members()->units.push_back(ecs::handle_of(u));
            u->tags()->values.add(role).expect("unit tag");
        }
        auto* u            = domain.findUnit(subject("mover"));
        auto* ally         = domain.findUnit(subject("ally"));
        auto* passenger    = domain.findUnit(subject("passenger"));
        auto* opponent     = domain.findUnit(subject("enemy-unit"));
        auto* b            = domain.newBuilding(subject("base")).expect("building");
        b->faction()->link = FactionLink::bind(ecs::handle_of(own)).expect("building owner");
        own->members()->buildings.push_back(ecs::handle_of(b));
        b->placement()->link                = PlacementLink::bind("saved-placement-provider").expect("placement key");
        b->placement()->placed              = true;
        b->placement()->worldX              = 6;
        b->placement()->rotation            = 45;
        b->construction()->progress         = .625f;
        b->construction()->buildTimeSeconds = 8;
        b->construction()->builders         = {ecs::handle_of(u)};
        b->garrison()->capacity             = 3;
        b->garrison()->occupants            = {ecs::handle_of(passenger)};
        passenger->containment()->container = ContainerLink::bind(ecs::handle_of(b)).expect("container");
        b->supply()->productionResource     = "metal";
        b->supply()->productionCostPerRound = INT64_C(9007199254740993);
        b->rally()->reinforcementTypeLimits["infantry"]     = 7;
        b->rally()->reinforcementTypePriorities["infantry"] = -2;
        b->rally()->reinforcementFallbacks["infantry"]      = "scout";
        b->rally()->reinforcements                          = {ecs::handle_of(ally)};
        b->rally()->settledProductionTasks                  = {"retired-task"};
        b->rally()->reinforcementPolicyPausedTask           = "paused-task";
        b->integrity()->repairCostRemainder                 = .25f;
        b->integrity()->repairResource                      = "ore";
        b->infrastructure()->incomeResource                 = "ore";
        b->infrastructure()->incomeProgress                 = .375f;
        b->production()
            ->values.enqueue("base", "train", "infantry", Duration::fromSeconds(2).expect("work"), 5)
            .expect("work task");
        b->production()
            ->values.advance({SimulationTick(1), Duration::fromSeconds(.375).expect("delta")})
            .expect("partial work");
        auto* node = domain.newResourceNode(subject("ore"), "ore", 100, {3, 5}, 2).expect("resource node");
        node->stock()->remaining                  = 73.5f;
        node->harvest()->workers                  = {ecs::handle_of(u)};
        u->worker()->resourceType                 = "ore";
        u->worker()->resourceNode                 = ResourceNodeLink::bind(ecs::handle_of(node)).expect("node");
        u->worker()->dropoff                      = BuildingLink::bind(ecs::handle_of(b)).expect("dropoff");
        u->worker()->cargo                        = 2.5f;
        u->motion()->speed                        = 2.5f;
        u->motion()->arrived                      = false;
        u->navigation()->waypoints                = {{4, 3}, {8, 6}};
        u->navigation()->waypointIndex            = 1;
        u->command()->source                      = ecs::handle_of(b);
        u->command()->uplink                      = ecs::handle_of(ally);
        u->command()->range                       = 10;
        u->supply()->assignedTarget               = ecs::handle_of(ally);
        u->supply()->convoyLeader                 = ecs::handle_of(ally);
        u->supply()->rendezvousPoint              = {4, 9};
        u->supply()->reservedStock                = .5f;
        u->supply()->routeThreat                  = 2;
        u->artillery()->fireSupportRequester      = ecs::handle_of(ally);
        u->artillery()->observedFireSpotter       = ecs::handle_of(ally);
        u->artillery()->shotSequence              = 11;
        u->artillery()->counterBatteryWindowTicks = INT64_C(9007199254740995);
        u->artillery()->departedPosition          = {7, 4};
        u->artillery()->hasDepartedPosition       = true;
        u->tactics()->escortTarget                = ecs::handle_of(ally);
        u->tactics()->escortInterceptTarget       = subject("retired-contact");
        u->tactics()->escortHandoffCount          = 9;
        u->tactics()->retreatCoverElapsed         = .125f;
        u->combat()->target                       = ecs::handle_of(opponent);
        u->combat()->targetPriorities["tank"]     = 2.25f;
        u->combat()->shotSequence                 = UINT64_C(9007199254740999);
        AbilitySpec spell;
        spell.id                      = "saved-cast";
        spell.castTime                = 1;
        spell.channelTickInterval     = .25f;
        spell.cooldown                = 2;
        spell.range                   = 100;
        spell.appliesEffect           = true;
        spell.effect.id               = "cast-effect";
        spell.effect.duration         = .75;
        spell.effect.speedMultiplier  = .75;
        spell.effect.healingPerSecond = 2.5;
        spell.effect.policy.maxStacks = 7;
        spell.effect.policy.magnitude = effects::MagnitudePolicy::Max;
        combat::DamageRuntime castDamage;
        AbilitySystem::cast(*u, spell, ecs::handle_of(opponent), {}, castDamage).expect("real channel start");
        u->abilities()->cooldowns.push_back({"other-cooldown", .25f});
        own->strategy()->workerDefinition                  = *LogicalId::parse("unit:worker");
        own->strategy()->thinkAccumulator                  = .375f;
        own->workforce()->reserveWorkers                   = 2;
        own->productionPolicy()->resourceReserves["metal"] = {INT64_C(9007199254740993), 3};
        own->intel()->contacts.push_back({subject("retired-contact"), "unit", {9, 7}, 1.75, false, true});
        own->technology()->unlocked                          = {"saved-tech"};
        own->technology()->consumedTasks                     = {"retired-research"};
        own->technology()->unlockedDefinitions["saved-tech"] = {{"speedMultiplier", 1.25},
                                                                {"opaque", Value::Array{true, 3}}};
        auto* p                                              = domain.newPlayer(subject("player")).expect("player");
        p->selection()->units                                = {ecs::handle_of(u), ecs::handle_of(ally)};
        p->selection()->buildings                            = {ecs::handle_of(b)};
        p->selection()->revision                             = UINT64_C(9007199254741001);
        p->selection()->tick                                 = SimulationTick(1);
        auto* m                                              = domain.newMatch(subject("match")).expect("match");
        MatchSystem::addParticipant(*m, *own, 1).expect("own participant");
        MatchSystem::addParticipant(*m, *enemy, 2).expect("enemy participant");
        MatchSystem::start(*m).expect("start match");
        domain.submitMovementGroup({{subject("mover"), subject("ally")}, {18, 9}, {}, 3.f, OrderKind::Move})
            .expect("actual movement group");
        RTSEffectDefinition effect;
        effect.id        = "periodic-suppression";
        effect.kind      = RTSEffectKind::Suppression;
        effect.duration  = 2;
        effect.period    = .25;
        effect.magnitude = 2;
        u->effects()->values.apply(effect).expect("effect");
        const SimulationStep partial{SimulationTick(1), Duration::fromSeconds(.375).expect("delta")};
        AbilitySystem::step(partial, castDamage).expect("partial channel");
        auto effectView = ecs::View<Unit, Unit::Effects>();
        for (auto it = effectView.begin(); it != effectView.end(); ++it) {
            auto [effects] = *it;
            effects->values.advance(partial).expect("partial effect");
        }
    }
};
inline std::string wire(RTS& domain) {
    return domain.snapshotState()
        .expect("root capture")
        .encodeState()
        .expect("root value")
        .toJson()
        .expect("root file");
}
inline void advance(RTS& domain, ecs::Table& world, std::uint64_t tick) {
    ecs::ScopedTable     scope(world);
    const SimulationStep step{SimulationTick(tick), Duration::fromSeconds(.25).expect("delta")};
    MotionSystem::step(step).expect("actual motion");
    combat::DamageRuntime damage;
    AbilitySystem::step(step, damage).expect("actual channel continuation");
    domain.findBuilding(subject("base"))->production()->values.advance(step).expect("actual production");
    auto effectView = ecs::View<Unit, Unit::Effects>();
    for (auto it = effectView.begin(); it != effectView.end(); ++it) {
        auto [effects] = *it;
        effects->values.advance(step).expect("actual effects");
    }
}
}  // namespace test_rts_root_file
