#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ik/IK.h"
#include "ik/Skeleton2D.h"
#include "ik/Skeleton3D.h"
#include "ik/Solver2D.h"
#include "ik/Solver3D.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
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

static bool near(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}

TEST_CASE("ik.module.name") {
    auto *mod = IK::create();
    CHECK_EQ(mod->getName(), std::string("IK"));
    CHECK_EQ(IK::create(), mod);
}

TEST_CASE("ik.skeleton2d.topology") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    CHECK_EQ(sk->getBoneCount(), 1);
    CHECK_EQ(sk->getRootId(), 0);
    CHECK_EQ(sk->getParent(0), -1);

    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);
    CHECK_EQ(sk->getBoneCount(), 3);
    CHECK_EQ(b1, 1);
    CHECK_EQ(b2, 2);
    CHECK_EQ(sk->getParent(b2), b1);
    CHECK_EQ(sk->getChildCount(0), 1);
    CHECK_EQ(sk->getChild(0, 0), b1);
    CHECK(near(sk->getLength(b1), 1.f));
    CHECK(near(sk->totalLengthTo(b2), 2.f));
}

TEST_CASE("ik.skeleton2d.forwardKinematics") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);

    sk->setPosition(0, 0.f, 0.f);
    sk->setRotation(0, 0.f);
    sk->setRotation(b1, 0.f);
    sk->setRotation(b2, float(M_PI / 2.0));
    sk->forwardKinematics();

    CHECK(near(sk->getX(b1), 1.f));
    CHECK(near(sk->getY(b1), 0.f));
    CHECK(near(sk->getX(b2), 1.f));
    CHECK(near(sk->getY(b2), 1.f));
}

TEST_CASE("ik.solver2d.reachesTarget") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);
    sk->initStraightPose(0.f, 0.f);

    std::unique_ptr<Solver2D> solver(new Solver2D());
    solver->setMaxIterations(32);
    solver->setTolerance(1e-3f);
    solver->addTarget(b2, 1.f, 1.f, 1.f);
    CHECK_EQ(solver->getTargetCount(), 1);
    CHECK(solver->solve(sk.get()));
    CHECK(near(sk->getX(b2), 1.f, 1e-2f));
    CHECK(near(sk->getY(b2), 1.f, 1e-2f));
}

TEST_CASE("ik.solver2d.constraints") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);
    sk->initStraightPose(0.f, 0.f);
    sk->setConstraints(b1, -0.2f, 0.2f);
    CHECK(sk->hasConstraints(b1));

    std::unique_ptr<Solver2D> solver(new Solver2D());
    solver->setMaxIterations(64);
    solver->setTolerance(1e-3f);
    solver->addTarget(b2, 0.f, 2.f, 1.f);
    solver->solve(sk.get());

    // Hinge limit on b1 should keep the chain from folding fully upward.
    CHECK(std::fabs(sk->getRotation(b1)) <= 0.25f + 1e-3f);
}

TEST_CASE("ik.skeleton3d.straightPoseAndSolve") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);
    sk->initStraightPose(0.f, 0.f, 0.f);
    CHECK(near(sk->getX(b2), 2.f));
    CHECK(near(sk->getY(b2), 0.f));
    CHECK(near(sk->getZ(b2), 0.f));

    std::unique_ptr<Solver3D> solver(new Solver3D());
    solver->setMaxIterations(48);
    solver->addTarget(b2, 1.f, 1.f, 0.f, 1.f);
    CHECK(solver->solve(sk.get()));
    CHECK(near(sk->getX(b2), 1.f, 2e-2f));
    CHECK(near(sk->getY(b2), 1.f, 2e-2f));
}

