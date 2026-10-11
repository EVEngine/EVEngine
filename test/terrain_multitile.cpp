#include <algorithm>
#include <limits>
#include <vector>
#include <simplesquirrel/simplesquirrel.hpp>
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainTreePlacement.h"
#include "procgen/PointSet.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;
namespace {
Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    std::fill(result.data().begin(), result.data().end(), value);
    return result;
}
TerrainStampSettings setStamp(double x, double z, double width, double depth) {
    TerrainStampSettings settings;
    settings.centerX = x; settings.centerZ = z; settings.width = width; settings.depth = depth;
    settings.operation = TerrainStampOperation::Set;
    return settings;
}
}  // namespace

TEST_CASE("procgen.multitile.pcgHeightmapSeamPixelMappingAndAtomicCommit") {
    Heightmap a = filled(3, 3, 0), b = filled(3, 3, 0), stamp = filled(1, 1, 7), one = filled(1, 1, 1);
    std::vector<TerrainHeightTile> tiles{{"west", &a, 0, 0, 2, 2, false},
                                         {"east", &b, 2, 0, 2, 2, false}};
    auto report = applyTerrainStampMultiTile(tiles, stamp, setStamp(2, 1, 2, 2), one, one);
    REQUIRE(report.ok());
    CHECK(report.value().affectedTiles == 2);
    CHECK(report.value().operationX == 1);
    CHECK(report.value().operationWidth == 3);
    REQUIRE(report.value().mappings.size() == 2);
    CHECK(report.value().mappings[0].localX == 1);
    CHECK(report.value().mappings[0].operationX == 0);
    CHECK(report.value().mappings[0].width == 2);
    CHECK(report.value().mappings[1].localX == 0);
    CHECK(report.value().mappings[1].operationX == 1);
    CHECK(report.value().mappings[1].width == 2);
    CHECK(a.height(2, 1) == 7);
    CHECK(b.height(0, 1) == 7);
}

TEST_CASE("procgen.multitile.operationDomainsUsePcgSeamStride") {
    TerrainStampSettings settings = setStamp(2, 1, 2, 2);
    std::vector<TerrainOperationTile> tiles = {
        {"west", 0, 0, 2, 2, 3, 3, false},
        {"east", 2, 0, 2, 2, 3, 3, false},
    };
    auto height = mapTerrainOperationMultiTile(tiles, settings, TerrainOperationDomain::Heightmap);
    REQUIRE(height.ok());
    CHECK(height.value().operationX == 1);
    CHECK(height.value().operationWidth == 3);
    REQUIRE(height.value().mappings.size() == 2);
    CHECK(height.value().mappings[0].operationX == 0);
    CHECK(height.value().mappings[0].width == 2);
    CHECK(height.value().mappings[1].operationX == 1);
    CHECK(height.value().mappings[1].width == 2);

    auto detail = mapTerrainOperationMultiTile(tiles, settings, TerrainOperationDomain::TerrainDetail);
    REQUIRE(detail.ok());
    CHECK(detail.value().operationX == 1);
    CHECK(detail.value().operationWidth == 4);
    REQUIRE(detail.value().mappings.size() == 2);
    CHECK(detail.value().mappings[0].operationX == 0);
    CHECK(detail.value().mappings[0].width == 2);
    CHECK(detail.value().mappings[1].operationX == 2);
    CHECK(detail.value().mappings[1].width == 2);

    tiles[1].worldMap = true;
    auto ordinary = mapTerrainOperationMultiTile(tiles, settings, TerrainOperationDomain::Texture);
    REQUIRE(ordinary.ok());
    CHECK(ordinary.value().affectedTiles == 1);
    auto selectedWorld = mapTerrainOperationMultiTile(tiles, settings, TerrainOperationDomain::GameObject, true,
                                                       {"east"});
    REQUIRE(selectedWorld.ok());
    CHECK(selectedWorld.value().affectedTiles == 1);
    CHECK(selectedWorld.value().mappings[0].terrainName == "east");

    tiles[1].name = "west";
    CHECK(!mapTerrainOperationMultiTile(tiles, settings, TerrainOperationDomain::Tree).ok());
    tiles[1].name = "east";
    tiles[1].originX = 2.25;
    CHECK(!mapTerrainOperationMultiTile(tiles, settings, TerrainOperationDomain::BakedMask).ok());
}

