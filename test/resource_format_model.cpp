#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "data/ByteData.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
#include "PathBesideSource.h"
EVE_DEFINE_PATH_BESIDE_SOURCE()

void triangle(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, const std::string &extension) {
    const auto    path = pathBesideThisSource(("fixtures/resource_formats/triangle." + extension).c_str());
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    std::vector<char>   bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    eve::data::ByteData source(bytes.data(), bytes.size());
    std::unique_ptr<eve::model3d::ModelData> model(
        eve::model3d::Model3D::create()->newModelData(&source, "." + extension));
    REQUIRE(model != nullptr);
    REQUIRE_EQ(model->getMeshCount(), 1);
    REQUIRE_EQ(model->getVertexCount(0), 3);
    REQUIRE_EQ(model->getFaceCount(0), 1);
    std::vector<std::array<float, 3>> positions;
    for (int vertex = 0; vertex < 3; ++vertex)
        positions.push_back({model->getVertexPosition(0, vertex, 0), model->getVertexPosition(0, vertex, 1),
                             model->getVertexPosition(0, vertex, 2)});
    std::sort(positions.begin(), positions.end());
    const std::array<std::array<float, 3>, 3> expected{{{0, 0, 0}, {0, 3, 0}, {2, 0, 0}}};
    for (size_t vertex = 0; vertex < positions.size(); ++vertex)
        for (size_t component = 0; component < 3; ++component)
            CHECK(std::fabs(positions[vertex][component] - expected[vertex][component]) < 1e-6f);
}
}  // namespace

TEST_CASE("resourceFormats.model.obj.independentTriangle") { triangle(_ZEROERR_TEST_CONTEXT, "obj"); }
TEST_CASE("resourceFormats.model.stl.independentTriangle") { triangle(_ZEROERR_TEST_CONTEXT, "stl"); }
TEST_CASE("resourceFormats.model.ply.independentTriangle") { triangle(_ZEROERR_TEST_CONTEXT, "ply"); }
TEST_CASE("resourceFormats.model.off.independentTriangle") { triangle(_ZEROERR_TEST_CONTEXT, "off"); }
TEST_CASE("resourceFormats.model.x.independentTriangle") { triangle(_ZEROERR_TEST_CONTEXT, "x"); }
TEST_CASE("resourceFormats.model.dae.independentTriangle") { triangle(_ZEROERR_TEST_CONTEXT, "dae"); }
TEST_CASE("resourceFormats.model.gltf.independentTriangle") { triangle(_ZEROERR_TEST_CONTEXT, "gltf"); }
TEST_CASE("resourceFormats.model.glb.independentTriangle") { triangle(_ZEROERR_TEST_CONTEXT, "glb"); }
