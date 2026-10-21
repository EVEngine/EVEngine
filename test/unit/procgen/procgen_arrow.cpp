#include "procgen/mesh/ArrowGenerator.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <map>
#include <tuple>

using eve::procgen::ArrowGenerator;
using eve::procgen::MeshBuild;

namespace {
void checkMesh(const MeshBuild& mesh) {
    REQUIRE(mesh.getIndexCount() > 0);
    for (float v : mesh.positions()) REQUIRE(std::isfinite(v));
    for (float v : mesh.normals()) REQUIRE(std::isfinite(v));
    for (float v : mesh.uvs()) REQUIRE(std::isfinite(v));
    for (int i = 0; i < mesh.getIndexCount(); i += 3) {
        const auto a = mesh.getIndex(i), b = mesh.getIndex(i + 1), c = mesh.getIndex(i + 2);
        REQUIRE((a >= 0 && a < mesh.getVertexCount()));
        REQUIRE((b >= 0 && b < mesh.getVertexCount()));
        REQUIRE((c >= 0 && c < mesh.getVertexCount()));
        const double ux = mesh.getPositionX(b) - mesh.getPositionX(a), uy = mesh.getPositionY(b) - mesh.getPositionY(a),
                     uz = mesh.getPositionZ(b) - mesh.getPositionZ(a);
        const double vx = mesh.getPositionX(c) - mesh.getPositionX(a), vy = mesh.getPositionY(c) - mesh.getPositionY(a),
                     vz = mesh.getPositionZ(c) - mesh.getPositionZ(a);
        const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        // Match the production float-face validity contract; fine contour cuts legitimately create tiny faces.
        REQUIRE(std::sqrt(nx * nx + ny * ny + nz * nz) >= 1e-16);
        REQUIRE(nx * mesh.getNormalX(a) + ny * mesh.getNormalY(a) + nz * mesh.getNormalZ(a) > 0.);
    }
}
}  // namespace

TEST_CASE("procgen.arrow.planarStraightAndCurvedHaveExactTipAndRepeatableMesh") {
    ArrowGenerator arrow;
    REQUIRE(arrow.setEndpoints(0, 0, 0, 4, 0, 0).ok());
    for (float bend : {0.f, 1.f, -1.f}) {
        REQUIRE(arrow.setBend(0, bend, 0).ok());
        auto result = arrow.buildMeshResult();
        REQUIRE(result.ok());
        const auto& mesh = result.value();
        checkMesh(mesh);
        bool tip = false;
        for (int i = 0; i < mesh.getVertexCount(); ++i) {
            REQUIRE_EQ(mesh.getPositionZ(i), 0.f);
            REQUIRE(mesh.getNormalZ(i) > 0.99f);
            tip = tip || (mesh.getPositionX(i) == 4.f && mesh.getPositionY(i) == 0.f);
        }
        REQUIRE(tip);
        auto repeat = arrow.buildMeshResult();
        REQUIRE(repeat.ok());
        REQUIRE(mesh.positions() == repeat.value().positions());
    }
}

TEST_CASE("procgen.arrow.spatialCurvesHaveClosedOrientedSurface") {
    for (int sample = 0; sample < 9; ++sample) {
        const int      axis = sample % 3;
        const float    bend = static_cast<float>(sample / 3 - 1) * 0.5f;
        ArrowGenerator arrow;
        REQUIRE(arrow.setMode("3d").ok());
        REQUIRE(arrow.setEndpoints(0, 0, 0, axis == 0 ? 4.f : 0.f, axis == 1 ? 4.f : 0.f, axis == 2 ? 4.f : 0.f).ok());
        REQUIRE(arrow.setBend(axis == 0 ? 0.f : bend, axis == 0 ? bend : 0.f, 0.f).ok());
        auto result = arrow.buildMeshResult();
        REQUIRE(result.ok());
        const auto& mesh = result.value();
        checkMesh(mesh);
        using Point = std::tuple<float, float, float>;
        using Edge  = std::pair<Point, Point>;
        std::map<Edge, int> counts, directions;
        const auto          point = [&](int index) {
            return Point{mesh.getPositionX(index), mesh.getPositionY(index), mesh.getPositionZ(index)};
        };
        for (int i = 0; i < mesh.getIndexCount(); i += 3)
            for (int j = 0; j < 3; ++j) {
                Point      a = point(mesh.getIndex(i + j)), b = point(mesh.getIndex(i + (j + 1) % 3));
                const bool forward = a < b;
                if (!forward) std::swap(a, b);
                ++counts[{a, b}];
                directions[{a, b}] += forward ? 1 : -1;
            }
        for (const auto& entry : counts) REQUIRE_EQ(entry.second, 2);
        for (const auto& entry : directions) REQUIRE_EQ(entry.second, 0);
    }
}

