#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/trajectory/BoneCollider.h"
#include "physics/trajectory/TrajectoryCollision.h"

#include "common/AttachmentPoint.h"
#include "common/Identity.h"
#include "common/SubjectRef.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace {

eve::SubjectRef subject(const char* value) {
    auto parsed = eve::PersistentId::parse(value);
    REQUIRE(parsed.has_value());
    return eve::SubjectRef::fromPersistentId(*parsed);
}

/** @brief Deterministic attachment map used as a stand-in for AnimPose / Avatar bones. */
class FixedBones final : public eve::IAttachmentPointSource {
public:
    std::unordered_map<std::string, eve::AttachmentPoint> bones;

    [[nodiscard]] eve::Result<eve::AttachmentPoint> sampleAttachmentPoint(
        std::string_view name, eve::AttachmentPoint localOffset = {}) const override {
        const auto found = bones.find(std::string(name));
        if (found == bones.end())
            return eve::Result<eve::AttachmentPoint>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "test attachment bone was not found", std::string(name)));
        // Treat the authored bone as a pure translation (bind-pose aligned), matching
        // Avatar's matrix transform for the identity-rotation case.
        return eve::Result<eve::AttachmentPoint>::success(
            {found->second.x + localOffset.x, found->second.y + localOffset.y,
             found->second.z + localOffset.z});
    }
};

}  // namespace

TEST_CASE("trajectoryCollision.sphereSweepHitsAndDedupsAgainstWorld3D") {
    auto* physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    eve::physics::Body3D* wall = world->newBody("static", 2.f, 0.f, 0.f);
    wall->newBoxShape(1.f, 2.f, 2.f, 0.f, 0.5f, 0.f);

    const auto attacker = subject("11121314-1516-1718-991a-1b1c1d1e1f20");
    FixedBones bones;
    bones.bones["hand.R"] = {-1.f, 0.f, 0.f};

    eve::physics::trajectory::TrajectoryCollisionRuntime runtime(*world);
    eve::physics::trajectory::BoneColliderDefinition hand;
    hand.colliderId = "hand.R";
    hand.boneName   = "hand.R";
    hand.kind       = eve::physics::trajectory::BoneColliderShapeKind::Sphere;
    hand.radius     = 0.25f;
    REQUIRE(runtime.registerCollider(hand).ok());
    REQUIRE(runtime.bindPoseSource(attacker, bones).ok());
    REQUIRE(runtime.arm(attacker, "hand.R").ok());

    // Arming frame establishes previous==current; no motion yet.
    auto primed = runtime.advance(eve::SimulationTick(1));
    REQUIRE(primed.ok());
    CHECK_EQ(primed.value().hits.size(), 0u);

    // Sweep the hand sphere through the static wall.
    bones.bones["hand.R"] = {3.f, 0.f, 0.f};
    auto hitFrame = runtime.advance(eve::SimulationTick(2));
    REQUIRE(hitFrame.ok());
    REQUIRE_EQ(hitFrame.value().hits.size(), 1u);
    CHECK_EQ(hitFrame.value().hits.front().colliderId, std::string("hand.R"));
    CHECK_EQ(hitFrame.value().hits.front().bodyId, wall->getId());
    CHECK(hitFrame.value().hits.front().body.isValid());
    CHECK(hitFrame.value().hits.front().fraction >= 0.f);
    CHECK(hitFrame.value().hits.front().fraction <= 1.f);

    // Same contact is de-duplicated until disarm.
    auto again = runtime.advance(eve::SimulationTick(3));
    REQUIRE(again.ok());
    CHECK_EQ(again.value().hits.size(), 0u);

    REQUIRE(runtime.disarm(attacker, "hand.R").ok());
    bones.bones["hand.R"] = {-1.f, 0.f, 0.f};
    REQUIRE(runtime.arm(attacker, "hand.R").ok());
    bones.bones["hand.R"] = {3.f, 0.f, 0.f};
    auto rearmed = runtime.advance(eve::SimulationTick(4));
    REQUIRE(rearmed.ok());
    CHECK_EQ(rearmed.value().hits.size(), 1u);
}

TEST_CASE("trajectoryCollision.dualSocketCapsuleSweepsWeaponBlade") {
    auto* physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    eve::physics::Body3D* target = world->newBody("static", 0.f, 0.f, 2.f);
    target->newSphereShape(0.5f, 0.f, 0.5f, 0.f);

    const auto attacker = subject("01020304-0506-0708-890a-0b0c0d0e0f10");
    FixedBones bones;
    bones.bones["weapon.guard"] = {0.f, 0.f, -1.f};
    bones.bones["weapon.tip"]   = {0.f, 0.f, -0.2f};

    eve::physics::trajectory::TrajectoryCollisionRuntime runtime(*world);
    eve::physics::trajectory::BoneColliderDefinition blade;
    blade.colliderId     = "weapon.blade";
    blade.boneName       = "weapon.guard";
    blade.endBoneName    = "weapon.tip";
    blade.kind           = eve::physics::trajectory::BoneColliderShapeKind::Capsule;
    blade.radius         = 0.08f;
    REQUIRE(runtime.registerCollider(blade).ok());
    REQUIRE(runtime.bindPoseSource(attacker, bones).ok());
    REQUIRE(runtime.arm(attacker, "weapon.blade").ok());
    REQUIRE(runtime.advance(eve::SimulationTick(1)).ok());

    // Swing the blade forward so the capsule sweeps through the target sphere.
    bones.bones["weapon.guard"] = {0.f, 0.f, 1.5f};
    bones.bones["weapon.tip"]   = {0.f, 0.f, 2.5f};
    auto frame = runtime.advance(eve::SimulationTick(2));
    REQUIRE(frame.ok());
    REQUIRE_EQ(frame.value().hits.size(), 1u);
    CHECK_EQ(frame.value().hits.front().bodyId, target->getId());
    CHECK(frame.value().hits.front().shape.isValid());
}

