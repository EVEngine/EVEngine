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

TEST_CASE("procgen.waterSplash.spec.claySheetCrowns") {
    SUB_CASE("same seed replays identical clay-sheet geometry") {
        Params params;
        params.setSeed(20261103u);
        params.setFloat("height", 2.4f);
        params.setFloat("width", 3.2f);
        params.setInt("layers", 4);

        MeshBuild   a;
        MeshBuild   b;
        std::string error;
        REQUIRE(eve::procgen::generateWaterSplashMesh(params, a, error));
        REQUIRE(eve::procgen::generateWaterSplashMesh(params, b, error));
        CHECK(a.getVertexCount() > 32);
        CHECK_EQ(a.getIndexCount() % 3, 0);
        CHECK(meshIndicesInRange(a));
        CHECK(!a.hasVertexColors());
        CHECK_EQ(a.positions(), b.positions());
        CHECK_EQ(a.indices(), b.indices());
        CHECK(a.getGroupCount() >= 4);
        CHECK(maxHeight(a) > 1.5f);
        CHECK(maxRadius(a) > 1.0f);
        CHECK_EQ(a.getMeta("sheet", ""), std::string("clay"));
    };

    SUB_CASE("UV.v spans the sheet for material alpha") {
        Params params;
        params.setSeed(7u);
        params.setInt("layers", 1);
        params.setInt("onlyLayer", 0);
        MeshBuild   mesh;
        std::string error;
        REQUIRE(eve::procgen::generateWaterSplashMesh(params, mesh, error));
        float vmin = 1.f;
        float vmax = 0.f;
        for (int i = 0; i < mesh.getVertexCount(); ++i) {
            vmin = std::min(vmin, mesh.getUvV(i));
            vmax = std::max(vmax, mesh.getUvV(i));
        }
        CHECK(vmin <= 0.001f);
        CHECK(vmax >= 0.999f);
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
    };
}