TEST_CASE("procgen.multitile.detailReplaceUsesSharedWindowAndAtomicPublish") {
    TerrainDetailLayer west, east;
    REQUIRE(west.reset(3, 3, 1).ok());
    REQUIRE(east.reset(3, 3, 1).ok());
    std::vector<TerrainDetailTile> tiles = {
        {"west", &west, 0, 0, 2, 2, false},
        {"east", &east, 2, 0, 2, 2, false},
    };
    Heightmap fitness = filled(4, 3, 1);
    TerrainDetailSettings detail;
    detail.minimumFitness = 0;
    detail.fadeStart = 0;
    detail.density = 4;
    detail.mode = TerrainDetailMode::Replace;
    auto result = applyTerrainDetailMultiTile(tiles, fitness, detail, setStamp(2, 1, 2, 2), 77);
    REQUIRE(result.ok());
    CHECK(result.value().affectedTiles == 2);
    CHECK(result.value().changedSamples == 18);
    CHECK(west.sample(0, 1).value() == 0);
    CHECK(west.sample(1, 1).value() == 4);
    CHECK(west.sample(2, 1).value() == 4);
    CHECK(east.sample(0, 1).value() == 4);
    CHECK(east.sample(1, 1).value() == 4);
    CHECK(east.sample(2, 1).value() == 0);

    Heightmap wrong = filled(3, 3, 0);
    const int beforeWest = west.sample(1, 1).value();
    const int beforeEast = east.sample(0, 1).value();
    CHECK(!applyTerrainDetailMultiTile(tiles, wrong, detail, setStamp(2, 1, 2, 2), 77).ok());
    CHECK(west.sample(1, 1).value() == beforeWest);
    CHECK(east.sample(0, 1).value() == beforeEast);

    tiles[1].layer = &west;
    CHECK(!applyTerrainDetailMultiTile(tiles, fitness, detail, setStamp(2, 1, 2, 2), 77).ok());
    CHECK(west.sample(1, 1).value() == beforeWest);
}

TEST_CASE("procgen.multitile.detailWorkspaceOwnsHistoryAndScriptBinding") {
    TerrainDetailLayer source, output;
    REQUIRE(source.reset(3, 3, 1).ok());
    REQUIRE(output.reset(3, 3, 0).ok());
    Heightmap fitness = filled(4, 3, 1);
    TerrainDetailSettings detail;
    detail.minimumFitness = detail.fadeStart = 0;
    detail.density = 5;
    TerrainMultiDetailWorkspace workspace;
    REQUIRE(workspace.addTile("west", source, 0, 0, 2, 2).ok());
    REQUIRE(workspace.addTile("east", source, 2, 0, 2, 2).ok());
    REQUIRE(workspace.apply(fitness, detail, setStamp(2, 1, 2, 2), 9).ok());
    CHECK(workspace.getTileCount() == 2);
    CHECK(workspace.getLastAffectedTiles() == 2);
    CHECK(workspace.getOperationCount() == 1);
    REQUIRE(workspace.copyTile("east", output).ok());
    CHECK(output.sample(0, 1).value() == 5);
    REQUIRE(workspace.undo().ok());
    REQUIRE(workspace.copyTile("east", output).ok());
    CHECK(output.sample(0, 1).value() == 1);
    REQUIRE(workspace.redo().ok());
    CHECK(workspace.getAppliedCount() == 1);

    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("source", [&]() { return &source; });
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local w=eve.TerrainMultiDetailWorkspace();
        assert(w.addTile("west",source(),0.0,0.0,2.0,2.0,false).ok);
        assert(w.addTile("east",source(),2.0,0.0,2.0,2.0,false).ok);
        local d=eve.TerrainDetailSettings();d.minimumFitness=0.0;d.fadeStart=0.0;d.density=6.0;
        local s=eve.TerrainStampSettings();s.setCenter(2.0,1.0);s.setSize(2.0,2.0);
        assert(w.apply(fitness(),d,s,0,11,false).ok);
        assert(w.getTileCount()==2 && w.getLastAffectedTiles()==2 && w.getLastChangedSamples()==18);
        assert(w.copyTile("east",output()).ok && output().sample(0,1).value==6);
        assert(w.undo().ok && w.copyTile("east",output()).ok && output().sample(0,1).value==1);
        assert(w.redo().ok && w.getOperationCount()==1 && w.getAppliedCount()==1);
    )"));
}

