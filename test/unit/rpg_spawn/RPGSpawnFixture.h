#pragma once
#include "definitions/Definitions.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"
#include "rpg/spawn/RPGSpawnFactory.h"
#include "rpg/spawn/RPGUnitCatalogue.h"
namespace test_rpg_spawn {
class Space final : public eve::spawn::ISpawnPlacement {
public:
    eve::Result<void> validate(const eve::spawn::PlacementSpec&, const eve::spawn::Region&) const override {
        return eve::Result<void>::success();
    }
    eve::Result<eve::spawn::PlacementOutcome> plan(const eve::spawn::BatchRequest& request,
                                                   const eve::spawn::PlacementSpec&, const eve::spawn::Region&,
                                                   std::uint64_t&) override {
        return eve::Result<eve::spawn::PlacementOutcome>::success(
            std::vector<eve::spawn::Point>(request.units.size(), {1, 2}));
    }
};
struct Fixture {
    std::shared_ptr<ecs::Table>                           table = std::make_shared<ecs::Table>();
    std::shared_ptr<eve::definitions::DefinitionRegistry> registry =
        std::make_shared<eve::definitions::DefinitionRegistry>();
    std::shared_ptr<eve::rpg_spawn::RPGUnitCatalogue> units =
        std::make_shared<eve::rpg_spawn::RPGUnitCatalogue>(registry);
    std::shared_ptr<eve::rpg_spawn::RPGSpawnFactory> factory =
        std::make_shared<eve::rpg_spawn::RPGSpawnFactory>(table, units);
    std::unique_ptr<eve::spawn::SpawnWorld>     sourceWorld;
    std::shared_ptr<eve::spawn::SpawnProviders> providers;
    eve::spawn::BatchRequest                    request;
    std::vector<eve::spawn::Point>              positions{{1, 2}, {4, 5}};
    Fixture(std::shared_ptr<const eve::rpg::SkillConditionBindings> conditions = {})
        : factory(std::make_shared<eve::rpg_spawn::RPGSpawnFactory>(table, units, std::move(conditions))) {
        eve::rpg::SkillDefinition skill;
        skill.id = "spawn.basic";
        eve::rpg::SkillRegistry::registerSkill(skill);
        auto source = asset();
        units->publish(source).expect("publish real unit");
        auto owner  = *eve::PersistentId::parse("01900000-0000-7000-8000-000000000009");
        sourceWorld = std::make_unique<eve::spawn::SpawnWorld>(owner, 42);
        providers   = std::make_shared<eve::spawn::SpawnProviders>(
            eve::spawn::SpawnProviders{std::make_shared<Space>(), factory});
        struct Templates final : eve::spawn::ITemplateResolver {
            const eve::rpg_spawn::RPGUnitCatalogue& units;
            explicit Templates(const eve::rpg_spawn::RPGUnitCatalogue& source) : units(source) {}
            eve::Result<eve::spawn::CompiledTemplate> resolve(const eve::DefinitionRef&) const override {
                auto value = eve::Value::fromJson(R"({"schema":"eve.spawn-template","version":1,"id":"wolves",
                  "content":{"kind":"weighted","selection":"per_unit","entries":[{"unit":{"type":"rpg.unit","id":"wolf"},"weight":1}]},
                  "schedule":{"kind":"maintain_population","target_alive":2,"initial_population":2,"replacement":{"after":"death","delay_seconds":[1,1]}},
                  "limits":{"alive":2,"total":{"kind":"unlimited"},"budget_group":"wolves"},
                  "placement":{"kind":"random_in_region","constraints":[{"kind":"walkable"}],"attempts_per_unit":2},
                  "batch_policy":"atomic","failure":{"placement_blocked":{"kind":"retry","delay_seconds":1}}})")
                                 .expect("template source");
                return eve::spawn::compileTemplate(value, units);
            }
        } templates(*units);
        auto mapSource = eve::Value::fromJson(R"({"schema":"eve.map-spawns","version":1,"map":"forest",
          "budgets":[{"id":"wolves","alive_limit":2}],"instances":[{"id":"wolves",
          "template":{"type":"spawn.template","id":"wolves"},"region":{"kind":"circle","space":"world2d","center":[0,0],"radius":10},
          "activation":{"kind":"map_loaded"},"inactive_policy":"pause","unload_policy":"persist"}]})")
                             .expect("map source");
        auto map         = eve::spawn::compileMap(mapSource, templates, *units).expect("real unit map");
        request.source   = sourceWorld->attach(map, providers).expect("source lifetime")[0];
        request.lifetime = sourceWorld->sourceLease(request.source).expect("source lease");
        request.operation =
            request.lifetime.operationIdentity(eve::spawn::OperationOrdinal(1)).expect("canonical operation");
        request.ordinal = eve::spawn::OperationOrdinal(1);
        for (int i = 0; i < 2; ++i)
            request.units.push_back(
                {request.lifetime.unitIdentity(eve::spawn::OperationOrdinal(1), static_cast<std::uint32_t>(i))
                     .expect("canonical member"),
                 eve::DefinitionRef::parse("rpg.unit:wolf").expect("unit reference")});
    }
    static eve::Value asset() {
        return eve::Value::fromJson(R"({"schema":"eve.rpg.unit","version":1,"id":"wolf","display_name":"Wolf",
          "skill":"spawn.basic","attack":7,"defense":2,"max_hp":20,"speed":3})")
            .expect("unit JSON");
    }
    std::size_t count() {
        std::size_t result = 0;
        auto        view   = ecs::View<eve::rpg::RPGActor>(*table);
        for (auto it = view.begin(); it != view.end(); ++it) ++result;
        return result;
    }
    void commit() {
        auto token = factory->prepare(request, positions).expect("prepare real actors");
        std::get<std::unique_ptr<eve::spawn::IPreparedSpawn>>(token)->commit().expect("commit real actors");
    }
};
}  // namespace test_rpg_spawn
