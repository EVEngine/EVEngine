#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "climbing/Climbing.h"
#include "climbing/ClimbingCodec.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

#include <cmath>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

namespace {

eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return std::move(duration).takeValue();
}

eve::climbing::ClimbingActionDefinition action(std::string id, eve::climbing::ClimbingActionKind kind) {
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
    auto*        floor = fixture.world->newBody("static", 0.f, -0.25f, 0.f);
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
    auto*        wall = fixture.world->newBody("static", -0.5f, 1.5f, 1.f);
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
    for (const auto& event : events.value()) {
        landed    = landed || event.kind == eve::climbing::ClimbingEventKind::Landed;
        completed = completed || event.kind == eve::climbing::ClimbingEventKind::Completed;
    }
    CHECK(!landed);
    CHECK(completed);
}

TEST_CASE("climbing.parkour.highSpeedKindsRoundTripThroughActionCodec") {
    const eve::climbing::ClimbingActionKind kinds[] = {
        eve::climbing::ClimbingActionKind::WallRun,     eve::climbing::ClimbingActionKind::Slide,
        eve::climbing::ClimbingActionKind::BeamBalance, eve::climbing::ClimbingActionKind::PoleSwing,
        eve::climbing::ClimbingActionKind::BarSwing,
    };
    for (std::size_t index = 0; index < std::size(kinds); ++index) {
        auto source  = action("parkour:" + std::to_string(index), kinds[index]);
        auto encoded = eve::climbing::encodeClimbingActionDefinition(source);
        REQUIRE(encoded.ok());
        auto decoded = eve::climbing::decodeClimbingActionDefinition(encoded.value());
        REQUIRE(decoded.ok());
        CHECK_EQ(static_cast<int>(decoded.value().kind), static_cast<int>(kinds[index]));
    }
}
