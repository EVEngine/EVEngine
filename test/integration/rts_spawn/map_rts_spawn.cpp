// eve-test-policy:
// {"schema":"evengine.test-policy","version":1,"module":"rts_spawn","task":"rts_spawn","default":"module-composition","modules":["rts_spawn","rts","spawn","map_spawn","action"]}
#include <cmath>
#include "RTSSpawnFixture.h"
#include "action/Action.h"
#include "map/Map.h"
#include "map/Pathfinder.h"
#include "map/spawn/MapSpawnSpace.h"
#include "rts/spawn/RTSSpawnOccupancy.h"
#include "zeroerr/unittest.h"
namespace test_map_rts_spawn {
TEST_CASE("RTS reinforcements use actual map clearance and preserve neighbors across blocked placement and retry") {
    test_rts_spawn::Fixture fixture;
    // Match canonical RTS script-world tuning: this map uses one world unit per cell,
    // while Crowd's legacy flocking defaults are expressed at pixel scale.
    fixture.crowd.setArriveRadius(1.f);
    fixture.crowd.setSeparationWeight(0.f);
    fixture.commit();
    auto command    = std::make_shared<eve::rts::CommandSpec>();
    command->kind   = eve::rts::OrderKind::AttackMove;
    command->target = {12, 12};
    const auto faction =
        eve::SubjectRef::fromPersistentId(*eve::PersistentId::parse("01900000-0000-7000-8000-000000000889"));
    auto factory =
        std::make_shared<eve::rts_spawn::RTSSpawnFactory>(*fixture.domain, faction, fixture.registry, command);
    auto occupancy  = std::make_shared<eve::rts_spawn::RTSSpawnOccupancy>(*fixture.domain);
    auto definition = eve::Value::fromJson(R"({"schema":"eve.spawn-template","version":1,"id":"reinforcement",
        "content":{"kind":"fixed","members":[{"id":"fighter","unit":{"type":"unit","id":"fighter"},"count":1}]},
        "schedule":{"kind":"once"},"limits":{"alive":1,"total":{"kind":"finite","count":1},"budget_group":"reinforcements"},
        "placement":{"kind":"random_in_region","constraints":[{"kind":"walkable"},{"kind":"clearance","radius":0.35}],"attempts_per_unit":3},
        "batch_policy":"atomic","failure":{"placement_blocked":{"kind":"retry","delay_seconds":1}}})")
                          .expect("actual template source");
    struct Templates final : eve::spawn::ITemplateResolver {
        eve::spawn::CompiledTemplate definition;
        explicit Templates(eve::spawn::CompiledTemplate value) : definition(std::move(value)) {}
        eve::Result<eve::spawn::CompiledTemplate> resolve(const eve::DefinitionRef&) const override {
            return eve::Result<eve::spawn::CompiledTemplate>::success(definition);
        }
    } templates(eve::spawn::compileTemplate(definition, *factory).expect("canonical RTS recipe validation"));
    auto source = eve::Value::fromJson(R"({"schema":"eve.map-spawns","version":1,"map":"reinforcement-map",
        "budgets":[{"id":"reinforcements","alive_limit":1}],"instances":[{"id":"reinforcement",
        "template":{"type":"spawn.template","id":"reinforcement"},"region":{"kind":"points","space":"world2d","points":[[1,2]]},
        "activation":{"kind":"map_loaded"},"inactive_policy":"pause","unload_policy":"persist"}]})")
                      .expect("actual map source");
    const auto    content = eve::spawn::compileMap(source, templates, *factory).expect("actual compiled map");
    eve::map::Map map;
    auto*         layer = map.newLayer(20, 20, 1, 1);
    layer->fill(1);
    eve::map::Pathfinder navigation(layer);
    const auto           owner = fixture.request.source.owner.child("map-reinforcements");
    auto space = std::make_shared<eve::map_spawn::MapSpawnSpace>(content, owner, eve::map_spawn::SpawnGridTransform{},
                                                                 occupancy);
    space->setNavigation(navigation.captureNavigationGrid().expect("actual map navigation")).expect("install grid");
    auto spatial   = std::make_shared<eve::map_spawn::SpatialSpawnFactory>(factory, space);
    auto providers = std::make_shared<eve::spawn::SpawnProviders>(eve::spawn::SpawnProviders{space, spatial});
    eve::spawn::SpawnWorld world(owner, 42);
    const auto             sources = world.attach(content, providers).expect("actual native RTS/map composition");
    fixture.domain->setNavigationProvider(&navigation, {1.f, 0.f, 0.f});
    eve::action::ActionRuntime actions;
    eve::rts::ActionAdapter    adapter(actions);
    const auto                 stepDomain = [&](const eve::SimulationStep& step) {
        ecs::ScopedTable scope(*fixture.table);
        fixture.domain->step(step, adapter).expect("canonical RTS action navigation and crowd frame");
    };
    stepDomain({eve::SimulationTick(1), eve::Duration(100000000)});
    const auto before = occupancy->capture().expect("actual neighbors");
    auto       blocked =
        world.advance({eve::SimulationTick(1), eve::Duration(100000000)}).expect("ordinary placement contention");
    REQUIRE_EQ(blocked.units, std::uint32_t(0));
    REQUIRE_EQ(fixture.count(), std::size_t(2));
    REQUIRE_EQ(fixture.crowd.getAgentCount(), 2);
    REQUIRE(world.inspect(sources[0]).expect("actual blocked state").blocked ==
            eve::spawn::BlockReason::PlacementBlocked);
    for (const auto& disk : before) {
        const auto actual = fixture.factory->inspect(disk.subject).expect("unchanged original unit");
        REQUIRE_EQ(actual.position.x, disk.center.x);
        REQUIRE_EQ(actual.position.y, disk.center.y);
    }
    {
        ecs::ScopedTable              scope(*fixture.table);
        eve::rts::UnitRelocationBatch movement;
        movement.units.push_back({fixture.request.units[0].subject, {8, 8}});
        fixture.domain->relocateUnits(movement).expect("explicit gameplay relocation frees entry");
    }
    stepDomain({eve::SimulationTick(2), eve::Duration(1000000000)});
    REQUIRE_EQ(
        world.advance({eve::SimulationTick(2), eve::Duration(1000000000)}).expect("real delayed reinforcement").units,
        std::uint32_t(1));
    REQUIRE_EQ(fixture.count(), std::size_t(3));
    REQUIRE_EQ(fixture.crowd.getAgentCount(), 3);
    auto admitted = world.inspect(sources[0]).expect("actual committed member");
    REQUIRE_EQ(admitted.subjects.size(), std::size_t(1));
    REQUIRE_EQ(factory->inspect(admitted.subjects[0]).expect("actual reinforcement position").position.x, 1.0);
    {
        ecs::ScopedTable scope(*fixture.table);
        auto*            unit = fixture.domain->findUnit(admitted.subjects[0]);
        REQUIRE(unit != nullptr);
        REQUIRE(unit->faction()->link.resolve() == fixture.domain->findFaction(faction));
        const auto order = unit->orders()->values.current().expect("canonical initial attack move");
        REQUIRE(order.kind == eve::rts::OrderKind::AttackMove);
        REQUIRE_EQ(order.target.x, 12.0f);
    }
    // Drive the real domain using navigation attached to the same map used for admission.
    // Spawn does not own another clock, action executor, navigation grid or movement cache.
    const auto departure = factory->inspect(admitted.subjects[0]).expect("actual departure");
    for (std::uint64_t tick = 3; tick <= 12; ++tick) {
        const eve::SimulationStep step{eve::SimulationTick(tick), eve::Duration(100000000)};
        stepDomain(step);
        world.advance(step).expect("spawn follows domain without stepping it again");
    }
    const auto arrived = factory->inspect(admitted.subjects[0]).expect("actual moved reinforcement");
    REQUIRE(arrived.position.x > departure.position.x);
    REQUIRE(std::hypot(12.0 - arrived.position.x, 12.0 - arrived.position.y) <
            std::hypot(12.0 - departure.position.x, 12.0 - departure.position.y));
    REQUIRE_EQ(world.inspect(sources[0]).expect("movement preserves source membership").total, std::uint64_t(1));
    fixture.domain->setNavigationProvider(nullptr);
    world.detach(sources[0], eve::spawn::DetachPolicy::RemoveUnits).expect("explicit reinforcement cleanup");
    REQUIRE_EQ(fixture.count(), std::size_t(2));
    REQUIRE_EQ(fixture.crowd.getAgentCount(), 2);
    REQUIRE_EQ(occupancy->capture().expect("removed unit no longer occupies space").size(), std::size_t(2));
    eve::map_spawn::MapSpawnSpace absent(content, owner, {}, nullptr);
    absent.setNavigation(navigation.captureNavigationGrid().expect("known grid")).expect("install grid");
    REQUIRE(
        !absent.validate(content.instances()[0].revision->definition().placement, content.instances()[0].region).ok());
}
}  // namespace test_map_rts_spawn
