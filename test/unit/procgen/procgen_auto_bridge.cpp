#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/road/BridgeGenerator.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <tuple>

using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
bool groupExists(const MeshBuild& mesh, const std::string& prefix) {
    for (int index = 0; index < mesh.getGroupCount(); ++index)
        if (mesh.getGroupName(index).starts_with(prefix + "@")) return true;
    return false;
}

void validTriangles(const MeshBuild& mesh) {
    REQUIRE(mesh.getVertexCount() > 0);
    REQUIRE(mesh.getIndexCount() > 0);
    REQUIRE_EQ(mesh.getIndexCount() % 3, 0);
    for (int vertex = 0; vertex < mesh.getVertexCount(); ++vertex) {
        CHECK(std::isfinite(mesh.getPositionX(vertex)));
        CHECK(std::isfinite(mesh.getPositionY(vertex)));
        CHECK(std::isfinite(mesh.getPositionZ(vertex)));
    }
    for (int index = 0; index < mesh.getIndexCount(); index += 3) {
        const int a = mesh.getIndex(index);
        const int b = mesh.getIndex(index + 1);
        const int c = mesh.getIndex(index + 2);
        REQUIRE(a >= 0);
        REQUIRE(a < mesh.getVertexCount());
        REQUIRE(b >= 0);
        REQUIRE(b < mesh.getVertexCount());
        REQUIRE(c >= 0);
        REQUIRE(c < mesh.getVertexCount());
        const double ux = mesh.getPositionX(b) - mesh.getPositionX(a);
        const double uy = mesh.getPositionY(b) - mesh.getPositionY(a);
        const double uz = mesh.getPositionZ(b) - mesh.getPositionZ(a);
        const double vx = mesh.getPositionX(c) - mesh.getPositionX(a);
        const double vy = mesh.getPositionY(c) - mesh.getPositionY(a);
        const double vz = mesh.getPositionZ(c) - mesh.getPositionZ(a);
        const double nx = uy * vz - uz * vy;
        const double ny = uz * vx - ux * vz;
        const double nz = ux * vy - uy * vx;
        CHECK(nx * mesh.getNormalX(a) + ny * mesh.getNormalY(a) + nz * mesh.getNormalZ(a) > 1e-10);
    }
}
}  // namespace

TEST_CASE("procgen.bridge.each_named_preset_decodes_to_a_valid_plan") {
    auto& recipes = MeshRecipeRegistry::instance();
    recipes.registerBuiltins();
    for (int index = 0; index < 10; ++index) {
        const auto kind   = static_cast<BridgeKind>(index);
        const auto preset = bridgePreset(kind);
        REQUIRE(preset.ok());
        const auto name = bridgeKindName(kind);
        REQUIRE(!name.empty());
        Params params;
        params.setString("bridgeType", std::string(name));
        REQUIRE(recipes.applyDefaults("mesh.autoBridge", params));
        const auto decoded = bridgeParametersFromParams(params);
        REQUIRE(decoded.ok());
        CHECK_EQ(decoded.value().start.x, preset.value().start.x);
        CHECK_EQ(decoded.value().end.x, preset.value().end.x);
        CHECK_EQ(decoded.value().panels, preset.value().panels);
        CHECK(decoded.value().railing == preset.value().railing);
        const auto plan = planBridge(decoded.value());
        REQUIRE(plan.ok());
        bool foundation = false;
        bool steel      = false;
        for (const auto& member : plan.value().members) {
            CHECK(!bridgePartName(member.part).empty());
            CHECK(!bridgeMaterialName(member.material).empty());
            foundation |= member.part == BridgePart::Footing;
            steel |= member.section == BridgeSection::IBeam;
        }
        CHECK(foundation);
        if (kind == BridgeKind::ThroughTruss || kind == BridgeKind::DeckTruss || kind == BridgeKind::Cantilever)
            CHECK(steel);
    }
}

TEST_CASE("procgen.bridge.all_topologies_bake_finite_oriented_distinct_meshes") {
    std::set<std::tuple<int, int, int, int>> signatures;
    for (int index = 0; index < 10; ++index) {
        BridgeParameters input;
        input.kind      = static_cast<BridgeKind>(index);
        input.spans     = index == 9 ? 12 : 3;
        const auto plan = planBridge(input);
        REQUIRE(plan.ok());
        const auto baked = bakeBridge(plan.value());
        REQUIRE(baked.ok());
        validTriangles(baked.value());
        CHECK(groupExists(baked.value(), "deck"));
        if (input.kind == BridgeKind::Arch || input.kind == BridgeKind::TiedArch)
            CHECK(groupExists(baked.value(), "arch"));
        if (input.kind == BridgeKind::Suspension || input.kind == BridgeKind::CableStayed)
            CHECK(groupExists(baked.value(), "cable"));
        float highest = 0.f;
        for (int vertex = 0; vertex < baked.value().getVertexCount(); ++vertex)
            highest = std::max(highest, baked.value().getPositionY(vertex));
        signatures.emplace(baked.value().getVertexCount(), baked.value().getIndexCount(), baked.value().getGroupCount(),
                           static_cast<int>(highest * 1000.f));
    }
    CHECK_EQ(signatures.size(), 10U);
}

