#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_incremental_build)
#include "PrivateTestScope.h"
#include "procgen/IncrementalBuild.h"
#include "procgen/Semantic.h"

namespace test_procgen_incremental_build_unit {
using namespace eve::procgen;

bool hasChange(const IncrementalBuildDelta &delta, int x, int z, bool removed) {
    for (int i = 0; i < delta.getCount(); ++i)
        if (delta.getClusterX(i) == x && delta.getClusterZ(i) == z && delta.isRemoved(i) == removed) return true;
    return false;
}

// update(stack, grid, points, clusterSize, cellSize): the first call emits
// every occupied cluster, a repeated call emits nothing, and a local edit
// emits only the affected cluster. A border edit includes the neighbor halo.
TEST_CASE("procgen.incrementalBuild.upsert_noop_local_edit_and_halo") {
    Grid2D grid;
    grid.resize(8, 2);
    grid.setCell(1, 0, int(Semantic::Floor));
    grid.setCell(5, 0, int(Semantic::Floor));
    PointSet        points;
    BuildLayerStack stack;
    REQUIRE(stack.addTileLayer("floor", true, 1.f, 0.f, "floor").ok());
    IncrementalBuildExecutor executor;
    auto                     first = executor.update(stack, grid, points, 4, 1.f);
    REQUIRE(first.ok());
    CHECK_EQ(first.value().getCount(), 2);
    CHECK(hasChange(first.value(), 0, 0, false));
    CHECK(hasChange(first.value(), 1, 0, false));
    auto repeated = executor.update(stack, grid, points, 4, 1.f);
    REQUIRE(repeated.ok());
    CHECK_EQ(repeated.value().getCount(), 0);
    grid.setDetail(1, 0, 4);
    auto local = executor.update(stack, grid, points, 4, 1.f);
    REQUIRE(local.ok());
    CHECK_EQ(local.value().getCount(), 1);
    CHECK(hasChange(local.value(), 0, 0, false));
    grid.setCell(3, 0, int(Semantic::Floor));
    auto border = executor.update(stack, grid, points, 4, 1.f);
    REQUIRE(border.ok());
    CHECK_EQ(border.value().getCount(), 2);
    CHECK(hasChange(border.value(), 1, 0, false));
}

// Empty clusters are reported as removals. Invalid sizing must fail before
// changing the previously committed cache.
TEST_CASE("procgen.incrementalBuild.removal_and_rejected_update_are_transactional") {
    Grid2D grid;
    grid.resize(4, 1);
    grid.setCell(0, 0, int(Semantic::Floor));
    PointSet        points;
    BuildLayerStack stack;
    REQUIRE(stack.addTileLayer("floor", true, 1.f, 0.f, "floor").ok());
    IncrementalBuildExecutor executor;
    REQUIRE(executor.update(stack, grid, points, 4, 1.f).ok());
    CHECK_EQ(executor.getCachedClusterCount(), 1);
    CHECK(!executor.update(stack, grid, points, 0, 1.f).ok());
    CHECK(!executor.update(stack, grid, points, 4, 0.f).ok());
    CHECK_EQ(executor.getCachedClusterCount(), 1);
    CHECK(executor.getCachedArtifacts(0, 0).ok());
    grid.setCell(0, 0, int(Semantic::Empty));
    auto removed = executor.update(stack, grid, points, 4, 1.f);
    REQUIRE(removed.ok());
    CHECK(hasChange(removed.value(), 0, 0, true));
    CHECK_EQ(executor.getCachedClusterCount(), 0);
}
}  // namespace test_procgen_incremental_build_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_module_assembly)
#include "PrivateTestScope.h"
#include "procgen/ModuleAssembly.h"

#include <array>
#include <limits>
#include <string>

