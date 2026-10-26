// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "virtualgeometry_builder.cpp"
  ]
}
*/
// clang-format on

#include "virtualgeometry/Builder.h"
#include "virtualgeometry/LodSelection.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

using namespace eve::virtualgeometry;

namespace {

// A closed curved surface gives QEM a nonzero simplification error. Subdivision
// deliberately keeps independent midpoint vertices; the builder must partition
// the resulting indexed triangles without requiring a pre-merged vertex buffer.
struct SphereInput {
    std::vector<float>         positions;
    std::vector<std::uint32_t> indices;

    std::uint32_t vertex(float x, float y, float z) {
        const float length = std::sqrt(x * x + y * y + z * z);
        positions.insert(positions.end(), {x / length, y / length, z / length});
        return static_cast<std::uint32_t>(positions.size() / 3 - 1);
    }

    void make(int subdivisions) {
        constexpr float                            t        = 1.61803398875f;
        const std::array<std::array<float, 3>, 12> vertices = {{{-1, t, 0},
                                                                {1, t, 0},
                                                                {-1, -t, 0},
                                                                {1, -t, 0},
                                                                {0, -1, t},
                                                                {0, 1, t},
                                                                {0, -1, -t},
                                                                {0, 1, -t},
                                                                {t, 0, -1},
                                                                {t, 0, 1},
                                                                {-t, 0, -1},
                                                                {-t, 0, 1}}};
        for (const auto& v : vertices) vertex(v[0], v[1], v[2]);
        constexpr int faces[20][3] = {{0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11},
                                      {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                                      {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},
                                      {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1}};
        for (const auto& f : faces)
            indices.insert(indices.end(), {static_cast<std::uint32_t>(f[0]), static_cast<std::uint32_t>(f[1]),
                                           static_cast<std::uint32_t>(f[2])});
        for (int level = 0; level < subdivisions; ++level) {
            std::vector<std::uint32_t> refined;
            refined.reserve(indices.size() * 4);
            for (std::size_t i = 0; i < indices.size(); i += 3) {
                const auto a = indices[i], b = indices[i + 1], c = indices[i + 2];
                const auto midpoint = [&](std::uint32_t u, std::uint32_t v) {
                    const auto p = [&](int axis) { return (positions[3 * u + axis] + positions[3 * v + axis]) * 0.5f; };
                    return vertex(p(0), p(1), p(2));
                };
                const auto ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
                refined.insert(refined.end(), {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca});
            }
            indices.swap(refined);
        }
    }

    VirtualGeometryBuilder::MeshInput input() const {
        VirtualGeometryBuilder::MeshInput in;
        in.vertexCount = static_cast<int>(positions.size() / 3);
        in.positions   = positions.data();
        in.indices     = indices.data();
        in.indexCount  = static_cast<int>(indices.size());
        return in;
    }
};

VirtualGeometryAsset buildSphere() {
    SphereInput source;
    source.make(3);  // 20 * 4^3 = 1280 source triangles.
    VirtualGeometryBuilder::Options options;
    options.minLodLevels = 5;
    VirtualGeometryAsset asset;
    REQUIRE(VirtualGeometryBuilder().build(source.input(), options, asset));
    return asset;
}

std::vector<std::uint32_t> select(const VirtualGeometryAsset& asset, float distance, float error = 1.f) {
    std::vector<std::uint32_t> selected;
    REQUIRE_EQ(selectClusters(asset, distance, 540.f, error, selected), static_cast<int>(selected.size()));
    return selected;
}

}  // namespace

TEST_CASE("VirtualGeometryBuilder validates input and publishes a bounded LOD hierarchy") {
    SUB_CASE("invalid mesh input returns false") {
        VirtualGeometryBuilder            builder;
        VirtualGeometryAsset              asset;
        VirtualGeometryBuilder::MeshInput input;
        CHECK(!builder.build(input, {}, asset));
        SphereInput source;
        source.make(0);
        input           = source.input();
        input.positions = nullptr;
        CHECK(!builder.build(input, {}, asset));
        input         = source.input();
        input.indices = nullptr;
        CHECK(!builder.build(input, {}, asset));
        input            = source.input();
        input.indexCount = 2;
        CHECK(!builder.build(input, {}, asset));
    };

    SUB_CASE("leaf clusters partition all source triangles within budget") {
        const auto asset = buildSphere();
        CHECK_EQ(asset.vertexCount, static_cast<int>(asset.positions.size() / 3));
        REQUIRE(!asset.clusters.empty());
        std::uint32_t leafTriangles = 0;
        for (const auto& cluster : asset.clusters) {
            CHECK(cluster.triStart * 3 + cluster.triCount * 3 <= asset.triangles.size());
            CHECK(cluster.r >= 0.f);
            if (cluster.lodLevel == 0) {
                leafTriangles += cluster.triCount;
                CHECK(cluster.triCount <= 124);
                CHECK(cluster.vertCount <= 64);
            }
        }
        CHECK_EQ(leafTriangles, 1280u);
        for (const auto index : asset.triangles) CHECK(index < static_cast<std::uint32_t>(asset.vertexCount));
    };

    SUB_CASE("parent links name their children and terminate at a root") {
        const auto asset = buildSphere();
        int        roots = 0;
        for (std::size_t i = 0; i < asset.clusters.size(); ++i) {
            const auto& child = asset.clusters[i];
            for (std::uint32_t j = 0; j < child.childCount; ++j) {
                REQUIRE(child.children[j] < asset.clusters.size());
                CHECK_EQ(asset.clusters[child.children[j]].parent, static_cast<std::uint32_t>(i));
            }
            if (child.parent == 0xFFFFFFFFu) {
                ++roots;
                continue;
            }
            REQUIRE(child.parent < asset.clusters.size());
            const auto& parent = asset.clusters[child.parent];
            CHECK(parent.lodLevel > child.lodLevel);
            CHECK(parent.childCount <= 4);
            bool listed = false;
            for (std::uint32_t j = 0; j < parent.childCount; ++j) listed |= parent.children[j] == i;
            CHECK(listed);
        }
        CHECK_EQ(roots, 1);
    };

    SUB_CASE("successive LODs reduce triangle count and accumulate error") {
        const auto    asset    = buildSphere();
        std::uint32_t maxLevel = 0;
        for (const auto& cluster : asset.clusters) maxLevel = std::max(maxLevel, cluster.lodLevel);
        CHECK(maxLevel >= 1);
        std::uint32_t previousTriangles = 1280;
        for (std::uint32_t level = 1; level <= maxLevel; ++level) {
            std::uint32_t triangles = 0;
            float         error     = 0.f;
            for (const auto& cluster : asset.clusters) {
                if (cluster.lodLevel != level) continue;
                triangles += cluster.triCount;
                error = std::max(error, cluster.errorR);
            }
            CHECK(triangles < previousTriangles);
            CHECK(error > 0.f);
            previousTriangles = triangles;
        }
    };
}

TEST_CASE("VirtualGeometry CPU LOD selection covers hierarchy under distance and error changes") {
    SUB_CASE("selection covers every branch exactly once") {
        const auto asset = buildSphere();
        for (float distance : {1.f, 3.f, 10.f, 30.f, 100.f, 200.f}) {
            const auto selected = select(asset, distance);
            REQUIRE(!selected.empty());
            std::vector<bool> chosen(asset.clusters.size(), false);
            for (const auto index : selected) {
                REQUIRE(index < chosen.size());
                CHECK(!chosen[index]);
                chosen[index] = true;
            }
            for (std::size_t i = 0; i < asset.clusters.size(); ++i) {
                // A selected descendant covers an internal cluster. Coverage
                // and exclusivity are measured on each source leaf's path.
                if (asset.clusters[i].childCount != 0) continue;
                int  representatives = chosen[i] ? 1 : 0;
                auto parent          = asset.clusters[i].parent;
                while (parent != 0xFFFFFFFFu) {
                    REQUIRE(parent < chosen.size());
                    representatives += chosen[parent] ? 1 : 0;
                    parent = asset.clusters[parent].parent;
                }
                CHECK_EQ(representatives, 1);
            }
        }
    };

    SUB_CASE("distance and error budget select expected relative detail") {
        const auto asset = buildSphere();
        const auto near  = select(asset, 1.f);
        const auto far   = select(asset, 200.f);
        CHECK(near.size() >= far.size());
        const auto strict = select(asset, 5.f, 0.5f);
        const auto loose  = select(asset, 5.f, 2.f);
        CHECK(strict.size() >= loose.size());

        std::vector<int> histogram;
        lodHistogram(asset, near, histogram);
        REQUIRE(!histogram.empty());
        CHECK_EQ(histogram[0], static_cast<int>(near.size()));
        lodHistogram(asset, far, histogram);
        CHECK_EQ(std::accumulate(histogram.begin(), histogram.end(), 0), static_cast<int>(far.size()));
    };
}
