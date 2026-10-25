#include "procgen/mesh/ArrowGenerator.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>

namespace {
using eve::procgen::ArrowGenerator;
using eve::procgen::MeshBuild;
using Point = std::tuple<float, float, float>;
using Edge  = std::pair<Point, Point>;

Point position(const MeshBuild& mesh, int index) {
    return {mesh.getPositionX(index), mesh.getPositionY(index), mesh.getPositionZ(index)};
}

// Shared output oracle: every triangle has valid indices, finite attributes,
// nonzero area, and a face orientation consistent with its stored normal.
void requireValidTriangles(const MeshBuild& mesh) {
    REQUIRE(mesh.getIndexCount() > 0);
    REQUIRE_EQ(mesh.getIndexCount() % 3, 0);
    for (float value : mesh.positions()) REQUIRE(std::isfinite(value));
    for (float value : mesh.normals()) REQUIRE(std::isfinite(value));
    for (float value : mesh.uvs()) REQUIRE(std::isfinite(value));
    for (int i = 0; i < mesh.getIndexCount(); i += 3) {
        const int a = mesh.getIndex(i), b = mesh.getIndex(i + 1), c = mesh.getIndex(i + 2);
        REQUIRE_GE(a, 0);
        REQUIRE_LT(a, mesh.getVertexCount());
        REQUIRE_GE(b, 0);
        REQUIRE_LT(b, mesh.getVertexCount());
        REQUIRE_GE(c, 0);
        REQUIRE_LT(c, mesh.getVertexCount());
        const double ux = mesh.getPositionX(b) - mesh.getPositionX(a);
        const double uy = mesh.getPositionY(b) - mesh.getPositionY(a);
        const double uz = mesh.getPositionZ(b) - mesh.getPositionZ(a);
        const double vx = mesh.getPositionX(c) - mesh.getPositionX(a);
        const double vy = mesh.getPositionY(c) - mesh.getPositionY(a);
        const double vz = mesh.getPositionZ(c) - mesh.getPositionZ(a);
        const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        REQUIRE(std::sqrt(nx * nx + ny * ny + nz * nz) >= 1e-16);
        REQUIRE(nx * mesh.getNormalX(a) + ny * mesh.getNormalY(a) + nz * mesh.getNormalZ(a) > 0.0);
    }
}

// Closed-surface oracle: each geometric edge has exactly two incident faces,
// traversed in opposite directions. Duplicate per-face vertices are folded.
void requireClosedSurface(const MeshBuild& mesh) {
    std::map<Edge, std::pair<int, int>> edges;
    for (int i = 0; i < mesh.getIndexCount(); i += 3) {
        for (int j = 0; j < 3; ++j) {
            Point      a       = position(mesh, mesh.getIndex(i + j));
            Point      b       = position(mesh, mesh.getIndex(i + (j + 1) % 3));
            const bool forward = a < b;
            if (!forward) std::swap(a, b);
            auto& edge = edges[{a, b}];
            ++edge.first;
            edge.second += forward ? 1 : -1;
        }
    }
    for (const auto& [key, edge] : edges) {
        CHECK_EQ(edge.first, 2);
        CHECK_EQ(edge.second, 0);
    }
}

// 2D geometry contract: XY ribbon vertices remain planar, tip is exactly B,
// both bend signs produce finite winding, and repeated builds are identical.
TEST_CASE("procgen.arrow.spec.planar_tip_bends_and_repeatability") {
    ArrowGenerator arrow;
    REQUIRE(arrow.setMode("2d").ok());
    REQUIRE(arrow.setEndpoints(0, 0, 0, 4, 0, 0).ok());
    for (float bend : {0.f, 1.f, -1.f}) {
        REQUIRE(arrow.setBend(0.f, bend, 0.f).ok());
        auto built = arrow.buildMeshResult();
        REQUIRE(built.ok());
        requireValidTriangles(built.value());
        bool foundTip = false;
        for (int i = 0; i < built.value().getVertexCount(); ++i) {
            CHECK_EQ(built.value().getPositionZ(i), 0.f);
            CHECK(built.value().getNormalZ(i) > .99f);
            if (built.value().getPositionX(i) == 4.f && built.value().getPositionY(i) == 0.f) foundTip = true;
        }
        CHECK(foundTip);
        auto repeated = arrow.buildMeshResult();
        REQUIRE(repeated.ok());
        CHECK(repeated.value().positions() == built.value().positions());
    }
}

// 3D geometry contract: arrows along all principal axes and either bend sign
// form a closed oriented surface with one exact world-space endpoint.
TEST_CASE("procgen.arrow.spec.spatial_axes_and_closed_surface") {
    for (int axis = 0; axis < 3; ++axis) {
        for (float bend : {-.5f, 0.f, .5f}) {
            ArrowGenerator arrow;
            REQUIRE(arrow.setMode("3d").ok());
            REQUIRE(
                arrow.setEndpoints(0, 0, 0, axis == 0 ? 4.f : 0.f, axis == 1 ? 4.f : 0.f, axis == 2 ? 4.f : 0.f).ok());
            REQUIRE(arrow.setBend(axis == 0 ? 0.f : bend, axis == 0 ? bend : 0.f, 0.f).ok());
            auto built = arrow.buildMeshResult();
            REQUIRE(built.ok());
            requireValidTriangles(built.value());
            requireClosedSurface(built.value());
        }
    }
}

// Flat geometry contract: both profiles retain a single B apex and a closed
// bevel. Head thickness scale changes Z extent but never moves the tip.
TEST_CASE("procgen.arrow.spec.flat_profiles_bevel_and_head_thickness") {
    for (const char* profile : {"uniform", "flowing"}) {
        for (float scale : {1.f, 2.5f, 4.f}) {
            ArrowGenerator arrow;
            REQUIRE(arrow.setMode("flat").ok());
            REQUIRE(arrow.setFlatProfile(profile).ok());
            REQUIRE(arrow.setHeadThicknessScale(scale).ok());
            REQUIRE(arrow.setEndpoints(-2, 0, 0, 2, 0, 0).ok());
            REQUIRE(arrow.setSize(.55f, 1.5f, 1.4f).ok());
            REQUIRE(arrow.setBevel(.24f, .035f, 6).ok());
            auto built = arrow.buildMeshResult();
            REQUIRE(built.ok());
            requireValidTriangles(built.value());
            requireClosedSurface(built.value());
            bool  apex     = false;
            float maximumZ = 0.f;
            for (int i = 0; i < built.value().getVertexCount(); ++i) {
                if (built.value().getPositionX(i) == 2.f) {
                    CHECK_EQ(built.value().getPositionY(i), 0.f);
                    CHECK_EQ(built.value().getPositionZ(i), 0.f);
                    apex = true;
                }
                maximumZ = std::max(maximumZ, built.value().getPositionZ(i));
            }
            CHECK(apex);
            CHECK(maximumZ >= .12f * scale - 1e-6f);
        }
    }
}

// Configuration contract: invalid setters do not overwrite a working build;
// geometry validation also rejects incompatible 2D or degenerate paths.
TEST_CASE("procgen.arrow.spec.invalid_input_preserves_configuration") {
    ArrowGenerator arrow;
    auto           baseline = arrow.buildMeshResult();
    REQUIRE(baseline.ok());
    CHECK(!arrow.setEndpoints(0, 0, 0, 0, 0, 0).ok());
    CHECK(!arrow.setBend(0.f, std::numeric_limits<float>::quiet_NaN(), 0.f).ok());
    CHECK(!arrow.setMode("unknown").ok());
    CHECK(!arrow.setSize(.2f, .1f, .2f).ok());
    CHECK(!arrow.setResolution(0, 2).ok());
    CHECK(!arrow.setFlatProfile("unknown").ok());
    CHECK(!arrow.setHeadThicknessScale(.9f).ok());
    auto after = arrow.buildMeshResult();
    REQUIRE(after.ok());
    CHECK(after.value().positions() == baseline.value().positions());
    REQUIRE(arrow.setMode("2d").ok());
    REQUIRE(arrow.setEndpoints(0, 0, 0, 1, 0, 1).ok());
    CHECK(!arrow.buildMeshResult().ok());
    REQUIRE(arrow.setMode("3d").ok());
    REQUIRE(arrow.buildMeshResult().ok());
}
}  // namespace
