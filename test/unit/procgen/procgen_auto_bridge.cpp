// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "procgen_auto_bridge.cpp"
  ]
}
*/
// clang-format on

#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/road/BridgeGenerator.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>

using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
bool hasBridgeGroup(const MeshBuild& mesh, const std::string& name) {
    for (int i = 0; i < mesh.getGroupCount(); ++i)
        if (mesh.getGroupName(i).starts_with(name + "@")) return true;
    return false;
}
void verifyBridgeTriangles(const MeshBuild& mesh) {
    REQUIRE(mesh.getVertexCount() > 0);
    for (int i = 0; i < mesh.getVertexCount(); ++i) {
        REQUIRE(std::isfinite(mesh.getPositionX(i)));
        REQUIRE(std::isfinite(mesh.getPositionY(i)));
        REQUIRE(std::isfinite(mesh.getPositionZ(i)));
    }
    for (int i = 0; i < mesh.getIndexCount(); i += 3) {
        const int a = mesh.getIndex(i), b = mesh.getIndex(i + 1), c = mesh.getIndex(i + 2);
        REQUIRE(a >= 0);
        REQUIRE(a < mesh.getVertexCount());
        REQUIRE(b >= 0);
        REQUIRE(b < mesh.getVertexCount());
        REQUIRE(c >= 0);
        REQUIRE(c < mesh.getVertexCount());
        const double ux = mesh.getPositionX(b) - mesh.getPositionX(a), uy = mesh.getPositionY(b) - mesh.getPositionY(a),
                     uz = mesh.getPositionZ(b) - mesh.getPositionZ(a);
        const double vx = mesh.getPositionX(c) - mesh.getPositionX(a), vy = mesh.getPositionY(c) - mesh.getPositionY(a),
                     vz = mesh.getPositionZ(c) - mesh.getPositionZ(a);
        const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        REQUIRE(nx * mesh.getNormalX(a) + ny * mesh.getNormalY(a) + nz * mesh.getNormalZ(a) > 1e-10);
    }
}
}  // namespace

TEST_CASE("procgen.autoBridge.tenTopologiesHaveValidDistinctGeometry") {
    std::set<std::tuple<int, int, int, int>> signatures;
    for (int kind = 0; kind < 10; ++kind) {
        BridgeParameters p;
        p.kind    = static_cast<BridgeKind>(kind);
        p.spans   = kind == 9 ? 12 : 3;
        auto plan = planBridge(p);
        REQUIRE(plan.ok());
        auto mesh = bakeBridge(plan.value());
        REQUIRE(mesh.ok());
        verifyBridgeTriangles(mesh.value());
        REQUIRE(hasBridgeGroup(mesh.value(), "deck"));
        // Abutments span the bridge width, while their longitudinal depth remains pierDepth.
        for (int triangle = 0; triangle < mesh.value().getIndexCount() / 3; ++triangle) {
            if (!mesh.value().getGroupName(mesh.value().getTriangleGroup(triangle)).starts_with("abutment@")) continue;
            for (int corner = 0; corner < 3; ++corner) {
                const int vertex = mesh.value().getIndex(triangle * 3 + corner);
                REQUIRE(std::fabs(mesh.value().getPositionX(vertex)) <= 24.f + p.wingWallLength + .02f);
            }
        }
        float highest = 0.f;
        for (int i = 0; i < mesh.value().getVertexCount(); ++i)
            highest = std::max(highest, mesh.value().getPositionY(i));
        signatures.emplace(mesh.value().getVertexCount(), mesh.value().getIndexCount(), mesh.value().getGroupCount(),
                           static_cast<int>(highest * 1000.f));
        if (p.kind == BridgeKind::Arch || p.kind == BridgeKind::TiedArch) REQUIRE(hasBridgeGroup(mesh.value(), "arch"));
        if (p.kind == BridgeKind::Suspension || p.kind == BridgeKind::CableStayed)
            REQUIRE(hasBridgeGroup(mesh.value(), "cable"));
    }
    REQUIRE(signatures.size() == 10u);
}

