#include <memory>
#include "building/BuildingAssembly.h"
#include "building/BuildingAssemblyProtocol.h"
#include "building/BuildingDef.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve;
using namespace eve::building;
namespace {
AssetGuid  asset() { return *AssetGuid::parse("00000000-0000-7000-8000-000000000001"); }
AssemblyId house(int n) { return AssemblyId::parse("00000000-0000-7000-8000-000000000010")->child(std::to_string(n)); }
void       definitions() {
    BuildingRegistry::clear();
    BuildingDefinition floor;
    floor.id      = "assembly.floor";
    floor.channel = "floor";
    floor.tags    = {"support"};
    BuildingRegistry::registerBuilding(floor);
    auto upper        = floor;
    upper.id          = "assembly.upper";
    upper.supportMode = "cell_below";
    upper.supportTag  = "support";
    BuildingRegistry::registerBuilding(upper);
    BuildingDefinition wall;
    wall.id            = "assembly.wall";
    wall.placementKind = "edge";
    wall.channel       = "wall";
    BuildingRegistry::registerBuilding(wall);
    auto corner          = wall;
    corner.id            = "assembly.corner";
    corner.placementKind = "corner";
    BuildingRegistry::registerBuilding(corner);
    BuildingDefinition chair;
    chair.id                       = "assembly.chair";
    chair.placementKind            = "free";
    chair.channel                  = "furniture";
    chair.freeFootprintWidthCells  = .5f;
    chair.freeFootprintHeightCells = .3f;
    BuildingRegistry::registerBuilding(chair);
}
BuildingBlueprint recipe() {
    PlacementWorld source(12, 12, 1);
    source.setGridPlane("xz");
    source.setOrigin(100, 200);
    std::vector<int> ids;
    for (int y = 2; y < 4; ++y)
        for (int x = 2; x < 4; ++x) ids.push_back(source.placeAt("assembly.floor", x, y, 0));
    ids.push_back(PlacementSystem::placeEdge(&source, "assembly.wall", 2, 2, "north"));
    ids.push_back(PlacementSystem::placeCorner(&source, "assembly.corner", 2, 2));
    ids.push_back(PlacementSystem::placeFree(&source, "assembly.chair", 102.5f, 202.5f, 0));
    source.setActiveLevel(1);
    ids.push_back(source.placeAt("assembly.upper", 2, 2, 0));
    for (int id : ids) REQUIRE(id > 0);
    auto b = AssemblyService::capture(source, ids, asset(), {2, 2, 0, 0, 0});
    REQUIRE(b.ok());
    return std::move(b).takeValue();
}
AssemblyReceipt place(PlacementWorld& w, const BuildingBlueprint& b, int n, AssemblyPose pose) {
    auto token = AssemblyService::preparePlacement(w, b, house(n), pose);
    REQUIRE(token.ok());
    auto committed = AssemblyService::commit(w, std::move(token).takeValue());
    REQUIRE(committed.ok());
    return std::move(committed).takeValue();
}
}  // namespace
TEST_CASE("building.assembly.completeHouseVillageRemodelSaveRestore") {
    definitions();
    auto b       = recipe();
    auto encoded = AssemblyService::encodeBlueprint(b);
    REQUIRE(encoded.ok());
    auto json = encoded.value().toJson();
    REQUIRE(json.ok());
    auto parsed = Value::fromJson(json.value());
    REQUIRE(parsed.ok());
    auto decoded = AssemblyService::decodeBlueprint(parsed.value());
    REQUIRE(decoded.ok());
    PlacementWorld town(32, 32, 1);
    town.setGridPlane("xz");
    town.setOrigin(-20, 40);
    auto receipt = place(town, decoded.value(), 1, {5, 5, 0, 0, 0});
    place(town, decoded.value(), 2, {15, 10, 0, 1, 0});
    auto first  = AssemblyService::assembly(town, house(1));
    auto second = AssemblyService::assembly(town, house(2));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(first.value().members.size() == b.parts.size());
    REQUIRE(second.value().members.size() == b.parts.size());
    std::vector<PlacedBuilding> desired;
    for (auto [key, id] : first.value().members) {
        auto p           = town.buildings().at(id);
        p.props["paint"] = "blue";
        desired.push_back(p);
    }
    auto remodel = AssemblyService::prepareRemodel(town, house(1), desired);
    REQUIRE(remodel.ok());
    auto remodelReceipt = AssemblyService::commit(town, std::move(remodel).takeValue());
    REQUIRE(remodelReceipt.ok());
    for (auto [key, id] : second.value().members) REQUIRE(town.buildings().at(id).props.empty());
    auto saved = AssemblyService::snapshotWorld(town);
    REQUIRE(saved.ok());
    auto serialized = saved.value().toJson();
    REQUIRE(serialized.ok());
    auto loaded = Value::fromJson(serialized.value());
    REQUIRE(loaded.ok());
    PlacementWorld restored(1, 1);
    auto           restoredResult = AssemblyService::restoreWorld(restored, loaded.value());
    REQUIRE(restoredResult.ok());
    auto after = AssemblyService::snapshotWorld(restored);
    REQUIRE(after.ok());
    REQUIRE(after.value() == saved.value());
    auto undo = AssemblyService::applyReceipt(restored, remodelReceipt.value().after, remodelReceipt.value().before);
    REQUIRE(undo.ok());
    auto redo = AssemblyService::applyReceipt(restored, remodelReceipt.value().before, remodelReceipt.value().after);
    REQUIRE(redo.ok());
    auto moved = AssemblyService::prepareMove(restored, house(1), {22, 22, 0, 2, 0});
    REQUIRE(moved.ok());
    auto movedResult = AssemblyService::commit(restored, std::move(moved).takeValue());
    REQUIRE(movedResult.ok());
    auto removed = AssemblyService::prepareRemoval(restored, house(2));
    REQUIRE(removed.ok());
    auto removeResult = AssemblyService::commit(restored, std::move(removed).takeValue());
    REQUIRE(removeResult.ok());
    REQUIRE(AssemblyService::assemblies(restored).size() == 1);
    REQUIRE(restored.getBuildingCount() == int(b.parts.size()));
}
TEST_CASE("building.assembly.atomicRejectionStaleHandlesAndDefinitions") {
    definitions();
    auto           b = recipe();
    PlacementWorld w(32, 32, 1);
    w.setGridPlane("xz");
    place(w, b, 1, {5, 5, 0, 0, 0});
    auto before = AssemblyService::snapshotWorld(w);
    REQUIRE(before.ok());
    PlacementSystem::clearEvents();
    auto blocked = AssemblyService::preparePlacement(w, b, house(2), {5, 5, 0, 0, 0});
    REQUIRE(!blocked.ok());
    auto unchanged = AssemblyService::snapshotWorld(w);
    REQUIRE(unchanged.ok());
    REQUIRE(unchanged.value() == before.value());
    REQUIRE(PlacementSystem::events().empty());
    auto token = AssemblyService::preparePlacement(w, b, house(2), {15, 15, 0, 0, 0});
    REQUIRE(token.ok());
    w.setTerrain(0, 0, 3);
    auto stale = AssemblyService::commit(w, std::move(token).takeValue());
    REQUIRE(!stale.ok());
    auto h = AssemblyService::handle(w, house(1));
    REQUIRE(h.ok());
    PlacementWorld other(32, 32, 1);
    REQUIRE(!AssemblyService::resolve(other, h.value()).ok());
    auto restore = AssemblyService::restoreWorld(w, before.value());
    REQUIRE(restore.ok());
    REQUIRE(!AssemblyService::resolve(w, h.value()).ok());
    auto pending = AssemblyService::preparePlacement(w, b, house(2), {15, 15, 0, 0, 0});
    REQUIRE(pending.ok());
    auto def           = *BuildingRegistry::find("assembly.floor");
    def.requireTerrain = {7};
    BuildingRegistry::registerBuilding(def);
    auto invalidated = AssemblyService::commit(w, std::move(pending).takeValue());
    REQUIRE(!invalidated.ok());
    REQUIRE(w.getBuildingCount() == int(b.parts.size()));
}
TEST_CASE("building.assembly.malformedRestoreAndExternalSupportAreAtomic") {
    definitions();
    auto           b = recipe();
    PlacementWorld w(32, 32, 1);
    w.setGridPlane("xz");
    place(w, b, 1, {5, 5, 0, 0, 0});
    auto saved = AssemblyService::snapshotWorld(w);
    REQUIRE(saved.ok());
    auto invalid = saved.value();
    invalid.set("version", 99);
    auto rejected = AssemblyService::restoreWorld(w, invalid);
    REQUIRE(!rejected.ok());
    auto same = AssemblyService::snapshotWorld(w);
    REQUIRE(same.ok());
    REQUIRE(same.value() == saved.value());
    w.setActiveLevel(2);
    int dependent = w.placeAt("assembly.upper", 5, 5, 0);
    REQUIRE(dependent > 0);
    auto removal = AssemblyService::prepareRemoval(w, house(1));
    REQUIRE(!removal.ok());
    REQUIRE(w.hasBuilding(dependent));
}
TEST_CASE("building.assembly.rotatesAsymmetricFootprintsAndCompleteCurves") {
    definitions();
    auto mask          = *BuildingRegistry::find("assembly.floor");
    mask.id            = "assembly.mask";
    mask.footprintW    = 2;
    mask.footprintH    = 2;
    mask.footprintMask = {1, 1, 1, 0};
    BuildingRegistry::registerBuilding(mask);
    PlacementWorld source(16, 16, 1);
    const int      piece = source.placeAt(mask.id, 2, 2, 0);
    REQUIRE(piece > 0);
    auto curve = PlacementSystem::placeEdgeCubicBezier(&source, "assembly.wall", {{2, 5}, {3, 5}, {5, 6}, {6, 6}}, 12);
    REQUIRE(curve.ok());
    std::vector<int> selection{piece};
    selection.insert(selection.end(), curve.value().instanceIds.begin(), curve.value().instanceIds.end());
    auto captured = AssemblyService::capture(source, selection, asset(), {2, 2, 0, 0, 0});
    REQUIRE(captured.ok());
    REQUIRE(captured.value().curves.size() == 1);
    for (int q = 0; q < 4; ++q) {
        PlacementWorld town(32, 32, 1);
        place(town, captured.value(), 1, {12, 12, 0, q, 0});
        auto rotate = [q](int x, int y) {
            for (int i = 0; i < q; ++i) {
                const int old = x;
                x             = y;
                y             = -old;
            }
            return std::pair{x, y};
        };
        for (auto [x, y] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{0, 1}}) {
            auto a = rotate(x, y), b = rotate(x + 1, y + 1);
            REQUIRE(town.channelOccupancy(
                        "floor")[size_t(12 + std::min(a.second, b.second)) * 32 + 12 + std::min(a.first, b.first)] > 0);
        }
        REQUIRE(town.edgeCurveGroupIds().size() == 1);
        std::vector<int> townIds;
        for (const auto& [id, part] : town.buildings()) townIds.push_back(id);
        auto recaptured = AssemblyService::capture(town, townIds, asset(), {12, 12, 0, q, 0});
        REQUIRE(recaptured.ok());
        REQUIRE(recaptured.value().curves.front().group.controlPoints.front().x == 0.f);
        REQUIRE(recaptured.value().curves.front().group.controlPoints.front().y == 3.f);
        auto saved = AssemblyService::snapshotWorld(town);
        REQUIRE(saved.ok());
        PlacementWorld restored(1, 1);
        auto           restoredResult = AssemblyService::restoreWorld(restored, saved.value());
        REQUIRE(restoredResult.ok());
        auto after = AssemblyService::snapshotWorld(restored);
        REQUIRE(after.ok());
        REQUIRE(after.value() == saved.value());
    }
    REQUIRE(curve.value().instanceIds.size() > 1);
    std::vector<int> partial{curve.value().instanceIds.front()};
    auto             rejected = AssemblyService::capture(source, partial, asset(), {});
    REQUIRE(!rejected.ok());
}
TEST_CASE("building.assembly.wallFurnitureCollisionIsSymmetric") {
    definitions();
    auto wall               = *BuildingRegistry::find("assembly.wall");
    wall.edgeThicknessCells = .2f;
    BuildingRegistry::registerBuilding(wall);
    auto chair                  = *BuildingRegistry::find("assembly.chair");
    chair.blockedByEdgeChannels = {"wall"};
    BuildingRegistry::registerBuilding(chair);
    PlacementWorld wallsFirst(10, 10, 1);
    REQUIRE(PlacementSystem::placeEdge(&wallsFirst, wall.id, 3, 3, "north") > 0);
    REQUIRE(!PlacementSystem::canPlaceFree(&wallsFirst, chair.id, 3.5f, 3.f));
    REQUIRE(PlacementSystem::canPlaceFree(&wallsFirst, chair.id, 3.5f, 3.5f));
    PlacementWorld chairFirst(10, 10, 1);
    REQUIRE(PlacementSystem::placeFree(&chairFirst, chair.id, 3.5f, 3.f, 0) > 0);
    REQUIRE(!PlacementSystem::canPlaceEdge(&chairFirst, wall.id, 3, 3, "north"));
}
TEST_CASE("building.assembly.staleIndexesAndRemodelMemberKeys") {
    definitions();
    auto           b = recipe();
    PlacementWorld w(32, 32, 1);
    w.setGridPlane("xz");
    place(w, b, 1, {5, 5, 0, 0, 0});
    auto pending = AssemblyService::preparePlacement(w, b, house(2), {15, 15, 0, 0, 0});
    REQUIRE(pending.ok());
    auto&     channel = w.channelOccupancy("floor");
    const int old     = channel.front();
    channel.front()   = 123456;
    auto stale        = AssemblyService::commit(w, std::move(pending).takeValue());
    REQUIRE(!stale.ok());
    channel.front() = old;
    auto assembly   = AssemblyService::assembly(w, house(1));
    REQUIRE(assembly.ok());
    std::vector<PlacedBuilding> desired;
    uint32_t                    removedKey = 0;
    PlacedBuilding              replacement;
    for (auto [key, id] : assembly.value().members) {
        auto part = w.buildings().at(id);
        if (part.buildingId == "assembly.chair") {
            removedKey  = key;
            replacement = part;
        } else
            desired.push_back(part);
    }
    REQUIRE(removedKey > 0);
    auto removal = AssemblyService::prepareRemodel(w, house(1), desired);
    REQUIRE(removal.ok());
    auto removed = AssemblyService::commit(w, std::move(removal).takeValue());
    REQUIRE(removed.ok());
    replacement.instanceId = 0;
    desired.push_back(replacement);
    auto addition = AssemblyService::prepareRemodel(w, house(1), desired);
    REQUIRE(addition.ok());
    auto added = AssemblyService::commit(w, std::move(addition).takeValue());
    REQUIRE(added.ok());
    auto updated = AssemblyService::assembly(w, house(1));
    REQUIRE(updated.ok());
    REQUIRE(!updated.value().members.contains(removedKey));
    REQUIRE(updated.value().members.rbegin()->first >= assembly.value().nextPartKey);
    auto empty = AssemblyService::prepareRemodel(w, house(1), {});
    REQUIRE(empty.ok());
    auto erased = AssemblyService::commit(w, std::move(empty).takeValue());
    REQUIRE(erased.ok());
    REQUIRE(AssemblyService::assemblies(w).empty());
    REQUIRE(w.getBuildingCount() == 0);
}

TEST_CASE("building.assembly.protocolRejectsOversizeReceiptBeforeMutation") {
    definitions();
    auto b       = recipe();
    auto encoded = AssemblyService::encodeBlueprint(b);
    REQUIRE(encoded.ok());
    PlacementWorld world(230, 230, 1);
    world.setGridPlane("xz");
    PlacementSystem::clearEvents();
    auto result = executeHouseRequest(
        world, Value::Object{
                   {"version", 1},
                   {"op", "place"},
                   {"assemblyId", house(1).format()},
                   {"blueprint", encoded.value()},
                   {"pose", Value::Object{{"x", 5}, {"y", 5}, {"level", 0}, {"quarterTurns", 0}, {"elevation", 0.0}}}});
    REQUIRE(!result.ok());
    REQUIRE(world.getBuildingCount() == 0);
    REQUIRE(AssemblyService::assemblies(world).empty());
    REQUIRE(PlacementSystem::events().empty());
}
