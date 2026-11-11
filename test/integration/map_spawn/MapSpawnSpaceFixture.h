#pragma once
#include <map>

#include "SpawnFactoryContract.h"
#include "common/ServiceInterfaces.h"
#include "definitions/Definitions.h"
#include "map/Map.h"
#include "map/Pathfinder.h"
#include "map/spawn/MapSpawnLoad.h"
#include "map/spawn/MapSpawnSpace.h"
#include "rpg/RPGActor.h"
#include "rpg/Skill.h"
#include "rpg/VitalsSystem.h"
#include "rpg/spawn/RPGSpawnFactory.h"
#include "rpg/spawn/RPGSpawnOccupancy.h"
#include "rpg/spawn/RPGUnitCatalogue.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_map_spawn_space {
namespace {
struct Templates final : eve::spawn::ITemplateResolver {
    const eve::spawn::IUnitDefinitionResolver& units;
    bool                                       footprints = true;
    explicit Templates(const eve::spawn::IUnitDefinitionResolver& source) : units(source) {}
    eve::Result<eve::spawn::CompiledTemplate> resolve(const eve::DefinitionRef&) const override {
        auto source = eve::Value::fromJson(R"({"schema":"eve.spawn-template","version":1,"id":"camp",
          "content":{"kind":"weighted","selection":"per_unit","entries":[{"unit":{"type":"rpg.unit","id":"wolf"},"weight":1}]},
          "schedule":{"kind":"maintain_population","target_alive":1,"initial_population":1,"replacement":{"after":"death","delay_seconds":[1,1]}},
          "limits":{"alive":1,"total":{"kind":"unlimited"},"budget_group":"hostiles"},
          "placement":{"kind":"random_in_region","constraints":[{"kind":"walkable"},{"kind":"clearance","radius":0.4},{"kind":"distance_from_players","min":1}],"attempts_per_unit":3},
          "batch_policy":"atomic","failure":{"placement_blocked":{"kind":"retry","delay_seconds":1}}})")
                          .expect("spatial template");
        if (!footprints)
            source.find("placement")
                ->set("constraints", eve::Value::array({eve::Value::object({{"kind", "walkable"}})}));
        return eve::spawn::compileTemplate(source, units);
    }
};
struct Assets final : eve::service::IFileSystem {
    std::map<std::string, std::string> text;
    bool                               readFile(const std::string& path, std::vector<std::uint8_t>& out) override {
        auto it = text.find(path);
        if (it == text.end()) return false;
        out.assign(it->second.begin(), it->second.end());
        return true;
    }
    bool writeFile(const std::string&, const void*, std::size_t) override { return false; }
    bool fileExists(const std::string& path) override { return text.contains(path); }
};
struct Fixture {
    eve::PersistentId           owner = *eve::PersistentId::parse("01900000-0000-7000-8000-000000000099");
    std::shared_ptr<ecs::Table> table = std::make_shared<ecs::Table>();
    std::shared_ptr<eve::definitions::DefinitionRegistry> registry =
        std::make_shared<eve::definitions::DefinitionRegistry>();
    std::shared_ptr<eve::rpg_spawn::RPGUnitCatalogue> units =
        std::make_shared<eve::rpg_spawn::RPGUnitCatalogue>(registry);
    std::shared_ptr<eve::rpg_spawn::RPGSpawnFactory> domain =
        std::make_shared<eve::rpg_spawn::RPGSpawnFactory>(table, units);
    std::shared_ptr<eve::rpg_spawn::RPGSpawnOccupancy> occupancy =
        std::make_shared<eve::rpg_spawn::RPGSpawnOccupancy>(table, 0.4);
    eve::map::Map                                        mapModule;
    eve::map::TileLayer*                                 layer = mapModule.newLayer(10, 10, 1, 1);
    eve::map::Pathfinder                                 navigation{layer};
    std::optional<eve::spawn::CompiledMap>               map;
    std::shared_ptr<eve::map_spawn::MapSpawnSpace>       space;
    std::shared_ptr<eve::map_spawn::SpatialSpawnFactory> factory;
    std::shared_ptr<eve::spawn::SpawnProviders>          providers;
    std::unique_ptr<eve::spawn::SpawnWorld>              sourceWorld;
    eve::spawn::BatchRequest                             request;
    std::vector<eve::spawn::Point>                       positions{{5, 5}};
    Fixture() {
        layer->fill(1);
        eve::rpg::SkillDefinition skill;
        skill.id = "spatial.basic";
        eve::rpg::SkillRegistry::registerSkill(skill);
        units
            ->publish(eve::Value::fromJson(R"({"schema":"eve.rpg.unit","version":1,"id":"wolf","display_name":"Wolf",
          "skill":"spatial.basic","attack":7,"defense":2,"max_hp":20,"speed":3})")
                          .expect("unit"))
            .expect("published unit");
        auto asset = eve::Value::fromJson(R"({"schema":"eve.map-spawns","version":1,"map":"forest",
          "budgets":[{"id":"hostiles","alive_limit":1}],"instances":[{"id":"camp","template":{"type":"spawn.template","id":"camp"},
          "region":{"kind":"points","space":"world2d","points":[[5,5]]},"activation":{"kind":"map_loaded"},
          "inactive_policy":"pause","unload_policy":"persist"}]})")
                         .expect("map");
        map   = eve::spawn::compileMap(asset, Templates(*units), *units).expect("compiled spatial map");
        space = std::make_shared<eve::map_spawn::MapSpawnSpace>(*map, owner, eve::map_spawn::SpawnGridTransform{},
                                                                occupancy);
        space->setNavigation(navigation.captureNavigationGrid().expect("real grid capture"))
            .expect("install navigation");
        space->setPlayers({}).expect("empty player frame");
        factory           = std::make_shared<eve::map_spawn::SpatialSpawnFactory>(domain, space);
        providers         = std::make_shared<eve::spawn::SpawnProviders>(eve::spawn::SpawnProviders{space, factory});
        sourceWorld       = std::make_unique<eve::spawn::SpawnWorld>(owner, 42);
        request.source    = sourceWorld->attach(*map, providers).expect("source")[0];
        request.lifetime  = sourceWorld->sourceLease(request.source).expect("lease");
        request.ordinal   = eve::spawn::OperationOrdinal(1);
        request.operation = request.lifetime.operationIdentity(request.ordinal).expect("operation");
        request.units.push_back({request.lifetime.unitIdentity(request.ordinal, 0).expect("subject"),
                                 eve::DefinitionRef::parse("rpg.unit:wolf").expect("reference")});
    }
    eve::Result<eve::map_spawn::PreparedMapSpawn> prepareAssets(eve::map::Map& target, bool withOccupancy = true) {
        Assets            files;
        eve::Value::Array cells(100, eve::Value(1));
        auto              document       = eve::Value::object({{"width", 10},
                                                               {"height", 10},
                                                               {"tilewidth", 1},
                                                               {"tileheight", 1},
                                                               {"data", eve::Value::array(std::move(cells))}});
        files.text["forest/map.json"]    = document.toJson().expect("map asset");
        files.text["forest/spawns.json"] = map->canonicalJson();
        auto binding = eve::Value::fromJson(R"({"schema":"eve.map-spawn-binding","version":1,"map_id":"forest",
            "map_uri":"project://forest/map.json","spawns_uri":"project://forest/spawns.json",
            "pixel_to_world":{"origin":[0,0],"scale":1}})")
                           .expect("asset binding");
        return eve::map_spawn::PreparedMapSpawn::prepare(
            target, binding, files, Templates(*units), *units, owner.child("asset-session"), 42,
            R"({"schema":"eve.map.object-contract","version":1,"types":[]})",
            {domain, withOccupancy ? occupancy : nullptr, std::vector<eve::spawn::Point>{}});
    }
    std::size_t count() {
        std::size_t count = 0;
        auto        view  = ecs::View<eve::rpg::RPGActor>(*table);
        for (auto it = view.begin(); it != view.end(); ++it) ++count;
        return count;
    }
};
}  // namespace
}  // namespace test_map_spawn_space
