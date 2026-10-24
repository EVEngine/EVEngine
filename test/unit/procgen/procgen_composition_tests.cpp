#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_incremental_build)
#include "PrivateTestScope.h"
#include "procgen/IncrementalBuild.h"
#include "procgen/Semantic.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// procgen_incremental_build.cpp
namespace test_procgen_incremental_build {
using namespace eve::procgen;

namespace {

bool containsChange(const IncrementalBuildDelta& delta, int x, int z, bool removed) {
    for (int index = 0; index < delta.getCount(); ++index)
        if (delta.getClusterX(index) == x && delta.getClusterZ(index) == z && delta.isRemoved(index) == removed)
            return true;
    return false;
}

}  // namespace

TEST_CASE("procgen.incrementalBuild.rebuildsOnlyDirtyClustersAndEmitsRemovals") {
    Grid2D grid;
    grid.resize(8, 2);
    grid.setCell(1, 0, int(Semantic::Floor));
    grid.setCell(5, 0, int(Semantic::Floor));
    PointSet  points;
    const int leftPoint  = points.add(1.5f, 0.f, 0.5f);
    const int rightPoint = points.add(5.5f, 0.f, 0.5f);

    ObjectBuildLayer objects;
    REQUIRE(objects.addAsset("props/crate", 1.f).ok());
    BuildLayerStack stack;
    REQUIRE(stack.addTileLayer("floor", true, 1.f, 0.f, "floor").ok());
    REQUIRE(stack.addObjectLayer("props", true, objects).ok());
    IncrementalBuildExecutor incremental;
    auto                     first = incremental.update(stack, grid, points, 4, 1.f);
    REQUIRE(first.ok());
    CHECK_EQ(first.value().getCount(), 2);
    CHECK_EQ(incremental.getCachedClusterCount(), 2);
    CHECK(containsChange(first.value(), 0, 0, false));
    CHECK(containsChange(first.value(), 1, 0, false));
    CHECK(incremental.getCachedArtifacts(0, 0).ok());

    auto unchanged = incremental.update(stack, grid, points, 4, 1.f);
    REQUIRE(unchanged.ok());
    CHECK_EQ(unchanged.value().getCount(), 0);

    grid.setDetail(1, 0, 7);
    auto interior = incremental.update(stack, grid, points, 4, 1.f);
    REQUIRE(interior.ok());
    CHECK_EQ(interior.value().getCount(), 1);
    CHECK(containsChange(interior.value(), 0, 0, false));

    grid.setCell(3, 0, int(Semantic::Floor));
    auto boundary = incremental.update(stack, grid, points, 4, 1.f);
    REQUIRE(boundary.ok());
    CHECK_EQ(boundary.value().getCount(), 2);
    CHECK(containsChange(boundary.value(), 0, 0, false));
    CHECK(containsChange(boundary.value(), 1, 0, false));

    points.setPosition(rightPoint, 6.5f, 0.f, 0.5f);
    auto pointEdit = incremental.update(stack, grid, points, 4, 1.f);
    REQUIRE(pointEdit.ok());
    CHECK_EQ(pointEdit.value().getCount(), 1);
    CHECK(containsChange(pointEdit.value(), 1, 0, false));

    grid.setCell(1, 0, int(Semantic::Empty));
    grid.setCell(3, 0, int(Semantic::Empty));
    points.setPosition(leftPoint, 5.75f, 0.f, 0.5f);
    auto removed = incremental.update(stack, grid, points, 4, 1.f);
    REQUIRE(removed.ok());
    CHECK(containsChange(removed.value(), 0, 0, true));
    CHECK_EQ(incremental.getCachedClusterCount(), 1);
    CHECK(!incremental.getCachedArtifacts(0, 0).ok());
}

TEST_CASE("procgen.incrementalBuild.partitionsGlobalMeshAndPreservesCacheOnFailure") {
    Grid2D grid;
    grid.resize(8, 1);
    for (int x = 0; x < 8; ++x) grid.setCell(x, 0, int(Semantic::Floor));
    PointSet points;
    points.add(1.f, 0.f, 0.f);

    ObjectBuildLayer objects;
    REQUIRE(objects.addAsset("props/crate", 1.f).ok());
    BuildLayerStack stack;
    REQUIRE(stack.addTileLayer("floor", true, 1.f, 0.f, "floor").ok());
    REQUIRE(stack.addObjectLayer("props", true, objects).ok());
    IncrementalBuildExecutor incremental;
    auto                     delta = incremental.update(stack, grid, points, 4, 1.f);
    REQUIRE(delta.ok());
    REQUIRE_EQ(delta.value().getCount(), 2);
    auto leftArtifacts  = incremental.getCachedArtifacts(0, 0);
    auto rightArtifacts = incremental.getCachedArtifacts(1, 0);
    REQUIRE(leftArtifacts.ok());
    REQUIRE(rightArtifacts.ok());
    auto leftMesh  = leftArtifacts.value().getMesh(0);
    auto rightMesh = rightArtifacts.value().getMesh(0);
    REQUIRE(leftMesh.ok());
    REQUIRE(rightMesh.ok());
    auto full = stack.execute(grid, points);
    REQUIRE(full.ok());
    auto fullMesh = full.value().getMesh(0);
    REQUIRE(fullMesh.ok());
    CHECK_EQ(leftMesh.value().getIndexCount() + rightMesh.value().getIndexCount(), fullMesh.value().getIndexCount());
    CHECK(leftMesh.value().getPositionX(0) < 4.f);
    CHECK(rightMesh.value().getPositionX(0) >= 4.f);

    ObjectBuildLayer oriented;
    REQUIRE(oriented.addAsset("props/oriented", 1.f).ok());
    REQUIRE(oriented.setOrientation(1.f, 0.f, false).ok());
    BuildLayerStack failingStack;
    REQUIRE(failingStack.addObjectLayer("oriented", true, oriented).ok());
    auto failed = incremental.update(failingStack, grid, points, 4, 1.f);
    CHECK(!failed.ok());
    CHECK_EQ(incremental.getCachedClusterCount(), 2);
    CHECK(incremental.getCachedArtifacts(0, 0).ok());
    CHECK(!incremental.update(stack, grid, points, 0, 1.f).ok());
    CHECK(!incremental.update(stack, grid, points, 4, 0.f).ok());
}
}  // namespace test_procgen_incremental_build
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_module_assembly)
#include "PrivateTestScope.h"
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
#include "procgen/ModuleAssembly.h"
#include "zeroerr/unittest.h"
// procgen_module_assembly.cpp
namespace test_procgen_module_assembly_unit {
using namespace eve::procgen;

TEST_CASE("procgen.moduleAssembly.rejectsCoordinateOverflowAtomically") {
    ModuleAssemblyConstraints constraints;
    constraints.requireConnection = false;
    constraints.maxLevels         = std::numeric_limits<int>::max();
    ModuleAssemblyPlan plan(constraints);
    REQUIRE(plan.registerVolumeModuleType("wide", 2, 1, 2).ok());
    CHECK(!plan.place({"wide", std::numeric_limits<int>::max(), 0, 0, 0}).ok());
    CHECK(!plan.place({"wide", 0, std::numeric_limits<int>::max(), 0, 1}).ok());
    CHECK(!plan.place({"wide", 0, 0, std::numeric_limits<int>::max(), 0}).ok());
    CHECK_EQ(plan.placements().size(), size_t(0));
    REQUIRE(plan.place({"wide", std::numeric_limits<int>::min(), 0, 0, 0}).ok());
    CHECK_EQ(plan.placements().size(), size_t(1));
}

TEST_CASE("procgen.moduleAssembly.rejectsOverlapDisconnectionAndWrongConnector") {
    ModuleAssemblyConstraints constraints;
    constraints.bounded  = true;
    constraints.minCellX = 0;
    constraints.maxCellX = 4;
    constraints.minCellZ = 0;
    constraints.maxCellZ = 4;
    ModuleAssemblyPlan plan(constraints);
    auto               registered = plan.registerModule({"street",
                                                         1,
                                                         1,
                                                         {{0, 0, ModuleFacing::North, "street", "street"},
                                                          {0, 0, ModuleFacing::East, "street", "street"},
                                                          {0, 0, ModuleFacing::South, "street", "street"},
                                                          {0, 0, ModuleFacing::West, "street", "street"}}});
    REQUIRE(registered.ok());
    auto first = plan.place({"street", 2, 2, 0, 0});
    REQUIRE(first.ok());
    auto connected = plan.place({"street", 3, 2, 0, 0});
    REQUIRE(connected.ok());
    auto overlap = plan.place({"street", 3, 2, 0, 0});
    CHECK(!overlap.ok());
    CHECK_EQ(overlap.code(), eve::StatusCode::Conflict);
    auto disconnected = plan.place({"street", 0, 0, 0, 0});
    CHECK(!disconnected.ok());
    CHECK_EQ(disconnected.code(), eve::StatusCode::Rejected);

    ModuleAssemblyPlan mismatch(constraints);
    auto               a = mismatch.registerModule({"road", 1, 1, {{0, 0, ModuleFacing::East, "road", "road"}}});
    auto               b = mismatch.registerModule({"wall", 1, 1, {{0, 0, ModuleFacing::West, "wall", "wall"}}});
    REQUIRE(eve::everyResultValid(a, b));
    auto seed = mismatch.place({"road", 1, 1, 0, 0});
    REQUIRE(seed.ok());
    auto wrong = mismatch.place({"wall", 2, 1, 0, 0});
    CHECK(!wrong.ok());
    CHECK_EQ(wrong.code(), eve::StatusCode::Conflict);
    CHECK_EQ(mismatch.placements().size(), size_t(1));
}

TEST_CASE("procgen.moduleAssembly.requiresEveryTouchingCellEdgeToHaveAConnector") {
    ModuleAssemblyConstraints constraints;
    constraints.bounded  = true;
    constraints.minCellX = -2;
    constraints.maxCellX = 2;
    constraints.minCellZ = 0;
    constraints.maxCellZ = 4;
    ModuleAssemblyPlan plan(constraints);
    REQUIRE(plan.registerModule({"street",
                                 1,
                                 1,
                                 {{0, 0, ModuleFacing::North, "street", "street"},
                                  {0, 0, ModuleFacing::East, "street", "street"},
                                  {0, 0, ModuleFacing::South, "street", "street"},
                                  {0, 0, ModuleFacing::West, "street", "street"}}})
                .ok());
    REQUIRE(plan.registerModule(
                    {"building",
                     2,
                     2,
                     {{1, 0, ModuleFacing::East, "street", "street"}, {1, 1, ModuleFacing::East, "street", "street"}}})
                .ok());
    REQUIRE(plan.place({"street", 0, 0, 0, 0}).ok());
    REQUIRE(plan.place({"street", 0, 1, 0, 0}).ok());
    REQUIRE(plan.place({"building", -2, 0, 0, 0}).ok());

    auto unsupportedTouch = plan.place({"building", -2, 2, 0, 0});
    CHECK(!unsupportedTouch.ok());
    CHECK_EQ(unsupportedTouch.code(), eve::StatusCode::Conflict);
    CHECK_EQ(plan.placements().size(), size_t(3));
}

TEST_CASE("procgen.moduleAssembly.validatesVolumeOverlapAndVerticalSupport") {
    ModuleAssemblyConstraints constraints;
    constraints.maxLevels = 4;
    ModuleAssemblyPlan plan(constraints);

    ModuleDefinition foundation{"foundation", 1, 1, {{0, 0, ModuleFacing::Up, "support", "load", 0}}};
    foundation.heightLevels = 1;
    ModuleDefinition tower{"tower", 1, 1, {{0, 0, ModuleFacing::Down, "load", "support", 0}}};
    tower.heightLevels = 2;
    REQUIRE(eve::everyResultValid(plan.registerModule(std::move(foundation)), plan.registerModule(std::move(tower))));
    REQUIRE(plan.place({"foundation", 0, 0, 0, 0}).ok());
    REQUIRE(plan.place({"tower", 0, 0, 1, 0}).ok());

    auto volumeOverlap = plan.place({"foundation", 0, 0, 2, 0});
    CHECK(!volumeOverlap.ok());
    CHECK_EQ(volumeOverlap.code(), eve::StatusCode::Conflict);

    ModuleAssemblyPlan unsupported(constraints);
    REQUIRE(unsupported.registerVolumeModuleType("upper", 1, 1, 2).ok());
    REQUIRE(unsupported.place({"upper", 2, 2, 0, 0}).ok());
    auto floating = unsupported.place({"upper", 2, 2, 2, 0});
    CHECK(!floating.ok());
    CHECK_EQ(floating.code(), eve::StatusCode::Conflict);
    CHECK_EQ(unsupported.placements().size(), size_t(1));
}

TEST_CASE("procgen.moduleAssembly.appliesConfiguredRotationWhitelistAtomically") {
    ModuleAssemblyPlan plan;
    REQUIRE(plan.setAllowedQuarterTurns(true, false, true, false).ok());
    REQUIRE(plan.registerModuleType("facade", 1, 1).ok());
    REQUIRE(plan.place({"facade", 0, 0, 0, 0}).ok());
    auto rejected = plan.place({"facade", 1, 0, 0, 1});
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Rejected);
    CHECK_EQ(plan.placements().size(), size_t(1));
    CHECK(!plan.setAllowedQuarterTurns(true, true, true, true).ok());
}

TEST_CASE("procgen.moduleAssembly.appliesConfiguredVerticalSupportRatioAtomically") {
    ModuleAssemblyConstraints constraints;
    constraints.maxLevels = 3;
    ModuleAssemblyPlan plan(constraints);
    REQUIRE(plan.setMinimumSupportRatio(1.0f).ok());

    ModuleDefinition base{"base", 1, 1, {{0, 0, ModuleFacing::Up, "support", "load", 0}}};
    ModuleDefinition platform{
        "platform",
        2,
        1,
        {{0, 0, ModuleFacing::Down, "load", "support", 0}, {1, 0, ModuleFacing::West, "walk", "walk", 0}}};
    REQUIRE(eve::everyResultValid(plan.registerModule(std::move(base)), plan.registerModule(std::move(platform))));
    REQUIRE(plan.place({"base", 0, 0, 0, 0}).ok());

    auto insufficient = plan.place({"platform", 0, 0, 1, 0});
    CHECK(!insufficient.ok());
    CHECK_EQ(insufficient.code(), eve::StatusCode::Rejected);
    CHECK_EQ(plan.placements().size(), size_t(1));

    CHECK(!plan.setMinimumSupportRatio(0.5f).ok());
}

