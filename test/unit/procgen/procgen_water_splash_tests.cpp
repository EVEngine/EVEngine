#include "procgen/Params.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/algorithms/WaterSplashMesh.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <string>

using eve::procgen::MeshBuild;
using eve::procgen::MeshRecipeRegistry;
using eve::procgen::Params;

namespace {

bool meshIndicesInRange(const MeshBuild &mesh) {
    for (int i = 0; i < mesh.getIndexCount(); ++i) {
        const int idx = mesh.getIndex(i);
        if (idx < 0 || idx >= mesh.getVertexCount()) return false;
    }
    return true;
}

float maxHeight(const MeshBuild &mesh) {
    float y = 0.f;
    for (int i = 0; i < mesh.getVertexCount(); ++i) y = std::max(y, mesh.getPositionY(i));
    return y;
}

float maxRadius(const MeshBuild &mesh) {
    float r = 0.f;
    for (int i = 0; i < mesh.getVertexCount(); ++i) {
        const float x = mesh.getPositionX(i);
        const float z = mesh.getPositionZ(i);
        r             = std::max(r, std::sqrt(x * x + z * z));
    }
    return r;
}

}  // namespace

TEST_CASE("procgen.waterSplash.spec.deterministicCrownsAndFoam") {
    SUB_CASE("same seed replays identical geometry and colors") {
        Params params;
        params.setSeed(20261103u);
        params.setFloat("height", 2.4f);
        params.setFloat("width", 3.2f);
        params.setInt("layers", 4);
        params.setFloat("jaggedness", 0.8f);
        params.setFloat("foam", 0.9f);

        MeshBuild   a;
        MeshBuild   b;
        std::string error;
        REQUIRE(eve::procgen::generateWaterSplashMesh(params, a, error));
        REQUIRE(eve::procgen::generateWaterSplashMesh(params, b, error));
        CHECK(a.getVertexCount() > 32);
        CHECK_EQ(a.getIndexCount() % 3, 0);
        CHECK(meshIndicesInRange(a));
        CHECK(a.hasVertexColors());
        CHECK_EQ(a.positions(), b.positions());
        CHECK_EQ(a.indices(), b.indices());
        CHECK_EQ(a.colors(), b.colors());
        CHECK(a.getGroupCount() >= 4);
        CHECK(maxHeight(a) > 1.5f);
        CHECK(maxRadius(a) > 1.0f);
    };

    SUB_CASE("jaggedness shortens the torn crown lip") {
        Params smooth;
        smooth.setSeed(99u);
        smooth.setFloat("jaggedness", 0.0f);
        Params jagged;
        jagged.setSeed(99u);
        jagged.setFloat("jaggedness", 1.0f);

        MeshBuild   a;
        MeshBuild   b;
        std::string error;
        REQUIRE(eve::procgen::generateWaterSplashMesh(smooth, a, error));
        REQUIRE(eve::procgen::generateWaterSplashMesh(jagged, b, error));
        CHECK(maxHeight(b) < maxHeight(a));
        CHECK(a.positions() != b.positions());
    };

    SUB_CASE("registry recipe mesh.water_splash is available") {
        auto &registry = MeshRecipeRegistry::instance();
        registry.registerBuiltins();
        REQUIRE(registry.has("mesh.water_splash"));
        Params params;
        params.setSeed(7u);
        MeshBuild   mesh;
        std::string error;
        REQUIRE(registry.generate("mesh.water_splash", params, mesh, error));
        CHECK(mesh.getVertexCount() > 0);
        CHECK(mesh.hasVertexColors());
    };
}
