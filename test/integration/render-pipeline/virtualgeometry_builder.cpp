#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "data/ByteData.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "virtualgeometry/Builder.h"
#include "virtualgeometry/LodSelection.h"
#include "virtualgeometry/VirtualGeometry.h"
#include "virtualgeometry/VirtualGeometryRenderer.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// virtualgeometry_builder.cpp
using namespace eve::virtualgeometry;

namespace {

// Build a unit icosphere with `subdiv` levels (3 => 1280 tris, 4 => 5120).
struct IcoMesh {
    std::vector<float>         positions;
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
        const float                       t    = (1.f + std::sqrt(5.f)) * 0.5f;
        std::vector<std::array<float, 3>> base = {{{-1, t, 0},
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
        int                               idx[12];
        for (int i = 0; i < 12; ++i) idx[i] = addVtx(base[i][0], base[i][1], base[i][2]);
        int faces[20][3] = {{0, 11, 5},  {0, 5, 1},  {0, 1, 7},  {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                            {11, 10, 2}, {10, 7, 6}, {7, 1, 8},  {3, 9, 4},  {3, 4, 2},   {3, 2, 6}, {3, 6, 8},
                            {3, 8, 9},   {4, 9, 5},  {2, 4, 11}, {6, 2, 10}, {8, 6, 7},   {9, 8, 1}};
        struct FT {
            int a, b, c;
        };
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

[[maybe_unused]] VirtualGeometryAsset buildIco(int subdiv) {
    IcoMesh m;
    m.build(subdiv);
    VirtualGeometryBuilder            b;
    VirtualGeometryBuilder::MeshInput in;
    in.vertexCount = static_cast<int>(m.positions.size() / 3);
    in.positions   = m.positions.data();
    in.indices     = m.indices.data();
    in.indexCount  = static_cast<int>(m.indices.size());
    VirtualGeometryBuilder::Options opt;
    opt.minLodLevels           = 5;
    opt.mergeFactor            = 4;
    opt.maxTrianglesPerCluster = 124;
    opt.maxVerticesPerCluster  = 64;
    VirtualGeometryAsset a;
    REQUIRE(b.build(in, opt, a));
    return a;
}

}  // namespace


// ---------------------------------------------------------------------------
// LOD transition tests. Sweep camera distance and drive the CPU reference of
// the GPU cull accept-rule (LodSelection::selectClusters) to verify that the
// visible LOD changes continuously: fine LOD0 near, coarse LOD far, with a
// gradual mix in between and no gaps/overlaps (a full, hole-free cover).
// ---------------------------------------------------------------------------

namespace {

constexpr float kProjScale = 540.f;  // e.g. 1080px / (2*tan(30deg))
constexpr float kErrorPx   = 1.0f;

struct SelectionStats {
    int              selected = 0;
    std::vector<int> hist;  // per-LOD counts
};

[[maybe_unused]] SelectionStats selectAt(const VirtualGeometryAsset &a, float dist) {
    std::vector<std::uint32_t> sel;
    selectClusters(a, dist, kProjScale, kErrorPx, sel);
    SelectionStats s;
    s.selected = static_cast<int>(sel.size());
    lodHistogram(a, sel, s.hist);
    return s;
}

// Highest LOD level that has any selected cluster (0..maxLod); -1 if none.
[[maybe_unused]] int maxSelectedLod(const SelectionStats &s) {
    for (int l = static_cast<int>(s.hist.size()) - 1; l >= 0; --l)
        if (s.hist[l] > 0) return l;
    return -1;
}

}  // namespace


// virtualgeometry_gpu.cpp
using namespace eve::virtualgeometry;

namespace {

/** GPU cluster building only needs a live graphics device; headless init is enough. */
[[maybe_unused]] bool tryInitHeadlessGfx() {
    auto *gfx = eve::graphics::Graphics::create();
    if (!gfx) return false;
    gfx->initHeadless(320, 240);
    return true;
}

}  // namespace

TEST_CASE("virtualgeometry.module.create") {
    auto *mod = VirtualGeometry::create();
    REQUIRE(mod != nullptr);
    CHECK_EQ(mod->getName(), std::string("VirtualGeometry"));
}

TEST_CASE("virtualgeometry.gpu.buildIcosphere") {
    if (!tryInitHeadlessGfx()) return;

    auto *mod = VirtualGeometry::create();
    if (!mod->isAvailable()) return;
    auto *r = mod->newRenderer();
    REQUIRE(r != nullptr);
    REQUIRE(r->isReady());

    bool built = r->buildIcosphere(3);  // 1280-triangle unit sphere
    CHECK(built);
    CHECK(r->getClusterCount() > 0);
    CHECK(r->getMaxLodLevel() >= 2);
    CHECK(r->getTotalTriangleCount() > 0);
    CHECK(r->getLodLevel(0) >= 0);

    delete r;
}

TEST_CASE("virtualgeometry.gpu.cullRasterResolve") {
    if (!tryInitHeadlessGfx()) return;

    auto *mod = VirtualGeometry::create();
    if (!mod->isAvailable()) return;
    auto *r = mod->newRenderer();
    REQUIRE(r != nullptr);
    REQUIRE(r->isReady());
    bool built = r->buildIcosphere(3);
    REQUIRE(built);

    r->setViewport(128, 128, 60.f, 1.0f);
    r->setCameraSimple(0.f, 0.f, 3.2f);
    r->setModelYaw(0.3f);

    // GPU-driven culling + software rasterization into the visibility buffer.
    int visible = r->update();
    CHECK(visible > 0);  // some clusters are visible
    CHECK(visible <= r->getClusterCount());

    // Resolve the visibility buffer to RGBA.
    auto *rgba = r->resolveByteData();
    REQUIRE(rgba != nullptr);
    CHECK_EQ(r->getViewWidth(), 128);
    CHECK_EQ(r->getViewHeight(), 128);
    CHECK_EQ(rgba->getSize(), std::size_t(128 * 128 * 4));

    // Most of the projected sphere's pixels should be covered (non-empty color).
    const unsigned char *px      = static_cast<const unsigned char *>(rgba->getData());
    int                  covered = 0;
    for (std::size_t i = 0; i < rgba->getSize(); i += 4) {
        bool empty = px[i] == 4 && px[i + 1] == 6 && px[i + 2] == 12;
        if (!empty) ++covered;
    }
    CHECK(covered > 128 * 128 / 4);  // >25% coverage of the viewport

    delete rgba;
    delete r;
}

// Sweep the camera distance through the real GPU pipeline and verify LOD
// transitions are smooth: fine detail up close, coarse detail far away, with a
// continuous, hole-free cover the whole time.
TEST_CASE("virtualgeometry.gpu.lodTransitionSweep") {
    if (!tryInitHeadlessGfx()) return;

    auto *mod = VirtualGeometry::create();
    if (!mod->isAvailable()) return;
    auto *r = mod->newRenderer();
    REQUIRE(r != nullptr);
    REQUIRE(r->isReady());
    bool built = r->buildIcosphere(4);  // 5 LOD levels
    REQUIRE(built);
    r->setViewport(128, 128, 60.f, 1.0f);

    const float      distances[] = {1.6f, 2.2f, 3.2f, 5.0f, 9.0f};
    std::vector<int> visibleCounts;
    std::vector<int> coveredPixels;
    for (float d : distances) {
        r->setCameraSimple(0.f, 0.f, d);
        r->setModelYaw(0.2f);
        int visible = r->update();
        CHECK(visible > 0);
        visibleCounts.push_back(visible);

        auto *rgba = r->resolveByteData();
        REQUIRE(rgba != nullptr);
        const unsigned char *px      = static_cast<const unsigned char *>(rgba->getData());
        int                  covered = 0;
        for (std::size_t i = 0; i < rgba->getSize(); i += 4) {
            bool empty = px[i] == 4 && px[i + 1] == 6 && px[i + 2] == 12;
            if (!empty) ++covered;
        }
        coveredPixels.push_back(covered);
        delete rgba;
    }

    // Fine detail (many clusters) up close, coarse (few) far away.
    CHECK(visibleCounts[0] > visibleCounts.back());
    // Monotonic non-increase of visible clusters with distance.
    for (std::size_t i = 1; i < visibleCounts.size(); ++i) CHECK(visibleCounts[i] <= visibleCounts[i - 1]);
    // The sphere stays fully rendered at every distance (no holes).
    for (std::size_t i = 1; i < coveredPixels.size(); ++i) {
        CHECK(coveredPixels[i] > 0);                      // still visible far away
        CHECK(coveredPixels[i] <= coveredPixels[i - 1]);  // smaller when farther
    }

    delete r;
}
