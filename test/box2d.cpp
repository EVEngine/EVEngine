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
#include "physics/Body.h"
#include "physics/Fixture.h"
#include "physics/Physics.h"
#include "physics/World.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <memory>
#include <vector>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::physics;
using namespace eve::graphics;

TEST_CASE("box2d.module.meter") {
    auto *mod = Physics::create();
    CHECK_EQ(mod->getMeter(), 30.f);
    mod->setMeter(50.f);
    CHECK_EQ(mod->getMeter(), 50.f);
    mod->setMeter(30.f);
}

TEST_CASE("box2d.world.createAndGravity") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 900.f, true));
    REQUIRE(world.get() != nullptr);
    CHECK(std::fabs(world->getGravityX()) < 0.01f);
    CHECK(std::fabs(world->getGravityY() - 900.f) < 0.01f);
    world->setGravity(10.f, 20.f);
    CHECK(std::fabs(world->getGravityX() - 10.f) < 0.01f);
    CHECK(std::fabs(world->getGravityY() - 20.f) < 0.01f);
}

TEST_CASE("box2d.body.fallsUnderGravity") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 900.f, true));
    Body *ground = world->newBody("static", 400.f, 500.f);
    ground->newRectangleFixture(800.f, 40.f, 0.f, 0.5f, 0.f);

    Body *box = world->newBody("dynamic", 400.f, 100.f);
    box->newRectangleFixture(40.f, 40.f, 1.f, 0.3f, 0.1f);
    float y0 = box->getY();

    for (int i = 0; i < 60; ++i)
        world->update(1.f / 60.f);

    CHECK_GT(box->getY(), y0);
    CHECK_LT(box->getY(), 500.f);
}

TEST_CASE("box2d.body.staticDoesNotMove") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 900.f, true));
    Body *ground = world->newBody("static", 200.f, 300.f);
    ground->newRectangleFixture(100.f, 20.f, 0.f);
    float y0 = ground->getY();
    for (int i = 0; i < 30; ++i)
        world->update(1.f / 60.f);
    CHECK(std::fabs(ground->getY() - y0) < 0.01f);
    CHECK_EQ(ground->getType(), std::string("static"));
}

TEST_CASE("box2d.body.circleAndVelocity") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body *ball = world->newBody("dynamic", 0.f, 0.f);
    ball->newCircleFixture(16.f, 1.f, 0.2f, 0.8f);
    ball->setLinearVelocity(300.f, 0.f);

    for (int i = 0; i < 30; ++i)
        world->update(1.f / 60.f);

    CHECK_GT(ball->getX(), 100.f);
    CHECK(std::fabs(ball->getY()) < 5.f);
}

TEST_CASE("box2d.fixture.sensor") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body *b = world->newBody("dynamic", 0.f, 0.f);
    Fixture *fixture = b->newRectangleFixture(10.f, 10.f);
    CHECK(!fixture->isSensor());
    fixture->setSensor(true);
    CHECK(fixture->isSensor());
    fixture->setFriction(0.7f);
    CHECK(std::fabs(fixture->getFriction() - 0.7f) < 0.001f);
}

TEST_CASE("box2d.fixture.tagsFiltersAndBodyProperties") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    std::unique_ptr<Body> body(world->newBody("dynamic", 30.f, 60.f));
    std::unique_ptr<Fixture> fixture(body->newRectangleFixture(30.f, 60.f, 2.f));

    fixture->setTag("prop.crate");
    fixture->setCategoryBits(0x0020);
    fixture->setMaskBits(0x0006);
    fixture->setGroupIndex(-3);
    CHECK_EQ(fixture->getTag(), std::string("prop.crate"));
    CHECK_EQ(fixture->getCategoryBits(), 0x0020);
    CHECK_EQ(fixture->getMaskBits(), 0x0006);
    CHECK_EQ(fixture->getGroupIndex(), -3);
    CHECK_EQ(fixture->getBodyId(), body->getId());
    CHECK_GT(body->getMass(), 0.f);
    CHECK(std::fabs(body->getWorldCenterX() - 30.f) < 0.01f);
    CHECK(std::fabs(body->getWorldCenterY() - 60.f) < 0.01f);
    body->setLinearVelocity(300.f, 400.f);
    CHECK(std::fabs(body->getLinearSpeed() - 500.f) < 0.1f);
}