TEST_CASE("procgen.bridge.equal_inclined_plans_bake_equal_positions_and_edit_independently") {
    BridgeParameters input;
    input.start       = {-20.f, 14.f, -12.f};
    input.end         = {24.f, 18.f, 16.f};
    input.kind        = BridgeKind::Suspension;
    const auto first  = planBridge(input);
    const auto second = planBridge(input);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value().members.size(), second.value().members.size());
    const auto firstMesh  = bakeBridge(first.value());
    const auto secondMesh = bakeBridge(second.value());
    REQUIRE(firstMesh.ok());
    REQUIRE(secondMesh.ok());
    CHECK_EQ(firstMesh.value().positions(), secondMesh.value().positions());
    validTriangles(firstMesh.value());

    auto edited = first.value();
    edited.members.push_back({{0, 2, 0}, {0, 6, 0}, .5f, .5f, BridgeSection::Box, BridgePart::Pier});
    const auto editedMesh = bakeBridge(edited);
    REQUIRE(editedMesh.ok());
    CHECK_EQ(editedMesh.value().getVertexCount(), firstMesh.value().getVertexCount() + 24);
    CHECK_EQ(first.value().members.size(), second.value().members.size());
}

TEST_CASE("procgen.bridge.invalid_parameters_and_degenerate_plans_fail_before_publication") {
    BridgeParameters input;
    input.end = input.start;
    CHECK(!planBridge(input).ok());
    input       = {};
    input.spans = 10000;
    CHECK(!planBridge(input).ok());
    input       = {};
    input.width = std::numeric_limits<float>::quiet_NaN();
    CHECK(!planBridge(input).ok());
    input          = {};
    input.kind     = BridgeKind::Arch;
    input.archRise = 30.f;
    CHECK(!planBridge(input).ok());
    input      = {};
    input.kind = static_cast<BridgeKind>(99);
    CHECK(!planBridge(input).ok());

    BridgePlan empty;
    CHECK(!bakeBridge(empty).ok());
    auto valid = planBridge(BridgeParameters{});
    REQUIRE(valid.ok());
    valid.value().members.front().to = valid.value().members.front().from;
    CHECK(!bakeBridge(valid.value()).ok());
}

TEST_CASE("procgen.bridge.recipe_supports_named_types_and_failure_preserves_output_mesh") {
    auto& recipes = MeshRecipeRegistry::instance();
    recipes.registerBuiltins();
    Params params;
    params.setFloat("deckWidth", 9.f);
    params.setFloat("endY", 16.f);
    params.setFloat("endZ", 12.f);
    MeshBuild   mesh;
    std::string error;
    for (const char* type : {"beam", "slab", "arch", "tiedArch", "throughTruss", "deckTruss", "suspension",
                             "cableStayed", "cantilever", "trestle"}) {
        params.setString("bridgeType", type);
        REQUIRE(recipes.generate("mesh.autoBridge", params, mesh, error));
        CHECK(groupExists(mesh, "deck"));
    }
    params.setString("bridgeType", "beam");
    params.setBool("piers", false);
    params.setBool("railings", false);
    REQUIRE(recipes.generate("mesh.autoBridge", params, mesh, error));
    CHECK(!groupExists(mesh, "pier"));
    CHECK(!groupExists(mesh, "railing"));
    const int beforeVertices = mesh.getVertexCount();

    params.setString("bridgeType", "unknown");
    CHECK(!recipes.generate("mesh.autoBridge", params, mesh, error));
    CHECK_EQ(mesh.getVertexCount(), beforeVertices);
    CHECK(!error.empty());
    params.setString("bridgeType", "beam");
    params.setFloat("deckWidth", std::numeric_limits<float>::quiet_NaN());
    CHECK(!recipes.generate("mesh.autoBridge", params, mesh, error));
}

TEST_CASE("procgen.bridge.custom_section_validates_dimensions_material_and_scale") {
    BridgePlan plan;
    plan.members.push_back({{0, 0, 0}, {4, 0, 0}, 1.f, 2.f, BridgeSection::IBeam, BridgePart::Girder});
    const auto beam = bakeBridge(plan);
    REQUIRE(beam.ok());
    CHECK_EQ(beam.value().getVertexCount(), 72);
    validTriangles(beam.value());
    plan.members.front().section = BridgeSection::TaperedBox;
    CHECK(!bakeBridge(plan).ok());
    plan.members.front().endWidth  = .5f;
    plan.members.front().endHeight = 1.f;
    const auto tapered             = bakeBridge(plan);
    REQUIRE(tapered.ok());
    validTriangles(tapered.value());
    plan.members.front().material = static_cast<BridgeMaterial>(99);
    CHECK(!bakeBridge(plan).ok());
    plan.members.front() = {{99990.f, 99990.f, 99990.f}, {99995.f, 99990.f, 99990.f}, .001f, .001f,
                            BridgeSection::Box,          BridgePart::Girder};
    CHECK(!bakeBridge(plan).ok());
}
