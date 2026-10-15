#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "climbing/Climbing.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

#include <algorithm>
#include <cmath>
#include <memory>

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace {

#if defined(_MSC_VER)
std::size_t trackedHeapAllocations = 0;
bool        trackHeapAllocations   = false;

[[maybe_unused]] int __cdecl climbingAllocationHook(int allocationType, void*, std::size_t, int, long, const unsigned char*, int) {
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
    eve::physics::Body3D*                  obstacle = nullptr;
    eve::physics::Shape3D*                 shape    = nullptr;
};

}  // namespace

TEST_CASE("climbing.profile.rejectsInvalidCapsuleAndActionDefinitions") {
    eve::climbing::ClimbingRuntime runtime;
    eve::climbing::ClimbingProfile profile;
    profile.capsuleRadius = 0.5f;
    profile.capsuleHeight = 1.f;
    auto invalidProfile   = runtime.setProfile(profile);
    CHECK(!invalidProfile.ok());
    CHECK_EQ(static_cast<int>(invalidProfile.code()), static_cast<int>(eve::StatusCode::Rejected));

    auto action        = mantleAction();
    action.duration    = eve::Duration::zero();
    auto invalidAction = runtime.upsertAction(std::move(action));
    CHECK(!invalidAction.ok());
    CHECK_EQ(static_cast<int>(invalidAction.code()), static_cast<int>(eve::StatusCode::Rejected));
}