TEST_CASE("procgen.autoBridge.planEditsAndInclinedEndpointsAreDeterministic") {
    BridgeParameters p;
    p.start    = {-20.f, 14.f, -12.f};
    p.end      = {24.f, 18.f, 16.f};
    p.kind     = BridgeKind::Suspension;
    auto first = planBridge(p), second = planBridge(p);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(first.value().members.size() == second.value().members.size());
    auto original = bakeBridge(first.value()), again = bakeBridge(second.value());
    REQUIRE(original.ok());
    REQUIRE(again.ok());
    verifyBridgeTriangles(original.value());
    REQUIRE(original.value().getVertexCount() == again.value().getVertexCount());
    for (int i = 0; i < original.value().getVertexCount(); ++i) {
        REQUIRE(original.value().getPositionX(i) == again.value().getPositionX(i));
        REQUIRE(original.value().getPositionY(i) == again.value().getPositionY(i));
        REQUIRE(original.value().getPositionZ(i) == again.value().getPositionZ(i));
    }
    // A caller can adjust a structural member without a second generation mechanism.
    first.value().members.push_back({{0, 2, 0}, {0, 6, 0}, .5f, .5f, BridgeSection::Box, BridgePart::Pier});
    auto modified = bakeBridge(first.value());
    REQUIRE(modified.ok());
    REQUIRE(modified.value().getVertexCount() == original.value().getVertexCount() + 24);
}

TEST_CASE("procgen.autoBridge.rejectsInvalidRequestsAndPlans") {
    BridgeParameters p;
    p.end = p.start;
    REQUIRE(!planBridge(p).ok());
    p       = BridgeParameters{};
    p.spans = 10000;
    REQUIRE(!planBridge(p).ok());
    p       = BridgeParameters{};
    p.width = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!planBridge(p).ok());
    p          = BridgeParameters{};
    p.kind     = BridgeKind::Arch;
    p.archRise = 30.f;
    REQUIRE(!planBridge(p).ok());
    p      = BridgeParameters{};
    p.kind = static_cast<BridgeKind>(99);
    REQUIRE(!planBridge(p).ok());
    BridgePlan empty;
    REQUIRE(!bakeBridge(empty).ok());
    p          = BridgeParameters{};
    auto valid = planBridge(p);
    REQUIRE(valid.ok());
    valid.value().members.front().to = valid.value().members.front().from;
    REQUIRE(!bakeBridge(valid.value()).ok());
}

TEST_CASE("procgen.autoBridge.recipeSupportsAllTypesAndToggles") {
    auto& registry = MeshRecipeRegistry::instance();
    registry.registerBuiltins();
    Params params;
    params.setFloat("deckWidth", 9.f);
    params.setFloat("endY", 16.f);
    params.setFloat("endZ", 12.f);
    MeshBuild   mesh;
    std::string error;
    for (const auto& type : {"beam", "slab", "arch", "tiedArch", "throughTruss", "deckTruss", "suspension",
                             "cableStayed", "cantilever", "trestle"}) {
        params.setString("bridgeType", type);
        REQUIRE(registry.generate("mesh.autoBridge", params, mesh, error));
        REQUIRE(hasBridgeGroup(mesh, "deck"));
    }
    params.setString("bridgeType", "beam");
    params.setBool("piers", false);
    params.setBool("railings", false);
    REQUIRE(registry.generate("mesh.autoBridge", params, mesh, error));
    REQUIRE(!hasBridgeGroup(mesh, "pier"));
    REQUIRE(!hasBridgeGroup(mesh, "railing"));
    params.setFloat("deckWidth", std::numeric_limits<float>::quiet_NaN());
    REQUIRE(!registry.generate("mesh.autoBridge", params, mesh, error));
    params.setFloat("deckWidth", 9.f);
    params.setString("piers", "invalid");
    REQUIRE(!registry.generate("mesh.autoBridge", params, mesh, error));
    params.setBool("piers", true);
    params.setString("bridgeType", "unknown");
    const int previous = mesh.getVertexCount();
    REQUIRE(!registry.generate("mesh.autoBridge", params, mesh, error));
    REQUIRE(mesh.getVertexCount() == previous);
    REQUIRE(!error.empty());
}