namespace test_procgen_module_assembly_unit {
using namespace eve::procgen;

// A plan accepts valid placements in order. Occupancy, bounds and connection
// errors return a failure without appending any placement.
TEST_CASE("procgen.moduleAssembly.placement_validation_is_atomic") {
    ModuleAssemblyConstraints constraints;
    constraints.bounded  = true;
    constraints.minCellX = 0;
    constraints.maxCellX = 3;
    constraints.minCellZ = 0;
    constraints.maxCellZ = 3;
    ModuleAssemblyPlan plan(constraints);
    REQUIRE(
        plan.registerModule({"road",
                             1,
                             1,
                             {{0, 0, ModuleFacing::East, "road", "road"}, {0, 0, ModuleFacing::West, "road", "road"}}})
            .ok());
    REQUIRE(plan.place({"road", 1, 1, 0, 0}).ok());
    REQUIRE(plan.place({"road", 2, 1, 0, 0}).ok());
    CHECK_EQ(plan.getPlacementCount(), 2);
    CHECK(!plan.place({"road", 2, 1, 0, 0}).ok());  // occupied
    CHECK_EQ(plan.getPlacementCount(), 2);
    CHECK(!plan.place({"road", 4, 1, 0, 0}).ok());  // out of bounds
    CHECK_EQ(plan.getPlacementCount(), 2);
    CHECK(!plan.place({"road", 0, 3, 0, 0}).ok());  // disconnected
    CHECK_EQ(plan.getPlacementCount(), 2);
}

// Rotations and support ratio are setup-time policy. A failed placement must
// leave those policies and the existing placement ledger unchanged.
TEST_CASE("procgen.moduleAssembly.rotation_support_and_overflow_rejections") {
    ModuleAssemblyPlan plan;
    REQUIRE(plan.setAllowedQuarterTurns(true, false, true, false).ok());
    REQUIRE(plan.registerModuleType("tile", 1, 1).ok());
    REQUIRE(plan.place({"tile", 0, 0, 0, 0}).ok());
    CHECK(!plan.place({"tile", 1, 0, 0, 1}).ok());
    CHECK_EQ(plan.getPlacementCount(), 1);
    CHECK(!plan.setAllowedQuarterTurns(true, true, true, true).ok());

    ModuleAssemblyConstraints roomy;
    roomy.requireConnection = false;
    roomy.maxLevels         = std::numeric_limits<int>::max();
    ModuleAssemblyPlan overflow(roomy);
    REQUIRE(overflow.registerVolumeModuleType("wide", 2, 1, 2).ok());
    CHECK(!overflow.place({"wide", std::numeric_limits<int>::max(), 0, 0, 0}).ok());
    CHECK(!overflow.place({"wide", 0, 0, std::numeric_limits<int>::max(), 0}).ok());
    CHECK_EQ(overflow.getPlacementCount(), 0);

    ModuleAssemblyPlan support;
    REQUIRE(support.setMinimumSupportRatio(1.f).ok());
    CHECK_EQ(support.constraints().minimumSupportRatio, 1.f);
    CHECK(!support.setMinimumSupportRatio(1.5f).ok());
    CHECK_EQ(support.constraints().minimumSupportRatio, 1.f);
}

// JSON configuration has a fixed schema/version and rejects unknown fields.
// An invalid document must leave an empty plan configurable afterward.
TEST_CASE("procgen.moduleAssembly.versioned_json_acceptance_and_failure") {
    const std::string  json = R"({"schema":"eve.procgen.module-assembly","version":1,
        "unknownFields":"reject","constraints":{"cellSize":8.0,"floorHeight":4.0,
        "bounds":{"enabled":false,"minCellX":0,"maxCellX":0,"minCellZ":0,"maxCellZ":0},
        "maxLevels":1,"allowedQuarterTurns":[true,true,true,true],
        "requireConnection":false,"minimumSupportRatio":0.0},
        "modules":[{"id":"tile","widthCells":1,"depthCells":1,"heightLevels":1,"connectors":[]}]})";
    ModuleAssemblyPlan plan;
    REQUIRE(plan.applyConfigJson(json).ok());
    CHECK_EQ(plan.constraints().cellSize, 8.f);
    REQUIRE(plan.place({"tile", 0, 0, 0, 0}).ok());
    ModuleAssemblyPlan rejected;
    auto               unknown = json;
    unknown.replace(unknown.find("\"modules\""), 9, "\"unknown\"");
    CHECK(!rejected.applyConfigJson(unknown).ok());
    REQUIRE(rejected.registerModuleType("still-empty", 1, 1).ok());
}
}  // namespace test_procgen_module_assembly_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_object_build_layer)
#include "PrivateTestScope.h"
#include "procgen/ObjectBuildLayer.h"

