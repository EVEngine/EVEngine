#include "common/Module.h"
#include "procgen/GridGraph.h"
#include "procgen/MeshBuild.h"
#include "procgen/Params.h"
#include "procgen/ScalarField3D.h"
#include "procgen/algorithms/CaveGeneration.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/house/HouseComponentLibrary.h"
#include "procgen/house/HouseGeneration.h"
#include "procgen/house/HouseGenerator.h"
#include "procgen/house/HouseLayout.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <limits>
#include <type_traits>

using namespace eve::procgen;
using namespace eve::housegen;

namespace {
HouseComponentLibrary kit() {
    HouseComponentLibrary library;
    for (const auto* category :
         {"foundation", "floor", "wall", "door", "roof", "stairs", "interior_wall", "interior_door"}) {
        HouseComponent component;
        component.id        = category;
        component.category  = category;
        component.modelPath = std::string(category) + ".glb";
        auto result         = library.registerComponent(component);
        REQUIRE(result.ok());
    }
    return library;
}
Params caveParams() {
    Params params;
    params.setSeed(20260830);
    params.setInt("resolution", 16);
    params.setInt("dripstones", 2);
    params.setInt("flowstones", 1);
    params.setInt("curtains", 1);
    params.setFloat("curvatureDissolution", 0.5f);
    return params;
}
void prepare(CaveGeneration& generation, const Params& params) {
    REQUIRE(generation.configure(params).ok());
    REQUIRE(generation.buildSkeleton().ok());
    REQUIRE(generation.sampleField().ok());
}
}  // namespace

TEST_CASE("procgen.stages.scalarFieldIsOwningBoundedAndTransactional") {
    ScalarField3D field;
    REQUIRE(field.resize(4, 3, 2, 1.f).ok());
    auto copy = field;
    REQUIRE(field.setSample(1, 1, 1, -1.f).ok());
    REQUIRE(copy.sample(1, 1, 1).ok());
    CHECK_EQ(copy.sample(1, 1, 1).value(), 1.f);
    CHECK(!field.setSample(-1, 0, 0, 1.f).ok());
    CHECK(!field.setSample(1, 1, 1, std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK(!field.resize(0, 3, 2, 0.f).ok());
    CHECK(!field.assign(4, 3, 2, {}).ok());
    CHECK_EQ(field.sample(1, 1, 1).value(), -1.f);
    CHECK_EQ(field.width(), 4);
}

TEST_CASE("procgen.stages.scalarFieldProducesReusableParametricGeometry") {
    ScalarField3D field;
    REQUIRE(field.resize(17, 17, 17, 1.f).ok());
    CHECK(!field.extractMesh(0.f).ok());
    REQUIRE(field.carveEllipsoid(0.f, 0.f, 0.f, 0.6f, 0.4f, 0.5f).ok());
    auto mesh = field.extractMesh(0.f);
    REQUIRE(mesh.ok());
    CHECK(mesh.value().getVertexCount() > 0);
    auto prior = field;
    CHECK(!field.carveEllipsoid(0.f, 0.f, 0.f, -1.f, 0.4f, 0.5f).ok());
    CHECK(std::equal(field.samples().begin(), field.samples().end(), prior.samples().begin()));
    REQUIRE(field.carveEllipsoid(0.3f, 0.f, 0.f, 0.4f, 0.2f, 0.2f).ok());
    auto changed = field.extractMesh(0.f);
    REQUIRE(changed.ok());
    CHECK(changed.value().positions() != mesh.value().positions());
}

TEST_CASE("procgen.stages.houseDefaultScheduleMatchesCompatibilityAndOwnsKit") {
    static_assert(!std::is_copy_constructible_v<HouseGeneration>);
    auto         library = kit();
    HouseRequest request;
    request.seed          = 20260815;
    request.width         = 8;
    request.depth         = 7;
    request.floors        = 2;
    request.footprint     = "rectangle";
    request.roof          = "gable";
    request.requiredRooms = {"living", "kitchen", "bedroom"};
    HouseLayout compatibility;
    REQUIRE(HouseGenerator(library).generate(request, compatibility).ok());
    HouseGeneration generation;
    REQUIRE(generation.configure(request, library).ok());
    library.clear();
    for (int floor = 0; floor < request.floors; ++floor) {
        auto grid = generation.nextFloorGrid();
        REQUIRE(grid.ok());
        auto repeated = generation.nextFloorGrid();
        REQUIRE(repeated.ok());
        CHECK(grid.value().cells() == repeated.value().cells());
        REQUIRE(generation.emitFloor(grid.value(), request.requiredRooms).ok());
    }
    auto result = generation.finish();
    REQUIRE(result.ok());
    CHECK_EQ(result.value().toJson(), compatibility.toJson());
    auto moved = std::move(generation);
    CHECK_EQ(generation.floorCount(), 0);
    REQUIRE(moved.finish().ok());
}

TEST_CASE("procgen.stages.houseGraphControlsGenerationAndFailuresPreserveStage") {
    auto         library = kit();
    HouseRequest request;
    request.width     = 9;
    request.depth     = 9;
    request.floors    = 2;
    request.footprint = "rectangle";
    HouseGeneration generation;
    CHECK(!generation.finish().ok());
    REQUIRE(generation.configure(request, library).ok());
    auto base = generation.nextFloorGrid();
    REQUIRE(base.ok());
    REQUIRE(generation.emitFloor(base.value(), {"living"}).ok());
    auto planned = generation.nextFloorGrid();
    REQUIRE(planned.ok());
    Grid2D unsupported;
    unsupported.resize(10, 9);
    unsupported.fill(1);
    CHECK(!generation.emitFloor(unsupported, {"upper"}).ok());
    CHECK_EQ(generation.floorCount(), 1);
    GridGraph graph;
    REQUIRE(graph.addNode("input", "grid.input").ok());
    REQUIRE(graph.addNode("inset", "grid.shrink").ok());
    REQUIRE(graph.connect("input", "inset").ok());
    REQUIRE(graph.setNodeGrid("input", planned.value()).ok());
    REQUIRE(graph.setNodeInt("inset", "radius", 1).ok());
    auto result = graph.execute("inset");
    REQUIRE(result.ok());
    auto upper = std::get<Grid2D>(std::move(result).takeValue());
    // Keep the authoritative vertical stair column when editing a floor mask.
    for (int y = 1; y < 3; ++y)
        for (int x = 1; x < 3; ++x) upper.setCell(x, y, 1);
    REQUIRE(generation.emitFloor(upper, {"bedroom"}).ok());
    auto layout = generation.finish();
    REQUIRE(layout.ok());
    CHECK_EQ(layout.value().rooms.back().z, 1);
    CHECK_EQ(generation.floorCount(), 2);
    auto bad  = request;
    bad.width = 0;
    CHECK(!generation.configure(bad, library).ok());
    CHECK_EQ(generation.floorCount(), 2);
    REQUIRE(generation.finish().ok());
}

TEST_CASE("procgen.stages.caveDefaultScheduleMatchesRecipe") {
    auto params = caveParams();
    MeshRecipeRegistry::instance().registerBuiltins();
    MeshBuild   expected;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, expected, error));
    CaveGeneration generation;
    prepare(generation, params);
    for (auto op : {CaveSurfaceOperation::Reactivity, CaveSurfaceOperation::ReactivePatches,
                    CaveSurfaceOperation::Condensation, CaveSurfaceOperation::Curvature})
        REQUIRE(generation.evolveSurface(op).ok());
    REQUIRE(generation.deposit().ok());
    auto mesh = generation.buildMesh();
    REQUIRE(mesh.ok());
    CHECK(mesh.value().positions() == expected.positions());
    CHECK(mesh.value().normals() == expected.normals());
    CHECK(mesh.value().indices() == expected.indices());
    auto repeated = generation.buildMesh();
    REQUIRE(repeated.ok());
    CHECK(mesh.value().positions() == repeated.value().positions());
}

