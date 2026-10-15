#include <zeroerr/unittest.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include "Fixtures.h"
#include "animation/AnimSkin.h"
#include "common/SquirrelBinding.h"
#include "graphics/Graphics.h"
#include "procgen/PcgFrameRateManager.h"
#include "procgen/PcgSpawnProgress.h"
#include "procgen/PcgTaskQueue.h"
#include "procgen/Procgen.h"
#include "procgen/animation/PcgSkinnedMesh.h"
#include "procgen/animation/ProcgenAnimation.h"
#include "procgen/heightmap/PcgBiomeController.h"
#include "procgen/heightmap/PcgRuntimeStamper.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// pcg_biome_controller.cpp
using namespace eve::procgen;


TEST_CASE("procgen.pcgBiomeController.bindsRealVm") {
    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    exposeHeightmap(table);
    vm.run(vm.compileSource(R"(
      local b=eve.PcgBiomeController();
      assert(b.configure(10.0,20.0,30.0,50.0,200.0,3).ok);
      assert(b.getLoadMode()==3 && b.getRange()==50.0);
      assert(b.getRegularCenterX()==10.0 && b.getRegularCenterY()==20.0 && b.getRegularCenterZ()==30.0);
      assert(b.getRegularSize()==99.5 && b.getImpostorSize()==299.5);
      assert(b.tierAt(10.0,20.0,30.0)==0 && b.tierAt(110.0,20.0,30.0)==1);
      assert(b.fitToTerrain(-100.0,7.0,-50.0,200.0,80.0,100.0).ok);
      assert(b.getRegularCenterX()==0.0 && b.getRegularCenterY()==7.0 && b.getRange()==100.0);
    )"));
}

// pcg_frame_rate_manager.cpp
using namespace eve::procgen;


TEST_CASE("procgen.pcgFrameRateManager.squirrelContract") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    eve::script::exposeResultBindings(table);
    exposePcgFrameRateManagerBindings(table);
    vm.run(vm.compileSource(R"(
  local m=eve.PcgFrameRateManager();assert(m.configure(60,0.5,0,5,2).ok);
  for(local i=0;i<6;i++)assert(m.update(0.1,1.0).ok);
  assert(m.getQuality()==1&&m.getQualityChanged());local p=m.getPreset();
  assert(p.treeDistance==500.0&&p.detailObjectDensity==0.25&&p.heightmapMaximumLod==1);
 )"));
}

// pcg_runtime_stamper.cpp
using namespace eve::procgen;


// pcg_skinned_mesh.cpp
using namespace eve;

namespace {
[[maybe_unused]] procgen::MeshBuild triangle(const char* material, float offset) {
    procgen::MeshBuild mesh;
    mesh.setActiveGroup(material);
    mesh.addVertex(offset, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 0.F);
    mesh.addVertex(offset + 1.F, 0.F, 0.F, 1.F, 0.F, 0.F, 1.F, 0.F);
    mesh.addVertex(offset, 1.F, 0.F, 1.F, 0.F, 0.F, 0.F, 1.F);
    mesh.addTriangle(0, 1, 2);
    return mesh;
}

[[maybe_unused]] std::array<float, 16> identityBind() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

[[maybe_unused]] std::unique_ptr<animation::AnimSkin> skinFor(const procgen::MeshBuild& mesh, std::vector<int> bones,
                                             std::vector<std::array<float, 16>> binds, std::vector<int> joints,
                                             std::vector<float> weights) {
    animation::AnimSkinStreamData data;
    data.vertexCount         = mesh.getVertexCount();
    data.bindPositions       = mesh.positions();
    data.bindNormals         = mesh.normals();
    data.skeletonBones       = std::move(bones);
    data.inverseBindMatrices = std::move(binds);
    for (int bone : data.skeletonBones) data.boneNames.push_back("bone-" + std::to_string(bone));
    data.vertexSkinJoints = std::move(joints);
    data.vertexWeights    = std::move(weights);
    auto result           = animation::AnimSkin::fromStreams(std::move(data));
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}
}  // namespace


TEST_CASE("procgen.animation.pcgSkinnedMesh.realVmCombinesOwnedResult") {
    auto mesh = triangle("skin", 0.F);
    auto bind = identityBind();
    auto skin =
        skinFor(mesh, {7}, {bind}, {0, -1, -1, -1, 0, -1, -1, -1, 0, -1, -1, -1}, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
    procgen::Procgen                    procgen;
    procgen_animation::ProcgenAnimation bridge;
    ssq::VM                             vm(1024);
    auto                                table     = vm.addTable("eve");
    auto                                skinClass = table.addClass<animation::AnimSkin>(
        "AnimSkin", std::function<animation::AnimSkin*()>([]() -> animation::AnimSkin* { return nullptr; }), true);
    skinClass.addFunc("getVertexCount", &animation::AnimSkin::getVertexCount);
    procgen.expose(table);
    bridge.expose(table);
    vm.addFunc("pcgSkinMesh", [&]() { return &mesh; });
    vm.addFunc("pcgSkin", [&]() { return skin.get(); });
    vm.run(vm.compileSource(R"(
        local transform=eve.PcgMeshTransform();
        local plan=eve.PcgSkinnedMeshPlan();
        assert(plan.appendSource(pcgSkinMesh(),pcgSkin(),transform,"skin").ok);
        local output=eve.PcgSkinnedMeshResult();
        local module=eve.ProcgenAnimation();
        assert(module.combinePcgSkinnedMeshes(output,plan).ok);
        assert(output.isValid() && output.getSkin().getVertexCount()==3);
        local combined=output.copyMesh();
        assert(combined.getVertexCount()==3 && combined.getIndexCount()==3);
    )"));
}

TEST_CASE("procgen.animation.pcgSkinnedMesh.realGraphicsAcceptsCombinedSkinStreams") {
    auto mesh = triangle("skin", 0.F);
    auto bind = identityBind();
    auto skin =
        skinFor(mesh, {3}, {bind}, {0, -1, -1, -1, 0, -1, -1, -1, 0, -1, -1, -1}, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
    procgen_animation::PcgSkinnedMeshPlan plan;
    procgen::PcgMeshTransform             transform;
    REQUIRE(plan.appendSource(mesh, *skin, transform, "skin").ok());
    procgen_animation::PcgSkinnedMeshResult output;
    REQUIRE(combinePcgSkinnedMeshesInto(output, plan).ok());
    GfxFixture       fixture(96, 96, true);
    procgen::Procgen procgen;
    auto             gpuMesh = procgen.uploadMeshBorrowed(*output.mesh(), *fixture.gfx);
    REQUIRE(gpuMesh.isBound());
    REQUIRE(output.skin()->bindGpuMesh(fixture.gfx, gpuMesh.get()));
}

// pcg_spawn_progress.cpp
using namespace eve::procgen;


// pcg_task_queue.cpp
using namespace eve::procgen;
