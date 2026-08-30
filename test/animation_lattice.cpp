#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimLattice.h"
#include "animation/Animation.h"

#include "common/Exception.h"
#include "data/ByteData.h"
#include "filesystem/Filesystem.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace eve::animation;

namespace {

bool approx(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

/** @brief Minimal 4-vertex quad OBJ used to exercise the ModelData bind path. */
const char* kQuadObj =
    "v -1 -1 0\n"
    "v 1 -1 0\n"
    "v 1 1 0\n"
    "v -1 1 0\n"
    "f 1 2 3\n"
    "f 1 3 4\n";

eve::model3d::ModelData* loadQuadModel() {
    auto* fs = eve::filesystem::Filesystem::create();
    if (!fs) return nullptr;
    fs->setIdentity("animation_lattice_test", true);
    fs->setupWriteDirectory();
    eve::data::ByteData bytes(kQuadObj, std::strlen(kQuadObj));
    auto*               mod = eve::model3d::Model3D::create();
    return mod->newModelData(&bytes, ".obj");
}

}  // namespace

TEST_CASE("animation.lattice.identityDefault") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);

    const float bind[9] = {-0.75f, 0.25f, 0.5f, 0.1f, -0.3f, 0.9f, 0.4f, 0.6f, -0.8f};
    lat.bindPositions(bind, 3);
    REQUIRE(lat.getVertexCount() == 3);

    std::vector<float> out;
    REQUIRE(lat.updateDeformedPositions());
    for (int v = 0; v < 3; ++v) {
        CHECK(approx(lat.getDeformedPositionX(v), bind[v * 3 + 0]));
        CHECK(approx(lat.getDeformedPositionY(v), bind[v * 3 + 1]));
        CHECK(approx(lat.getDeformedPositionZ(v), bind[v * 3 + 2]));
    }

    // Default-constructed lattice is a valid identity too.
    AnimLattice def;
    CHECK(def.getPointCount() == 8);
    std::vector<float> defOut;
    REQUIRE(def.deformPositionsTo({0.5f, 0.5f, 0.5f}, defOut));
    CHECK(approx(defOut[0], 0.5f));
    CHECK(approx(defOut[1], 0.5f));
    CHECK(approx(defOut[2], 0.5f));
}

TEST_CASE("animation.lattice.uniformScale") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.setScale(2.f, 1.f, 0.5f);

    const float bind[9] = {0.5f, 0.f, 0.f, 0.f, 0.f, 0.f, -0.5f, 0.75f, 1.f};
    lat.bindPositions(bind, 3);
    REQUIRE(lat.updateDeformedPositions());

    // Scaling happens about the lattice origin.
    CHECK(approx(lat.getDeformedPositionX(0), 1.f));
    CHECK(approx(lat.getDeformedPositionY(0), 0.f));
    CHECK(approx(lat.getDeformedPositionZ(0), 0.f));
    CHECK(approx(lat.getDeformedPositionX(1), 0.f));
    CHECK(approx(lat.getDeformedPositionY(1), 0.f));
    CHECK(approx(lat.getDeformedPositionZ(1), 0.f));
    CHECK(approx(lat.getDeformedPositionX(2), -1.f));
    CHECK(approx(lat.getDeformedPositionY(2), 0.75f));
    CHECK(approx(lat.getDeformedPositionZ(2), 0.5f));
}

TEST_CASE("animation.lattice.cornerScaleTrilinear") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.setPointScale(1, 1, 1, 2.f, 1.f, 1.f);

    // Corner vertex: exactly the scaled control point.
    std::vector<float> corner = {1.f, 1.f, 1.f};
    std::vector<float> out;
    REQUIRE(lat.deformPositionsTo(corner, out));
    CHECK(approx(out[0], 2.f));
    CHECK(approx(out[1], 1.f));
    CHECK(approx(out[2], 1.f));

    // Center vertex: all 8 weights are 1/8 → scale x = 1 + 1/8.
    std::vector<float> center = {0.5f, 0.5f, 0.5f};
    REQUIRE(lat.deformPositionsTo(center, out));
    CHECK(approx(out[0], 0.5f * 1.125f));
    CHECK(approx(out[1], 0.5f));
    CHECK(approx(out[2], 0.5f));
}

TEST_CASE("animation.lattice.offsetBulge") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.setPointOffset(1, 1, 1, 0.4f, -0.2f, 0.f);

    std::vector<float> corner = {1.f, 1.f, 1.f};
    std::vector<float> out;
    REQUIRE(lat.deformPositionsTo(corner, out));
    CHECK(approx(out[0], 1.4f));
    CHECK(approx(out[1], 0.8f));
    CHECK(approx(out[2], 1.f));

    // Center: offset diluted by 1/8, position unchanged otherwise.
    std::vector<float> center = {0.5f, 0.5f, 0.5f};
    REQUIRE(lat.deformPositionsTo(center, out));
    CHECK(approx(out[0], 0.5f + 0.4f / 8.f));
    CHECK(approx(out[1], 0.5f - 0.2f / 8.f));
    CHECK(approx(out[2], 0.5f));
}

