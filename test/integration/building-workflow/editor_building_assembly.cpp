#include "building/BuildingAssembly.h"
#include "building/BuildingDef.h"
#include "building/PlacementWorld.h"
#include "building/editing/BuildingAssemblyTarget.h"
#include "editing/EditingAuthority.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve;
TEST_CASE("editing.buildingAssembly.atomicStagingUndoRedo") {
    building::BuildingDefinition d;
    d.id = "editing.assembly.floor";
    building::BuildingRegistry::registerBuilding(d);
    building::PlacementWorld source(8, 8, 1), town(20, 20, 1);
    int                      member = source.placeAt(d.id, 1, 1, 0);
    REQUIRE(member > 0);
    auto guid = *AssetGuid::parse("00000000-0000-7000-8000-000000000001");
    auto id   = *building::AssemblyId::parse("00000000-0000-7000-8000-000000000002");
    auto blueprint =
        building::AssemblyService::capture(source, std::span<const int>(&member, 1), guid, {1, 1, 0, 0, 0});
    REQUIRE(blueprint.ok());
    auto prepared = building::AssemblyService::preparePlacement(town, blueprint.value(), id, {5, 5, 0, 0, 0});
    REQUIRE(prepared.ok());
    building_editing::BuildingAssemblyTarget target("village", town);
    auto                                     operation = target.makeOperation(std::move(prepared).takeValue());
    REQUIRE(operation.ok());
    CHECK(town.getBuildingCount() == 0);
    auto candidate = target.cloneDomainState();
    REQUIRE(candidate != nullptr);
    auto applied = candidate->applyDomainOperation(operation.value());
    REQUIRE(applied.ok());
    CHECK(town.getBuildingCount() == 0);
    auto committed = target.commitDomainState(std::move(candidate));
    REQUIRE(committed.ok());
    CHECK(town.getBuildingCount() == 1);
    editing::DomainOperation inverse = operation.value();
    inverse.payload                  = operation.value().inverse;
    auto undone                      = target.applyDomainOperation(inverse);
    REQUIRE(undone.ok());
    CHECK(town.getBuildingCount() == 0);
    auto redone = target.applyDomainOperation(operation.value());
    REQUIRE(redone.ok());
    CHECK(town.getBuildingCount() == 1);
    auto stale = target.cloneDomainState();
    REQUIRE(stale != nullptr);
    town.setTerrain(0, 0, 9);
    auto rejected = target.commitDomainState(std::move(stale));
    CHECK(!rejected.ok());
    CHECK(town.getTerrain(0, 0) == 9);
}