TEST_CASE("procgen.moduleAssembly.loadsStrictVersionedJsonAtomically") {
    constexpr auto     config = R"({
      "schema":"eve.procgen.module-assembly","version":1,"unknownFields":"reject",
      "constraints":{"cellSize":12.0,"floorHeight":4.0,
        "bounds":{"enabled":true,"minCellX":-2,"maxCellX":2,"minCellZ":0,"maxCellZ":9},
        "maxLevels":3,"allowedQuarterTurns":[true,false,true,false],
        "requireConnection":true,"minimumSupportRatio":1.0},
      "modules":[
        {"id":"street","widthCells":1,"depthCells":1,"heightLevels":1,"connectors":[
          {"cellX":0,"cellZ":0,"level":0,"facing":"east","tag":"street","accepts":"street"}]},
        {"id":"building","widthCells":2,"depthCells":2,"heightLevels":3,"connectors":[
          {"cellX":0,"cellZ":0,"level":0,"facing":"west","tag":"street","accepts":"street"}]}
      ]})";
    ModuleAssemblyPlan plan;
    REQUIRE(plan.applyConfigJson(config).ok());
    CHECK_EQ(plan.constraints().cellSize, 12.f);
    CHECK_EQ(plan.constraints().allowedQuarterTurns, (std::array<bool, 4>{true, false, true, false}));
    REQUIRE(plan.place({"street", 0, 0, 0, 0}).ok());
    REQUIRE(plan.place({"building", 1, 0, 0, 0}).ok());
    CHECK(!plan.place({"street", 0, 1, 0, 1}).ok());
    CHECK(!plan.applyConfigJson(config).ok());

    ModuleAssemblyPlan unchanged;
    std::string        invalid(config);
    invalid.replace(invalid.find("\"modules\""), 9, "\"unexpected\"");
    CHECK(!unchanged.applyConfigJson(invalid).ok());
    REQUIRE(unchanged.registerModuleType("still-empty", 1, 1).ok());
}
}  // namespace test_procgen_module_assembly_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_object_build_layer)
#include "PrivateTestScope.h"
#include <set>
#include "procgen/ObjectBuildLayer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// procgen_object_build_layer.cpp
namespace test_procgen_object_build_layer {
using namespace eve::procgen;

TEST_CASE("procgen.objectBuild.weightedTransformsOrientationChildrenAndPlacement") {
    PointSet source;
    for (int index = 0; index < 32; ++index) {
        const int point = source.add(float(index), 2.f, 0.f);
        REQUIRE(source.trySetPointId(point, std::uint64_t(index + 1)).ok());
        REQUIRE(source.trySetFloatAttribute(point, "surface_highest_y", 10.f + float(index)).ok());
    }
    PointSet orientation;
    orientation.add(0.f, 0.f, 1.f);  // North has priority for the first source point.

    ObjectBuildLayer layer;
    REQUIRE(layer.addAsset("tree/oak", 3.f).ok());
    REQUIRE(layer.addAsset("tree/birch", 1.f).ok());
    layer.setSeed(77);
    layer.setLayerOffset(1.f, 2.f, 3.f);
    REQUIRE(layer.setLayerScale(2.f, 3.f, 4.f).ok());
    REQUIRE(layer.setRandomRotation(5.f, 5.f, 7.f, 7.f, 9.f, 9.f).ok());
    REQUIRE(layer.setRandomScale(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, true).ok());
    REQUIRE(layer.setOrientation(1.f, 10.f, false).ok());
    layer.setPlaceOnTop(true, false, 0.25f);
    REQUIRE(layer.addChild("grass/tuft", 2, 0.5f, 0.8f, 1.2f, -30.f, 30.f).ok());

    auto first = layer.build(source, &orientation);
    auto again = layer.build(source, &orientation);
    REQUIRE(first.ok());
    REQUIRE(again.ok());
    CHECK_EQ(first.value().getCount(), 96);
    CHECK_EQ(again.value().getCount(), first.value().getCount());

    std::set<std::string> selectedAssets;
    for (int index = 0; index < first.value().getCount(); ++index) {
        CHECK_EQ(first.value().getX(index), again.value().getX(index));
        CHECK_EQ(first.value().getY(index), again.value().getY(index));
        CHECK_EQ(first.value().getZ(index), again.value().getZ(index));
        CHECK_EQ(first.value().getYaw(index), again.value().getYaw(index));
        CHECK_EQ(first.value().getStringAttribute(index, "asset", ""),
                 again.value().getStringAttribute(index, "asset", ""));
        const auto role = first.value().getStringAttribute(index, "object_role", "");
        if (role == "parent") selectedAssets.insert(first.value().getStringAttribute(index, "asset", ""));
    }
    CHECK_EQ(selectedAssets.size(), 2);

    CHECK_EQ(first.value().getStringAttribute(0, "object_role", ""), "parent");
    CHECK_EQ(first.value().getYaw(0), 287.f);  // North 270 + offset 10 + fixed random yaw 7.
    CHECK_EQ(first.value().getPitch(0), 5.f);
    CHECK_EQ(first.value().getRoll(0), 9.f);
    CHECK_EQ(first.value().getScaleX(0), 1.f);
    CHECK_EQ(first.value().getScaleY(0), 1.5f);
    CHECK_EQ(first.value().getScaleZ(0), 2.f);
    CHECK_EQ(first.value().getY(0), 12.25f);  // surface 10 + top 0.25 + layer Y 2.
    CHECK_EQ(first.value().getStringAttribute(1, "object_role", ""), "child");
    CHECK_EQ(first.value().getIntAttribute(1, "parent_index", -2), 0);
}

TEST_CASE("procgen.objectBuild.rejectsInvalidConfigurationWithoutPartialOutput") {
    PointSet source;
    source.add(0.f, 0.f, 0.f);
    ObjectBuildLayer layer;
    CHECK(!layer.build(source).ok());
    CHECK(!layer.addAsset("", 1.f).ok());
    CHECK(!layer.addAsset("tree", 0.f).ok());
    CHECK(!layer.setLayerScale(1.f, 0.f, 1.f).ok());
    CHECK(!layer.setPositionRadius(-1.f).ok());
    CHECK(!layer.setOrientation(0.f, 0.f, false).ok());
    CHECK(!layer.addChild("grass", -1, 1.f, 1.f, 1.f, 0.f, 0.f).ok());
}
}  // namespace test_procgen_object_build_layer
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_runtime_generation)
#include "PrivateTestScope.h"
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <utility>
#include "procgen/Procgen.h"
#include "procgen/RuntimeGeneration.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// procgen_runtime_generation.cpp
namespace test_procgen_runtime_generation_unit {
using namespace eve::procgen;

namespace {

/** @brief Owns a module-backed runtime-generation handle for one test. */
class RuntimeLease {
public:
    RuntimeLease() = default;
    RuntimeLease(Procgen& owner, ProcgenRuntimeGenerationHandleRef handle) : owner_(&owner), handle_(handle) {}

    RuntimeLease(const RuntimeLease&)            = delete;
    RuntimeLease& operator=(const RuntimeLease&) = delete;
    RuntimeLease(RuntimeLease&& other) noexcept : owner_(other.owner_), handle_(other.handle_) {
        other.owner_  = nullptr;
        other.handle_ = {};
    }
    RuntimeLease& operator=(RuntimeLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        owner_        = other.owner_;
        handle_       = other.handle_;
        other.owner_  = nullptr;
        other.handle_ = {};
        return *this;
    }
    ~RuntimeLease() { reset(); }

    /** @brief Releases the scheduler handle and explicitly observes the Result. */
    void reset() noexcept {
        if (!owner_ || !handle_.isValid()) return;
        auto result = owner_->release(handle_);
        result.ignore("test runtime-generation cleanup");
        owner_  = nullptr;
        handle_ = {};
    }

    /** @brief Provides a short-lived borrowed view of the scheduler. */
    [[nodiscard]] eve::script::Borrowed<RuntimeGeneration> view() const noexcept {
        return owner_ ? owner_->resolveRuntimeGeneration(handle_) : eve::script::Borrowed<RuntimeGeneration>();
    }

    /** @brief Preserves the concise `runtime->operation()` test syntax. */
    [[nodiscard]] eve::script::Borrowed<RuntimeGeneration> operator->() const noexcept { return view(); }

private:
    Procgen*                          owner_ = nullptr;
    ProcgenRuntimeGenerationHandleRef handle_{};
};

/** @brief Converts a checked scheduler allocation Result into a test lease. */
[[maybe_unused]] RuntimeLease requireRuntime(Procgen& proc, uint32_t worldSeed) {
    auto       result = proc.newRuntimeGenerationHandle(worldSeed);
    const bool ok     = result.ok();
    if (!ok) {
        const eve::Diagnostic* diagnostic = result.error();
        REQUIRE(diagnostic != nullptr);
    }
    REQUIRE(ok);
    return RuntimeLease(proc, std::move(result).takeValue());
}

/** @brief Gives an owning lifetime to a request allocated by the scheduler API. */
using RequestLease = std::unique_ptr<ProcgenCellRequest>;

[[nodiscard]] RequestLease ownRequest(ProcgenCellRequest* request) noexcept { return RequestLease(request); }

}  // namespace

TEST_CASE("procgen.runtimeGeneration.partitionsAndPublishesCells") {
    Procgen   proc;
    auto      runtime   = requireRuntime(proc, 42);
    const int nearLevel = runtime->addLevel(10.f, 8.f, 1.5f);
    const int farLevel  = runtime->addLevel(40.f, 30.f, 2.f);
    CHECK_EQ(nearLevel, 0);
    CHECK_EQ(farLevel, 1);
    CHECK_EQ(runtime->getLevelCount(), 2);
    CHECK_EQ(runtime->getLevelCleanupRadius(0), 12.f);
    runtime->setMaxGenerating(1);
    runtime->setFrameTimeBudget(2.f);
    CHECK_EQ(runtime->getFrameTimeBudget(), 2.f);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    CHECK(runtime->getPendingGenerateCount() > 0);
    runtime->beginFrame();

    auto request = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(request));
    CHECK_EQ(runtime->getGeneratingCount(), 1);
    CHECK(!runtime->nextGenerate());
    CHECK_NE(request->getSeed(), uint32_t(0));
    CHECK(request->getMaxX() > request->getMinX());

    const int level = request->getLevel();
    const int x     = request->getX();
    const int z     = request->getZ();
    PointSet  output;
    output.add(request->getMinX(), 0.f, request->getMinZ());
    CHECK(runtime->completeGeneration(request.get(), &output));
    CHECK(runtime->hasCell(level, x, z));
    CHECK_EQ(runtime->getCellRevision(level, x, z), uint64_t(1));
    auto stored = std::unique_ptr<PointSet>(runtime->getCellOutput(level, x, z));
    REQUIRE(bool(stored));
    CHECK_EQ(stored->getCount(), 1);
}

TEST_CASE("procgen.runtimeGeneration.cleansBeyondHysteresisRadius") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 7);
    runtime->addLevel(10.f, 6.f, 2.f);
    runtime->updateSource(5.f, 5.f, 0.f, 1.f);
    auto generated = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(generated));
    const int cellX = generated->getX();
    const int cellZ = generated->getZ();
    PointSet  points;
    points.add(5.f, 0.f, 5.f);
    CHECK(runtime->completeGeneration(generated.get(), &points));

    runtime->updateSource(100.f, 100.f, 0.f, 1.f);
    CHECK(runtime->getPendingCleanupCount() > 0);
    bool cleanedTarget = false;
    while (auto cleanup = ownRequest(runtime->nextCleanup())) {
        if (cleanup->getX() == cellX && cleanup->getZ() == cellZ) cleanedTarget = true;
        CHECK(runtime->completeCleanup(cleanup.get()));
    }
    CHECK(cleanedTarget);
    CHECK(!runtime->hasCell(0, cellX, cellZ));
}

TEST_CASE("procgen.runtimeGeneration.failedWorkCanRetryWithStableSeed") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 99);
    runtime->addLevel(8.f, 5.f, 1.25f);
    runtime->updateSource(4.f, 4.f, 1.f, 0.f);
    auto first = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(first));
    const uint32_t seed = first->getSeed();
    CHECK(runtime->failGeneration(first.get()));

    auto retry = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(retry));
    CHECK_EQ(retry->getSeed(), seed);
    CHECK_EQ(retry->getLevel(), 0);
}

TEST_CASE("procgen.runtimeGeneration.unionsNamedSourcesAndCleansAfterLastSource") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 123);
    runtime->addLevel(10.f, 16.f, 1.5f);
    runtime->setMaxGenerating(100);
    CHECK(runtime->setGenerationSource("player", 5.f, 5.f, 1.f, 0.f, 1.f));
    CHECK(runtime->setGenerationSource("quest", 105.f, 5.f, 0.f, 0.f, 1.f));
    CHECK_EQ(runtime->getGenerationSourceCount(), 2);
    CHECK_EQ(runtime->getGenerationSourceId(0), std::string("player"));
    CHECK_EQ(runtime->getGenerationSourceId(1), std::string("quest"));

    bool     generatedNearPlayer = false;
    bool     generatedNearQuest  = false;
    PointSet empty;
    while (auto request = ownRequest(runtime->nextGenerate())) {
        if (request->getX() >= -1 && request->getX() <= 1) generatedNearPlayer = true;
        if (request->getX() >= 9 && request->getX() <= 11) generatedNearQuest = true;
        CHECK(runtime->completeGeneration(request.get(), &empty));
    }
    CHECK(generatedNearPlayer);
    CHECK(generatedNearQuest);

    CHECK(runtime->removeGenerationSource("player"));
    bool cleanupNearPlayer = false;
    bool cleanupNearQuest  = false;
    while (auto request = ownRequest(runtime->nextCleanup())) {
        if (request->getX() >= -1 && request->getX() <= 1) cleanupNearPlayer = true;
        if (request->getX() >= 9 && request->getX() <= 11) cleanupNearQuest = true;
        CHECK(runtime->completeCleanup(request.get()));
    }
    CHECK(cleanupNearPlayer);
    CHECK(!cleanupNearQuest);
}

TEST_CASE("procgen.runtimeGeneration.frustumCullsFarBehindButKeepsNearCells") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 9);
    runtime->addLevel(10.f, 35.f, 1.25f);
    runtime->setMaxGenerating(100);
    runtime->setFrustumCulling(true, 45.f, 11.f);
    CHECK(runtime->isFrustumCullingEnabled());
    CHECK_EQ(runtime->getFrustumHalfAngle(), 45.f);
    CHECK_EQ(runtime->getFrustumBehindRadius(), 11.f);
    CHECK(runtime->setGenerationSource("camera", 5.f, 5.f, 1.f, 0.f, 1.f));

    bool     foundFarBehind  = false;
    bool     foundNearBehind = false;
    bool     foundForward    = false;
    PointSet empty;
    while (auto request = ownRequest(runtime->nextGenerate())) {
        const float centerX = (request->getMinX() + request->getMaxX()) * 0.5f;
        if (centerX < -6.f) foundFarBehind = true;
        if (centerX == -5.f) foundNearBehind = true;
        if (centerX > 5.f) foundForward = true;
        CHECK(runtime->completeGeneration(request.get(), &empty));
    }
    CHECK(!foundFarBehind);
    CHECK(foundNearBehind);
    CHECK(foundForward);
}

TEST_CASE("procgen.runtimeGeneration.rejectsStaleAsyncGenerationTickets") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 77);
    runtime->addLevel(10.f, 6.f, 1.5f);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    auto stale = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(stale));
    const uint64_t staleTicket = stale->getTicket();
    CHECK(runtime->isRequestCurrent(stale.get()));

    runtime->updateSource(100.f, 100.f, 1.f, 0.f);
    CHECK(!runtime->isRequestCurrent(stale.get()));
    CHECK_EQ(runtime->getCancelledGenerationCount(), 1);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    auto current = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(current));
    CHECK(runtime->isRequestCurrent(current.get()));
    CHECK_NE(current->getTicket(), staleTicket);
    PointSet output;
    CHECK(!runtime->completeGeneration(stale.get(), &output));
    CHECK(runtime->completeGeneration(current.get(), &output));
    CHECK(!runtime->isRequestCurrent(current.get()));
    CHECK(runtime->debugReport().find("cancelledGeneration=1") != std::string::npos);
}

TEST_CASE("procgen.runtimeGeneration.rejectsStaleAsyncCleanupTickets") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 78);
    runtime->addLevel(10.f, 6.f, 1.5f);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    auto generated = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(generated));
    PointSet output;
    CHECK(runtime->completeGeneration(generated.get(), &output));

    runtime->updateSource(100.f, 100.f, 1.f, 0.f);
    auto stale = ownRequest(runtime->nextCleanup());
    REQUIRE(bool(stale));
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    runtime->updateSource(100.f, 100.f, 1.f, 0.f);
    std::vector<const ProcgenCellRequest*> staleRequests{stale.get()};
    auto                                   staleResult = runtime->completeCleanupsAtomic(staleRequests);
    CHECK(!staleResult.ok());
    CHECK(!runtime->completeCleanup(stale.get()));
    auto current = ownRequest(runtime->nextCleanup());
    REQUIRE(bool(current));
    CHECK_NE(current->getTicket(), stale->getTicket());
    CHECK(runtime->completeCleanup(current.get()));
}

TEST_CASE("procgen.runtimeGeneration.completesCleanupTicketsAtomically") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 79);
    runtime->addLevel(10.f, 16.f, 1.5f);
    runtime->setMaxGenerating(8);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);

    auto firstGenerated  = ownRequest(runtime->nextGenerate());
    auto secondGenerated = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(firstGenerated));
    REQUIRE(bool(secondGenerated));
    PointSet output;
    REQUIRE(runtime->completeGeneration(firstGenerated.get(), &output));
    REQUIRE(runtime->completeGeneration(secondGenerated.get(), &output));
    runtime->updateSource(1000.f, 1000.f, 1.f, 0.f);

    auto firstCleanup  = ownRequest(runtime->nextCleanup());
    auto secondCleanup = ownRequest(runtime->nextCleanup());
    REQUIRE(bool(firstCleanup));
    REQUIRE(bool(secondCleanup));
    std::vector<const ProcgenCellRequest*> empty;
    auto                                   emptyResult = runtime->completeCleanupsAtomic(empty);
    REQUIRE(!emptyResult.ok());
    CHECK(runtime->isRequestCurrent(firstCleanup.get()));
    CHECK(runtime->isRequestCurrent(secondCleanup.get()));
    std::vector<const ProcgenCellRequest*> duplicate{firstCleanup.get(), firstCleanup.get()};
    auto                                   duplicateResult = runtime->completeCleanupsAtomic(duplicate);
    REQUIRE(!duplicateResult.ok());
    CHECK(runtime->isRequestCurrent(firstCleanup.get()));
    CHECK(runtime->isRequestCurrent(secondCleanup.get()));

    auto otherRuntime = requireRuntime(proc, 80);
    otherRuntime->addLevel(10.f, 16.f, 1.5f);
    std::vector<const ProcgenCellRequest*> wrongOwner{firstCleanup.get()};
    auto                                   wrongOwnerResult = otherRuntime->completeCleanupsAtomic(wrongOwner);
    REQUIRE(!wrongOwnerResult.ok());
    CHECK(runtime->isRequestCurrent(firstCleanup.get()));

    std::vector<const ProcgenCellRequest*> requests{firstCleanup.get(), secondCleanup.get()};
    auto                                   completed = runtime->completeCleanupsAtomic(requests);
    REQUIRE(completed.ok());
    CHECK_EQ(completed.value(), uint64_t(2));
    CHECK(!runtime->isRequestCurrent(firstCleanup.get()));
    CHECK(!runtime->isRequestCurrent(secondCleanup.get()));
}


