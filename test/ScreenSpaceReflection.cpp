#include "zeroerr/unittest.h"
#include "Fixtures.h"

#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "window/Window.h"

#include <memory>
#include <vector>

using namespace eve::graphics;

TEST_CASE("ssr.createConfigureAndApply") {
    eve::window::Window *win = nullptr;
    Graphics *gfx = nullptr;
    openGfxWindow(win, gfx, 96, 64);

    std::unique_ptr<ScreenSpaceReflection> ssr(gfx->newScreenSpaceReflection());
    REQUIRE(ssr.get() != nullptr);
    REQUIRE(ssr->getShader() != nullptr);
    REQUIRE(ssr->getShader()->gpuHandle != nullptr);
    ssr->setCamera(0.f, 1.f, 3.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 55.f, 1.5f, 0.1f,
                   30.f);
    ssr->setMaxDistance(12.f);
    ssr->setStepLength(0.25f);
    ssr->setMaxSteps(24);
    ssr->setThickness(0.35f);
    ssr->setStrength(0.75f);
    CHECK(ssr->hasParam("maxDist"));
    CHECK(ssr->getFloat("maxSteps") == 24.f);

    std::vector<uint8_t> scene(64u * 48u * 4u, 255u);
    std::vector<uint8_t> depth(64u * 48u * 4u, 128u);
    Texture *sceneTexture = gfx->newTexture(64, 48, scene.data());
    Texture *depthTexture = gfx->newTexture(64, 48, depth.data());
    Canvas *dest = gfx->newCanvas(64, 48);
    REQUIRE(sceneTexture != nullptr);
    REQUIRE(depthTexture != nullptr);
    REQUIRE(dest != nullptr);
    ssr->applyFromSceneTo(gfx, sceneTexture, depthTexture, nullptr, dest);
    REQUIRE(dest->getTexture() != nullptr);

    ssr->setEnabled(false);
    CHECK(!ssr->getEnabled());
    ssr->applyFromSceneTo(gfx, sceneTexture, depthTexture, nullptr, dest);
    win->close();
}
