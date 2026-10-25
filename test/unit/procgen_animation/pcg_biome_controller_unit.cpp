#include "animation/AnimSkin.h"
#include "procgen/PcgFrameRateManager.h"
#include "procgen/PcgSpawnProgress.h"
#include "procgen/PcgTaskQueue.h"
#include "procgen/animation/PcgSkinnedMesh.h"
#include "procgen/heightmap/PcgBiomeController.h"
#include "procgen/heightmap/PcgRuntimeStamper.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace eve::procgen;
namespace procgen_animation = eve::procgen_animation;

namespace {
MeshBuild triangle(const char* material, float offset) {
    MeshBuild mesh;
    mesh.setActiveGroup(material);
    mesh.addVertex(offset, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f);
    mesh.addVertex(offset + 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f);
    mesh.addVertex(offset, 1.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    return mesh;
}

std::array<float, 16> identityBind() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

std::unique_ptr<eve::animation::AnimSkin> skinFor(const MeshBuild& mesh, std::vector<int> bones,
                                                  std::vector<std::array<float, 16>> binds, std::vector<int> joints,
                                                  std::vector<float> weights) {
    eve::animation::AnimSkinStreamData data;
    data.vertexCount         = mesh.getVertexCount();
    data.bindPositions       = mesh.positions();
    data.bindNormals         = mesh.normals();
    data.skeletonBones       = std::move(bones);
    data.inverseBindMatrices = std::move(binds);
    for (int bone : data.skeletonBones) data.boneNames.push_back("bone-" + std::to_string(bone));
    data.vertexSkinJoints = std::move(joints);
    data.vertexWeights    = std::move(weights);
    auto result           = eve::animation::AnimSkin::fromStreams(std::move(data));
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}
}  // namespace

TEST_CASE("procgen.pcg.spec.biomeStreaming") {
    SUB_CASE("regular, impostor, and unloaded tiers obey configured bounds") {
        PcgBiomeController controller;
        REQUIRE(controller.configure(100, 20, -40, 50, 200, PcgBiomeLoadMode::RuntimeAlways).ok());
        REQUIRE(controller.regularBounds().sizeX == 99.5);
        REQUIRE(controller.impostorBounds().sizeX == 299.5);
        REQUIRE(controller.tierAt(149.75, 20, -40) == PcgTerrainStreamingTier::Regular);
        REQUIRE(controller.tierAt(200, 20, -40) == PcgTerrainStreamingTier::Impostor);
        REQUIRE(controller.tierAt(250, 20, -40) == PcgTerrainStreamingTier::Unloaded);
        REQUIRE(controller.configure(1, 2, 3, 5, 0, PcgBiomeLoadMode::EditorAlways).ok());
        REQUIRE(controller.impostorBounds().sizeX == 0);
        REQUIRE(controller.tierAt(7, 2, 3) == PcgTerrainStreamingTier::Unloaded);
    };
    SUB_CASE("terrain fit uses surface Y; all-terrain fit uses volume midpoint") {
        PcgBiomeController controller;
        REQUIRE(controller.configure(0, 0, 0, 10, 20, PcgBiomeLoadMode::RuntimeAlways).ok());
        REQUIRE(controller.fitToTerrain(-100, 7, -50, 200, 80, 100).ok());
        REQUIRE(controller.regularBounds().centerX == 0);
        REQUIRE(controller.regularBounds().centerY == 7);
        REQUIRE(controller.range() == 100);
        REQUIRE(controller.fitToAllTerrains(-100, -10, -50, 300, 80, 100).ok());
        REQUIRE(controller.regularBounds().centerX == 50);
        REQUIRE(controller.regularBounds().centerY == 30);
        REQUIRE(controller.range() == 150);
        const auto before = controller.regularBounds();
        REQUIRE(!controller.fitToTerrain(0, 0, 0, -1, 1, 1).ok());
        REQUIRE(controller.regularBounds().centerX == before.centerX);
        REQUIRE(!controller.configure(0, 0, 0, std::numeric_limits<double>::infinity(), 0, PcgBiomeLoadMode::Disabled)
                     .ok());
    };
}

TEST_CASE("procgen.pcg.spec.frameRateQuality") {
    SUB_CASE("sustained low and high frame rates choose distinct quality presets") {
        PcgFrameRateManager low, high;
        REQUIRE(low.configure(60, .5f, 0, 5, 2).ok());
        REQUIRE(high.configure(60, .5f, 0, 5, 2).ok());
        for (int i = 0; i < 6; ++i) REQUIRE(low.update(.1f, 1).ok());
        for (int i = 0; i < 51; ++i) REQUIRE(high.update(.01f, 1).ok());
        REQUIRE(low.getQuality() == 1);
        REQUIRE(low.getQualityChanged());
        REQUIRE(low.getPreset().treeDistance == 500.f);
        REQUIRE(high.getQuality() == 3);
        REQUIRE(high.getFps() >= 99.f);
        REQUIRE(high.getPreset().detailObjectDensity == .7f);
    };
    SUB_CASE("manual quality and invalid update preserve the selected level") {
        PcgFrameRateManager manager;
        REQUIRE(manager.configure(60, 10, 1, 4, 2).ok());
        REQUIRE(manager.selectManualQuality(4).ok());
        REQUIRE(!manager.getAutomatic());
        REQUIRE(manager.getQuality() == 4);
        REQUIRE(!manager.selectManualQuality(5).ok());
        REQUIRE(!manager.update(std::numeric_limits<float>::quiet_NaN(), 1).ok());
        REQUIRE(manager.getQuality() == 4);
    };
}

TEST_CASE("procgen.pcg.spec.runtimeStamp") {
    SUB_CASE("configured stamp normalizes address, loads, executes, and reports completion") {
        PcgRuntimeStamper stamper;
        REQUIRE(stamper.configure("Pcg\\Stamps\\Rugged", true, true).ok());
        REQUIRE(stamper.stampAddress() == "Pcg/Stamps/Rugged");
        REQUIRE(stamper.status() == PcgRuntimeStampStatus::AwaitingResource);
        Heightmap stamp(3, 3);
        std::fill(stamp.data().begin(), stamp.data().end(), 1.f);
        REQUIRE(stamper.loadStamp(stamp, "Rugged").ok());
        Heightmap terrain(5, 5);
        std::fill(terrain.data().begin(), terrain.data().end(), 20.f);
        REQUIRE(stamper.execute(terrain).ok());
        REQUIRE(terrain.height(2, 2) == 6.f);
        REQUIRE(terrain.height(0, 0) == 0.f);
        REQUIRE(stamper.status() == PcgRuntimeStampStatus::Completed);
        REQUIRE(stamper.progressText() == "Stamp progress: 1");
        REQUIRE(std::fabs(stamper.updateTimeAllowed() - 1.0 / 15.0) < 1e-12);
    };
    SUB_CASE("missing resource reports failure and invalid execution preserves terrain") {
        PcgRuntimeStamper stamper;
        Heightmap         terrain(2, 2);
        std::fill(terrain.data().begin(), terrain.data().end(), 3.f);
        const auto before = terrain.data();
        REQUIRE(!stamper.execute(terrain).ok());
        REQUIRE(terrain.data() == before);
        REQUIRE(!stamper.configure("", true, true).ok());
        REQUIRE(stamper.configure("missing\\stamp", false, true).ok());
        REQUIRE(!stamper.reportMissingStamp().ok());
        REQUIRE(stamper.status() == PcgRuntimeStampStatus::Failed);
        REQUIRE(stamper.progressText() == "Failed to load stamp at missing/stamp");
        REQUIRE(stamper.updateLayout(1920, 1080).ok());
        REQUIRE(stamper.labelCenterX() == 960);
        REQUIRE(stamper.labelCenterY() == 1060);
    };
}

TEST_CASE("procgen.pcg.spec.skinnedMeshCombination") {
    SUB_CASE("shared source bone ids remap by identity and bind pose") {
        auto first  = triangle("shared", 0.f);
        auto second = triangle("shared", 2.f);
        auto bindA = identityBind(), bindB = identityBind();
        bindB[12]      = 3.f;
        auto firstSkin = skinFor(first, {10, 10}, {bindA, bindB}, {0, -1, -1, -1, 1, -1, -1, -1, -1, -1, -1, -1},
                                 {1, 0, 0, 0, .75f, 0, 0, 0, 0, 0, 0, 0});
        auto secondSkin =
            skinFor(second, {10, 20}, {bindA, identityBind()}, {0, -1, -1, -1, 1, -1, -1, -1, -1, -1, -1, -1},
                    {.5f, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0});
        procgen_animation::PcgSkinnedMeshPlan plan;
        PcgMeshTransform                      transform;
        REQUIRE(plan.appendSource(first, *firstSkin, transform, "shared").ok());
        REQUIRE(plan.appendSource(second, *secondSkin, transform, "shared").ok());
        procgen_animation::PcgSkinnedMeshResult output;
        REQUIRE(procgen_animation::combinePcgSkinnedMeshesInto(output, plan).ok());
        REQUIRE(output.valid());
        REQUIRE(output.mesh()->getVertexCount() == 6);
        REQUIRE(output.skin()->getBoneCount() == 4);
        REQUIRE(output.skin()->getVertexSkinJoint(0, 0) == 0);
        REQUIRE(output.skin()->getVertexSkinJoint(1, 0) == 2);
        REQUIRE(output.skin()->getVertexSkinJoint(4, 0) == 3);
        REQUIRE(output.skin()->getVertexWeight(1, 0) == .75f);
        REQUIRE(output.skin()->getInverseBindElement(2, 12) == 3.f);
    };
    SUB_CASE("invalid input and empty publication do not replace prior result") {
        eve::animation::AnimSkinStreamData invalid;
        invalid.vertexCount = 1;
        REQUIRE(!eve::animation::AnimSkin::fromStreams(std::move(invalid)).ok());
        auto mesh = triangle("kept", 0.f);
        auto skin = skinFor(mesh, {1}, {identityBind()}, {0, -1, -1, -1, 0, -1, -1, -1, 0, -1, -1, -1},
                            {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
        procgen_animation::PcgSkinnedMeshPlan plan;
        PcgMeshTransform                      transform;
        REQUIRE(plan.appendSource(mesh, *skin, transform, "kept").ok());
        procgen_animation::PcgSkinnedMeshResult output;
        REQUIRE(procgen_animation::combinePcgSkinnedMeshesInto(output, plan).ok());
        auto*                                 beforeSkin     = output.skin();
        const auto                            beforeVertices = output.mesh()->getVertexCount();
        procgen_animation::PcgSkinnedMeshPlan empty;
        REQUIRE(!procgen_animation::combinePcgSkinnedMeshesInto(output, empty).ok());
        REQUIRE(output.skin() == beforeSkin);
        REQUIRE(output.mesh()->getVertexCount() == beforeVertices);
    };
}

TEST_CASE("procgen.pcg.spec.spawnProgressAndTaskQueue") {
    SUB_CASE("progress combines completed rules and fraction; cancel and clear update status") {
        PcgSpawnProgress progress;
        REQUIRE(progress.updateRule("Forest", 10, 3, 4, 1).ok());
        REQUIRE(progress.title() == "Spawning");
        REQUIRE(progress.subtitle() == "Forest 1 of 4 Rules");
        REQUIRE(progress.updateRuleFraction(.5).ok());
        REQUIRE(std::fabs(progress.progress() - .35) < 1e-12);
        REQUIRE(!progress.updateRuleFraction(2).ok());
        REQUIRE(progress.requestCancel().ok());
        REQUIRE(progress.status() == PcgSpawnProgressStatus::CancelRequested);
        REQUIRE(progress.clear() == PcgSpawnProgressStatus::Hidden);
        REQUIRE(progress.title().empty());
        REQUIRE(progress.updateRule("World", 5, 5, 1, 1).ok());
        REQUIRE(progress.status() == PcgSpawnProgressStatus::Completed);
        REQUIRE(progress.progress() == 1);
        REQUIRE(!progress.requestCancel().ok());
    };
    SUB_CASE("task queue waits, publishes readiness, and rotates unfinished work") {
        PcgTaskQueue queue;
        auto         first = queue.add();
        REQUIRE(first.ok());
        auto waiting = queue.tick(.24);
        REQUIRE(waiting.ok());
        REQUIRE(waiting.value() == PcgTaskQueueStatus::Waiting);
        auto ready = queue.tick(.01);
        REQUIRE(ready.ok());
        REQUIRE(ready.value() == PcgTaskQueueStatus::Ready);
        REQUIRE(queue.readyTaskId() == first.value());
        REQUIRE(queue.resolveReady(true).ok());
        REQUIRE(queue.queueSize() == 0);

        PcgTaskQueue roundRobin;
        auto         a      = roundRobin.add(0);
        auto         second = roundRobin.add(0);
        REQUIRE(a.ok());
        REQUIRE(second.ok());
        REQUIRE(roundRobin.tick(0).ok());
        REQUIRE(roundRobin.readyTaskId() == a.value());
        REQUIRE(roundRobin.resolveReady(false).ok());
        REQUIRE(roundRobin.tick(0).ok());
        REQUIRE(roundRobin.readyTaskId() == second.value());
        REQUIRE(roundRobin.resolveReady(true).ok());
        REQUIRE(roundRobin.tick(0).ok());
        REQUIRE(roundRobin.readyTaskId() == a.value());
        REQUIRE(!roundRobin.add(-1).ok());
        REQUIRE(!roundRobin.tick(-1).ok());
        REQUIRE(roundRobin.cancelAll() == PcgTaskQueueStatus::Idle);
    };
}