#include <string>

namespace test_procgen_object_build_layer {
using namespace eve::procgen;

// One source point emits a parent plus the configured child count. The
// parent keeps identity and receives fixed transforms and surface placement.
TEST_CASE("procgen.objectBuild.parent_child_output_and_repeatable_seed") {
    PointSet  source;
    const int point = source.add(2.f, 3.f, 4.f);
    REQUIRE(source.trySetPointId(point, 42).ok());
    REQUIRE(source.trySetFloatAttribute(point, "surface_highest_y", 10.f).ok());
    ObjectBuildLayer layer;
    REQUIRE(layer.addAsset("tree", 1.f).ok());
    layer.setSeed(77);
    layer.setLayerOffset(1.f, 2.f, 3.f);
    REQUIRE(layer.setLayerScale(2.f, 3.f, 4.f).ok());
    REQUIRE(layer.setRandomRotation(5.f, 5.f, 7.f, 7.f, 9.f, 9.f).ok());
    layer.setPlaceOnTop(true, false, .25f);
    REQUIRE(layer.addChild("leaf", 2, 0.f, 1.f, 1.f, 0.f, 0.f).ok());
    const auto first    = layer.build(source);
    const auto repeated = layer.build(source);
    REQUIRE(first.ok());
    REQUIRE(repeated.ok());
    CHECK_EQ(first.value().getCount(), 3);
    CHECK_EQ(first.value().getStringAttribute(0, "asset", ""), std::string("tree"));
    CHECK_EQ(first.value().getStringAttribute(0, "object_role", ""), std::string("parent"));
    CHECK_EQ(first.value().getX(0), 3.f);
    CHECK_EQ(first.value().getY(0), 12.25f);
    CHECK_EQ(first.value().getZ(0), 7.f);
    CHECK_EQ(first.value().getPitch(0), 5.f);
    CHECK_EQ(first.value().getYaw(0), 7.f);
    CHECK_EQ(first.value().getRoll(0), 9.f);
    for (int i = 1; i < 3; ++i) {
        CHECK_EQ(first.value().getStringAttribute(i, "object_role", ""), std::string("child"));
        CHECK_EQ(first.value().getIntAttribute(i, "parent_index", -1), 0);
        CHECK_EQ(first.value().getX(i), repeated.value().getX(i));
        CHECK_EQ(first.value().getY(i), repeated.value().getY(i));
    }
}

// Invalid parameters fail at configuration time. Definition import rejects
// malformed data without replacing a previously usable layer.
TEST_CASE("procgen.objectBuild.validation_and_definition_transaction") {
    PointSet points;
    points.add(0.f, 0.f, 0.f);
    ObjectBuildLayer layer;
    CHECK(!layer.build(points).ok());
    CHECK(!layer.addAsset("", 1.f).ok());
    CHECK(!layer.addAsset("tree", 0.f).ok());
    CHECK(!layer.setLayerScale(1.f, 0.f, 1.f).ok());
    CHECK(!layer.setPositionRadius(-1.f).ok());
    CHECK(!layer.setOrientation(0.f, 0.f, false).ok());
    CHECK(!layer.addChild("leaf", -1, 0.f, 1.f, 1.f, 0.f, 0.f).ok());
    REQUIRE(layer.addAsset("tree", 1.f).ok());
    auto before = layer.serializeDefinition();
    REQUIRE(!before.empty());
    CHECK(!layer.deserializeDefinition("{malformed").ok());
    CHECK_EQ(layer.serializeDefinition(), before);
    ObjectBuildLayer restored;
    REQUIRE(restored.deserializeDefinition(before).ok());
    CHECK(restored.build(points).ok());
}
}  // namespace test_procgen_object_build_layer
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_runtime_generation)
#include "PrivateTestScope.h"
#include "procgen/RuntimeGeneration.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace test_procgen_runtime_generation_unit {
using namespace eve::procgen;

