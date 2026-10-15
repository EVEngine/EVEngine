#include "zeroerr/assert.h"

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

TEST_CASE("procgen.pcgBiomeController.matchesPcgLoadingBoundsAndTiers") {
    PcgBiomeController controller;
    REQUIRE(controller.configure(100.0, 20.0, -40.0, 50.0, 200.0, PcgBiomeLoadMode::RuntimeAlways).ok());
    CHECK_EQ(controller.regularBounds().sizeX, 99.5);
    CHECK_EQ(controller.impostorBounds().sizeX, 299.5);
    CHECK_EQ(controller.tierAt(149.75, 20.0, -40.0), PcgTerrainStreamingTier::Regular);
    CHECK_EQ(controller.tierAt(200.0, 20.0, -40.0), PcgTerrainStreamingTier::Impostor);
    CHECK_EQ(controller.tierAt(250.0, 20.0, -40.0), PcgTerrainStreamingTier::Unloaded);

    REQUIRE(controller.configure(1.0, 2.0, 3.0, 5.0, 0.0, PcgBiomeLoadMode::EditorAlways).ok());
    CHECK_EQ(controller.impostorBounds().sizeX, 0.0);
    CHECK_EQ(controller.tierAt(7.0, 2.0, 3.0), PcgTerrainStreamingTier::Unloaded);
}

TEST_CASE("procgen.pcgBiomeController.fitsTerrainWithDistinctYRulesAndIsAtomic") {
    PcgBiomeController controller;
    REQUIRE(controller.configure(0, 0, 0, 10, 20, PcgBiomeLoadMode::RuntimeAlways).ok());
    REQUIRE(controller.fitToTerrain(-100, 7, -50, 200, 80, 100).ok());
    CHECK_EQ(controller.regularBounds().centerX, 0.0);
    CHECK_EQ(controller.regularBounds().centerY, 7.0);
    CHECK_EQ(controller.regularBounds().centerZ, 0.0);
    CHECK_EQ(controller.range(), 100.0);

    REQUIRE(controller.fitToAllTerrains(-100, -10, -50, 300, 80, 100).ok());
    CHECK_EQ(controller.regularBounds().centerX, 50.0);
    CHECK_EQ(controller.regularBounds().centerY, 30.0);
    CHECK_EQ(controller.range(), 150.0);
    const auto before = controller.regularBounds();
    CHECK(!controller.fitToTerrain(0, 0, 0, -1, 1, 1).ok());
    CHECK_EQ(controller.regularBounds().centerX, before.centerX);
    CHECK(!controller.configure(0, 0, 0, std::numeric_limits<double>::infinity(), 0, PcgBiomeLoadMode::Disabled).ok());
}


// pcg_frame_rate_manager.cpp
using namespace eve::procgen;
TEST_CASE("procgen.pcgFrameRateManager.matchesSamplingAndQualityPolicy") {
    PcgFrameRateManager low;
    REQUIRE(low.configure(60, .5f, 0, 5, 2).ok());
    for (int i = 0; i < 6; ++i) REQUIRE(low.update(.1f, 1).ok());
    CHECK_EQ(low.getQuality(), 1);
    CHECK(low.getQualityChanged());
    CHECK_EQ(low.getPreset().treeDistance, 500.f);
    PcgFrameRateManager high;
    REQUIRE(high.configure(60, .5f, 0, 5, 2).ok());
    for (int i = 0; i < 51; ++i) REQUIRE(high.update(.01f, 1).ok());
    CHECK_EQ(high.getQuality(), 3);
    CHECK_GE(high.getFps(), 99.f);
    CHECK_EQ(high.getPreset().detailObjectDensity, .7f);
}
TEST_CASE("procgen.pcgFrameRateManager.manualAndFailureAreAtomic") {
    PcgFrameRateManager m;
    REQUIRE(m.configure(60, 10, 1, 4, 2).ok());
    REQUIRE(m.selectManualQuality(4).ok());
    CHECK(!m.getAutomatic());
    CHECK_EQ(m.getQuality(), 4);
    CHECK(!m.selectManualQuality(5).ok());
    CHECK_EQ(m.getQuality(), 4);
    CHECK(!m.update(std::numeric_limits<float>::quiet_NaN(), 1).ok());
    CHECK_EQ(m.getQuality(), 4);
}


// pcg_runtime_stamper.cpp
using namespace eve::procgen;

