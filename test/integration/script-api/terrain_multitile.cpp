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
[[maybe_unused]] Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    std::fill(result.data().begin(), result.data().end(), value);
    return result;
}
[[maybe_unused]] TerrainStampSettings setStamp(double x, double z, double width, double depth) {
    TerrainStampSettings settings;
    settings.centerX = x; settings.centerZ = z; settings.width = width; settings.depth = depth;
    settings.operation = TerrainStampOperation::Set;
    return settings;
}
}  // namespace


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