TEST_CASE("procgen.multitile.treeSharedStreamAddRemoveAndAtomicFailure") {
    PointSet west, east;
    Heightmap heights = filled(2, 2, 0.25F);
    Heightmap fitness = filled(4, 2, 1);
    std::vector<TerrainTreeTile> tiles = {
        {"west", &west, &heights, 0, 0, 2, 2, 2, 2, false},
        {"east", &east, &heights, 2, 0, 2, 2, 2, 2, false},
    };
    TerrainTreePlacementSettings tree;
    tree.spacing = tree.spawnDensity = 1;
    tree.jitterPercent = tree.failureRate = tree.minimumFitness = 0;
    tree.scaleMode = TerrainTreeScaleMode::Fixed;
    tree.minimumWidth = tree.maximumWidth = tree.minimumHeight = tree.maximumHeight = 1;
    tree.asset = "oak";
    tree.namespaceId = 42;
    tree.maxPoints = 100;
    tree.seed = 19;
    auto bounds = setStamp(2, 1, 4, 2);
    auto added = applyTerrainTreesMultiTile(tiles, fitness, tree, bounds, TerrainTreeOperationMode::Add);
    REQUIRE(added.ok());
    CHECK(added.value().affectedTiles == 2);
    CHECK(added.value().changedSamples == 8);
    CHECK(west.getCount() == 4);
    CHECK(east.getCount() == 4);
    CHECK(west.hasIntAttribute(0, "treeCandidate"));
    CHECK(east.hasIntAttribute(0, "treeCandidate"));
    CHECK(west.getIntAttribute(0, "treeCandidate", -1) != east.getIntAttribute(0, "treeCandidate", -1));
    std::vector<std::uint64_t> ids;
    for (const auto& point : west.points()) ids.push_back(point.id);
    for (const auto& point : east.points()) ids.push_back(point.id);
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());

    Heightmap wrong = filled(3, 2, 1);
    CHECK(!applyTerrainTreesMultiTile(tiles, wrong, tree, bounds, TerrainTreeOperationMode::Add).ok());
    CHECK(west.getCount() == 4);
    CHECK(east.getCount() == 4);
    PointSet existing;
    existing.add(99, 0, 99);
    tiles[0].trees = &existing;
    tree.maxPoints = 8;
    auto budgetIgnoresExisting = applyTerrainTreesMultiTile(tiles, fitness, tree, bounds, TerrainTreeOperationMode::Add);
    REQUIRE(budgetIgnoresExisting.ok());
    CHECK(existing.getCount() == 5);
    tree.maxPoints = 100;
    tiles[0].trees = &west;
    auto removed = applyTerrainTreesMultiTile(tiles, fitness, tree, bounds, TerrainTreeOperationMode::Remove);
    REQUIRE(removed.ok());
    CHECK(removed.value().changedSamples == 8);
    CHECK(west.empty());
    CHECK(east.empty());
}

