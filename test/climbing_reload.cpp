#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "climbing/Climbing.h"
#include "climbing/ClimbingCodec.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

#include <memory>

namespace {

struct ReloadWorld {
    ReloadWorld() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        body  = world->newBody("static", 0.f, 0.5f, 1.f);
        shape = body->newBoxShape(2.f, 1.f, 0.5f);
    }
    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D*                  body  = nullptr;
    eve::physics::Shape3D*                 shape = nullptr;
};

eve::climbing::ClimbingProfileDefinition profile(float apexHeight) {
    eve::climbing::ClimbingActionDefinition action;
    action.id             = "mantle";
    action.minHeight      = 0.4f;
    action.maxHeight      = 1.2f;
    action.duration       = eve::Duration::fromNanoseconds(600000000);
    action.landingForward = 0.6f;
    action.apexHeight     = apexHeight;
    eve::climbing::ClimbingProfileDefinition result;
    result.actions.push_back(std::move(action));
    return result;
}

float pinnedApex(const eve::Value& snapshot) {
    const auto* execution = snapshot.find("execution");
    REQUIRE(execution != nullptr);
    const auto* executionObject = execution->getIf<eve::Value::Object>();
    REQUIRE(executionObject != nullptr);
    const auto* action = executionObject->at("action").getIf<eve::Value::Object>();
    REQUIRE(action != nullptr);
    return static_cast<float>(action->at("apexHeight").asDouble());
}

}  // namespace

TEST_CASE("climbing.reload.isAtomicAndPinsActiveDefinition") {
    ReloadWorld                    fixture;
    eve::climbing::ClimbingRuntime runtime;
    REQUIRE(runtime.setProfile(profile(0.5f)).ok());
    const std::uint64_t oldGeneration = runtime.definitionGeneration();
    REQUIRE(
        runtime.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, eve::SimulationTick(10)).ok());

    REQUIRE(runtime.reloadProfile(profile(3.0f)).ok());
    CHECK(runtime.definitionGeneration() > oldGeneration);
    auto active = runtime.snapshot();
    REQUIRE(active.ok());
    CHECK(pinnedApex(active.value()) < 1.f);

    const std::uint64_t generationAfterReload = runtime.definitionGeneration();
    auto                invalid               = profile(2.f);
    invalid.capsuleRadius                     = -1.f;
    CHECK(!runtime.reloadProfile(std::move(invalid)).ok());
    CHECK_EQ(runtime.definitionGeneration(), generationAfterReload);
    auto afterFailure = runtime.snapshot();
    REQUIRE(afterFailure.ok());
    CHECK(afterFailure.value() == active.value());

    REQUIRE(runtime.cancel(eve::climbing::ClimbingCancelReason::PlayerRequest, eve::SimulationTick(10)).ok());
    REQUIRE(
        runtime.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, eve::SimulationTick(11)).ok());
    auto next = runtime.snapshot();
    REQUIRE(next.ok());
    CHECK(pinnedApex(next.value()) > 2.f);
}

TEST_CASE("climbing.reload.jsonProfileAndRuntimeRoundTrip") {
    ReloadWorld                    fixture;
    eve::climbing::ClimbingRuntime source;
    auto                           encodedProfile = eve::climbing::encodeClimbingProfileDefinition(profile(0.75f));
    REQUIRE(encodedProfile.ok());
    auto profileJson = encodedProfile.value().toJson();
    REQUIRE(profileJson.ok());
    REQUIRE(source.setProfileJson(profileJson.value()).ok());
    REQUIRE(source.tryBegin(*fixture.world, {{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 1.f, -1}, eve::SimulationTick(20)).ok());
    auto json = source.snapshotJson();
    REQUIRE(json.ok());

    eve::climbing::ClimbingRuntime restored;
    REQUIRE(restored.restoreJson(json.value(), *fixture.world).ok());
    CHECK_EQ(restored.definitionGeneration(), source.definitionGeneration());
    auto restoredJson = restored.snapshotJson();
    REQUIRE(restoredJson.ok());
    CHECK_EQ(restoredJson.value(), json.value());

    const std::string before = restoredJson.value();
    CHECK(!restored.restoreJson("{\"schemaId\":", *fixture.world).ok());
    auto after = restored.snapshotJson();
    REQUIRE(after.ok());
    CHECK_EQ(after.value(), before);
}