TEST_CASE("procgen.arrow.invalidParametersPreservePriorConfiguration") {
    ArrowGenerator defaultFlowing;
    REQUIRE(defaultFlowing.setMode("flat").ok());
    REQUIRE(defaultFlowing.setFlatProfile("flowing").ok());
    auto defaultFlowingMesh = defaultFlowing.buildMeshResult();
    REQUIRE(defaultFlowingMesh.ok());
    checkMesh(defaultFlowingMesh.value());

    ArrowGenerator arrow;
    auto           initial = arrow.buildMeshResult();
    REQUIRE(initial.ok());
    REQUIRE(!arrow.setEndpoints(0, 0, 0, 0, 0, 0).ok());
    REQUIRE(!arrow.setBend(0, std::numeric_limits<float>::quiet_NaN(), 0).ok());
    REQUIRE(!arrow.setSize(0.2f, 0.1f, 0.2f).ok());
    REQUIRE(!arrow.setMode("invalid").ok());
    REQUIRE(!arrow.setResolution(0, 2).ok());
    auto preserved = arrow.buildMeshResult();
    REQUIRE(preserved.ok());
    REQUIRE(initial.value().positions() == preserved.value().positions());
    REQUIRE(arrow.setMode("3d").ok());
    REQUIRE(arrow.setEndpoints(1000000, 1000000, 1000000, 999996, 1000000, 1000000).ok());
    REQUIRE(!arrow.buildMeshResult().ok());  // Float precision collapses this thin shaft.
    REQUIRE(arrow.setMode("2d").ok());
    REQUIRE(arrow.setEndpoints(0, 0, 0, 0.01f, 0, 0).ok());
    REQUIRE(!arrow.buildMeshResult().ok());
    REQUIRE(arrow.setEndpoints(0, 0, 0, 1, 0, 1).ok());
    REQUIRE(!arrow.buildMeshResult().ok());
    REQUIRE(arrow.setMode("3d").ok());
    REQUIRE(arrow.buildMeshResult().ok());
    REQUIRE(arrow.setBend(1, 0, 1).ok());
    REQUIRE(!arrow.buildMeshResult().ok());
}

TEST_CASE("procgen.arrow.flatBeveledMeshIsClosedAndHasConfiguredThickness") {
    for (bool flowing : {false, true}) {
        for (float scale : {1.f, 2.5f, 4.f}) {
            for (float bend : {0.f, 2.f, -2.f}) {
                ArrowGenerator arrow;
                REQUIRE(arrow.setMode("flat").ok());
                REQUIRE(arrow.setFlatProfile(flowing ? "flowing" : "uniform").ok());
                REQUIRE(arrow.setHeadThicknessScale(scale).ok());
                REQUIRE(arrow.setEndpoints(-2, 0, 0, 2, 0, 0).ok());
                REQUIRE(arrow.setSize(flowing ? 0.55f : 0.26f, flowing ? 1.5f : 0.7f, flowing ? 1.4f : 0.8f).ok());
                REQUIRE(arrow.setBevel(flowing ? 0.24f : 0.12f, flowing ? 0.035f : 0.02f, 6).ok());
                REQUIRE(arrow.setBend(0, bend, 0).ok());
                auto result = arrow.buildMeshResult();
                REQUIRE(result.ok());
                const auto& mesh = result.value();
                checkMesh(mesh);
                float minZ = 1.f, maxZ = -1.f;
                using Point = std::tuple<float, float, float>;
                using Edge  = std::pair<Point, Point>;
                std::map<Edge, int> counts, directions;
                const auto          point = [&](int index) {
                    return Point{mesh.getPositionX(index), mesh.getPositionY(index), mesh.getPositionZ(index)};
                };
                bool   apex   = false;
                double volume = 0.;
                for (int i = 0; i < mesh.getVertexCount(); ++i) {
                    if (mesh.getPositionX(i) == 2.f) {
                        REQUIRE_EQ(mesh.getPositionY(i), 0.f);
                        REQUIRE_EQ(mesh.getPositionZ(i), 0.f);
                        apex = true;
                    }
                    REQUIRE(mesh.getPositionX(i) <= 2.f + 1e-6f);
                    if (mesh.getPositionX(i) < -1.5f)
                        REQUIRE(std::abs(mesh.getPositionZ(i)) <= (flowing ? 0.120001f : 0.060001f));
                    minZ = std::min(minZ, mesh.getPositionZ(i));
                    maxZ = std::max(maxZ, mesh.getPositionZ(i));
                }
                REQUIRE(apex);
                if (flowing) {
                    REQUIRE(maxZ >= 0.12f * scale - 1e-6f);
                    REQUIRE(maxZ <= 0.12f * scale * 1.35f + 1e-6f);
                    REQUIRE(std::abs(maxZ + minZ) < 1e-6f);
                } else {
                    REQUIRE(std::abs(minZ + 0.06f * scale) < 1e-6f);
                    REQUIRE(std::abs(maxZ - 0.06f * scale) < 1e-6f);
                }
                for (int i = 0; i < mesh.getIndexCount(); i += 3) {
                    const auto [ax, ay, az] = point(mesh.getIndex(i));
                    const auto [bx, by, bz] = point(mesh.getIndex(i + 1));
                    const auto [cx, cy, cz] = point(mesh.getIndex(i + 2));
                    volume += ax * (by * cz - bz * cy) + ay * (bz * cx - bx * cz) + az * (bx * cy - by * cx);
                    for (int j = 0; j < 3; ++j) {
                        Point      a = point(mesh.getIndex(i + j)), b = point(mesh.getIndex(i + (j + 1) % 3));
                        const bool forward = a < b;
                        if (!forward) std::swap(a, b);
                        ++counts[{a, b}];
                        directions[{a, b}] += forward ? 1 : -1;
                    }
                }
                REQUIRE(volume > 0.);
                for (const auto& entry : counts) REQUIRE_EQ(entry.second, 2);
                for (const auto& entry : directions) REQUIRE_EQ(entry.second, 0);
                REQUIRE(!arrow.setBevel(0.01f, 0.02f, 6).ok());
                REQUIRE(!arrow.setBevel(0.12f, 0.02f, 0).ok());
                REQUIRE(!arrow.setHeadThicknessScale(0.9f).ok());
                REQUIRE(!arrow.setHeadThicknessScale(4.1f).ok());
                REQUIRE(!arrow.setHeadThicknessScale(std::numeric_limits<float>::quiet_NaN()).ok());
                REQUIRE(!arrow.setFlatProfile("unknown").ok());
                auto preserved = arrow.buildMeshResult();
                REQUIRE(preserved.ok());
                REQUIRE(mesh.positions() == preserved.value().positions());
                REQUIRE(arrow.setBend(0, 0, 1).ok());
                REQUIRE(!arrow.buildMeshResult().ok());
            }
        }
    }
}