// addLevel validates sizes. A source issues immutable jobs with stable cell
// bounds; a completion publishes owned points and starts revision 1.
TEST_CASE("procgen.runtimeGeneration.level_source_job_and_publication") {
    RuntimeGeneration runtime(42);
    const int         zeroCellSize      = runtime.addLevel(0.f, 8.f, 1.5f);
    const int         invalidHysteresis = runtime.addLevel(10.f, 8.f, .5f);
    const int         level             = runtime.addLevel(10.f, 8.f, 1.5f);
    CHECK_EQ(zeroCellSize, -1);
    CHECK_EQ(invalidHysteresis, -1);
    REQUIRE_EQ(level, 0);
    CHECK_EQ(runtime.getLevelCleanupRadius(0), 12.f);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    CHECK(runtime.getPendingGenerateCount() > 0);
    auto issued = runtime.nextGenerationJob();
    REQUIRE(issued.ok());
    REQUIRE(issued.value().has_value());
    const auto job = *issued.value();
    CHECK(job.getMaxX() > job.getMinX());
    CHECK(job.getMaxZ() > job.getMinZ());
    PointSet points;
    points.add(job.getMinX(), 0.f, job.getMinZ());
    auto committed = runtime.completeGenerationJob({job, std::move(points)});
    REQUIRE(committed.ok());
    CHECK(runtime.hasCell(job.getLevel(), job.getX(), job.getZ()));
    CHECK_EQ(runtime.getCellRevision(job.getLevel(), job.getX(), job.getZ()), uint64_t{1});
    auto output = std::unique_ptr<PointSet>(runtime.getCellOutput(job.getLevel(), job.getX(), job.getZ()));
    REQUIRE(bool(output));
    CHECK_EQ(output->getCount(), 1);
}

// Scheduler identity and ticket prevent replay, cross-scheduler completion,
// and completion after clear from mutating any cell.
TEST_CASE("procgen.runtimeGeneration.rejects_replay_foreign_and_cleared_jobs") {
    RuntimeGeneration first(7), second(7);
    const int         firstLevel  = first.addLevel(10.f, 6.f, 1.5f);
    const int         secondLevel = second.addLevel(10.f, 6.f, 1.5f);
    REQUIRE_EQ(firstLevel, 0);
    REQUIRE_EQ(secondLevel, 0);
    first.updateSource(5.f, 5.f, 0.f, 1.f);
    second.updateSource(5.f, 5.f, 0.f, 1.f);
    auto issued = first.nextGenerationJob();
    REQUIRE(issued.ok());
    REQUIRE(issued.value().has_value());
    const auto job = *issued.value();
    CHECK(!second.completeGenerationJob({job, PointSet{}}).ok());
    REQUIRE(first.completeGenerationJob({job, PointSet{}}).ok());
    CHECK(!first.completeGenerationJob({job, PointSet{}}).ok());
    auto pending = first.nextGenerationJob();
    REQUIRE(pending.ok());
    if (pending.value().has_value()) {
        const auto stale = *pending.value();
        first.clear();
        CHECK(!first.completeGenerationJob({stale, PointSet{}}).ok());
    }
}

// Removing a source schedules cleanup outside the hysteresis radius. A
// completed cleanup makes that cell absent from the published cache.
TEST_CASE("procgen.runtimeGeneration.cleanup_after_source_moves") {
    RuntimeGeneration runtime(17);
    const int         level = runtime.addLevel(10.f, 6.f, 2.f);
    REQUIRE_EQ(level, 0);
    runtime.updateSource(5.f, 5.f, 0.f, 1.f);
    auto issued = runtime.nextGenerationJob();
    REQUIRE(issued.ok());
    REQUIRE(issued.value().has_value());
    const auto job = *issued.value();
    REQUIRE(runtime.completeGenerationJob({job, PointSet{}}).ok());
    runtime.updateSource(100.f, 100.f, 0.f, 1.f);
    CHECK(runtime.getPendingCleanupCount() > 0);
    bool found = false;
    for (;;) {
        auto request = runtime.nextCleanupRequest();
        REQUIRE(request.ok());
        if (!request.value().has_value()) break;
        const auto cell = *request.value();
        if (cell.getX() == job.getX() && cell.getZ() == job.getZ()) found = true;
        REQUIRE(runtime.completeCleanupRequest(cell).ok());
    }
    CHECK(found);
    CHECK(!runtime.hasCell(job.getLevel(), job.getX(), job.getZ()));
}
}  // namespace test_procgen_runtime_generation_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_scene_sink)
#include "PrivateTestScope.h"
#include "common/ProcgenSceneSink.h"
#include "procgen/Procgen.h"