TEST_CASE("Pcg runtime stamper reproduces the sample orchestration atomically") {
    PcgRuntimeStamper stamper;
    REQUIRE(stamper.configure("Pcg\\Stamps\\Rugged", true, true).ok());
    CHECK(stamper.stampAddress() == "Pcg/Stamps/Rugged");
    CHECK(stamper.status() == PcgRuntimeStampStatus::AwaitingResource);

    Heightmap stamp(3, 3);
    std::fill(stamp.data().begin(), stamp.data().end(), 1.0F);
    REQUIRE(stamper.loadStamp(stamp, "Rugged").ok());
    Heightmap terrain(5, 5);
    std::fill(terrain.data().begin(), terrain.data().end(), 20.0F);
    auto result = stamper.execute(terrain);
    REQUIRE(result.ok());
    CHECK(terrain.height(2, 2) == 6.0F);
    CHECK(terrain.height(0, 0) == 0.0F);
    CHECK(stamper.status() == PcgRuntimeStampStatus::Completed);
    CHECK(stamper.progressText() == "Stamp progress: 1");
    CHECK(std::abs(stamper.updateTimeAllowed() - 1.0 / 15.0) < 1e-12);
}

TEST_CASE("Pcg runtime stamper exposes load failure and bottom-centred layout") {
    PcgRuntimeStamper stamper;
    REQUIRE(stamper.configure("missing\\stamp", false, true).ok());
    CHECK(!stamper.reportMissingStamp().ok());
    CHECK(stamper.progressText() == "Failed to load stamp at missing/stamp");
    CHECK(stamper.status() == PcgRuntimeStampStatus::Failed);
    REQUIRE(stamper.updateLayout(1920, 1080).ok());
    CHECK(stamper.labelCenterX() == 960.0);
    CHECK(stamper.labelCenterY() == 1060.0);
}

