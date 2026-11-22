#include <zeroerr/unittest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/mesh/VesselGenerator.h"
#include "procgen/texture/PotteryTextures.h"
#include "procgen/texture/TextureRecipe.h"
#include "zeroerr/assert.h"
using namespace eve::procgen;
namespace {
void verify(const MeshBuild& mesh, zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    REQUIRE(!mesh.empty());
    using Point = std::tuple<int, int, int>;
    std::map<std::pair<Point, Point>, int> edges;
    auto                                   point = [&](int v) {
        return Point{int(std::lround(mesh.getPositionX(v) * 100000)), int(std::lround(mesh.getPositionY(v) * 100000)),
                     int(std::lround(mesh.getPositionZ(v) * 100000))};
    };
    for (int i = 0; i < mesh.getVertexCount(); ++i) {
        const float x = mesh.getNormalX(i), y = mesh.getNormalY(i), z = mesh.getNormalZ(i);
        CHECK(std::abs(x * x + y * y + z * z - 1.f) < 0.0001f);
        CHECK(std::isfinite(mesh.getPositionX(i)));
    }
    for (int i = 0; i < mesh.getIndexCount(); i += 3) {
        const int a = mesh.getIndex(i), b = mesh.getIndex(i + 1), c = mesh.getIndex(i + 2);
        REQUIRE((a >= 0 && a < mesh.getVertexCount()));
        REQUIRE((b >= 0 && b < mesh.getVertexCount()));
        REQUIRE((c >= 0 && c < mesh.getVertexCount()));
        const float ux = mesh.getPositionX(b) - mesh.getPositionX(a), uy = mesh.getPositionY(b) - mesh.getPositionY(a),
                    uz = mesh.getPositionZ(b) - mesh.getPositionZ(a);
        const float vx = mesh.getPositionX(c) - mesh.getPositionX(a), vy = mesh.getPositionY(c) - mesh.getPositionY(a),
                    vz = mesh.getPositionZ(c) - mesh.getPositionZ(a);
        const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        CHECK(nx * nx + ny * ny + nz * nz > 1e-16f);
        CHECK(nx * mesh.getNormalX(b) + ny * mesh.getNormalY(b) + nz * mesh.getNormalZ(b) > 0.f);
        int vertices[] = {a, b, c};
        for (int j = 0; j < 3; ++j) {
            auto p = point(vertices[j]), q = point(vertices[(j + 1) % 3]);
            if (q < p)
                ++edges[{q, p}];
            else
                ++edges[{p, q}];
        }
    }
    for (const auto& edge : edges) CHECK_EQ(edge.second, 2);
}
}  // namespace
TEST_CASE("procgen.vessel.presetsAreClosedHollowDeterministicSolids") {
    auto& registry = MeshRecipeRegistry::instance();
    registerVesselRecipes(registry);
    for (const auto* shape : {"planter", "jar", "bowl", "plate", "saucer", "cylinder"}) {
        Params p;
        p.setString("shape", shape);
        p.setInt("radialSegments", 24);
        p.setInt("curveSegments", 16);
        MeshBuild   a, b;
        std::string error;
        REQUIRE(registry.generate("mesh.vessel", p, a, error));
        REQUIRE(registry.generate("mesh.vessel", p, b, error));
        CHECK_EQ(a.positions(), b.positions());
        CHECK_EQ(a.indices(), b.indices());
        verify(a, _ZEROERR_TEST_CONTEXT);
        CHECK_EQ(a.getMeta("materialRecipe", ""), std::string(shape) == "planter" ? "pbr.terracotta" : "pbr.porcelain");
        REQUIRE(registry.applyDefaults("mesh.vessel", p));
        REQUIRE(registry.generate("mesh.vessel", p, b, error));
        CHECK_EQ(a.positions(), b.positions());
    }
}
TEST_CASE("procgen.vessel.customCurveAndFailurePreserveOutput") {
    auto& registry = MeshRecipeRegistry::instance();
    registerVesselRecipes(registry);
    Params p;
    p.setFloat("radius0", 0.5f);
    p.setFloat("radius1", 1.4f);
    p.setFloat("radius2", 0.3f);
    p.setFloat("radius3", 0.7f);
    p.setString("material", "celadon");
    MeshBuild   mesh;
    std::string error;
    REQUIRE(registry.generate("mesh.vessel", p, mesh, error));
    CHECK_EQ(mesh.getMeta("materialRecipe", ""), "pbr.celadon");
    verify(mesh, _ZEROERR_TEST_CONTEXT);
    const auto positions = mesh.positions();
    for (const float bad : {-1.f, 0.001f, std::numeric_limits<float>::infinity()}) {
        p.setFloat("vesselHeight", bad);
        CHECK(!registry.generate("mesh.vessel", p, mesh, error));
        CHECK_EQ(mesh.positions(), positions);
    }
}
TEST_CASE("procgen.vessel.rejectsCrossingsReversedAndInvalidSections") {
    for (const auto& profile : std::vector<std::vector<VesselSectionPoint>>{{},
                                                                            {{0, 0}, {1, 1}, {1, 0}, {0, 1}},
                                                                            {{0, 0}, {0, 1}, {1, 1}, {0, 2}},
                                                                            {{0, 1}, {1, 1}, {1, 0}, {0, 0}},
                                                                            {{0, 0}, {1, 0}, {1, 0}, {0, 1}}}) {
        auto result = revolveVesselSection(profile, 32);
        CHECK(!result.ok());
    }
    const std::vector<VesselSectionPoint> profile{{0, 0}, {1, 0}, {1, 1}, {0.8f, 1}, {0.8f, 0.2f}, {0, 0.2f}};
    auto                                  result = revolveVesselSection(profile, 32);
    REQUIRE(result.ok());
    verify(result.value(), _ZEROERR_TEST_CONTEXT);
    auto invalid = revolveVesselSection(profile, 513);
    CHECK(!invalid.ok());
}
TEST_CASE("procgen.vessel.materialDefinitionsHaveDielectricSurfaceContracts") {
    const auto definitions = potteryTextureDefinitions();
    REQUIRE_EQ(definitions.size(), 5u);
    NoiseField noise;
    noise.seed = 42;
    for (const auto& def : definitions) {
        CHECK_EQ(def.pbr.metallic, 0.f);
        CHECK((def.pbr.roughnessLow > 0.f && def.pbr.roughnessHigh < 1.f));
        CHECK(def.albedo.sample(0.f).a == 255);
        for (int i = 0; i < 100; ++i) {
            const float h = def.height(float(i) / 17.f, float(i) / 31.f, noise);
            CHECK((std::isfinite(h) && h >= 0.f && h <= 1.f));
            CHECK_EQ(h, def.height(float(i) / 17.f, float(i) / 31.f, noise));
        }
    }
}
