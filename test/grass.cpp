#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "graphics/Grass.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/shaders/mesh3d_grass_frag_spv.inc"
#include "graphics/shaders/mesh3d_grass_vert_spv.inc"

#include <cmath>
#include <cstdint>
#include <vector>

using eve::graphics::GrassField;
using eve::graphics::Shader;
using eve::graphics::grass::BillboardMesh;
using eve::graphics::grass::Point;
using eve::graphics::grass::SampleParams;

namespace {

void makeTestPlane(std::vector<float> &pos, std::vector<float> &nrm, std::vector<uint32_t> &idx) {
    eve::graphics::grass::makePlane(4.f, 4.f, 4, 4, pos, nrm, idx);
}

float minPairDist(const std::vector<Point> &pts) {
    float best = 1e9f;
    for (size_t i = 0; i < pts.size(); ++i) {
        for (size_t j = i + 1; j < pts.size(); ++j) {
            const glm::vec3 d = pts[i].position - pts[j].position;
            best = std::min(best, glm::length(d));
        }
    }
    return best;
}

}  // namespace

TEST_CASE("graphics.Grass.bindDefaults") {
    Shader sh;
    sh.setKind(Shader::Kind::eMesh3D);
    eve::graphics::grass::bindDefaults(&sh);
    CHECK(sh.hasUniform("time"));
    CHECK(sh.hasUniform("frameDuration"));
    CHECK(sh.hasUniform("lightGreen"));
    CHECK(sh.hasUniform("darkGreen"));
    CHECK(sh.hasUniform("alwaysDark"));
    CHECK_EQ(sh.getUniformIndex("time"), 0);
    CHECK_EQ(sh.getUniformIndex("alwaysDark"), 5);
    CHECK_EQ(sh.getUniformIndex("lightGreen"), 6);
    CHECK_EQ(sh.getUniformIndex("darkGreen"), 9);
    CHECK_EQ(sh.getUniformIndex("frameCount"), 12);
    CHECK_EQ(sh.usedFloats(), 13);
}

TEST_CASE("graphics.Grass.paramNames") {
    CHECK_EQ(eve::graphics::grass::paramCount(), 13);
    CHECK_EQ(eve::graphics::grass::paramName(0), std::string("time"));
    CHECK_EQ(eve::graphics::grass::paramName(5), std::string("alwaysDark"));
    CHECK_EQ(eve::graphics::grass::paramName(12), std::string("frameCount"));
}

TEST_CASE("graphics.Grass.spvMagic") {
    CHECK(mesh3d_grass_vert_spv_count > 0);
    CHECK(mesh3d_grass_frag_spv_count > 0);
    CHECK_EQ(mesh3d_grass_vert_spv[0], 0x07230203u);
    CHECK_EQ(mesh3d_grass_frag_spv[0], 0x07230203u);
}

TEST_CASE("graphics.Grass.poissonMinDistance") {
    std::vector<float> pos, nrm;
    std::vector<uint32_t> idx;
    makeTestPlane(pos, nrm, idx);

    SampleParams p;
    p.radius = 0.45f;
    p.maxPoints = 200;
    p.seed = 7;
    p.minSlopeDot = 0.1f;
    const auto pts =
        eve::graphics::grass::samplePoisson(pos.data(), nrm.data(), int(pos.size() / 3), idx.data(),
                                            int(idx.size()), p);
    CHECK(pts.size() >= 8);
    CHECK(int(pts.size()) <= p.maxPoints);
    const float minD = minPairDist(pts);
    CHECK(minD + 1e-4f >= p.radius);

    for (const auto &pt : pts) {
        CHECK(std::abs(pt.position.y) < 1e-4f);
        CHECK(pt.position.x >= -2.01f);
        CHECK(pt.position.x <= 2.01f);
        CHECK(pt.position.z >= -2.01f);
        CHECK(pt.position.z <= 2.01f);
    }
}

TEST_CASE("graphics.Grass.haltonCountAndCoverage") {
    std::vector<float> pos, nrm;
    std::vector<uint32_t> idx;
    makeTestPlane(pos, nrm, idx);

    const auto pts = eve::graphics::grass::sampleHalton(
        pos.data(), nrm.data(), int(pos.size() / 3), idx.data(), int(idx.size()), 64, 3, 0.1f);
    CHECK_EQ(int(pts.size()), 64);
    CHECK_EQ(pts.front().id, 0u);
    CHECK_EQ(pts.back().id, 63u);

    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const auto &pt : pts) {
        minX = std::min(minX, pt.position.x);
        maxX = std::max(maxX, pt.position.x);
        minZ = std::min(minZ, pt.position.z);
        maxZ = std::max(maxZ, pt.position.z);
    }
    CHECK(maxX - minX > 2.f);
    CHECK(maxZ - minZ > 2.f);
}

