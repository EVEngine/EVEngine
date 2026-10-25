// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "physics-backend",
  "module": "action",
  "modules": [
    "action",
    "climbing",
    "physics"
  ],
  "origins": [
    "action_collision_ignore.cpp",
    "box2d.cpp",
    "box3d.cpp",
    "climbing.cpp",
    "climbing_anchor_execution.cpp",
    "climbing_anchor_graph.cpp",
    "climbing_anchor_route.cpp",
    "climbing_definition_policy.cpp",
    "climbing_ecs.cpp",
    "climbing_events.cpp",
    "climbing_input.cpp",
    "climbing_parkour.cpp",
    "climbing_performance.cpp",
    "climbing_physics_policy.cpp"
  ],
  "cases": {
    "render-window": [
      "box2d.render.fallingStackPreview"
    ]
  }
}
*/
// clang-format on

// Related integration contracts share one translation unit to amortize header parsing.

// Contract section: action collision ignore
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

#include <memory>
#include <string_view>
#include "action/ActionBlockRuntime.h"
#include "action/ActionNotifyRegistry.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/PhysicsLink.h"
#include "physics/World3D.h"
#include "physics/action/ActionCollisionIgnoreState.h"
#include "zeroerr/unittest.h"
// action_collision_ignore.cpp
namespace test_action_collision_ignore {
namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return *parsed;
}

eve::action::ActionActiveBlock active(const eve::action::ActionTimelineEvent &event) {
    return {event.trackId, event.itemId, event.type, eve::Duration::zero(), eve::Duration::fromSeconds(0.5).takeValue(),
            event.payload};
}

}  // namespace

TEST_CASE("actionCollisionIgnore.overlapRestoresPhysicsAuthorityAndSurvivesStaleEntity") {
    auto                                  *physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    auto                                  *firstBody  = world->newBody("dynamic", 0.f, 0.f, 0.f);
    auto                                  *secondBody = world->newBody("dynamic", 1.f, 0.f, 0.f);
    auto                                   firstLink  = eve::physics::PhysicsLink::fromBody(*firstBody);
    auto                                   secondLink = eve::physics::PhysicsLink::fromBody(*secondBody);
    REQUIRE(firstLink.ok());
    REQUIRE(secondLink.ok());

    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    const eve::action::ActionExecutionId                     execution(701);
    ecs::EntityHandle                                        sourceHandle{nullptr, typeid(void), 11, 3};
    bool                                                     entityAlive = true;
    eve::physics::action_adapter::ActionCollisionIgnoreState ignores(
        *world,
        [&](ecs::EntityHandle handle,
            std::string_view  channel) -> eve::Result<eve::physics::action_adapter::ActionCollisionPair> {
            if (!entityAlive || handle.id != sourceHandle.id || channel != "self-vs-owner")
                return eve::Result<eve::physics::action_adapter::ActionCollisionPair>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "collision subject is stale", "entity"));
            return eve::Result<eve::physics::action_adapter::ActionCollisionPair>::success(
                {firstLink.value(), secondLink.value()});
        });

    eve::action::ActionTimelineEvent first{eve::action::ActionTimelineEventKind::StateEnter,
                                           id("collision-track:ignore"),
                                           id("collision-window:first"),
                                           id("collision:ignore-window"),
                                           eve::Duration::zero(),
                                           {{"channel", eve::Value("self-vs-owner")}}};
    auto                             second = first;
    second.itemId                           = id("collision-window:second");
    eve::action::ActionNotifyContext context;
    context.executionId = execution;
    context.source      = sourceHandle;
    CHECK(!registry.value().dispatch(first, context).ok());

    ignores.setEnabled(true);
    eve::action::ActionBlockRuntime runtime(registry.value());
    eve::action::ActionAdvance      entered;
    entered.id             = execution;
    entered.phase          = eve::action::ActionPhase::Active;
    entered.timelineEvents = {first, second};
    entered.activeBlocks   = {active(first), active(second)};
    REQUIRE(runtime.apply(entered, context).ok());
    CHECK_EQ(ignores.activeCount(), 2U);
    CHECK_EQ(ignores.pairCount(), 1U);
    CHECK(!world->isBodyPairCollisionEnabled(firstBody, secondBody));

    entityAlive = false;
    REQUIRE(runtime.interrupt(context).ok());
    CHECK_EQ(ignores.activeCount(), 0U);
    CHECK_EQ(ignores.pairCount(), 0U);
    CHECK(world->isBodyPairCollisionEnabled(firstBody, secondBody));
    REQUIRE(ignores.shutdown().ok());
}

TEST_CASE("actionCollisionIgnore.preservesExternalDisableAndCleansUpDestroyedBodies") {
    auto                                  *physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    auto                                  *firstBody  = world->newBody("dynamic", 0.f, 0.f, 0.f);
    auto                                  *secondBody = world->newBody("dynamic", 1.f, 0.f, 0.f);
    auto                                   firstLink  = eve::physics::PhysicsLink::fromBody(*firstBody);
    auto                                   secondLink = eve::physics::PhysicsLink::fromBody(*secondBody);
    REQUIRE(firstLink.ok());
    REQUIRE(secondLink.ok());
    world->setBodyPairCollisionEnabled(firstBody, secondBody, false);

    eve::physics::action_adapter::ActionCollisionIgnoreState ignores(
        *world,
        [&](ecs::EntityHandle, std::string_view) -> eve::Result<eve::physics::action_adapter::ActionCollisionPair> {
            return eve::Result<eve::physics::action_adapter::ActionCollisionPair>::success(
                {firstLink.value(), secondLink.value()});
        });
    eve::action::ActionStateWindowBinding binding;
    binding.kind     = eve::action::ActionStateWindowKind::CollisionIgnore;
    binding.resource = "external-disable";
    eve::action::ActionTimelineEvent event{eve::action::ActionTimelineEventKind::StateEnter,
                                           id("collision-track:ignore"),
                                           id("collision-window:external"),
                                           id("collision:ignore-window"),
                                           eve::Duration::zero(),
                                           {}};
    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId(702);
    context.source      = ecs::EntityHandle{nullptr, typeid(void), 12, 1};
    REQUIRE(ignores.enter(binding, event, context).ok());
    REQUIRE(ignores.exit(binding, event, context).ok());
    CHECK(!world->isBodyPairCollisionEnabled(firstBody, secondBody));

    REQUIRE(ignores.enter(binding, event, context).ok());
    secondBody->destroy();
    REQUIRE(ignores.exit(binding, event, context).ok());
    CHECK_EQ(ignores.activeCount(), 0U);
    CHECK_EQ(ignores.pairCount(), 0U);
    REQUIRE(ignores.shutdown().ok());
}

TEST_CASE("actionCollisionIgnore.destructorRestoresAndWorldMayDieFirst") {
    auto                                  *physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    auto                                  *firstBody  = world->newBody("dynamic", 0.f, 0.f, 0.f);
    auto                                  *secondBody = world->newBody("dynamic", 1.f, 0.f, 0.f);
    auto                                   firstLink  = eve::physics::PhysicsLink::fromBody(*firstBody);
    auto                                   secondLink = eve::physics::PhysicsLink::fromBody(*secondBody);
    REQUIRE(firstLink.ok());
    REQUIRE(secondLink.ok());
    eve::action::ActionStateWindowBinding binding;
    binding.kind     = eve::action::ActionStateWindowKind::CollisionIgnore;
    binding.resource = "lifetime";
    eve::action::ActionTimelineEvent event{eve::action::ActionTimelineEventKind::StateEnter,
                                           id("collision-track:ignore"),
                                           id("collision-window:lifetime"),
                                           id("collision:ignore-window"),
                                           eve::Duration::zero(),
                                           {}};
    eve::action::ActionNotifyContext context;
    context.executionId = eve::action::ActionExecutionId(703);
    context.source      = ecs::EntityHandle{nullptr, typeid(void), 13, 1};

    {
        eve::physics::action_adapter::ActionCollisionIgnoreState ignores(
            *world,
            [&](ecs::EntityHandle, std::string_view) -> eve::Result<eve::physics::action_adapter::ActionCollisionPair> {
                return eve::Result<eve::physics::action_adapter::ActionCollisionPair>::success(
                    {firstLink.value(), secondLink.value()});
            });
        REQUIRE(ignores.enter(binding, event, context).ok());
        CHECK(!world->isBodyPairCollisionEnabled(firstBody, secondBody));
    }
    CHECK(world->isBodyPairCollisionEnabled(firstBody, secondBody));

    auto ignores = std::make_unique<eve::physics::action_adapter::ActionCollisionIgnoreState>(
        *world,
        [&](ecs::EntityHandle, std::string_view) -> eve::Result<eve::physics::action_adapter::ActionCollisionPair> {
            return eve::Result<eve::physics::action_adapter::ActionCollisionPair>::success(
                {firstLink.value(), secondLink.value()});
        });
    REQUIRE(ignores->enter(binding, event, context).ok());
    world->destroy();
    REQUIRE(ignores->shutdown().ok());
    ignores.reset();
}
}  // namespace test_action_collision_ignore

// Contract section: box2d
#include "PrivateTestScope.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <vector>
#include "graphics/AmbientOcclusion.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "physics/Body.h"
#include "physics/Fixture.h"
#include "physics/World.h"
#include "window/Window.h"
// box2d.cpp
namespace test_box2d {
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
    auto                  *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 900.f, true));
    REQUIRE(world.get() != nullptr);
    CHECK(std::fabs(world->getGravityX()) < 0.01f);
    CHECK(std::fabs(world->getGravityY() - 900.f) < 0.01f);
    world->setGravity(10.f, 20.f);
    CHECK(std::fabs(world->getGravityX() - 10.f) < 0.01f);
    CHECK(std::fabs(world->getGravityY() - 20.f) < 0.01f);
}

TEST_CASE("box2d.body.fallsUnderGravity") {
    auto                  *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 900.f, true));
    Body                  *ground = world->newBody("static", 400.f, 500.f);
    ground->newRectangleFixture(800.f, 40.f, 0.f, 0.5f, 0.f);

    Body *box = world->newBody("dynamic", 400.f, 100.f);
    box->newRectangleFixture(40.f, 40.f, 1.f, 0.3f, 0.1f);
    float y0 = box->getY();

    for (int i = 0; i < 60; ++i) world->update(1.f / 60.f);

    CHECK_GT(box->getY(), y0);
    CHECK_LT(box->getY(), 500.f);
}

TEST_CASE("box2d.body.staticDoesNotMove") {
    auto                  *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 900.f, true));
    Body                  *ground = world->newBody("static", 200.f, 300.f);
    ground->newRectangleFixture(100.f, 20.f, 0.f);
    float y0 = ground->getY();
    for (int i = 0; i < 30; ++i) world->update(1.f / 60.f);
    CHECK(std::fabs(ground->getY() - y0) < 0.01f);
    CHECK_EQ(ground->getType(), std::string("static"));
}

TEST_CASE("box2d.body.circleAndVelocity") {
    auto                  *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body                  *ball = world->newBody("dynamic", 0.f, 0.f);
    ball->newCircleFixture(16.f, 1.f, 0.2f, 0.8f);
    ball->setLinearVelocity(300.f, 0.f);

    for (int i = 0; i < 30; ++i) world->update(1.f / 60.f);

    CHECK_GT(ball->getX(), 100.f);
    CHECK(std::fabs(ball->getY()) < 5.f);
}

TEST_CASE("box2d.fixture.sensor") {
    auto                  *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body                  *b       = world->newBody("dynamic", 0.f, 0.f);
    Fixture               *fixture = b->newRectangleFixture(10.f, 10.f);
    CHECK(!fixture->isSensor());
    fixture->setSensor(true);
    CHECK(fixture->isSensor());
    fixture->setFriction(0.7f);
    CHECK(std::fabs(fixture->getFriction() - 0.7f) < 0.001f);
}

TEST_CASE("box2d.fixture.tagsFiltersAndBodyProperties") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World>   world(mod->newWorld(0.f, 0.f, true));
    std::unique_ptr<Body>    body(world->newBody("dynamic", 30.f, 60.f));
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
    auto                  *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body                  *wall        = world->newBody("static", 300.f, 100.f);
    Fixture               *wallFixture = wall->newRectangleFixture(20.f, 160.f, 0.f, 0.2f, 0.f);
    wallFixture->setTag("terrain");
    Body    *crate        = world->newBody("dynamic", 80.f, 100.f);
    Fixture *crateFixture = crate->newRectangleFixture(40.f, 40.f, 1.f, 0.2f, 0.f);
    crateFixture->setTag("prop.crate");
    crate->setBullet(true);
    crate->setLinearVelocity(900.f, 0.f);

    for (int i = 0; i < 30 && world->getImpactCount() == 0; ++i) world->update(1.f / 60.f);

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
    auto                  *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body                  *crate = world->newBody("dynamic", 0.f, 0.f);
    crate->newRectangleFixture(40.f, 40.f, 1.f, 0.45f, 0.05f);
    Body *rock = world->newBody("dynamic", 100.f, 0.f);
    rock->newRectangleFixture(40.f, 40.f, 8.f, 0.85f, 0.05f);

    crate->applyLinearImpulse(720.f, -105.f);
    rock->applyLinearImpulse(720.f, -105.f);

    CHECK_GT(rock->getMass(), crate->getMass() * 7.f);
    CHECK_GT(crate->getLinearSpeed(), rock->getLinearSpeed() * 7.f);
}

TEST_CASE("box2d.contacts.destroyFixturePurgesQueuedEvents") {
    auto                  *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body                  *wall        = world->newBody("static", 100.f, 100.f);
    Fixture               *wallFixture = wall->newRectangleFixture(40.f, 40.f);
    wallFixture->setTag("terrain");
    Body    *box        = world->newBody("dynamic", 100.f, 100.f);
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
    auto                  *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body                  *b = world->newBody("dynamic", 10.f, 20.f);
    b->newRectangleFixture(8.f, 8.f);
    int id = b->getId();
    CHECK_GT(id, 0);
    b->destroy();
    CHECK(std::fabs(b->getX()) < 0.001f);
    delete b;
}

TEST_CASE("box2d.query.rayCastAndAABB") {
    auto                  *mod = Physics::create();
    std::unique_ptr<World> world(mod->newWorld(0.f, 0.f, true));
    Body                  *a  = world->newBody("static", 100.f, 100.f);
    Fixture               *fa = a->newRectangleFixture(40.f, 40.f);
    Body                  *b  = world->newBody("static", 300.f, 100.f);
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
    auto  *mod   = Physics::create();
    World *world = mod->newWorld(0.f, 100.f, true);
    Body  *b     = world->newBody("dynamic", 50.f, 50.f);
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
            gfx->drawSolidRect(cx + std::cos(a) * rad - 2.f, cy + std::sin(a) * rad - 2.f, 4.f, 4.f, c);
        }
    }
}

static void drawOrientedBox(Graphics *gfx, float cx, float cy, float w, float h, float angle, const Color &c) {
    const float hw     = w * 0.5f;
    const float hh     = h * 0.5f;
    const float ca     = std::cos(angle);
    const float sa     = std::sin(angle);
    auto        corner = [&](float lx, float ly, float &ox, float &oy) {
        ox = cx + lx * ca - ly * sa;
        oy = cy + lx * sa + ly * ca;
    };
    float x0, y0, x1, y1, x2, y2, x3, y3;
    corner(-hw, -hh, x0, y0);
    corner(hw, -hh, x1, y1);
    corner(hw, hh, x2, y2);
    corner(-hw, hh, x3, y3);
    auto edge = [&](float ax, float ay, float bx, float by) {
        const float dx    = bx - ax;
        const float dy    = by - ay;
        const float len   = std::sqrt(dx * dx + dy * dy);
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
    s.width    = 640;
    s.height   = 480;
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
        bool  circle;
        Color color;
    };
    std::vector<Dyn> dyn;
    const Color      palette[] = {
        Color(0.95f, 0.45f, 0.35f, 1.f), Color(0.35f, 0.75f, 0.95f, 1.f), Color(0.45f, 0.9f, 0.5f, 1.f),
        Color(0.95f, 0.85f, 0.35f, 1.f), Color(0.8f, 0.5f, 0.95f, 1.f),
    };
    for (int i = 0; i < 8; ++i) {
        Body      *b      = world->newBody("dynamic", 220.f + float(i % 4) * 45.f, 40.f + float(i) * 28.f);
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
                drawOrientedBox(gfx, d.body->getX(), d.body->getY(), d.w, d.h, d.body->getAngle(), d.color);
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
}  // namespace test_box2d

// Contract section: box3d
#include "PrivateTestScope.h"

#include <box3d/box3d.h>
#include <limits>
#include "physics/DistanceField3D.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/TerrainLoadGravity.h"
#include "physics/box3d/Box3DBody.h"
// box3d.cpp
namespace test_box3d {
using namespace eve::physics;

TEST_CASE("box3d.world.createAndGravity") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -9.8f, 0.f, true));
    REQUIRE(world.get() != nullptr);
    CHECK(std::fabs(world->getGravityX()) < 0.01f);
    CHECK(std::fabs(world->getGravityY() + 9.8f) < 0.01f);
    CHECK(std::fabs(world->getGravityZ()) < 0.01f);
    world->setGravity(1.f, -10.f, 2.f);
    CHECK(std::fabs(world->getGravityX() - 1.f) < 0.01f);
    CHECK(std::fabs(world->getGravityY() + 10.f) < 0.01f);
    CHECK(std::fabs(world->getGravityZ() - 2.f) < 0.01f);
}

TEST_CASE("box3d.body.fallsUnderGravity") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, true));
    Body3D                  *ground = world->newBody("static", 0.f, -1.f, 0.f);
    ground->newBoxShape(20.f, 2.f, 20.f, 0.f, 0.5f, 0.f);

    Body3D *box = world->newBody("dynamic", 0.f, 4.f, 0.f);
    box->newBoxShape(1.f, 1.f, 1.f, 1.f, 0.3f, 0.1f);
    float y0 = box->getY();

    for (int i = 0; i < 90; ++i) world->update(1.f / 60.f);

    CHECK_LT(box->getY(), y0);
    CHECK_GT(box->getY(), 0.f);
}

TEST_CASE("box3d.terrainLoadGravity.waitsThenRestoresOriginalScale") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    Body3D                  *body = world->newBody("dynamic", 0.f, 10.f, 0.f);
    body->newSphereShape(0.5f, 1.f, 0.5f, 0.f);
    body->setGravityScale(0.35f);
    TerrainLoadGravityState state;
    REQUIRE(static_cast<bool>(beginTerrainLoadGravity(&state, body, true)));
    CHECK(state.isMonitoring());
    CHECK_EQ(body->getGravityScale(), 0.f);

    auto waiting = advanceTerrainLoadGravity(&state, body, false, 0.5f, 2.f);
    REQUIRE(static_cast<bool>(waiting));
    CHECK(!waiting.value());
    CHECK(!state.isActivationScheduled());
    auto scheduled = advanceTerrainLoadGravity(&state, body, true, 0.75f, 2.f);
    REQUIRE(static_cast<bool>(scheduled));
    CHECK(!scheduled.value());
    CHECK(state.isActivationScheduled());
    CHECK_EQ(state.getDelayElapsed(), 0.75f);
    CHECK(!advanceTerrainLoadGravity(&state, body, false, NAN, 2.f));
    CHECK_EQ(state.getDelayElapsed(), 0.75f);
    auto activated = advanceTerrainLoadGravity(&state, body, false, 1.25f, 2.f);
    REQUIRE(static_cast<bool>(activated));
    CHECK(activated.value());
    CHECK(state.isCompleted());
    CHECK(!state.isMonitoring());
    CHECK_EQ(body->getGravityScale(), 0.35f);
    auto alreadyDone = advanceTerrainLoadGravity(&state, body, true, 1.f, 2.f);
    REQUIRE(static_cast<bool>(alreadyDone));
    CHECK(!alreadyDone.value());

    Body3D                 *passthrough = world->newBody("dynamic", 0.f, 3.f, 0.f);
    TerrainLoadGravityState absent;
    REQUIRE(static_cast<bool>(beginTerrainLoadGravity(&absent, passthrough, false)));
    CHECK(absent.isCompleted());
    CHECK_EQ(passthrough->getGravityScale(), 1.f);
    CHECK(!beginTerrainLoadGravity(&absent, passthrough, false));
}

TEST_CASE("box3d.body.staticDoesNotMove") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, true));
    Body3D                  *ground = world->newBody("static", 0.f, 0.f, 0.f);
    ground->newBoxShape(4.f, 1.f, 4.f, 0.f);
    float y0 = ground->getY();
    for (int i = 0; i < 30; ++i) world->update(1.f / 60.f);
    CHECK(std::fabs(ground->getY() - y0) < 0.01f);
    CHECK_EQ(ground->getType(), std::string("static"));
}

TEST_CASE("box3d.body.sphereAndVelocity") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D                  *ball = world->newBody("dynamic", 0.f, 0.f, 0.f);
    ball->newSphereShape(0.5f, 1.f, 0.2f, 0.8f);
    ball->setLinearVelocity(5.f, 0.f, 0.f);

    for (int i = 0; i < 30; ++i) world->update(1.f / 60.f);

    CHECK_GT(ball->getX(), 1.f);
    CHECK(std::fabs(ball->getY()) < 0.1f);
    CHECK(std::fabs(ball->getZ()) < 0.1f);
}

TEST_CASE("box3d.body.dampingGravitySleepMassAndCenters") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    Body3D                  *body = world->newBody("dynamic", 3.f, 4.f, 5.f);
    body->newBoxShape(2.f, 2.f, 2.f, 2.f);

    CHECK(std::fabs(body->getMass() - 16.f) < 0.05f);
    CHECK(std::fabs(body->getLocalCenterX()) < 0.001f);
    CHECK(std::fabs(body->getLocalCenterY()) < 0.001f);
    CHECK(std::fabs(body->getLocalCenterZ()) < 0.001f);
    CHECK(std::fabs(body->getWorldCenterX() - 3.f) < 0.001f);
    CHECK(std::fabs(body->getWorldCenterY() - 4.f) < 0.001f);
    CHECK(std::fabs(body->getWorldCenterZ() - 5.f) < 0.001f);

    body->setLinearDamping(3.f);
    body->setAngularDamping(4.f);
    body->setGravityScale(-0.5f);
    body->setSleepEnabled(false);
    body->setSleepThreshold(0.25f);
    CHECK(std::fabs(body->getLinearDamping() - 3.f) < 0.001f);
    CHECK(std::fabs(body->getAngularDamping() - 4.f) < 0.001f);
    CHECK(std::fabs(body->getGravityScale() + 0.5f) < 0.001f);
    CHECK(!body->isSleepEnabled());
    CHECK(std::fabs(body->getSleepThreshold() - 0.25f) < 0.001f);

    body->setLinearVelocity(6.f, 0.f, 0.f);
    for (int i = 0; i < 15; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_LT(body->getLinearVelocityX(), 6.f);
    CHECK_GT(body->getLinearVelocityY(), 0.f);

    CHECK_THROWS((body->setLinearDamping(-1.f), false));
    CHECK_THROWS((body->setAngularDamping(std::numeric_limits<float>::infinity()), false));
    CHECK_THROWS((body->setGravityScale(std::numeric_limits<float>::quiet_NaN()), false));
    CHECK_THROWS((body->setSleepThreshold(-0.1f), false));
}

TEST_CASE("box3d.body.customMassCenterAndInertia") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *body = world->newBody("dynamic", 10.f, 0.f, 0.f);
    body->newBoxShape(2.f, 2.f, 2.f, 2.f);
    const float automaticMass = body->getMass();

    body->setMassProperties(4.f, 0.25f, -0.5f, 0.75f, 2.f, 3.f, 4.f, 0.1f, 0.2f, 0.3f);
    CHECK(std::fabs(body->getMass() - 4.f) < 1e-6f);
    CHECK(std::fabs(body->getLocalCenterX() - 0.25f) < 1e-6f);
    CHECK(std::fabs(body->getLocalCenterY() + 0.5f) < 1e-6f);
    CHECK(std::fabs(body->getLocalCenterZ() - 0.75f) < 1e-6f);
    CHECK(std::fabs(body->getWorldCenterX() - 10.25f) < 1e-6f);
    CHECK(std::fabs(body->getInertiaXX() - 2.f) < 1e-6f);
    CHECK(std::fabs(body->getInertiaYY() - 3.f) < 1e-6f);
    CHECK(std::fabs(body->getInertiaZZ() - 4.f) < 1e-6f);
    CHECK(std::fabs(body->getInertiaXY() - 0.1f) < 1e-6f);
    CHECK(std::fabs(body->getInertiaXZ() - 0.2f) < 1e-6f);
    CHECK(std::fabs(body->getInertiaYZ() - 0.3f) < 1e-6f);

    body->applyLinearImpulse(8.f, 0.f, 0.f);
    CHECK(std::fabs(body->getLinearVelocityX() - 2.f) < 1e-6f);

    body->resetMassProperties();
    CHECK(std::fabs(body->getMass() - automaticMass) < 0.05f);
    CHECK(std::fabs(body->getLocalCenterX()) < 1e-6f);
    CHECK(std::fabs(body->getLocalCenterY()) < 1e-6f);
    CHECK(std::fabs(body->getLocalCenterZ()) < 1e-6f);

    CHECK_THROWS((body->setMassProperties(0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f), false));
    CHECK_THROWS((body->setMassProperties(1.f, 0.f, 0.f, 0.f, 1.f, 1.f, -1.f), false));
    CHECK_THROWS((body->setMassProperties(1.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 2.f), false));
    CHECK_THROWS(
        (body->setMassProperties(1.f, std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f, 1.f, 1.f, 1.f), false));

    Body3D *ground = world->newBody("static", 0.f, 0.f, 0.f);
    CHECK_THROWS((ground->setMassProperties(1.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f), false));
}

TEST_CASE("box3d.body.spaceTransformsPointVelocityAndLocalImpulses") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *body = world->newBody("dynamic", 3.f, 4.f, 5.f);
    body->newSphereShape(0.5f, 1.f);
    const float halfSqrt2 = std::sqrt(0.5f);
    body->setRotation(0.f, 0.f, halfSqrt2, halfSqrt2);  // local +X becomes world +Y

    const std::vector<float> worldPoint = body->localToWorldPoint(1.f, 2.f, 3.f);
    REQUIRE(worldPoint.size() == 3);
    CHECK(std::fabs(worldPoint[0] - 1.f) < 1e-5f);
    CHECK(std::fabs(worldPoint[1] - 5.f) < 1e-5f);
    CHECK(std::fabs(worldPoint[2] - 8.f) < 1e-5f);
    const std::vector<float> localPoint = body->worldToLocalPoint(worldPoint[0], worldPoint[1], worldPoint[2]);
    CHECK(std::fabs(localPoint[0] - 1.f) < 1e-5f);
    CHECK(std::fabs(localPoint[1] - 2.f) < 1e-5f);
    CHECK(std::fabs(localPoint[2] - 3.f) < 1e-5f);

    const std::vector<float> worldVector = body->localToWorldVector(2.f, 0.f, 0.f);
    CHECK(std::fabs(worldVector[0]) < 1e-5f);
    CHECK(std::fabs(worldVector[1] - 2.f) < 1e-5f);
    CHECK(std::fabs(worldVector[2]) < 1e-5f);
    const std::vector<float> localVector = body->worldToLocalVector(worldVector[0], worldVector[1], worldVector[2]);
    CHECK(std::fabs(localVector[0] - 2.f) < 1e-5f);
    CHECK(std::fabs(localVector[1]) < 1e-5f);

    body->setLinearVelocity(1.f, 2.f, 3.f);
    body->setAngularVelocity(0.f, 0.f, 2.f);
    // Local (2,0,0) is world offset (0,2,0): v + omega x r = (-3,2,3).
    const std::vector<float> localPointVelocity = body->getLocalPointVelocity(2.f, 0.f, 0.f);
    const std::vector<float> worldPointVelocity = body->getWorldPointVelocity(3.f, 6.f, 5.f);
    for (const std::vector<float> *velocity : {&localPointVelocity, &worldPointVelocity}) {
        CHECK(std::fabs((*velocity)[0] + 3.f) < 1e-5f);
        CHECK(std::fabs((*velocity)[1] - 2.f) < 1e-5f);
        CHECK(std::fabs((*velocity)[2] - 3.f) < 1e-5f);
    }

    body->setLinearVelocity(0.f, 0.f, 0.f);
    body->setAngularVelocity(0.f, 0.f, 0.f);
    body->setMassProperties(2.f, 0.f, 0.f, 0.f, 2.f, 2.f, 2.f);
    body->applyLocalLinearImpulseToCenter(4.f, 0.f, 0.f);
    CHECK(std::fabs(body->getLinearVelocityX()) < 1e-5f);
    CHECK(std::fabs(body->getLinearVelocityY() - 2.f) < 1e-5f);
    body->applyLocalAngularImpulse(4.f, 0.f, 0.f);
    CHECK(std::fabs(body->getAngularVelocityX()) < 1e-5f);
    CHECK(std::fabs(body->getAngularVelocityY() - 2.f) < 1e-5f);

    CHECK_THROWS((body->localToWorldPoint(std::numeric_limits<float>::infinity(), 0.f, 0.f), false));
    CHECK_THROWS((body->applyLocalForceToCenter(0.f, std::numeric_limits<float>::quiet_NaN(), 0.f), false));
}