TEST_CASE("procgen.stages.caveFieldEditsAffectOutputAndFailedExtractionKeepsInput") {
    CaveGeneration generation;
    CHECK(!generation.sampleField().ok());
    auto params = caveParams();
    prepare(generation, params);
    auto snapshot = generation.field();
    REQUIRE(snapshot.ok());
    auto baseline = generation.buildMesh();
    REQUIRE(baseline.ok());
    ScalarField3D empty;
    REQUIRE(empty.resize(16, 12, 16, 1.f).ok());
    REQUIRE(generation.replaceField(empty).ok());
    CHECK(!generation.buildMesh().ok());
    CHECK(generation.phase() == CaveGenerationPhase::Sampled);
    REQUIRE(generation.replaceField(snapshot.value()).ok());
    auto restored = generation.buildMesh();
    REQUIRE(restored.ok());
    CHECK(restored.value().positions() == baseline.value().positions());
    // An owning field can be modified while the staged source remains unchanged.
    REQUIRE(snapshot.value().setSample(8, 6, 8, 1.f).ok());
    REQUIRE(generation.replaceField(snapshot.value()).ok());
    auto changed = generation.buildMesh();
    REQUIRE(changed.ok());
    CHECK(changed.value().positions() != baseline.value().positions());
    REQUIRE(generation.deposit().ok());
    CHECK(!generation.replaceField(snapshot.value()).ok());
    CHECK(!generation.evolveSurface(CaveSurfaceOperation::Curvature).ok());
    auto invalid = params;
    invalid.setInt("nx", 1);
    CHECK(!generation.configure(invalid).ok());
    CHECK(generation.phase() == CaveGenerationPhase::Deposited);
}

TEST_CASE("procgen.stages.caveSkeletonAndSurfacePoliciesAreEditable") {
    auto params = caveParams();
    params.setFloat("surfaceSlopeReactivity", 0.5f);
    CaveGeneration generation;
    REQUIRE(generation.configure(params).ok());
    REQUIRE(generation.buildSkeleton().ok());
    CHECK(!generation.setChamber(-1, 0.f, 0.f, 0.f, 0.2f, 0.2f, 0.2f).ok());
    REQUIRE(generation.setChamber(0, 0.f, 0.f, 0.f, 0.5f, 0.4f, 0.5f).ok());
    REQUIRE(generation.sampleField().ok());
    CHECK(!generation.setChamber(0, 0.f, 0.f, 0.f, 0.2f, 0.2f, 0.2f).ok());
    REQUIRE(generation.evolveSurface(CaveSurfaceOperation::Curvature).ok());
    REQUIRE(generation.evolveSurface(CaveSurfaceOperation::Reactivity).ok());
    REQUIRE(generation.evolveSurface(CaveSurfaceOperation::Curvature).ok());
    auto mesh = generation.buildMesh();
    REQUIRE(mesh.ok());
    CHECK_EQ(mesh.value().getMeta("surfaceSchedule", ""), "curvature,reactivity,curvature");
    CHECK_EQ(mesh.value().getMeta("depositionStage", ""), "omitted");
    CHECK_EQ(mesh.value().getMeta("skeletonEdited", ""), "true");
}