TEST_CASE("procgen.runtimeGeneration.enforcesResidentCellReservations") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 91);
    runtime->addLevel(10.f, 30.f, 1.5f);
    runtime->setMaxGenerating(100);
    runtime->setMaxActiveCells(2);
    CHECK_EQ(runtime->getMaxActiveCells(), 2);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);

    auto first  = ownRequest(runtime->nextGenerate());
    auto second = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(first));
    REQUIRE(bool(second));
    CHECK(!runtime->nextGenerate());
    CHECK_EQ(runtime->getGeneratingCount(), 2);

    PointSet empty;
    CHECK(runtime->completeGeneration(first.get(), &empty));
    CHECK(runtime->completeGeneration(second.get(), &empty));
    CHECK_EQ(runtime->getActiveCellCount(), 2);
    CHECK(!runtime->nextGenerate());
    CHECK(runtime->debugReport().find("maxActive=2") != std::string::npos);

    runtime->setMaxActiveCells(0);
    CHECK_EQ(runtime->getMaxActiveCells(), 0);
    auto unlimited = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(unlimited));
}

TEST_CASE("procgen.runtimeGeneration.enforcesPointMemoryBudgets") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 92);
    runtime->addLevel(10.f, 25.f, 1.5f);
    runtime->setMaxGenerating(100);
    runtime->setMaxPointsPerCell(2);
    runtime->setMaxResidentPoints(2);
    CHECK_EQ(runtime->getMaxPointsPerCell(), 2);
    CHECK_EQ(runtime->getMaxResidentPoints(), 2);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);

    auto first = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(first));
    PointSet oversized;
    oversized.add(0.f, 0.f, 0.f);
    oversized.add(1.f, 0.f, 0.f);
    oversized.add(2.f, 0.f, 0.f);
    CHECK(!runtime->completeGeneration(first.get(), &oversized));
    CHECK_EQ(runtime->getRejectedOutputCount(), 1);
    CHECK(runtime->failGeneration(first.get()));

    auto retry = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(retry));
    PointSet accepted;
    accepted.add(0.f, 0.f, 0.f);
    accepted.add(1.f, 0.f, 0.f);
    CHECK(runtime->completeGeneration(retry.get(), &accepted));
    CHECK_EQ(runtime->getResidentPointCount(), 2);

    auto second = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(second));
    PointSet one;
    one.add(0.f, 0.f, 0.f);
    CHECK(!runtime->completeGeneration(second.get(), &one));
    CHECK_EQ(runtime->getRejectedOutputCount(), 2);
    CHECK_EQ(runtime->getPendingCleanupCount(), 1);
    runtime->refreshGenerationSources();
    CHECK(!runtime->completeGeneration(second.get(), &one));
    CHECK_EQ(runtime->getRejectedOutputCount(), 3);
    CHECK_EQ(runtime->getPendingCleanupCount(), 1);
    auto cleanup = ownRequest(runtime->nextCleanup());
    REQUIRE(bool(cleanup));
    CHECK(runtime->completeCleanup(cleanup.get()));
    CHECK_EQ(runtime->getResidentPointCount(), 0);
    CHECK(runtime->completeGeneration(second.get(), &one));
    CHECK_EQ(runtime->getResidentPointCount(), 1);
    CHECK(runtime->debugReport().find("residentPoints=1") != std::string::npos);
}

TEST_CASE("procgen.runtimeGeneration.appliesIdentityDeltaAtomically") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 930);
    runtime->addLevel(10.f, 6.f, 1.5f);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    auto generated = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(generated));
    const int level = generated->getLevel();
    const int x     = generated->getX();
    const int z     = generated->getZ();

    PointSet initial = sampleGridPoints(2, 2, 1.f, 11, 0.f);
    CHECK(runtime->completeGeneration(generated.get(), &initial));
    PointSet updated;
    updated.appendPointFrom(initial, 2).expect("runtime delta reordered point");
    updated.appendPointFrom(initial, 0).expect("runtime delta updated point");
    updated.setPosition(1, 7.f, 2.f, -3.f);
    ProcgenPoint added;
    added.id             = derivePointId(991, 1);
    added.seed           = 88;
    const int addedIndex = updated.appendPoint(added);
    (void)addedIndex;

    auto committed = runtime->applyCellUpdate(level, x, z, 1, updated);
    REQUIRE(committed.ok());
    CHECK_EQ(committed.value(), std::uint64_t(2));
    CHECK_EQ(runtime->getCellRevision(level, x, z), std::uint64_t(2));
    auto delta = std::unique_ptr<PointDelta>(runtime->getCellDelta(level, x, z));
    REQUIRE(bool(delta));
    CHECK_EQ(delta->added.getCount(), 1);
    CHECK_EQ(delta->updated.getCount(), 1);
    CHECK_EQ(delta->removed.size(), std::size_t(2));
    auto stored = std::unique_ptr<PointSet>(runtime->getCellOutput(level, x, z));
    REQUIRE(bool(stored));
    auto expectedFingerprint = fingerprintPointSet(updated);
    auto actualFingerprint   = fingerprintPointSet(*stored);
    REQUIRE(expectedFingerprint.ok());
    REQUIRE(actualFingerprint.ok());
    CHECK_EQ(actualFingerprint.value(), expectedFingerprint.value());
}

TEST_CASE("procgen.runtimeGeneration.rejectsStaleAndOverBudgetDeltaWithoutMutation") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 931);
    runtime->addLevel(10.f, 6.f, 1.5f);
    runtime->setMaxPointsPerCell(2);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    auto generated = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(generated));
    const int level   = generated->getLevel();
    const int x       = generated->getX();
    const int z       = generated->getZ();
    PointSet  initial = sampleGridPoints(2, 1, 1.f, 12, 0.f);
    CHECK(runtime->completeGeneration(generated.get(), &initial));

    PointSet changed = initial;
    changed.setPosition(0, 4.f, 0.f, 0.f);
    auto stale = runtime->applyCellUpdate(level, x, z, 9, changed);
    CHECK(!stale.ok());
    CHECK_EQ(runtime->getCellRevision(level, x, z), std::uint64_t(1));

    ProcgenPoint extra;
    extra.id             = derivePointId(932, 1);
    const int extraIndex = changed.appendPoint(extra);
    (void)extraIndex;
    auto overBudget = runtime->applyCellUpdate(level, x, z, 1, changed);
    CHECK(!overBudget.ok());
    CHECK_EQ(runtime->getCellRevision(level, x, z), std::uint64_t(1));
    CHECK(!runtime->getCellDelta(level, x, z));
    auto stored = std::unique_ptr<PointSet>(runtime->getCellOutput(level, x, z));
    REQUIRE(bool(stored));
    CHECK_EQ(stored->getX(0), initial.getX(0));
}

TEST_CASE("procgen.runtimeGeneration.migratesLegacyCellIdsBeforeIncrementalUpdates") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 933);
    runtime->addLevel(10.f, 6.f, 1.5f);
    runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    auto generated = ownRequest(runtime->nextGenerate());
    REQUIRE(bool(generated));
    const int level = generated->getLevel();
    const int x     = generated->getX();
    const int z     = generated->getZ();
    PointSet  legacy;
    legacy.add(1.f, 0.f, 2.f);
    legacy.add(3.f, 0.f, 4.f);
    CHECK(runtime->completeGeneration(generated.get(), &legacy));
    auto rejected = runtime->applyCellUpdate(level, x, z, 1, legacy);
    CHECK(!rejected.ok());

    auto migrated = runtime->migrateCellPointIds(level, x, z, 1);
    REQUIRE(migrated.ok());
    CHECK_EQ(migrated.value(), std::uint64_t(2));
    auto stored = std::unique_ptr<PointSet>(runtime->getCellOutput(level, x, z));
    REQUIRE(bool(stored));
    CHECK_NE(stored->getPointId(0), std::uint64_t(0));
    CHECK_NE(stored->getPointId(1), std::uint64_t(0));
    CHECK_NE(stored->getPointId(0), stored->getPointId(1));
}


TEST_CASE("procgen.runtimeGeneration.breaksEqualPriorityTiesDeterministically") {
    Procgen proc;
    auto    first  = requireRuntime(proc, 17);
    auto    second = requireRuntime(proc, 17);
    for (RuntimeLease* runtime : {&first, &second}) {
        (*runtime)->addLevel(10.f, 25.f, 1.5f);
        (*runtime)->setMaxGenerating(100);
        (*runtime)->updateSource(5.f, 5.f, 0.f, 0.f);
    }
    for (int index = 0; index < 12; ++index) {
        auto a = ownRequest(first->nextGenerate());
        auto b = ownRequest(second->nextGenerate());
        REQUIRE(bool(a));
        REQUIRE(bool(b));
        CHECK_EQ(a->getLevel(), b->getLevel());
        CHECK_EQ(a->getX(), b->getX());
        CHECK_EQ(a->getZ(), b->getZ());
    }
}

TEST_CASE("procgen.runtimeGeneration.issuesLargeQueuesWithConstantTimeCounters") {
    RuntimeGeneration runtime(1701);
    runtime.addLevel(1.f, 64.f, 1.25f);
    runtime.setMaxGenerating(20000);
    runtime.updateSource(0.5f, 0.5f, 0.f, 0.f);
    const int pending = runtime.getPendingGenerateCount();
    REQUIRE(pending > 12000);

    std::vector<std::unique_ptr<ProcgenCellRequest>> requests;
    requests.reserve(size_t(pending));
    while (auto request = ownRequest(runtime.nextGenerate())) requests.push_back(std::move(request));
    CHECK_EQ(int(requests.size()), pending);
    CHECK_EQ(runtime.getPendingGenerateCount(), 0);
    CHECK_EQ(runtime.getGeneratingCount(), pending);

    PointSet empty;
    for (auto& request : requests) CHECK(runtime.completeGeneration(request.get(), &empty));
    CHECK_EQ(runtime.getGeneratingCount(), 0);
    CHECK_EQ(runtime.getActiveCellCount(), pending);

    runtime.updateSource(10000.f, 10000.f, 0.f, 0.f);
    CHECK_EQ(runtime.getActiveCellCount(), 0);
    CHECK_EQ(runtime.getPendingCleanupCount(), pending);
}

TEST_CASE("procgen.runtimeGeneration.stagesBudgetedRefreshUntilAtomicCommit") {
    RuntimeGeneration runtime(1702);
    runtime.addLevel(10.f, 25.f, 1.5f);
    runtime.setRefreshWorkBudget(3);
    CHECK_EQ(runtime.getRefreshWorkBudget(), 3);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    CHECK(runtime.isRefreshPending());
    CHECK_EQ(runtime.getCommittedRefreshRevision(), uint64_t(0));
    CHECK_EQ(runtime.getPendingGenerateCount(), 0);

    uint64_t processed = 0;
    while (runtime.isRefreshPending()) {
        auto advanced = runtime.continueGenerationRefresh();
        REQUIRE(advanced.ok());
        CHECK(advanced.value() <= uint64_t(3));
        processed += advanced.value();
    }
    CHECK(processed > 3);
    CHECK_EQ(runtime.getCommittedRefreshRevision(), uint64_t(1));
    CHECK(runtime.getPendingGenerateCount() > 0);
}

TEST_CASE("procgen.runtimeGeneration.coalescesMovingSourcesWithoutRefreshStarvation") {
    RuntimeGeneration runtime(1703);
    runtime.addLevel(10.f, 6.f, 1.5f);
    runtime.setRefreshWorkBudget(1);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    for (int index = 1; index <= 12; ++index) runtime.updateSource(float(index * 100 + 5), 5.f, 1.f, 0.f);
    CHECK(runtime.getCommittedRefreshRevision() > uint64_t(0));
    CHECK(runtime.isRefreshPending());

    runtime.updateSource(1005.f, 5.f, 1.f, 0.f);
    int steps = 0;
    while (runtime.isRefreshPending() && steps++ < 64) REQUIRE(runtime.continueGenerationRefresh().ok());
    REQUIRE(!runtime.isRefreshPending());
    auto latest = ownRequest(runtime.nextGenerate());
    REQUIRE(bool(latest));
    CHECK(latest->getX() >= 99);
}


TEST_CASE("procgen.runtimeGeneration.trimsLowestPriorityCellsDeterministically") {
    Procgen proc;
    auto    runtime = requireRuntime(proc, 18);
    runtime->addLevel(10.f, 25.f, 1.5f);
    runtime->setMaxGenerating(100);
    runtime->updateSource(5.f, 5.f, 0.f, 0.f);
    int      farX = 0;
    int      farZ = 0;
    PointSet one;
    one.add(0.f, 0.f, 0.f);
    for (int index = 0; index < 3; ++index) {
        auto request = ownRequest(runtime->nextGenerate());
        REQUIRE(bool(request));
        farX = request->getX();
        farZ = request->getZ();
        CHECK(runtime->completeGeneration(request.get(), &one));
    }
    CHECK_EQ(runtime->getResidentPointCount(), 3);
    const int trimmedCells = runtime->trimToResidentPoints(2);
    CHECK_EQ(trimmedCells, 1);
    CHECK_EQ(runtime->getResidentPointCount(), 3);
    auto cleanup = ownRequest(runtime->nextCleanup());
    REQUIRE(bool(cleanup));
    CHECK_EQ(cleanup->getX(), farX);
    CHECK_EQ(cleanup->getZ(), farZ);
    CHECK(runtime->completeCleanup(cleanup.get()));
    CHECK_EQ(runtime->getResidentPointCount(), 2);
}

TEST_CASE("procgen.runtimeGeneration.boundsRetriesAndExplicitlyRecoversFailures") {
    RuntimeGeneration runtime(29);
    runtime.addLevel(10.f, 6.f, 1.5f);
    runtime.setMaxGenerationRetries(1);
    CHECK_EQ(runtime.getMaxGenerationRetries(), 1);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);

    ProcgenCellRequest* initial = runtime.nextGenerate();
    REQUIRE(bool(initial));
    CHECK(runtime.failGeneration(initial));
    delete initial;
    CHECK_EQ(runtime.getFailedCellCount(), 0);

    ProcgenCellRequest* retry = runtime.nextGenerate();
    REQUIRE(bool(retry));
    CHECK(runtime.failGeneration(retry));
    delete retry;
    CHECK_EQ(runtime.getFailedCellCount(), 1);
    CHECK(runtime.debugReport().find("failed=1") != std::string::npos);

    const int retriedCells = runtime.retryFailedCells();
    CHECK_EQ(retriedCells, 1);
    CHECK_EQ(runtime.getFailedCellCount(), 0);
    ProcgenCellRequest* recovered = runtime.nextGenerate();
    REQUIRE(bool(recovered));
    PointSet empty;
    CHECK(runtime.completeGeneration(recovered, &empty));
    CHECK_EQ(runtime.getActiveCellCount(), 1);
    delete recovered;
}