#include <string>
#include <vector>

namespace test_procgen_scene_sink_unit {

// A point-identity delta is a pure, revision-checked transformation of an
// owned scene snapshot. An update preserves order while changing only payload.
TEST_CASE("procgen.sceneSink.identity_delta_updates_and_removes_in_target_order") {
    eve::ProcgenInstanceDesc oak;
    oak.sourcePointId = 1;
    oak.id            = "oak";
    oak.asset         = "tree";
    oak.x             = 2.f;
    eve::ProcgenInstanceDesc rock;
    rock.sourcePointId = 2;
    rock.id            = "rock";
    rock.asset         = "stone";
    std::vector<eve::ProcgenInstanceDesc> current{oak, rock};
    auto                                  updated = oak;
    updated.x                                     = 9.f;
    eve::ProcgenInstanceDelta delta;
    delta.baseRevision   = 3;
    delta.targetRevision = 4;
    delta.updated.push_back(updated);
    delta.removedPointIds.push_back(2);
    delta.targetPointOrder.push_back(1);
    auto result = eve::buildProcgenInstanceDeltaSnapshot(current, 3, delta);
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().size(), std::size_t{1});
    CHECK_EQ(result.value()[0].sourcePointId, uint64_t{1});
    CHECK_EQ(result.value()[0].x, 9.f);
    CHECK_EQ(current.size(), std::size_t{2});
    CHECK_EQ(current[0].x, 2.f);
}

// Wrong base revision, nonincreasing target revision and unknown removals
// fail before changing the caller's current snapshot.
TEST_CASE("procgen.sceneSink.delta_rejection_is_nonmutating") {
    eve::ProcgenInstanceDesc currentInstance;
    currentInstance.sourcePointId = 7;
    currentInstance.id            = "stable";
    const std::vector<eve::ProcgenInstanceDesc> current{currentInstance};
    eve::ProcgenInstanceDelta                   delta;
    delta.baseRevision   = 2;
    delta.targetRevision = 3;
    delta.targetPointOrder.push_back(7);
    REQUIRE(!eve::buildProcgenInstanceDeltaSnapshot(current, 1, delta).ok());
    delta.baseRevision   = 1;
    delta.targetRevision = 1;
    CHECK(!eve::buildProcgenInstanceDeltaSnapshot(current, 1, delta).ok());
    delta.targetRevision = 2;
    delta.removedPointIds.push_back(99);
    CHECK(!eve::buildProcgenInstanceDeltaSnapshot(current, 1, delta).ok());
    CHECK_EQ(current[0].sourcePointId, uint64_t{7});
}
}  // namespace test_procgen_scene_sink_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_scene_sink)
#include "PrivateTestScope.h"
#include "procgen/ShapeGrammar.h"
#include "procgen/SpatialData.h"

#include <memory>
#include <string>