TEST_CASE("box3d.body.perAxisLocksAndAdvancedForces") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *body = world->newBody("dynamic", 0.f, 0.f, 0.f);
    body->newBoxShape(1.f, 1.f, 1.f, 1.f);
    body->setMotionLocks(true, false, true, true, true, false);
    CHECK(body->isLinearXLocked());
    CHECK(!body->isLinearYLocked());
    CHECK(body->isLinearZLocked());
    CHECK(body->isAngularXLocked());
    CHECK(body->isAngularYLocked());
    CHECK(!body->isAngularZLocked());

    body->setLinearVelocity(5.f, 2.f, 4.f);
    body->applyLinearImpulseAt(0.f, 1.f, 0.f, 0.5f, 0.f, 0.f);
    body->applyTorque(0.f, 0.f, 4.f);
    for (int i = 0; i < 15; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK(std::fabs(body->getX()) < 0.001f);
    CHECK_GT(body->getY(), 0.1f);
    CHECK(std::fabs(body->getZ()) < 0.001f);
    CHECK(std::fabs(body->getAngularVelocityX()) < 0.001f);
    CHECK(std::fabs(body->getAngularVelocityY()) < 0.001f);
    CHECK_GT(std::fabs(body->getAngularVelocityZ()), 0.1f);

    body->setFixedRotation(true);
    CHECK(body->isAngularXLocked());
    CHECK(body->isAngularYLocked());
    CHECK(body->isAngularZLocked());
    CHECK(body->isFixedRotation());
}

TEST_CASE("box3d.body.kinematicTargetTransform") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *body = world->newBody("kinematic", 0.f, 0.f, 0.f);
    body->newBoxShape(1.f, 1.f, 1.f);
    body->setTargetTransform(2.f, 1.f, -1.f, 0.f, 0.f, 0.f, 2.f, 0.5f);
    for (int i = 0; i < 30; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK(std::fabs(body->getX() - 2.f) < 0.05f);
    CHECK(std::fabs(body->getY() - 1.f) < 0.05f);
    CHECK(std::fabs(body->getZ() + 1.f) < 0.05f);
    CHECK(std::fabs(body->getRotW() - 1.f) < 0.001f);
    CHECK_THROWS((body->setTargetTransform(0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f), false));
    CHECK_THROWS((body->setTargetTransform(0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f), false));
}

TEST_CASE("box3d.joint.distanceMaintainsLengthAndSupportsSpringMotorConfiguration") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    Body3D                  *ground = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D                  *bob    = world->newBody("dynamic", 0.f, 1.f, 0.f);
    bob->newSphereShape(0.25f, 2.f);
    Joint3D *joint = world->newDistanceJoint(ground, bob, 0.f, 3.f, 0.f, 0.f, 1.f, 0.f, 2.f, false);
    REQUIRE(joint != nullptr);
    CHECK(joint->isValid());
    CHECK_EQ(joint->getKind(), std::string("distance"));
    CHECK_EQ(joint->getBodyAId(), ground->getId());
    CHECK_EQ(joint->getBodyBId(), bob->getId());
    CHECK(std::fabs(joint->getDistanceLength() - 2.f) < 0.001f);
    CHECK(!joint->getCollideConnected());

    bob->applyLinearImpulse(1.f, 0.f, 0.f);
    for (int i = 0; i < 120; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK(std::fabs(joint->getDistanceCurrentLength() - 2.f) < 0.03f);
    CHECK_GT(std::fabs(joint->getConstraintForceY()), 0.1f);

    joint->setDistanceSpring(true, 4.f, 0.7f);
    joint->setDistanceLimits(true, 1.5f, 2.5f);
    joint->setDistanceMotor(true, 0.25f, 20.f);
    joint->setDistanceLength(2.1f);
    CHECK(std::fabs(joint->getDistanceLength() - 2.1f) < 0.001f);
    CHECK_THROWS((joint->setDistanceLimits(true, 3.f, 2.f), false));
    CHECK_THROWS((joint->setRevoluteMotor(true, 1.f, 2.f), false));
}

TEST_CASE("box3d.joint.revoluteMotorHonorsLimitAndBodyDestructionInvalidatesWrapper") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *frame = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D                  *door  = world->newBody("dynamic", 1.f, 0.f, 0.f);
    door->newBoxShape(2.f, 0.25f, 0.25f, 1.f);
    Joint3D *hinge = world->newRevoluteJoint(frame, door, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, false);
    REQUIRE(hinge != nullptr);
    CHECK_EQ(hinge->getKind(), std::string("revolute"));
    hinge->setRevoluteLimits(true, -0.5f, 0.5f);
    hinge->setRevoluteSpring(false, 0.f, 0.f, 0.f);
    hinge->setRevoluteMotor(true, 3.f, 100.f);
    for (int i = 0; i < 120; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_GT(hinge->getRevoluteAngle(), 0.35f);
    CHECK_LT(hinge->getRevoluteAngle(), 0.55f);
    CHECK_GT(std::fabs(hinge->getRevoluteMotorTorque()), 0.01f);
    CHECK_THROWS((hinge->setRevoluteLimits(true, 1.f, -1.f), false));
    CHECK_THROWS((hinge->setDistanceLength(1.f), false));

    door->destroy();
    CHECK(!hinge->isValid());
    CHECK_EQ(hinge->getBodyAId(), -1);
    CHECK_EQ(hinge->getBodyBId(), -1);
    hinge->destroy();
}

TEST_CASE("box3d.joint.validatesBodiesAxisAndWorldOwnership") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> worldA(mod->newWorld3D(0.f, 0.f, 0.f, false));
    std::unique_ptr<World3D> worldB(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *a = worldA->newBody("static", 0.f, 0.f, 0.f);
    Body3D                  *b = worldA->newBody("dynamic", 1.f, 0.f, 0.f);
    b->newSphereShape(0.25f);
    Body3D *foreign = worldB->newBody("dynamic", 0.f, 0.f, 0.f);
    foreign->newSphereShape(0.25f);
    CHECK_THROWS((worldA->newDistanceJoint(a, foreign, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, false), false));
    CHECK_THROWS((worldA->newRevoluteJoint(a, b, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, false), false));
    CHECK_THROWS((worldA->newDistanceJoint(a, a, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, false), false));
}

TEST_CASE("box3d.joint.prismaticMotorMovesOnlyAlongAxisAndHonorsLimit") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *rail     = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D                  *carriage = world->newBody("dynamic", 0.f, 0.f, 0.f);
    carriage->newBoxShape(0.5f, 0.5f, 0.5f);
    Joint3D *slider = world->newPrismaticJoint(rail, carriage, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, false);
    REQUIRE(slider != nullptr);
    CHECK_EQ(slider->getKind(), std::string("prismatic"));
    slider->setPrismaticLimits(true, 0.f, 1.f);
    slider->setPrismaticSpring(false, 0.f, 0.f, 0.f);
    slider->setPrismaticMotor(true, 3.f, 100.f);
    for (int i = 0; i < 120; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_GT(slider->getPrismaticTranslation(), 0.85f);
    CHECK_LT(slider->getPrismaticTranslation(), 1.05f);
    CHECK(std::fabs(carriage->getY()) < 0.01f);
    CHECK(std::fabs(carriage->getZ()) < 0.01f);
    CHECK_GT(std::fabs(slider->getPrismaticMotorForce()), 0.01f);
    CHECK_THROWS((slider->setPrismaticLimits(true, 2.f, 1.f), false));
    CHECK_THROWS((slider->setSphericalConeLimit(true, 0.5f), false));
}

TEST_CASE("box3d.joint.sphericalMotorRespectsConeAndKeepsSharedAnchor") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    Body3D                  *socket = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D                  *limb   = world->newBody("dynamic", 0.f, -1.f, 0.f);
    limb->newBoxShape(0.25f, 2.f, 0.25f);
    Joint3D *ball = world->newSphericalJoint(socket, limb, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, false);
    REQUIRE(ball != nullptr);
    CHECK_EQ(ball->getKind(), std::string("spherical"));
    ball->setSphericalConeLimit(true, 0.4f);
    ball->setSphericalTwistLimits(true, -0.3f, 0.3f);
    ball->setSphericalMotor(true, 4.f, 0.f, 0.f, 100.f);
    for (int i = 0; i < 120; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_GT(ball->getSphericalConeAngle(), 0.25f);
    CHECK_LT(ball->getSphericalConeAngle(), 0.45f);
    CHECK_LT(std::fabs(ball->getSphericalTwistAngle()), 0.35f);
    const b3Pos anchor = b3Body_GetWorldPoint(eve::physics::box3d::Access::bodyId(*limb), b3Vec3{0.f, 1.f, 0.f});
    CHECK(std::fabs(anchor.x) < 0.03f);
    CHECK(std::fabs(anchor.y) < 0.03f);
    CHECK(std::fabs(anchor.z) < 0.03f);
    CHECK_THROWS((ball->setSphericalConeLimit(true, 4.f), false));
    CHECK_THROWS((ball->setSphericalTwistLimits(true, 0.5f, -0.5f), false));
}

TEST_CASE("box3d.joint.stressEventsExposeStableIdentityAndSolverFeedback") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    Body3D                  *support = world->newBody("static", 0.f, 2.f, 0.f);
    Body3D                  *load    = world->newBody("dynamic", 0.f, 0.f, 0.f);
    load->newSphereShape(0.5f, 20.f);
    Joint3D *joint = world->newDistanceJoint(support, load, 0.f, 2.f, 0.f, 0.f, 0.f, 0.f, 2.f, false);
    joint->setForceThreshold(0.1f);
    joint->setTorqueThreshold(1000000.f);
    CHECK(std::fabs(joint->getForceThreshold() - 0.1f) < 0.001f);
    CHECK(std::fabs(joint->getTorqueThreshold() - 1000000.f) < 1.f);

    world->updateFull(1.f / 60.f, 4);
    REQUIRE_EQ(world->getJointStressCount(), 1);
    CHECK_EQ(world->getJointStressJointId(0), joint->getId());
    CHECK_EQ(world->getJointStressBodyAId(0), support->getId());
    CHECK_EQ(world->getJointStressBodyBId(0), load->getId());
    CHECK_EQ(world->getJointStressKind(0), 0);
    CHECK_GT(std::fabs(world->getJointStressForceY(0)), 1.f);
    CHECK(std::isfinite(world->getJointStressTorqueX(0)));
    CHECK_THROWS((world->getJointStressJointId(1), false));

    joint->setForceThreshold(1000000.f);
    world->updateFull(1.f / 60.f, 4);
    CHECK_EQ(world->getJointStressCount(), 0);
    CHECK_THROWS((joint->setForceThreshold(-1.f), false));
    CHECK_THROWS((joint->setTorqueThreshold(std::numeric_limits<float>::quiet_NaN()), false));
}

TEST_CASE("box3d.joint.wheelCombinesSuspensionSpinAndSteering") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *chassis   = world->newBody("static", 0.f, 0.f, 0.f);
    Body3D                  *wheelBody = world->newBody("dynamic", 0.f, 0.f, 0.f);
    wheelBody->newSphereShape(0.5f, 1.f);
    Joint3D *wheel = world->newWheelJoint(chassis, wheelBody, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, false);
    REQUIRE(wheel != nullptr);
    CHECK_EQ(wheel->getKind(), std::string("wheel"));
    wheel->setWheelSuspension(true, 5.f, 0.7f);
    wheel->setWheelSuspensionLimits(true, -0.1f, 0.1f);
    wheel->setWheelSpinMotor(true, 5.f, 100.f);
    wheel->setWheelSteering(true, 0.3f, 5.f, 0.8f, 100.f);
    wheel->setWheelSteeringLimits(true, -0.4f, 0.4f);
    // Torque reports the last substep: sample the initial acceleration directly.
    world->updateFull(1.f / 60.f, 1);
    CHECK_GT(std::fabs(wheel->getWheelSpinTorque()), 0.001f);
    for (int i = 0; i < 120; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_GT(wheel->getWheelSpinSpeed(), 4.f);
    CHECK_GT(wheel->getWheelSteeringAngle(), 0.2f);
    CHECK_LT(wheel->getWheelSteeringAngle(), 0.4f);
    CHECK(std::isfinite(wheel->getWheelSteeringTorque()));
    CHECK(std::fabs(wheelBody->getX()) < 0.02f);
    CHECK(std::fabs(wheelBody->getY()) < 0.12f);
    CHECK(std::fabs(wheelBody->getZ()) < 0.02f);
    CHECK_THROWS((wheel->setWheelSuspensionLimits(true, 1.f, -1.f), false));
    CHECK_THROWS((wheel->setWheelSpinMotor(true, 1.f, -1.f), false));

    CHECK_THROWS((world->newWheelJoint(chassis, wheelBody, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 2.f, 0.f, false), false));
}

TEST_CASE("box3d.shape.sensorAndMaterial") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D                  *b     = world->newBody("dynamic", 0.f, 0.f, 0.f);
    Shape3D                 *shape = b->newBoxShape(1.f, 1.f, 1.f);
    CHECK(!shape->isSensor());
    shape->setSensor(true);
    CHECK(shape->isSensor());
    shape->setFriction(0.7f);
    CHECK(std::fabs(shape->getFriction() - 0.7f) < 0.001f);
    shape->setRestitution(0.35f);
    shape->setRollingResistance(0.12f);
    shape->setTangentVelocity(1.f, 2.f, 3.f);
    shape->setMaterialId(-559038737);  // 0xDEADBEEF preserves all low bits.
    shape->setFrictionCombineMode("multiply");
    shape->setRestitutionCombineMode("minimum");
    shape->setDensity(2.f);
    CHECK(std::fabs(shape->getRestitution() - 0.35f) < 0.001f);
    CHECK(std::fabs(shape->getRollingResistance() - 0.12f) < 0.001f);
    CHECK(std::fabs(shape->getTangentVelocityX() - 1.f) < 0.001f);
    CHECK(std::fabs(shape->getTangentVelocityY() - 2.f) < 0.001f);
    CHECK(std::fabs(shape->getTangentVelocityZ() - 3.f) < 0.001f);
    CHECK_EQ(shape->getMaterialId(), -559038737);
    CHECK_EQ(shape->getFrictionCombineMode(), std::string("multiply"));
    CHECK_EQ(shape->getRestitutionCombineMode(), std::string("minimum"));
    shape->setSensor(false);
    CHECK(std::fabs(shape->getRollingResistance() - 0.12f) < 0.001f);
    CHECK(std::fabs(shape->getTangentVelocityZ() - 3.f) < 0.001f);
    CHECK_EQ(shape->getMaterialId(), -559038737);
    CHECK_EQ(shape->getFrictionCombineMode(), std::string("multiply"));
    CHECK_EQ(shape->getRestitutionCombineMode(), std::string("minimum"));
    shape->setMaterialId(1234);
    CHECK_EQ(shape->getMaterialId(), 1234);
    CHECK_EQ(shape->getFrictionCombineMode(), std::string("multiply"));
    CHECK_THROWS((shape->setFriction(-0.1f), false));
    CHECK_THROWS((shape->setRestitution(std::numeric_limits<float>::quiet_NaN()), false));
    CHECK_THROWS((shape->setRollingResistance(-0.1f), false));
    CHECK_THROWS((shape->setTangentVelocity(0.f, std::numeric_limits<float>::infinity(), 0.f), false));
    CHECK_THROWS((shape->setDensity(-1.f), false));
    CHECK_THROWS((shape->setFrictionCombineMode("median"), false));
    CHECK_THROWS((shape->setRestitutionCombineMode("add"), false));
}

TEST_CASE("box3d.material.tangentVelocityDrivesConveyorContact") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    Shape3D                 *belt = world->newBody("static", 0.f, -0.25f, 0.f)->newBoxShape(20.f, 0.5f, 4.f);
    belt->setFriction(1.f);
    belt->setTangentVelocity(3.f, 0.f, 0.f);
    Body3D *cargo = world->newBody("dynamic", 0.f, 0.6f, 0.f);
    cargo->newBoxShape(0.8f, 0.8f, 0.8f, 1.f, 1.f);
    for (int i = 0; i < 180; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_GT(std::fabs(cargo->getX()), 1.f);
    CHECK_LT(std::fabs(cargo->getY() - 0.4f), 0.08f);
}

TEST_CASE("box3d.material.restitutionCombineModeChangesCollisionResponse") {
    auto reboundSpeed = [](const std::string &mode) {
        auto                    *mod = Physics::create();
        std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
        Shape3D                 *floor = world->newBody("static", 0.f, -0.25f, 0.f)->newBoxShape(10.f, 0.5f, 10.f);
        floor->setRestitution(0.2f);
        floor->setRestitutionCombineMode(mode);
        Body3D  *ball      = world->newBody("dynamic", 0.f, 2.f, 0.f);
        Shape3D *ballShape = ball->newSphereShape(0.25f);
        ballShape->setRestitution(0.8f);
        float maximumUpwardSpeed = 0.f;
        for (int i = 0; i < 120; ++i) {
            world->updateFull(1.f / 120.f, 2);
            if (i > 30) maximumUpwardSpeed = std::max(maximumUpwardSpeed, ball->getLinearVelocityY());
        }
        return maximumUpwardSpeed;
    };

    const float minimumBounce = reboundSpeed("minimum");
    const float maximumBounce = reboundSpeed("maximum");
    CHECK_GT(minimumBounce, 0.5f);
    CHECK_GT(maximumBounce, minimumBounce + 2.f);
}

TEST_CASE("box3d.world.solverAndContinuousCollisionTuning") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));

    CHECK(world->isContinuousCollisionEnabled());
    world->setContinuousCollisionEnabled(false);
    CHECK(!world->isContinuousCollisionEnabled());
    world->setContinuousCollisionEnabled(true);

    world->setRestitutionThreshold(0.75f);
    CHECK(std::fabs(world->getRestitutionThreshold() - 0.75f) < 0.001f);
    world->setContactTuning(42.f, 0.85f, 2.5f);
    CHECK(std::fabs(world->getContactHertz() - 42.f) < 0.001f);
    CHECK(std::fabs(world->getContactDampingRatio() - 0.85f) < 0.001f);
    CHECK(std::fabs(world->getContactPushOutSpeed() - 2.5f) < 0.001f);
    world->setContactRecycleDistance(0.02f);
    CHECK(std::fabs(world->getContactRecycleDistance() - 0.02f) < 0.001f);
    world->setMaximumLinearSpeed(12.f);
    CHECK(std::fabs(world->getMaximumLinearSpeed() - 12.f) < 0.001f);

    CHECK(world->isWarmStartingEnabled());
    world->setWarmStartingEnabled(false);
    CHECK(!world->isWarmStartingEnabled());
    world->setWarmStartingEnabled(true);

    Body3D *fast = world->newBody("dynamic", 0.f, 0.f, 0.f);
    fast->newSphereShape(0.1f);
    fast->setLinearVelocity(100.f, 0.f, 0.f);
    world->updateFull(1.f / 60.f, 1);
    CHECK_LE(fast->getLinearVelocityX(), 12.01f);

    CHECK_THROWS((world->setRestitutionThreshold(-0.01f), false));
    CHECK_THROWS((world->setContactTuning(0.f, 1.f, 1.f), false));
    CHECK_THROWS((world->setContactTuning(30.f, -0.1f, 1.f), false));
    CHECK_THROWS((world->setContactTuning(30.f, 1.f, std::numeric_limits<float>::infinity()), false));
    CHECK_THROWS((world->setContactRecycleDistance(-0.01f), false));
    CHECK_THROWS((world->setMaximumLinearSpeed(0.f), false));
}

TEST_CASE("box3d.contact.runtimeBodyAndShapePairOverridesRefreshImmediately") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *wallBody = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D                 *nearWall = wallBody->newBoxShape(0.2f, 4.f, 4.f);
    Shape3D                 *farWall  = wallBody->newBoxShape(0.2f, 4.f, 4.f);
    farWall->setLocalPosition(2.f, 0.f, 0.f);
    Body3D  *mover = world->newBody("dynamic", -2.f, 0.f, 0.f);
    Shape3D *ball  = mover->newSphereShape(0.25f);
    mover->setBullet(true);

    CHECK(world->isBodyPairCollisionEnabled(wallBody, mover));
    world->setBodyPairCollisionEnabled(wallBody, mover, false);
    CHECK(!world->isBodyPairCollisionEnabled(mover, wallBody));
    mover->setLinearVelocity(5.f, 0.f, 0.f);
    for (int i = 0; i < 60; ++i) world->updateFull(1.f / 60.f, 2);
    CHECK_GT(mover->getX(), 2.5f);

    mover->setPosition(-2.f, 0.f, 0.f);
    mover->setLinearVelocity(5.f, 0.f, 0.f);
    world->setBodyPairCollisionEnabled(wallBody, mover, true);
    CHECK(world->isBodyPairCollisionEnabled(wallBody, mover));
    for (int i = 0; i < 60; ++i) world->updateFull(1.f / 60.f, 2);
    CHECK_LT(mover->getX(), -0.25f);

    mover->setPosition(-2.f, 0.f, 0.f);
    mover->setLinearVelocity(5.f, 0.f, 0.f);
    world->setShapePairCollisionEnabled(nearWall, ball, false);
    CHECK(!world->isShapePairCollisionEnabled(ball, nearWall));
    CHECK(world->isShapePairCollisionEnabled(ball, farWall));
    for (int i = 0; i < 90; ++i) world->updateFull(1.f / 60.f, 2);
    CHECK_GT(mover->getX(), 0.5f);
    CHECK_LT(mover->getX(), 1.75f);

    world->setShapePairCollisionEnabled(nearWall, ball, true);
    CHECK(world->isShapePairCollisionEnabled(nearWall, ball));
    CHECK_THROWS((world->setBodyPairCollisionEnabled(mover, mover, false), false));
    CHECK_THROWS((world->setShapePairCollisionEnabled(ball, ball, false), false));
}

TEST_CASE("box3d.world.explosionUsesGeometryFalloffMaskAndReportsVelocityDelta") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *nearBody = world->newBody("dynamic", 1.f, 0.f, 0.f);
    nearBody->newSphereShape(0.25f)->setCategoryBits(1);
    Body3D *farBody = world->newBody("dynamic", 3.5f, 0.f, 0.f);
    farBody->newSphereShape(0.25f)->setCategoryBits(1);
    Body3D *filteredBody = world->newBody("dynamic", 1.f, 0.f, 2.f);
    filteredBody->newSphereShape(0.25f)->setCategoryBits(2);
    Body3D *staticBody = world->newBody("static", 1.f, 0.f, -2.f);
    staticBody->newSphereShape(0.25f)->setCategoryBits(1);

    const int repelled = world->explode(0.f, 0.f, 0.f, 1.f, 4.f, 10.f, 1);
    CHECK_EQ(repelled, 2);
    CHECK_EQ(world->getExplosionResultCount(), 2);
    CHECK_EQ(world->getExplosionResultBodyId(0), nearBody->getId());
    CHECK_EQ(world->getExplosionResultBodyId(1), farBody->getId());
    CHECK_GT(nearBody->getLinearVelocityX(), farBody->getLinearVelocityX());
    CHECK_GT(farBody->getLinearVelocityX(), 0.f);
    CHECK(std::fabs(filteredBody->getLinearVelocityX()) < 1e-6f);
    CHECK(std::fabs(world->getExplosionResultDeltaVelocityX(0) - nearBody->getLinearVelocityX()) < 1e-5f);
    CHECK(std::isfinite(world->getExplosionResultDeltaAngularVelocityZ(0)));

    nearBody->setLinearVelocity(0.f, 0.f, 0.f);
    farBody->setLinearVelocity(0.f, 0.f, 0.f);
    const int attracted = world->explode(0.f, 0.f, 0.f, 1.f, 4.f, -10.f, 1);
    CHECK_EQ(attracted, 2);
    CHECK_LT(nearBody->getLinearVelocityX(), 0.f);
    const int untouched = world->explode(0.f, 0.f, 0.f, 1.f, 4.f, 0.f, 1);
    CHECK_EQ(untouched, 0);
    CHECK_EQ(world->getExplosionResultCount(), 0);
    CHECK_THROWS((world->explode(0.f, 0.f, 0.f, -1.f, 0.f, 1.f), false));
    CHECK_THROWS((world->explode(0.f, 0.f, 0.f, 1.f, -1.f, 1.f), false));
    CHECK_THROWS((world->explode(std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f, 1.f, 0.f, 1.f), false));
    CHECK_THROWS((world->getExplosionResultBodyId(0), false));
}

TEST_CASE("box3d.world.diagnosticsExposeBoundsCountersAndStepProfile") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, true));
    Body3D                  *floor = world->newBody("static", 0.f, -0.5f, 0.f);
    floor->newBoxShape(10.f, 1.f, 10.f);
    Body3D *a = world->newBody("dynamic", -0.5f, 2.f, 0.f);
    a->newSphereShape(0.25f);
    Body3D *b = world->newBody("dynamic", 0.5f, 2.f, 0.f);
    b->newSphereShape(0.25f);
    world->newDistanceJoint(a, b, -0.5f, 2.f, 0.f, 0.5f, 2.f, 0.f, 1.f);

    CHECK_EQ(world->getBodyCount(), 3);
    CHECK_EQ(world->getShapeCount(), 3);
    CHECK_EQ(world->getJointCount(), 1);
    CHECK_LE(world->getBoundsMinX(), -5.f);
    CHECK_GE(world->getBoundsMaxX(), 5.f);
    CHECK_LE(world->getBoundsMinY(), -1.f);
    CHECK_GE(world->getBoundsMaxY(), 2.25f);
    CHECK_GE(world->getStaticTreeHeight(), 0);
    CHECK_GE(world->getDynamicTreeHeight(), 0);
    CHECK_GT(world->getMemoryByteCount(), 0);

    for (int i = 0; i < 120; ++i) world->updateFull(1.f / 60.f, 4);
    CHECK_GT(world->getContactCount(), 0);
    CHECK_GE(world->getIslandCount(), 1);
    CHECK_GE(world->getAwakeBodyCount(), 0);
    CHECK_GE(world->getAwakeContactCount(), 0);
    CHECK_GE(world->getRecycledContactCount(), 0);
    CHECK_GE(world->getProfileStepMs(), 0.f);
    CHECK_GE(world->getProfilePairsMs(), 0.f);
    CHECK_GE(world->getProfileCollideMs(), 0.f);
    CHECK_GE(world->getProfileSolveMs(), 0.f);
    CHECK_GE(world->getProfileBulletsMs(), 0.f);
    CHECK_GE(world->getProfileSensorsMs(), 0.f);
}

