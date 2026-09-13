#include "procgen/Params.h"
#include "procgen/algorithms/MarchingCubes.h"

#include <zeroerr/unittest.h>

#include <algorithm>
#include <cmath>

using namespace eve::procgen;

TEST_CASE("procgen.meshDeformationGeometry.extendedPlaneSupportsCurvatureAndSharpNormals") {
    auto& registry = MeshRecipeRegistry::instance();
    registry.registerBuiltins();
    Params smooth;
    smooth.setInt("segmentsX", 4);
    smooth.setInt("segmentsZ", 3);
    smooth.setFloat("width", 4.f);
    smooth.setFloat("depth", 3.f);
    smooth.setFloat("curvature", 1.f);
    MeshBuild   smoothMesh;
    std::string error;
    REQUIRE(registry.generate("mesh.extendedPlane", smooth, smoothMesh, error));
    CHECK_EQ(smoothMesh.getVertexCount(), 20);
    CHECK_EQ(smoothMesh.getIndexCount(), 72);
    float maximumY = 0.f;
    for (int index = 0; index < smoothMesh.getVertexCount(); ++index)
        maximumY = std::max(maximumY, smoothMesh.getPositionY(index));
    CHECK(maximumY > 0.99f);

    Params sharp = smooth;
    sharp.setBool("sharpNormals", true);
    MeshBuild sharpMesh;
    REQUIRE(registry.generate("mesh.extendedPlane", sharp, sharpMesh, error));
    CHECK_EQ(sharpMesh.getVertexCount(), 48);
    CHECK_EQ(sharpMesh.getMeta("normalMode", ""), "sharp");
}

TEST_CASE("procgen.meshDeformationGeometry.hexGridIsDeterministicAndConfigurable") {
    auto& registry = MeshRecipeRegistry::instance();
    registry.registerBuiltins();
    Params params;
    params.setInt("columns", 3);
    params.setInt("rows", 2);
    params.setFloat("radius", 0.5f);
    params.setFloat("height", 0.2f);
    params.setFloat("randomHeight", 1.f);
    params.setSeed(77);
    MeshBuild   a, b;
    std::string error;
    REQUIRE(registry.generate("mesh.hexGrid", params, a, error));
    REQUIRE(registry.generate("mesh.hexGrid", params, b, error));
    CHECK_EQ(a.positions(), b.positions());
    CHECK_EQ(a.getVertexCount(), 42);
    CHECK_EQ(a.getIndexCount(), 108);

    params.setBool("planar", true);
    params.setBool("flipFaces", true);
    MeshBuild planar;
    REQUIRE(registry.generate("mesh.hexGrid", params, planar, error));
    for (int index = 0; index < planar.getVertexCount(); ++index) CHECK(std::abs(planar.getPositionY(index)) < 1e-6f);
    CHECK(planar.getNormalY(0) < 0.f);
}
