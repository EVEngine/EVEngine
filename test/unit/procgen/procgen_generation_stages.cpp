// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "procgen_generation_stages.cpp"
  ]
}
*/
// clang-format on

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

#include <algorithm>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace eve::procgen;
using namespace eve::housegen;

namespace {
HouseComponentLibrary componentKit() {
    HouseComponentLibrary kit;
    for (const char* category :
         {"foundation", "floor", "wall", "door", "roof", "stairs", "interior_wall", "interior_door"}) {
        HouseComponent component;
        component.id        = category;
        component.category  = category;
        component.modelPath = std::string(category) + ".glb";
        REQUIRE(kit.registerComponent(component).ok());
    }
    return kit;
}

Params caveRecipe() {
    Params params;
    params.setSeed(20260830);
    params.setInt("resolution", 16);
    params.setInt("dripstones", 2);
    params.setInt("flowstones", 1);
    params.setInt("curtains", 1);
    params.setFloat("curvatureDissolution", .5f);
    return params;
}

void sample(CaveGeneration& cave, const Params& params) {
    REQUIRE(cave.configure(params).ok());
    REQUIRE(cave.buildSkeleton().ok());
    REQUIRE(cave.sampleField().ok());
}
}  // namespace

TEST_CASE("procgen.field.resize_copy_and_invalid_mutation_contract") {
    ScalarField3D field;
    REQUIRE(field.resize(4, 3, 2, 1.f).ok());
    CHECK_EQ(field.width(), 4);
    auto independent = field;
    REQUIRE(field.setSample(1, 1, 1, -1.f).ok());
    REQUIRE(independent.sample(1, 1, 1).ok());
    CHECK_EQ(independent.sample(1, 1, 1).value(), 1.f);

    CHECK(!field.setSample(-1, 0, 0, 1.f).ok());
    CHECK(!field.setSample(4, 0, 0, 1.f).ok());
    CHECK(!field.setSample(1, 1, 1, std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK(!field.resize(0, 3, 2, 0.f).ok());
    CHECK(!field.assign(4, 3, 2, {}).ok());
    CHECK_EQ(field.width(), 4);
    CHECK_EQ(field.sample(1, 1, 1).value(), -1.f);
}

TEST_CASE("procgen.field.carving_produces_geometry_and_rejects_bad_shape_atomically") {
    ScalarField3D field;
    REQUIRE(field.resize(17, 17, 17, 1.f).ok());
    CHECK(!field.extractMesh(0.f).ok());
    REQUIRE(field.carveEllipsoid(0.f, 0.f, 0.f, .6f, .4f, .5f).ok());
    const auto first = field.extractMesh(0.f);
    REQUIRE(first.ok());
    CHECK(first.value().getVertexCount() > 0);
    const std::vector<float> before(field.samples().begin(), field.samples().end());
    CHECK(!field.carveEllipsoid(0.f, 0.f, 0.f, -1.f, .4f, .5f).ok());
    CHECK_EQ(field.samples().size(), before.size());
    CHECK(std::equal(field.samples().begin(), field.samples().end(), before.begin()));
    REQUIRE(field.carveEllipsoid(.3f, 0.f, 0.f, .4f, .2f, .2f).ok());
    const auto second = field.extractMesh(0.f);
    REQUIRE(second.ok());
    CHECK(second.value().positions() != first.value().positions());
}

TEST_CASE("procgen.house.staged_default_schedule_matches_single_call_recipe") {
    static_assert(!std::is_copy_constructible_v<HouseGeneration>);
    auto         kit = componentKit();
    HouseRequest request;
    request.seed          = 20260815;
    request.width         = 8;
    request.depth         = 7;
    request.floors        = 2;
    request.footprint     = "rectangle";
    request.roof          = "gable";
    request.requiredRooms = {"living", "kitchen", "bedroom"};
    HouseLayout singleCall;
    REQUIRE(HouseGenerator(kit).generate(request, singleCall).ok());

    HouseGeneration staged;
    REQUIRE(staged.configure(request, kit).ok());
    kit.clear();  // Configuration must own the component data it needs.
    for (int floor = 0; floor < request.floors; ++floor) {
        const auto grid = staged.nextFloorGrid();
        REQUIRE(grid.ok());
        const auto repeat = staged.nextFloorGrid();
        REQUIRE(repeat.ok());
        CHECK_EQ(grid.value().cells(), repeat.value().cells());
        REQUIRE(staged.emitFloor(grid.value(), request.requiredRooms).ok());
    }
    const auto finished = staged.finish();
    REQUIRE(finished.ok());
    CHECK_EQ(finished.value().toJson(), singleCall.toJson());
    auto moved = std::move(staged);
    CHECK_EQ(staged.floorCount(), 0);
    CHECK(moved.finish().ok());
}

TEST_CASE("procgen.house.invalid_floor_and_request_do_not_replace_completed_stage") {
    auto         kit = componentKit();
    HouseRequest request;
    request.width     = 9;
    request.depth     = 9;
    request.floors    = 2;
    request.footprint = "rectangle";
    HouseGeneration house;
    CHECK(!house.finish().ok());
    REQUIRE(house.configure(request, kit).ok());
    const auto ground = house.nextFloorGrid();
    REQUIRE(ground.ok());
    REQUIRE(house.emitFloor(ground.value(), {"living"}).ok());
    const auto upper = house.nextFloorGrid();
    REQUIRE(upper.ok());
    Grid2D wrongSize;
    wrongSize.resize(10, 9);
    wrongSize.fill(1);
    CHECK(!house.emitFloor(wrongSize, {"bedroom"}).ok());
    CHECK_EQ(house.floorCount(), 1);

    GridGraph graph;
    REQUIRE(graph.addNode("input", "grid.input").ok());
    REQUIRE(graph.addNode("inset", "grid.shrink").ok());
    REQUIRE(graph.connect("input", "inset").ok());
    REQUIRE(graph.setNodeGrid("input", upper.value()).ok());
    REQUIRE(graph.setNodeInt("inset", "radius", 1).ok());
    auto result = graph.execute("inset");
    REQUIRE(result.ok());
    auto edited = std::get<Grid2D>(std::move(result).takeValue());
    for (int y = 1; y < 3; ++y)
        for (int x = 1; x < 3; ++x) edited.setCell(x, y, 1);
    REQUIRE(house.emitFloor(edited, {"bedroom"}).ok());
    const auto complete = house.finish();
    REQUIRE(complete.ok());
    CHECK_EQ(complete.value().rooms.back().z, 1);
    auto invalid  = request;
    invalid.width = 0;
    CHECK(!house.configure(invalid, kit).ok());
    CHECK_EQ(house.floorCount(), 2);
    CHECK(house.finish().ok());
}

TEST_CASE("procgen.cave.staged_default_schedule_matches_recipe_and_repeats") {
    const auto params = caveRecipe();
    MeshRecipeRegistry::instance().registerBuiltins();
    MeshBuild   reference;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.cave", params, reference, error));

    CaveGeneration cave;
    sample(cave, params);
    for (auto operation : {CaveSurfaceOperation::Reactivity, CaveSurfaceOperation::ReactivePatches,
                           CaveSurfaceOperation::Condensation, CaveSurfaceOperation::Curvature})
        REQUIRE(cave.evolveSurface(operation).ok());
    REQUIRE(cave.deposit().ok());
    const auto mesh = cave.buildMesh();
    REQUIRE(mesh.ok());
    CHECK_EQ(mesh.value().positions(), reference.positions());
    CHECK_EQ(mesh.value().normals(), reference.normals());
    CHECK_EQ(mesh.value().indices(), reference.indices());
    const auto repeat = cave.buildMesh();
    REQUIRE(repeat.ok());
    CHECK_EQ(repeat.value().positions(), mesh.value().positions());
}

TEST_CASE("procgen.cave.field_replacement_is_owned_and_failed_extraction_keeps_phase") {
    CaveGeneration cave;
    CHECK(!cave.sampleField().ok());
    const auto params = caveRecipe();
    sample(cave, params);
    auto field = cave.field();
    REQUIRE(field.ok());
    const auto baseline = cave.buildMesh();
    REQUIRE(baseline.ok());

    ScalarField3D empty;
    REQUIRE(empty.resize(16, 12, 16, 1.f).ok());
    REQUIRE(cave.replaceField(empty).ok());
    CHECK(!cave.buildMesh().ok());
    CHECK(cave.phase() == CaveGenerationPhase::Sampled);
    REQUIRE(cave.replaceField(field.value()).ok());
    const auto restored = cave.buildMesh();
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().positions(), baseline.value().positions());

    REQUIRE(field.value().setSample(8, 6, 8, 1.f).ok());
    REQUIRE(cave.replaceField(field.value()).ok());
    const auto changed = cave.buildMesh();
    REQUIRE(changed.ok());
    CHECK(changed.value().positions() != baseline.value().positions());
    REQUIRE(cave.deposit().ok());
    CHECK(!cave.replaceField(field.value()).ok());
    CHECK(!cave.evolveSurface(CaveSurfaceOperation::Curvature).ok());
    auto invalid = params;
    invalid.setInt("nx", 1);
    CHECK(!cave.configure(invalid).ok());
    CHECK(cave.phase() == CaveGenerationPhase::Deposited);
}

TEST_CASE("procgen.cave.skeleton_and_surface_edits_follow_phase_order") {
    auto params = caveRecipe();
    params.setFloat("surfaceSlopeReactivity", .5f);
    CaveGeneration cave;
    REQUIRE(cave.configure(params).ok());
    REQUIRE(cave.buildSkeleton().ok());
    CHECK(!cave.setChamber(-1, 0.f, 0.f, 0.f, .2f, .2f, .2f).ok());
    REQUIRE(cave.setChamber(0, 0.f, 0.f, 0.f, .5f, .4f, .5f).ok());
    REQUIRE(cave.sampleField().ok());
    CHECK(!cave.setChamber(0, 0.f, 0.f, 0.f, .2f, .2f, .2f).ok());
    REQUIRE(cave.evolveSurface(CaveSurfaceOperation::Curvature).ok());
    REQUIRE(cave.evolveSurface(CaveSurfaceOperation::Reactivity).ok());
    REQUIRE(cave.evolveSurface(CaveSurfaceOperation::Curvature).ok());
    const auto mesh = cave.buildMesh();
    REQUIRE(mesh.ok());
    CHECK_EQ(mesh.value().getMeta("surfaceSchedule", ""), std::string("curvature,reactivity,curvature"));
    CHECK_EQ(mesh.value().getMeta("depositionStage", ""), std::string("omitted"));
    CHECK_EQ(mesh.value().getMeta("skeletonEdited", ""), std::string("true"));
}