TEST_CASE("box3d.shape.explosionScaleDisablesAmplifiesAndSurvivesRecreation") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *normal         = world->newBody("dynamic", 2.f, 0.f, 0.f);
    Shape3D                 *normalShape    = normal->newSphereShape(0.25f);
    Body3D                  *amplified      = world->newBody("dynamic", 0.f, 0.f, 2.f);
    Shape3D                 *amplifiedShape = amplified->newSphereShape(0.25f);
    Body3D                  *immune         = world->newBody("dynamic", -2.f, 0.f, 0.f);
    Shape3D                 *immuneShape    = immune->newSphereShape(0.25f);

    const int stableId = amplifiedShape->getId();
    amplifiedShape->setExplosionScale(2.f);
    immuneShape->setExplosionScale(0.f);
    amplifiedShape->setSensor(true);
    amplifiedShape->setSensor(false);
    CHECK_EQ(amplifiedShape->getId(), stableId);
    CHECK(std::fabs(amplifiedShape->getExplosionScale() - 2.f) < 1e-6f);
    CHECK(std::fabs(normalShape->getExplosionScale() - 1.f) < 1e-6f);

    CHECK_EQ(world->explode(0.f, 0.f, 0.f, 3.f, 0.f, 10.f), 2);
    const float normalSpeed    = std::sqrt(normal->getLinearVelocityX() * normal->getLinearVelocityX() +
                                           normal->getLinearVelocityY() * normal->getLinearVelocityY() +
                                           normal->getLinearVelocityZ() * normal->getLinearVelocityZ());
    const float amplifiedSpeed = std::sqrt(amplified->getLinearVelocityX() * amplified->getLinearVelocityX() +
                                           amplified->getLinearVelocityY() * amplified->getLinearVelocityY() +
                                           amplified->getLinearVelocityZ() * amplified->getLinearVelocityZ());
    CHECK_GT(normalSpeed, 0.f);
    CHECK(std::fabs(amplifiedSpeed / normalSpeed - 2.f) < 0.02f);
    CHECK(std::fabs(immune->getLinearVelocityX()) < 1e-6f);
    CHECK(std::fabs(immune->getLinearVelocityZ()) < 1e-6f);
    CHECK_THROWS((immuneShape->setExplosionScale(-0.1f), false));
    CHECK_THROWS((immuneShape->setExplosionScale(std::numeric_limits<float>::infinity()), false));
}

TEST_CASE("box3d.oneway.configurationAndSensorRecreation") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Shape3D                 *shape = world->newBody("static", 0.f, 0.f, 0.f)->newBoxShape(4.f, 0.5f, 4.f);

    CHECK(!shape->isOneWay());
    CHECK_THROWS((shape->setOneWay(0.f, 0.f, 0.f, 0.f, 0.05f, 0.1f), false));
    CHECK_THROWS((shape->setOneWay(0.f, 1.f, 0.f, 0.f, -0.01f, 0.1f), false));
    CHECK_THROWS((shape->setOneWay(0.f, 1.f, 0.f, 0.f, 0.05f, 1.01f), false));

    shape->setOneWay(0.f, 2.f, 0.f, 0.25f, 0.05f, 0.2f);
    CHECK(shape->isOneWay());
    CHECK(b3Shape_ArePreSolveEventsEnabled(eve::physics::box3d::Access::shapeId(*shape)));
    shape->setSensor(true);
    CHECK(shape->isOneWay());
    CHECK(!b3Shape_ArePreSolveEventsEnabled(eve::physics::box3d::Access::shapeId(*shape)));
    shape->setSensor(false);
    CHECK(shape->isOneWay());
    CHECK(b3Shape_ArePreSolveEventsEnabled(eve::physics::box3d::Access::shapeId(*shape)));
    shape->disableOneWay();
    CHECK(!shape->isOneWay());
    CHECK(!b3Shape_ArePreSolveEventsEnabled(eve::physics::box3d::Access::shapeId(*shape)));
}

TEST_CASE("box3d.oneway.platformPassesFromBelowAndBlocksFromAbove") {
    auto run = [](float startY, float velocityY, bool oneWay) {
        auto                    *mod = Physics::create();
        std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
        Shape3D                 *platform = world->newBody("static", 0.f, 0.f, 0.f)->newBoxShape(10.f, 0.5f, 10.f);
        if (oneWay) platform->setOneWay(0.f, 1.f, 0.f, 0.25f, 0.06f, 0.2f);
        Body3D *ball = world->newBody("dynamic", 0.f, startY, 0.f);
        ball->newSphereShape(0.25f);
        ball->setLinearVelocity(0.f, velocityY, 0.f);
        for (int i = 0; i < 45; ++i) world->updateFull(1.f / 60.f, 4);
        return ball->getY();
    };

    CHECK_GT(run(-2.f, 5.f, true), 1.f);
    const float fromAbove = run(2.f, -5.f, true);
    CHECK_GT(fromAbove, 0.45f);
    CHECK_LT(fromAbove, 0.55f);
    CHECK_LT(run(-2.f, 5.f, false), -0.45f);
}

TEST_CASE("box3d.shape.filterSurvivesSensorRecreation") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D                  *body  = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D                 *shape = body->newSphereShape(0.5f);
    shape->setCategoryBits(8);
    shape->setMaskBits(3);
    shape->setGroupIndex(-7);
    shape->setTag(42);
    CHECK_EQ(shape->getCategoryBits(), 8);
    CHECK_EQ(shape->getMaskBits(), 3);
    CHECK_EQ(shape->getGroupIndex(), -7);
    CHECK_EQ(shape->getTag(), 42);

    const int stableShapeId = shape->getId();
    CHECK_GT(stableShapeId, 0);
    shape->setSensor(true);
    CHECK_EQ(shape->getId(), stableShapeId);
    CHECK_EQ(shape->getTag(), 42);
    CHECK_EQ(shape->getCategoryBits(), 8);
    CHECK_EQ(shape->getMaskBits(), 3);
    CHECK_EQ(shape->getGroupIndex(), -7);
    shape->setSphereRadius(0.75f);
    CHECK(shape->isSensor());
    CHECK_EQ(shape->getId(), stableShapeId);
    CHECK_EQ(shape->getTag(), 42);
    CHECK(std::fabs(shape->getRadius() - 0.75f) < 1e-5f);
}

TEST_CASE("box3d.shape.localTransformBuildsCompoundCollider") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *body = world->newBody("static", 0.f, 0.f, 0.f);

    Shape3D *box = body->newBoxShape(4.f, 1.f, 1.f);
    box->setTag(2101);
    box->setCategoryBits(8);
    box->setMaskBits(3);
    box->setGroupIndex(-4);
    box->setFriction(0.65f);
    box->setRestitution(0.25f);
    box->setDensity(2.5f);
    box->setHitEventsEnabled(true);
    const int   stableBoxId = box->getId();
    const float halfRoot    = std::sqrt(0.5f);
    box->setLocalTransform(3.f, 0.f, 0.f, 0.f, 0.f, halfRoot, halfRoot);

    CHECK_EQ(box->getId(), stableBoxId);
    CHECK_EQ(box->getTag(), 2101);
    CHECK_EQ(box->getCategoryBits(), 8);
    CHECK_EQ(box->getMaskBits(), 3);
    CHECK_EQ(box->getGroupIndex(), -4);
    CHECK(std::fabs(box->getFriction() - 0.65f) < 1e-5f);
    CHECK(std::fabs(box->getRestitution() - 0.25f) < 1e-5f);
    CHECK(std::fabs(box->getDensity() - 2.5f) < 1e-5f);
    CHECK(box->areHitEventsEnabled());
    CHECK(std::fabs(box->getLocalX() - 3.f) < 1e-5f);
    CHECK(std::fabs(box->getLocalRotZ() - halfRoot) < 1e-5f);
    CHECK(box->testPoint(3.f, 1.5f, 0.f));
    CHECK(!box->testPoint(4.5f, 0.f, 0.f));

    Shape3D *sphere = body->newSphereShape(0.75f);
    sphere->setLocalPosition(-3.f, 0.f, 0.f);
    CHECK(sphere->testPoint(-3.f, 0.f, 0.f));
    CHECK(!sphere->testPoint(0.f, 0.f, 0.f));

    Shape3D *capsule = body->newCapsuleShape(4.f, 0.5f);
    capsule->setLocalTransform(0.f, 0.f, 3.f, 0.f, 0.f, halfRoot, halfRoot);
    CHECK(capsule->testPoint(1.5f, 0.f, 3.f));
    CHECK(!capsule->testPoint(0.f, 1.5f, 3.f));

    CHECK_THROWS((box->setLocalRotation(0.f, 0.f, 0.f, 0.f), false));
}

TEST_CASE("box3d.shape.localTransformParticipatesInWorldQueries") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *body  = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D                 *shape = body->newSphereShape(1.f);
    shape->setTag(2201);
    shape->setLocalPosition(5.f, 0.f, 0.f);

    CHECK_EQ(world->rayCast(5.f, -4.f, 0.f, 5.f, 4.f, 0.f), body->getId());
    CHECK_EQ(world->getRayHitShapeId(), shape->getId());
    CHECK_EQ(world->getRayHitShapeTag(), 2201);
    CHECK_EQ(world->querySphere(5.f, 0.f, 0.f, 0.25f), 1);
    CHECK_EQ(world->getQueryShapeId(0), shape->getId());
    CHECK_EQ(world->querySphere(0.f, 0.f, 0.f, 0.25f), 0);
}

TEST_CASE("box3d.shape.runtimePrimitiveResizePreservesIdentityAndUpdatesMass") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *body = world->newBody("dynamic", 0.f, 0.f, 0.f);
    Shape3D                 *box  = body->newBoxShape(1.f, 1.f, 1.f, 2.f, 0.6f, 0.2f);
    box->setTag(2301);
    box->setCategoryBits(16);
    box->setMaskBits(7);
    box->setGroupIndex(-3);
    box->setHitEventsEnabled(true);
    box->setLocalPosition(1.f, 0.f, 0.f);
    const int   stableId    = box->getId();
    const float initialMass = body->getMass();

    CHECK_EQ(box->getKind(), std::string("box"));
    box->setBoxSize(2.f, 2.f, 2.f);
    CHECK_EQ(box->getId(), stableId);
    CHECK_EQ(box->getTag(), 2301);
    CHECK_EQ(box->getCategoryBits(), 16);
    CHECK_EQ(box->getMaskBits(), 7);
    CHECK_EQ(box->getGroupIndex(), -3);
    CHECK(box->areHitEventsEnabled());
    CHECK(std::fabs(box->getFriction() - 0.6f) < 1e-5f);
    CHECK(std::fabs(box->getRestitution() - 0.2f) < 1e-5f);
    CHECK(std::fabs(box->getDensity() - 2.f) < 1e-5f);
    CHECK(std::fabs(box->getBoxWidth() - 2.f) < 1e-5f);
    CHECK(std::fabs(box->getBoxHeight() - 2.f) < 1e-5f);
    CHECK(std::fabs(box->getBoxDepth() - 2.f) < 1e-5f);
    CHECK(box->testPoint(1.f, 0.9f, 0.f));
    CHECK_GT(body->getMass(), initialMass * 7.5f);
    CHECK_THROWS((box->setSphereRadius(1.f), false));
    CHECK_THROWS((box->setBoxSize(0.f, 1.f, 1.f), false));
    CHECK(std::fabs(box->getBoxWidth() - 2.f) < 1e-5f);
}

TEST_CASE("box3d.shape.runtimeSphereAndCapsuleResizeAffectsQueries") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *body   = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D                 *sphere = body->newSphereShape(0.5f);
    sphere->setLocalPosition(-3.f, 0.f, 0.f);
    sphere->setSphereRadius(1.5f);
    CHECK_EQ(sphere->getKind(), std::string("sphere"));
    CHECK(std::fabs(sphere->getRadius() - 1.5f) < 1e-5f);
    CHECK(sphere->testPoint(-1.75f, 0.f, 0.f));

    Shape3D *capsule = body->newCapsuleShape(4.f, 0.5f);
    CHECK_EQ(capsule->getKind(), std::string("capsule"));
    CHECK(capsule->testPoint(0.f, 2.f, 0.f));
    capsule->setCapsuleSize(1.f, 0.25f);
    CHECK(std::fabs(capsule->getCapsuleHeight() - 1.f) < 1e-5f);
    CHECK(std::fabs(capsule->getRadius() - 0.25f) < 1e-5f);
    CHECK(!capsule->testPoint(0.f, 2.f, 0.f));
    CHECK(capsule->testPoint(0.f, 0.6f, 0.f));
    CHECK_THROWS((capsule->setCapsuleSize(-1.f, 0.25f), false));
    CHECK_THROWS((sphere->setCapsuleSize(1.f, 0.25f), false));
}

TEST_CASE("box3d.shape.convexHullCreatesQueriesAndRebuildsAtomically") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D                  *body = world->newBody("static", 0.f, 0.f, 0.f);
    const std::vector<float> smallCube{-0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f,
                                       -0.5f, -0.5f, 0.5f,  0.5f, -0.5f, 0.5f,  -0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f};
    Shape3D                 *hull = body->newConvexHullShape(smallCube, 16);
    REQUIRE(hull != nullptr);
    CHECK_EQ(hull->getKind(), std::string("convexHull"));
    CHECK_EQ(hull->getConvexHullPointCount(), 8);
    CHECK_EQ(hull->getConvexHullMaxVertices(), 16);
    CHECK(hull->testPoint(0.f, 0.f, 0.f));
    CHECK(!hull->testPoint(0.75f, 0.f, 0.f));
    CHECK_EQ(world->querySphere(0.f, 0.f, 0.f, 0.1f), 1);

    hull->setTag(77);
    hull->setCategoryBits(8);
    hull->setSensor(true);
    const int                stableId = hull->getId();
    const std::vector<float> largeCube{-1.f, -1.f, -1.f, 1.f, -1.f, -1.f, -1.f, 1.f, -1.f, 1.f, 1.f, -1.f,
                                       -1.f, -1.f, 1.f,  1.f, -1.f, 1.f,  -1.f, 1.f, 1.f,  1.f, 1.f, 1.f};
    hull->setConvexHullVertices(largeCube, 32);
    CHECK_EQ(hull->getId(), stableId);
    CHECK_EQ(hull->getTag(), 77);
    CHECK_EQ(hull->getCategoryBits(), 8);
    CHECK(hull->isSensor());
    CHECK_EQ(hull->getConvexHullMaxVertices(), 32);
    CHECK(hull->testPoint(0.75f, 0.f, 0.f));

    const std::vector<float> coplanar{0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f};
    CHECK_THROWS((hull->setConvexHullVertices(coplanar, 16), false));
    CHECK_THROWS((hull->setConvexHullVertices(largeCube, 3), false));
    CHECK_THROWS((body->newConvexHullShape({0.f, 0.f, 0.f}, 16), false));
    CHECK_EQ(hull->getConvexHullPointCount(), 8);
    CHECK_EQ(hull->getConvexHullMaxVertices(), 32);
    CHECK(hull->testPoint(0.75f, 0.f, 0.f));
}

TEST_CASE("box3d.shape.convexHullSupportsLocalTransformAndRigidCollision") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    const std::vector<float> wedge{-2.f, -0.5f, -2.f, 2.f,  -0.5f, -2.f, -2.f, -0.5f, 2.f,
                                   2.f,  -0.5f, 2.f,  -2.f, 0.5f,  -2.f, 2.f,  0.5f,  -2.f};
    Shape3D                 *ground = world->newBody("static", 0.f, 0.f, 0.f)->newConvexHullShape(wedge, 16);
    ground->setLocalPosition(1.f, 0.f, 0.f);
    CHECK(ground->testPoint(1.f, 0.f, -1.5f));
    CHECK_EQ(world->querySphere(1.f, 0.f, -1.5f, 0.1f), 1);

    Body3D *ball = world->newBody("dynamic", 1.f, 3.f, -1.5f);
    ball->newSphereShape(0.25f);
    // Isolate vertical support; an unconstrained sphere rolls off this wedge.
    ball->setMotionLocks(true, false, true, true, true, true);
    for (int i = 0; i < 120; ++i) world->update(1.f / 60.f);
    const float supportedY = 0.375f + 0.25f * std::sqrt(1.f + 0.25f * 0.25f);
    CHECK(std::fabs(ball->getY() - supportedY) < 0.02f);
    CHECK(std::fabs(ball->getZ() + 1.5f) < 0.001f);
}

TEST_CASE("box3d.shape.triangleMeshValidatesRebuildsAndStaysStatic") {
    auto                      *mod = Physics::create();
    std::unique_ptr<World3D>   world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    const std::vector<float>   vertices{-1.f, 0.f, -1.f, 1.f, 0.f, -1.f, 1.f, 0.f, 1.f, -1.f, 0.f, 1.f};
    const std::vector<int32_t> indices{0, 2, 1, 0, 3, 2};
    Body3D                    *body = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D                   *mesh = body->newTriangleMeshShape(vertices, indices);
    REQUIRE(mesh != nullptr);
    CHECK_EQ(mesh->getKind(), std::string("triangleMesh"));
    CHECK_EQ(mesh->getTriangleMeshVertexCount(), 4);
    CHECK_EQ(mesh->getTriangleMeshTriangleCount(), 2);
    CHECK_EQ(world->rayCast(0.f, 2.f, 0.f, 0.f, -2.f, 0.f), body->getId());

    mesh->setTag(91);
    mesh->setCategoryBits(16);
    mesh->setSensor(true);
    mesh->setLocalPosition(0.f, 1.f, 0.f);
    const int stableId = mesh->getId();
    CHECK_EQ(world->rayCast(0.f, 2.f, 0.f, 0.f, 0.f, 0.f), body->getId());

    const std::vector<float> larger{-2.f, 0.f, -2.f, 2.f, 0.f, -2.f, 2.f, 0.f, 2.f, -2.f, 0.f, 2.f};
    mesh->setTriangleMeshData(larger, indices, false, 0.f, true, true);
    CHECK_EQ(mesh->getId(), stableId);
    CHECK_EQ(mesh->getTag(), 91);
    CHECK_EQ(mesh->getCategoryBits(), 16);
    CHECK(mesh->isSensor());
    CHECK_EQ(mesh->getTriangleMeshVertexCount(), 4);
    CHECK_EQ(world->rayCast(1.5f, 2.f, 0.f, 1.5f, 0.f, 0.f), body->getId());

    CHECK_THROWS((mesh->setTriangleMeshData(larger, {0, 1, 8}), false));
    CHECK_THROWS((mesh->setTriangleMeshData(larger, {0, 0, 1}), false));
    CHECK_THROWS((mesh->setTriangleMeshData(larger, indices, true, -0.1f), false));
    CHECK_EQ(mesh->getTriangleMeshTriangleCount(), 2);
    CHECK_EQ(world->rayCast(1.5f, 2.f, 0.f, 1.5f, 0.f, 0.f), body->getId());
    CHECK_THROWS((body->setType("dynamic"), false));
    CHECK_EQ(body->getType(), std::string("static"));

    Body3D *dynamicBody = world->newBody("dynamic", 0.f, 0.f, 0.f);
    CHECK_THROWS((dynamicBody->newTriangleMeshShape(vertices, indices), false));
}

TEST_CASE("box3d.shape.concaveTriangleMeshPreservesHoleAndSupportsRigidContact") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    std::vector<float>       vertices;
    std::vector<int32_t>     indices;
    auto                     addQuad = [&](float minX, float maxX, float minZ, float maxZ) {
        const int32_t base = static_cast<int32_t>(vertices.size() / 3);
        vertices.insert(vertices.end(), {minX, 0.f, minZ, maxX, 0.f, minZ, maxX, 0.f, maxZ, minX, 0.f, maxZ});
        indices.insert(indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
    };
    addQuad(-3.f, -1.f, -3.f, 3.f);
    addQuad(1.f, 3.f, -3.f, 3.f);
    addQuad(-1.f, 1.f, -3.f, -1.f);
    addQuad(-1.f, 1.f, 1.f, 3.f);
    Body3D *groundBody = world->newBody("static", 0.f, 0.f, 0.f);
    groundBody->newTriangleMeshShape(vertices, indices, true, 0.001f, true, false);

    CHECK_EQ(world->querySphere(0.f, 0.f, 0.f, 0.2f), 0);
    CHECK_EQ(world->querySphere(2.f, 0.f, 0.f, 0.2f), 1);
    CHECK_EQ(world->rayCast(0.f, 2.f, 0.f, 0.f, -2.f, 0.f), -1);
    CHECK_EQ(world->rayCast(2.f, 2.f, 0.f, 2.f, -2.f, 0.f), groundBody->getId());

    Body3D *ball = world->newBody("dynamic", 2.f, 3.f, 0.f);
    ball->newSphereShape(0.25f);
    for (int i = 0; i < 120; ++i) world->update(1.f / 60.f);
    CHECK_GT(ball->getY(), 0.2f);
    CHECK_LT(ball->getY(), 0.35f);
}

TEST_CASE("box3d.shape.heightFieldUpdatesRegionAndPreservesConfiguration") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D                  *body = world->newBody("static", -1.f, 0.f, -1.f);
    std::vector<float>       heights(9, 0.f);
    Shape3D                 *terrain = body->newHeightFieldShape(3, 3, 1.f, 1.f, heights, -4.f, 4.f);
    REQUIRE(terrain != nullptr);
    CHECK_EQ(terrain->getKind(), std::string("heightField"));
    CHECK_EQ(terrain->getHeightFieldCountX(), 3);
    CHECK_EQ(terrain->getHeightFieldCountZ(), 3);
    CHECK(std::fabs(terrain->getHeightFieldCellSizeX() - 1.f) < 1e-6f);
    CHECK(std::fabs(terrain->getHeightFieldCellSizeZ() - 1.f) < 1e-6f);
    CHECK_EQ(world->rayCast(0.f, 3.f, 0.f, 0.f, -3.f, 0.f), body->getId());
    CHECK(std::fabs(world->getRayHitY()) < 0.001f);

    terrain->setTag(123);
    terrain->setCategoryBits(32);
    terrain->setSensor(true);
    const int stableId = terrain->getId();
    terrain->setHeightFieldRegion(1, 1, 1, 1, {2.f});
    CHECK_EQ(terrain->getId(), stableId);
    CHECK_EQ(terrain->getTag(), 123);
    CHECK_EQ(terrain->getCategoryBits(), 32);
    CHECK(terrain->isSensor());
    CHECK(std::fabs(terrain->getHeightFieldHeight(1, 1) - 2.f) < 1e-6f);
    CHECK_EQ(world->rayCast(0.f, 3.f, 0.f, 0.f, -3.f, 0.f), body->getId());
    CHECK(std::fabs(world->getRayHitY() - 2.f) < 0.001f);

    CHECK_THROWS((terrain->setHeightFieldRegion(3, 0, 1, 1, {0.f}), false));
    CHECK_THROWS((terrain->setHeightFieldRegion(0, 0, 2, 2, {0.f}), false));
    CHECK_THROWS((terrain->setHeightFieldHeights(std::vector<float>(9, 5.f)), false));
    CHECK_THROWS((terrain->getHeightFieldHeight(3, 0), false));
    CHECK_THROWS((terrain->setLocalPosition(1.f, 0.f, 0.f), false));
    CHECK(std::fabs(terrain->getHeightFieldHeight(1, 1) - 2.f) < 1e-6f);

    body->setPosition(4.f, 1.f, 5.f);
    CHECK_EQ(world->rayCast(5.f, 5.f, 6.f, 5.f, -2.f, 6.f), body->getId());
    CHECK(std::fabs(world->getRayHitY() - 3.f) < 0.001f);
    CHECK_THROWS((body->setType("kinematic"), false));
}

TEST_CASE("box3d.shape.heightFieldSupportsTerrainContactAndStaticValidation") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    Body3D                  *ground = world->newBody("static", -1.f, 0.f, -1.f);
    const std::vector<float> heights(9, 0.f);
    ground->newHeightFieldShape(3, 3, 1.f, 1.f, heights, -1.f, 1.f);
    Body3D *ball = world->newBody("dynamic", 0.f, 3.f, 0.f);
    ball->newSphereShape(0.25f);
    for (int i = 0; i < 120; ++i) world->update(1.f / 60.f);
    CHECK_GT(ball->getY(), 0.2f);
    CHECK_LT(ball->getY(), 0.35f);

    Body3D *dynamicBody = world->newBody("dynamic", 0.f, 0.f, 0.f);
    CHECK_THROWS((dynamicBody->newHeightFieldShape(3, 3, 1.f, 1.f, heights, -1.f, 1.f), false));
    CHECK_THROWS((ground->newHeightFieldShape(1, 3, 1.f, 1.f, heights, -1.f, 1.f), false));
    CHECK_THROWS((ground->newHeightFieldShape(3, 3, 0.f, 1.f, heights, -1.f, 1.f), false));
    CHECK_THROWS((ground->newHeightFieldShape(3, 3, 1.f, 1.f, heights, 2.f, 1.f), false));
}

TEST_CASE("box3d.query.rayAndShapeCastExposeMeshAndHeightFieldTriangleIndex") {
    auto                      *mod = Physics::create();
    std::unique_ptr<World3D>   world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    const std::vector<float>   meshVertices{-1.f, 0.f, -1.f, 1.f, 0.f, -1.f, 1.f, 0.f, 1.f, -1.f, 0.f, 1.f};
    const std::vector<int32_t> meshIndices{0, 2, 1, 0, 3, 2};
    Body3D                    *meshBody  = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D                   *meshShape = meshBody->newTriangleMeshShape(meshVertices, meshIndices);
    meshShape->setMaterialId(0x12345678);

    CHECK_EQ(world->rayCast(-0.5f, 2.f, -0.5f, -0.5f, -2.f, -0.5f), meshBody->getId());
    CHECK_GE(world->getRayHitTriangleIndex(), 0);
    CHECK_LT(world->getRayHitTriangleIndex(), 2);
    CHECK_EQ(world->getRayResultTriangleIndex(0), world->getRayHitTriangleIndex());
    CHECK_EQ(world->getRayHitMaterialId(), 0x12345678);
    CHECK_EQ(world->getRayResultMaterialId(0), 0x12345678);
    CHECK_EQ(world->castSphere(-0.5f, 2.f, -0.5f, 0.1f, 0.f, -4.f, 0.f), meshBody->getId());
    CHECK_GE(world->getShapeCastTriangleIndex(), 0);
    CHECK_LT(world->getShapeCastTriangleIndex(), 2);
    CHECK_EQ(world->getShapeCastResultTriangleIndex(0), world->getShapeCastTriangleIndex());
    CHECK_EQ(world->getShapeCastMaterialId(), 0x12345678);
    CHECK_EQ(world->getShapeCastResultMaterialId(0), 0x12345678);

    Body3D *terrainBody = world->newBody("static", 10.f, 0.f, 0.f);
    terrainBody->newHeightFieldShape(3, 3, 1.f, 1.f, std::vector<float>(9, 0.f), -1.f, 1.f);
    CHECK_EQ(world->rayCast(10.25f, 2.f, 0.25f, 10.25f, -2.f, 0.25f), terrainBody->getId());
    CHECK_GE(world->getRayHitTriangleIndex(), 0);
    CHECK_LT(world->getRayHitTriangleIndex(), 8);
    CHECK_EQ(world->castSphere(10.25f, 2.f, 0.25f, 0.1f, 0.f, -4.f, 0.f), terrainBody->getId());
    CHECK_GE(world->getShapeCastTriangleIndex(), 0);
    CHECK_LT(world->getShapeCastTriangleIndex(), 8);

    Body3D *boxBody = world->newBody("static", 20.f, 0.f, 0.f);
    boxBody->newBoxShape(2.f, 0.5f, 2.f);
    CHECK_EQ(world->rayCast(20.f, 2.f, 0.f, 20.f, -2.f, 0.f), boxBody->getId());
    CHECK_EQ(world->getRayHitTriangleIndex(), -1);
    CHECK_EQ(world->getRayHitMaterialId(), 0);
    CHECK_EQ(world->castSphere(20.f, 2.f, 0.f, 0.1f, 0.f, -4.f, 0.f), boxBody->getId());
    CHECK_EQ(world->getShapeCastTriangleIndex(), -1);
    CHECK_EQ(world->getShapeCastMaterialId(), 0);

    CHECK_EQ(world->rayCast(30.f, 2.f, 0.f, 30.f, -2.f, 0.f), -1);
    CHECK_EQ(world->getRayHitTriangleIndex(), -1);
    CHECK_EQ(world->castSphere(30.f, 2.f, 0.f, 0.1f, 0.f, -4.f, 0.f), -1);
    CHECK_EQ(world->getShapeCastTriangleIndex(), -1);
    CHECK_THROWS((world->getRayResultTriangleIndex(0), false));
    CHECK_THROWS((world->getShapeCastResultTriangleIndex(0), false));
}