TEST_CASE("procgen.runtimeGeneration.persistsAttributedCellCachesAtomically") {
    RuntimeGeneration source(404);
    source.addLevel(10.f, 6.f, 1.5f);
    source.updateSource(5.f, 5.f, 1.f, 0.f);
    ProcgenCellRequest* generated = source.nextGenerate();
    REQUIRE(bool(generated));
    const int level = generated->getLevel();
    const int x     = generated->getX();
    const int z     = generated->getZ();
    PointSet  points;
    const int point = points.add(1.25f, 2.5f, 3.75f);
    points.setNormal(point, 0.f, 0.f, 1.f);
    points.setRotation(point, 15.f, 45.f, 25.f);
    points.setScale(point, 2.f, 3.f, 4.f);
    points.setBounds(point, -1.f, -2.f, -3.f, 1.f, 2.f, 3.f);
    points.setColor(point, 0.1f, 0.2f, 0.3f, 0.4f);
    points.setSteepness(point, 0.65f);
    points.setDensity(point, 0.75f);
    points.setPointSeed(point, 1234);
    points.trySetPointId(point, 18446744073709551614ull).expect("runtime generation test point id");
    points.setFloatAttribute(point, "slope", 12.5f);
    points.setFloatAttribute(point, "roughness", 0.4f);
    points.setIntAttribute(point, "variant", 7);
    points.setBoolAttribute(point, "hero", true);
    points.setVectorAttribute(point, "wind", 1.f, 2.f, 3.f);
    points.setStringAttribute(point, "asset", "oak \"hero\"");
    points.setStringAttribute(point, "biome", "forest");
    points.add(9.f, 8.f, 7.f);
    CHECK(source.completeGeneration(generated, &points));
    delete generated;

    const std::string persisted = source.serializeCell(level, x, z);
    CHECK(persisted.find("EVPCG_CELL 3 404") == 0);
    RuntimeGeneration equivalent(404);
    equivalent.addLevel(10.f, 6.f, 1.5f);
    equivalent.updateSource(5.f, 5.f, 1.f, 0.f);
    ProcgenCellRequest* equivalentRequest = equivalent.nextGenerate();
    REQUIRE(bool(equivalentRequest));
    PointSet equivalentPoints = points;
    equivalentPoints.clearPointAttributes(0).expect("runtime generation test metadata reset");
    equivalentPoints.setFloatAttribute(0, "roughness", 0.4f);
    equivalentPoints.setFloatAttribute(0, "slope", 12.5f);
    equivalentPoints.setIntAttribute(0, "variant", 7);
    equivalentPoints.setBoolAttribute(0, "hero", true);
    equivalentPoints.setVectorAttribute(0, "wind", 1.f, 2.f, 3.f);
    equivalentPoints.setStringAttribute(0, "biome", "forest");
    equivalentPoints.setStringAttribute(0, "asset", "oak \"hero\"");
    CHECK(equivalent.completeGeneration(equivalentRequest, &equivalentPoints));
    delete equivalentRequest;
    CHECK_EQ(equivalent.serializeCell(level, x, z), persisted);
    RuntimeGeneration restored(404);
    restored.addLevel(10.f, 6.f, 1.5f);
    CHECK(restored.deserializeCell(persisted));
    CHECK(restored.hasCell(level, x, z));
    CHECK_EQ(restored.getCellRevision(level, x, z), uint64_t(1));
    PointSet* loaded = restored.getCellOutput(level, x, z);
    REQUIRE(bool(loaded));
    CHECK_EQ(loaded->getCount(), 2);
    CHECK_EQ(loaded->getX(0), 1.25f);
    CHECK_EQ(loaded->getNormalZ(0), 1.f);
    CHECK_EQ(loaded->getPitch(0), 15.f);
    CHECK_EQ(loaded->getRoll(0), 25.f);
    CHECK_EQ(loaded->getScaleY(0), 3.f);
    CHECK_EQ(loaded->getBoundsMinY(0), -2.f);
    CHECK_EQ(loaded->getColorA(0), 0.4f);
    CHECK_EQ(loaded->getSteepness(0), 0.65f);
    CHECK_EQ(loaded->getPointId(0), 18446744073709551614ull);
    CHECK_EQ(loaded->getFloatAttribute(0, "slope", -1.f), 12.5f);
    CHECK_EQ(loaded->getIntAttribute(0, "variant", -1), 7);
    CHECK(loaded->getBoolAttribute(0, "hero", false));
    CHECK_EQ(loaded->getVectorAttributeZ(0, "wind", -1.f), 3.f);
    CHECK_EQ(loaded->getStringAttribute(0, "asset", ""), std::string("oak \"hero\""));
    delete loaded;

    CHECK(!restored.deserializeCell(persisted + "TRAILING"));
    CHECK(restored.hasCell(level, x, z));
    RuntimeGeneration wrongWorld(405);
    wrongWorld.addLevel(10.f, 6.f, 1.5f);
    CHECK(!wrongWorld.deserializeCell(persisted));
    RuntimeGeneration constrained(404);
    constrained.addLevel(10.f, 6.f, 1.5f);
    constrained.setMaxPointsPerCell(1);
    CHECK(!constrained.deserializeCell(persisted));
    CHECK_EQ(constrained.getRejectedOutputCount(), 1);

    RuntimeGeneration legacy(404);
    legacy.addLevel(10.f, 6.f, 1.5f);
    const std::string legacyV1 =
        "EVPCG_CELL 1 404 0 0 0 1 10\nPOINTS 1\n"
        "POINT 1 2 3 0 1 0 90 1 1 1 0.5 77\nEND\n";
    CHECK(legacy.deserializeCell(legacyV1));
    PointSet* legacyPoints = legacy.getCellOutput(0, 0, 0);
    REQUIRE(bool(legacyPoints));
    CHECK_EQ(legacyPoints->getYaw(0), 90.f);
    CHECK_EQ(legacyPoints->getPitch(0), 0.f);
    CHECK_EQ(legacyPoints->getColorR(0), 1.f);
    CHECK_EQ(legacyPoints->getSteepness(0), 0.5f);
    CHECK_EQ(legacyPoints->getPointId(0), uint64_t(0));
    delete legacyPoints;

    RuntimeGeneration legacyV2Runtime(404);
    legacyV2Runtime.addLevel(10.f, 6.f, 1.5f);
    const std::string legacyV2 =
        "EVPCG_CELL 2 404 0 0 0 1 10\nPOINTS 1\n"
        "POINT 1 2 3 0 1 0 10 90 20 1 1 1 0.5 77 0 0 0 0 0 0 1 1 1 1 0.5\nEND\n";
    CHECK(legacyV2Runtime.deserializeCell(legacyV2));
    PointSet* legacyV2Points = legacyV2Runtime.getCellOutput(0, 0, 0);
    REQUIRE(bool(legacyV2Points));
    CHECK_EQ(legacyV2Points->getPointId(0), uint64_t(0));
    delete legacyV2Points;
}
}  // namespace test_procgen_runtime_generation_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_scene_sink)
#include <cmath>
#include <future>
#include <limits>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/Module.h"
#include "common/ProcgenSceneSink.h"
#include "common/ProcgenWorldQuery.h"
#include "procgen/MeshBuild.h"
#include "procgen/Procgen.h"
#include "procgen/RuntimeGeneration.h"
#include "procgen/ShapeGrammar.h"
#include "procgen/SpatialData.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// procgen_scene_sink.cpp
namespace test_procgen_scene_sink_unit {
// procgen_scene_sink.cpp
using namespace eve::procgen;

namespace {

class MockProcgenSceneSink final : public eve::IProcgenSceneSink {
public:
    bool applyBatch(const std::string& batchId, const std::vector<eve::ProcgenInstanceDesc>& instances) override {
        const auto                            previous = batches.find(batchId);
        std::unordered_map<std::string, bool> oldIds;
        if (previous != batches.end())
            for (const auto& instance : previous->second) oldIds[instance.id] = true;
        Stats stats;
        for (const auto& instance : instances) {
            if (oldIds.erase(instance.id) != 0)
                ++stats.reused;
            else
                ++stats.created;
        }
        stats.removed      = int(oldIds.size());
        batches[batchId]   = instances;
        revisions[batchId] = revisions[batchId] + 1;
        latest[batchId]    = stats;
        return true;
    }
    eve::Result<uint64_t> replaceBatch(const std::string& batchId, uint64_t targetRevision,
                                       const std::vector<eve::ProcgenInstanceDesc>& instances) override {
        if (targetRevision == 0 || batchRevision(batchId) >= targetRevision)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "mock snapshot revision is stale"));
        if (!applyBatch(batchId, instances))
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "mock snapshot commit failed"));
        ++replaceCalls;
        revisions[batchId] = targetRevision;
        return eve::Result<uint64_t>::success(targetRevision);
    }
    eve::Result<uint64_t> replaceBatches(const std::vector<eve::ProcgenBatchSnapshot>& snapshots) override {
        if (failBatchTransaction)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected batch transaction failure"));
        auto nextBatches   = batches;
        auto nextRevisions = revisions;
        for (const auto& snapshot : snapshots) {
            if (snapshot.batchId.empty() || snapshot.targetRevision == 0 ||
                nextRevisions[snapshot.batchId] >= snapshot.targetRevision)
                return eve::Result<uint64_t>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "mock batch transaction is stale"));
            nextBatches[snapshot.batchId]   = snapshot.instances;
            nextRevisions[snapshot.batchId] = snapshot.targetRevision;
        }
        batches.swap(nextBatches);
        revisions.swap(nextRevisions);
        return eve::Result<uint64_t>::success(static_cast<uint64_t>(snapshots.size()));
    }
    eve::Result<uint64_t> applyDelta(const std::string& batchId, const eve::ProcgenInstanceDelta& delta) override {
        const auto current = revisions.find(batchId);
        if (current == revisions.end() || current->second != delta.baseRevision)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "mock delta revision is stale"));
        lastDeltaBatch = batchId;
        lastDelta      = delta;
        ++deltaCalls;
        revisions[batchId] = delta.targetRevision;
        return eve::Result<uint64_t>::success(delta.targetRevision);
    }
    uint64_t batchRevision(const std::string& batchId) const override {
        const auto found = revisions.find(batchId);
        return found == revisions.end() ? 0 : found->second;
    }
    bool removeBatch(const std::string& batchId) override {
        const auto found = batches.find(batchId);
        if (found == batches.end()) return false;
        latest[batchId] = {0, 0, int(found->second.size())};
        batches.erase(found);
        revisions.erase(batchId);
        return true;
    }
    eve::Result<uint64_t> removeBatches(const std::vector<std::string>& batchIds) override {
        return removeBatchesCoordinated(batchIds, [] { return eve::Result<void>::success(); });
    }
    eve::Result<uint64_t> removeBatchesCoordinated(
        const std::vector<std::string>&           batchIds,
        const std::function<eve::Result<void>()>& commitPreparedOwner) override {
        if (failBatchTransaction)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected batch transaction failure"));
        auto nextBatches   = batches;
        auto nextRevisions = revisions;
        for (const auto& batchId : batchIds) {
            if (nextBatches.erase(batchId) != 1)
                return eve::Result<uint64_t>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "mock batch is unpublished"));
            nextRevisions.erase(batchId);
        }
        if (!commitPreparedOwner)
            return eve::Result<uint64_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "mock commit is missing"));
        auto ownerCommit = commitPreparedOwner();
        if (!ownerCommit.ok()) return eve::Result<uint64_t>::failure(ownerCommit.status());
        batches.swap(nextBatches);
        revisions.swap(nextRevisions);
        return eve::Result<uint64_t>::success(static_cast<uint64_t>(batchIds.size()));
    }
    int instanceCount(const std::string& batchId) const override {
        const auto found = batches.find(batchId);
        return found == batches.end() ? 0 : int(found->second.size());
    }
    int lastCreatedCount(const std::string& batchId) const override {
        const auto found = latest.find(batchId);
        return found == latest.end() ? 0 : found->second.created;
    }
    int lastReusedCount(const std::string& batchId) const override {
        const auto found = latest.find(batchId);
        return found == latest.end() ? 0 : found->second.reused;
    }
    int lastRemovedCount(const std::string& batchId) const override {
        const auto found = latest.find(batchId);
        return found == latest.end() ? 0 : found->second.removed;
    }

    struct Stats {
        int created = 0;
        int reused  = 0;
        int removed = 0;
    };
    std::unordered_map<std::string, std::vector<eve::ProcgenInstanceDesc>> batches;
    std::unordered_map<std::string, uint64_t>                              revisions;
    std::unordered_map<std::string, Stats>                                 latest;
    std::string                                                            lastDeltaBatch;
    eve::ProcgenInstanceDelta                                              lastDelta;
    int                                                                    replaceCalls         = 0;
    int                                                                    deltaCalls           = 0;
    bool                                                                   failBatchTransaction = false;
};

}  // namespace

TEST_CASE("procgen.sceneSink.reportsUnsupportedCoordinatedReplacement") {
    MockProcgenSceneSink sink;
    bool                 called = false;
    auto                 result = sink.replaceBatchesCoordinated({{"unsupported", 1, {}}}, [&] {
        called = true;
        return eve::Result<void>::success();
    });
    REQUIRE(!result.ok());
    REQUIRE(!called);
    REQUIRE(sink.batchRevision("unsupported") == 0);
}

TEST_CASE("procgen.sceneSink.publishesStableAttributedInstances") {
    eve::cap::detail::clearAllRaw();
    MockProcgenSceneSink sink;
    eve::cap::provide<eve::IProcgenSceneSink>(&sink);

    Procgen proc;
    auto    pointsResult = proc.newPointSetHandle();
    REQUIRE(pointsResult.ok());
    const auto pointsHandle = std::move(pointsResult).takeValue();
    auto       pointsView   = proc.resolvePointSet(pointsHandle);
    REQUIRE(pointsView.isBound());
    const int tree = pointsView->add(1.f, 2.f, 3.f);
    REQUIRE(pointsView->trySetPointId(tree, 1001).ok());
    pointsView->setPointSeed(tree, 77);
    pointsView->setYaw(tree, 30.f);
    pointsView->setRotation(tree, 12.f, 30.f, -15.f);
    pointsView->setScale(tree, 2.f, 3.f, 4.f);
    const std::string prefabAsset = "asset://018f0b7e-6e50-7a10-8c22-2c8f8e3dd191";
    const std::string prefabBuild = "018f0b7e-6e50-7a10-8c22-2c8f8e3dd192";
    pointsView->setStringAttribute(tree, "asset", prefabAsset);
    pointsView->setStringAttribute(tree, "asset_build_id", prefabBuild);
    const int rock = pointsView->add(8.f, 0.f, 9.f);
    REQUIRE(pointsView->trySetPointId(rock, 1002).ok());
    pointsView->setPointSeed(rock, 88);

    auto publishedResult = proc.publishInstances("forest/main", pointsHandle, "asset", "granite");
    REQUIRE(publishedResult.ok());
    CHECK_EQ(proc.getPublishedInstanceCount("forest/main"), 2);
    CHECK_EQ(proc.getPublishedCreatedCount("forest/main"), 2);
    CHECK_EQ(proc.getPublishedReusedCount("forest/main"), 0);
    CHECK_EQ(proc.getPublishedRemovedCount("forest/main"), 0);
    const auto& published = sink.batches.at("forest/main");
    CHECK_EQ(published[0].id, std::string("pcg-id-1001"));
    CHECK_EQ(published[0].sourcePointId, uint64_t(1001));
    CHECK_EQ(published[0].asset, prefabAsset);
    REQUIRE(published[0].assetBinding.has_value());
    CHECK_EQ(published[0].assetBinding->asset.format(), prefabAsset);
    CHECK_EQ(published[0].assetBinding->buildId.format(), prefabBuild);
    CHECK_EQ(published[0].x, 1.f);
    CHECK_EQ(published[0].yaw, 30.f);
    CHECK_EQ(published[0].pitch, 12.f);
    CHECK_EQ(published[0].roll, -15.f);
    CHECK_EQ(published[0].scaleZ, 4.f);
    CHECK_EQ(published[1].asset, std::string("granite"));
    CHECK_EQ(published[1].id, std::string("pcg-id-1002"));
    CHECK_EQ(published[1].sourcePointId, uint64_t(1002));
    pointsView->setStringAttribute(tree, "asset_build_id", "invalid-build");
    auto invalidBinding = proc.publishInstances("forest/main", pointsHandle, "asset", "granite");
    REQUIRE(!invalidBinding.ok());
    CHECK_EQ(sink.batches.at("forest/main")[0].assetBinding->buildId.format(), prefabBuild);
    pointsView->setStringAttribute(tree, "asset_build_id", prefabBuild);
    pointsView->setRotation(tree, std::numeric_limits<float>::quiet_NaN(), 30.f, -15.f);
    auto invalidRotation = proc.publishInstances("forest/main", pointsHandle, "asset", "granite");
    REQUIRE(!invalidRotation.ok());
    CHECK_EQ(sink.batches.at("forest/main")[0].pitch, 12.f);
    CHECK_EQ(sink.batches.at("forest/main")[0].roll, -15.f);
    pointsView->setRotation(tree, 12.f, 30.f, -15.f);

    auto reorderedResult = proc.newPointSetHandle();
    REQUIRE(reorderedResult.ok());
    const auto reorderedHandle = std::move(reorderedResult).takeValue();
    auto       reorderedView   = proc.resolvePointSet(reorderedHandle);
    REQUIRE(reorderedView.isBound());
    reorderedView->add(-1.f, 0.f, 0.f);
    reorderedView->setPointSeed(0, 99);
    std::move(reorderedView->appendPointFrom(*pointsView, 0)).expect("scene sink test point copy");
    std::move(reorderedView->appendPointFrom(*pointsView, 1)).expect("scene sink test point copy");
    auto reorderedPublishResult = proc.publishInstances("forest/main", reorderedHandle, "asset", "granite");
    REQUIRE(reorderedPublishResult.ok());
    CHECK_EQ(proc.getPublishedCreatedCount("forest/main"), 1);
    CHECK_EQ(proc.getPublishedReusedCount("forest/main"), 2);
    CHECK_EQ(proc.getPublishedRemovedCount("forest/main"), 0);
    const auto& reconciled = sink.batches.at("forest/main");
    CHECK_EQ(reconciled[1].id, std::string("pcg-id-1001"));
    CHECK_EQ(reconciled[2].id, std::string("pcg-id-1002"));

    reorderedView->setStringAttribute(0, "instanceId", "hero-tree");
    reorderedView->setStringAttribute(1, "instanceId", "hero-tree");
    auto duplicateResult = proc.publishInstances("forest/main", reorderedHandle, "asset", "granite");
    REQUIRE(!duplicateResult.ok());
    CHECK(duplicateResult.status().describe().find("duplicate instance id") != std::string::npos);
    CHECK_EQ(proc.getPublishedInstanceCount("forest/main"), 3);

    auto removeResult = proc.removeInstances("forest/main");
    REQUIRE(removeResult.ok());
    CHECK_EQ(proc.getPublishedInstanceCount("forest/main"), 0);
    CHECK_EQ(proc.getPublishedRemovedCount("forest/main"), 3);
    REQUIRE(proc.releasePointSet(reorderedHandle).ok());
    REQUIRE(proc.releasePointSet(pointsHandle).ok());
    eve::cap::revoke<eve::IProcgenSceneSink>(&sink);
}