TEST_CASE("animation.lattice.clampVsExtrapolate") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.setPointScale(1, 0, 0, 2.f, 1.f, 1.f);

    // Vertex outside the box on +x: u = 1.5.
    std::vector<float> p = {2.f, 0.f, 0.f};
    std::vector<float> out;

    lat.setClamp(true);
    REQUIRE(lat.deformPositionsTo(p, out));
    CHECK(approx(out[0], 4.f));  // clamped to the corner cell scale

    lat.setClamp(false);
    REQUIRE(lat.deformPositionsTo(p, out));
    CHECK(approx(out[0], 5.f));  // linear extrapolation keeps growing
}

TEST_CASE("animation.lattice.normals") {
    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.setScale(2.f, 1.f, 1.f);

    const float pos[9] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    const float nrm[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f};
    float       out[9];
    REQUIRE(lat.deformNormals(pos, nrm, out, 3));

    CHECK(approx(out[0], 1.f));
    CHECK(approx(out[1], 0.f));
    CHECK(approx(out[2], 0.f));
    CHECK(approx(out[3], 0.f));
    CHECK(approx(out[4], 1.f));
    CHECK(approx(out[5], 0.f));
    // (2,1,0) normalized.
    const float inv = 1.f / std::sqrt(5.f);
    CHECK(approx(out[6], 2.f * inv));
    CHECK(approx(out[7], 1.f * inv));
    CHECK(approx(out[8], 0.f));
}

TEST_CASE("animation.lattice.bindModelAndFromModel") {
    std::unique_ptr<eve::model3d::ModelData> model(loadQuadModel());
    REQUIRE(model.get() != nullptr);
    REQUIRE(model->getMeshCount() >= 1);
    REQUIRE(model->getVertexCount(0) == 4);

    AnimLattice lat(2, 2, 2);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);
    lat.bindModel(model.get(), 0);
    REQUIRE(lat.getVertexCount() == 4);

    // Bind positions match the OBJ vertices.
    CHECK(approx(lat.getBindPositionX(0), -1.f));
    CHECK(approx(lat.getBindPositionY(1), -1.f));
    CHECK(approx(lat.getBindPositionZ(2), 0.f));

    // fromModel factory binds the same vertices with identity deformation.
    std::unique_ptr<AnimLattice> fromModel(AnimLattice::fromModel(model.get(), 0, 2, 2, 2));
    REQUIRE(fromModel.get() != nullptr);
    REQUIRE(fromModel->getVertexCount() == 4);
    REQUIRE(fromModel->updateDeformedPositions());
    CHECK(approx(fromModel->getDeformedPositionX(0), -1.f));
    CHECK(approx(fromModel->getDeformedPositionY(3), 1.f));
}

TEST_CASE("animation.lattice.validation") {
    AnimLattice lat(2, 2, 2);

    CHECK_THROWS((lat.setDivisions(1, 2, 2), false));
    CHECK_THROWS((lat.setDivisions(2, 0, 2), false));
    CHECK_THROWS((lat.setSize(0.f, 1.f, 1.f), false));
    CHECK_THROWS((lat.setPointScale(2, 0, 0, 1.f, 1.f, 1.f), false));
    CHECK_THROWS((lat.setPointOffset(0, -1, 0, 0.f, 0.f, 0.f), false));
    CHECK_THROWS((lat.getPointScaleX(0, 0, 2), false));
    CHECK_THROWS((lat.bindPositions(nullptr, 3), false));
    CHECK_THROWS((lat.getBindPositionX(0), false));
    CHECK_THROWS((lat.getDeformedPositionX(0), false));

    const float pos[3] = {0.f, 0.f, 0.f};
    lat.bindPositions(pos, 1);
    CHECK_THROWS((lat.getBindPositionY(1), false));
}

TEST_CASE("animation.lattice.cacheMatchesArray") {
    AnimLattice lat(3, 3, 3);
    lat.setSize(2.f, 2.f, 2.f);
    lat.setOrigin(0.f, 0.f, 0.f);

    std::vector<float> pos = {-0.8f, 0.2f, 0.1f, 0.7f, -0.6f, 0.9f, 0.f, 0.f, 0.f};
    std::vector<float> nrm = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    lat.bindPositions(pos.data(), 3);
    lat.setPointScale(1, 1, 1, 1.5f, 1.f, 1.f);

    REQUIRE(lat.updateDeformedPositions(pos));
    REQUIRE(lat.updateDeformedNormals(pos, nrm));

    const std::vector<float> outPos = lat.getDeformedPositions();
    const std::vector<float> outNrm = lat.getDeformedNormals();
    REQUIRE(outPos.size() == 9u);
    REQUIRE(outNrm.size() == 9u);
    for (int v = 0; v < 3; ++v) {
        CHECK(approx(lat.getDeformedPositionX(v), outPos[v * 3 + 0]));
        CHECK(approx(lat.getDeformedPositionY(v), outPos[v * 3 + 1]));
        CHECK(approx(lat.getDeformedPositionZ(v), outPos[v * 3 + 2]));
    }
    CHECK(approx(outNrm[0], 0.f));
    CHECK(approx(outNrm[1], 0.f));
    CHECK(approx(outNrm[2], 1.f));

    lat.clearBind();
    CHECK(lat.getVertexCount() == 0);
    CHECK(lat.getDeformedPositions().empty());
}
