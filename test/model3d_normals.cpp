#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Diagnostic.h"
#include "common/Exception.h"
#include "common/Status.h"
#include "data/ByteData.h"
#include "image/ImageData.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"

#include <cmath>
#include <memory>
#include <string>

using eve::image::ImageData;

namespace {

// One +Z quad (z=0.5) covering UV 0..1, two triangles. Center of the mesh AABB is origin.
static const char kUvQuadObj[] =
    "v -0.5 -0.5  0.5\n"
    "v  0.5 -0.5  0.5\n"
    "v  0.5  0.5  0.5\n"
    "v -0.5  0.5  0.5\n"
    "vt 0 0\n"
    "vt 1 0\n"
    "vt 1 1\n"
    "vt 0 1\n"
    "f 1/1 2/2 3/3\n"
    "f 1/1 3/3 4/4\n";

static const char kNoUvCubeObj[] =
    "v -0.5 -0.5 -0.5\n"
    "v  0.5 -0.5 -0.5\n"
    "v  0.5  0.5 -0.5\n"
    "v -0.5  0.5 -0.5\n"
    "f 1 2 3\n"
    "f 1 3 4\n";

eve::model3d::ModelData *loadObj(const char *text) {
    auto *mod = eve::model3d::Model3D::create();
    eve::data::ByteData data(text, std::char_traits<char>::length(text));
    return mod->newModelData(&data, ".obj");
}

float length3(float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); }

}  // namespace

TEST_CASE("model3d.normals.applyRadialFromOrigin") {
    std::unique_ptr<eve::model3d::ModelData> md(loadObj(kUvQuadObj));
    REQUIRE(md.get() != nullptr);
    REQUIRE(md->getMeshCount() >= 1);

    auto applied = md->applyVertexNormalsFrom(0, "radial", 0.f, 0.f, 0.f);
    CHECK(applied.ok());
    CHECK_EQ(applied.code(), eve::StatusCode::Applied);

    const int n = md->getVertexCount(0);
    REQUIRE(n >= 4);
    for (int i = 0; i < n; ++i) {
        const float px = md->getVertexPosition(0, i, 0);
        const float py = md->getVertexPosition(0, i, 1);
        const float pz = md->getVertexPosition(0, i, 2);
        const float nx = md->getVertexNormal(0, i, 0);
        const float ny = md->getVertexNormal(0, i, 1);
        const float nz = md->getVertexNormal(0, i, 2);
        const float len = length3(px, py, pz);
        REQUIRE(len > 1e-6f);
        CHECK(std::fabs(nx - px / len) < 1e-5f);
        CHECK(std::fabs(ny - py / len) < 1e-5f);
        CHECK(std::fabs(nz - pz / len) < 1e-5f);
        CHECK(std::fabs(length3(nx, ny, nz) - 1.f) < 1e-5f);
    }
}

TEST_CASE("model3d.normals.setVertexNormal") {
    std::unique_ptr<eve::model3d::ModelData> md(loadObj(kUvQuadObj));
    REQUIRE(md.get() != nullptr);
    auto set = md->setVertexNormal(0, 0, 0.f, 1.f, 0.f);
    CHECK(set.ok());
    CHECK_EQ(md->getVertexNormal(0, 0, 0), 0.f);
    CHECK_EQ(md->getVertexNormal(0, 0, 1), 1.f);
    CHECK_EQ(md->getVertexNormal(0, 0, 2), 0.f);
}

TEST_CASE("model3d.normals.applyUnknownKindFails") {
    std::unique_ptr<eve::model3d::ModelData> md(loadObj(kUvQuadObj));
    REQUIRE(md.get() != nullptr);
    auto failed = md->applyVertexNormals(0, "smooth");
    CHECK(!failed.ok());
    REQUIRE(failed.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(failed.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
}

TEST_CASE("model3d.normals.bakeObjectSpaceCorner") {
    std::unique_ptr<eve::model3d::ModelData> md(loadObj(kUvQuadObj));
    REQUIRE(md.get() != nullptr);
    REQUIRE(md->applyVertexNormalsFrom(0, "radial", 0.f, 0.f, 0.f).ok());

    auto baked = md->bakeNormalMap(0, 32, 32, 0, "object");
    REQUIRE(baked.ok());
    std::unique_ptr<ImageData> map = std::move(baked).takeValue();
    REQUIRE(map.get() != nullptr);
    CHECK_EQ(map->getWidth(), 32);
    CHECK_EQ(map->getHeight(), 32);

    // UV (0.9, 0.9) is near vertex (0.5, 0.5, 0.5); image Y = (1-v)*height.
    const auto pixel = map->getPixel(28, 3);
    const float nx = pixel.r * 2.f - 1.f;
    const float ny = pixel.g * 2.f - 1.f;
    const float nz = pixel.b * 2.f - 1.f;
    const float len = length3(nx, ny, nz);
    REQUIRE(len > 0.5f);
    CHECK(nx > 0.2f);
    CHECK(ny > 0.2f);
    CHECK(nz > 0.2f);
}

TEST_CASE("model3d.normals.bakeRequiresUv") {
    std::unique_ptr<eve::model3d::ModelData> md(loadObj(kNoUvCubeObj));
    REQUIRE(md.get() != nullptr);
    REQUIRE(md->applyVertexNormals(0, "radial").ok());
    auto baked = md->bakeNormalMap(0, 8, 8, 0, "tangent");
    CHECK(!baked.ok());
    REQUIRE(baked.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(baked.status().primaryDiagnostic()->code(), eve::DiagnosticCode::NotFound);
}