TEST_CASE("box3d.shape.triangleMeshPerTriangleMaterialsDriveQueriesAndSurviveRebuild") {
    auto                      *mod = Physics::create();
    std::unique_ptr<World3D>   world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    const std::vector<float>   vertices{-4.f, 0.f, -1.f, -2.f, 0.f, -1.f, -3.f, 0.f, 1.f,
                                      2.f,  0.f, -1.f, 4.f,  0.f, -1.f, 3.f,  0.f, 1.f};
    const std::vector<int32_t> indices{0, 2, 1, 3, 5, 4};
    Shape3D *mesh = world->newBody("static", 0.f, 0.f, 0.f)->newTriangleMeshShape(vertices, indices, false);
    mesh->setTriangleMeshMaterialIndices({0, 1});
    CHECK_EQ(mesh->getTriangleMeshMaterialCount(), 2);
    CHECK_EQ(mesh->getTriangleMeshMaterialIndex(0), 0);
    CHECK_EQ(mesh->getTriangleMeshMaterialIndex(1), 1);
    mesh->setTriangleMeshMaterial(0, 0.1f, 0.2f, 0.01f, 0.f, 0.f, 0.f, 101, "minimum", "average");
    mesh->setTriangleMeshMaterial(1, 0.9f, 0.8f, 0.03f, 1.f, 0.f, 0.f, 202, "maximum", "multiply");
    CHECK(std::fabs(mesh->getTriangleMeshMaterialFriction(1) - 0.9f) < 0.001f);
    CHECK(std::fabs(mesh->getTriangleMeshMaterialRestitution(0) - 0.2f) < 0.001f);
    CHECK(std::fabs(mesh->getTriangleMeshMaterialRollingResistance(1) - 0.03f) < 0.001f);
    CHECK_EQ(mesh->getTriangleMeshMaterialId(0), 101);
    CHECK_EQ(mesh->getTriangleMeshMaterialId(1), 202);

    CHECK_NE(world->rayCast(-3.f, 2.f, 0.f, -3.f, -2.f, 0.f), -1);
    CHECK_EQ(world->getRayHitMaterialId(), 101);
    CHECK_NE(world->rayCast(3.f, 2.f, 0.f, 3.f, -2.f, 0.f), -1);
    CHECK_EQ(world->getRayHitMaterialId(), 202);
    CHECK_NE(world->castSphere(3.f, 2.f, 0.f, 0.1f, 0.f, -4.f, 0.f), -1);
    CHECK_EQ(world->getShapeCastMaterialId(), 202);

    mesh->setSensor(true);
    mesh->setSensor(false);
    CHECK_EQ(mesh->getTriangleMeshMaterialCount(), 2);
    CHECK_EQ(mesh->getTriangleMeshMaterialId(1), 202);
    CHECK_NE(world->rayCast(3.f, 2.f, 0.f, 3.f, -2.f, 0.f), -1);
    CHECK_EQ(world->getRayHitMaterialId(), 202);

    CHECK_THROWS((mesh->setTriangleMeshMaterialIndices({0}), false));
    CHECK_THROWS((mesh->setTriangleMeshMaterialIndices({0, 255}), false));
    CHECK_THROWS((mesh->setTriangleMeshMaterial(2, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0, "default", "default"), false));
    CHECK_THROWS((mesh->getTriangleMeshMaterialIndex(2), false));
}

TEST_CASE("box3d.query.ignoreBodyAndShapeAppliesAcrossQueryTypes") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *self      = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D                 *selfFront = self->newSphereShape(1.f);
    Shape3D                 *selfRear  = self->newSphereShape(0.75f);
    selfRear->setLocalPosition(2.f, 0.f, 0.f);
    Body3D  *target      = world->newBody("static", 6.f, 0.f, 0.f);
    Shape3D *targetShape = target->newSphereShape(1.f);

    CHECK_EQ(world->rayCast(-3.f, 0.f, 0.f, 10.f, 0.f, 0.f), self->getId());
    CHECK_EQ(world->getRayHitShapeId(), selfFront->getId());

    world->setQueryIgnoredShapeId(selfFront->getId());
    CHECK_EQ(world->rayCast(-3.f, 0.f, 0.f, 10.f, 0.f, 0.f), self->getId());
    CHECK_EQ(world->getRayHitShapeId(), selfRear->getId());

    world->setQueryIgnoredBodyId(self->getId());
    CHECK_EQ(world->rayCast(-3.f, 0.f, 0.f, 10.f, 0.f, 0.f), target->getId());
    CHECK_EQ(world->getRayHitShapeId(), targetShape->getId());
    CHECK_EQ(world->querySphere(0.f, 0.f, 0.f, 3.f), 0);
    CHECK_EQ(world->castSphere(-3.f, 0.f, 0.f, 0.25f, 12.f, 0.f, 0.f), target->getId());
    CHECK_EQ(world->closestPoint(0.f, 0.f, 0.f, 10.f), target->getId());

    CHECK(world->moveCapsule(-3.f, -0.5f, 0.f, -3.f, 0.5f, 0.f, 0.25f, 12.f, 0.f, 0.f));
    CHECK_GT(world->getMoverDeltaX(), 1.f);
    CHECK_LT(world->getMoverDeltaX(), 12.f);

    CHECK_EQ(world->getQueryIgnoredBodyId(), self->getId());
    CHECK_EQ(world->getQueryIgnoredShapeId(), selfFront->getId());
    world->clearQueryIgnores();
    CHECK_EQ(world->getQueryIgnoredBodyId(), -1);
    CHECK_EQ(world->getQueryIgnoredShapeId(), -1);
    CHECK_EQ(world->rayCast(-3.f, 0.f, 0.f, 10.f, 0.f, 0.f), self->getId());
    CHECK_THROWS((world->setQueryIgnoredBodyId(0), false));
    CHECK_THROWS((world->setQueryIgnoredShapeId(-2), false));
}

TEST_CASE("box3d.events.contactCarriesStableShapeIdentity") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *wall      = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D                 *wallShape = wall->newBoxShape(2.f, 2.f, 2.f);
    wallShape->setTag(3101);
    Body3D  *actor      = world->newBody("dynamic", 0.f, 0.f, 0.f);
    Shape3D *actorShape = actor->newSphereShape(0.5f);
    actorShape->setTag(3102);

    world->update(1.f / 60.f);
    REQUIRE_EQ(world->getBeginContactCount(), 1);
    CHECK_EQ(world->getBeginContactShapeAId(0), wallShape->getId());
    CHECK_EQ(world->getBeginContactShapeBId(0), actorShape->getId());
    CHECK_EQ(world->getBeginContactBodyAId(0), wall->getId());
    CHECK_EQ(world->getBeginContactBodyBId(0), actor->getId());
    CHECK_EQ(world->getBeginContactShapeATag(0), 3101);
    CHECK_EQ(world->getBeginContactShapeBTag(0), 3102);

    actor->setPosition(10.f, 0.f, 0.f);
    world->update(1.f / 60.f);
    REQUIRE_EQ(world->getEndContactCount(), 1);
    CHECK_EQ(world->getEndContactShapeAId(0), wallShape->getId());
    CHECK_EQ(world->getEndContactShapeBId(0), actorShape->getId());
    CHECK_EQ(world->getBeginContactCount(), 0);
    world->clearContactEvents();
    CHECK_EQ(world->getEndContactCount(), 0);
}

TEST_CASE("box3d.events.sensorUsesDedicatedTriggerBuffer") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *zone   = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D                 *sensor = zone->newBoxShape(4.f, 4.f, 4.f);
    sensor->setTag(3201);
    sensor->setSensor(true);
    Body3D  *actor   = world->newBody("dynamic", 0.f, 0.f, 0.f);
    Shape3D *visitor = actor->newSphereShape(0.5f);
    visitor->setTag(3202);

    world->update(1.f / 60.f);
    REQUIRE_EQ(world->getBeginTriggerCount(), 1);
    CHECK_EQ(world->getBeginContactCount(), 0);
    CHECK_EQ(world->getBeginTriggerSensorBodyId(0), zone->getId());
    CHECK_EQ(world->getBeginTriggerVisitorBodyId(0), actor->getId());
    CHECK_EQ(world->getBeginTriggerSensorShapeId(0), sensor->getId());
    CHECK_EQ(world->getBeginTriggerVisitorShapeId(0), visitor->getId());
    CHECK_EQ(world->getBeginTriggerSensorShapeTag(0), 3201);
    CHECK_EQ(world->getBeginTriggerVisitorShapeTag(0), 3202);

    actor->setPosition(10.f, 0.f, 0.f);
    world->update(1.f / 60.f);
    REQUIRE_EQ(world->getEndTriggerCount(), 1);
    CHECK_EQ(world->getEndTriggerSensorShapeId(0), sensor->getId());
    CHECK_EQ(world->getEndTriggerVisitorShapeId(0), visitor->getId());
    CHECK_EQ(world->getBeginTriggerCount(), 0);
}

TEST_CASE("box3d.events.endTriggerSurvivesSensorBackendRecreation") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *zone   = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D                 *sensor = zone->newBoxShape(4.f, 4.f, 4.f);
    sensor->setTag(3301);
    sensor->setSensor(true);
    Body3D  *actor   = world->newBody("dynamic", 0.f, 0.f, 0.f);
    Shape3D *visitor = actor->newSphereShape(0.5f);
    visitor->setTag(3302);

    world->update(1.f / 60.f);
    REQUIRE_EQ(world->getBeginTriggerCount(), 1);
    const int stableSensorId = sensor->getId();
    sensor->setSensor(false);
    world->update(1.f / 60.f);

    REQUIRE_EQ(world->getEndTriggerCount(), 1);
    CHECK_EQ(world->getEndTriggerSensorShapeId(0), stableSensorId);
    CHECK_EQ(world->getEndTriggerVisitorShapeId(0), visitor->getId());
    CHECK_EQ(world->getEndTriggerSensorShapeTag(0), 3301);
    CHECK_EQ(world->getEndTriggerVisitorShapeTag(0), 3302);
}

TEST_CASE("box3d.events.hitReportsPointNormalSpeedAndImpulse") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    world->setHitEventThreshold(1.f);
    CHECK(std::fabs(world->getHitEventThreshold() - 1.f) < 1e-5f);

    Body3D  *ground      = world->newBody("static", 0.f, -0.5f, 0.f);
    Shape3D *groundShape = ground->newBoxShape(10.f, 1.f, 10.f);
    groundShape->setTag(3401);
    Body3D  *ball      = world->newBody("dynamic", 0.f, 2.f, 0.f);
    Shape3D *ballShape = ball->newSphereShape(0.5f, 1.f, 0.2f, 0.f);
    ballShape->setTag(3402);
    ballShape->setHitEventsEnabled(true);
    CHECK(ballShape->areHitEventsEnabled());
    ball->setLinearVelocity(0.f, -30.f, 0.f);

    bool captured = false;
    for (int step = 0; step < 30 && !captured; ++step) {
        world->update(1.f / 60.f);
        captured = world->getHitCount() > 0;
    }
    REQUIRE(captured);
    CHECK_EQ(world->getHitShapeAId(0), groundShape->getId());
    CHECK_EQ(world->getHitShapeBId(0), ballShape->getId());
    CHECK_EQ(world->getHitShapeATag(0), 3401);
    CHECK_EQ(world->getHitShapeBTag(0), 3402);
    CHECK_EQ(world->getHitBodyAId(0), ground->getId());
    CHECK_EQ(world->getHitBodyBId(0), ball->getId());
    CHECK(std::fabs(world->getHitPointX(0)) < 0.1f);
    CHECK(std::fabs(world->getHitPointY(0)) < 0.2f);
    CHECK(std::fabs(world->getHitPointZ(0)) < 0.1f);
    CHECK_GT(world->getHitNormalY(0), 0.9f);
    CHECK_GT(world->getHitApproachSpeed(0), 1.f);
    CHECK_GT(world->getHitNormalImpulse(0), 0.f);
}

TEST_CASE("box3d.events.hitPreferenceSurvivesSensorToggle") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *body  = world->newBody("dynamic", 0.f, 0.f, 0.f);
    Shape3D                 *shape = body->newSphereShape(0.5f);
    shape->setHitEventsEnabled(true);
    shape->setSensor(true);
    CHECK(shape->areHitEventsEnabled());
    shape->setSensor(false);
    CHECK(shape->areHitEventsEnabled());
    CHECK(b3Shape_AreHitEventsEnabled(eve::physics::box3d::Access::shapeId(*shape)));
}

TEST_CASE("box3d.contacts.bodyManifoldIsPersistentAndBodyRelative") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, -10.f, 0.f, false));
    Body3D                  *ground      = world->newBody("static", 0.f, -0.5f, 0.f);
    Shape3D                 *groundShape = ground->newBoxShape(10.f, 1.f, 10.f);
    groundShape->setTag(3501);
    Body3D  *ball      = world->newBody("dynamic", 0.f, 2.f, 0.f);
    Shape3D *ballShape = ball->newSphereShape(0.5f, 1.f, 0.4f, 0.f);
    ballShape->setTag(3502);

    for (int step = 0; step < 120; ++step) world->update(1.f / 60.f);

    REQUIRE_GT(world->queryBodyContacts(ball->getId(), 16), 0);
    CHECK_EQ(world->getContactPointCount(), world->queryBodyContacts(ball->getId(), 16));
    CHECK_EQ(world->getContactPointShapeId(0), ballShape->getId());
    CHECK_EQ(world->getContactPointShapeTag(0), 3502);
    CHECK_EQ(world->getContactPointOtherBodyId(0), ground->getId());
    CHECK_EQ(world->getContactPointOtherShapeId(0), groundShape->getId());
    CHECK_EQ(world->getContactPointOtherShapeTag(0), 3501);
    CHECK_LT(world->getContactPointNormalY(0), -0.9f);
    CHECK(std::fabs(world->getContactPointNormalX(0)) < 0.1f);
    CHECK(std::fabs(world->getContactPointZ(0)) < 0.1f);
    CHECK(std::fabs(world->getContactPointY(0)) < 0.1f);
    CHECK_LT(world->getContactPointSeparation(0), 0.05f);
    CHECK_GE(world->getContactPointNormalImpulse(0), 0.f);
    CHECK_GT(world->getContactPointTotalNormalImpulse(0), 0.f);
    CHECK(world->isContactPointPersisted(0));

    REQUIRE_GT(world->queryBodyContacts(ground->getId(), 1), 0);
    CHECK_EQ(world->getContactPointCount(), 1);
    CHECK_EQ(world->getContactPointShapeId(0), groundShape->getId());
    CHECK_EQ(world->getContactPointOtherShapeId(0), ballShape->getId());
    CHECK_GT(world->getContactPointNormalY(0), 0.9f);

    ball->setPosition(0.f, 5.f, 0.f);
    world->update(1.f / 60.f);
    CHECK_EQ(world->queryBodyContacts(ball->getId(), 16), 0);
    CHECK_EQ(world->getContactPointCount(), 0);
    CHECK_THROWS((world->queryBodyContacts(ball->getId(), 0), false));
    CHECK_THROWS((world->getContactPointShapeId(0), false));
}

TEST_CASE("box3d.body.destroy") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D                  *b = world->newBody("dynamic", 1.f, 2.f, 3.f);
    b->newBoxShape(0.5f, 0.5f, 0.5f);
    int id = b->getId();
    CHECK_GT(id, 0);
    b->destroy();
    CHECK(std::fabs(b->getX()) < 0.001f);
    delete b;
}

TEST_CASE("box3d.query.rayCastAndAABB") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D                  *a  = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D                 *sa = a->newBoxShape(2.f, 2.f, 2.f);
    sa->setTag(101);
    Body3D *b = world->newBody("static", 5.f, 0.f, 0.f);
    b->newSphereShape(1.f);

    CHECK(sa->testPoint(0.f, 0.f, 0.f));
    CHECK(!sa->testPoint(10.f, 10.f, 10.f));

    int hit = world->rayCast(-5.f, 0.f, 0.f, 10.f, 0.f, 0.f);
    CHECK_EQ(hit, a->getId());
    CHECK(world->hasRayHit());
    CHECK_EQ(world->getRayHitShapeId(), sa->getId());
    CHECK_EQ(world->getRayHitShapeTag(), 101);
    CHECK(std::fabs(world->getRayHitY()) < 0.5f);

    int count = world->queryAABB(-2.f, -2.f, -2.f, 2.f, 2.f, 2.f);
    CHECK_GE(count, 1);
    bool foundA = false;
    for (int i = 0; i < world->getQueryCount(); ++i) {
        if (world->getQueryBodyId(i) == a->getId()) foundA = true;
    }
    CHECK(foundA);
    CHECK_EQ(world->getQueryShapeCount(), 1);
    CHECK_EQ(world->getQueryShapeId(0), sa->getId());
}

TEST_CASE("box3d.query.rayCastAllIsSortedBoundedAndCompatible") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *nearBody  = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D                 *nearShape = nearBody->newSphereShape(0.5f);
    nearShape->setTag(4101);
    Body3D  *middleBody  = world->newBody("static", 3.f, 0.f, 0.f);
    Shape3D *middleShape = middleBody->newSphereShape(0.5f);
    middleShape->setTag(4102);
    Body3D  *farBody  = world->newBody("static", 6.f, 0.f, 0.f);
    Shape3D *farShape = farBody->newSphereShape(0.5f);
    farShape->setTag(4103);

    REQUIRE_EQ(world->rayCastAll(-3.f, 0.f, 0.f, 9.f, 0.f, 0.f, 8), 3);
    CHECK_EQ(world->getRayResultCount(), 3);
    CHECK_EQ(world->getRayResultBodyId(0), nearBody->getId());
    CHECK_EQ(world->getRayResultBodyId(1), middleBody->getId());
    CHECK_EQ(world->getRayResultBodyId(2), farBody->getId());
    CHECK_EQ(world->getRayResultShapeId(0), nearShape->getId());
    CHECK_EQ(world->getRayResultShapeTag(1), 4102);
    CHECK_LT(world->getRayResultFraction(0), world->getRayResultFraction(1));
    CHECK_LT(world->getRayResultFraction(1), world->getRayResultFraction(2));
    CHECK_LT(world->getRayResultNormalX(0), -0.9f);
    CHECK(std::fabs(world->getRayResultY(0)) < 1e-5f);

    REQUIRE_EQ(world->rayCastAll(-3.f, 0.f, 0.f, 9.f, 0.f, 0.f, 2), 2);
    CHECK_EQ(world->getRayResultBodyId(0), nearBody->getId());
    CHECK_EQ(world->getRayResultBodyId(1), middleBody->getId());
    CHECK_EQ(world->getRayHitBodyId(), nearBody->getId());

    world->setQueryIgnoredBodyId(nearBody->getId());
    REQUIRE_EQ(world->rayCastAll(-3.f, 0.f, 0.f, 9.f, 0.f, 0.f, 8), 2);
    CHECK_EQ(world->getRayResultBodyId(0), middleBody->getId());
    CHECK_EQ(world->getRayResultShapeId(1), farShape->getId());
    world->clearQueryIgnores();

    CHECK_EQ(world->rayCast(-3.f, 0.f, 0.f, 9.f, 0.f, 0.f), nearBody->getId());
    CHECK_EQ(world->getRayResultCount(), 1);
    CHECK_EQ(world->getRayResultShapeId(0), nearShape->getId());
    CHECK_THROWS((world->rayCastAll(0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0), false));
    CHECK_EQ(world->getRayResultCount(), 0);
    CHECK_THROWS((world->getRayResultBodyId(0), false));
}

TEST_CASE("box3d.query.rayCastAllTieUsesStableShapeId") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *body   = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D                 *first  = body->newSphereShape(1.f);
    Shape3D                 *second = body->newSphereShape(1.f);
    REQUIRE_EQ(world->rayCastAll(-3.f, 0.f, 0.f, 3.f, 0.f, 0.f, 2), 2);
    CHECK_LT(first->getId(), second->getId());
    CHECK_EQ(world->getRayResultShapeId(0), first->getId());
    CHECK_EQ(world->getRayResultShapeId(1), second->getId());
    CHECK(std::fabs(world->getRayResultFraction(0) - world->getRayResultFraction(1)) < 1e-6f);
}

TEST_CASE("box3d.query.reportsEveryShapeOnOneBody") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D                  *body   = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D                 *sphere = body->newSphereShape(0.5f);
    Shape3D                 *box    = body->newBoxShape(2.f, 2.f, 2.f);
    sphere->setTag(11);
    box->setTag(22);

    CHECK_NE(sphere->getId(), box->getId());
    CHECK_EQ(world->querySphere(0.f, 0.f, 0.f, 0.1f), 1);  // Unique bodies.
    CHECK_EQ(world->getQueryCount(), 1);
    CHECK_EQ(world->getQueryShapeCount(), 2);
    CHECK_EQ(world->getQueryShapeId(0), std::min(sphere->getId(), box->getId()));
    CHECK_EQ(world->getQueryShapeId(1), std::max(sphere->getId(), box->getId()));
    const int firstTag = sphere->getId() < box->getId() ? 11 : 22;
    CHECK_EQ(world->getQueryShapeTag(0), firstTag);
    CHECK_EQ(world->getQueryShapeTag(1), firstTag == 11 ? 22 : 11);
    CHECK_THROWS((world->getQueryShapeId(2), false));
}

TEST_CASE("box3d.body.capsuleAndRotation") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D                  *cap = world->newBody("dynamic", 0.f, 1.f, 0.f);
    cap->newCapsuleShape(1.f, 0.25f, 1.f);
    cap->setRotation(0.f, 0.f, 0.f, 1.f);
    CHECK(std::fabs(cap->getRotW() - 1.f) < 0.001f);
    cap->setFixedRotation(true);
    CHECK(cap->isFixedRotation());
}

TEST_CASE("box3d.query.sphereAndCapsuleOverlap") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D                  *left = world->newBody("static", -2.f, 0.f, 0.f);
    left->newSphereShape(0.5f);
    Body3D *middle = world->newBody("static", 0.f, 0.f, 0.f);
    middle->newBoxShape(0.5f, 0.5f, 0.5f);
    Body3D *right = world->newBody("static", 2.f, 0.f, 0.f);
    right->newCapsuleShape(1.f, 0.25f);

    CHECK_EQ(world->querySphere(0.f, 0.f, 0.f, 0.1f), 1);
    CHECK_EQ(world->getQueryBodyId(0), middle->getId());

    CHECK_EQ(world->queryCapsule(-2.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.1f), 3);
    CHECK_EQ(world->getQueryBodyId(0), left->getId());
    CHECK_EQ(world->getQueryBodyId(1), middle->getId());
    CHECK_EQ(world->getQueryBodyId(2), right->getId());

    CHECK_EQ(world->queryCapsule(-2.f, 3.f, 0.f, 2.f, 3.f, 0.f, 0.1f), 0);
}

TEST_CASE("box3d.query.collisionLayerFilter") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Shape3D                 *layer1 = world->newBody("static", -1.f, 0.f, 0.f)->newSphereShape(0.5f);
    Shape3D                 *layer2 = world->newBody("static", 1.f, 0.f, 0.f)->newSphereShape(0.5f);
    layer1->setCategoryBits(1);
    layer2->setCategoryBits(2);

    world->setQueryFilter(4, 1);
    CHECK_EQ(world->queryCapsule(-2.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.1f), 1);
    CHECK_EQ(world->getQueryMaskBits(), 1);

    world->setQueryFilter(4, 2);
    CHECK_EQ(world->queryCapsule(-2.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.1f), 1);

    world->resetQueryFilter();
    CHECK_EQ(world->queryCapsule(-2.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.1f), 2);
}

TEST_CASE("box3d.query.sphereAndCapsuleCast") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D                  *wall      = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D                 *wallShape = wall->newBoxShape(1.f, 4.f, 4.f);
    wallShape->setTag(77);

    CHECK_EQ(world->castSphere(-5.f, 0.f, 0.f, 0.5f, 10.f, 0.f, 0.f), wall->getId());
    CHECK(world->hasShapeCastHit());
    CHECK_EQ(world->getShapeCastShapeId(), wallShape->getId());
    CHECK_EQ(world->getShapeCastShapeTag(), 77);
    CHECK(world->getShapeCastFraction() > 0.35f);
    CHECK(world->getShapeCastFraction() < 0.45f);
    CHECK(world->getShapeCastNormalX() < -0.9f);

    CHECK_EQ(world->castCapsule(-5.f, -1.f, 0.f, -5.f, 1.f, 0.f, 0.25f, 10.f, 0.f, 0.f), wall->getId());
    CHECK(world->getShapeCastFraction() > 0.35f);
    CHECK(world->getShapeCastFraction() < 0.45f);

    CHECK_EQ(world->castSphere(-5.f, 5.f, 0.f, 0.5f, 10.f, 0.f, 0.f), -1);
    CHECK(!world->hasShapeCastHit());
    CHECK_EQ(world->getShapeCastShapeId(), -1);
    CHECK_EQ(world->getShapeCastShapeTag(), 0);
}

TEST_CASE("box3d.query.shapeCastAllIsSortedBoundedAndSharedByPrimitives") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, false));
    Body3D                  *nearBody  = world->newBody("static", 0.f, 0.f, 0.f);
    Shape3D                 *nearShape = nearBody->newBoxShape(0.5f, 4.f, 4.f);
    nearShape->setTag(4201);
    Body3D  *middleBody  = world->newBody("static", 3.f, 0.f, 0.f);
    Shape3D *middleShape = middleBody->newBoxShape(0.5f, 4.f, 4.f);
    middleShape->setTag(4202);
    Body3D  *farBody  = world->newBody("static", 6.f, 0.f, 0.f);
    Shape3D *farShape = farBody->newBoxShape(0.5f, 4.f, 4.f);
    farShape->setTag(4203);

    REQUIRE_EQ(world->castSphereAll(-4.f, 0.f, 0.f, 0.25f, 12.f, 0.f, 0.f, 8), 3);
    CHECK_EQ(world->getShapeCastResultCount(), 3);
    CHECK_EQ(world->getShapeCastResultBodyId(0), nearBody->getId());
    CHECK_EQ(world->getShapeCastResultBodyId(1), middleBody->getId());
    CHECK_EQ(world->getShapeCastResultBodyId(2), farBody->getId());
    CHECK_EQ(world->getShapeCastResultShapeId(0), nearShape->getId());
    CHECK_EQ(world->getShapeCastResultShapeTag(1), 4202);
    CHECK_LT(world->getShapeCastResultFraction(0), world->getShapeCastResultFraction(1));
    CHECK_LT(world->getShapeCastResultFraction(1), world->getShapeCastResultFraction(2));
    CHECK_LT(world->getShapeCastResultNormalX(0), -0.9f);
    CHECK(std::fabs(world->getShapeCastResultY(0)) < 1e-5f);
    CHECK_EQ(world->getShapeCastBodyId(), nearBody->getId());

    REQUIRE_EQ(world->castCapsuleAll(-4.f, -0.5f, 0.f, -4.f, 0.5f, 0.f, 0.2f, 12.f, 0.f, 0.f, 2), 2);
    CHECK_EQ(world->getShapeCastResultBodyId(0), nearBody->getId());
    CHECK_EQ(world->getShapeCastResultBodyId(1), middleBody->getId());

    world->setQueryIgnoredBodyId(nearBody->getId());
    REQUIRE_EQ(world->castBoxAll(-4.f, 0.f, 0.f, 0.4f, 0.4f, 0.4f, 0.f, 0.f, 0.f, 1.f, 12.f, 0.f, 0.f, 8), 2);
    CHECK_EQ(world->getShapeCastResultBodyId(0), middleBody->getId());
    CHECK_EQ(world->getShapeCastResultShapeId(1), farShape->getId());
    world->clearQueryIgnores();

    CHECK_EQ(world->castSphere(-4.f, 0.f, 0.f, 0.25f, 12.f, 0.f, 0.f), nearBody->getId());
    CHECK_EQ(world->getShapeCastResultCount(), 1);
    CHECK_EQ(world->getShapeCastResultShapeId(0), nearShape->getId());
    CHECK_THROWS((world->castSphereAll(-4.f, 0.f, 0.f, 0.25f, 12.f, 0.f, 0.f, 0), false));
    CHECK_EQ(world->getShapeCastResultCount(), 0);
    CHECK_THROWS((world->getShapeCastResultBodyId(0), false));
}

