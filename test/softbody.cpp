#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
#include "physics/Cloth.h"
#include "physics/Fluid.h"
#include "physics/Physics.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <memory>

using namespace eve::physics;
using namespace eve::graphics;

TEST_CASE("softbody.cloth.fallsAndPinsHold") {
    auto *mod = Physics::create();
    std::unique_ptr<Cloth> cloth(mod->newCloth(8, 6, 10.f, 100.f, 40.f));
    REQUIRE(cloth.get() != nullptr);
    CHECK_EQ(cloth->getCols(), 8);
    CHECK_EQ(cloth->getRows(), 6);
    CHECK_EQ(cloth->getParticleCount(), 48);
    CHECK(cloth->isPinned(0));
    CHECK(cloth->isPinned(7));

    // Pinned top row holds under gravity.
    const float pinY0 = cloth->getParticleY(0);
    const float pinX0 = cloth->getParticleX(0);
    cloth->setBounds(0.f, 0.f, 800.f, 600.f);
    for (int i = 0; i < 60; ++i)
        cloth->update(1.f / 60.f);
    CHECK(std::fabs(cloth->getParticleY(0) - pinY0) < 0.5f);
    CHECK(std::fabs(cloth->getParticleX(0) - pinX0) < 0.5f);

    // Free-fall: unpin everything and verify gravity moves particles down.
    for (int i = 0; i < cloth->getParticleCount(); ++i)
        cloth->unpin(i);
    const float freeY0 = cloth->getParticleY(20);
    for (int i = 0; i < 45; ++i)
        cloth->update(1.f / 60.f);
    CHECK_GT(cloth->getParticleY(20), freeY0 + 20.f);
}

TEST_CASE("softbody.cloth.grabMovesParticle") {
    auto *mod = Physics::create();
    std::unique_ptr<Cloth> cloth(mod->newCloth(6, 6, 12.f, 50.f, 50.f));
    // Unpin a mid particle so it can be grabbed.
    const int idx = 3 * 6 + 3;
    cloth->unpin(idx);
    const float x = cloth->getParticleX(idx);
    const float y = cloth->getParticleY(idx);

    int grabbed = cloth->grabAt(x, y, 20.f);
    CHECK_EQ(grabbed, idx);
    CHECK(cloth->isGrabbing());
    cloth->moveGrab(x + 40.f, y + 30.f);
    for (int i = 0; i < 10; ++i)
        cloth->update(1.f / 60.f);

    CHECK(std::fabs(cloth->getParticleX(idx) - (x + 40.f)) < 1.f);
    CHECK(std::fabs(cloth->getParticleY(idx) - (y + 30.f)) < 1.f);
    cloth->releaseGrab();
    CHECK(!cloth->isGrabbing());
}

TEST_CASE("softbody.fluid.emitAndSettle") {
    auto *mod = Physics::create();
    std::unique_ptr<Fluid> fluid(mod->newFluid(256));
    REQUIRE(fluid.get() != nullptr);
    fluid->setBounds(0.f, 0.f, 400.f, 400.f);
    fluid->setGravity(0.f, 900.f);
    int added = fluid->emit(200.f, 80.f, 40, 0.f, 0.f);
    CHECK_EQ(added, 40);
    CHECK_EQ(fluid->getParticleCount(), 40);

    float y0 = 0.f;
    for (int i = 0; i < fluid->getParticleCount(); ++i)
        y0 += fluid->getParticleY(i);
    y0 /= float(fluid->getParticleCount());

    for (int i = 0; i < 120; ++i)
        fluid->update(1.f / 60.f);

    float y1 = 0.f;
    for (int i = 0; i < fluid->getParticleCount(); ++i)
        y1 += fluid->getParticleY(i);
    y1 /= float(fluid->getParticleCount());

    CHECK_GT(y1, y0 + 10.f);
    // Stay inside container.
    for (int i = 0; i < fluid->getParticleCount(); ++i) {
        CHECK_GE(fluid->getParticleX(i), -1.f);
        CHECK_LE(fluid->getParticleX(i), 401.f);
        CHECK_GE(fluid->getParticleY(i), -1.f);
        CHECK_LE(fluid->getParticleY(i), 401.f);
    }
}

TEST_CASE("softbody.fluid.interactRepels") {
    auto *mod = Physics::create();
    std::unique_ptr<Fluid> fluid(mod->newFluid(128));
    fluid->setBounds(0.f, 0.f, 300.f, 300.f);
    fluid->setGravity(0.f, 0.f);
    fluid->emit(150.f, 150.f, 25, 0.f, 0.f);

    float avgDist0 = 0.f;
    for (int i = 0; i < fluid->getParticleCount(); ++i) {
        const float dx = fluid->getParticleX(i) - 150.f;
        const float dy = fluid->getParticleY(i) - 150.f;
        avgDist0 += std::sqrt(dx * dx + dy * dy);
    }
    avgDist0 /= float(fluid->getParticleCount());

    for (int i = 0; i < 45; ++i) {
        fluid->interactAt(150.f, 150.f, 80.f, -4000.f);
        fluid->update(1.f / 60.f);
    }

    float avgDist1 = 0.f;
    for (int i = 0; i < fluid->getParticleCount(); ++i) {
        const float dx = fluid->getParticleX(i) - 150.f;
        const float dy = fluid->getParticleY(i) - 150.f;
        avgDist1 += std::sqrt(dx * dx + dy * dy);
    }
    avgDist1 /= float(fluid->getParticleCount());
    CHECK_GT(avgDist1, avgDist0 + 2.f);
}

TEST_CASE("softbody.render.clothAndFluidPreview") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width = 720;
    s.height = 420;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto *mod = Physics::create();
    std::unique_ptr<Cloth> cloth(mod->newCloth(14, 10, 12.f, 40.f, 30.f));
    cloth->setBounds(0.f, 0.f, 720.f, 420.f);
    cloth->setColor(0.78f, 0.84f, 0.98f, 1.f);

    std::unique_ptr<Fluid> fluid(mod->newFluid(400));
    fluid->setBounds(360.f, 40.f, 320.f, 340.f);
    fluid->setGravity(0.f, 980.f);
    fluid->setColor(0.2f, 0.55f, 0.95f, 0.9f);
    fluid->emit(520.f, 90.f, 120, 0.f, 40.f);

    gfx->setBackgroundColorRGBA(0.07f, 0.08f, 0.11f, 1.f);
    for (int frame = 0; frame < 90; ++frame) {
        if (frame == 30)
            cloth->grabAt(cloth->getParticleX(5 * 14 + 7), cloth->getParticleY(5 * 14 + 7), 30.f);
        if (frame >= 30 && frame < 60)
            cloth->moveGrab(180.f + float(frame - 30) * 2.f, 220.f);
        if (frame == 60)
            cloth->releaseGrab();
        if ((frame % 15) == 0)
            fluid->emit(500.f, 70.f, 8, 20.f, 60.f);
        fluid->interactAt(520.f, 200.f, 70.f, -2500.f);

        cloth->update(1.f / 60.f);
        fluid->update(1.f / 60.f);

        gfx->clearScreen();
        // Fluid tank outline.
        gfx->drawSolidRectRGBA(360.f, 40.f, 320.f, 340.f, 0.12f, 0.14f, 0.18f, 1.f);
        cloth->draw(gfx);
        fluid->draw(gfx);
        gfx->present();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK_GT(cloth->getParticleY(5 * 14 + 7), 40.f);
    CHECK_GT(fluid->getParticleCount(), 100);
    win->close();
}