TEST_CASE("trajectoryCollision.singleBoneCapsuleUsesLocalHalfHeight") {
    auto* physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    eve::physics::Body3D* post = world->newBody("static", 0.f, 1.f, 2.f);
    post->newBoxShape(0.5f, 2.f, 0.5f, 0.f, 0.5f, 0.f);

    const auto attacker = subject("21222324-2526-2728-992a-2b2c2d2e2f30");
    FixedBones bones;
    bones.bones["forearm.L"] = {0.f, 1.f, -1.f};

    eve::physics::trajectory::TrajectoryCollisionRuntime runtime(*world);
    eve::physics::trajectory::BoneColliderDefinition forearm;
    forearm.colliderId = "forearm.L";
    forearm.boneName   = "forearm.L";
    forearm.kind       = eve::physics::trajectory::BoneColliderShapeKind::Capsule;
    forearm.radius     = 0.12f;
    forearm.halfHeight = 0.35f;
    REQUIRE(runtime.registerCollider(forearm).ok());
    REQUIRE(runtime.bindPoseSource(attacker, bones).ok());
    REQUIRE(runtime.arm(attacker, "forearm.L").ok());
    REQUIRE(runtime.advance(eve::SimulationTick(1)).ok());

    bones.bones["forearm.L"] = {0.f, 1.f, 2.f};
    auto frame = runtime.advance(eve::SimulationTick(2));
    REQUIRE(frame.ok());
    REQUIRE_EQ(frame.value().hits.size(), 1u);
    CHECK_EQ(frame.value().hits.front().colliderId, std::string("forearm.L"));
}

TEST_CASE("trajectoryCollision.rejectsInvalidCatalogAndSurvivesWorldDestroy") {
    eve::physics::trajectory::BoneColliderDefinition bad;
    bad.colliderId = "bad";
    bad.boneName   = "bone";
    bad.kind       = eve::physics::trajectory::BoneColliderShapeKind::Sphere;
    bad.radius     = -1.f;
    CHECK(!bad.validate().ok());

    auto* physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    eve::physics::trajectory::TrajectoryCollisionRuntime runtime(*world);
    eve::physics::trajectory::BoneColliderDefinition hand;
    hand.colliderId = "hand";
    hand.boneName   = "hand";
    hand.kind       = eve::physics::trajectory::BoneColliderShapeKind::Sphere;
    hand.radius     = 0.2f;
    REQUIRE(runtime.registerCollider(hand).ok());

    const auto attacker = subject("31323334-3536-3738-993a-3b3c3d3e3f40");
    FixedBones bones;
    bones.bones["hand"] = {0.f, 0.f, 0.f};
    REQUIRE(runtime.bindPoseSource(attacker, bones).ok());
    REQUIRE(runtime.arm(attacker, "hand").ok());

    world.reset();
    auto stale = runtime.advance(eve::SimulationTick(1));
    REQUIRE(!stale.ok());
    REQUIRE(stale.error() != nullptr);
    CHECK_EQ(stale.error()->code(), eve::DiagnosticCode::StaleHandle);
}

TEST_CASE("trajectoryCollision.filterBitsIgnoreNonMatchingCategory") {
    auto* physics = eve::physics::Physics::create();
    std::unique_ptr<eve::physics::World3D> world(physics->newWorld3D(0.f, 0.f, 0.f, false));
    eve::physics::Body3D* wall = world->newBody("static", 2.f, 0.f, 0.f);
    eve::physics::Shape3D* shape = wall->newBoxShape(1.f, 2.f, 2.f, 0.f, 0.5f, 0.f);
    shape->setFilterBits(0x2u, 0xFFFFFFFFu);

    const auto attacker = subject("41424344-4546-4748-994a-4b4c4d4e4f50");
    FixedBones bones;
    bones.bones["fist"] = {-1.f, 0.f, 0.f};

    eve::physics::trajectory::TrajectoryCollisionRuntime runtime(*world);
    eve::physics::trajectory::BoneColliderDefinition fist;
    fist.colliderId   = "fist";
    fist.boneName     = "fist";
    fist.kind         = eve::physics::trajectory::BoneColliderShapeKind::Sphere;
    fist.radius       = 0.25f;
    fist.categoryBits = 0x1u;
    fist.maskBits     = 0x1u;  // does not match wall category 0x2
    REQUIRE(runtime.registerCollider(fist).ok());
    REQUIRE(runtime.bindPoseSource(attacker, bones).ok());
    REQUIRE(runtime.arm(attacker, "fist").ok());
    REQUIRE(runtime.advance(eve::SimulationTick(1)).ok());

    bones.bones["fist"] = {3.f, 0.f, 0.f};
    auto miss = runtime.advance(eve::SimulationTick(2));
    REQUIRE(miss.ok());
    CHECK_EQ(miss.value().hits.size(), 0u);

    fist.maskBits = 0x2u;
    REQUIRE(runtime.registerCollider(fist).ok());
    REQUIRE(runtime.disarm(attacker, "fist").ok());
    bones.bones["fist"] = {-1.f, 0.f, 0.f};
    REQUIRE(runtime.arm(attacker, "fist").ok());
    REQUIRE(runtime.advance(eve::SimulationTick(3)).ok());
    bones.bones["fist"] = {3.f, 0.f, 0.f};
    auto hit = runtime.advance(eve::SimulationTick(4));
    REQUIRE(hit.ok());
    CHECK_EQ(hit.value().hits.size(), 1u);
}