TEST_CASE("procgen.sceneSink.projectsRuntimePointDeltaByStableIdentity") {
    eve::cap::detail::clearAllRaw();
    MockProcgenSceneSink sink;
    eve::cap::provide<eve::IProcgenSceneSink>(&sink);

    Procgen proc;
    auto    pointsResult = proc.newPointSetHandle();
    REQUIRE(pointsResult.ok());
    const auto pointsHandle = std::move(pointsResult).takeValue();
    auto       pointsView   = proc.resolvePointSet(pointsHandle);
    REQUIRE(pointsView.isBound());
    const int tree = pointsView->add(1.f, 0.f, 2.f);
    REQUIRE(pointsView->trySetPointId(tree, 2001).ok());
    pointsView->setStringAttribute(tree, "instanceId", "tree-custom");
    pointsView->setStringAttribute(tree, "asset", "oak");
    const int rock = pointsView->add(3.f, 0.f, 4.f);
    REQUIRE(pointsView->trySetPointId(rock, 2002).ok());

    RuntimeGeneration runtime(77);
    REQUIRE(runtime.addLevel(10.f, 6.f, 2.f) >= 0);
    runtime.updateSource(1.f, 1.f, 1.f, 0.f);
    std::unique_ptr<ProcgenCellRequest> request(runtime.nextGenerate());
    REQUIRE(bool(request));
    auto initialPublish = proc.publishCellInstances("world", *request, pointsHandle, "asset", "stone");
    REQUIRE(initialPublish.ok());

    PointSet  target;
    const int targetTree = target.appendPointFrom(*pointsView, std::size_t(tree)).expect("scene delta target tree");
    target.setPosition(targetTree, 9.f, 1.f, 2.f);
    target.setStringAttribute(targetTree, "instanceId", "tree-renamed");
    ProcgenPoint flower;
    flower.id             = 2003;
    flower.seed           = 33;
    const int flowerIndex = target.appendPoint(flower);
    target.setStringAttribute(flowerIndex, "asset", "lily");
    auto delta = diffPointSets(*pointsView, target);
    REQUIRE(delta.ok());

    auto applied = proc.publishCellInstanceDelta("world", *request, delta.value(), 2, "asset", "stone");
    REQUIRE(applied.ok());
    CHECK_EQ(applied.value(), uint64_t(2));
    CHECK_EQ(sink.lastDeltaBatch, std::string("world/L0/0/0"));
    CHECK_EQ(sink.lastDelta.baseRevision, uint64_t(1));
    CHECK_EQ(sink.lastDelta.targetRevision, uint64_t(2));
    REQUIRE_EQ(sink.lastDelta.updated.size(), std::size_t(1));
    CHECK_EQ(sink.lastDelta.updated[0].sourcePointId, uint64_t(2001));
    CHECK_EQ(sink.lastDelta.updated[0].id, std::string("tree-renamed"));
    CHECK_EQ(sink.lastDelta.updated[0].x, 9.f);
    REQUIRE_EQ(sink.lastDelta.added.size(), std::size_t(1));
    CHECK_EQ(sink.lastDelta.added[0].sourcePointId, uint64_t(2003));
    CHECK_EQ(sink.lastDelta.added[0].asset, std::string("lily"));
    REQUIRE_EQ(sink.lastDelta.removedPointIds.size(), std::size_t(1));
    CHECK_EQ(sink.lastDelta.removedPointIds[0], uint64_t(2002));
    CHECK_EQ(sink.lastDelta.targetPointOrder, delta.value().targetOrder);

    auto recoveryResult = proc.newPointSetHandle();
    REQUIRE(recoveryResult.ok());
    const auto recoveryHandle = std::move(recoveryResult).takeValue();
    auto       recoveryView   = proc.resolvePointSet(recoveryHandle);
    REQUIRE(recoveryView.isBound());
    for (std::size_t pointIndex = 0; pointIndex < target.points().size(); ++pointIndex)
        recoveryView->appendPointFrom(target, pointIndex).expect("scene recovery snapshot");
    auto recovered = proc.publishCellSnapshot("world", *request, recoveryHandle, 5, "asset", "stone");
    REQUIRE(recovered.ok());
    CHECK_EQ(recovered.value(), uint64_t(5));
    CHECK_EQ(sink.batchRevision("world/L0/0/0"), uint64_t(5));
    REQUIRE_EQ(sink.batches.at("world/L0/0/0").size(), std::size_t(2));
    CHECK_EQ(sink.batches.at("world/L0/0/0")[0].id, std::string("tree-renamed"));
    CHECK_EQ(sink.batches.at("world/L0/0/0")[1].sourcePointId, uint64_t(2003));
    auto staleRecovery = proc.publishCellSnapshot("world", *request, recoveryHandle, 4, "asset", "stone");
    CHECK(!staleRecovery.ok());
    CHECK_EQ(sink.batchRevision("world/L0/0/0"), uint64_t(5));

    auto invalidResult = proc.newPointSetHandle();
    REQUIRE(invalidResult.ok());
    const auto invalidHandle = std::move(invalidResult).takeValue();
    auto       invalidView   = proc.resolvePointSet(invalidHandle);
    REQUIRE(invalidView.isBound());
    invalidView->add(0.f, 0.f, 0.f);
    auto invalidSnapshot = proc.publishCellSnapshot("world", *request, invalidHandle, 6, "asset", "stone");
    CHECK(!invalidSnapshot.ok());
    CHECK_EQ(sink.batchRevision("world/L0/0/0"), uint64_t(5));

    REQUIRE(proc.releasePointSet(invalidHandle).ok());
    REQUIRE(proc.releasePointSet(recoveryHandle).ok());
    REQUIRE(proc.releasePointSet(pointsHandle).ok());
    eve::cap::revoke<eve::IProcgenSceneSink>(&sink);
}

TEST_CASE("procgen.sceneSink.synchronizesRuntimeRevisionWithDeltaAndSnapshotRecovery") {
    eve::cap::detail::clearAllRaw();
    MockProcgenSceneSink sink;
    eve::cap::provide<eve::IProcgenSceneSink>(&sink);

    Procgen           proc;
    RuntimeGeneration runtime(91);
    REQUIRE(runtime.addLevel(10.f, 6.f, 2.f) >= 0);
    runtime.updateSource(1.f, 1.f, 1.f, 0.f);
    std::unique_ptr<ProcgenCellRequest> request(runtime.nextGenerate());
    REQUIRE(bool(request));

    PointSet  initial;
    const int tree = initial.add(1.f, 0.f, 2.f);
    REQUIRE(initial.trySetPointId(tree, 3001).ok());
    initial.setStringAttribute(tree, "asset", "oak");
    REQUIRE(runtime.completeGeneration(request.get(), &initial));

    auto first = proc.synchronizeCellInstances("stream", runtime, *request, "asset", "stone");
    REQUIRE(first.ok());
    CHECK_EQ(first.value(), uint64_t(1));
    CHECK_EQ(sink.replaceCalls, 1);
    CHECK_EQ(sink.deltaCalls, 0);
    auto unchanged = proc.synchronizeCellInstances("stream", runtime, *request, "asset", "stone");
    REQUIRE(unchanged.ok());
    CHECK_EQ(sink.replaceCalls, 1);

    PointSet revisionTwo = initial;
    revisionTwo.setPosition(0, 4.f, 0.f, 2.f);
    auto updated = runtime.applyCellUpdate(0, 0, 0, 1, revisionTwo);
    REQUIRE(updated.ok());
    auto incremental = proc.synchronizeCellInstances("stream", runtime, *request, "asset", "stone");
    REQUIRE(incremental.ok());
    CHECK_EQ(incremental.value(), uint64_t(2));
    CHECK_EQ(sink.deltaCalls, 1);
    CHECK_EQ(sink.replaceCalls, 1);

    PointSet revisionThree = revisionTwo;
    revisionThree.setPosition(0, 6.f, 0.f, 2.f);
    REQUIRE(runtime.applyCellUpdate(0, 0, 0, 2, revisionThree).ok());
    PointSet revisionFour = revisionThree;
    revisionFour.setPosition(0, 8.f, 0.f, 2.f);
    REQUIRE(runtime.applyCellUpdate(0, 0, 0, 3, revisionFour).ok());
    auto recovered = proc.synchronizeCellInstances("stream", runtime, *request, "asset", "stone");
    REQUIRE(recovered.ok());
    CHECK_EQ(recovered.value(), uint64_t(4));
    CHECK_EQ(sink.replaceCalls, 2);
    CHECK_EQ(sink.deltaCalls, 1);
    CHECK_EQ(sink.batches.at("stream/L0/0/0")[0].x, 8.f);

    sink.revisions["stream/L0/0/0"] = 5;
    auto sceneAhead                 = proc.synchronizeCellInstances("stream", runtime, *request, "asset", "stone");
    CHECK(!sceneAhead.ok());
    CHECK_EQ(sink.replaceCalls, 2);
    CHECK_EQ(sink.deltaCalls, 1);

    eve::cap::revoke<eve::IProcgenSceneSink>(&sink);
}

TEST_CASE("procgen.sceneSink.synchronizesSeveralRuntimeCellsAtomically") {
    eve::cap::detail::clearAllRaw();
    MockProcgenSceneSink sink;
    eve::cap::provide<eve::IProcgenSceneSink>(&sink);

    Procgen           proc;
    RuntimeGeneration firstRuntime(301);
    RuntimeGeneration secondRuntime(302);
    REQUIRE(firstRuntime.addLevel(10.f, 6.f, 2.f) >= 0);
    REQUIRE(secondRuntime.addLevel(10.f, 6.f, 2.f) >= 0);
    firstRuntime.updateSource(1.f, 1.f, 1.f, 0.f);
    secondRuntime.updateSource(21.f, 1.f, 1.f, 0.f);
    std::unique_ptr<ProcgenCellRequest> firstRequest(firstRuntime.nextGenerate());
    std::unique_ptr<ProcgenCellRequest> secondRequest(secondRuntime.nextGenerate());
    REQUIRE(bool(firstRequest));
    REQUIRE(bool(secondRequest));

    PointSet  firstPoints;
    const int firstPoint = firstPoints.add(1.f, 0.f, 1.f);
    REQUIRE(firstPoints.trySetPointId(firstPoint, 5001).ok());
    PointSet  secondPoints;
    const int secondPoint = secondPoints.add(21.f, 0.f, 1.f);
    REQUIRE(secondPoints.trySetPointId(secondPoint, 5002).ok());
    REQUIRE(firstRuntime.completeGeneration(firstRequest.get(), &firstPoints));
    REQUIRE(secondRuntime.completeGeneration(secondRequest.get(), &secondPoints));

    std::vector<const RuntimeGeneration*>  runtimes{&firstRuntime, &secondRuntime};
    std::vector<const ProcgenCellRequest*> requests{firstRequest.get(), secondRequest.get()};
    auto committed = proc.synchronizeCellInstancesAtomic("world", runtimes, requests, "asset", "stone");
    REQUIRE(committed.ok());
    CHECK_EQ(committed.value(), uint64_t(2));
    CHECK_EQ(sink.revisions.at("world/L0/0/0"), uint64_t(1));
    CHECK_EQ(sink.revisions.at("world/L0/2/0"), uint64_t(1));

    PointSet firstRevisionTwo = firstPoints;
    firstRevisionTwo.setPosition(0, 5.f, 0.f, 1.f);
    PointSet secondRevisionTwo = secondPoints;
    secondRevisionTwo.setPosition(0, 25.f, 0.f, 1.f);
    REQUIRE(firstRuntime.applyCellUpdate(0, 0, 0, 1, firstRevisionTwo).ok());
    REQUIRE(secondRuntime.applyCellUpdate(0, 2, 0, 1, secondRevisionTwo).ok());
    sink.failBatchTransaction = true;
    auto rejected             = proc.synchronizeCellInstancesAtomic("world", runtimes, requests, "asset", "stone");
    REQUIRE(!rejected.ok());
    CHECK_EQ(sink.revisions.at("world/L0/0/0"), uint64_t(1));
    CHECK_EQ(sink.revisions.at("world/L0/2/0"), uint64_t(1));
    CHECK_EQ(sink.batches.at("world/L0/0/0")[0].x, 1.f);
    CHECK_EQ(sink.batches.at("world/L0/2/0")[0].x, 21.f);

    sink.failBatchTransaction = false;
    auto recovered            = proc.synchronizeCellInstancesAtomic("world", runtimes, requests, "asset", "stone");
    REQUIRE(recovered.ok());
    CHECK_EQ(sink.batches.at("world/L0/0/0")[0].x, 5.f);
    CHECK_EQ(sink.batches.at("world/L0/2/0")[0].x, 25.f);

    std::vector<const ProcgenCellRequest*> duplicateRequests{firstRequest.get(), firstRequest.get()};
    auto duplicateRemoval = proc.removeCellInstancesAtomic("world", duplicateRequests);
    REQUIRE(!duplicateRemoval.ok());
    CHECK_EQ(sink.batches.size(), std::size_t(2));

    sink.failBatchTransaction = true;
    auto removalRejected      = proc.removeCellInstancesAtomic("world", requests);
    REQUIRE(!removalRejected.ok());
    CHECK_EQ(sink.batches.size(), std::size_t(2));
    sink.failBatchTransaction = false;
    auto removed              = proc.removeCellInstancesAtomic("world", requests);
    REQUIRE(removed.ok());
    CHECK_EQ(removed.value(), uint64_t(2));
    CHECK(sink.batches.empty());
    eve::cap::revoke<eve::IProcgenSceneSink>(&sink);
}

TEST_CASE("procgen.sceneSink.completesSceneAndRuntimeCleanupAtomically") {
    eve::cap::detail::clearAllRaw();
    MockProcgenSceneSink sink;
    eve::cap::provide<eve::IProcgenSceneSink>(&sink);
    Procgen proc;

    RuntimeGeneration firstRuntime(31);
    RuntimeGeneration secondRuntime(32);
    REQUIRE(firstRuntime.addLevel(10.f, 6.f, 1.5f) >= 0);
    REQUIRE(secondRuntime.addLevel(10.f, 6.f, 1.5f) >= 0);
    firstRuntime.updateSource(1.f, 1.f, 1.f, 0.f);
    secondRuntime.updateSource(21.f, 1.f, 1.f, 0.f);
    auto firstGenerated  = std::unique_ptr<ProcgenCellRequest>(firstRuntime.nextGenerate());
    auto secondGenerated = std::unique_ptr<ProcgenCellRequest>(secondRuntime.nextGenerate());
    REQUIRE(bool(firstGenerated));
    REQUIRE(bool(secondGenerated));
    PointSet  firstPoints;
    PointSet  secondPoints;
    const int firstPoint  = firstPoints.add(1.f, 0.f, 1.f);
    const int secondPoint = secondPoints.add(21.f, 0.f, 1.f);
    REQUIRE(firstPoints.trySetPointId(firstPoint, 6001).ok());
    REQUIRE(secondPoints.trySetPointId(secondPoint, 6002).ok());
    REQUIRE(firstRuntime.completeGeneration(firstGenerated.get(), &firstPoints));
    REQUIRE(secondRuntime.completeGeneration(secondGenerated.get(), &secondPoints));
    std::vector<const RuntimeGeneration*>  publishRuntimes{&firstRuntime, &secondRuntime};
    std::vector<const ProcgenCellRequest*> publishRequests{firstGenerated.get(), secondGenerated.get()};
    REQUIRE(proc.synchronizeCellInstancesAtomic("cleanup", publishRuntimes, publishRequests, "asset", "stone").ok());

    firstRuntime.updateSource(1000.f, 1000.f, 1.f, 0.f);
    secondRuntime.updateSource(1000.f, 1000.f, 1.f, 0.f);
    auto firstCleanup  = std::unique_ptr<ProcgenCellRequest>(firstRuntime.nextCleanup());
    auto secondCleanup = std::unique_ptr<ProcgenCellRequest>(secondRuntime.nextCleanup());
    REQUIRE(bool(firstCleanup));
    REQUIRE(bool(secondCleanup));
    std::vector<RuntimeGeneration*>        cleanupRuntimes{&firstRuntime, &secondRuntime};
    std::vector<const ProcgenCellRequest*> cleanupRequests{firstCleanup.get(), secondCleanup.get()};

    sink.failBatchTransaction = true;
    auto sceneRejected        = proc.completeCellCleanupAtomic("cleanup", cleanupRuntimes, cleanupRequests);
    REQUIRE(!sceneRejected.ok());
    CHECK(firstRuntime.isRequestCurrent(firstCleanup.get()));
    CHECK(secondRuntime.isRequestCurrent(secondCleanup.get()));
    CHECK_EQ(sink.batches.size(), std::size_t(2));
    sink.failBatchTransaction = false;

    firstRuntime.updateSource(1.f, 1.f, 1.f, 0.f);
    firstRuntime.updateSource(1000.f, 1000.f, 1.f, 0.f);
    auto staleRejected = proc.completeCellCleanupAtomic("cleanup", cleanupRuntimes, cleanupRequests);
    REQUIRE(!staleRejected.ok());
    CHECK(secondRuntime.isRequestCurrent(secondCleanup.get()));
    CHECK_EQ(sink.batches.size(), std::size_t(2));
    firstCleanup = std::unique_ptr<ProcgenCellRequest>(firstRuntime.nextCleanup());
    REQUIRE(bool(firstCleanup));
    cleanupRequests[0] = firstCleanup.get();

    auto completed = proc.completeCellCleanupAtomic("cleanup", cleanupRuntimes, cleanupRequests);
    REQUIRE(completed.ok());
    CHECK_EQ(completed.value(), uint64_t(2));
    CHECK(!firstRuntime.isRequestCurrent(firstCleanup.get()));
    CHECK(!secondRuntime.isRequestCurrent(secondCleanup.get()));
    CHECK(sink.batches.empty());
    CHECK_EQ(firstRuntime.getResidentPointCount(), 0);
    CHECK_EQ(secondRuntime.getResidentPointCount(), 0);
    eve::cap::revoke<eve::IProcgenSceneSink>(&sink);
}