TEST_CASE("procgen.arrow.flatTipHasOneWorldSpaceApexForOddAndEvenBevels") {
    for (bool flowing : {false, true}) {
        for (int segments : {3, 6}) {
            ArrowGenerator arrow;
            REQUIRE(arrow.setMode("flat").ok());
            REQUIRE(arrow.setFlatProfile(flowing ? "flowing" : "uniform").ok());
            REQUIRE(arrow.setEndpoints(0, 0, 2, 3, 4, 2).ok());
            REQUIRE(arrow.setBend(-0.4f, 0.3f, 0).ok());
            REQUIRE(arrow.setSize(0.2f, 0.6f, 0.8f).ok());
            REQUIRE(arrow.setBevel(0.12f, 0.02f, segments).ok());
            auto result = arrow.buildMeshResult();
            REQUIRE(result.ok());
            const auto& mesh = result.value();
            checkMesh(mesh);
            bool apex   = false;
            using Point = std::tuple<float, float, float>;
            using Edge  = std::pair<Point, Point>;
            std::map<Edge, int> counts, directions;
            const auto          point = [&](int i) {
                return Point{mesh.getPositionX(i), mesh.getPositionY(i), mesh.getPositionZ(i)};
            };
            for (int i = 0; i < mesh.getVertexCount(); ++i) {
                const auto [x, y, z]    = point(i);
                const double projection = 0.6 * x + 0.8 * y;
                REQUIRE(projection <= 5. + 1e-6);
                if (projection >= 5. - 1e-6) {
                    REQUIRE_EQ(x, 3.f);
                    REQUIRE_EQ(y, 4.f);
                    REQUIRE_EQ(z, 2.f);
                    apex = true;
                }
            }
            REQUIRE(apex);
            for (int i = 0; i < mesh.getIndexCount(); i += 3)
                for (int j = 0; j < 3; ++j) {
                    Point      a = point(mesh.getIndex(i + j)), b = point(mesh.getIndex(i + (j + 1) % 3));
                    const bool forward = a < b;
                    if (!forward) std::swap(a, b);
                    ++counts[{a, b}];
                    directions[{a, b}] += forward ? 1 : -1;
                }
            for (const auto& entry : counts) REQUIRE_EQ(entry.second, 2);
            for (const auto& entry : directions) REQUIRE_EQ(entry.second, 0);
        }
    }
}