TEST_CASE("box2d.contacts.exposeTagsAndImpactData") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body *wall = world->newBody("static", 300.f, 100.f);
    Fixture *wallFixture = wall->newRectangleFixture(20.f, 160.f, 0.f, 0.2f, 0.f);
    wallFixture->setTag("terrain");
    Body *crate = world->newBody("dynamic", 80.f, 100.f);
    Fixture *crateFixture = crate->newRectangleFixture(40.f, 40.f, 1.f, 0.2f, 0.f);
    crateFixture->setTag("prop.crate");
    crate->setBullet(true);
    crate->setLinearVelocity(900.f, 0.f);

    for (int i = 0; i < 30 && world->getImpactCount() == 0; ++i)
        world->update(1.f / 60.f);

    REQUIRE_GT(world->getBeginContactCount(), 0);
    CHECK_EQ(world->getBeginContactFixtureATag(0), std::string("terrain"));
    CHECK_EQ(world->getBeginContactFixtureBTag(0), std::string("prop.crate"));
    REQUIRE_GT(world->getImpactCount(), 0);
    CHECK_GT(world->getImpactRelativeNormalSpeed(0), 100.f);
    CHECK_GT(world->getImpactNormalImpulse(0), 0.f);
    CHECK(std::fabs(world->getImpactNormalX(0)) > 0.9f);
    CHECK(std::fabs(world->getImpactPointX(0) - 290.f) < 5.f);

    world->clearContactEvents();
    CHECK_EQ(world->getBeginContactCount(), 0);
    CHECK_EQ(world->getEndContactCount(), 0);
    CHECK_EQ(world->getImpactCount(), 0);
    crate->setPosition(80.f, 100.f);
    crate->setLinearVelocity(0.f, 0.f);
    for (int i = 0; i < 5; ++i) world->update(1.f / 60.f);
    CHECK_EQ(world->getImpactCount(), 0);

    delete crateFixture;
    delete crate;
    delete wallFixture;
    delete wall;
}

TEST_CASE("box2d.gameplay.sameKickMovesLightPropFaster") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body *crate = world->newBody("dynamic", 0.f, 0.f);
    crate->newRectangleFixture(40.f, 40.f, 1.f, 0.45f, 0.05f);
    Body *rock = world->newBody("dynamic", 100.f, 0.f);
    rock->newRectangleFixture(40.f, 40.f, 8.f, 0.85f, 0.05f);

    crate->applyLinearImpulse(720.f, -105.f);
    rock->applyLinearImpulse(720.f, -105.f);

    CHECK_GT(rock->getMass(), crate->getMass() * 7.f);
    CHECK_GT(crate->getLinearSpeed(), rock->getLinearSpeed() * 7.f);
}

TEST_CASE("box2d.contacts.destroyFixturePurgesQueuedEvents") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body *wall = world->newBody("static", 100.f, 100.f);
    Fixture *wallFixture = wall->newRectangleFixture(40.f, 40.f);
    wallFixture->setTag("terrain");
    Body *box = world->newBody("dynamic", 100.f, 100.f);
    Fixture *boxFixture = box->newRectangleFixture(30.f, 30.f, 1.f);
    boxFixture->setTag("prop.crate");
    world->update(1.f / 60.f);
    REQUIRE_GT(world->getBeginContactCount(), 0);

    boxFixture->destroy();
    CHECK_EQ(world->getBeginContactCount(), 0);
    CHECK_EQ(world->getEndContactCount(), 0);
    CHECK_EQ(world->getImpactCount(), 0);

    delete boxFixture;
    delete box;
    delete wallFixture;
    delete wall;
}