TEST_CASE("procgen.autoBridge.referencePresetsAndDefaultsShareOneContract") {
    auto& registry = MeshRecipeRegistry::instance();
    registry.registerBuiltins();
    for (int index = 0; index < 10; ++index) {
        auto kind   = static_cast<BridgeKind>(index);
        auto source = bridgePreset(kind);
        REQUIRE(source.ok());
        Params params;
        params.setString("bridgeType", std::string(bridgeKindName(kind)));
        REQUIRE(registry.applyDefaults("mesh.autoBridge", params));
        auto decoded = bridgeParametersFromParams(params);
        REQUIRE(decoded.ok());
        REQUIRE(decoded.value().start.x == source.value().start.x);
        REQUIRE(decoded.value().end.x == source.value().end.x);
        REQUIRE(decoded.value().start.y == source.value().start.y);
        REQUIRE(decoded.value().panels == source.value().panels);
        REQUIRE(decoded.value().railing == source.value().railing);
        REQUIRE(decoded.value().markings == source.value().markings);
        auto planned = planBridge(decoded.value());
        REQUIRE(planned.ok());
        bool hasFoundation = false, hasSteelSection = false;
        for (const auto& member : planned.value().members) {
            REQUIRE(!bridgePartName(member.part).empty());
            REQUIRE(!bridgeMaterialName(member.material).empty());
            if (member.part == BridgePart::Footing) hasFoundation = true;
            if (member.section == BridgeSection::IBeam) hasSteelSection = true;
        }
        REQUIRE(hasFoundation);
        if (kind == BridgeKind::ThroughTruss || kind == BridgeKind::DeckTruss || kind == BridgeKind::Cantilever)
            REQUIRE(hasSteelSection);
        auto baked = bakeBridge(planned.value());
        REQUIRE(baked.ok());
        verifyBridgeTriangles(baked.value());
    }
    Params invalid;
    invalid.setString("bridgeType", "arch");
    invalid.setFloat("archRise", -1.f);
    const auto before = invalid.canonicalString();
    REQUIRE(!applyBridgeRecipeDefaults(invalid).ok());
    REQUIRE(invalid.canonicalString() == before);
}

TEST_CASE("procgen.autoBridge.customSectionsRejectInvalidAndCollapsedGeometry") {
    BridgePlan plan;
    plan.members.push_back({{0, 0, 0}, {4, 0, 0}, 1.f, 2.f, BridgeSection::IBeam, BridgePart::Girder});
    auto section = bakeBridge(plan);
    REQUIRE(section.ok());
    REQUIRE(section.value().getVertexCount() == 72);
    verifyBridgeTriangles(section.value());
    plan.members.front().section = BridgeSection::TaperedBox;
    REQUIRE(!bakeBridge(plan).ok());
    plan.members.front().endWidth  = .5f;
    plan.members.front().endHeight = 1.f;
    auto tapered                   = bakeBridge(plan);
    REQUIRE(tapered.ok());
    verifyBridgeTriangles(tapered.value());
    plan.members.front().material = static_cast<BridgeMaterial>(99);
    REQUIRE(!bakeBridge(plan).ok());
    plan.members.front() = {{99990.f, 99990.f, 99990.f}, {99995.f, 99990.f, 99990.f}, .001f, .001f,
                            BridgeSection::Box,          BridgePart::Girder};
    REQUIRE(!bakeBridge(plan).ok());
}