TEST_CASE("graphics.Grass.sparseIsSparser") {
    std::vector<float> pos, nrm;
    std::vector<uint32_t> idx;
    makeTestPlane(pos, nrm, idx);

    SampleParams dense;
    dense.radius = 0.3f;
    dense.maxPoints = 400;
    dense.seed = 1;
    SampleParams sparse = dense;
    sparse.radius = 1.2f;
    sparse.maxPoints = 80;
    sparse.seed = 99;

    const auto d = eve::graphics::grass::samplePoisson(
        pos.data(), nrm.data(), int(pos.size() / 3), idx.data(), int(idx.size()), dense);
    const auto s = eve::graphics::grass::samplePoisson(
        pos.data(), nrm.data(), int(pos.size() / 3), idx.data(), int(idx.size()), sparse);
    CHECK(d.size() > s.size());
    CHECK(s.size() >= 1);
}

TEST_CASE("graphics.Grass.billboardRootPivot") {
    std::vector<Point> pts(1);
    pts[0].position = glm::vec3(3.f, 1.5f, -2.f);
    pts[0].id = 11;
    pts[0].scale = 1.25f;
    const BillboardMesh mesh = eve::graphics::grass::buildBillboards(pts, 0.4f, 0.8f);
    CHECK_EQ(int(mesh.posXYZ.size()), 12);
    CHECK_EQ(int(mesh.uvST.size()), 8);
    CHECK_EQ(int(mesh.indices.size()), 6);
    for (int i = 0; i < 4; ++i) {
        CHECK(std::abs(mesh.posXYZ[size_t(i * 3 + 0)] - 3.f) < 1e-5f);
        CHECK(std::abs(mesh.posXYZ[size_t(i * 3 + 1)] - 1.5f) < 1e-5f);
        CHECK(std::abs(mesh.posXYZ[size_t(i * 3 + 2)] + 2.f) < 1e-5f);
        CHECK(std::abs(mesh.nrmXYZ[size_t(i * 3 + 0)] - 11.f) < 1e-5f);
        CHECK(std::abs(mesh.nrmXYZ[size_t(i * 3 + 1)] - 1.25f) < 1e-5f);
    }
    // Bottom edge UVs sit at v=0; their midpoint is the root (u=0.5).
    CHECK(std::abs(mesh.uvST[1]) < 1e-5f);
    CHECK(std::abs(mesh.uvST[3]) < 1e-5f);
    CHECK(std::abs(0.5f * (mesh.uvST[0] + mesh.uvST[2]) - 0.5f) < 1e-5f);
}

TEST_CASE("graphics.Grass.swayDesyncByInstanceId") {
    const int a = eve::graphics::grass::swayFrame(0.f, 0.12f, 0, 4);
    const int b = eve::graphics::grass::swayFrame(0.f, 0.12f, 1, 4);
    const int c = eve::graphics::grass::swayFrame(0.f, 0.12f, 2, 4);
    CHECK(a >= 0);
    CHECK(a < 4);
    CHECK(b >= 0);
    CHECK(b < 4);
    CHECK(c >= 0);
    CHECK(c < 4);
    CHECK((a != b || b != c));

    const int later = eve::graphics::grass::swayFrame(0.48f, 0.12f, 0, 4);
    CHECK(later != a);
}

TEST_CASE("graphics.Grass.swayAtlasFourFrames") {
    std::vector<uint8_t> rgba;
    eve::graphics::grass::makeSwayAtlasRGBA(32, 32, 4, rgba);
    CHECK_EQ(eve::graphics::grass::swayAtlasWidth(32, 4), 128);
    CHECK_EQ(eve::graphics::grass::swayAtlasHeight(32), 32);
    CHECK_EQ(int(rgba.size()), 128 * 32 * 4);

    int opaque = 0;
    int perFrame[4] = {};
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 128; ++x) {
            const uint8_t a = rgba[(size_t(y) * 128u + size_t(x)) * 4u + 3u];
            if (a > 16) {
                ++opaque;
                perFrame[x / 32]++;
            }
        }
    }
    CHECK(opaque > 80);
    for (int f = 0; f < 4; ++f) CHECK(perFrame[f] > 10);

    // Frames actually differ (wind lean).
    int diffs = 0;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const uint8_t a0 = rgba[(size_t(y) * 128u + size_t(x)) * 4u + 3u];
            const uint8_t a3 = rgba[(size_t(y) * 128u + size_t(x + 96)) * 4u + 3u];
            if (a0 != a3) ++diffs;
        }
    }
    CHECK(diffs > 8);
}

TEST_CASE("graphics.Grass.layerFlag") {
    Shader sh;
    sh.setKind(Shader::Kind::eMesh3D);
    eve::graphics::grass::bindDefaults(&sh);
    eve::graphics::grass::bindLayer(&sh, true);
    float dark = 0.f;
    REQUIRE_EQ(sh.getFromVar("alwaysDark", &dark, sizeof(dark)), int(sizeof(dark)));
    CHECK(dark > 0.5f);
    eve::graphics::grass::bindLayer(&sh, false);
    REQUIRE_EQ(sh.getFromVar("alwaysDark", &dark, sizeof(dark)), int(sizeof(dark)));
    CHECK(dark < 0.5f);
}