TEST_CASE("ik.solver2d.solveChain") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);
    int b3 = sk->createBone(0, 1.f);  // side branch under the skeleton root
    // createBone renumbers the skeleton (BFS order), so re-query ids afterwards.
    b1 = sk->getChild(0, 0);
    b3 = sk->getChild(0, 1);
    b2 = sk->getChild(b1, 0);
    sk->initStraightPose(10.f, 20.f);
    CHECK(near(sk->getX(b1), 11.f));
    CHECK(near(sk->getX(b2), 12.f));
    CHECK(near(sk->getX(b3), 11.f));

    std::unique_ptr<Solver2D> solver(new Solver2D());
    solver->setMaxIterations(32);
    solver->setTolerance(1e-3f);
    solver->addTarget(b2, 11.f, 21.f, 1.f);
    CHECK(solver->solveChain(sk.get(), b1, b2));

    // Chain root (b1) stays pinned; skeleton root and the side branch are untouched.
    CHECK(near(sk->getX(0), 10.f));
    CHECK(near(sk->getY(0), 20.f));
    CHECK(near(sk->getX(b1), 11.f));
    CHECK(near(sk->getY(b1), 20.f));
    CHECK(near(sk->getX(b3), 11.f));
    CHECK(near(sk->getY(b3), 20.f));
    CHECK(near(sk->getX(b2), 11.f, 1e-2f));
    CHECK(near(sk->getY(b2), 21.f, 1e-2f));
}

TEST_CASE("ik.solver2d.solveChain.validation") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);
    int b3 = sk->createBone(0, 1.f);
    b1 = sk->getChild(0, 0);
    b3 = sk->getChild(0, 1);
    b2 = sk->getChild(b1, 0);
    sk->initStraightPose(0.f, 0.f);

    std::unique_ptr<Solver2D> solver(new Solver2D());
    solver->addTarget(b3, 1.f, 1.f, 1.f);
    // Target on the side branch is not part of the chain: nothing to solve.
    CHECK(solver->solveChain(sk.get(), b1, b2));
    CHECK(near(sk->getX(b2), 2.f));
    CHECK_THROWS(solver->solveChain(sk.get(), b2, b1));  // tip not a descendant of root
    CHECK_THROWS(solver->solveChain(sk.get(), b1, b1));  // root == tip
    CHECK_THROWS(solver->solveChain(sk.get(), 0, 99));   // out of range
}

TEST_CASE("ik.solver2d.influence") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);
    sk->initStraightPose(0.f, 0.f);

    std::unique_ptr<Solver2D> solver(new Solver2D());
    solver->setMaxIterations(32);
    solver->addTarget(b2, 0.f, 2.f, 1.f);

    solver->setInfluence(0.5f);
    solver->solve(sk.get());
    CHECK(near(sk->getX(b2), 1.f, 1e-2f));
    CHECK(near(sk->getY(b2), 1.f, 1e-2f));

    sk->initStraightPose(0.f, 0.f);
    solver->setInfluence(0.f);
    solver->solve(sk.get());
    CHECK(near(sk->getX(b2), 2.f));
    CHECK(near(sk->getY(b2), 0.f));
    CHECK_THROWS((solver->setInfluence(1.5f), false));
}

TEST_CASE("ik.solver3d.solveChain.pinsChainRoot") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);
    int b3 = sk->createBone(0, 1.f);
    b1 = sk->getChild(0, 0);
    b3 = sk->getChild(0, 1);
    b2 = sk->getChild(b1, 0);
    sk->initStraightPose(0.f, 0.f, 0.f);

    std::unique_ptr<Solver3D> solver(new Solver3D());
    solver->setMaxIterations(48);
    solver->setTolerance(1e-3f);
    solver->addTarget(b2, 1.f, 1.f, 0.f, 1.f);
    CHECK(solver->solveChain(sk.get(), b1, b2));

    CHECK(near(sk->getX(0), 0.f));
    CHECK(near(sk->getX(b1), 1.f));
    CHECK(near(sk->getY(b1), 0.f));
    CHECK(near(sk->getX(b3), 1.f));
    CHECK(near(sk->getY(b3), 0.f));
    CHECK(near(sk->getX(b2), 1.f, 2e-2f));
    CHECK(near(sk->getY(b2), 1.f, 2e-2f));
}

