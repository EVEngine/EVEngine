// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"building"}
#include "building/BuildingAssembly.h"
#include "building/BuildingDef.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve;
using namespace eve::building;
namespace {
void roofDefinitions() {
    BuildingRegistry::clear();
    BuildingDefinition floor;
    floor.id      = "roof.floor";
    floor.channel = "floor";
    floor.tags    = {"support"};
    BuildingRegistry::registerBuilding(floor);
    auto untagged = floor;
    untagged.id   = "roof.untagged";
    untagged.tags.clear();
    BuildingRegistry::registerBuilding(untagged);
    BuildingDefinition roof;
    roof.id          = "roof.gable";
    roof.channel     = "roof";
    roof.footprintW  = 4;
    roof.footprintH  = 3;
    roof.supportMode = "cell_below";
    roof.supportTag  = "support";
    BuildingRegistry::registerBuilding(roof);
}
}  // namespace

TEST_CASE("building.roof.everyCellRequiresSupportAtEveryRotation") {
    roofDefinitions();
    for (int q = 0; q < 4; ++q) {
        const int width  = q % 2 == 0 ? 4 : 3;
        const int height = q % 2 == 0 ? 3 : 4;
        for (int missing = 0; missing < width * height; ++missing) {
            for (bool wrongTag : {false, true}) {
                PlacementWorld world(8, 8, 1);
                for (int i = 0; i < width * height; ++i) {
                    if (i == missing && !wrongTag) continue;
                    const int id =
                        world.placeAt(i == missing ? "roof.untagged" : "roof.floor", 1 + i % width, 1 + i / width, 0);
                    REQUIRE(id > 0);
                }
                world.setActiveLevel(1);
                const int before = world.getBuildingCount();
                const int placed = world.placeAt("roof.gable", 1, 1, float(q * 90));
                REQUIRE(placed == 0);
                REQUIRE(world.getBuildingCount() == before);
            }
        }
    }
}

TEST_CASE("building.roof.assemblyRemodelRejectsMissingSupportAtomically") {
    roofDefinitions();
    PlacementWorld   workshop(8, 8, 1);
    std::vector<int> ids;
    for (int y = 1; y < 4; ++y)
        for (int x = 1; x < 5; ++x) {
            const int id = workshop.placeAt("roof.floor", x, y, 0);
            REQUIRE(id > 0);
            ids.push_back(id);
        }
    workshop.setActiveLevel(1);
    const int roofId = workshop.placeAt("roof.gable", 1, 1, 0);
    REQUIRE(roofId > 0);
    ids.push_back(roofId);
    auto blueprint = AssemblyService::capture(workshop, ids, *AssetGuid::parse("00000000-0000-7000-8000-000000000001"),
                                              {1, 1, 0, 0, 0});
    REQUIRE(blueprint.ok());
    for (int q = 0; q < 4; ++q) {
        PlacementWorld town(24, 24, 1);
        const auto     id        = *AssemblyId::parse("00000000-0000-7000-8000-000000000002");
        auto           placement = AssemblyService::preparePlacement(town, blueprint.value(), id, {10, 10, 0, q, 0});
        REQUIRE(placement.ok());
        auto receipt = AssemblyService::commit(town, std::move(placement).takeValue());
        REQUIRE(receipt.ok());
        auto assembly = AssemblyService::assembly(town, id);
        REQUIRE(assembly.ok());
        const auto before = AssemblyService::snapshotWorld(town);
        REQUIRE(before.ok());
        std::vector<PlacedBuilding> members;
        for (const auto& [key, member] : assembly.value().members) members.push_back(town.buildings().at(member));
        for (size_t i = 0; i < members.size(); ++i) {
            if (members[i].buildingId != "roof.floor") continue;
            auto desired = members;
            desired.erase(desired.begin() + i);
            auto remodel = AssemblyService::prepareRemodel(town, id, desired);
            REQUIRE(!remodel.ok());
            auto after = AssemblyService::snapshotWorld(town);
            REQUIRE(after.ok());
            REQUIRE(after.value() == before.value());
        }
        PlacementWorld restored(1, 1, 1);
        auto           restore = AssemblyService::restoreWorld(restored, before.value());
        REQUIRE(restore.ok());
        for (const auto& [memberId, part] : restored.buildings())
            if (part.buildingId == "roof.gable") REQUIRE(part.supportInstanceIds.size() == 12);
    }
}
