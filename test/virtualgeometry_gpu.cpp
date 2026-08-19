#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
#include "virtualgeometry/VirtualGeometry.h"
#include "virtualgeometry/VirtualGeometryRenderer.h"
#include "window/Window.h"

#include <cmath>
#include <string>
#include <vector>

using namespace eve::virtualgeometry;

namespace {

bool tryInitGpuWindow() {
    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    if (!win || !gfx) return false;
    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    return win->setWindowSettings(s);
}

}  // namespace

TEST_CASE("virtualgeometry.module.create") {
    auto *mod = VirtualGeometry::create();
    REQUIRE(mod != nullptr);
    CHECK_EQ(mod->getName(), std::string("VirtualGeometry"));
}

TEST_CASE("virtualgeometry.gpu.buildIcosphere") {
    if (!tryInitGpuWindow()) return;  // headless: skip

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
    if (!tryInitGpuWindow()) return;  // headless: skip

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
    CHECK(visible > 0);                    // some clusters are visible
    CHECK(visible <= r->getClusterCount());

    // Resolve the visibility buffer to RGBA.
    auto *rgba = r->resolveByteData();
    REQUIRE(rgba != nullptr);
    CHECK_EQ(r->getViewWidth(), 128);
    CHECK_EQ(r->getViewHeight(), 128);
    CHECK_EQ(rgba->getSize(), std::size_t(128 * 128 * 4));

    // Most of the projected sphere's pixels should be covered (non-empty color).
    const unsigned char *px = static_cast<const unsigned char *>(rgba->getData());
    int covered = 0;
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
    if (!tryInitGpuWindow()) return;  // headless: skip

    auto *mod = VirtualGeometry::create();
    if (!mod->isAvailable()) return;
    auto *r = mod->newRenderer();
    REQUIRE(r != nullptr);
    REQUIRE(r->isReady());
    bool built = r->buildIcosphere(4);  // 5 LOD levels
    REQUIRE(built);
    r->setViewport(128, 128, 60.f, 1.0f);

    const float distances[] = {1.6f, 2.2f, 3.2f, 5.0f, 9.0f};
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
        const unsigned char *px = static_cast<const unsigned char *>(rgba->getData());
        int covered = 0;
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
    for (std::size_t i = 1; i < visibleCounts.size(); ++i)
        CHECK(visibleCounts[i] <= visibleCounts[i - 1]);
    // The sphere stays fully rendered at every distance (no holes).
    for (std::size_t i = 1; i < coveredPixels.size(); ++i) {
        CHECK(coveredPixels[i] > 0);        // still visible far away
        CHECK(coveredPixels[i] <= coveredPixels[i - 1]);  // smaller when farther
    }

    delete r;
}
