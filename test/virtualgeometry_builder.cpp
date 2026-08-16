#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "virtualgeometry/Builder.h"
#include "virtualgeometry/LodSelection.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace eve::virtualgeometry;

namespace {

// Build a unit icosphere with `subdiv` levels (3 => 1280 tris, 4 => 5120).
struct IcoMesh {
    std::vector<float> positions;
    std::vector<std::uint32_t> indices;

    int addVtx(float x, float y, float z) {
        float l = std::sqrt(x * x + y * y + z * z);
        positions.push_back(x / l);
        positions.push_back(y / l);
        positions.push_back(z / l);
        return static_cast<int>(positions.size() / 3) - 1;
    }
    void tri(int a, int b, int c) {
        indices.push_back(static_cast<std::uint32_t>(a));
        indices.push_back(static_cast<std::uint32_t>(b));
        indices.push_back(static_cast<std::uint32_t>(c));
    }
    int mid(int i0, int i1) {
        float x = (positions[3 * i0] + positions[3 * i1]) * 0.5f;
        float y = (positions[3 * i0 + 1] + positions[3 * i1 + 1]) * 0.5f;
        float z = (positions[3 * i0 + 2] + positions[3 * i1 + 2]) * 0.5f;
        return addVtx(x, y, z);
    }
    void build(int subdiv) {
        const float t = (1.f + std::sqrt(5.f)) * 0.5f;
        std::vector<std::array<float, 3>> base = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
                                                  {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
                                                  {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
        int idx[12];
        for (int i = 0; i < 12; ++i) idx[i] = addVtx(base[i][0], base[i][1], base[i][2]);
        int faces[20][3] = {{0, 11, 5}, {0, 5, 1},   {0, 1, 7},  {0, 7, 10}, {0, 10, 11},
                            {1, 5, 9},  {5, 11, 4},  {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                            {3, 9, 4},  {3, 4, 2},   {3, 2, 6},  {3, 6, 8},  {3, 8, 9},
                            {4, 9, 5},  {2, 4, 11},  {6, 2, 10}, {8, 6, 7},  {9, 8, 1}};
        struct FT { int a, b, c; };
        std::vector<FT> cur;
        for (auto &f : faces) cur.push_back({idx[f[0]], idx[f[1]], idx[f[2]]});
        for (int d = 0; d < subdiv; ++d) {
            std::vector<FT> nf;
            for (auto &x : cur) {
                int ab = mid(x.a, x.b), ac = mid(x.a, x.c), bc = mid(x.b, x.c);
                nf.push_back({x.a, ab, ac});
                nf.push_back({ab, x.b, bc});
                nf.push_back({ac, bc, x.c});
                nf.push_back({ab, bc, ac});
            }
            cur.swap(nf);
        }
        indices.clear();
        for (auto &x : cur) tri(x.a, x.b, x.c);
    }
};

VirtualGeometryAsset buildIco(int subdiv) {
    IcoMesh m;
    m.build(subdiv);
    VirtualGeometryBuilder b;
    VirtualGeometryBuilder::MeshInput in;
    in.vertexCount = static_cast<int>(m.positions.size() / 3);
    in.positions = m.positions.data();
    in.indices = m.indices.data();
    in.indexCount = static_cast<int>(m.indices.size());
    VirtualGeometryBuilder::Options opt;
    opt.minLodLevels = 5;
    opt.mergeFactor = 4;
    opt.maxTrianglesPerCluster = 124;
    opt.maxVerticesPerCluster = 64;
    VirtualGeometryAsset a;
    REQUIRE(b.build(in, opt, a));
    return a;
}

}  // namespace

TEST_CASE("virtualgeometry.builder.rejectsEmptyInput") {
    VirtualGeometryBuilder b;
    VirtualGeometryBuilder::MeshInput in;  // all null / zero
    VirtualGeometryAsset a;
    CHECK(!b.build(in, VirtualGeometryBuilder::Options(), a));
}

TEST_CASE("virtualgeometry.builder.icosphereCoversAllTriangles") {
    auto a = buildIco(4);  // 5120 triangles
    long l0 = 0;
    for (const auto &c : a.clusters)
        if (c.lodLevel == 0) l0 += static_cast<long>(c.triCount);
    // Every LOD0 triangle is covered exactly once.
    CHECK_EQ(l0, 5120);
    // All triangle indices are in range.
    bool inRange = true;
    for (std::uint32_t t : a.triangles)
        if (t >= static_cast<std::uint32_t>(a.vertexCount)) inRange = false;
    CHECK(inRange);
}

TEST_CASE("virtualgeometry.builder.clusterSizeBounds") {
    auto a = buildIco(4);
    for (const auto &c : a.clusters)
        if (c.lodLevel == 0) CHECK(c.triCount <= 124);
}

TEST_CASE("virtualgeometry.builder.dagParentChildConsistent") {
    auto a = buildIco(4);
    int roots = 0;
    for (const auto &c : a.clusters) {
        if (c.parent == 0xFFFFFFFFu) {
            ++roots;
        } else {
            const VgCluster &p = a.clusters[c.parent];
            bool isChild = false;
            for (std::uint32_t k = 0; k < p.childCount; ++k)
                if (p.children[k] != 0xFFFFFFFFu && a.clusters.size() > p.children[k] &&
                    &a.clusters[p.children[k]] == &c)
                    isChild = true;
            CHECK(isChild);
        }
    }
    CHECK(roots >= 1);
}

TEST_CASE("virtualgeometry.builder.lodMonotonicReduction") {
    auto a = buildIco(4);
    int maxLod = 0;
    for (const auto &c : a.clusters) maxLod = std::max(maxLod, static_cast<int>(c.lodLevel));
    long prev = 1L << 30;
    for (int l = 0; l <= maxLod; ++l) {
        long tris = 0;
        float err = 0.f;
        for (const auto &c : a.clusters)
            if (static_cast<int>(c.lodLevel) == l) {
                tris += static_cast<long>(c.triCount);
                err = std::max(err, c.errorR);
            }
        if (l > 0) {
            CHECK(tris < prev);          // each level is coarser
            CHECK(err > 0.f);            // and carries positive geometric error
        }
        prev = tris;
    }
}

// ---------------------------------------------------------------------------
// LOD transition tests. Sweep camera distance and drive the CPU reference of
// the GPU cull accept-rule (LodSelection::selectClusters) to verify that the
// visible LOD changes continuously: fine LOD0 near, coarse LOD far, with a
// gradual mix in between and no gaps/overlaps (a full, hole-free cover).
// ---------------------------------------------------------------------------

namespace {

constexpr float kProjScale = 540.f;   // e.g. 1080px / (2*tan(30deg))
constexpr float kErrorPx = 1.0f;

struct SelectionStats {
    int selected = 0;
    std::vector<int> hist;  // per-LOD counts
};

SelectionStats selectAt(const VirtualGeometryAsset &a, float dist) {
    std::vector<std::uint32_t> sel;
    selectClusters(a, dist, kProjScale, kErrorPx, sel);
    SelectionStats s;
    s.selected = static_cast<int>(sel.size());
    lodHistogram(a, sel, s.hist);
    return s;
}

// Highest LOD level that has any selected cluster (0..maxLod); -1 if none.
int maxSelectedLod(const SelectionStats &s) {
    for (int l = static_cast<int>(s.hist.size()) - 1; l >= 0; --l)
        if (s.hist[l] > 0) return l;
    return -1;
}

}  // namespace

TEST_CASE("virtualgeometry.lod.selectionFineNearCoarseFar") {
    auto a = buildIco(4);  // 5 LOD levels
    // Very near: needs full detail -> mostly LOD0.
    auto near_ = selectAt(a, 0.9f);
    CHECK(maxSelectedLod(near_) == 0);           // only finest LOD selected
    // Very far: only coarse detail -> only top LOD.
    auto far = selectAt(a, 200.f);
    CHECK(maxSelectedLod(far) > 0);              // some coarse level
    // The sphere is on-screen at all these distances (frustum cull disabled).
    CHECK(near_.selected >= far.selected);       // near sees at least as many
}

TEST_CASE("virtualgeometry.lod.monotonicCoarsening") {
    auto a = buildIco(4);
    std::vector<int> countPerDist;
    std::vector<int> maxLodPerDist;
    for (int d = 1; d <= 200; d += 2) {
        auto s = selectAt(a, static_cast<float>(d));
        countPerDist.push_back(s.selected);
        maxLodPerDist.push_back(maxSelectedLod(s));
    }
    // Selected count never increases with distance (LOD only gets coarser).
    for (std::size_t i = 1; i < countPerDist.size(); ++i)
        CHECK(countPerDist[i] <= countPerDist[i - 1]);
    // Max LOD never decreases with distance.
    for (std::size_t i = 1; i < maxLodPerDist.size(); ++i)
        CHECK(maxLodPerDist[i] >= maxLodPerDist[i - 1]);
}

TEST_CASE("virtualgeometry.lod.gradualTransitionNoAbruptJump") {
    auto a = buildIco(4);
    // A fine sweep over the transition range: the selected-LOD histogram should
    // evolve gradually, not flip wholesale. Track the LOD-0 share per step.
    int prevLod0 = -1;
    for (int d = 1; d <= 120; ++d) {
        auto s = selectAt(a, static_cast<float>(d));
        int lod0 = s.hist.empty() ? 0 : s.hist[0];
        if (prevLod0 >= 0) {
            // LOD0 count decreases monotonically as we move away.
            CHECK(lod0 <= prevLod0);
        }
        prevLod0 = lod0;
    }
    // Some intermediate distance must contain BOTH LOD0 and a coarser LOD
    // (a genuine in-transition mixture, not an on/off switch).
    bool foundMix = false;
    for (int d = 1; d <= 120; ++d) {
        auto s = selectAt(a, static_cast<float>(d));
        int nonLod0 = 0;
        for (std::size_t l = 1; l < s.hist.size(); ++l) nonLod0 += s.hist[l];
        if (s.hist.size() >= 2 && s.hist[0] > 0 && nonLod0 > 0) foundMix = true;
    }
    CHECK(foundMix);
}

TEST_CASE("virtualgeometry.lod.coverHasNoGapsOrOverlaps") {
    auto a = buildIco(4);
    // At every distance, the selected clusters must exactly cover the mesh:
    // every cluster is either selected or has a selected ancestor (no gaps),
    // and no cluster has both itself and an ancestor selected (no overlap).
    for (int d = 1; d <= 200; d += 3) {
        std::vector<std::uint32_t> sel;
        selectClusters(a, static_cast<float>(d), kProjScale, kErrorPx, sel);
        std::vector<char> isSelected(a.clusters.size(), 0);
        for (std::uint32_t c : sel) isSelected[c] = 1;

        for (std::size_t c = 0; c < a.clusters.size(); ++c) {
            bool covered = isSelected[c];
            // Walk up the DAG: covered if a selected ancestor exists.
            std::uint32_t p = a.clusters[c].parent;
            while (p != 0xFFFFFFFFu && !covered) {
                covered = isSelected[p];
                p = a.clusters[p].parent;
            }
            CHECK(covered);  // every branch has exactly one selected node

            // Overlap check: a selected cluster must not have a selected parent.
            if (isSelected[c] && a.clusters[c].parent != 0xFFFFFFFFu)
                CHECK(!isSelected[a.clusters[c].parent]);
        }
    }
}

TEST_CASE("virtualgeometry.lod.errorThresholdSelectsMoreDetailCloser") {
    auto a = buildIco(4);
    // Lower errorPx demands finer detail -> more clusters selected at the same
    // distance.
    std::vector<std::uint32_t> strictSel, looseSel;
    selectClusters(a, 5.f, kProjScale, 0.5f, strictSel);
    selectClusters(a, 5.f, kProjScale, 2.0f, looseSel);
    CHECK(strictSel.size() >= looseSel.size());
}
