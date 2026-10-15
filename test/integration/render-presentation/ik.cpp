#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ik/IK.h"
#include "ik/Skeleton2D.h"
#include "ik/Skeleton3D.h"
#include "ik/Solver2D.h"
#include "ik/Solver3D.h"

#include "common/Exception.h"
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
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <memory>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace eve::ik;
using namespace eve::graphics;

[[maybe_unused]] static bool near(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}


[[maybe_unused]] static void drawSeg(Graphics *gfx, float x0, float y0, float x1, float y1, const Color &c) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.5f) return;
    const float steps = std::max(1.f, len / 2.5f);
    for (float t = 0.f; t <= 1.f; t += 1.f / steps) {
        gfx->drawSolidRect(x0 + dx * t - 2.f, y0 + dy * t - 2.f, 4.f, 4.f, c);
    }
}

TEST_CASE("ik.solver2d.armReachPreview") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    // Multi-segment arm in world units (will scale to pixels).
    int b1 = sk->createBone(0, 1.1f);
    int b2 = sk->createBone(b1, 0.95f);
    int b3 = sk->createBone(b2, 0.7f);
    sk->initStraightPose(0.f, 0.f);
    sk->setConstraints(b1, -1.2f, 1.2f);
    sk->setConstraints(b2, -1.6f, 1.6f);

    std::unique_ptr<Solver2D> solver(new Solver2D());
    solver->setMaxIterations(40);
    solver->setTolerance(1e-3f);
    solver->addTarget(b3, 1.5f, 0.8f, 1.f);

    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width = 560;
    s.height = 420;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    const float scale = 110.f;
    const float ox = float(gfx->getWidth()) * 0.35f;
    const float oy = float(gfx->getHeight()) * 0.55f;

    gfx->setBackgroundColorRGBA(0.07f, 0.08f, 0.11f, 1.f);
    float tipErr = 1.f;

    for (int frame = 0; frame < 120; ++frame) {
        const float t = float(frame) * 0.05f;
        // Orbiting reach target (figure-eight-ish).
        const float tx = 1.1f + 0.85f * std::cos(t);
        const float ty = 0.2f + 0.75f * std::sin(t * 1.3f);
        solver->clearTargets();
        solver->addTarget(b3, tx, ty, 1.f);
        solver->solve(sk.get());

        const float tipX = sk->getX(b3);
        const float tipY = sk->getY(b3);
        tipErr = std::sqrt((tipX - tx) * (tipX - tx) + (tipY - ty) * (tipY - ty));

        gfx->clearScreen();
        // Ground / base
        gfx->drawSolidRect(ox - 18.f, oy - 6.f, 36.f, 12.f, Color(0.35f, 0.38f, 0.45f, 1.f));

        auto toScreen = [&](float wx, float wy, float &sx, float &sy) {
            sx = ox + wx * scale;
            sy = oy - wy * scale;
        };

        float sx0, sy0, sx1, sy1;
        toScreen(sk->getX(0), sk->getY(0), sx0, sy0);
        for (int b = 1; b < sk->getBoneCount(); ++b) {
            const int p = sk->getParent(b);
            toScreen(sk->getX(p), sk->getY(p), sx0, sy0);
            toScreen(sk->getX(b), sk->getY(b), sx1, sy1);
            drawSeg(gfx, sx0, sy0, sx1, sy1, Color(0.45f, 0.85f, 1.f, 1.f));
            gfx->drawSolidRect(sx1 - 3.f, sy1 - 3.f, 6.f, 6.f, Color(1.f, 1.f, 1.f, 1.f));
        }

        float tSx, tSy;
        toScreen(tx, ty, tSx, tSy);
        gfx->drawSolidRect(tSx - 5.f, tSy - 5.f, 10.f, 10.f, Color(1.f, 0.45f, 0.25f, 1.f));
        gfx->present();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK(tipErr < 0.25f);
    win->close();
}