TEST_CASE("box3d.query.orientedBoxOverlap") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D                  *target = world->newBody("static", 1.2f, 0.f, 0.f);
    target->newSphereShape(0.1f);

    CHECK_EQ(world->queryBox(0.f, 0.f, 0.f, 3.f, 0.4f, 0.4f, 0.f, 0.f, 0.f, 1.f), 1);
    CHECK_EQ(world->getQueryBodyId(0), target->getId());

    constexpr float sin45 = 0.70710678f;
    // A 90-degree yaw turns the long local X axis onto world Z, so it no longer reaches x=1.2.
    CHECK_EQ(world->queryBox(0.f, 0.f, 0.f, 3.f, 0.4f, 0.4f, 0.f, sin45, 0.f, sin45), 0);
    CHECK_THROWS((world->queryBox(0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f), false));
    CHECK_THROWS((world->queryBox(0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f, 0.f, 0.f, 1.f), false));
}

TEST_CASE("box3d.query.orientedBoxCast") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D                  *wall = world->newBody("static", 2.f, 0.f, 0.f);
    wall->newBoxShape(0.5f, 4.f, 4.f);

    CHECK_EQ(world->castBox(-1.f, 0.f, 0.f, 2.f, 0.4f, 0.4f, 0.f, 0.f, 0.f, 1.f, 4.f, 0.f, 0.f), wall->getId());
    const float identityFraction = world->getShapeCastFraction();
    CHECK(std::fabs(identityFraction - 0.4375f) < 0.01f);
    CHECK(world->getShapeCastNormalX() < -0.99f);

    constexpr float sin45 = 0.70710678f;
    CHECK_EQ(world->castBox(-1.f, 0.f, 0.f, 2.f, 0.4f, 0.4f, 0.f, sin45, 0.f, sin45, 4.f, 0.f, 0.f), wall->getId());
    CHECK(world->getShapeCastFraction() > identityFraction + 0.15f);
    CHECK(world->getShapeCastFraction() < 0.65f);
}

TEST_CASE("box3d.query.closestPointAndLayerFilter") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D                  *sphere      = world->newBody("static", 2.f, 0.f, 0.f);
    Shape3D                 *sphereShape = sphere->newSphereShape(0.5f);
    sphereShape->setCategoryBits(2);
    sphereShape->setTag(201);
    Body3D  *box      = world->newBody("static", -3.f, 0.f, 0.f);
    Shape3D *boxShape = box->newBoxShape(1.f, 2.f, 2.f);
    boxShape->setCategoryBits(4);
    boxShape->setTag(202);

    CHECK_EQ(world->closestPoint(0.f, 0.f, 0.f, 10.f), sphere->getId());
    CHECK(world->hasClosestPoint());
    CHECK_EQ(world->getClosestBodyId(), sphere->getId());
    CHECK_EQ(world->getClosestShapeId(), sphereShape->getId());
    CHECK_EQ(world->getClosestShapeTag(), 201);
    CHECK(std::fabs(world->getClosestX() - 1.5f) < 1e-5f);
    CHECK(std::fabs(world->getClosestDistance() - 1.5f) < 1e-5f);
    CHECK(world->getClosestNormalX() < -0.999f);
    CHECK(std::fabs(world->getClosestNormalY()) < 1e-5f);

    world->setQueryFilter(1, 4);
    CHECK_EQ(world->closestPoint(0.f, 0.f, 0.f, 10.f), box->getId());
    CHECK_EQ(world->getClosestShapeId(), boxShape->getId());
    CHECK_EQ(world->getClosestShapeTag(), 202);
    CHECK(std::fabs(world->getClosestX() + 2.5f) < 1e-5f);
    CHECK(std::fabs(world->getClosestDistance() - 2.5f) < 1e-5f);
    CHECK(world->getClosestNormalX() > 0.999f);

    world->resetQueryFilter();
    CHECK_EQ(world->closestPoint(2.f, 0.f, 0.f, 1.f), sphere->getId());
    CHECK(std::fabs(world->getClosestDistance()) < 1e-6f);
    CHECK(std::fabs(world->getClosestNormalX()) < 1e-6f);
    CHECK_EQ(world->closestPoint(0.f, 5.f, 0.f, 1.f), -1);
    CHECK(!world->hasClosestPoint());
    CHECK_EQ(world->getClosestBodyId(), -1);
    CHECK_EQ(world->getClosestShapeId(), -1);
    CHECK_EQ(world->getClosestShapeTag(), 0);
    CHECK_THROWS((world->closestPoint(0.f, 0.f, 0.f, -1.f), false));
}

TEST_CASE("box3d.mover.capsuleSlidesAlongWall") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    Body3D                  *wall = world->newBody("static", 0.f, 0.f, 0.f);
    wall->newBoxShape(1.f, 6.f, 10.f);

    CHECK(world->moveCapsule(-2.f, -1.f, 0.f, -2.f, 1.f, 0.f, 0.25f, 4.f, 0.f, 2.f));
    CHECK(world->getMoverDeltaX() > 1.15f);
    CHECK(world->getMoverDeltaX() < 1.3f);
    CHECK(world->getMoverDeltaZ() > 1.9f);
    CHECK(world->getMoverNormalX() < -0.9f);
    CHECK_GT(world->getMoverPlaneCount(), 0);
    CHECK_GE(world->getMoverIterations(), 2);
}

TEST_CASE("box3d.mover.capsuleStopsAtCornerAndMovesFreely") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    world->newBody("static", 0.f, 0.f, -2.f)->newBoxShape(1.f, 6.f, 6.f);
    world->newBody("static", -2.f, 0.f, 0.f)->newBoxShape(6.f, 6.f, 1.f);

    CHECK(world->moveCapsule(-2.f, -1.f, -2.f, -2.f, 1.f, -2.f, 0.25f, 4.f, 0.f, 4.f));
    CHECK(world->getMoverDeltaX() > 1.15f);
    CHECK(world->getMoverDeltaX() < 1.3f);
    CHECK(world->getMoverDeltaZ() > 1.15f);
    CHECK(world->getMoverDeltaZ() < 1.3f);

    CHECK(!world->moveCapsule(-5.f, -1.f, -5.f, -5.f, 1.f, -5.f, 0.25f, -1.f, 0.f, -2.f));
    CHECK(std::fabs(world->getMoverDeltaX() + 1.f) < 1e-5f);
    CHECK(std::fabs(world->getMoverDeltaZ() + 2.f) < 1e-5f);
}

TEST_CASE("box3d.mover.recoversInitialPenetration") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    world->newBody("static", 0.f, 0.f, 0.f)->newBoxShape(1.f, 6.f, 6.f);

    // The wall face is x=-0.5 and the capsule reaches to x=-0.35: 0.15m penetration.
    CHECK(world->moveCapsule(-0.6f, -1.f, 0.f, -0.6f, 1.f, 0.f, 0.25f, 0.f, 0.f, 0.f));
    CHECK(world->getMoverDeltaX() < -0.14f);
    CHECK(world->getMoverDeltaX() > -0.17f);
    CHECK(world->getMoverNormalX() < -0.9f);
    CHECK_GT(world->getMoverPlaneCount(), 0);
}

TEST_CASE("box3d.mover.groundAndSlopeClassification") {
    auto                    *mod = Physics::create();
    std::unique_ptr<World3D> world(mod->newWorld3D(0.f, 0.f, 0.f, true));
    world->newBody("static", 0.f, -0.5f, 0.f)->newBoxShape(10.f, 1.f, 10.f);

    world->setMoverUp(0.f, 1.f, 1.f);  // normalized internally; floor dot is sqrt(1/2)
    world->setMoverSlopeLimit(40.f);
    CHECK(world->moveCapsule(0.f, 0.5f, 0.f, 0.f, 1.5f, 0.f, 0.25f, 0.f, -1.f, 0.f));
    CHECK(!world->isMoverGrounded());
    CHECK(world->getMoverGroundDot() > 0.70f);
    CHECK(world->getMoverGroundDot() < 0.72f);

    world->setMoverSlopeLimit(50.f);
    CHECK(world->moveCapsule(0.f, 0.5f, 0.f, 0.f, 1.5f, 0.f, 0.25f, 0.f, -1.f, 0.f));
    CHECK(world->isMoverGrounded());
}
}  // namespace test_box3d

// Contract section: climbing
#include "PrivateTestScope.h"

#include <algorithm>
#include "animation/AnimClip.h"
#include "climbing/Climbing.h"
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif
// climbing.cpp
namespace test_climbing {
#if defined(_MSC_VER)

#endif

namespace {

#if defined(_MSC_VER)
std::size_t trackedHeapAllocations = 0;
bool        trackHeapAllocations   = false;

[[maybe_unused]] int __cdecl climbingAllocationHook(int allocationType, void *, std::size_t, int, long,
                                                    const unsigned char *, int) {
    if (trackHeapAllocations && allocationType == _HOOK_ALLOC) ++trackedHeapAllocations;
    return 1;
}
#endif

[[maybe_unused]] eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return std::move(duration).takeValue();
}

[[maybe_unused]] eve::climbing::ClimbingActionDefinition mantleAction(std::string id = "core:mantle") {
    return {std::move(id), 0.4f, 1.2f, 0.f, seconds(0.6), 0.6f, 0.7f, 0};
}

[[maybe_unused]] eve::climbing::ClimbingActionDefinition ledgeGrabAction() {
    auto action       = mantleAction("core:ledge_grab");
    action.kind       = eve::climbing::ClimbingActionKind::LedgeGrab;
    action.apexHeight = 0.1f;
    return action;
}

struct ClimbingWorld {
    ClimbingWorld() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        obstacle = world->newBody("static", 0.f, 0.5f, 1.f);
        shape    = obstacle->newBoxShape(2.f, 1.f, 0.5f);
    }

    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D                  *obstacle = nullptr;
    eve::physics::Shape3D                 *shape    = nullptr;
};

}  // namespace


TEST_CASE("climbing.probeReturnsOwningHandlesAndRestoresPhysicsQueryState") {
    ClimbingWorld fixture;
    fixture.world->setQueryFilter(7, 13);
    fixture.world->setQueryIgnoredBodyId(99);
    fixture.world->setQueryIgnoredShapeId(101);

    eve::climbing::ClimbingRuntime runtime;
    auto                           added = runtime.upsertAction(mantleAction());
    REQUIRE(added.ok());
    auto candidates = runtime.probe(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1});
    REQUIRE(candidates.ok());
    REQUIRE_EQ(candidates.value().size(), std::size_t{1});
    const auto &candidate = candidates.value().front();
    CHECK_EQ(candidate.actionId, std::string("core:mantle"));
    CHECK(candidate.world == fixture.world->runtimeHandle());
    CHECK(candidate.obstacleBody == fixture.obstacle->runtimeHandle());
    CHECK(candidate.obstacleShape == fixture.shape->runtimeHandle());
    CHECK(std::fabs(candidate.obstacleHeight - 1.f) < 0.02f);
    CHECK_EQ(fixture.world->getQueryCategoryBits(), 7);
    CHECK_EQ(fixture.world->getQueryMaskBits(), 13);
    CHECK_EQ(fixture.world->getQueryIgnoredBodyId(), 99);
    CHECK_EQ(fixture.world->getQueryIgnoredShapeId(), 101);
}

TEST_CASE("climbing.selectionIsDeterministicAcrossDefinitionInsertionOrder") {
    ClimbingWorld fixture;
    auto          select = [&](bool reverse) {
        eve::climbing::ClimbingRuntime runtime;
        auto                           preferred = mantleAction("core:mantle");
        auto                           fallback  = mantleAction("core:vault");
        fallback.selectionBias                   = 2;
        auto first                               = runtime.upsertAction(reverse ? fallback : preferred);
        REQUIRE(first.ok());
        auto second = runtime.upsertAction(reverse ? preferred : fallback);
        REQUIRE(second.ok());
        auto candidates = runtime.probe(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1});
        REQUIRE(candidates.ok());
        REQUIRE_EQ(candidates.value().size(), std::size_t{2});
        return candidates.value().front().actionId;
    };
    CHECK_EQ(select(false), std::string("core:mantle"));
    CHECK_EQ(select(true), std::string("core:mantle"));
}

TEST_CASE("climbing.selectionProfileWeightsAndInputModeDriveQuantizedCost") {
    ClimbingWorld fixture;
    const auto    scoreFor = [&](eve::climbing::ClimbingScoreWeights weights, eve::climbing::ClimbingInputMode mode,
                              eve::climbing::Vec3 forward = {0.f, 0.f, 1.f}) {
        eve::climbing::ClimbingRuntime runtime;
        eve::climbing::ClimbingProfile profile;
        profile.scoreWeights = weights;
        profile.actions.push_back(mantleAction());
        REQUIRE(runtime.setProfile(std::move(profile)).ok());
        eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, forward, 1.f, -1};
        pose.moveIntent = {1.f, 0.f, 0.f};
        pose.lookIntent = {0.f, 0.f, 1.f};
        pose.inputMode  = mode;
        auto candidates = runtime.probe(*fixture.world, pose);
        REQUIRE(candidates.ok());
        REQUIRE_EQ(candidates.value().size(), std::size_t(1));
        return candidates.value().front().score;
    };

    eve::climbing::ClimbingScoreWeights zero{};
    zero.direction = zero.approachSpeed = zero.height = zero.distance = 0;
    zero.warpTranslation = zero.warpRotation = zero.intentMismatch = 0;
    CHECK_EQ(scoreFor(zero, eve::climbing::ClimbingInputMode::Flow), std::int64_t(0));

    auto translation            = zero;
    translation.warpTranslation = 1000;
    CHECK(scoreFor(translation, eve::climbing::ClimbingInputMode::Flow) > 0);

    auto rotation         = zero;
    rotation.warpRotation = 1000;
    CHECK(scoreFor(rotation, eve::climbing::ClimbingInputMode::Flow, {0.35f, 0.f, 0.93675f}) > 0);

    auto mismatch            = zero;
    mismatch.intentMismatch  = 1000;
    const auto flowCost      = scoreFor(mismatch, eve::climbing::ClimbingInputMode::Flow);
    const auto precisionCost = scoreFor(mismatch, eve::climbing::ClimbingInputMode::Precision);
    CHECK(flowCost > precisionCost);
    CHECK_EQ(precisionCost, std::int64_t(0));
}

TEST_CASE("climbing.selectionRepetitionPenaltyAppliesAfterCommittedExecution") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    auto                           action = mantleAction();
    action.repetitionPenalty              = 321;
    REQUIRE(runtime.upsertAction(std::move(action)).ok());
    const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1};
    auto                              before = runtime.probe(*fixture.world, pose);
    REQUIRE(before.ok());
    REQUIRE_EQ(before.value().size(), std::size_t(1));
    const auto beforeScore = before.value().front().score;
    REQUIRE(runtime.tryBegin(*fixture.world, pose, eve::SimulationTick(1)).ok());
    REQUIRE(runtime.cancel(eve::climbing::ClimbingCancelReason::PlayerRequest, eve::SimulationTick(1)).ok());
    auto after = runtime.probe(*fixture.world, pose);
    REQUIRE(after.ok());
    REQUIRE_EQ(after.value().size(), std::size_t(1));
    CHECK_EQ(after.value().front().score - beforeScore, std::int64_t(321));
}

TEST_CASE("climbing.executionRejectsDuplicateTicksAndSupportsExplicitCancellation") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    auto                           added = runtime.upsertAction(mantleAction());
    REQUIRE(added.ok());
    const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1};
    auto                              begun = runtime.tryBegin(*fixture.world, pose, eve::SimulationTick(10));
    REQUIRE(begun.ok());
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Requested));

    auto duplicate = runtime.advance(*fixture.world, {eve::SimulationTick(10), seconds(1.0 / 60.0)});
    CHECK(!duplicate.ok());
    CHECK_EQ(static_cast<int>(duplicate.code()), static_cast<int>(eve::StatusCode::Conflict));
    auto cancelled = runtime.cancel(eve::climbing::ClimbingCancelReason::PlayerRequest, eve::SimulationTick(10));
    REQUIRE(cancelled.ok());
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Cancelled));
}

TEST_CASE("climbing.rootMotionWarpRespectsPerTickTranslationAndFacingCaps") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    eve::climbing::ClimbingProfile profile;
    auto                           action = mantleAction();
    action.maxTranslationWarpPerTick      = 0.05f;
    action.maxYawWarpRadiansPerTick       = 0.04f;
    action.horizontalWarpBudget           = 0.2f;
    action.verticalWarpBudget             = 0.2f;
    action.facingWarpBudgetRadians        = 0.2f;
    profile.actions.push_back(action);
    REQUIRE(runtime.setProfile(profile).ok());
    REQUIRE(
        runtime.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, eve::SimulationTick(10)).ok());

    eve::climbing::ClimbingMotionInput motion;
    motion.hasRootMotion = true;
    motion.facing        = {1.f, 0.f, 0.f};
    auto advanced        = runtime.advance(*fixture.world, {eve::SimulationTick(11), seconds(0.1)}, motion);
    REQUIRE(advanced.ok());
    const auto &warp = advanced.value().appliedWarp;
    CHECK(std::sqrt(warp.x * warp.x + warp.y * warp.y + warp.z * warp.z) <= 0.0501f);
    CHECK(std::fabs(advanced.value().desiredYawDelta) <= 0.0401f);
    CHECK(advanced.value().executionId == runtime.executionId());
}

TEST_CASE("climbing.invalidPelvisMotionDoesNotConsumeSimulationTick") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(mantleAction()).ok());
    REQUIRE(
        runtime.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, eve::SimulationTick(20)).ok());

    eve::climbing::ClimbingMotionInput invalid;
    invalid.pelvisOffset = {0.f, 0.36f, 0.f};
    auto rejected        = runtime.advance(*fixture.world, {eve::SimulationTick(21), seconds(0.1)}, invalid);
    CHECK(!rejected.ok());

    auto retried = runtime.advance(*fixture.world, {eve::SimulationTick(21), seconds(0.1)});
    REQUIRE(retried.ok());
    CHECK_EQ(retried.value().executionId.value(), std::uint64_t(1));
}

TEST_CASE("climbing.semanticNotifiesDrivePerHandContactsAndCollisionRequest") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(mantleAction()).ok());
    REQUIRE(
        runtime.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, eve::SimulationTick(40)).ok());

    eve::climbing::ClimbingMotionInput left;
    left.notifies = {eve::climbing::ClimbingNotifyKind::ContactLeftHand};
    auto first    = runtime.advance(*fixture.world, {eve::SimulationTick(41), seconds(0.1)}, left);
    REQUIRE(first.ok());
    auto firstEvents = runtime.drainEvents();
    REQUIRE(firstEvents.ok());
    CHECK_EQ(firstEvents.value().size(), std::size_t(2));  // started, then left contact
    CHECK(std::fabs(first.value().leftHandWeight - 1.f) < 1e-6f);
    CHECK(std::fabs(first.value().rightHandWeight) < 1e-6f);

    eve::climbing::ClimbingMotionInput right;
    right.notifies = {eve::climbing::ClimbingNotifyKind::ContactLeftHand,
                      eve::climbing::ClimbingNotifyKind::ContactRightHand,
                      eve::climbing::ClimbingNotifyKind::CollisionCompact};
    auto second    = runtime.advance(*fixture.world, {eve::SimulationTick(42), seconds(0.1)}, right);
    REQUIRE(second.ok());
    auto secondEvents = runtime.drainEvents();
    REQUIRE(secondEvents.ok());
    REQUIRE_EQ(secondEvents.value().size(), std::size_t(1));
    CHECK_EQ(static_cast<int>(secondEvents.value().front().kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::ContactRightHand));
    CHECK(std::fabs(second.value().leftHandWeight - 1.f) < 1e-6f);
    CHECK(std::fabs(second.value().rightHandWeight - 1.f) < 1e-6f);
    CHECK(second.value().compactCollisionRequested);
    CHECK(second.value().compactCollisionActive);

    eve::climbing::ClimbingMotionInput branch;
    branch.notifies = {eve::climbing::ClimbingNotifyKind::BranchOpen, eve::climbing::ClimbingNotifyKind::BranchClose,
                       eve::climbing::ClimbingNotifyKind::BranchOpen};
    auto third      = runtime.advance(*fixture.world, {eve::SimulationTick(43), seconds(0.1)}, branch);
    REQUIRE(third.ok());
    CHECK(!third.value().compactCollisionRequested);
    CHECK(third.value().compactCollisionActive);
    CHECK(third.value().branchWindowOpen);

    eve::climbing::ClimbingMotionInput landed;
    landed.notifies = {eve::climbing::ClimbingNotifyKind::Land};
    auto fourth     = runtime.advance(*fixture.world, {eve::SimulationTick(44), seconds(0.1)}, landed);
    REQUIRE(fourth.ok());
    CHECK(std::fabs(fourth.value().contactWeight) < 1e-6f);
    CHECK(std::fabs(fourth.value().leftHandWeight) < 1e-6f);
    CHECK(std::fabs(fourth.value().rightHandWeight) < 1e-6f);
    CHECK(!fourth.value().compactCollisionActive);
    CHECK(!fourth.value().branchWindowOpen);
}

TEST_CASE("climbing.definitionWindowsDriveBranchAndContactWeightsWithoutNotifies") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    auto                           action = mantleAction();
    action.duration                       = seconds(1.0);
    action.branchWindows                  = {{0.2f, 0.3f, "combo:follow_up"}};
    action.comboTags                      = {"combo:follow_up"};
    action.contactConstraints             = {
        {eve::climbing::ClimbingContactTarget::LeftHand, 0.2f, 0.3f, 0.7f},
        {eve::climbing::ClimbingContactTarget::LeftFoot, 0.2f, 0.3f, 0.5f},
        {eve::climbing::ClimbingContactTarget::Pelvis, 0.2f, 0.3f, 0.4f},
    };
    REQUIRE(runtime.upsertAction(std::move(action)).ok());
    REQUIRE(
        runtime.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, eve::SimulationTick(100)).ok());

    auto before = runtime.advance(*fixture.world, {eve::SimulationTick(101), seconds(0.1)});
    REQUIRE(before.ok());
    CHECK(!before.value().branchWindowOpen);
    CHECK(before.value().leftHandWeight == 0.f);

    auto inside = runtime.advance(*fixture.world, {eve::SimulationTick(102), seconds(0.15)});
    REQUIRE(inside.ok());
    CHECK(inside.value().branchWindowOpen);
    CHECK_EQ(inside.value().branchComboTag, std::string("combo:follow_up"));
    CHECK(std::fabs(inside.value().leftHandWeight - 0.7f) < 0.001f);
    CHECK(std::fabs(inside.value().leftFootWeight - 0.5f) < 0.001f);
    CHECK(std::fabs(inside.value().pelvisWeight - 0.4f) < 0.001f);

    auto after = runtime.advance(*fixture.world, {eve::SimulationTick(103), seconds(0.1)});
    REQUIRE(after.ok());
    CHECK(!after.value().branchWindowOpen);
    CHECK(after.value().leftHandWeight == 0.f);
}

TEST_CASE("climbing.landingAndTerminalVelocityPoliciesPublishAuthoritativeVelocity") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    auto                           action = mantleAction();
    action.duration                       = seconds(0.1);
    action.landingPolicy                  = eve::climbing::ClimbingLandingPolicy::Stop;
    action.terminalVelocityPolicy         = eve::climbing::ClimbingTerminalVelocityPolicy::Preserve;
    REQUIRE(runtime.upsertAction(std::move(action)).ok());
    REQUIRE(
        runtime.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, eve::SimulationTick(200)).ok());

    auto completed = runtime.advance(*fixture.world, {eve::SimulationTick(201), seconds(0.1)});
    REQUIRE(completed.ok());
    CHECK(static_cast<int>(completed.value().phase) == static_cast<int>(eve::climbing::ClimbingPhase::Completed));
    CHECK(completed.value().hasTerminalVelocity);
    CHECK(completed.value().terminalVelocity.x == 0.f);
    CHECK(completed.value().terminalVelocity.y == 0.f);
    CHECK(completed.value().terminalVelocity.z == 0.f);
}

TEST_CASE("climbing.runtimeCountersExposeBoundedProbeTelemetryWithDebugDisabled") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    runtime.setDebugCapture(eve::climbing::ClimbingDebugCapture::Disabled);
    REQUIRE(runtime.upsertAction(mantleAction()).ok());
    eve::climbing::ClimbingCandidateSet candidates;
    REQUIRE(runtime
                .probeInto(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, candidates,
                           eve::SimulationTick(300))
                .ok());
    const auto counters = runtime.counters();
    CHECK(static_cast<int>(counters.workload) == static_cast<int>(eve::climbing::ClimbingWorkload::CandidateProbe));
    CHECK(counters.queryCount <= eve::climbing::ClimbingQueryBudgets::CandidateProbe);
    CHECK_EQ(counters.broadPhaseQueryCount, std::uint32_t{1});
    CHECK(counters.broadPhaseHitCount >= 1);
    CHECK(counters.selectedCost == candidates.front().score);
    const auto summary = runtime.telemetry().summarize(eve::climbing::ClimbingWorkload::CandidateProbe);
    CHECK(summary.sampleCount == 1);
    CHECK(summary.maxQueryCount == counters.queryCount);

    runtime.recordOrdinaryTick(eve::SimulationTick(301));
    CHECK(runtime.counters().queryCount == 0);
    CHECK(runtime.counters().queryBudget == eve::climbing::ClimbingQueryBudgets::Ordinary);
}

TEST_CASE("climbing.probeDebugOffWarmPathPerformsNoPerTickHeapAllocation") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    runtime.setDebugCapture(eve::climbing::ClimbingDebugCapture::Disabled);
    REQUIRE(runtime.upsertAction(mantleAction("parkour:vault_long_identity_that_exceeds_small_string_storage")).ok());
    const eve::climbing::ClimbingPose   pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1};
    eve::climbing::ClimbingCandidateSet output;
    for (std::uint64_t tick = 1; tick <= 4; ++tick)
        REQUIRE(runtime.probeInto(*fixture.world, pose, output, eve::SimulationTick(tick)).ok());

#if defined(_MSC_VER)
    trackedHeapAllocations             = 0;
    const _CRT_ALLOC_HOOK previousHook = _CrtSetAllocHook(&climbingAllocationHook);
    trackHeapAllocations               = true;
    auto probed                        = runtime.probeInto(*fixture.world, pose, output, eve::SimulationTick(5));
    trackHeapAllocations               = false;
    _CrtSetAllocHook(previousHook);
    REQUIRE(probed.ok());
    CHECK_EQ(trackedHeapAllocations, std::size_t(0));
#else
    // The fixed-capacity storage path is exercised on every platform; MSVC additionally proves CRT allocation count.
    REQUIRE(runtime.probeInto(*fixture.world, pose, output, eve::SimulationTick(5)).ok());
#endif
    REQUIRE_EQ(output.size(), std::size_t(1));
}

TEST_CASE("climbing.authoredMotionFailsExplicitlyWhenWarpCannotConverge") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    eve::climbing::ClimbingProfile profile;
    auto                           action = mantleAction();
    action.duration                       = seconds(0.2);
    action.maxTranslationWarpPerTick      = 0.01f;
    action.horizontalWarpBudget           = 0.01f;
    action.verticalWarpBudget             = 0.01f;
    profile.maxWarpResidual               = 0.02f;
    profile.actions.push_back(action);
    REQUIRE(runtime.setProfile(profile).ok());
    REQUIRE(
        runtime.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, eve::SimulationTick(30)).ok());

    eve::climbing::ClimbingMotionInput motion;
    motion.hasRootMotion = true;
    auto advanced        = runtime.advance(*fixture.world, {eve::SimulationTick(31), seconds(0.2)}, motion);
    REQUIRE(advanced.ok());
    CHECK_EQ(static_cast<int>(advanced.value().phase), static_cast<int>(eve::climbing::ClimbingPhase::Failed));
    CHECK_EQ(runtime.inspect().terminalCode, std::string("climbing.warp.budget_exceeded"));
    CHECK(runtime.executionId().isZero());
}