namespace test_procgen_spatial_contract_unit {
using namespace eve::procgen;

// Grammar symbols resolve to asset variants and expand along the spline
// without crossing its end. The same seed chooses the same variant sequence.
TEST_CASE("procgen.shapeGrammar.modules_repetition_and_seeded_output") {
    ShapeGrammar grammar;
    REQUIRE(grammar.addModule("A", "wall-a", 2.f));
    REQUIRE(grammar.addModule("A", "wall-b", 2.f));
    REQUIRE(grammar.addModule("B", "post", 1.f));
    CHECK(!grammar.addModule("A", "wrong-length", 3.f));
    CHECK(grammar.validate("A,[B,A]*"));
    PointSet line;
    line.add(0.f, 0.f, 0.f);
    line.add(10.f, 0.f, 0.f);
    std::unique_ptr<PointSet> first(grammar.generate("A,[B,A]*", &line, 42, false));
    std::unique_ptr<PointSet> repeated(grammar.generate("A,[B,A]*", &line, 42, false));
    REQUIRE(bool(first));
    REQUIRE(bool(repeated));
    CHECK_EQ(first->getCount(), 5);
    CHECK_EQ(first->getStringAttribute(0, "module", ""), std::string("A"));
    CHECK_EQ(first->getStringAttribute(1, "module", ""), std::string("B"));
    for (int i = 0; i < first->getCount(); ++i)
        CHECK_EQ(first->getStringAttribute(i, "asset", ""), repeated->getStringAttribute(i, "asset", ""));
}

// Syntax and unknown-symbol failures expose diagnostics. A mandatory module
// that does not fit fails, while incomplete mode emits complete prefixes.
TEST_CASE("procgen.shapeGrammar.invalid_and_partial_inputs") {
    ShapeGrammar grammar;
    REQUIRE(grammar.addModule("A", "wall", 2.f));
    CHECK(!grammar.validate("[A"));
    CHECK(!grammar.getError().empty());
    CHECK(!grammar.validate("A,Missing"));
    CHECK(grammar.getError().find("unknown module") != std::string::npos);
    PointSet shortLine;
    shortLine.add(0.f, 0.f, 0.f);
    shortLine.add(5.f, 0.f, 0.f);
    CHECK(grammar.generate("A3", &shortLine, 1, false) == nullptr);
    std::unique_ptr<PointSet> partial(grammar.generate("A3", &shortLine, 1, true));
    REQUIRE(bool(partial));
    CHECK_EQ(partial->getCount(), 2);
}

// Spatial composition uses world coordinates. Subtraction removes the sphere
// interior, intersection keeps only shared volume, and sample is repeatable.
TEST_CASE("procgen.spatialData.box_sphere_boolean_and_sampling_contract") {
    const auto box        = SpatialData::box(0.f, 0.f, 0.f, 10.f, 4.f, 10.f);
    const auto hole       = SpatialData::sphere(5.f, 2.f, 5.f, 2.f);
    const auto difference = SpatialData::subtract(box, hole);
    REQUIRE(box.contains(1.f, 1.f, 1.f));
    CHECK(!box.contains(11.f, 1.f, 1.f));
    CHECK(!difference.contains(5.f, 2.f, 5.f));
    CHECK(difference.contains(1.f, 1.f, 1.f));
    const auto overlap = SpatialData::intersect(box, SpatialData::box(8.f, -2.f, 8.f, 12.f, 8.f, 12.f));
    CHECK(overlap.contains(9.f, 2.f, 9.f));
    CHECK(!overlap.contains(7.f, 2.f, 9.f));
    CHECK_EQ(overlap.getMinX(), 8.f);
    CHECK_EQ(overlap.getMaxZ(), 10.f);
    const auto first = overlap.sample(1.f, 99, 0.f);
    const auto again = overlap.sample(1.f, 99, 0.f);
    CHECK_EQ(first.getCount(), again.getCount());
    for (int i = 0; i < first.getCount(); ++i) {
        CHECK_EQ(first.getX(i), again.getX(i));
        CHECK_EQ(first.getZ(i), again.getZ(i));
    }
}

// Filtering retains source point attributes but changes membership according
// to the spatial predicate and the invert flag.
TEST_CASE("procgen.spatialData.filter_and_invert_membership") {
    PointSet source;
    source.add(1.f, 1.f, 1.f);
    source.add(20.f, 1.f, 1.f);
    const auto domain  = SpatialData::box(0.f, 0.f, 0.f, 10.f, 2.f, 2.f);
    const auto inside  = domain.filter(source);
    const auto outside = domain.filter(source, true);
    REQUIRE_EQ(inside.getCount(), 1);
    REQUIRE_EQ(outside.getCount(), 1);
    CHECK_EQ(inside.getX(0), 1.f);
    CHECK_EQ(outside.getX(0), 20.f);
}
}  // namespace test_procgen_spatial_contract_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_prototype_kit)
#include "PrivateTestScope.h"
#include "image/ImageData.h"
#include "procgen/Params.h"
#include "procgen/algorithms/PrototypeKit.h"
#include "procgen/texture/PrototypeTextures.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace test_prototype_kit {
using namespace eve::procgen;