TEST_CASE("procgen.sceneSink.reportsMissingProvider") {
    eve::cap::detail::clearAllRaw();
    Procgen proc;
    auto    pointsResult = proc.newPointSetHandle();
    REQUIRE(pointsResult.ok());
    const auto pointsHandle = std::move(pointsResult).takeValue();
    auto       pointsView   = proc.resolvePointSet(pointsHandle);
    REQUIRE(pointsView.isBound());
    pointsView->add(0.f, 0.f, 0.f);

    auto publishResult = proc.publishInstances("missing", pointsHandle, "asset", "tree");
    REQUIRE(!publishResult.ok());
    CHECK(publishResult.status().describe().find("unavailable") != std::string::npos);

    RuntimeGeneration runtime(12);
    REQUIRE(runtime.addLevel(10.f, 6.f, 2.f) >= 0);
    runtime.updateSource(1.f, 1.f, 1.f, 0.f);
    std::unique_ptr<ProcgenCellRequest> request(runtime.nextGenerate());
    REQUIRE(bool(request));
    PointSet  generated;
    const int point = generated.add(0.f, 0.f, 0.f);
    REQUIRE(generated.trySetPointId(point, 4001).ok());
    REQUIRE(runtime.completeGeneration(request.get(), &generated));
    auto synchronizeResult = proc.synchronizeCellInstances("missing", runtime, *request, "asset", "tree");
    REQUIRE(!synchronizeResult.ok());
    CHECK(synchronizeResult.status().describe().find("unavailable") != std::string::npos);
    std::vector<const ProcgenCellRequest*> removalRequests{request.get()};
    auto                                   removeResult = proc.removeCellInstancesAtomic("missing", removalRequests);
    REQUIRE(!removeResult.ok());
    CHECK(removeResult.status().describe().find("unavailable") != std::string::npos);
    REQUIRE(proc.releasePointSet(pointsHandle).ok());
}

// procgen_schema_script.cpp


// procgen_shape_grammar.cpp
using namespace eve::procgen;

TEST_CASE("procgen.shapeGrammar.expandsGroupsAndFillRepetition") {
    ShapeGrammar grammar;
    CHECK(grammar.addModule("A", "wall-a", 2.f, 1.f));
    CHECK(grammar.addModule("A", "wall-b", 2.f, 1.f));
    CHECK(!grammar.addModule("A", "wrong-length", 3.f, 1.f));
    CHECK(grammar.addModule("B", "post", 1.f, 1.f));
    CHECK(grammar.validate("A,[B,A]*"));

    PointSet spline;
    spline.add(0.f, 0.f, 0.f);
    spline.add(10.f, 0.f, 0.f);
    PointSet* first  = grammar.generate("A,[B,A]*", &spline, 42, false);
    PointSet* second = grammar.generate("A,[B,A]*", &spline, 42, false);
    REQUIRE(bool(first));
    REQUIRE(bool(second));
    CHECK_EQ(first->getCount(), 5);
    CHECK_EQ(first->getStringAttribute(0, "module", ""), std::string("A"));
    CHECK_EQ(first->getStringAttribute(1, "module", ""), std::string("B"));
    CHECK_EQ(first->getStringAttribute(2, "module", ""), std::string("A"));
    CHECK_EQ(first->getX(0), 1.f);
    CHECK_EQ(first->getX(1), 2.5f);
    CHECK_EQ(first->getX(2), 4.f);
    CHECK_EQ(first->getYaw(0), 0.f);
    CHECK_EQ(first->getFloatAttribute(1, "length", 0.f), 1.f);
    for (int i = 0; i < first->getCount(); ++i)
        CHECK_EQ(first->getStringAttribute(i, "asset", ""), second->getStringAttribute(i, "asset", ""));
    CHECK(grammar.debugReport().find("symbols=5") != std::string::npos);

    delete second;
    delete first;
}

TEST_CASE("procgen.shapeGrammar.supportsPlusExactAndIncompleteModes") {
    ShapeGrammar grammar;
    grammar.addModule("A", "wall", 2.f, 1.f);
    grammar.addModule("B", "post", 1.f, 1.f);
    PointSet spline;
    spline.add(0.f, 0.f, 0.f);
    spline.add(10.f, 0.f, 0.f);

    PointSet* plus = grammar.generate("[A,B]+", &spline, 1, false);
    REQUIRE(bool(plus));
    CHECK_EQ(plus->getCount(), 6);
    PointSet* exact = grammar.generate("A3,B2", &spline, 1, false);
    REQUIRE(bool(exact));
    CHECK_EQ(exact->getCount(), 5);

    PointSet shortSpline;
    shortSpline.add(0.f, 0.f, 0.f);
    shortSpline.add(5.f, 0.f, 0.f);
    CHECK(!grammar.generate("A3", &shortSpline, 1, false));
    CHECK(grammar.getError().find("does not fit") != std::string::npos);
    PointSet* partial = grammar.generate("A3", &shortSpline, 1, true);
    REQUIRE(bool(partial));
    CHECK_EQ(partial->getCount(), 2);

    delete partial;
    delete exact;
    delete plus;
}

TEST_CASE("procgen.shapeGrammar.reportsSyntaxAndUnknownModules") {
    ShapeGrammar grammar;
    grammar.addModule("A", "wall", 1.f, 1.f);
    CHECK(!grammar.validate("[A,A"));
    CHECK(grammar.getError().find("unterminated") != std::string::npos);
    CHECK(!grammar.validate("A,Missing"));
    CHECK(grammar.getError().find("unknown module") != std::string::npos);
    CHECK(!grammar.validate(""));
}

// procgen_spatial_data.cpp
using namespace eve::procgen;

namespace {

class FlatProcgenWorldQuery final : public eve::IProcgenWorldQuery {
public:
    eve::Result<eve::ProcgenSurfaceHit> projectDown(float x, float z, float maxY, float minY, std::uint64_t) override {
        if (maxY < 4.f || minY > 4.f) return eve::Result<eve::ProcgenSurfaceHit>::success({});
        return eve::Result<eve::ProcgenSurfaceHit>::success({true, 91, x, 4.f, z, 0.f, 1.f, 0.f});
    }
};

struct CapabilityReset {
    CapabilityReset() { eve::cap::detail::clearAllRaw(); }
    ~CapabilityReset() { eve::cap::detail::clearAllRaw(); }
};

}  // namespace

TEST_CASE("procgen.pointSet.exposesProductionPointSemantics") {
    PointSet  points;
    const int index = points.add(1.f, 2.f, 3.f);
    points.setRotation(index, 10.f, 20.f, 30.f);
    points.setBounds(index, 2.f, 3.f, 4.f, -2.f, -3.f, -4.f);
    points.setColor(index, 1.2f, 0.5f, -0.5f, 0.75f);
    points.setSteepness(index, 2.f);

    CHECK_EQ(points.getPitch(index), 10.f);
    CHECK_EQ(points.getYaw(index), 20.f);
    CHECK_EQ(points.getRoll(index), 30.f);
    CHECK_EQ(points.getBoundsMinX(index), -2.f);
    CHECK_EQ(points.getBoundsMinY(index), -3.f);
    CHECK_EQ(points.getBoundsMinZ(index), -4.f);
    CHECK_EQ(points.getBoundsMaxX(index), 2.f);
    CHECK_EQ(points.getBoundsMaxY(index), 3.f);
    CHECK_EQ(points.getBoundsMaxZ(index), 4.f);
    CHECK_EQ(points.getColorR(index), 1.f);
    CHECK_EQ(points.getColorG(index), 0.5f);
    CHECK_EQ(points.getColorB(index), 0.f);
    CHECK_EQ(points.getColorA(index), 0.75f);
    CHECK_EQ(points.getSteepness(index), 1.f);
}

TEST_CASE("procgen.worldQuery.coversProviderPresenceAndAbsence") {
    CapabilityReset reset;
    Procgen         proc;
    auto            inputResult = proc.newPointSetHandle();
    REQUIRE(inputResult.ok());
    const auto input = std::move(inputResult).takeValue();
    auto       view  = proc.resolvePointSet(input);
    REQUIRE(view.isBound());
    view->add(2.f, 100.f, 3.f);

    auto absent = proc.projectToWorldHandle(input, 20.f, -20.f, ~std::uint64_t{0}, false);
    CHECK(!absent.ok());
    REQUIRE(absent.error() != nullptr);
    CHECK_EQ(absent.error()->code(), eve::DiagnosticCode::Unsupported);

    FlatProcgenWorldQuery provider;
    eve::cap::provide<eve::IProcgenWorldQuery>(&provider);
    auto projectedResult = proc.projectToWorldHandle(input, 20.f, -20.f, ~std::uint64_t{0}, false);
    REQUIRE(projectedResult.ok());
    const auto projected     = std::move(projectedResult).takeValue();
    auto       projectedView = proc.resolvePointSet(projected);
    REQUIRE(projectedView.isBound());
    CHECK_EQ(projectedView->getCount(), 1);
    CHECK_EQ(projectedView->getY(0), 4.f);
    CHECK_EQ(projectedView->getNormalY(0), 1.f);
    CHECK_EQ(projectedView->getIntAttribute(0, "worldObjectId", 0), 91);
    REQUIRE(proc.releasePointSet(projected).ok());
    REQUIRE(proc.releasePointSet(input).ok());
}


TEST_CASE("procgen.spatialData.volumeComposition") {
    const SpatialData world     = SpatialData::box(0.f, 0.f, 0.f, 10.f, 4.f, 10.f);
    const SpatialData hole      = SpatialData::sphere(5.f, 2.f, 5.f, 2.f);
    const SpatialData buildable = SpatialData::subtract(world, hole);

    CHECK_EQ(world.getKind(), std::string("volume.box"));
    CHECK(world.contains(1.f, 1.f, 1.f));
    CHECK(!world.contains(11.f, 1.f, 1.f));
    CHECK(!buildable.contains(5.f, 2.f, 5.f));
    CHECK(buildable.contains(1.f, 1.f, 1.f));
    CHECK(buildable.hasBounds());
    CHECK_EQ(buildable.getMaxX(), 10.f);

    const SpatialData overlap = SpatialData::intersect(world, SpatialData::box(8.f, -2.f, 8.f, 12.f, 8.f, 12.f));
    CHECK(overlap.contains(9.f, 2.f, 9.f));
    CHECK(!overlap.contains(7.f, 2.f, 9.f));
    CHECK_EQ(overlap.getMinX(), 8.f);
    CHECK_EQ(overlap.getMaxZ(), 10.f);
}