TEST_CASE("climbing.ledgeGrabTransitionsToHangAndDrop") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    auto                           added = runtime.upsertAction(ledgeGrabAction());
    REQUIRE(added.ok());
    const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1, -1.f, false};
    auto                              begun = runtime.tryBegin(*fixture.world, pose, eve::SimulationTick(10));
    REQUIRE(begun.ok());
    CHECK_EQ(static_cast<int>(begun.value().kind), static_cast<int>(eve::climbing::ClimbingActionKind::LedgeGrab));
    CHECK_NE(static_cast<int>(begun.value().support), static_cast<int>(eve::climbing::HangSupport::None));

    const auto probeDebug = runtime.inspect();
    CHECK(probeDebug.queryCount >= 4);

    for (std::uint64_t tick = 11; tick <= 16; ++tick) {
        auto advanced = runtime.advance(*fixture.world, {eve::SimulationTick(tick), seconds(0.1)});
        REQUIRE(advanced.ok());
    }
    auto events = runtime.drainEvents();
    REQUIRE(events.ok());
    const bool sawHangingEvent = std::any_of(events.value().begin(), events.value().end(), [](const auto &event) {
        return event.kind == eve::climbing::ClimbingEventKind::Hanging;
    });
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Hanging));
    CHECK(sawHangingEvent);
    auto debug = runtime.inspect();
    CHECK_EQ(debug.candidates.size(), std::size_t{1});
    CHECK_EQ(debug.queryCount, std::uint64_t{1});
    CHECK_EQ(debug.queryCount, runtime.counters().queryCount);
    CHECK(!debug.queries.empty());
    CHECK(!debug.evidence.empty());
    CHECK(!debug.motion.empty());
    CHECK_EQ(debug.evidence.front().code, std::string("accepted"));
    runtime.setDebugCapture(eve::climbing::ClimbingDebugCapture::Disabled);
    auto disabledDebug = runtime.inspect();
    CHECK(disabledDebug.candidates.empty());
    CHECK(disabledDebug.queries.empty());
    CHECK(disabledDebug.evidence.empty());
    CHECK(disabledDebug.motion.empty());

    auto dropped = runtime.drop(eve::SimulationTick(17));
    REQUIRE(dropped.ok());
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Dropping));
    auto falling = runtime.advance(*fixture.world, {eve::SimulationTick(18), seconds(1.0 / 60.0)});
    REQUIRE(falling.ok());
    CHECK(falling.value().desiredDelta.y < 0.f);
    auto dropEvents = runtime.drainEvents();
    REQUIRE(dropEvents.ok());
    REQUIRE_EQ(dropEvents.value().size(), std::size_t(1));
    CHECK_EQ(static_cast<int>(dropEvents.value().front().kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::Dropped));
}

TEST_CASE("climbing.activeExecutionCancelsWhenDynamicAnchorBecomesStale") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    auto                           added = runtime.upsertAction(ledgeGrabAction());
    REQUIRE(added.ok());
    auto begun = runtime.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1, -1.f, false},
                                  eve::SimulationTick(20));
    REQUIRE(begun.ok());
    fixture.world->destroyBody(fixture.obstacle);
    fixture.obstacle = nullptr;
    fixture.shape    = nullptr;
    auto advanced    = runtime.advance(*fixture.world, {eve::SimulationTick(21), seconds(1.0 / 60.0)});
    CHECK(!advanced.ok());
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Cancelled));
    CHECK_EQ(runtime.inspect().terminalCode, std::string("climbing.anchor.stale"));
    auto events = runtime.drainEvents();
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), std::size_t(2));
    CHECK_EQ(static_cast<int>(events.value().back().kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::Cancelled));
}

TEST_CASE("climbing.animationNotifyContractIsValidatedBeforeExecution") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    auto                           action = mantleAction("core:notify_mantle");
    action.requiredNotifies               = {"contact.left_hand", "contact.right_hand", "land"};
    REQUIRE(runtime.upsertAction(action).ok());

    eve::animation::AnimClip incomplete("incomplete-mantle");
    incomplete.setDuration(0.6f);
    incomplete.addEvent(0.2f, "contact.left_hand");
    auto invalid = runtime.validateAnimationBinding(action.id, incomplete);
    CHECK(!invalid.ok());
    auto rejected =
        runtime.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, eve::SimulationTick(30));
    CHECK(!rejected.ok());

    eve::animation::AnimClip complete("complete-mantle");
    complete.setDuration(0.6f);
    complete.addEvent(0.2f, "contact.left_hand");
    complete.addEvent(0.3f, "contact.right_hand");
    complete.addEvent(0.55f, "land");
    REQUIRE(runtime.validateAnimationBinding(action.id, complete).ok());
    auto begun = runtime.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, eve::SimulationTick(31));
    REQUIRE(begun.ok());
}
}  // namespace test_climbing

// Contract section: climbing anchor execution
#include "PrivateTestScope.h"

#include <string>
// climbing_anchor_execution.cpp
namespace test_climbing_anchor_execution {
namespace {

eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return std::move(duration).takeValue();
}

eve::climbing::ClimbingActionDefinition action(std::string id, eve::climbing::ClimbingActionKind kind) {
    eve::climbing::ClimbingActionDefinition result;
    result.id         = std::move(id);
    result.kind       = kind;
    result.minHeight  = 0.f;
    result.maxHeight  = 3.f;
    result.duration   = seconds(0.1);
    result.apexHeight = kind == eve::climbing::ClimbingActionKind::LedgeJump ? 0.35f : 0.f;
    return result;
}

eve::climbing::ClimbingAnchorNodeDefinition node(std::string id, eve::climbing::Vec3 position,
                                                 eve::climbing::ClimbingAnchorKind kind) {
    eve::climbing::ClimbingAnchorNodeDefinition result;
    result.id              = std::move(id);
    result.kind            = kind;
    result.localPosition   = position;
    result.localNormal     = {0.f, 0.f, -1.f};
    result.localTangent    = {1.f, 0.f, 0.f};
    result.leftHandSocket  = {position.x - 0.2f, position.y, position.z};
    result.rightHandSocket = {position.x + 0.2f, position.y, position.z};
    result.feetSocket      = {position.x, position.y - 1.f, position.z - 0.2f};
    result.tags            = {"route"};
    return result;
}

eve::climbing::ClimbingAnchorEdgeDefinition edge(std::string from, std::string to,
                                                 eve::climbing::ClimbingAnchorEdgeKind kind) {
    eve::climbing::ClimbingAnchorEdgeDefinition result;
    result.from = std::move(from);
    result.to   = std::move(to);
    result.kind = kind;
    return result;
}

eve::climbing::ClimbingAnchorGraphDefinition ledgeGraph() {
    eve::climbing::ClimbingAnchorGraphDefinition graph;
    graph.id                      = "test:ledge-route";
    graph.sourceGeometryContentId = "test:geometry-v1";
    graph.buildSettingsHash       = "test:settings-v1";
    graph.nodes                   = {
        node("ledge:left", {-0.5f, 1.f, -0.25f}, eve::climbing::ClimbingAnchorKind::Ledge),
        node("ledge:right", {0.5f, 1.f, -0.25f}, eve::climbing::ClimbingAnchorKind::Ledge),
        node("corner:outer", {1.f, 1.f, 0.25f}, eve::climbing::ClimbingAnchorKind::CornerOuter),
    };
    graph.edges = {
        edge("ledge:left", "ledge:right", eve::climbing::ClimbingAnchorEdgeKind::Shimmy),
        edge("ledge:right", "corner:outer", eve::climbing::ClimbingAnchorEdgeKind::Corner),
    };
    return graph;
}

eve::climbing::ClimbingAnchorGraphDefinition ladderGraph() {
    eve::climbing::ClimbingAnchorGraphDefinition graph;
    graph.id                      = "test:ladder-route";
    graph.sourceGeometryContentId = "test:geometry-v1";
    graph.buildSettingsHash       = "test:settings-v1";
    graph.nodes                   = {
        node("ladder:bottom", {0.f, 0.f, -0.25f}, eve::climbing::ClimbingAnchorKind::LadderRung),
        node("ladder:top", {0.f, 1.25f, -0.25f}, eve::climbing::ClimbingAnchorKind::LadderRung),
        node("ladder:exit", {0.6f, 0.2f, -0.25f}, eve::climbing::ClimbingAnchorKind::Ledge),
    };
    graph.edges = {
        edge("ladder:bottom", "ladder:top", eve::climbing::ClimbingAnchorEdgeKind::Climb),
        edge("ladder:top", "ladder:exit", eve::climbing::ClimbingAnchorEdgeKind::Dismount),
    };
    return graph;
}

eve::climbing::ClimbingAnchorGraphDefinition parkourGraph() {
    eve::climbing::ClimbingAnchorGraphDefinition graph;
    graph.id                      = "test:parkour-route";
    graph.sourceGeometryContentId = "test:parkour-geometry-v1";
    graph.buildSettingsHash       = "test:parkour-settings-v1";
    graph.nodes                   = {
        node("beam:a", {-0.5f, 2.f, -0.25f}, eve::climbing::ClimbingAnchorKind::Beam),
        node("beam:b", {0.5f, 2.f, -0.25f}, eve::climbing::ClimbingAnchorKind::Beam),
        node("pole:a", {-0.5f, 1.f, -0.25f}, eve::climbing::ClimbingAnchorKind::Pole),
        node("bar:a", {0.5f, 1.f, -0.25f}, eve::climbing::ClimbingAnchorKind::Bar),
    };
    graph.edges = {
        edge("beam:a", "beam:b", eve::climbing::ClimbingAnchorEdgeKind::Balance),
        edge("pole:a", "bar:a", eve::climbing::ClimbingAnchorEdgeKind::Swing),
    };
    return graph;
}

struct AnchorWorld {
    AnchorWorld() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        body  = world->newBody("kinematic", 0.f, 1.f, 1.f);
        shape = body->newBoxShape(4.f, 4.f, 0.5f);
    }

    eve::climbing::ClimbingPose poseFor(const eve::climbing::ClimbingActionDefinition &start,
                                        eve::climbing::Vec3                            localNode) const {
        const eve::climbing::Vec3 worldNode{localNode.x, localNode.y + 1.f, localNode.z + 1.f};
        return {
            {worldNode.x, worldNode.y - start.hangFeetBelowLedge, worldNode.z - (0.3f + 0.03f + start.hangBodyOffset)},
            {0.f, 0.f, 1.f},
            0.f,
            body->getId(),
            0.f,
            false};
    }

    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D                  *body  = nullptr;
    eve::physics::Shape3D                 *shape = nullptr;
};

struct GraphLease {
    eve::climbing::ClimbingAnchorGraphHandleRef handle;
    ~GraphLease() {
        if (handle.isValid()) {
            auto released = eve::climbing::Climbing::releaseAnchorGraph(handle);
            released.ignore("test graph lease cleanup");
        }
    }
};

eve::climbing::ClimbingAnchorNodeRef nodeRef(eve::climbing::ClimbingAnchorGraphHandleRef handle, std::string_view id) {
    auto graph = eve::climbing::Climbing::resolveAnchorGraph(handle);
    REQUIRE(graph.isBound());
    auto reference = graph->nodeRef(id);
    REQUIRE(reference.ok());
    return std::move(reference).takeValue();
}

void advanceToEnd(eve::climbing::ClimbingRuntime &runtime, eve::physics::World3D &world, std::uint64_t tick) {
    auto advanced = runtime.advance(world, {eve::SimulationTick(tick), seconds(0.1)});
    REQUIRE(advanced.ok());
}

}  // namespace

TEST_CASE("climbing.anchorExecution.shimmyAndCornerKeepOneAtomicReservation") {
    AnchorWorld fixture;
    auto        graphResult =
        eve::climbing::Climbing::newAnchorGraph(ledgeGraph(), *fixture.world, fixture.body->runtimeHandle());
    REQUIRE(graphResult.ok());
    GraphLease lease{std::move(graphResult).takeValue()};
    auto       graph = eve::climbing::Climbing::resolveAnchorGraph(lease.handle);
    REQUIRE(graph.isBound());

    auto grab   = action("climbing:ledge_grab", eve::climbing::ClimbingActionKind::LedgeGrab);
    auto shimmy = action("climbing:shimmy", eve::climbing::ClimbingActionKind::Shimmy);
    auto corner = action("climbing:corner_outer", eve::climbing::ClimbingActionKind::CornerOuter);
    eve::climbing::ClimbingProfile profile;
    profile.maxObstacleHeight      = 3.f;
    profile.pathValidationSegments = 4;
    profile.actions                = {grab, shimmy, corner};

    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.setProfile(profile).ok());
    const auto left = nodeRef(lease.handle, "ledge:left");
    REQUIRE(runtime
                .tryBeginAnchor(*fixture.world, lease.handle, left, eve::climbing::ClimbingAnchorAgentId(1001), grab.id,
                                fixture.poseFor(grab, {-0.5f, 1.f, -0.25f}), eve::SimulationTick(1))
                .ok());
    CHECK_EQ(graph->reservationCount(), std::size_t(1));
    advanceToEnd(runtime, *fixture.world, 2);
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Hanging));

    const auto right = nodeRef(lease.handle, "ledge:right");
    REQUIRE(runtime
                .transitionAnchor(*fixture.world, right, eve::climbing::ClimbingAnchorEdgeKind::Shimmy, shimmy.id,
                                  eve::SimulationTick(3))
                .ok());
    CHECK_EQ(graph->reservationCount(), std::size_t(1));
    advanceToEnd(runtime, *fixture.world, 4);
    auto current = runtime.currentAnchor();
    REQUIRE(current.ok());
    CHECK_EQ(current.value().nodeId, std::string("ledge:right"));

    const auto cornerNode = nodeRef(lease.handle, "corner:outer");
    REQUIRE(runtime
                .transitionAnchor(*fixture.world, cornerNode, eve::climbing::ClimbingAnchorEdgeKind::Corner, corner.id,
                                  eve::SimulationTick(5))
                .ok());
    CHECK_EQ(graph->reservationCount(), std::size_t(1));
    advanceToEnd(runtime, *fixture.world, 6);
    current = runtime.currentAnchor();
    REQUIRE(current.ok());
    CHECK_EQ(current.value().nodeId, std::string("corner:outer"));
    CHECK_EQ(graph->reservationCount(), std::size_t(1));

    REQUIRE(runtime.cancel(eve::climbing::ClimbingCancelReason::PlayerRequest, eve::SimulationTick(6)).ok());
    CHECK_EQ(graph->reservationCount(), std::size_t(0));
    auto events = runtime.drainEvents();
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), std::size_t(7));
    CHECK_EQ(static_cast<int>(events.value()[0].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Started));
    CHECK_EQ(static_cast<int>(events.value()[1].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Hanging));
    CHECK_EQ(static_cast<int>(events.value()[2].kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::AnchorTransitionStarted));
    CHECK_EQ(static_cast<int>(events.value()[3].kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::AnchorReached));
    CHECK_EQ(static_cast<int>(events.value()[6].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Cancelled));
}

TEST_CASE("climbing.anchorExecution.beamPoleAndBarUseExplicitStablePhases") {
    AnchorWorld fixture;
    auto        graphResult =
        eve::climbing::Climbing::newAnchorGraph(parkourGraph(), *fixture.world, fixture.body->runtimeHandle());
    REQUIRE(graphResult.ok());
    GraphLease lease{std::move(graphResult).takeValue()};

    auto beam = action("parkour:beam_balance", eve::climbing::ClimbingActionKind::BeamBalance);
    auto pole = action("parkour:pole_swing", eve::climbing::ClimbingActionKind::PoleSwing);
    auto bar  = action("parkour:bar_swing", eve::climbing::ClimbingActionKind::BarSwing);
    eve::climbing::ClimbingProfile profile;
    profile.maxObstacleHeight      = 4.f;
    profile.pathValidationSegments = 4;
    profile.actions                = {beam, pole, bar};

    eve::climbing::ClimbingRuntime beamRuntime;
    REQUIRE(beamRuntime.setProfile(profile).ok());
    auto                        beamStart = nodeRef(lease.handle, "beam:a");
    eve::climbing::ClimbingPose beamPose{{-0.5f, 3.f, 0.75f}, {1.f, 0.f, 0.f}, 0.f, fixture.body->getId(), 0.f, true};
    REQUIRE(beamRuntime
                .tryBeginAnchor(*fixture.world, lease.handle, beamStart, eve::climbing::ClimbingAnchorAgentId(2001),
                                beam.id, beamPose, eve::SimulationTick(1))
                .ok());
    advanceToEnd(beamRuntime, *fixture.world, 2);
    CHECK_EQ(static_cast<int>(beamRuntime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Balanced));
    auto beamTarget = nodeRef(lease.handle, "beam:b");
    REQUIRE(beamRuntime
                .transitionAnchor(*fixture.world, beamTarget, eve::climbing::ClimbingAnchorEdgeKind::Balance, beam.id,
                                  eve::SimulationTick(3))
                .ok());
    advanceToEnd(beamRuntime, *fixture.world, 4);
    CHECK_EQ(static_cast<int>(beamRuntime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Balanced));
    REQUIRE(beamRuntime.cancel(eve::climbing::ClimbingCancelReason::PlayerRequest, eve::SimulationTick(5)).ok());

    eve::climbing::ClimbingRuntime swingRuntime;
    REQUIRE(swingRuntime.setProfile(profile).ok());
    auto poleStart = nodeRef(lease.handle, "pole:a");
    REQUIRE(swingRuntime
                .tryBeginAnchor(*fixture.world, lease.handle, poleStart, eve::climbing::ClimbingAnchorAgentId(2002),
                                pole.id, fixture.poseFor(pole, {-0.5f, 1.f, -0.25f}), eve::SimulationTick(10))
                .ok());
    advanceToEnd(swingRuntime, *fixture.world, 11);
    CHECK_EQ(static_cast<int>(swingRuntime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Swinging));
    auto barTarget = nodeRef(lease.handle, "bar:a");
    REQUIRE(swingRuntime
                .transitionAnchor(*fixture.world, barTarget, eve::climbing::ClimbingAnchorEdgeKind::Swing, bar.id,
                                  eve::SimulationTick(12))
                .ok());
    advanceToEnd(swingRuntime, *fixture.world, 13);
    CHECK_EQ(static_cast<int>(swingRuntime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Swinging));
}

TEST_CASE("climbing.anchorExecution.ladderMountClimbAndDismountShareOneExecution") {
    AnchorWorld fixture;
    auto        graphResult =
        eve::climbing::Climbing::newAnchorGraph(ladderGraph(), *fixture.world, fixture.body->runtimeHandle());
    REQUIRE(graphResult.ok());
    GraphLease lease{std::move(graphResult).takeValue()};
    auto       graph = eve::climbing::Climbing::resolveAnchorGraph(lease.handle);
    REQUIRE(graph.isBound());

    auto mount    = action("climbing:ladder_mount", eve::climbing::ClimbingActionKind::LadderMount);
    auto climb    = action("climbing:ladder_climb", eve::climbing::ClimbingActionKind::LadderClimb);
    auto dismount = action("climbing:ladder_dismount", eve::climbing::ClimbingActionKind::LadderDismount);
    eve::climbing::ClimbingProfile profile;
    profile.maxObstacleHeight      = 3.f;
    profile.pathValidationSegments = 4;
    profile.actions                = {mount, climb, dismount};

    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.setProfile(profile).ok());
    const auto bottom = nodeRef(lease.handle, "ladder:bottom");
    REQUIRE(runtime
                .tryBeginAnchor(*fixture.world, lease.handle, bottom, eve::climbing::ClimbingAnchorAgentId(2001),
                                mount.id, fixture.poseFor(mount, {0.f, 0.f, -0.25f}), eve::SimulationTick(10))
                .ok());
    const auto executionId = runtime.executionId();
    advanceToEnd(runtime, *fixture.world, 11);

    const auto top = nodeRef(lease.handle, "ladder:top");
    REQUIRE(runtime
                .transitionAnchor(*fixture.world, top, eve::climbing::ClimbingAnchorEdgeKind::Climb, climb.id,
                                  eve::SimulationTick(12))
                .ok());
    advanceToEnd(runtime, *fixture.world, 13);
    CHECK(runtime.executionId() == executionId);
    CHECK_EQ(graph->reservationCount(), std::size_t(1));

    const auto exit = nodeRef(lease.handle, "ladder:exit");
    REQUIRE(runtime
                .transitionAnchor(*fixture.world, exit, eve::climbing::ClimbingAnchorEdgeKind::Dismount, dismount.id,
                                  eve::SimulationTick(14))
                .ok());
    advanceToEnd(runtime, *fixture.world, 15);
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Completed));
    CHECK(runtime.executionId().isZero());
    CHECK_EQ(graph->reservationCount(), std::size_t(0));
}

TEST_CASE("climbing.anchorExecution.reloadAndDestructionCannotLeakOccupancy") {
    AnchorWorld fixture;
    auto        graphResult =
        eve::climbing::Climbing::newAnchorGraph(ledgeGraph(), *fixture.world, fixture.body->runtimeHandle());
    REQUIRE(graphResult.ok());
    GraphLease lease{std::move(graphResult).takeValue()};
    auto       graph = eve::climbing::Climbing::resolveAnchorGraph(lease.handle);
    REQUIRE(graph.isBound());
    auto       grab = action("climbing:ledge_grab", eve::climbing::ClimbingActionKind::LedgeGrab);
    const auto left = nodeRef(lease.handle, "ledge:left");

    {
        eve::climbing::ClimbingRuntime runtime;
        REQUIRE(runtime.upsertAction(grab).ok());
        REQUIRE(runtime
                    .tryBeginAnchor(*fixture.world, lease.handle, left, eve::climbing::ClimbingAnchorAgentId(3001),
                                    grab.id, fixture.poseFor(grab, {-0.5f, 1.f, -0.25f}), eve::SimulationTick(20))
                    .ok());
        CHECK_EQ(graph->reservationCount(), std::size_t(1));
    }
    CHECK_EQ(graph->reservationCount(), std::size_t(0));

    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(grab).ok());
    REQUIRE(runtime
                .tryBeginAnchor(*fixture.world, lease.handle, left, eve::climbing::ClimbingAnchorAgentId(3002), grab.id,
                                fixture.poseFor(grab, {-0.5f, 1.f, -0.25f}), eve::SimulationTick(30))
                .ok());
    auto reload = graph->reload(ledgeGraph());
    REQUIRE(reload.ok());
    REQUIRE_EQ(reload.value().invalidatedOccupants.size(), std::size_t(1));
    CHECK_EQ(reload.value().invalidatedOccupants.front().agentId.value(), std::uint64_t(3002));
    auto stale = runtime.advance(*fixture.world, {eve::SimulationTick(31), seconds(0.1)});
    CHECK(!stale.ok());
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Cancelled));
    CHECK_EQ(runtime.inspect().terminalCode, std::string("climbing.anchor.stale"));
    CHECK_EQ(graph->reservationCount(), std::size_t(0));
}

TEST_CASE("climbing.anchorExecution.snapshotTransfersOrReacquiresOneExclusiveClaim") {
    AnchorWorld fixture;
    auto        graphResult =
        eve::climbing::Climbing::newAnchorGraph(ledgeGraph(), *fixture.world, fixture.body->runtimeHandle());
    REQUIRE(graphResult.ok());
    GraphLease lease{std::move(graphResult).takeValue()};
    auto       graph = eve::climbing::Climbing::resolveAnchorGraph(lease.handle);
    REQUIRE(graph.isBound());
    auto       grab = action("climbing:ledge_grab", eve::climbing::ClimbingActionKind::LedgeGrab);
    const auto left = nodeRef(lease.handle, "ledge:left");

    eve::climbing::ClimbingRuntime source;
    REQUIRE(source.upsertAction(grab).ok());
    REQUIRE(source
                .tryBeginAnchor(*fixture.world, lease.handle, left, eve::climbing::ClimbingAnchorAgentId(4001), grab.id,
                                fixture.poseFor(grab, {-0.5f, 1.f, -0.25f}), eve::SimulationTick(40))
                .ok());
    advanceToEnd(source, *fixture.world, 41);
    auto snapshot = source.snapshot();
    REQUIRE(snapshot.ok());

    eve::climbing::ClimbingRuntime restored;
    REQUIRE(restored.restore(snapshot.value(), *fixture.world).ok());
    CHECK_EQ(graph->reservationCount(), std::size_t(1));
    CHECK_EQ(static_cast<int>(restored.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Hanging));
    auto sourceStale = source.advance(*fixture.world, {eve::SimulationTick(42), seconds(0.1)});
    CHECK(!sourceStale.ok());
    CHECK_EQ(static_cast<int>(source.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Cancelled));
    CHECK_EQ(graph->reservationCount(), std::size_t(1));

    eve::climbing::ClimbingRuntime replay;
    auto                           duplicate = replay.restore(snapshot.value(), *fixture.world);
    CHECK(!duplicate.ok());
    CHECK_EQ(static_cast<int>(duplicate.error()->code()), static_cast<int>(eve::DiagnosticCode::Conflict));
    CHECK_EQ(graph->reservationCount(), std::size_t(1));
    REQUIRE(restored.cancel(eve::climbing::ClimbingCancelReason::PlayerRequest, eve::SimulationTick(42)).ok());
    CHECK_EQ(graph->reservationCount(), std::size_t(0));

    eve::Value detachedSnapshot;
    {
        eve::climbing::ClimbingRuntime detachedSource;
        REQUIRE(detachedSource.upsertAction(grab).ok());
        REQUIRE(detachedSource
                    .tryBeginAnchor(*fixture.world, lease.handle, left, eve::climbing::ClimbingAnchorAgentId(4002),
                                    grab.id, fixture.poseFor(grab, {-0.5f, 1.f, -0.25f}), eve::SimulationTick(50))
                    .ok());
        auto captured = detachedSource.snapshot();
        REQUIRE(captured.ok());
        detachedSnapshot = std::move(captured).takeValue();
        CHECK_EQ(graph->reservationCount(), std::size_t(1));
    }
    CHECK_EQ(graph->reservationCount(), std::size_t(0));
    eve::climbing::ClimbingRuntime reacquired;
    REQUIRE(reacquired.restore(detachedSnapshot, *fixture.world).ok());
    CHECK_EQ(graph->reservationCount(), std::size_t(1));
    REQUIRE(reacquired.cancel(eve::climbing::ClimbingCancelReason::PlayerRequest, eve::SimulationTick(50)).ok());
    CHECK_EQ(graph->reservationCount(), std::size_t(0));
}
}  // namespace test_climbing_anchor_execution

// Contract section: climbing anchor graph
#include "PrivateTestScope.h"

#include "climbing/ClimbingAnchorGraph.h"
// climbing_anchor_graph.cpp
namespace test_climbing_anchor_graph {
namespace {

[[maybe_unused]] eve::climbing::ClimbingAnchorGraphDefinition graphDefinition() {
    eve::climbing::ClimbingAnchorNodeDefinition left;
    left.id              = "ledge:left";
    left.localPosition   = {0.f, 1.f, 0.f};
    left.leftHandSocket  = {-0.2f, 1.f, 0.f};
    left.rightHandSocket = {0.2f, 1.f, 0.f};
    left.feetSocket      = {0.f, 0.f, -0.2f};
    left.occupancySlots  = 2;
    left.tags            = {"stone", "shimmy"};

    eve::climbing::ClimbingAnchorNodeDefinition right = left;
    right.id                                          = "ledge:right";
    right.localPosition.x                             = 1.f;
    right.leftHandSocket.x += 1.f;
    right.rightHandSocket.x += 1.f;
    right.feetSocket.x += 1.f;
    right.occupancySlots = 1;

    eve::climbing::ClimbingAnchorEdgeDefinition edge;
    edge.from          = left.id;
    edge.to            = right.id;
    edge.kind          = eve::climbing::ClimbingAnchorEdgeKind::Shimmy;
    edge.bidirectional = true;
    edge.requiredTags  = {"shimmy"};

    eve::climbing::ClimbingAnchorGraphDefinition graph;
    graph.id                      = "wall:route-a";
    graph.sourceGeometryContentId = "sha256:geometry-a";
    graph.buildSettingsHash       = "sha256:settings-a";
    graph.nodes                   = {std::move(right), std::move(left)};
    graph.edges                   = {std::move(edge)};
    return graph;
}

struct GraphWorld {
    GraphWorld() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        body  = world->newBody("kinematic", 3.f, 2.f, 4.f);
        shape = body->newBoxShape(3.f, 2.f, 0.5f);
    }
    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D                  *body  = nullptr;
    eve::physics::Shape3D                 *shape = nullptr;
};

}  // namespace