uint64_t checksum(const eve::image::ImageData &image) {
    const auto *bytes = static_cast<const uint8_t *>(image.getData());
    uint64_t    value = 1469598103934665603ull;
    for (std::size_t i = 0; i < image.getSize(); ++i) {
        value ^= bytes[i];
        value *= 1099511628211ull;
    }
    return value;
}

// The catalogue defines 75 mesh recipes. Each sample has valid index ranges,
// and mesh parameters affect either topology or UV density as documented.
TEST_CASE("procgen.prototype.mesh_catalogue_geometry_and_parameters") {
    REQUIRE_EQ(prototypePieceDescriptors().size(), std::size_t{75});
    Params defaults;
    for (const char *name : {"ground", "wall", "ramp", "stairs"}) {
        auto result = generatePrototypePiece(name, defaults);
        REQUIRE(result.ok());
        auto mesh = std::move(result).takeValue();
        REQUIRE(mesh.getVertexCount() > 0);
        REQUIRE(mesh.getIndexCount() > 0);
        for (int i = 0; i < mesh.getIndexCount(); ++i) {
            CHECK(mesh.getIndex(i) >= 0);
            CHECK(mesh.getIndex(i) < mesh.getVertexCount());
        }
        CHECK_EQ(mesh.getMeta("source", ""), std::string("procedural"));
    }
    Params coarse, fine;
    coarse.setInt("steps", 3);
    fine.setInt("steps", 9);
    auto a = generatePrototypePiece("stairs", coarse);
    auto b = generatePrototypePiece("stairs", fine);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(b.value().getVertexCount() > a.value().getVertexCount());
    CHECK(!generatePrototypePiece("unknown", defaults).ok());
}

// All texture names remain advertised while one representative pattern is
// checked across palette and cell-size inputs. Same inputs are byte-stable.
TEST_CASE("procgen.prototype.texture_catalogue_palettes_and_determinism") {
    REQUIRE_EQ(prototypeTextureDescriptors().size(), std::size_t{13});
    Params params;
    params.setSize(64, 64);
    params.setInt("cellSize", 8);
    params.setString("palette", "dark");
    auto first  = generatePrototypeTexture("diagonal-grid", params);
    auto repeat = generatePrototypeTexture("diagonal-grid", params);
    REQUIRE(first.ok());
    REQUIRE(repeat.ok());
    CHECK_EQ(first.value()->getWidth(), 64);
    CHECK_EQ(first.value()->getHeight(), 64);
    CHECK_EQ(checksum(*first.value()), checksum(*repeat.value()));
    const auto reference = checksum(*first.value());
    params.setString("palette", "light");
    auto light = generatePrototypeTexture("diagonal-grid", params);
    REQUIRE(light.ok());
    CHECK(checksum(*light.value()) != reference);
    params.setString("palette", "dark");
    params.setInt("cellSize", 16);
    auto dense = generatePrototypeTexture("diagonal-grid", params);
    REQUIRE(dense.ok());
    CHECK(checksum(*dense.value()) != reference);
}

// Invalid palette, non-finite opacity and unknown recipe fail as structured
// inputs before publishing an image.
TEST_CASE("procgen.prototype.texture_invalid_inputs") {
    Params params;
    params.setSize(64, 64);
    params.setString("palette", "blue");
    REQUIRE(!generatePrototypeTexture("fine-grid", params).ok());
    params.setString("palette", "dark");
    params.setFloat("minorAlpha", std::numeric_limits<float>::quiet_NaN());
    CHECK(!generatePrototypeTexture("fine-grid", params).ok());
    Params defaults;
    CHECK(!generatePrototypeTexture("unknown", defaults).ok());
}
}  // namespace test_prototype_kit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_roguelike_generator)
#include "PrivateTestScope.h"
#include "procgen/GeneratorRegistry.h"
#include "procgen/Semantic.h"