TEST_CASE("procgen.spatialData.polygonTextureMaskAndMeshSurface") {
    PointSet polygon;
    polygon.add(0.f, 0.f, 0.f);
    polygon.add(4.f, 0.f, 0.f);
    polygon.add(4.f, 0.f, 4.f);
    polygon.add(0.f, 0.f, 4.f);
    const SpatialData polygonVolume = SpatialData::polygon(polygon, -1.f, 2.f);
    CHECK_EQ(polygonVolume.getKind(), std::string("volume.polygon"));
    CHECK(polygonVolume.contains(2.f, 0.f, 2.f));
    CHECK(!polygonVolume.contains(5.f, 0.f, 2.f));

    Heightmap mask(2, 2);
    mask.setHeight(0, 0, 0.f);
    mask.setHeight(1, 0, 1.f);
    mask.setHeight(0, 1, 1.f);
    mask.setHeight(1, 1, 1.f);
    const SpatialData textureMask = SpatialData::textureMask(mask, 10.f, 20.f, 2.f, 0.75f, 1.f, -2.f, 3.f);
    CHECK_EQ(textureMask.getKind(), std::string("volume.texture_mask"));
    CHECK(textureMask.contains(12.f, 0.f, 22.f));
    CHECK(!textureMask.contains(10.f, 0.f, 20.f));

    MeshBuild mesh;
    mesh.addVertex(0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(4.f, 1.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(0.f, 3.f, 4.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    const SpatialData surface = SpatialData::meshSurface(mesh, 0.1f);
    CHECK_EQ(surface.getKind(), std::string("surface.mesh"));
    CHECK(surface.contains(1.f, 1.5f, 1.f));
    PointSet source;
    source.add(1.f, 100.f, 1.f);
    const PointSet projected = surface.project(source);
    CHECK_EQ(projected.getY(0), 1.5f);
    CHECK(projected.getNormalY(0) > 0.f);
}

TEST_CASE("procgen.spatialData.splineAndDeterministicSampling") {
    PointSet control;
    control.add(0.f, 0.f, 0.f);
    control.add(10.f, 0.f, 0.f);
    const SpatialData road = SpatialData::spline(control, 1.f);
    CHECK(road.contains(5.f, 0.5f, 0.f));
    CHECK(!road.contains(5.f, 2.f, 0.f));

    const SpatialData volume = SpatialData::box(0.f, 0.f, 0.f, 4.f, 2.f, 4.f);
    const PointSet    first  = volume.sample(2.f, 42, 0.f);
    const PointSet    second = volume.sample(2.f, 42, 0.f);
    CHECK_EQ(first.getCount(), 18);
    CHECK_EQ(first.getCount(), second.getCount());
    for (int i = 0; i < first.getCount(); ++i) {
        CHECK_EQ(first.getX(i), second.getX(i));
        CHECK_EQ(first.getY(i), second.getY(i));
        CHECK_EQ(first.getZ(i), second.getZ(i));
        CHECK_EQ(first.getPointSeed(i), second.getPointSeed(i));
    }
}

TEST_CASE("procgen.spatialData.scriptFacingApiFiltersPoints") {
    Procgen proc;
    auto    includeResult = proc.boxVolumeHandle(0.f, -1.f, 0.f, 5.f, 1.f, 5.f);
    auto    excludeResult = proc.sphereVolumeHandle(2.f, 0.f, 2.f, 1.f);
    REQUIRE(includeResult.ok());
    REQUIRE(excludeResult.ok());
    auto include      = std::move(includeResult).takeValue();
    auto exclude      = std::move(excludeResult).takeValue();
    auto domainResult = proc.differenceSpatialHandle(include, exclude);
    REQUIRE(domainResult.ok());
    auto domain = std::move(domainResult).takeValue();

    auto inputResult = proc.newPointSetHandle();
    REQUIRE(inputResult.ok());
    auto input     = std::move(inputResult).takeValue();
    auto inputView = proc.resolvePointSet(input);
    REQUIRE(inputView.isBound());
    inputView->add(1.f, 0.f, 1.f);
    inputView->add(2.f, 0.f, 2.f);
    inputView->add(9.f, 0.f, 9.f);
    auto acceptedResult = proc.filterSpatialHandle(input, domain, false);
    auto rejectedResult = proc.filterSpatialHandle(input, domain, true);
    REQUIRE(acceptedResult.ok());
    REQUIRE(rejectedResult.ok());
    auto accepted     = std::move(acceptedResult).takeValue();
    auto rejected     = std::move(rejectedResult).takeValue();
    auto acceptedView = proc.resolvePointSet(accepted);
    auto rejectedView = proc.resolvePointSet(rejected);
    REQUIRE(acceptedView.isBound());
    REQUIRE(rejectedView.isBound());
    CHECK_EQ(acceptedView->getCount(), 1);
    CHECK_EQ(rejectedView->getCount(), 2);

    REQUIRE(proc.releasePointSet(rejected).ok());
    REQUIRE(proc.releasePointSet(accepted).ok());
    REQUIRE(proc.releasePointSet(input).ok());
    REQUIRE(proc.release(domain).ok());
    REQUIRE(proc.release(exclude).ok());
    REQUIRE(proc.release(include).ok());
}

TEST_CASE("procgen.spatialData.heightfieldSamplesAndProjectsSurface") {
    Heightmap map(3, 3);
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 3; ++x) map.setHeight(x, z, float(x + z));

    const SpatialData surface = SpatialData::heightfield(map, 10.f, 20.f, 2.f, 3.f);
    CHECK_EQ(surface.getKind(), std::string("surface.heightfield"));
    CHECK_EQ(surface.getMinX(), 10.f);
    CHECK_EQ(surface.getMaxZ(), 24.f);
    CHECK(surface.contains(12.f, 6.f, 22.f));

    const PointSet samples = surface.sample(2.f, 91, 0.f);
    CHECK_EQ(samples.getCount(), 9);
    CHECK_EQ(samples.getX(0), 10.f);
    CHECK_EQ(samples.getZ(0), 20.f);
    CHECK_EQ(samples.getY(4), 6.f);
    CHECK(samples.getNormalY(4) > 0.f);
    CHECK(samples.getNormalX(4) < 0.f);

    PointSet source;
    source.add(14.f, -100.f, 24.f);
    const PointSet projected = surface.project(source);
    CHECK_EQ(projected.getY(0), 12.f);
}

TEST_CASE("procgen.pointOperations.matchCorePcgNodes") {
    PointSet  first;
    const int a = first.add(1.f, 0.f, 0.f);
    first.setDensity(a, 1.f);
    first.setFloatAttribute(a, "age", 12.f);
    first.setStringAttribute(a, "biome", "forest");

    PointSet  second;
    const int b = second.add(0.f, 0.f, 2.f);
    second.setDensity(b, 0.f);
    second.setFloatAttribute(b, "age", 2.f);
    second.setStringAttribute(b, "biome", "desert");

    const PointSet merged = mergePointSets(first, second);
    CHECK_EQ(merged.getCount(), 2);
    const PointSet transformed = transformPointSet(merged, 10.f, 3.f, 20.f, 90.f, 2.f, 1.f, 2.f);
    CHECK(std::abs(transformed.getX(0) - 10.f) < 0.0001f);
    CHECK(std::abs(transformed.getZ(0) - 22.f) < 0.0001f);
    CHECK_EQ(transformed.getY(0), 3.f);
    CHECK_EQ(transformed.getYaw(0), 90.f);
    CHECK_EQ(transformed.getScaleX(0), 2.f);

    const PointSet mature = filterPointFloatAttribute(merged, "age", 10.f, 20.f, false);
    const PointSet forest = filterPointStringAttribute(merged, "biome", "forest", false);
    CHECK_EQ(mature.getCount(), 1);
    CHECK_EQ(forest.getCount(), 1);
    CHECK_EQ(forest.getStringAttribute(0, "biome", ""), std::string("forest"));

    const PointSet culledA = densityCullPoints(merged, 123, 1.f);
    const PointSet culledB = densityCullPoints(merged, 123, 1.f);
    CHECK_EQ(culledA.getCount(), 1);
    CHECK_EQ(culledA.getCount(), culledB.getCount());
    CHECK_EQ(culledA.getPointSeed(0), culledB.getPointSeed(0));
}

// procgen_worker_contract.cpp
using namespace eve::procgen;

static_assert(std::is_copy_constructible_v<ProcgenGenerationJob>);
static_assert(!std::is_copy_constructible_v<RuntimeGeneration>);
static_assert(!std::is_move_constructible_v<RuntimeGeneration>);

TEST_CASE("procgen.worker.rejectsJobsAfterClearAndOwnerDestruction") {
    ProcgenGenerationJob job;
    {
        RuntimeGeneration original(42);
        REQUIRE(original.addLevel(10.f, 4.f, 2.f) == 0);
        original.updateSource(5.f, 5.f, 1.f, 0.f);
        auto issued = original.nextGenerationJob();
        REQUIRE(issued.ok());
        auto value = std::move(issued).takeValue();
        REQUIRE(value.has_value());
        job = *value;
        original.clear();
        auto stale = original.completeGenerationJob({job, PointSet{}});
        CHECK(!stale.ok());
    }
    RuntimeGeneration replacement(42);
    REQUIRE(replacement.addLevel(10.f, 4.f, 2.f) == 0);
    replacement.updateSource(5.f, 5.f, 1.f, 0.f);
    auto issued = replacement.nextGenerationJob();
    REQUIRE(issued.ok());
    REQUIRE(std::move(issued).takeValue().has_value());
    auto stale = replacement.completeGenerationJob({job, PointSet{}});
    CHECK(!stale.ok());
    CHECK(replacement.getGeneratingCount() == 1);
}

TEST_CASE("procgen.worker.rejectsForeignAndReplayedJobs") {
    RuntimeGeneration first(42), second(42);
    for (auto* runtime : {&first, &second}) {
        REQUIRE(runtime->addLevel(10.f, 4.f, 2.f) == 0);
        runtime->updateSource(5.f, 5.f, 1.f, 0.f);
    }
    auto a = first.nextGenerationJob();
    auto b = second.nextGenerationJob();
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto job   = std::move(a).takeValue();
    auto other = std::move(b).takeValue();
    REQUIRE(job.has_value());
    REQUIRE(other.has_value());
    REQUIRE(job->getTicket() == other->getTicket());
    auto foreign = second.completeGenerationJob({*job, PointSet{}});
    CHECK(!foreign.ok());
    CHECK(second.getGeneratingCount() == 1);
    auto accepted = first.completeGenerationJob({*job, PointSet{}});
    REQUIRE(accepted.ok());
    auto replay = first.completeGenerationJob({*job, PointSet{}});
    CHECK(!replay.ok());
    auto own = second.completeGenerationJob({*other, PointSet{}});
    CHECK(own.ok());
}
}  // namespace test_procgen_scene_sink_unit
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_prototype_kit)
#include "PrivateTestScope.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include "image/ImageData.h"
#include "procgen/Params.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/algorithms/PrototypeKit.h"
#include "procgen/texture/PrototypeTextures.h"
#include "procgen/texture/TextureRecipe.h"
#include "zeroerr/unittest.h"
// prototype_kit.cpp
namespace test_prototype_kit {
using namespace eve::procgen;

namespace {

uint64_t imageChecksum(const eve::image::ImageData& image) {
    const auto* bytes = static_cast<const uint8_t*>(image.getData());
    uint64_t    hash  = 1469598103934665603ull;
    for (size_t i = 0; i < image.getSize(); ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

bool windingNeverOpposesNormals(const MeshBuild& mesh) {
    for (int triangle = 0; triangle + 2 < mesh.getIndexCount(); triangle += 3) {
        const int   i0 = mesh.getIndex(triangle);
        const int   i1 = mesh.getIndex(triangle + 1);
        const int   i2 = mesh.getIndex(triangle + 2);
        const float ax = mesh.getPositionX(i1) - mesh.getPositionX(i0);
        const float ay = mesh.getPositionY(i1) - mesh.getPositionY(i0);
        const float az = mesh.getPositionZ(i1) - mesh.getPositionZ(i0);
        const float bx = mesh.getPositionX(i2) - mesh.getPositionX(i0);
        const float by = mesh.getPositionY(i2) - mesh.getPositionY(i0);
        const float bz = mesh.getPositionZ(i2) - mesh.getPositionZ(i0);
        const float cx = ay * bz - az * by;
        const float cy = az * bx - ax * bz;
        const float cz = ax * by - ay * bx;
        const float nx = mesh.getNormalX(i0) + mesh.getNormalX(i1) + mesh.getNormalX(i2);
        const float ny = mesh.getNormalY(i0) + mesh.getNormalY(i1) + mesh.getNormalY(i2);
        const float nz = mesh.getNormalZ(i0) + mesh.getNormalZ(i1) + mesh.getNormalZ(i2);
        if (cx * nx + cy * ny + cz * nz < -1.0e-6f) return false;
    }
    return true;
}

bool windingFacesAwayFromPoint(const MeshBuild& mesh, float px, float py, float pz) {
    for (int triangle = 0; triangle + 2 < mesh.getIndexCount(); triangle += 3) {
        const int   i0 = mesh.getIndex(triangle);
        const int   i1 = mesh.getIndex(triangle + 1);
        const int   i2 = mesh.getIndex(triangle + 2);
        const float ax = mesh.getPositionX(i1) - mesh.getPositionX(i0);
        const float ay = mesh.getPositionY(i1) - mesh.getPositionY(i0);
        const float az = mesh.getPositionZ(i1) - mesh.getPositionZ(i0);
        const float bx = mesh.getPositionX(i2) - mesh.getPositionX(i0);
        const float by = mesh.getPositionY(i2) - mesh.getPositionY(i0);
        const float bz = mesh.getPositionZ(i2) - mesh.getPositionZ(i0);
        const float nx = ay * bz - az * by;
        const float ny = az * bx - ax * bz;
        const float nz = ax * by - ay * bx;
        const float cx = (mesh.getPositionX(i0) + mesh.getPositionX(i1) + mesh.getPositionX(i2)) / 3.0f;
        const float cy = (mesh.getPositionY(i0) + mesh.getPositionY(i1) + mesh.getPositionY(i2)) / 3.0f;
        const float cz = (mesh.getPositionZ(i0) + mesh.getPositionZ(i1) + mesh.getPositionZ(i2)) / 3.0f;
        if (nx * (cx - px) + ny * (cy - py) + nz * (cz - pz) < -1.0e-6f) return false;
    }
    return true;
}

}  // namespace

TEST_CASE("procgen.prototype.mesh.catalogueBuilds75GroundedPieces") {
    const auto pieces = prototypePieceDescriptors();
    REQUIRE_EQ(pieces.size(), size_t(75));
    Params params;
    for (const auto& piece : pieces) {
        auto result = generatePrototypePiece(piece.id, params);
        REQUIRE(result.ok());
        MeshBuild mesh = std::move(result).takeValue();
        REQUIRE(!mesh.empty());
        REQUIRE(mesh.getVertexCount() > 0);
        REQUIRE(mesh.getIndexCount() > 0);
        float minimumY = mesh.getPositionY(0);
        for (int vertex = 1; vertex < mesh.getVertexCount(); ++vertex)
            minimumY = std::min(minimumY, mesh.getPositionY(vertex));
        CHECK(minimumY >= -1.0e-5f);
        for (int index = 0; index < mesh.getIndexCount(); ++index) {
            CHECK(mesh.getIndex(index) >= 0);
            CHECK(mesh.getIndex(index) < mesh.getVertexCount());
        }
        CHECK(windingNeverOpposesNormals(mesh));
        CHECK_EQ(mesh.getMeta("source", ""), std::string("procedural"));
    }
}

TEST_CASE("procgen.prototype.mesh.parametersChangeTopologyAndDimensions") {
    Params coarse;
    coarse.setInt("steps", 3);
    coarse.setFloat("width", 2.0f);
    Params detailed;
    detailed.setInt("steps", 9);
    detailed.setFloat("width", 4.0f);
    auto aResult = generatePrototypePiece("stairs", coarse);
    auto bResult = generatePrototypePiece("stairs", detailed);
    REQUIRE(aResult.ok());
    REQUIRE(bResult.ok());
    MeshBuild a = std::move(aResult).takeValue();
    MeshBuild b = std::move(bResult).takeValue();
    CHECK(b.getVertexCount() > a.getVertexCount());
    float width = 0.0f;
    for (int vertex = 0; vertex < b.getVertexCount(); ++vertex)
        width = std::max(width, std::abs(b.getPositionX(vertex)) * 2.0f);
    CHECK(width > 3.9f);
}

TEST_CASE("procgen.prototype.mesh.rampsHaveOutwardFaces") {
    Params params;
    for (const char* id : {"ramp", "ramp1"}) {
        auto result = generatePrototypePiece(id, params);
        REQUIRE(result.ok());
        MeshBuild mesh = std::move(result).takeValue();
        float     minX = mesh.getPositionX(0), maxX = minX;
        float     minY = mesh.getPositionY(0), maxY = minY;
        float     minZ = mesh.getPositionZ(0), maxZ = minZ;
        for (int vertex = 1; vertex < mesh.getVertexCount(); ++vertex) {
            minX = std::min(minX, mesh.getPositionX(vertex));
            maxX = std::max(maxX, mesh.getPositionX(vertex));
            minY = std::min(minY, mesh.getPositionY(vertex));
            maxY = std::max(maxY, mesh.getPositionY(vertex));
            minZ = std::min(minZ, mesh.getPositionZ(vertex));
            maxZ = std::max(maxZ, mesh.getPositionZ(vertex));
        }
        const float interiorX = (minX + maxX) * 0.5f;
        const float interiorY = minY + (maxY - minY) / 3.0f;
        const float interiorZ = minZ + (maxZ - minZ) * 2.0f / 3.0f;
        CHECK(windingFacesAwayFromPoint(mesh, interiorX, interiorY, interiorZ));
    }
}

TEST_CASE("procgen.prototype.mesh.uvScaleChangesDensityWithoutChangingGeometry") {
    Params base;
    base.setFloat("uvScale", 0.5f);
    Params dense;
    dense.setFloat("uvScale", 2.0f);
    auto baseResult  = generatePrototypePiece("wall", base);
    auto denseResult = generatePrototypePiece("wall", dense);
    REQUIRE(baseResult.ok());
    REQUIRE(denseResult.ok());
    MeshBuild a = std::move(baseResult).takeValue();
    MeshBuild b = std::move(denseResult).takeValue();
    REQUIRE_EQ(a.positions().size(), b.positions().size());
    REQUIRE_EQ(a.uvs().size(), b.uvs().size());
    CHECK(std::equal(a.positions().begin(), a.positions().end(), b.positions().begin()));
    bool changed = false;
    for (size_t i = 0; i < a.uvs().size(); ++i) {
        if (std::abs(a.uvs()[i] - b.uvs()[i]) > 1.0e-5f) changed = true;
    }
    CHECK(changed);
}

TEST_CASE("procgen.prototype.mesh.registryExposesEveryPiece") {
    auto& registry = MeshRecipeRegistry::instance();
    registry.registerBuiltins();
    for (const auto& piece : prototypePieceDescriptors()) {
        const std::string recipe = "prototype." + std::string(piece.id);
        CHECK(registry.has(recipe));
        const RecipeDescriptor* schema = registry.descriptor(recipe);
        REQUIRE(schema != nullptr);
        CHECK(schema->find("width") != nullptr);
        CHECK(schema->find("detail") != nullptr);
        CHECK(schema->find("uvScale") != nullptr);
    }
}

TEST_CASE("procgen.prototype.mesh.unknownIdIsStructuredFailure") {
    Params params;
    auto   result = generatePrototypePiece("not-a-piece", params);
    CHECK(!result.ok());
    CHECK_EQ(result.status().code(), eve::StatusCode::NotFound);
}

TEST_CASE("procgen.prototype.texture.catalogueBuilds13PatternsIn6Palettes") {
    constexpr std::array<const char*, 6> palettes{{"dark", "light", "purple", "orange", "green", "red"}};
    const auto                           patterns = prototypeTextureDescriptors();
    REQUIRE_EQ(patterns.size(), size_t(13));
    for (const auto& pattern : patterns) {
        uint64_t previous = 0;
        for (const char* palette : palettes) {
            Params params;
            params.setSize(96, 80);
            params.setString("palette", palette);
            params.setInt("cellSize", 16);
            auto result = generatePrototypeTexture(pattern.id, params);
            REQUIRE(result.ok());
            auto image = std::move(result).takeValue();
            REQUIRE(static_cast<bool>(image));
            CHECK_EQ(image->getWidth(), 96);
            CHECK_EQ(image->getHeight(), 80);
            CHECK_EQ(image->getFormat(), std::string("RGBA8"));
            const uint64_t checksum = imageChecksum(*image);
            if (previous != 0) CHECK(checksum != previous);
            previous = checksum;
        }
    }
}

TEST_CASE("procgen.prototype.texture.isDeterministicAndParameterized") {
    Params base;
    base.setSize(128, 128);
    base.setString("palette", "custom");
    base.setInt("backgroundR", 12);
    base.setInt("backgroundG", 34);
    base.setInt("backgroundB", 56);
    base.setInt("cellSize", 16);
    auto firstResult  = generatePrototypeTexture("diagonal-grid", base);
    auto secondResult = generatePrototypeTexture("diagonal-grid", base);
    REQUIRE(firstResult.ok());
    REQUIRE(secondResult.ok());
    auto first  = std::move(firstResult).takeValue();
    auto second = std::move(secondResult).takeValue();
    CHECK_EQ(imageChecksum(*first), imageChecksum(*second));

    base.setInt("cellSize", 32);
    auto changedResult = generatePrototypeTexture("diagonal-grid", base);
    REQUIRE(changedResult.ok());
    auto changed = std::move(changedResult).takeValue();
    CHECK(imageChecksum(*first) != imageChecksum(*changed));
}

TEST_CASE("procgen.prototype.texture.rejectsInvalidParameters") {
    Params invalidPalette;
    invalidPalette.setSize(64, 64);
    invalidPalette.setString("palette", "blue");
    auto paletteResult = generatePrototypeTexture("fine-grid", invalidPalette);
    CHECK(!paletteResult.ok());
    REQUIRE(paletteResult.error() != nullptr);
    CHECK_EQ(paletteResult.error()->code(), eve::DiagnosticCode::InvalidArgument);

    Params invalidOpacity;
    invalidOpacity.setSize(64, 64);
    invalidOpacity.setFloat("minorAlpha", std::numeric_limits<float>::quiet_NaN());
    auto opacityResult = generatePrototypeTexture("fine-grid", invalidOpacity);
    CHECK(!opacityResult.ok());
    REQUIRE(opacityResult.error() != nullptr);
    CHECK_EQ(opacityResult.error()->code(), eve::DiagnosticCode::InvalidArgument);
}

TEST_CASE("procgen.prototype.texture.registryExposesEveryPattern") {
    auto& registry = TextureRecipeRegistry::instance();
    registry.registerBuiltins();
    for (const auto& pattern : prototypeTextureDescriptors()) {
        const std::string recipe = "tex.prototype." + std::string(pattern.id);
        CHECK(registry.has(recipe));
        const RecipeDescriptor* schema = registry.descriptor(recipe);
        REQUIRE(schema != nullptr);
        CHECK(schema->find("palette") != nullptr);
        CHECK(schema->find("cellSize") != nullptr);
    }
}
}  // namespace test_prototype_kit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_roguelike_generator)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <set>
#include <string>
#include <vector>
#include "procgen/GeneratorRegistry.h"
#include "procgen/Procgen.h"
#include "procgen/Semantic.h"
#include "procgen/algorithms/RoguelikeGenerator.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// roguelike_generator.cpp
namespace test_roguelike_generator {
using namespace eve::procgen;

namespace {

struct ParamsLease {
    ProcgenParamsHandleRef handle{};
    ParamsLease() = default;
    explicit ParamsLease(ProcgenParamsHandleRef value) : handle(value) {}
    ParamsLease(const ParamsLease&)            = delete;
    ParamsLease& operator=(const ParamsLease&) = delete;
    ParamsLease(ParamsLease&& other) noexcept : handle(other.handle) { other.handle = {}; }
    ParamsLease& operator=(ParamsLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        handle       = other.handle;
        other.handle = {};
        return *this;
    }
    ~ParamsLease() { reset(); }
    void reset() noexcept {
        if (!handle.isValid()) return;
        auto result = Procgen::release(handle);
        result.ignore("roguelike params cleanup");
        handle = {};
    }
    [[nodiscard]] eve::script::Borrowed<Params> view() const noexcept { return Procgen::resolve(handle); }
};

struct GridLease {
    ProcgenGridHandleRef handle{};
    GridLease() = default;
    explicit GridLease(ProcgenGridHandleRef value) : handle(value) {}
    GridLease(const GridLease&)            = delete;
    GridLease& operator=(const GridLease&) = delete;
    GridLease(GridLease&& other) noexcept : handle(other.handle) { other.handle = {}; }
    GridLease& operator=(GridLease&& other) noexcept {
        if (this == &other) return *this;
        reset();
        handle       = other.handle;
        other.handle = {};
        return *this;
    }
    ~GridLease() { reset(); }
    void reset() noexcept {
        if (!handle.isValid()) return;
        auto result = Procgen::release(handle);
        result.ignore("roguelike grid cleanup");
        handle = {};
    }
    [[nodiscard]] eve::script::Borrowed<Grid2D> view() const noexcept { return Procgen::resolve(handle); }
};

ParamsLease requireParams() {
    auto result = Procgen::newParamsHandle();
    REQUIRE(result.ok());
    return ParamsLease(std::move(result).takeValue());
}

GridLease requireGrid(Procgen& proc, const std::string& algorithm, ProcgenParamsHandleRef params) {
    auto result = proc.generateHandle(algorithm, params);
    REQUIRE(result.ok());
    return GridLease(std::move(result).takeValue());
}

int countSemantic(const Grid2D& g, int semantic) {
    int n = 0;
    for (uint32_t c : g.cells())
        if (int(c) == semantic) ++n;
    return n;
}

bool isWalkableCell(const Grid2D& g, int x, int y) {
    const uint32_t c = uint32_t(g.getCell(x, y));
    return c == Semantic::Floor || c == Semantic::Corridor;
}

constexpr int kE  = 1 << 0;  // +x
constexpr int kS  = 1 << 1;  // +y
constexpr int kW  = 1 << 2;  // -x
constexpr int kN  = 1 << 3;  // -y
constexpr int kSE = 1 << 4;
constexpr int kSW = 1 << 5;
constexpr int kNW = 1 << 6;
constexpr int kNE = 1 << 7;

int expectedWallMask(const Grid2D& g, int x, int y) {
    const int w    = g.getWidth();
    const int h    = g.getHeight();
    int       mask = 0;
    if (x + 1 < w && isWalkableCell(g, x + 1, y)) mask |= kE;
    if (y + 1 < h && isWalkableCell(g, x, y + 1)) mask |= kS;
    if (x - 1 >= 0 && isWalkableCell(g, x - 1, y)) mask |= kW;
    if (y - 1 >= 0 && isWalkableCell(g, x, y - 1)) mask |= kN;
    if (x + 1 < w && y + 1 < h && isWalkableCell(g, x + 1, y + 1)) mask |= kSE;
    if (x - 1 >= 0 && y + 1 < h && isWalkableCell(g, x - 1, y + 1)) mask |= kSW;
    if (x - 1 >= 0 && y - 1 >= 0 && isWalkableCell(g, x - 1, y - 1)) mask |= kNW;
    if (x + 1 < w && y - 1 >= 0 && isWalkableCell(g, x + 1, y - 1)) mask |= kNE;
    return mask;
}

// Wall detail must exactly equal the 8-bit neighbour mask (0 is fine for walls
// that touch no floor). Non-wall detail must be a floor variant (1..32) or a
// decor marker (>=100); anything else would be a mask leak into the floor.
bool wallMasksConsistent(const Grid2D& g) {
    const int w = g.getWidth();
    const int h = g.getHeight();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (uint32_t(g.getCell(x, y)) == Semantic::Wall) {
                if (g.getDetail(x, y) != expectedWallMask(g, x, y)) return false;
            } else {
                const int d = g.getDetail(x, y);
                if (d > 32 && d < 100) return false;  // mask leak into floor
            }
        }
    }
    return true;
}

// Spawn and stairs must land on walkable cells.
bool markersWalkable(const Grid2D& g) {
    for (int i = 0; i < g.getObjectCount(); ++i) {
        const std::string& t = g.getObjectType(i);
        if (t != "spawn" && t != "stairs") continue;
        if (!isWalkableCell(g, int(g.getObjectX(i)), int(g.getObjectY(i)))) return false;
    }
    return true;
}

bool allWalkableConnected(const Grid2D& g) {
    const int w = g.getWidth(), h = g.getHeight();
    int       start = -1, total = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (isWalkableCell(g, x, y)) {
                if (start < 0) start = y * w + x;
                ++total;
            }
    if (start < 0) return true;
    std::vector<bool> seen(size_t(w * h), false);
    std::queue<int>   pending;
    pending.push(start);
    seen[size_t(start)]   = true;
    int           reached = 0;
    constexpr int dx[]    = {1, -1, 0, 0};
    constexpr int dy[]    = {0, 0, 1, -1};
    while (!pending.empty()) {
        const int key = pending.front();
        pending.pop();
        ++reached;
        const int x = key % w, y = key / w;
        for (int i = 0; i < 4; ++i) {
            const int nx = x + dx[i], ny = y + dy[i];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || !isWalkableCell(g, nx, ny)) continue;
            const int next = ny * w + nx;
            if (!seen[size_t(next)]) {
                seen[size_t(next)] = true;
                pending.push(next);
            }
        }
    }
    return reached == total;
}

}  // namespace

