#include "data/ByteData.h"
#include "image/ImageData.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <memory>
#include <string>

namespace {
using eve::model3d::ModelData;

constexpr char kUvQuad[] =
    "v -0.5 -0.5 0.5\n"
    "v 0.5 -0.5 0.5\n"
    "v 0.5 0.5 0.5\n"
    "v -0.5 0.5 0.5\n"
    "vt 0 0\n"
    "vt 1 0\n"
    "vt 1 1\n"
    "vt 0 1\n"
    "f 1/1 2/2 3/3\n"
    "f 1/1 3/3 4/4\n";

constexpr char kNoUvQuad[] =
    "v -0.5 -0.5 0.5\n"
    "v 0.5 -0.5 0.5\n"
    "v 0.5 0.5 0.5\n"
    "v -0.5 0.5 0.5\n"
    "f 1 2 3\n"
    "f 1 3 4\n";

std::unique_ptr<ModelData> loadObj(const char* source, bool flipUvs = false) {
    auto*                          modelModule = eve::model3d::Model3D::create();
    eve::data::ByteData            bytes(source, std::char_traits<char>::length(source));
    eve::model3d::ModelLoadOptions options;
    options.flipUVs = flipUvs;
    return std::unique_ptr<ModelData>(modelModule->newModelData(&bytes, ".obj", options));
}

float length(float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); }

// Normal generation contract: explicit origin produces a unit radial vector
// for each vertex, including the component sign of each spatial quadrant.
TEST_CASE("model3d.normals.spec.radial_from_explicit_origin") {
    auto model = loadObj(kUvQuad);
    REQUIRE(model != nullptr);
    REQUIRE(model->applyVertexNormalsFrom(0, "radial", 0.f, 0.f, 0.f).ok());
    const int count = model->getVertexCount(0);
    REQUIRE(count >= 4);
    for (int i = 0; i < count; ++i) {
        const float x      = model->getVertexPosition(0, i, 0);
        const float y      = model->getVertexPosition(0, i, 1);
        const float z      = model->getVertexPosition(0, i, 2);
        const float radius = length(x, y, z);
        REQUIRE(radius > 0.f);
        const float nx = model->getVertexNormal(0, i, 0);
        const float ny = model->getVertexNormal(0, i, 1);
        const float nz = model->getVertexNormal(0, i, 2);
        CHECK(std::fabs(nx - x / radius) < 1e-5f);
        CHECK(std::fabs(ny - y / radius) < 1e-5f);
        CHECK(std::fabs(nz - z / radius) < 1e-5f);
        CHECK(std::fabs(length(nx, ny, nz) - 1.f) < 1e-5f);
    }
}

// Setter contract: input is normalized before storage; invalid mesh or vertex
// index fails and leaves a previously stored normal untouched.
TEST_CASE("model3d.normals.spec.set_normal_and_invalid_indexes") {
    auto model = loadObj(kUvQuad);
    REQUIRE(model != nullptr);
    REQUIRE(model->setVertexNormal(0, 0, 0.f, 4.f, 0.f).ok());
    CHECK_EQ(model->getVertexNormal(0, 0, 0), 0.f);
    CHECK_EQ(model->getVertexNormal(0, 0, 1), 1.f);
    CHECK_EQ(model->getVertexNormal(0, 0, 2), 0.f);
    CHECK(!model->setVertexNormal(-1, 0, 1.f, 0.f, 0.f).ok());
    CHECK(!model->setVertexNormal(0, -1, 1.f, 0.f, 0.f).ok());
    CHECK(!model->setVertexNormal(0, model->getVertexCount(0), 1.f, 0.f, 0.f).ok());
    CHECK_EQ(model->getVertexNormal(0, 0, 1), 1.f);
}

// Generation contract: only radial normals are implemented; an unsupported
// kind and invalid mesh index return structured failures.
TEST_CASE("model3d.normals.spec.rejects_unknown_generation") {
    auto model = loadObj(kUvQuad);
    REQUIRE(model != nullptr);
    auto unknown = model->applyVertexNormals(0, "smooth");
    CHECK(!unknown.ok());
    REQUIRE(unknown.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(unknown.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
    CHECK(!model->applyVertexNormals(-1, "radial").ok());
}

// Baking contract: object-space pixels encode the authored vertex normal in
// RGB after projection onto the source UV quad.
TEST_CASE("model3d.normals.spec.object_space_bake_output") {
    auto model = loadObj(kUvQuad);
    REQUIRE(model != nullptr);
    REQUIRE(model->applyVertexNormalsFrom(0, "radial", 0.f, 0.f, 0.f).ok());
    auto baked = model->bakeNormalMap(0, 32, 32, 0, "object");
    REQUIRE(baked.ok());
    auto image = std::move(baked).takeValue();
    REQUIRE(image != nullptr);
    CHECK_EQ(image->getWidth(), 32);
    CHECK_EQ(image->getHeight(), 32);
    const auto pixel = image->getPixel(28, 3);  // UV near the +X,+Y,+Z corner
    CHECK(pixel.r > 0.6f);
    CHECK(pixel.g > 0.6f);
    CHECK(pixel.b > 0.6f);
}

// Baking input contract: positive dimensions, a known space and an available
// UV channel are required. Failure does not erase authored vertex normals.
TEST_CASE("model3d.normals.spec.bake_rejects_invalid_input") {
    auto model = loadObj(kUvQuad);
    REQUIRE(model != nullptr);
    REQUIRE(model->applyVertexNormals(0, "radial").ok());
    CHECK(!model->bakeNormalMap(-1, 8, 8, 0, "object").ok());
    CHECK(!model->bakeNormalMap(0, 0, 8, 0, "object").ok());
    CHECK(!model->bakeNormalMap(0, 8, -1, 0, "object").ok());
    CHECK(!model->bakeNormalMap(0, 8, 8, 0, "world").ok());
    auto noUv = loadObj(kNoUvQuad);
    REQUIRE(noUv != nullptr);
    REQUIRE(noUv->applyVertexNormals(0, "radial").ok());
    auto missing = noUv->bakeNormalMap(0, 8, 8, 0, "tangent");
    CHECK(!missing.ok());
    REQUIRE(missing.status().primaryDiagnostic() != nullptr);
    CHECK_EQ(missing.status().primaryDiagnostic()->code(), eve::DiagnosticCode::NotFound);
    CHECK_EQ(model->hasNormals(0), true);
}
}  // namespace