TEST_CASE("ik.solver3d.pole") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);
    sk->initStraightPose(0.f, 0.f, 0.f);

    std::unique_ptr<Solver3D> solver(new Solver3D());
    solver->setMaxIterations(64);
    solver->setTolerance(1e-4f);
    solver->addTarget(b2, 1.5f, 0.5f, 0.f, 1.f);

    solver->setPole(1.f, 1.f, 0.f, 1.f);
    CHECK(solver->hasPole());
    CHECK(near(solver->getPoleWeight(), 1.f));
    CHECK(solver->solveChain(sk.get(), 0, b2));
    const float upY = sk->getY(b1);
    CHECK(upY > 0.1f);

    sk->initStraightPose(0.f, 0.f, 0.f);
    solver->setPole(1.f, -1.f, 0.f, 1.f);
    CHECK(solver->solveChain(sk.get(), 0, b2));
    const float downY = sk->getY(b1);
    CHECK(downY < -0.1f);

    sk->initStraightPose(0.f, 0.f, 0.f);
    solver->clearPole();
    CHECK(!solver->hasPole());
    CHECK(solver->solveChain(sk.get(), 0, b2));
}

TEST_CASE("ik.solver3d.tipRotation") {
    std::unique_ptr<Skeleton3D> sk(new Skeleton3D());
    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);
    sk->initStraightPose(0.f, 0.f, 0.f);

    std::unique_ptr<Solver3D> solver(new Solver3D());
    solver->setMaxIterations(48);
    solver->addTarget(b2, 2.f, 0.f, 0.f, 1.f);
    solver->setTipRotation(b2, 0.f, static_cast<float>(M_PI / 2.0), 1.f);
    CHECK(near(solver->getTipRotationWeight(), 1.f));
    CHECK(solver->solveChain(sk.get(), 0, b2));

    // Tip keeps reaching the target while its world direction is pitched up.
    CHECK(near(sk->getX(b2), 2.f, 2e-2f));
    CHECK(near(sk->getOrientationZ(b2), 1.f, 1e-2f));
    CHECK(near(sk->getRotationPitch(b2), static_cast<float>(M_PI / 2.0), 1e-2f));

    solver->clearTipRotation();
    CHECK(near(solver->getTipRotationWeight(), 0.f));
    CHECK(solver->solveChain(sk.get(), 0, b2));
    CHECK(near(sk->getOrientationZ(b2), 0.f, 1e-2f));
}

TEST_CASE("ik.solver2d.stepChain") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    int b1 = sk->createBone(0, 1.f);
    int b2 = sk->createBone(b1, 1.f);
    sk->initStraightPose(0.f, 0.f);

    std::unique_ptr<Solver2D> solver(new Solver2D());
    solver->addTarget(b2, 0.f, 2.f, 1.f);
    solver->stepChain(sk.get(), 0, b2, 1.f);
    CHECK(near(sk->getX(b2), 0.f, 1e-2f));
    CHECK(near(sk->getY(b2), 2.f, 1e-2f));
}

TEST_CASE("ik.factory") {
    auto *mod = IK::create();
    std::unique_ptr<Skeleton2D> sk(mod->newSkeleton2D());
    std::unique_ptr<Solver2D> solver(mod->newSolver2D());
    CHECK_EQ(sk->getBoneCount(), 1);
    CHECK_EQ(solver->getMaxIterations(), 16);
}

TEST_CASE("ik.invalidBoneThrows") {
    std::unique_ptr<Skeleton2D> sk(new Skeleton2D());
    bool threw = false;
    try {
        sk->getX(99);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

static void drawSeg(Graphics *gfx, float x0, float y0, float x1, float y1, const Color &c) {
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
    win->setGraphics(gfx);
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