TEST_CASE("climbing.anchorGraph.resolvesMovingBodyAndBidirectionalEdges") {
    GraphWorld fixture;
    auto       bound = eve::climbing::ClimbingAnchorGraphInstance::bind(graphDefinition(), *fixture.world,
                                                                        fixture.body->runtimeHandle());
    REQUIRE(bound.ok());
    auto instance = std::move(bound).takeValue();
    auto left     = instance.nodeRef("ledge:left");
    REQUIRE(left.ok());
    auto resolved = instance.resolveNode(*fixture.world, left.value());
    REQUIRE(resolved.ok());
    CHECK(std::fabs(resolved.value().position.x - 3.f) < 1e-5f);
    CHECK(std::fabs(resolved.value().position.y - 3.f) < 1e-5f);
    CHECK(std::fabs(resolved.value().position.z - 4.f) < 1e-5f);

    fixture.body->setPosition(5.f, 2.f, 1.f);
    auto moved = instance.resolveNode(*fixture.world, left.value());
    REQUIRE(moved.ok());
    CHECK(std::fabs(moved.value().position.x - 5.f) < 1e-5f);
    CHECK(std::fabs(moved.value().position.z - 1.f) < 1e-5f);

    auto right = instance.nodeRef("ledge:right");
    REQUIRE(right.ok());
    auto reverse = instance.edgesFrom(right.value());
    REQUIRE(reverse.ok());
    REQUIRE_EQ(reverse.value().size(), std::size_t(1));
    CHECK_EQ(reverse.value().front().to, std::string("ledge:left"));
}

TEST_CASE("climbing.anchorGraph.reservationSlotsAndReloadAreTransactional") {
    GraphWorld fixture;
    auto       bound = eve::climbing::ClimbingAnchorGraphInstance::bind(graphDefinition(), *fixture.world,
                                                                        fixture.body->runtimeHandle());
    REQUIRE(bound.ok());
    auto instance = std::move(bound).takeValue();
    auto node     = instance.nodeRef("ledge:left");
    REQUIRE(node.ok());
    const eve::climbing::ClimbingAnchorAgentId agent(42);
    auto first  = instance.reserve(node.value(), {agent, eve::climbing::ClimbingExecutionId(7)});
    auto second = instance.reserve(node.value(), {agent, eve::climbing::ClimbingExecutionId(8)});
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value().slot, std::uint32_t(0));
    CHECK_EQ(second.value().slot, std::uint32_t(1));
    auto full = instance.reserve(node.value(), {agent, eve::climbing::ClimbingExecutionId(9)});
    CHECK(!full.ok());

    auto forged = first.value();
    forged.slot = 1;
    CHECK(!instance.release(forged).ok());
    CHECK_EQ(instance.reservationCount(), std::size_t(2));
    REQUIRE(instance.release(first.value()).ok());
    auto reused = instance.reserve(node.value(), {agent, eve::climbing::ClimbingExecutionId(9)});
    REQUIRE(reused.ok());
    CHECK_EQ(reused.value().slot, std::uint32_t(0));

    auto invalid                         = graphDefinition();
    invalid.nodes.front().localNormal    = {};
    const std::uint64_t generationBefore = instance.generation();
    CHECK(!instance.reload(std::move(invalid)).ok());
    CHECK_EQ(instance.generation(), generationBefore);
    CHECK_EQ(instance.reservationCount(), std::size_t(2));

    auto reloaded = instance.reload(graphDefinition());
    REQUIRE(reloaded.ok());
    CHECK_EQ(reloaded.value().oldGeneration, generationBefore);
    CHECK_EQ(reloaded.value().newGeneration, generationBefore + 1);
    REQUIRE_EQ(reloaded.value().invalidatedOccupants.size(), std::size_t(2));
    CHECK_EQ(reloaded.value().invalidatedOccupants[0].agentId.value(), std::uint64_t(42));
    CHECK_EQ(reloaded.value().invalidatedOccupants[0].executionId.value(), std::uint64_t(8));
    CHECK_EQ(reloaded.value().invalidatedOccupants[1].executionId.value(), std::uint64_t(9));
    CHECK_EQ(instance.reservationCount(), std::size_t(0));
    CHECK(!instance.resolveNode(*fixture.world, node.value()).ok());
    CHECK(!instance.release(second.value()).ok());
}

TEST_CASE("climbing.anchorGraph.targetDestructionMakesResolutionStale") {
    GraphWorld fixture;
    auto       bound = eve::climbing::ClimbingAnchorGraphInstance::bind(graphDefinition(), *fixture.world,
                                                                        fixture.body->runtimeHandle());
    REQUIRE(bound.ok());
    auto instance = std::move(bound).takeValue();
    auto node     = instance.nodeRef("ledge:left");
    REQUIRE(node.ok());
    fixture.world->destroyBody(fixture.body);
    fixture.body  = nullptr;
    fixture.shape = nullptr;
    auto stale    = instance.resolveNode(*fixture.world, node.value());
    CHECK(!stale.ok());
    CHECK_EQ(static_cast<int>(stale.error()->code()), static_cast<int>(eve::DiagnosticCode::StaleHandle));
}

TEST_CASE("climbing.anchorGraph.nodeReferencesCannotCrossGraphIdentity") {
    GraphWorld fixture;
    auto       firstDefinition  = graphDefinition();
    auto       secondDefinition = graphDefinition();
    secondDefinition.id         = "wall:route-b";
    auto firstBound  = eve::climbing::ClimbingAnchorGraphInstance::bind(std::move(firstDefinition), *fixture.world,
                                                                        fixture.body->runtimeHandle());
    auto secondBound = eve::climbing::ClimbingAnchorGraphInstance::bind(std::move(secondDefinition), *fixture.world,
                                                                        fixture.body->runtimeHandle());
    REQUIRE(firstBound.ok());
    REQUIRE(secondBound.ok());
    auto first   = std::move(firstBound).takeValue();
    auto second  = std::move(secondBound).takeValue();
    auto foreign = second.nodeRef("ledge:left");
    REQUIRE(foreign.ok());
    auto rejected = first.resolveNode(*fixture.world, foreign.value());
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::Conflict));
    CHECK(!first.edgesFrom(foreign.value()).ok());
    CHECK(
        !first
             .reserve(foreign.value(), {eve::climbing::ClimbingAnchorAgentId(1), eve::climbing::ClimbingExecutionId(1)})
             .ok());
}
}  // namespace test_climbing_anchor_graph

// Contract section: climbing anchor route
#include "PrivateTestScope.h"

// climbing_anchor_route.cpp
namespace test_climbing_anchor_route {
namespace {

eve::climbing::ClimbingAnchorNodeDefinition node(std::string id) {
    eve::climbing::ClimbingAnchorNodeDefinition value;
    value.id             = std::move(id);
    value.localPosition  = {0.f, 0.f, 0.f};
    value.localNormal    = {0.f, 0.f, -1.f};
    value.localTangent   = {1.f, 0.f, 0.f};
    value.occupancySlots = 1;
    return value;
}

eve::climbing::ClimbingAnchorEdgeDefinition edge(std::string from, std::string to,
                                                 eve::climbing::ClimbingAnchorEdgeKind kind,
                                                 std::vector<std::string>              requiredTags = {}) {
    eve::climbing::ClimbingAnchorEdgeDefinition value;
    value.from         = std::move(from);
    value.to           = std::move(to);
    value.kind         = kind;
    value.requiredTags = std::move(requiredTags);
    return value;
}

eve::climbing::ClimbingAnchorGraphDefinition routeGraph() {
    eve::climbing::ClimbingAnchorGraphDefinition graph;
    graph.id                      = "test:ai-route";
    graph.sourceGeometryContentId = "test:geometry-v1";
    graph.buildSettingsHash       = "test:settings-v1";
    graph.nodes                   = {node("a"), node("b"), node("c"), node("d")};
    graph.edges                   = {
        edge("a", "c", eve::climbing::ClimbingAnchorEdgeKind::Shimmy),
        edge("c", "d", eve::climbing::ClimbingAnchorEdgeKind::Climb),
        edge("a", "b", eve::climbing::ClimbingAnchorEdgeKind::Shimmy),
        edge("b", "d", eve::climbing::ClimbingAnchorEdgeKind::Climb),
    };
    return graph;
}

struct Fixture {
    Fixture() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        body       = world->newBody("kinematic", 0.f, 0.f, 0.f);
        shape      = body->newBoxShape(1.f, 1.f, 1.f);
        auto bound = eve::climbing::ClimbingAnchorGraphInstance::bind(routeGraph(), *world, body->runtimeHandle());
        REQUIRE(bound.ok());
        graph = std::make_unique<eve::climbing::ClimbingAnchorGraphInstance>(std::move(bound).takeValue());
    }

    eve::climbing::ClimbingAnchorNodeRef ref(std::string_view id) const {
        auto result = graph->nodeRef(id);
        REQUIRE(result.ok());
        return std::move(result).takeValue();
    }

    std::unique_ptr<eve::physics::World3D>                      world;
    eve::physics::Body3D                                       *body  = nullptr;
    eve::physics::Shape3D                                      *shape = nullptr;
    std::unique_ptr<eve::climbing::ClimbingAnchorGraphInstance> graph;
};

}  // namespace

TEST_CASE("climbing.anchorRoute.shortestTieUsesStableNodeIdentity") {
    Fixture                                   fixture;
    eve::climbing::ClimbingAnchorRouteRequest request;
    request.start = fixture.ref("a");
    request.goal  = fixture.ref("d");
    auto route    = fixture.graph->planRoute(request);
    REQUIRE(route.ok());
    REQUIRE_EQ(route.value().nodes.size(), std::size_t(3));
    CHECK_EQ(route.value().nodes[0].nodeId, std::string("a"));
    CHECK_EQ(route.value().nodes[1].nodeId, std::string("b"));
    CHECK_EQ(route.value().nodes[2].nodeId, std::string("d"));
    REQUIRE_EQ(route.value().steps.size(), std::size_t(2));
    CHECK_EQ(static_cast<int>(route.value().steps[1].kind),
             static_cast<int>(eve::climbing::ClimbingAnchorEdgeKind::Climb));
}

TEST_CASE("climbing.anchorRoute.fullNodeReroutesWithoutMutatingOccupancy") {
    Fixture                                     fixture;
    const eve::climbing::ClimbingAnchorOccupant blocker{eve::climbing::ClimbingAnchorAgentId(40),
                                                        eve::climbing::ClimbingExecutionId(4)};
    auto                                        reservation = fixture.graph->reserve(fixture.ref("b"), blocker);
    REQUIRE(reservation.ok());

    eve::climbing::ClimbingAnchorRouteRequest request;
    request.start = fixture.ref("a");
    request.goal  = fixture.ref("d");
    auto route    = fixture.graph->planRoute(request);
    REQUIRE(route.ok());
    REQUIRE_EQ(route.value().nodes.size(), std::size_t(3));
    CHECK_EQ(route.value().nodes[1].nodeId, std::string("c"));
    CHECK_EQ(fixture.graph->reservationCount(), std::size_t(1));

    request.requester = blocker;
    route             = fixture.graph->planRoute(request);
    REQUIRE(route.ok());
    CHECK_EQ(route.value().nodes[1].nodeId, std::string("b"));
    CHECK_EQ(fixture.graph->reservationCount(), std::size_t(1));
}

TEST_CASE("climbing.anchorRoute.filtersBoundsAndGenerationAreExplicit") {
    Fixture                                   fixture;
    eve::climbing::ClimbingAnchorRouteRequest request;
    request.start            = fixture.ref("a");
    request.goal             = fixture.ref("d");
    request.allowedEdgeKinds = {eve::climbing::ClimbingAnchorEdgeKind::Shimmy};
    auto filtered            = fixture.graph->planRoute(request);
    CHECK(!filtered.ok());
    REQUIRE(filtered.error() != nullptr);
    CHECK_EQ(static_cast<int>(filtered.error()->code()), static_cast<int>(eve::DiagnosticCode::NotFound));

    request.allowedEdgeKinds.clear();
    request.maxVisitedNodes = 1;
    auto bounded            = fixture.graph->planRoute(request);
    CHECK(!bounded.ok());
    REQUIRE(bounded.error() != nullptr);
    CHECK_EQ(static_cast<int>(bounded.error()->code()), static_cast<int>(eve::DiagnosticCode::PreconditionViolation));

    request.maxVisitedNodes = 16;
    REQUIRE(fixture.graph->reload(routeGraph()).ok());
    auto stale = fixture.graph->planRoute(request);
    CHECK(!stale.ok());
    REQUIRE(stale.error() != nullptr);
    CHECK_EQ(static_cast<int>(stale.error()->code()), static_cast<int>(eve::DiagnosticCode::StaleHandle));
}

TEST_CASE("climbing.anchorRoute.enforcesAuthoredCapabilityTags") {
    Fixture fixture;
    auto    graph               = routeGraph();
    graph.edges[2].requiredTags = {"long_reach"};
    REQUIRE(fixture.graph->reload(std::move(graph)).ok());

    eve::climbing::ClimbingAnchorRouteRequest request;
    request.start          = fixture.ref("a");
    request.goal           = fixture.ref("d");
    auto withoutCapability = fixture.graph->planRoute(request);
    REQUIRE(withoutCapability.ok());
    CHECK_EQ(withoutCapability.value().nodes[1].nodeId, std::string("c"));

    request.availableTags = {"long_reach"};
    auto withCapability   = fixture.graph->planRoute(request);
    REQUIRE(withCapability.ok());
    CHECK_EQ(withCapability.value().nodes[1].nodeId, std::string("b"));

    request.availableTags = {"long_reach", "long_reach"};
    auto duplicate        = fixture.graph->planRoute(request);
    CHECK(!duplicate.ok());
    REQUIRE(duplicate.error() != nullptr);
    CHECK_EQ(static_cast<int>(duplicate.error()->code()), static_cast<int>(eve::DiagnosticCode::InvalidArgument));
}
}  // namespace test_climbing_anchor_route

// Contract section: climbing definition policy
#include "PrivateTestScope.h"

#include "climbing/ClimbingInput.h"
// climbing_definition_policy.cpp
namespace test_climbing_definition_policy {
namespace {

struct PolicyWorld {
    PolicyWorld() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        player = world->newBody("kinematic", 0.f, 0.9f, 0.f);
        player->newCapsuleShape(1.8f, 0.3f);
        obstacle = world->newBody("static", 0.f, 0.3f, 1.f);
        obstacle->newBoxShape(1.35f, 0.6f, 0.65f);
    }

    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D                  *player   = nullptr;
    eve::physics::Body3D                  *obstacle = nullptr;
};

eve::climbing::ClimbingActionDefinition vault(std::string id) {
    eve::climbing::ClimbingActionDefinition action;
    action.id             = std::move(id);
    action.kind           = eve::climbing::ClimbingActionKind::Vault;
    action.minHeight      = 0.4f;
    action.maxHeight      = 0.8f;
    action.landingForward = 0.75f;
    action.apexHeight     = 0.4f;
    action.tags           = {"parkour"};
    return action;
}

eve::climbing::ClimbingPose groundedPose(const PolicyWorld &fixture) {
    return {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 4.f, fixture.player->getId(), 0.f, true};
}

}  // namespace

TEST_CASE("climbing.definitionPolicy.enforcesSourceTagsAndCandidateBudget") {
    PolicyWorld                              fixture;
    eve::climbing::ClimbingRuntime           runtime;
    eve::climbing::ClimbingProfileDefinition profile;
    profile.maxCandidates     = 1;
    profile.allowedActionTags = {"parkour"};
    auto airborne             = vault("parkour:airborne_first");
    airborne.sourceModes      = eve::climbing::ClimbingSourceMode::Airborne;
    auto grounded             = vault("parkour:grounded_second");
    grounded.sourceModes      = eve::climbing::ClimbingSourceMode::Grounded;
    auto denied               = vault("parkour:denied");
    denied.tags               = {"parkour", "disabled"};
    profile.deniedActionTags  = {"disabled"};
    profile.actions           = {airborne, grounded, denied};
    REQUIRE(runtime.setProfile(profile).ok());

    auto candidates = runtime.probe(*fixture.world, groundedPose(fixture));
    REQUIRE(candidates.ok());
    REQUIRE_EQ(candidates.value().size(), std::size_t{1});
    CHECK_EQ(candidates.value().front().actionId, std::string("parkour:grounded_second"));
}

TEST_CASE("climbing.definitionPolicy.commandIsTransactionalAndCameraCueIsOwningOutput") {
    PolicyWorld                              fixture;
    eve::climbing::ClimbingRuntime           runtime;
    eve::climbing::ClimbingProfileDefinition profile;
    profile.cameraCueProfile = "camera:climbing";
    auto action              = vault("parkour:vault_commanded");
    action.requiredCommand   = eve::climbing::ClimbingCommandRequirement::Jump;
    action.cameraCue         = "camera:vault_low";
    profile.actions.push_back(action);
    REQUIRE(runtime.setProfile(profile).ok());

    eve::climbing::ClimbingIntent intent;
    REQUIRE(eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Climb,
                                                       eve::SimulationTick(10), profile)
                .ok());
    auto mismatch = eve::climbing::ClimbingSelectionSystem::tryStart(runtime, *fixture.world, groundedPose(fixture),
                                                                     intent, eve::climbing::ClimbingCommand::Climb,
                                                                     eve::SimulationTick(10), eve::SimulationTick(10));
    CHECK(!mismatch.ok());
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Idle));
    REQUIRE(
        eve::climbing::ClimbingInputSystem::peek(intent, eve::climbing::ClimbingCommand::Climb, eve::SimulationTick(10))
            .has_value());

    REQUIRE(eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Jump,
                                                       eve::SimulationTick(11), profile)
                .ok());
    auto started = eve::climbing::ClimbingSelectionSystem::tryStart(runtime, *fixture.world, groundedPose(fixture),
                                                                    intent, eve::climbing::ClimbingCommand::Jump,
                                                                    eve::SimulationTick(11), eve::SimulationTick(11));
    REQUIRE(started.ok());
    auto advanced =
        runtime.advance(*fixture.world, {eve::SimulationTick(12), eve::Duration::fromNanoseconds(16666667)});
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value().cameraCueProfile, std::string("camera:climbing"));
    CHECK_EQ(advanced.value().cameraCue, std::string("camera:vault_low"));
}
}  // namespace test_climbing_definition_policy

// Contract section: climbing ecs
#include "PrivateTestScope.h"

#include "climbing/ClimbingECS.h"
// climbing_ecs.cpp
namespace test_climbing_ecs {
namespace {

class TestClimber : public ecs::Entity {
public:
    ENTITY(TestClimber, ecs::Entity)

    void release() override { ecs::DestroyEntity(this); }

    COMPONENT(eve::climbing::ClimbingBody, climbingBody)
    COMPONENT(eve::climbing::ClimbingIntent, climbingIntent)
    COMPONENT(eve::climbing::ClimbingState, climbingState)
    COMPONENT(eve::climbing::ClimbingLinks, climbingLinks)
    COMPONENT(eve::climbing::ClimbingCandidateBuffer, climbingCandidates)
    COMPONENT(eve::climbing::ClimbingPoseProjection, climbingPose)
    COMPONENT(eve::climbing::ClimbingEventBatch, climbingEvents)
};

[[maybe_unused]] eve::climbing::ClimbingActionDefinition vaultAction() {
    eve::climbing::ClimbingActionDefinition action;
    action.id             = "parkour:vault_low";
    action.kind           = eve::climbing::ClimbingActionKind::Vault;
    action.minHeight      = 0.4f;
    action.maxHeight      = 1.2f;
    action.duration       = eve::Duration::fromNanoseconds(600000000);
    action.landingForward = 0.7f;
    action.apexHeight     = 0.7f;
    return action;
}

struct EcsClimbingFixture {
    EcsClimbingFixture() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        character = world->newBody("kinematic", 0.f, 0.9f, 0.f);
        character->newCapsuleShape(1.8f, 0.3f);
        obstacle = world->newBody("static", 0.f, 0.5f, 1.f);
        obstacle->newBoxShape(2.f, 1.f, 0.5f);
    }

    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D                  *character = nullptr;
    eve::physics::Body3D                  *obstacle  = nullptr;
};

}  // namespace

TEST_CASE("climbing.ecs.composesWithExistingRootAcrossFixedPhases") {
    ecs::Table         table;
    ecs::ScopedTable   tableScope(table);
    EcsClimbingFixture fixture;

    TestClimber *entity = TestClimber::create();
    auto         state  = eve::climbing::ClimbingState::create();
    REQUIRE(state.ok());
    *entity->climbingState() = std::move(state).takeValue();

    auto bodyLink = eve::physics::PhysicsLink::fromBody(*fixture.character);
    REQUIRE(bodyLink.ok());
    entity->climbingLinks()->physicsBody = std::move(bodyLink).takeValue();

    auto runtime = eve::climbing::Climbing::resolve(entity->climbingState()->runtime);
    REQUIRE(runtime.isBound());
    REQUIRE(runtime->upsertAction(vaultAction()).ok());

    auto body              = entity->climbingBody();
    body->feet             = {0.f, 0.f, 0.f};
    body->lastStableFeet   = body->feet;
    body->forward          = {0.f, 0.f, 1.f};
    body->velocity         = {0.f, 0.f, 1.f};
    body->grounded         = true;
    body->lastGroundedTick = eve::SimulationTick(10);
    REQUIRE(eve::climbing::ClimbingInputSystem::submit(*entity->climbingIntent(), eve::climbing::ClimbingCommand::Climb,
                                                       eve::SimulationTick(10), 4)
                .ok());

    auto probed = eve::climbing::ClimbingProbeSystem::step<TestClimber>(*fixture.world, eve::SimulationTick(10));
    REQUIRE(probed.ok());
    CHECK_EQ(probed.value(), std::size_t{1});
    CHECK(!entity->climbingCandidates()->values().empty());
    CHECK(entity->climbingCandidates()->tick() == eve::SimulationTick(10));

    auto selected = eve::climbing::ClimbingEcsSelectionSystem::step<TestClimber>(
        *fixture.world, eve::climbing::ClimbingCommand::Climb, eve::SimulationTick(10));
    REQUIRE(selected.ok());
    CHECK_EQ(selected.value(), std::size_t{1});
    CHECK(!runtime->executionId().isZero());
    CHECK(!entity->climbingIntent()->commands.front().consumedExecutionId.isZero());

    const eve::SimulationStep step{eve::SimulationTick(11), eve::Duration::fromNanoseconds(16666667)};
    auto                      advanced = eve::climbing::ClimbingMotionSystem::step<TestClimber>(*fixture.world, step);
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value(), std::size_t{1});
    CHECK(entity->climbingState()->lastAdvanceTick == step.tick);
    CHECK(std::fabs(fixture.character->getY() - (body->feet.y + body->capsuleHeight * 0.5f)) < 1e-4f);

    auto posed = eve::climbing::ClimbingPoseSystem::step<TestClimber>(step.tick);
    REQUIRE(posed.ok());
    CHECK_EQ(posed.value(), std::size_t{1});
    CHECK(entity->climbingPose()->executionId == runtime->executionId());
    REQUIRE(!entity->climbingEvents()->values().empty());
    CHECK_EQ(static_cast<int>(entity->climbingEvents()->values().front().kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::Started));

    REQUIRE(entity->climbingState()->releaseRuntime().ok());
    CHECK(!eve::climbing::Climbing::resolve(entity->climbingState()->runtime).isBound());
    entity->release();
}

TEST_CASE("climbing.ecs.rejectsStalePhysicsLinkWithoutMutatingDerivedCandidates") {
    ecs::Table         table;
    ecs::ScopedTable   tableScope(table);
    EcsClimbingFixture fixture;

    TestClimber *entity = TestClimber::create();
    auto         state  = eve::climbing::ClimbingState::create();
    REQUIRE(state.ok());
    *entity->climbingState() = std::move(state).takeValue();
    auto runtime             = eve::climbing::Climbing::resolve(entity->climbingState()->runtime);
    REQUIRE(runtime.isBound());
    REQUIRE(runtime->upsertAction(vaultAction()).ok());

    auto link = eve::physics::PhysicsLink::fromBody(*fixture.character);
    REQUIRE(link.ok());
    entity->climbingLinks()->physicsBody = std::move(link).takeValue();

    eve::climbing::ClimbingCandidate sentinel;
    sentinel.actionId = "test:sentinel";
    const std::vector<eve::climbing::ClimbingCandidate> oldValues{sentinel};
    REQUIRE(entity->climbingCandidates()->replace(oldValues, eve::SimulationTick(7)).ok());
    fixture.world->destroyBody(fixture.character);
    fixture.character = nullptr;

    auto probed = eve::climbing::ClimbingProbeSystem::step<TestClimber>(*fixture.world, eve::SimulationTick(8));
    CHECK(!probed.ok());
    CHECK_EQ(entity->climbingCandidates()->values().size(), std::size_t{1});
    CHECK_EQ(entity->climbingCandidates()->values().front().actionId, std::string("test:sentinel"));
    CHECK(entity->climbingCandidates()->tick() == eve::SimulationTick(7));

    REQUIRE(entity->climbingState()->releaseRuntime().ok());
    entity->release();
}

TEST_CASE("climbing.ecs.profileDrivesCollisionConstrainedGroundAndAirLocomotion") {
    ecs::Table         table;
    ecs::ScopedTable   tableScope(table);
    EcsClimbingFixture fixture;
    auto              *floor = fixture.world->newBody("static", 0.f, -0.1f, 0.f);
    floor->newBoxShape(20.f, 0.2f, 20.f);

    TestClimber *entity = TestClimber::create();
    auto         state  = eve::climbing::ClimbingState::create();
    REQUIRE(state.ok());
    *entity->climbingState() = std::move(state).takeValue();
    auto link                = eve::physics::PhysicsLink::fromBody(*fixture.character);
    REQUIRE(link.ok());
    entity->climbingLinks()->physicsBody = std::move(link).takeValue();
    auto runtime                         = eve::climbing::Climbing::resolve(entity->climbingState()->runtime);
    REQUIRE(runtime.isBound());

    eve::climbing::ClimbingProfileDefinition profile;
    profile.groundAcceleration = 12.f;
    profile.groundBraking      = 24.f;
    profile.airControl         = 0.25f;
    profile.gravity            = 18.f;
    profile.jumpSpeed          = 6.f;
    REQUIRE(runtime->setProfile(profile).ok());

    auto body                      = entity->climbingBody();
    body->feet                     = {0.f, 0.f, -2.f};
    body->lastStableFeet           = body->feet;
    body->lastGroundedTick         = eve::SimulationTick(1);
    entity->climbingIntent()->move = {3.f, 0.f, 0.f};
    const eve::Duration fixedDelta = eve::Duration::fromNanoseconds(16666667);
    auto                grounded =
        eve::climbing::ClimbingMotionSystem::step<TestClimber>(*fixture.world, {eve::SimulationTick(2), fixedDelta});
    REQUIRE(grounded.ok());
    CHECK_EQ(grounded.value(), std::size_t{1});
    CHECK(body->feet.x > 0.f);
    CHECK(body->velocity.x > 0.f);
    CHECK(body->velocity.x < 3.f);
    CHECK(body->grounded);
    CHECK_EQ(runtime->counters().queryCount, std::uint32_t{1});
    CHECK_EQ(static_cast<int>(runtime->counters().workload),
             static_cast<int>(eve::climbing::ClimbingWorkload::Ordinary));

    REQUIRE(eve::climbing::ClimbingInputSystem::submit(*entity->climbingIntent(), eve::climbing::ClimbingCommand::Jump,
                                                       eve::SimulationTick(3), profile)
                .ok());
    auto jumped =
        eve::climbing::ClimbingMotionSystem::step<TestClimber>(*fixture.world, {eve::SimulationTick(3), fixedDelta});
    REQUIRE(jumped.ok());
    CHECK(!body->grounded);
    CHECK(body->velocity.y > 0.f);
    const float launchedVelocity = body->velocity.y;

    auto airborne =
        eve::climbing::ClimbingMotionSystem::step<TestClimber>(*fixture.world, {eve::SimulationTick(4), fixedDelta});
    REQUIRE(airborne.ok());
    CHECK(body->velocity.y < launchedVelocity);
    const std::uint64_t lastJumpTick = body->lastOrdinaryJumpPressedTick.value();
    CHECK_EQ(lastJumpTick, std::uint64_t{3});

    REQUIRE(entity->climbingState()->releaseRuntime().ok());
    entity->release();
}