#include <string>

namespace test_roguelike_generator {
using namespace eve::procgen;

// Stable seed and parameters yield identical cells and details. A different
// seed changes the layout while preserving its wall/floor vocabulary.
TEST_CASE("procgen.roguelike.seeded_grid_and_layout_variation") {
    auto &registry = GeneratorRegistry::instance();
    registry.registerBuiltins();
    REQUIRE(registry.has("level.roguelike"));
    Params params;
    params.setSeed(42);
    params.setSize(48, 32);
    params.setInt("roomCount", 10);
    Grid2D      first, repeated, changed;
    std::string error;
    REQUIRE(registry.generate("level.roguelike", params, first, error));
    REQUIRE(registry.generate("level.roguelike", params, repeated, error));
    CHECK(first.cells() == repeated.cells());
    CHECK(first.detail() == repeated.detail());
    params.setSeed(43);
    REQUIRE(registry.generate("level.roguelike", params, changed, error));
    CHECK(first.cells() != changed.cells());
}

// A generated map fills the requested dimensions, retains solid padding,
// and places spawn/stair markers only on traversable cells.
TEST_CASE("procgen.roguelike.structure_and_walkable_markers") {
    auto &registry = GeneratorRegistry::instance();
    registry.registerBuiltins();
    Params params;
    params.setSeed(7);
    params.setSize(48, 32);
    params.setInt("roomCount", 12);
    params.setInt("stairCount", 2);
    Grid2D      grid;
    std::string error;
    REQUIRE(registry.generate("level.roguelike", params, grid, error));
    CHECK_EQ(grid.getWidth(), 48);
    CHECK_EQ(grid.getHeight(), 32);
    CHECK_EQ(grid.getCell(0, 0), int(Semantic::Wall));
    int walkable = 0, stairs = 0, spawns = 0;
    for (int y = 0; y < grid.getHeight(); ++y)
        for (int x = 0; x < grid.getWidth(); ++x) {
            const int cell = grid.getCell(x, y);
            if (cell == int(Semantic::Floor) || cell == int(Semantic::Corridor))
                ++walkable;
            else
                CHECK_EQ(cell, int(Semantic::Wall));
        }
    CHECK(walkable > 0);
    for (int i = 0; i < grid.getObjectCount(); ++i) {
        const auto kind = grid.getObjectType(i);
        if (kind != "spawn" && kind != "stairs") continue;
        const int  x = int(grid.getObjectX(i)), y = int(grid.getObjectY(i));
        const int  cell          = grid.getCell(x, y);
        const bool isTraversable = cell == int(Semantic::Floor) || cell == int(Semantic::Corridor);
        CHECK(isTraversable);
        if (kind == "spawn") ++spawns;
        if (kind == "stairs") ++stairs;
    }
    CHECK(spawns > 0);
    CHECK_EQ(stairs, 2);
}

// Unsupported dimensions fail and do not publish a valid grid. Detail writes
// on a generated grid remain bounded to the format's eight-bit storage.
TEST_CASE("procgen.roguelike.rejected_size_and_detail_edit_bounds") {
    auto &registry = GeneratorRegistry::instance();
    registry.registerBuiltins();
    Params params;
    params.setSeed(1);
    params.setSize(4, 4);
    Grid2D      grid;
    std::string error;
    CHECK(!registry.generate("level.roguelike", params, grid, error));
    CHECK(!error.empty());
    params.setSize(32, 24);
    REQUIRE(registry.generate("level.roguelike", params, grid, error));
    grid.setCell(1, 1, int(Semantic::Floor));
    grid.setDetail(1, 1, 5);
    CHECK_EQ(grid.getDetail(1, 1), 5);
    grid.setDetail(0, 0, 999);
    CHECK_EQ(grid.getDetail(0, 0), 255);
    CHECK_EQ(grid.getDetail(-1, -1), 0);
}
}  // namespace test_roguelike_generator
#endif
