#pragma once
#include "crowd/Crowd.h"
#include "definitions/Definitions.h"
#include "rts/RTS.h"
#include "rts/spawn/RTSSpawnFactory.h"
namespace test_rts_spawn {
class Space final : public eve::spawn::ISpawnPlacement {
public:
    eve::Result<void> validate(const eve::spawn::PlacementSpec&, const eve::spawn::Region&) const override {
        return eve::Result<void>::success();
    }
    eve::Result<eve::spawn::PlacementOutcome> plan(const eve::spawn::BatchRequest& request,
                                                   const eve::spawn::PlacementSpec&, const eve::spawn::Region&,
                                                   std::uint64_t&) override {
        std::vector<eve::spawn::Point> points;
        for (std::size_t i = 0; i < request.units.size(); ++i) points.push_back({double(i) * 4, 2});
        return eve::Result<eve::spawn::PlacementOutcome>::success(std::move(points));
    }
};
struct Fixture {
    std::shared_ptr<ecs::Table>                           table = std::make_shared<ecs::Table>();
    eve::crowd::Crowd                                     crowd;
    std::unique_ptr<eve::rts::RTS>                        domain = std::make_unique<eve::rts::RTS>(table);
    std::shared_ptr<eve::definitions::DefinitionRegistry> registry =
        std::make_shared<eve::definitions::DefinitionRegistry>();
    std::shared_ptr<eve::rts_spawn::RTSSpawnFactory> factory;
    std::unique_ptr<eve::spawn::SpawnWorld>          sourceWorld;
    std::shared_ptr<eve::spawn::SpawnProviders>      providers;
    eve::spawn::BatchRequest                         request;
    std::vector<eve::spawn::Point>                   positions{{1, 2}, {4, 5}};
    explicit Fixture(std::shared_ptr<const eve::rts::CommandSpec> command = {}) {
        crowd.setClampToField(false);
        domain->setCrowdProvider(&crowd);
        ecs::ScopedTable scope(*table);
        eve::rts::RTSContentLoader::load(*registry, R"({
          "weapons":[{"id":"club","damageType":"normal","damage":10,"range":1.2,"cooldown":0.2,"projectileSpeed":0,"magazineSize":1000}],
          "units":[{"id":"fighter","weaponType":"club","health":40,"speed":3,"radius":0.35}]
        })")
            .expect("canonical RTS content");
        auto faction = domain
                           ->newFaction(eve::SubjectRef::fromPersistentId(
                               *eve::PersistentId::parse("01900000-0000-7000-8000-000000000889")))
                           .expect("faction");
        factory     = std::make_shared<eve::rts_spawn::RTSSpawnFactory>(*domain, faction->identity()->subject, registry,
                                                                        std::move(command));
        auto owner  = *eve::PersistentId::parse("01900000-0000-7000-8000-000000000009");
        sourceWorld = std::make_unique<eve::spawn::SpawnWorld>(owner, 42);
        providers   = std::make_shared<eve::spawn::SpawnProviders>(
            eve::spawn::SpawnProviders{std::make_shared<Space>(), factory});
        struct Templates final : eve::spawn::ITemplateResolver {
            const eve::rts_spawn::RTSSpawnFactory& units;
            explicit Templates(const eve::rts_spawn::RTSSpawnFactory& source) : units(source) {}
            eve::Result<eve::spawn::CompiledTemplate> resolve(const eve::DefinitionRef&) const override {
                auto value = eve::Value::fromJson(R"({"schema":"eve.spawn-template","version":1,"id":"wolves",
                  "content":{"kind":"weighted","selection":"per_unit","entries":[{"unit":{"type":"unit","id":"fighter"},"weight":1}]},
                  "schedule":{"kind":"maintain_population","target_alive":2,"initial_population":2,"replacement":{"after":"death","delay_seconds":[1,1]}},
                  "limits":{"alive":2,"total":{"kind":"unlimited"},"budget_group":"wolves"},
                  "placement":{"kind":"random_in_region","constraints":[{"kind":"walkable"}],"attempts_per_unit":2},
                  "batch_policy":"atomic","failure":{"placement_blocked":{"kind":"retry","delay_seconds":1}}})")
                                 .expect("template source");
                return eve::spawn::compileTemplate(value, units);
            }
        } templates(*factory);
        auto mapSource = eve::Value::fromJson(R"({"schema":"eve.map-spawns","version":1,"map":"forest",
          "budgets":[{"id":"wolves","alive_limit":2}],"instances":[{"id":"wolves",
          "template":{"type":"spawn.template","id":"wolves"},"region":{"kind":"circle","space":"world2d","center":[0,0],"radius":10},
          "activation":{"kind":"map_loaded"},"inactive_policy":"pause","unload_policy":"persist"}]})")
                             .expect("map source");
        auto map         = eve::spawn::compileMap(mapSource, templates, *factory).expect("real unit map");
        request.source   = sourceWorld->attach(map, providers).expect("source lifetime")[0];
        request.lifetime = sourceWorld->sourceLease(request.source).expect("source lease");
        request.operation =
            request.lifetime.operationIdentity(eve::spawn::OperationOrdinal(1)).expect("canonical operation");
        request.ordinal = eve::spawn::OperationOrdinal(1);
        for (int i = 0; i < 2; ++i)
            request.units.push_back(
                {request.lifetime.unitIdentity(eve::spawn::OperationOrdinal(1), static_cast<std::uint32_t>(i))
                     .expect("canonical member"),
                 eve::DefinitionRef::parse("unit:fighter").expect("unit reference")});
    }
    std::size_t count() {
        ecs::ScopedTable scope(*table);
        std::size_t      count = 0;
        auto             view  = ecs::View<eve::rts::Unit>(*table);
        for (auto it = view.begin(); it != view.end(); ++it) ++count;
        return count;
    }
    void commit() {
        auto prepared = factory->prepare(request, positions).expect("prepare");
        std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(prepared)->commit().expect("commit");
    }
};
}  // namespace test_rts_spawn