TEST_CASE("climbing.ecs.stepHeightUsesBoundedOwningMoverQueries") {
    ecs::Table         table;
    ecs::ScopedTable   tableScope(table);
    EcsClimbingFixture fixture;
    auto              *floor = fixture.world->newBody("static", 0.f, -0.1f, -2.f);
    floor->newBoxShape(8.f, 0.2f, 4.f);
    auto *stepBody = fixture.world->newBody("static", 0.f, 0.1f, -2.f);
    stepBody->newBoxShape(0.5f, 0.2f, 2.f);

    TestClimber *entity = TestClimber::create();
    auto         state  = eve::climbing::ClimbingState::create();
    REQUIRE(state.ok());
    *entity->climbingState() = std::move(state).takeValue();
    auto link                = eve::physics::PhysicsLink::fromBody(*fixture.character);
    REQUIRE(link.ok());
    entity->climbingLinks()->physicsBody = std::move(link).takeValue();
    auto runtime                         = eve::climbing::Climbing::resolve(entity->climbingState()->runtime);
    REQUIRE(runtime.isBound());

    auto body  = entity->climbingBody();
    auto reset = [&] {
        body->feet             = {-0.8f, 0.f, -2.f};
        body->velocity         = {2.f, 0.f, 0.f};
        body->grounded         = true;
        body->lastGroundedTick = eve::SimulationTick(10);
        fixture.character->setPosition(-0.8f, body->capsuleHeight * 0.5f, -2.f);
    };
    entity->climbingIntent()->move = {2.f, 0.f, 0.f};
    const eve::Duration halfSecond = eve::Duration::fromNanoseconds(500000000);

    reset();
    body->stepHeight = 0.f;
    REQUIRE(
        eve::climbing::ClimbingMotionSystem::step<TestClimber>(*fixture.world, {eve::SimulationTick(11), halfSecond})
            .ok());
    const float blockedX = body->feet.x;

    reset();
    body->stepHeight = 0.35f;
    REQUIRE(
        eve::climbing::ClimbingMotionSystem::step<TestClimber>(*fixture.world, {eve::SimulationTick(12), halfSecond})
            .ok());
    CHECK(body->feet.x > blockedX + 0.2f);
    CHECK(body->feet.y > 0.15f);
    CHECK(body->grounded);
    CHECK_EQ(runtime->counters().queryCount, eve::climbing::ClimbingQueryBudgets::Ordinary);
    CHECK(runtime->counters().moverIterations > 0);
    const auto samples = runtime->telemetry().samples();
    REQUIRE(!samples.empty());
    CHECK(samples.back().elapsedNanoseconds > 0);

    REQUIRE(entity->climbingState()->releaseRuntime().ok());
    entity->release();
}
}  // namespace test_climbing_ecs

// Contract section: climbing events
#include "PrivateTestScope.h"

#include "climbing/ClimbingEventValue.h"
// climbing_events.cpp
namespace test_climbing_events {
namespace {

struct EventWorld {
    EventWorld() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        body  = world->newBody("static", 0.f, 0.5f, 1.f);
        shape = body->newBoxShape(2.f, 1.f, 0.5f);
    }
    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D                  *body  = nullptr;
    eve::physics::Shape3D                 *shape = nullptr;
};

eve::climbing::ClimbingActionDefinition action() {
    eve::climbing::ClimbingActionDefinition value;
    value.id             = "climb:event_mantle";
    value.minHeight      = 0.4f;
    value.maxHeight      = 1.2f;
    value.duration       = eve::Duration::fromNanoseconds(600000000);
    value.landingForward = 0.6f;
    value.apexHeight     = 0.7f;
    value.eventMetadata  = {"event:parkour", "surface:stone"};
    return value;
}

eve::climbing::ClimbingPose pose() { return {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}; }

}  // namespace

TEST_CASE("climbing.events.detachBeforeReentrantConsumerMutation") {
    EventWorld                     fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(action()).ok());
    auto first = runtime.tryBegin(*fixture.world, pose(), eve::SimulationTick(10));
    REQUIRE(first.ok());

    auto dispatched = runtime.drainEvents();
    REQUIRE(dispatched.ok());
    REQUIRE_EQ(dispatched.value().size(), std::size_t(1));
    CHECK_EQ(static_cast<int>(dispatched.value().front().kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::Started));
    REQUIRE_EQ(dispatched.value().front().metadata.size(), std::size_t(2));
    CHECK_EQ(dispatched.value().front().metadata.front(), std::string("event:parkour"));

    // This models a post-simulation callback: the batch is already detached, so
    // reentrant mutations append to a fresh queue rather than invalidating iteration.
    for (const auto &event : dispatched.value()) {
        REQUIRE(runtime.cancel(eve::climbing::ClimbingCancelReason::PlayerRequest, event.tick).ok());
        auto second = runtime.tryBegin(*fixture.world, pose(), eve::SimulationTick(event.tick.value() + 1));
        REQUIRE(second.ok());
        CHECK_EQ(second.value().definitionGeneration, first.value().definitionGeneration);
    }
    CHECK_EQ(runtime.executionId().value(), std::uint64_t(2));
    CHECK_EQ(runtime.pendingEventCount(), std::size_t(2));

    auto reentrant = runtime.drainEvents();
    REQUIRE(reentrant.ok());
    REQUIRE_EQ(reentrant.value().size(), std::size_t(2));
    CHECK_EQ(static_cast<int>(reentrant.value()[0].kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::Cancelled));
    CHECK_EQ(static_cast<int>(reentrant.value()[1].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Started));
    CHECK_EQ(reentrant.value()[0].executionId.value(), std::uint64_t(1));
    CHECK_EQ(reentrant.value()[1].executionId.value(), std::uint64_t(2));
}

TEST_CASE("climbing.events.snapshotPreservesUndeliveredOrder") {
    EventWorld                     fixture;
    eve::climbing::ClimbingRuntime source;
    REQUIRE(source.upsertAction(action()).ok());
    REQUIRE(source.tryBegin(*fixture.world, pose(), eve::SimulationTick(20)).ok());
    for (std::uint64_t tick = 21; tick <= 26; ++tick) {
        auto advanced =
            source.advance(*fixture.world, {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(100000000)});
        REQUIRE(advanced.ok());
    }
    CHECK_EQ(static_cast<int>(source.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Completed));

    auto snapshot = source.snapshot();
    REQUIRE(snapshot.ok());
    eve::climbing::ClimbingRuntime restored;
    REQUIRE(restored.restore(snapshot.value(), *fixture.world).ok());
    auto events = restored.drainEvents();
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), std::size_t(3));
    CHECK_EQ(static_cast<int>(events.value()[0].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Started));
    CHECK_EQ(static_cast<int>(events.value()[1].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Landed));
    CHECK_EQ(static_cast<int>(events.value()[2].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Completed));
    CHECK_EQ(events.value()[0].metadata, events.value()[2].metadata);
    CHECK_EQ(events.value()[1].metadata.front(), std::string("event:parkour"));
}

TEST_CASE("climbing.events.capacityBackpressureIsAtomic") {
    EventWorld                     fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.upsertAction(action()).ok());
    std::uint64_t tick = 30;
    for (std::size_t index = 0; index < eve::climbing::ClimbingRuntime::PendingEventCapacity / 2; ++index) {
        REQUIRE(runtime.tryBegin(*fixture.world, pose(), eve::SimulationTick(tick)).ok());
        REQUIRE(runtime.cancel(eve::climbing::ClimbingCancelReason::PlayerRequest, eve::SimulationTick(tick)).ok());
        ++tick;
    }
    CHECK_EQ(runtime.pendingEventCount(), eve::climbing::ClimbingRuntime::PendingEventCapacity);
    const auto phaseBefore = runtime.phase();
    auto       rejected    = runtime.tryBegin(*fixture.world, pose(), eve::SimulationTick(tick));
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.code()), static_cast<int>(eve::StatusCode::Conflict));
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(phaseBefore));
    CHECK(runtime.executionId().isZero());

    auto drained = runtime.drainEvents();
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value().size(), eve::climbing::ClimbingRuntime::PendingEventCapacity);
    REQUIRE(runtime.tryBegin(*fixture.world, pose(), eve::SimulationTick(tick)).ok());
}

/**
 * 事件种类名是**一处**映射（`ClimbingEventValue.cpp` 的表），绑定、快照编解码、脚本 sink
 * 都走它。这里遍历整个取值域做双向验证：新增一个 `ClimbingEventKind` 而忘了更新表（或忘了改
 * `kClimbingEventKindCount`）会在这里红，而不是让脚本绑定与存档各写一个名字。
 */

}  // namespace test_climbing_events

// Contract section: climbing input
#include "PrivateTestScope.h"

// climbing_input.cpp
namespace test_climbing_input {
namespace {

[[maybe_unused]] eve::climbing::ClimbingActionDefinition mantleAction() {
    eve::climbing::ClimbingActionDefinition action;
    action.id        = "parkour:mantle";
    action.minHeight = 0.4f;
    action.maxHeight = 1.2f;
    action.duration  = eve::Duration::fromNanoseconds(600000000);
    return action;
}

struct ClimbingWorld {
    ClimbingWorld() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        obstacle = world->newBody("static", 0.f, 0.5f, 1.f);
        obstacle->newBoxShape(2.f, 1.f, 0.5f);
    }

    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D                  *obstacle = nullptr;
};

}  // namespace


TEST_CASE("climbing.selection.commitsExecutionAndConsumesInputExactlyOnce") {
    ClimbingWorld                            fixture;
    eve::climbing::ClimbingRuntime           runtime;
    eve::climbing::ClimbingIntent            intent;
    eve::climbing::ClimbingProfileDefinition profile;
    profile.actions.push_back(mantleAction());
    profile.inputBufferTicks = 5;
    REQUIRE(runtime.setProfile(profile).ok());
    REQUIRE(eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Climb,
                                                       eve::SimulationTick(10), profile)
                .ok());

    const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1};
    auto started = eve::climbing::ClimbingSelectionSystem::tryStart(runtime, *fixture.world, pose, intent,
                                                                    eve::climbing::ClimbingCommand::Climb,
                                                                    eve::SimulationTick(12), eve::SimulationTick(12));
    REQUIRE(started.ok());
    CHECK(!started.value().executionId.isZero());
    CHECK(started.value().executionId == runtime.executionId());
    REQUIRE_EQ(intent.commands.size(), std::size_t(1));
    CHECK(intent.commands.front().consumedExecutionId == started.value().executionId);

    auto repeated = eve::climbing::ClimbingSelectionSystem::tryStart(runtime, *fixture.world, pose, intent,
                                                                     eve::climbing::ClimbingCommand::Climb,
                                                                     eve::SimulationTick(12), eve::SimulationTick(12));
    CHECK(!repeated.ok());
    CHECK_EQ(static_cast<int>(repeated.error()->code()), static_cast<int>(eve::DiagnosticCode::NotFound));
}

TEST_CASE("climbing.selection.failedPrepareLeavesInputAndAuthorityUnchanged") {
    ClimbingWorld                  fixture;
    eve::climbing::ClimbingRuntime runtime;
    eve::climbing::ClimbingIntent  intent;
    REQUIRE(eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Climb,
                                                       eve::SimulationTick(20), 4)
                .ok());
    const eve::climbing::ClimbingIntent before   = intent;
    auto                                rejected = eve::climbing::ClimbingSelectionSystem::tryStart(
        runtime, *fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, intent,
        eve::climbing::ClimbingCommand::Climb, eve::SimulationTick(21), eve::SimulationTick(21));
    CHECK(!rejected.ok());
    CHECK(intent.commands == before.commands);
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Idle));
    CHECK(runtime.executionId().isZero());
}

TEST_CASE("climbing.selection.appliesProfileCoyoteTicksAtTheBoundary") {
    ClimbingWorld                     fixture;
    const eve::climbing::ClimbingPose airborne{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1, -0.1f, false};

    auto attempt = [&](std::uint64_t tick) {
        eve::climbing::ClimbingRuntime           runtime;
        eve::climbing::ClimbingIntent            intent;
        eve::climbing::ClimbingProfileDefinition profile;
        profile.actions.push_back(mantleAction());
        profile.coyoteTicks = 6;
        REQUIRE(runtime.setProfile(profile).ok());
        REQUIRE(eve::climbing::ClimbingInputSystem::submit(intent, eve::climbing::ClimbingCommand::Climb,
                                                           eve::SimulationTick(tick), profile)
                    .ok());
        auto result = eve::climbing::ClimbingSelectionSystem::tryStart(
            runtime, *fixture.world, airborne, intent, eve::climbing::ClimbingCommand::Climb, eve::SimulationTick(tick),
            eve::SimulationTick(100));
        return std::pair(std::move(result), std::move(intent));
    };

    auto [inside, consumed] = attempt(106);
    REQUIRE(inside.ok());
    CHECK(!consumed.commands.front().consumedExecutionId.isZero());

    auto [outside, retained] = attempt(107);
    CHECK(!outside.ok());
    CHECK(retained.commands.front().consumedExecutionId.isZero());
}
}  // namespace test_climbing_input

// Contract section: climbing parkour
#include "PrivateTestScope.h"

#include <iterator>
#include <utility>
#include "climbing/ClimbingCodec.h"
// climbing_parkour.cpp
namespace test_climbing_parkour {
namespace {

[[maybe_unused]] eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return std::move(duration).takeValue();
}

[[maybe_unused]] eve::climbing::ClimbingActionDefinition action(std::string                       id,
                                                                eve::climbing::ClimbingActionKind kind) {
    eve::climbing::ClimbingActionDefinition result;
    result.id             = std::move(id);
    result.kind           = kind;
    result.minHeight      = 0.f;
    result.maxHeight      = 4.f;
    result.minSpeed       = 2.f;
    result.duration       = seconds(0.2);
    result.landingForward = 1.f;
    result.apexHeight     = 0.f;
    return result;
}

struct ParkourWorld {
    ParkourWorld() { world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false)); }

    std::unique_ptr<eve::physics::World3D> world;
};

}  // namespace

TEST_CASE("climbing.parkour.slideUsesCompactCapsulePath") {
    ParkourWorld fixture;
    auto        *floor = fixture.world->newBody("static", 0.f, -0.25f, 0.f);
    REQUIRE(floor != nullptr);
    REQUIRE(floor->newBoxShape(8.f, 0.5f, 8.f) != nullptr);

    eve::climbing::ClimbingRuntime runtime;
    auto                           slide = action("parkour:slide", eve::climbing::ClimbingActionKind::Slide);
    REQUIRE(runtime.upsertAction(slide).ok());
    const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 5.f, -1, 0.f, true};
    auto                              candidates = runtime.probe(*fixture.world, pose);
    REQUIRE(candidates.ok());
    REQUIRE_EQ(candidates.value().size(), std::size_t(1));
    CHECK_EQ(static_cast<int>(candidates.value().front().kind),
             static_cast<int>(eve::climbing::ClimbingActionKind::Slide));

    REQUIRE(runtime.tryBegin(*fixture.world, pose, eve::SimulationTick(1)).ok());
    eve::climbing::ClimbingMotionInput motion;
    motion.notifies = {eve::climbing::ClimbingNotifyKind::CollisionCompact, eve::climbing::ClimbingNotifyKind::Land};
    auto advanced   = runtime.advance(*fixture.world, {eve::SimulationTick(2), seconds(0.2)}, motion);
    REQUIRE(advanced.ok());
    CHECK_EQ(static_cast<int>(advanced.value().phase), static_cast<int>(eve::climbing::ClimbingPhase::Completed));
    CHECK(advanced.value().compactCollisionRequested);
    CHECK(std::fabs(advanced.value().feet.z - 1.f) < 0.02f);
}

TEST_CASE("climbing.parkour.wallRunChoosesStableSideAndDoesNotEmitLand") {
    ParkourWorld fixture;
    auto        *wall = fixture.world->newBody("static", -0.5f, 1.5f, 1.f);
    REQUIRE(wall != nullptr);
    REQUIRE(wall->newBoxShape(0.2f, 3.f, 6.f) != nullptr);

    eve::climbing::ClimbingRuntime runtime;
    auto                           wallRun = action("parkour:wall_run", eve::climbing::ClimbingActionKind::WallRun);
    REQUIRE(runtime.upsertAction(wallRun).ok());
    const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 6.f, -1, 0.f, false};
    auto                              candidates = runtime.probe(*fixture.world, pose);
    REQUIRE(candidates.ok());
    REQUIRE_EQ(candidates.value().size(), std::size_t(1));
    CHECK_EQ(static_cast<int>(candidates.value().front().kind),
             static_cast<int>(eve::climbing::ClimbingActionKind::WallRun));
    CHECK(candidates.value().front().surfaceNormal.x > 0.9f);
    CHECK(candidates.value().front().surfaceTangent.z > 0.9f);

    REQUIRE(runtime.tryBegin(*fixture.world, pose, eve::SimulationTick(10)).ok());
    auto advanced = runtime.advance(*fixture.world, {eve::SimulationTick(11), seconds(0.2)});
    REQUIRE(advanced.ok());
    CHECK_EQ(static_cast<int>(advanced.value().phase), static_cast<int>(eve::climbing::ClimbingPhase::Completed));
    auto events = runtime.drainEvents();
    REQUIRE(events.ok());
    bool landed    = false;
    bool completed = false;
    for (const auto &event : events.value()) {
        landed    = landed || event.kind == eve::climbing::ClimbingEventKind::Landed;
        completed = completed || event.kind == eve::climbing::ClimbingEventKind::Completed;
    }
    CHECK(!landed);
    CHECK(completed);
}
}  // namespace test_climbing_parkour

// Contract section: climbing performance
#include "PrivateTestScope.h"

#include <cstdio>
// climbing_performance.cpp
namespace test_climbing_performance {
namespace {

class PerformanceClimber : public ecs::Entity {
public:
    ENTITY(PerformanceClimber, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }

    COMPONENT(eve::climbing::ClimbingBody, climbingBody)
    COMPONENT(eve::climbing::ClimbingIntent, climbingIntent)
    COMPONENT(eve::climbing::ClimbingState, climbingState)
    COMPONENT(eve::climbing::ClimbingLinks, climbingLinks)
};

eve::climbing::ClimbingActionDefinition performanceAction(std::string id) {
    eve::climbing::ClimbingActionDefinition action;
    action.id                        = std::move(id);
    action.kind                      = eve::climbing::ClimbingActionKind::Vault;
    action.minHeight                 = 0.4f;
    action.maxHeight                 = 1.2f;
    action.duration                  = eve::Duration::fromNanoseconds(10000000000ll);
    action.landingForward            = 0.75f;
    action.apexHeight                = 0.55f;
    action.maxTranslationWarpPerTick = 1.f;
    action.horizontalWarpBudget      = 100.f;
    action.verticalWarpBudget        = 100.f;
    action.facingWarpBudgetRadians   = 100.f;
    return action;
}

void printSummary(const char *name, const eve::climbing::ClimbingTelemetrySummary &summary) {
    std::printf(
        "climbing.performance workload=%s samples=%u p50_ns=%llu p95_ns=%llu "
        "p50_queries=%u p95_queries=%u max_queries=%u budget_exceeded=%u\n",
        name, summary.sampleCount, static_cast<unsigned long long>(summary.p50Nanoseconds),
        static_cast<unsigned long long>(summary.p95Nanoseconds), summary.p50QueryCount, summary.p95QueryCount,
        summary.maxQueryCount, summary.budgetExceededCount);
}

}  // namespace

TEST_CASE("climbing.performance.reportsOrdinaryCandidateDenseAndActiveP50P95") {
    constexpr std::uint64_t sampleCount = 120;
    const eve::Duration     fixedDelta  = eve::Duration::fromNanoseconds(16666667);

    eve::climbing::ClimbingTelemetrySummary ordinarySummary;
    {
        ecs::Table                             table;
        ecs::ScopedTable                       tableScope(table);
        std::unique_ptr<eve::physics::World3D> world(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        world->newBody("static", 0.f, -0.1f, 0.f)->newBoxShape(20.f, 0.2f, 20.f);
        auto *character = world->newBody("kinematic", 0.f, 0.9f, 0.f);
        character->newCapsuleShape(1.8f, 0.3f);

        PerformanceClimber *entity = PerformanceClimber::create();
        auto                state  = eve::climbing::ClimbingState::create();
        REQUIRE(state.ok());
        *entity->climbingState() = std::move(state).takeValue();
        auto link                = eve::physics::PhysicsLink::fromBody(*character);
        REQUIRE(link.ok());
        entity->climbingLinks()->physicsBody = std::move(link).takeValue();
        auto runtime                         = eve::climbing::Climbing::resolve(entity->climbingState()->runtime);
        REQUIRE(runtime.isBound());
        entity->climbingIntent()->move = {1.f, 0.f, 0.f};
        runtime->clearTelemetry();
        for (std::uint64_t tick = 1; tick <= sampleCount; ++tick)
            REQUIRE(eve::climbing::ClimbingMotionSystem::step<PerformanceClimber>(
                        *world, {eve::SimulationTick(tick), fixedDelta})
                        .ok());
        ordinarySummary = runtime->telemetry().summarize(eve::climbing::ClimbingWorkload::Ordinary);
        REQUIRE(entity->climbingState()->releaseRuntime().ok());
        entity->release();
    }

    eve::climbing::ClimbingTelemetrySummary candidateSummary;
    {
        std::unique_ptr<eve::physics::World3D> world(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        world->newBody("static", 0.f, 0.3f, 1.f)->newBoxShape(1.35f, 0.6f, 0.65f);
        for (int index = 0; index < 16; ++index) {
            const float x = index % 2 == 0 ? -1.35f : 1.35f;
            const float z = -1.2f + static_cast<float>(index / 2) * 0.3f;
            world->newBody("static", x, 0.1f, z)->newBoxShape(0.1f, 0.2f, 0.1f);
        }
        eve::climbing::ClimbingRuntime           runtime;
        eve::climbing::ClimbingProfileDefinition profile;
        profile.maxProbeDistance   = 1.5f;
        profile.maxCandidates      = 8;
        profile.maxTotalWarpBudget = 100.f;
        for (int index = 0; index < 8; ++index)
            profile.actions.push_back(performanceAction("parkour:dense_vault_" + std::to_string(index)));
        REQUIRE(runtime.setProfile(std::move(profile)).ok());
        runtime.clearTelemetry();
        eve::climbing::ClimbingCandidateSet candidates;
        const eve::climbing::ClimbingPose   pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 4.f, -1};
        for (std::uint64_t tick = 1; tick <= sampleCount; ++tick)
            REQUIRE(runtime.probeInto(*world, pose, candidates, eve::SimulationTick(tick)).ok());
        candidateSummary = runtime.telemetry().summarize(eve::climbing::ClimbingWorkload::CandidateProbe);
    }

    eve::climbing::ClimbingTelemetrySummary activeSummary;
    {
        std::unique_ptr<eve::physics::World3D> world(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        world->newBody("static", 0.f, 0.3f, 1.f)->newBoxShape(1.35f, 0.6f, 0.65f);
        eve::climbing::ClimbingRuntime           runtime;
        eve::climbing::ClimbingProfileDefinition profile;
        profile.maxWarpResidual    = 100.f;
        profile.maxTotalWarpBudget = 100.f;
        profile.actions.push_back(performanceAction("parkour:active_vault"));
        REQUIRE(runtime.setProfile(std::move(profile)).ok());
        const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 4.f, -1};
        auto                              begun = runtime.tryBegin(*world, pose, eve::SimulationTick(1));
        if (!begun.ok() && begun.error())
            std::printf("climbing.performance active begin failed: %s\n", begun.error()->message().c_str());
        REQUIRE(begun.ok());
        runtime.clearTelemetry();
        for (std::uint64_t index = 0; index < sampleCount; ++index)
            REQUIRE(runtime.advance(*world, {eve::SimulationTick(index + 2), fixedDelta}).ok());
        activeSummary = runtime.telemetry().summarize(eve::climbing::ClimbingWorkload::Active);
    }

    CHECK_EQ(ordinarySummary.sampleCount, static_cast<std::uint32_t>(sampleCount));
    CHECK_EQ(candidateSummary.sampleCount, static_cast<std::uint32_t>(sampleCount));
    CHECK_EQ(activeSummary.sampleCount, static_cast<std::uint32_t>(sampleCount));
    CHECK(ordinarySummary.p50Nanoseconds > 0);
    CHECK(candidateSummary.p50Nanoseconds > 0);
    CHECK(activeSummary.p50Nanoseconds > 0);
    CHECK_EQ(ordinarySummary.budgetExceededCount, std::uint32_t(0));
    CHECK_EQ(candidateSummary.budgetExceededCount, std::uint32_t(0));
    CHECK_EQ(activeSummary.budgetExceededCount, std::uint32_t(0));
    CHECK(ordinarySummary.maxQueryCount <= eve::climbing::ClimbingQueryBudgets::Ordinary);
    CHECK(candidateSummary.maxQueryCount <= eve::climbing::ClimbingQueryBudgets::CandidateProbe);
    CHECK(activeSummary.maxQueryCount <= eve::climbing::ClimbingQueryBudgets::Active);
    printSummary("ordinary", ordinarySummary);
    printSummary("candidate_dense", candidateSummary);
    printSummary("active", activeSummary);
}
}  // namespace test_climbing_performance

// Contract section: climbing physics policy
#include "PrivateTestScope.h"

// climbing_physics_policy.cpp
namespace test_climbing_physics_policy {
TEST_CASE("climbing.physics.ownedMoverSlopePolicyIsPerCall") {
    auto                                  *physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    world->newBody("static", 0.f, -0.5f, 0.f)->newBoxShape(10.f, 1.f, 10.f);

    eve::physics::CapsuleMovePolicy3D strict;
    strict.upY             = 1.f;
    strict.upZ             = 1.f;
    strict.maxSlopeRadians = 0.69813170f;
    auto notGround         = world->moveCapsuleOwned(0.f, 0.5f, 0.f, 0.f, 1.5f, 0.f, 0.25f, 0.f, -1.f, 0.f, {}, strict);
    REQUIRE(notGround.ok());
    CHECK(!notGround.value().grounded);

    auto permissive            = strict;
    permissive.maxSlopeRadians = 0.87266463f;
    auto ground = world->moveCapsuleOwned(0.f, 0.5f, 0.f, 0.f, 1.5f, 0.f, 0.25f, 0.f, -1.f, 0.f, {}, permissive);
    REQUIRE(ground.ok());
    CHECK(ground.value().grounded);

    auto defaultPolicy = world->moveCapsuleOwned(0.f, 0.5f, 0.f, 0.f, 1.5f, 0.f, 0.25f, 0.f, -1.f, 0.f);
    REQUIRE(defaultPolicy.ok());
    CHECK(defaultPolicy.value().grounded);
}

TEST_CASE("climbing.physics.broadPhaseOwnsStableFilteredShapeEvidence") {
    auto                                  *physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    auto                                  *ignored = world->newBody("static", -1.f, 0.f, 0.f);
    ignored->newBoxShape(0.5f, 0.5f, 0.5f);
    auto *kept      = world->newBody("static", 1.f, 0.f, 0.f);
    auto *keptShape = kept->newBoxShape(0.5f, 0.5f, 0.5f);

    eve::physics::QueryFilter3D filter;
    filter.ignoredBodyId = ignored->getId();
    auto result          = world->queryAabbBroadPhaseOwned(-2.f, -1.f, -1.f, 2.f, 1.f, 1.f, filter);
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().count, std::size_t(1));
    CHECK(!result.value().truncated);
    CHECK(result.value().hits[0].body == kept->runtimeHandle());
    CHECK(result.value().hits[0].shape == keptShape->runtimeHandle());
}
}  // namespace test_climbing_physics_policy