TEST_CASE("box2d.body.destroy") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body *b = world->newBody("dynamic", 10.f, 20.f);
    b->newRectangleFixture(8.f, 8.f);
    int id = b->getId();
    CHECK_GT(id, 0);
    b->destroy();
    CHECK(std::fabs(b->getX()) < 0.001f);
    delete b;
}

TEST_CASE("box2d.query.rayCastAndAABB") {
    auto *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body *a = world->newBody("static", 100.f, 100.f);
    Fixture *fa = a->newRectangleFixture(40.f, 40.f);
    Body *b = world->newBody("static", 300.f, 100.f);
    b->newCircleFixture(20.f);

    CHECK(fa->testPoint(100.f, 100.f));
    CHECK(!fa->testPoint(0.f, 0.f));

    int hit = world->rayCast(0.f, 100.f, 400.f, 100.f);
    CHECK_EQ(hit, a->getId());
    CHECK(world->hasRayHit());
    CHECK(std::fabs(world->getRayHitY() - 100.f) < 1.f);
    CHECK(world->getRayHitFraction() > 0.f);
    CHECK(world->getRayHitFraction() < 1.f);

    int n = world->queryAABB(80.f, 80.f, 40.f, 40.f);
    CHECK_EQ(n, 1);
    CHECK_EQ(world->getQueryBodyId(0), a->getId());

    n = world->queryAABB(0.f, 0.f, 400.f, 200.f);
    CHECK_EQ(n, 2);
}

TEST_CASE("box2d.world.destroyInvalidates") {
    auto *mod = Physics::create();
    World *world = mod->newWorld(0.f, 100.f, true);
    Body *b = world->newBody("dynamic", 50.f, 50.f);
    b->newCircleFixture(5.f);
    world->destroy();
    CHECK(std::fabs(b->getX()) < 0.001f);
    delete b;
    delete world;
}

static void drawFilledCircle(Graphics *gfx, float cx, float cy, float r, const Color &c) {
    const int steps = 12;
    for (int i = 0; i < steps; ++i) {
        const float a0 = float(i) * 6.2831853f / float(steps);
        const float a1 = float(i + 1) * 6.2831853f / float(steps);
        const float x0 = cx + std::cos(a0) * r;
        const float y0 = cy + std::sin(a0) * r;
        const float x1 = cx + std::cos(a1) * r;
        const float y1 = cy + std::sin(a1) * r;
        // Approximate pie with a small rect along the rim.
        gfx->drawSolidRect((x0 + x1) * 0.5f - 2.f, (y0 + y1) * 0.5f - 2.f, 4.f, 4.f, c);
        gfx->drawSolidRect(cx - 2.f, cy - 2.f, 4.f, 4.f, c);
    }
    // Fill denser disk
    for (float rad = r; rad > 0.f; rad -= 3.f) {
        for (int i = 0; i < steps; ++i) {
            const float a = float(i) * 6.2831853f / float(steps);
            gfx->drawSolidRect(cx + std::cos(a) * rad - 2.f, cy + std::sin(a) * rad - 2.f, 4.f, 4.f,
                               c);
        }
    }
}

static void drawOrientedBox(Graphics *gfx, float cx, float cy, float w, float h, float angle,
                            const Color &c) {
    const float hw = w * 0.5f;
    const float hh = h * 0.5f;
    const float ca = std::cos(angle);
    const float sa = std::sin(angle);
    auto corner = [&](float lx, float ly, float &ox, float &oy) {
        ox = cx + lx * ca - ly * sa;
        oy = cy + lx * sa + ly * ca;
    };
    float x0, y0, x1, y1, x2, y2, x3, y3;
    corner(-hw, -hh, x0, y0);
    corner(hw, -hh, x1, y1);
    corner(hw, hh, x2, y2);
    corner(-hw, hh, x3, y3);
    auto edge = [&](float ax, float ay, float bx, float by) {
        const float dx = bx - ax;
        const float dy = by - ay;
        const float len = std::sqrt(dx * dx + dy * dy);
        const float steps = std::max(1.f, len / 3.f);
        for (float t = 0.f; t <= 1.f; t += 1.f / steps)
            gfx->drawSolidRect(ax + dx * t - 2.f, ay + dy * t - 2.f, 4.f, 4.f, c);
    };
    edge(x0, y0, x1, y1);
    edge(x1, y1, x2, y2);
    edge(x2, y2, x3, y3);
    edge(x3, y3, x0, y0);
    gfx->drawSolidRect(cx - 3.f, cy - 3.f, 6.f, 6.f, c);
}