TEST_CASE("procgen.multitile.treeWorkspaceOwnsHistoryAndScriptBinding") {
    PointSet empty, output;
    Heightmap heights = filled(2, 2, 0.25F), fitness = filled(4, 2, 1);
    TerrainTreePlacementSettings tree;
    tree.spacing = tree.spawnDensity = 1;
    tree.jitterPercent = tree.failureRate = tree.minimumFitness = 0;
    tree.scaleMode = TerrainTreeScaleMode::Fixed;
    tree.minimumWidth = tree.maximumWidth = tree.minimumHeight = tree.maximumHeight = 1;
    tree.asset = "oak"; tree.namespaceId = 77; tree.maxPoints = 100; tree.seed = 5;
    auto bounds = setStamp(2, 1, 4, 2);
    TerrainMultiTreeWorkspace workspace;
    REQUIRE(workspace.addTile("west", empty, heights, 0, 0, 2, 2, 2, 2).ok());
    REQUIRE(workspace.addTile("east", empty, heights, 2, 0, 2, 2, 2, 2).ok());
    REQUIRE(workspace.apply(fitness, tree, bounds, TerrainTreeOperationMode::Add).ok());
    REQUIRE(workspace.copyTile("east", output).ok());
    CHECK(output.getCount() == 4);
    REQUIRE(workspace.undo().ok());
    REQUIRE(workspace.copyTile("east", output).ok());
    CHECK(output.empty());
    REQUIRE(workspace.redo().ok());
    CHECK(workspace.getTileCount() == 2);
    CHECK(workspace.getLastChangedSamples() == 8);
    CHECK(workspace.getOperationCount() == 1);
    CHECK(workspace.getAppliedCount() == 1);

    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("emptyTrees", [&]() { return &empty; });
    vm.addFunc("heights", [&]() { return &heights; });
    vm.addFunc("fitness", [&]() { return &fitness; });
    vm.addFunc("output", [&]() { return &output; });
    vm.addFunc("pointCount", [](const PointSet* points) { return points->getCount(); });
    vm.run(vm.compileSource(R"(
        local w=eve.TerrainMultiTreeWorkspace();
        assert(w.addTile("west",emptyTrees(),heights(),0.0,0.0,2.0,2.0,2,2,false).ok);
        assert(w.addTile("east",emptyTrees(),heights(),2.0,0.0,2.0,2.0,2,2,false).ok);
        local t=eve.TerrainTreePlacementSettings();t.spacing=1.0;t.spawnDensity=1.0;
        t.jitterPercent=0.0;t.failureRate=0.0;t.minimumFitness=0.0;t.asset="oak";
        t.namespaceId=91;t.maxPoints=100;t.seed=5;t.setScaleMode(0);
        local s=eve.TerrainStampSettings();s.setCenter(2.0,1.0);s.setSize(4.0,2.0);
        assert(w.apply(fitness(),t,s,0,false).ok);
        assert(w.copyTile("east",output()).ok && pointCount(output())==4);
        assert(w.getTileCount()==2 && w.getLastAffectedTiles()==2 && w.getLastChangedSamples()==8);
        assert(w.undo().ok && w.copyTile("east",output()).ok && pointCount(output())==0);
        assert(w.redo().ok && w.getOperationCount()==1 && w.getAppliedCount()==1);
    )"));
}

TEST_CASE("procgen.multitile.touchOnlyAllowListAndWorldMapDomain") {
    Heightmap a = filled(3, 3, 0), b = filled(3, 3, 0), world = filled(3, 3, 0);
    Heightmap stamp = filled(1, 1, 4), one = filled(1, 1, 1);
    std::vector<TerrainHeightTile> tiles{{"a", &a, 0, 0, 2, 2, false},
                                         {"b", &b, 2, 0, 2, 2, false},
                                         {"world", &world, 0, 0, 2, 2, true}};
    auto ordinary = applyTerrainStampMultiTile(tiles, stamp, setStamp(1, 1, 2, 2), one, one, false, {"a", "b"});
    REQUIRE(ordinary.ok());
    CHECK(ordinary.value().affectedTiles == 1);
    CHECK(a.height(1, 1) == 4);
    CHECK(b.height(0, 1) == 0);
    CHECK(world.height(1, 1) == 0);
    auto worldOnly = applyTerrainStampMultiTile(tiles, stamp, setStamp(1, 1, 2, 2), one, one, true);
    REQUIRE(worldOnly.ok());
    CHECK(worldOnly.value().affectedTiles == 1);
    CHECK(world.height(1, 1) == 4);
}

TEST_CASE("procgen.multitile.lateFailureAndDescriptorErrorsPreserveEveryTile") {
    Heightmap a = filled(2, 2, 1), b = filled(2, 2, std::numeric_limits<float>::max() * 0.8F);
    Heightmap stamp = filled(1, 1, std::numeric_limits<float>::max() * 0.4F), one = filled(1, 1, 1);
    std::vector<TerrainHeightTile> tiles{{"a", &a, 0, 0, 1, 1, false}, {"b", &b, 1, 0, 1, 1, false}};
    TerrainStampSettings settings = setStamp(1, 0.5, 2, 1);
    settings.operation = TerrainStampOperation::Add;
    const auto beforeA = a.data(), beforeB = b.data();
    CHECK(!applyTerrainStampMultiTile(tiles, stamp, settings, one, one).ok());
    CHECK(a.data() == beforeA);
    CHECK(b.data() == beforeB);
    tiles[1].heightmap = &a;
    CHECK(!applyTerrainStampMultiTile(tiles, stamp, settings, one, one).ok());
    CHECK(a.data() == beforeA);
    CHECK(b.data() == beforeB);
    tiles[1] = {"b", &b, 1.25, 0, 1, 1, false};
    CHECK(!applyTerrainStampMultiTile(tiles, stamp, settings, one, one).ok());
    CHECK(a.data() == beforeA);
    CHECK(b.data() == beforeB);
}

TEST_CASE("procgen.multitile.workspaceOwnsInputsAndPublishesReport") {
    Heightmap source = filled(3, 3, 1), out = filled(3, 3, 0), stamp = filled(1, 1, 6), one = filled(1, 1, 1);
    TerrainMultiTileWorkspace workspace;
    REQUIRE(workspace.addTile("tile", source, 0, 0, 2, 2).ok());
    source.data()[4] = 99;
    REQUIRE(workspace.stamp(stamp, setStamp(1, 1, 2, 2), one, one).ok());
    CHECK(workspace.getTileCount() == 1);
    CHECK(workspace.getLastAffectedTiles() == 1);
    CHECK(workspace.getLastMappingCount() == 1);
    REQUIRE(workspace.copyTile("tile", out).ok());
    CHECK(out.height(1, 1) == 6);
    CHECK(workspace.getOperationCount() == 1);
    CHECK(workspace.getAppliedCount() == 1);
    REQUIRE(workspace.undo().ok());
    CHECK(workspace.getAppliedCount() == 0);
    REQUIRE(workspace.copyTile("tile", out).ok());
    CHECK(out.height(1, 1) == 1);
    REQUIRE(workspace.redo().ok());
    REQUIRE(workspace.copyTile("tile", out).ok());
    CHECK(out.height(1, 1) == 6);
    CHECK(!workspace.addTile("tile", source, 2, 0, 2, 2).ok());
    REQUIRE(workspace.copyTile("tile", out).ok());
    CHECK(out.height(1, 1) == 6);
}

TEST_CASE("procgen.multitile.scriptOwnedTransaction") {
    Heightmap west = filled(3, 3, 0), east = filled(3, 3, 0), out = filled(3, 3, 0);
    Heightmap stamp = filled(1, 1, 3), one = filled(1, 1, 1);
    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("west", [&]() { return &west; });
    vm.addFunc("east", [&]() { return &east; });
    vm.addFunc("stampRaster", [&]() { return &stamp; });
    vm.addFunc("one", [&]() { return &one; });
    vm.addFunc("out", [&]() { return &out; });
    vm.run(vm.compileSource(R"(
        local w=eve.TerrainMultiTileWorkspace();
        assert(w.addTile("west",west(),0.0,0.0,2.0,2.0,false).ok);
        assert(w.addTile("east",east(),2.0,0.0,2.0,2.0,false).ok);
        local s=eve.TerrainStampSettings();s.setCenter(2.0,1.0);s.setSize(2.0,2.0);
        assert(w.stamp(stampRaster(),s,3,one(),one(),false).ok);
        assert(w.getTileCount()==2 && w.getLastAffectedTiles()==2 && w.getLastMappingCount()==2);
        assert(w.copyTile("east",out()).ok && out().height(0,1)==3.0);
        assert(w.getOperationCount()==1 && w.getAppliedCount()==1);
        assert(w.undo().ok && w.getAppliedCount()==0);
        assert(w.copyTile("east",out()).ok && out().height(0,1)==0.0);
        assert(w.redo().ok && w.getAppliedCount()==1);
        assert(w.copyTile("east",out()).ok && out().height(0,1)==3.0);
        assert(!w.copyTile("missing",out()).ok);
    )"));
    CHECK(west.height(1, 1) == 0);
    CHECK(east.height(0, 1) == 0);
}

TEST_CASE("procgen.terrainStitcher.matchesPcgSeamAndFailsAtomically") {
    Heightmap south = filled(5, 5, 0), north = filled(5, 5, 1);
    TerrainHeightTile a{"south", &south, 0, 0, 4, 4, false};
    TerrainHeightTile b{"north", &north, 0, 4, 4, 4, false};
    TerrainHeightStitchSettings settings;
    settings.extraSeamSize = 1;
    auto stitched = stitchTerrainHeightmaps(a, b, settings);
    REQUIRE(stitched.ok());
    CHECK(stitched.value() == 10);
    for (int x = 0; x < 5; ++x) {
        CHECK(south.height(x, 4) == 0.5F);
        CHECK(north.height(x, 0) == 0.5F);
        CHECK(south.height(x, 3) == 0);
        CHECK(north.height(x, 1) == 1);
    }
    const auto beforeSouth = south.data(), beforeNorth = north.data();
    b.originZ = 9;
    CHECK(!stitchTerrainHeightmaps(a, b, settings).ok());
    CHECK(south.data() == beforeSouth);
    CHECK(north.data() == beforeNorth);
}

TEST_CASE("procgen.terrainStitcher.workspaceUndoAndScriptBinding") {
    Heightmap low = filled(3, 3, 0), high = filled(3, 3, 1), output = filled(3, 3, 0);
    TerrainMultiTileWorkspace workspace;
    REQUIRE(workspace.addTile("west", low, 0, 0, 2, 2).ok());
    REQUIRE(workspace.addTile("east", high, 2, 0, 2, 2).ok());
    TerrainHeightStitchSettings settings;
    REQUIRE(workspace.stitch("west", "east", settings).ok());
    REQUIRE(workspace.copyTile("west", output).ok());
    CHECK(output.height(2, 1) == 0.5F);
    REQUIRE(workspace.undo().ok());
    REQUIRE(workspace.copyTile("west", output).ok());
    CHECK(output.height(2, 1) == 0);
    REQUIRE(workspace.redo().ok());

    ssq::VM vm(1024);
    auto table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.addFunc("low", [&]() { return &low; });
    vm.addFunc("high", [&]() { return &high; });
    vm.addFunc("output", [&]() { return &output; });
    vm.run(vm.compileSource(R"(
        local w=eve.TerrainMultiTileWorkspace();
        assert(w.addTile("south",low(),0.0,0.0,2.0,2.0,false).ok);
        assert(w.addTile("north",high(),0.0,2.0,2.0,2.0,false).ok);
        local s=eve.TerrainHeightStitchSettings();s.extraSeamSize=1;s.maxDifference=1.0;
        assert(w.stitch("south","north",s).ok);
        assert(w.copyTile("south",output()).ok && output().height(1,2)==0.5);
        assert(w.undo().ok && w.copyTile("south",output()).ok && output().height(1,2)==0.0);
    )"));
}