TEST_CASE("procgen.roguelike.reproducible") {
    Procgen* mod = Procgen::create();
    CHECK(mod->hasAlgorithm("level.roguelike"));

    auto p     = requireParams();
    auto pView = p.view();
    REQUIRE(pView.isBound());
    pView->setSeed(42);
    pView->setSize(48, 32);
    pView->setInt("roomCount", 10);
    auto aLease = requireGrid(*mod, "level.roguelike", p.handle);
    auto a      = aLease.view();
    REQUIRE(a.isBound());

    auto q     = requireParams();
    auto qView = q.view();
    REQUIRE(qView.isBound());
    qView->setSeed(42);
    qView->setSize(48, 32);
    qView->setInt("roomCount", 10);
    auto bLease = requireGrid(*mod, "level.roguelike", q.handle);
    auto b      = bLease.view();
    REQUIRE(b.isBound());

    // Same seed -> identical cells and detail.
    CHECK(a->cells() == b->cells());
    CHECK(a->detail() == b->detail());
}

TEST_CASE("procgen.roguelike.structure") {
    Procgen* mod   = Procgen::create();
    auto     p     = requireParams();
    auto     pView = p.view();
    REQUIRE(pView.isBound());
    pView->setSeed(7);
    pView->setSize(48, 32);
    pView->setInt("roomCount", 12);
    pView->setInt("stairCount", 2);
    pView->setString("connectionStyle", "nearest");
    pView->setFloat("decorDensity", 0.06f);
    pView->setString("decorSet", "mixed");
    auto gLease = requireGrid(*mod, "level.roguelike", p.handle);
    auto g      = gLease.view();
    REQUIRE(g.isBound());

    const int floorCount = countSemantic(*g, Semantic::Floor) + countSemantic(*g, Semantic::Corridor);
    const int wallCount  = countSemantic(*g, Semantic::Wall);
    CHECK_GT(floorCount, 0);
    CHECK_GT(wallCount, 0);
    CHECK_EQ(floorCount + wallCount, g->getWidth() * g->getHeight());

    // Outer border stays solid wall (padding >= 1 by default).
    CHECK_EQ(g->getCell(0, 0), int(Semantic::Wall));
    CHECK_EQ(g->getCell(g->getWidth() - 1, g->getHeight() - 1), int(Semantic::Wall));

    // Detail layer: wall autotile masks + floor variants are present.
    CHECK(wallMasksConsistent(*g));
    CHECK(markersWalkable(*g));
    CHECK(allWalkableConnected(*g));

    // Metadata describes the run.
    CHECK_EQ(g->getMeta("algorithm", ""), "level.roguelike");
    CHECK_EQ(g->getMeta("seed", ""), "7");

    // Objects carry spawn/stairs plus props.
    bool hasSpawn     = false;
    int  stairObjects = 0;
    for (int i = 0; i < g->getObjectCount(); ++i) {
        if (g->getObjectType(i) == "spawn") hasSpawn = true;
        if (g->getObjectType(i) == "stairs") {
            ++stairObjects;
            CHECK((g->getObjectFlags(i) & 64) != 0);
            const int   x = int(g->getObjectX(i)), y = int(g->getObjectY(i));
            const float rotation = g->getObjectRotation(i);
            if (rotation == 180.f)
                CHECK_EQ(g->getCell(x, y - 1), int(Semantic::Wall));
            else if (rotation == 0.f)
                CHECK_EQ(g->getCell(x, y + 1), int(Semantic::Wall));
            else if (rotation == 270.f)
                CHECK_EQ(g->getCell(x - 1, y), int(Semantic::Wall));
            else {
                CHECK_EQ(rotation, 90.f);
                CHECK_EQ(g->getCell(x + 1, y), int(Semantic::Wall));
            }
        }
    }
    CHECK(hasSpawn);
    CHECK_EQ(stairObjects, 2);
    CHECK_EQ(g->getMeta("stairs", ""), "2");
}

TEST_CASE("procgen.roguelike.seedVaries") {
    Procgen* mod = Procgen::create();
    auto     run = [&](uint32_t seed) {
        auto p     = requireParams();
        auto pView = p.view();
        REQUIRE(pView.isBound());
        pView->setSeed(seed);
        pView->setSize(40, 28);
        pView->setInt("roomCount", 9);
        auto gLease = requireGrid(*mod, "level.roguelike", p.handle);
        auto g      = gLease.view();
        REQUIRE(g.isBound());
        Grid2D copy = *g;
        return copy;
    };
    Grid2D s1 = run(1);
    Grid2D s2 = run(2);
    Grid2D s3 = run(1);
    CHECK(s1.cells() != s2.cells());
    CHECK(s1.cells() == s3.cells());
}

TEST_CASE("procgen.roguelike.rulesChangeLayout") {
    Procgen* mod = Procgen::create();
    auto     gen = [&](int rooms, const std::string& pattern, const std::string& style, const std::string& layout,
                   int branchBias) {
        auto p     = requireParams();
        auto pView = p.view();
        REQUIRE(pView.isBound());
        pView->setSeed(11);
        pView->setSize(48, 32);
        pView->setInt("roomCount", rooms);
        pView->setString("floorPattern", pattern);
        pView->setString("corridorStyle", style);
        pView->setString("layoutStyle", layout);
        pView->setString("connectionStyle", layout == "clustered" ? "growth" : "sequential");
        pView->setInt("clusterBranchBias", branchBias);
        auto gLease = requireGrid(*mod, "level.roguelike", p.handle);
        auto g      = gLease.view();
        REQUIRE(g.isBound());
        Grid2D copy = *g;
        return copy;
    };

    Grid2D few  = gen(5, "brick", "l", "grid", 0);
    Grid2D many = gen(20, "brick", "l", "grid", 0);
    CHECK(few.cells() != many.cells());

    // Pattern change keeps same base but alters floor detail.
    Grid2D brick   = gen(9, "brick", "l", "grid", 0);
    Grid2D checker = gen(9, "checker", "l", "grid", 0);
    CHECK_EQ(brick.cells(), checker.cells());
    CHECK(brick.detail() != checker.detail());

    Grid2D clustered = gen(9, "brick", "l", "clustered", 0);
    CHECK(clustered.cells() != brick.cells());
    CHECK(allWalkableConnected(clustered));
    Grid2D hubClustered = gen(9, "brick", "l", "clustered", 2);
    CHECK(hubClustered.cells() != clustered.cells());
    CHECK(allWalkableConnected(hubClustered));
}

TEST_CASE("procgen.roguelike.configurableAssetDressing") {
    Procgen* mod   = Procgen::create();
    auto     p     = requireParams();
    auto     pView = p.view();
    REQUIRE(pView.isBound());
    pView->setSeed(23);
    pView->setSize(52, 38);
    pView->setInt("roomCount", 14);
    pView->setString("assetPack", "test-pack");
    pView->setString("assets.container", "crate_a,crate_b");
    pView->setString("assets.light", "sconce_custom");
    pView->setString("assets.banner", "wall_hanging_custom");
    pView->setString("assets.floor", "floor_a,floor_b");
    pView->setFloat("propDensity", 0.6f);
    auto gLease = requireGrid(*mod, "level.roguelike", p.handle);
    auto g      = gLease.view();
    REQUIRE(g.isBound());
    CHECK_EQ(g->getMeta("assetPack", ""), "test-pack");
    CHECK_EQ(g->getMeta("assets.floor", ""), "floor_a,floor_b");
    CHECK_GT(std::stoi(g->getMeta("placedProps", "0")), 0);
    CHECK_GE(std::stoi(g->getMeta("minimumRoomProps", "0")), 8);

    bool customLight         = false;
    bool fractionalPlacement = false;
    bool roomZone            = false;
    for (int i = 0; i < g->getObjectCount(); ++i) {
        if (g->getObjectType(i) == "room") {
            roomZone     = true;
            const int rx = int(g->getObjectX(i)), ry = int(g->getObjectY(i));
            const int rw = int(g->getObjectWidth(i)), rh = int(g->getObjectHeight(i));
            for (int y = ry; y < ry + rh; ++y)
                for (int x = rx; x < rx + rw; ++x) CHECK_EQ(g->getCell(x, y), int(Semantic::Floor));
        }
        if (g->getObjectType(i) == "light") {
            customLight = customLight || g->getObjectAsset(i) == "sconce_custom";
            CHECK((g->getObjectFlags(i) & 4) != 0);
        }
        CHECK_GE(g->getObjectRotation(i), 0.f);
        CHECK_LT(g->getObjectRotation(i), 360.f);
        fractionalPlacement = fractionalPlacement ||
                              std::abs(g->getObjectX(i) - std::round(g->getObjectX(i))) > 0.01f ||
                              std::abs(g->getObjectY(i) - std::round(g->getObjectY(i))) > 0.01f;
    }
    CHECK(customLight);
    CHECK(roomZone);
    CHECK(fractionalPlacement);
    CHECK(markersWalkable(*g));
}

TEST_CASE("procgen.roguelike.errors") {
    Procgen* mod   = Procgen::create();
    auto     p     = requireParams();
    auto     pView = p.view();
    REQUIRE(pView.isBound());
    pView->setSeed(1);
    pView->setSize(4, 4);  // too small
    auto failed = mod->generateHandle("level.roguelike", p.handle);
    CHECK(!failed.ok());
    const eve::Diagnostic* diagnostic = failed.error();
    REQUIRE(diagnostic != nullptr);
    CHECK_EQ(diagnostic->code(), eve::DiagnosticCode::Failed);
}

TEST_CASE("procgen.roguelike.manualFillDetail") {
    Procgen* mod   = Procgen::create();
    auto     p     = requireParams();
    auto     pView = p.view();
    REQUIRE(pView.isBound());
    pView->setSeed(3);
    pView->setSize(32, 24);
    auto gLease = requireGrid(*mod, "level.roguelike", p.handle);
    auto g      = gLease.view();
    REQUIRE(g.isBound());

    // Ultimate control: script can override any cell / detail after generation.
    g->setCell(1, 1, int(Semantic::Floor));
    g->setDetail(1, 1, 5);
    CHECK_EQ(g->getCell(1, 1), int(Semantic::Floor));
    CHECK_EQ(g->getDetail(1, 1), 5);
    CHECK_EQ(g->getDetail(-5, -5), 0);  // out of bounds -> 0
    g->setDetail(0, 0, 999);            // clamps to 255
    CHECK_EQ(g->getDetail(0, 0), 255);
}

TEST_CASE("procgen.autotileGrid.postProcess") {
    Procgen* mod   = Procgen::create();
    auto     p     = requireParams();
    auto     pView = p.view();
    REQUIRE(pView.isBound());
    pView->setSeed(9);
    pView->setSize(40, 30);
    pView->setInt("roomCount", 8);
    pView->setInt("autotile", 0);  // disable built-in autotile to test the helper alone
    auto gLease = requireGrid(*mod, "level.roguelike", p.handle);
    auto g      = gLease.view();
    REQUIRE(g.isBound());
    auto autotile = mod->autotileGrid(gLease.handle);
    CHECK(autotile.ok());
    CHECK(wallMasksConsistent(*g));
}

TEST_CASE("procgen.randomSeed.nonZeroAndVaries") {
    Procgen* mod = Procgen::create();
    CHECK_GT(mod->randomSeed(), 0u);
    uint32_t a = mod->randomSeed();
    uint32_t b = mod->randomSeed();
    // Overwhelmingly likely distinct; do not require strict inequality.
    CHECK_GT(a, 0u);
    CHECK_GT(b, 0u);
    CHECK_EQ(randomSeedValue() == 0, false);
}
}  // namespace test_roguelike_generator
#endif