TEST_CASE("box2d.render.fallingStackPreview") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    eve::window::WindowSettings s;
    s.width = 640;
    s.height = 480;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    auto *mod = Physics::create();
    mod->setMeter(30.f);
    std::unique_ptr<World> world(mod->newWorld(0.f, 1200.f, true));

    Body *ground = world->newBody("static", 320.f, 440.f);
    ground->newRectangleFixture(600.f, 30.f, 0.f, 0.6f, 0.1f);

    Body *ledge = world->newBody("static", 480.f, 280.f);
    ledge->newRectangleFixture(180.f, 20.f, 0.f, 0.5f, 0.05f);
    ledge->setAngle(0.25f);

    struct Dyn {
        Body *body;
        float w, h;
        bool circle;
        Color color;
    };
    std::vector<Dyn> dyn;
    const Color palette[] = {
        Color(0.95f, 0.45f, 0.35f, 1.f), Color(0.35f, 0.75f, 0.95f, 1.f),
        Color(0.45f, 0.9f, 0.5f, 1.f),   Color(0.95f, 0.85f, 0.35f, 1.f),
        Color(0.8f, 0.5f, 0.95f, 1.f),
    };
    for (int i = 0; i < 8; ++i) {
        Body *b = world->newBody("dynamic", 220.f + float(i % 4) * 45.f, 40.f + float(i) * 28.f);
        const bool circle = (i % 3 == 0);
        if (circle) {
            b->newCircleFixture(16.f, 1.f, 0.3f, 0.55f);
            dyn.push_back({b, 32.f, 32.f, true, palette[i % 5]});
        } else {
            b->newRectangleFixture(34.f, 28.f, 1.f, 0.35f, 0.15f);
            dyn.push_back({b, 34.f, 28.f, false, palette[i % 5]});
        }
    }

    // Launch a fast ball into the stack.
    Body *bullet = world->newBody("dynamic", 40.f, 200.f);
    bullet->newCircleFixture(14.f, 1.f, 0.2f, 0.7f);
    bullet->setLinearVelocity(520.f, -40.f);
    dyn.push_back({bullet, 28.f, 28.f, true, Color(1.f, 1.f, 1.f, 1.f)});

    gfx->setBackgroundColorRGBA(0.08f, 0.09f, 0.12f, 1.f);
    float maxY = 0.f;
    for (int frame = 0; frame < 120; ++frame) {
        world->update(1.f / 60.f);
        gfx->clearScreen();

        drawOrientedBox(gfx, ground->getX(), ground->getY(), 600.f, 30.f, ground->getAngle(),
                        Color(0.35f, 0.38f, 0.45f, 1.f));
        drawOrientedBox(gfx, ledge->getX(), ledge->getY(), 180.f, 20.f, ledge->getAngle(),
                        Color(0.45f, 0.42f, 0.38f, 1.f));

        for (const Dyn &d : dyn) {
            if (d.circle)
                drawFilledCircle(gfx, d.body->getX(), d.body->getY(), d.w * 0.5f, d.color);
            else
                drawOrientedBox(gfx, d.body->getX(), d.body->getY(), d.w, d.h, d.body->getAngle(),
                                d.color);
            maxY = std::max(maxY, d.body->getY());
        }

        gfx->present();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) break;
        }
        SDL_Delay(16);
    }

    CHECK_GT(maxY, 200.f);
    win->close();
}