TEST_CASE("Pcg runtime stamper rejects invalid input without changing terrain") {
    PcgRuntimeStamper stamper;
    Heightmap         terrain(2, 2);
    std::fill(terrain.data().begin(), terrain.data().end(), 3.0F);
    const auto before = terrain.data();
    CHECK(!stamper.execute(terrain).ok());
    CHECK(terrain.data() == before);
    CHECK(!stamper.configure("", true, true).ok());
}

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
                                                              std::vector<std::array<float, 16>> binds,
                                                              std::vector<int> joints, std::vector<float> weights) {
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

TEST_CASE("procgen.animation.pcgSkinnedMesh.remapsBoneIdentityAndBindposeExactly") {
    auto firstMesh = triangle("shared", 0.F), secondMesh = triangle("shared", 2.F);
    auto bindA = identityBind(), bindB = identityBind();
    bindB[12]       = 3.F;
    auto firstSkin  = skinFor(firstMesh, {10, 10}, {bindA, bindB}, {0, -1, -1, -1, 1, -1, -1, -1, -1, -1, -1, -1},
                              {1, 0, 0, 0, .75F, 0, 0, 0, 0, 0, 0, 0});
    auto secondSkin = skinFor(secondMesh, {10, 20}, {bindA, identityBind()},
                              {0, -1, -1, -1, 1, -1, -1, -1, -1, -1, -1, -1}, {.5F, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0});
    procgen::PcgMeshTransform             transform;
    procgen_animation::PcgSkinnedMeshPlan plan;
    REQUIRE(plan.appendSource(firstMesh, *firstSkin, transform, "shared").ok());
    REQUIRE(plan.appendSource(secondMesh, *secondSkin, transform, "shared").ok());
    procgen_animation::PcgSkinnedMeshResult output;
    REQUIRE(combinePcgSkinnedMeshesInto(output, plan).ok());
    REQUIRE(output.valid());
    REQUIRE(output.mesh());
    REQUIRE(output.skin());
    CHECK_EQ(output.mesh()->getVertexCount(), 6);
    CHECK_EQ(output.mesh()->getIndexCount(), 6);
    CHECK_EQ(output.skin()->getVertexCount(), 6);
    CHECK_EQ(output.skin()->getBoneCount(), 4);
    CHECK_EQ(output.skin()->getVertexSkinJoint(0, 0), 0);
    CHECK_EQ(output.skin()->getVertexSkinJoint(1, 0), 2);
    CHECK_EQ(output.skin()->getVertexSkinJoint(3, 0), 0);
    CHECK_EQ(output.skin()->getVertexSkinJoint(4, 0), 3);
    CHECK_EQ(output.skin()->getVertexWeight(1, 0), .75F);
    CHECK_EQ(output.skin()->getInverseBindElement(2, 12), 3.F);
}

TEST_CASE("procgen.animation.pcgSkinnedMesh.validationAndPublishAreAtomic") {
    animation::AnimSkinStreamData invalid;
    invalid.vertexCount = 1;
    CHECK(!animation::AnimSkin::fromStreams(std::move(invalid)).ok());
    auto mesh = triangle("kept", 0.F);
    auto bind = identityBind();
    auto skin =
        skinFor(mesh, {1}, {bind}, {0, -1, -1, -1, 0, -1, -1, -1, 0, -1, -1, -1}, {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0});
    procgen_animation::PcgSkinnedMeshPlan validPlan;
    procgen::PcgMeshTransform             transform;
    REQUIRE(validPlan.appendSource(mesh, *skin, transform, "kept").ok());
    procgen_animation::PcgSkinnedMeshResult output;
    REQUIRE(combinePcgSkinnedMeshesInto(output, validPlan).ok());
    auto*                                 beforeSkin     = output.skin();
    const int                             beforeVertices = output.mesh()->getVertexCount();
    procgen_animation::PcgSkinnedMeshPlan empty;
    CHECK(!combinePcgSkinnedMeshesInto(output, empty).ok());
    CHECK_EQ(output.skin(), beforeSkin);
    CHECK_EQ(output.mesh()->getVertexCount(), beforeVertices);
}


// pcg_spawn_progress.cpp
using namespace eve::procgen;
TEST_CASE("Pcg spawn progress combines completed rules and current fraction") {
    PcgSpawnProgress progress;
    auto             started = progress.updateRule("Forest", 10, 3, 4, 1);
    REQUIRE(started.ok());
    CHECK(progress.title() == "Spawning");
    CHECK(progress.subtitle() == "Forest 1 of 4 Rules");
    REQUIRE(progress.updateRuleFraction(0.5).ok());
    CHECK(std::abs(progress.progress() - 0.35) < 1e-12);
}
TEST_CASE("Pcg spawn progress validates atomically and exposes cancellation") {
    PcgSpawnProgress progress;
    REQUIRE(progress.updateRule("Biome", 2, 0, 2, 0).ok());
    CHECK(!progress.updateRuleFraction(2).ok());
    CHECK(progress.progress() == 0);
    auto cancelled = progress.requestCancel();
    REQUIRE(cancelled.ok());
    CHECK(progress.status() == PcgSpawnProgressStatus::CancelRequested);
    CHECK(progress.clear() == PcgSpawnProgressStatus::Hidden);
    CHECK(progress.title().empty());
}
TEST_CASE("Pcg spawn progress completes on the final rule") {
    PcgSpawnProgress progress;
    auto             result = progress.updateRule("World", 5, 5, 1, 1);
    REQUIRE(result.ok());
    CHECK(progress.status() == PcgSpawnProgressStatus::Completed);
    CHECK(progress.progress() == 1);
    CHECK(!progress.requestCancel().ok());
}

// pcg_task_queue.cpp
using namespace eve::procgen;
TEST_CASE("Pcg task queue waits then publishes a task without invoking callbacks") {
    PcgTaskQueue q;
    auto         id = q.add();
    REQUIRE(id.ok());
    auto state = q.tick(0.24);
    REQUIRE(state.ok());
    CHECK(state.value() == PcgTaskQueueStatus::Waiting);
    state = q.tick(0.01);
    REQUIRE(state.ok());
    CHECK(state.value() == PcgTaskQueueStatus::Ready);
    CHECK(q.readyTaskId() == id.value());
    REQUIRE(q.resolveReady(true).ok());
    CHECK(q.queueSize() == 0);
}
TEST_CASE("Pcg task queue repeats unfinished tasks in queue order") {
    PcgTaskQueue q;
    auto         a = q.add(0);
    auto         b = q.add(0);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(q.tick(0).ok());
    CHECK(q.readyTaskId() == a.value());
    REQUIRE(q.resolveReady(false).ok());
    REQUIRE(q.tick(0).ok());
    CHECK(q.readyTaskId() == b.value());
    REQUIRE(q.resolveReady(true).ok());
    REQUIRE(q.tick(0).ok());
    CHECK(q.readyTaskId() == a.value());
}
TEST_CASE("Pcg task queue validates time and cancels atomically") {
    PcgTaskQueue q;
    CHECK(!q.add(-1).ok());
    REQUIRE(q.add(1).ok());
    CHECK(!q.tick(-1).ok());
    CHECK(q.queueSize() == 1);
    CHECK(q.cancelAll() == PcgTaskQueueStatus::Idle);
    CHECK(q.queueSize() == 0);
}
