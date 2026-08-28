#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimSkeleton.h"
#include "climbing/Climbing.h"
#include "climbing/ClimbingAnimation.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

eve::climbing::ClimbingActionDefinition vaultAction(std::string id, float minHeight, float maxHeight,
                                                    int selectionBias = 0) {
    eve::climbing::ClimbingActionDefinition action;
    action.id                        = std::move(id);
    action.kind                      = eve::climbing::ClimbingActionKind::Vault;
    action.minHeight                 = minHeight;
    action.maxHeight                 = maxHeight;
    action.minSpeed                  = 3.f;
    action.duration                  = eve::Duration::fromNanoseconds(600000000);
    action.landingForward            = 0.75f;
    action.apexHeight                = 0.55f;
    action.selectionBias             = selectionBias;
    action.maxTranslationWarpPerTick = 0.2f;
    action.horizontalWarpBudget      = 1.2f;
    action.verticalWarpBudget        = 1.2f;
    action.requiredNotifies          = {"contact.left_hand", "land"};
    return action;
}

struct VaultWorld {
    VaultWorld() {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        player = world->newBody("kinematic", 0.f, 0.9f, 0.f);
        player->newCapsuleShape(1.8f, 0.3f);
        obstacle = world->newBody("static", 0.f, 0.3f, 1.f);
        obstacle->newBoxShape(1.35f, 0.6f, 0.65f);
    }

    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D*                  player   = nullptr;
    eve::physics::Body3D*                  obstacle = nullptr;
};

struct VaultRun {
    std::vector<eve::climbing::ClimbingAdvance> advances;
    std::vector<eve::climbing::ClimbingEvent>   events;
    bool                                        sawConstraint = false;
    bool                                        landingClear  = false;
    std::uint64_t                               replayHash    = 1469598103934665603ull;
};

void hashValue(std::uint64_t& hash, std::int64_t value) {
    const auto bits = static_cast<std::uint64_t>(value);
    for (int byte = 0; byte < 8; ++byte) {
        hash ^= (bits >> (byte * 8)) & 0xffu;
        hash *= 1099511628211ull;
    }
}

std::int64_t millimeters(float value) {
    return static_cast<std::int64_t>(std::llround(static_cast<double>(value) * 1000.0));
}

VaultRun runVault() {
    VaultWorld                               fixture;
    eve::climbing::ClimbingRuntime           runtime;
    eve::climbing::ClimbingProfileDefinition profile;
    profile.maxWarpResidual = 5.f;
    profile.actions.push_back(vaultAction("parkour:vault_low", 0.4f, 0.78f));
    profile.actions.push_back(vaultAction("parkour:vault_high", 0.79f, 1.2f, 1));
    REQUIRE(runtime.setProfile(profile).ok());

    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root", -1);
    eve::animation::AnimClip     clip("vault_low");
    clip.setDuration(0.6f);
    clip.setLoop(false);
    clip.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(root, 0.6f, 0.f, 0.6f, 1.8f);
    clip.addEvent(0.15f, "contact.left_hand");
    clip.addEvent(0.55f, "land");
    REQUIRE(runtime.validateAnimationBinding("parkour:vault_low", clip).ok());

    const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 4.5f, fixture.player->getId(), 0.f, true};
    auto                              candidates = runtime.probe(*fixture.world, pose);
    REQUIRE(candidates.ok());
    REQUIRE_EQ(candidates.value().size(), std::size_t{1});
    CHECK_EQ(candidates.value().front().actionId, std::string("parkour:vault_low"));
    CHECK(candidates.value().front().obstacleBody == fixture.obstacle->runtimeHandle());
    REQUIRE(runtime.tryBegin(*fixture.world, pose, eve::SimulationTick(100)).ok());

    eve::animation::AnimPlayer player(&skeleton);
    REQUIRE(eve::climbing::beginClimbingAnimation(player, clip, root).ok());
    VaultRun output;
    for (std::uint64_t frame = 1; frame <= 6; ++frame) {
        const eve::SimulationStep step{eve::SimulationTick(100 + frame), eve::Duration::fromNanoseconds(100000000)};
        auto animationFrame = eve::climbing::advanceClimbingAnimation(player, step, {0.f, 0.f, 1.f});
        REQUIRE(animationFrame.ok());
        auto advanced = runtime.advance(*fixture.world, step, animationFrame.value().motion);
        REQUIRE(advanced.ok());
        output.sawConstraint = output.sawConstraint || advanced.value().constrained ||
                               std::fabs(advanced.value().desiredDelta.x - advanced.value().actualDelta.x) > 1e-4f ||
                               std::fabs(advanced.value().desiredDelta.y - advanced.value().actualDelta.y) > 1e-4f ||
                               std::fabs(advanced.value().desiredDelta.z - advanced.value().actualDelta.z) > 1e-4f;
        hashValue(output.replayHash, millimeters(advanced.value().feet.x));
        hashValue(output.replayHash, millimeters(advanced.value().feet.y));
        hashValue(output.replayHash, millimeters(advanced.value().feet.z));
        output.advances.push_back(std::move(advanced).takeValue());
    }
    auto events = runtime.drainEvents();
    REQUIRE(events.ok());
    output.events = std::move(events).takeValue();
    for (const auto& event : output.events) {
        hashValue(output.replayHash, static_cast<std::int64_t>(event.kind));
        hashValue(output.replayHash, static_cast<std::int64_t>(event.tick.value()));
    }

    const auto&                 landing = output.advances.back().feet;
    eve::physics::QueryFilter3D filter;
    filter.ignoredBodyId = fixture.player->getId();
    auto overlap = fixture.world->queryCapsuleOwned(landing.x, landing.y + profile.capsuleRadius, landing.z, landing.x,
                                                    landing.y + profile.capsuleHeight - profile.capsuleRadius,
                                                    landing.z, profile.capsuleRadius, filter);
    REQUIRE(overlap.ok());
    output.landingClear = overlap.value().bodyCount == 0;
    return output;
}

}  // namespace

TEST_CASE("climbing.vault.runningJumpUsesLowVaultAndProducesStableConstrainedLanding") {
    const VaultRun first = runVault();
    REQUIRE_EQ(first.advances.size(), std::size_t{6});
    CHECK(first.sawConstraint);
    CHECK(first.landingClear);
    CHECK_EQ(static_cast<int>(first.advances.back().phase), static_cast<int>(eve::climbing::ClimbingPhase::Completed));

    REQUIRE_EQ(first.events.size(), std::size_t{4});
    CHECK_EQ(static_cast<int>(first.events[0].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Started));
    CHECK_EQ(static_cast<int>(first.events[1].kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::ContactLeftHand));
    CHECK_EQ(static_cast<int>(first.events[2].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Landed));
    CHECK_EQ(static_cast<int>(first.events[3].kind), static_cast<int>(eve::climbing::ClimbingEventKind::Completed));

    const VaultRun replay = runVault();
    CHECK_EQ(replay.replayHash, first.replayHash);
}

TEST_CASE("climbing.vault.destroyedTargetCancelsWithStaleEvidence") {
    VaultWorld                               fixture;
    eve::climbing::ClimbingRuntime           runtime;
    auto                                     low = vaultAction("parkour:vault_low", 0.4f, 0.78f);
    eve::climbing::ClimbingProfileDefinition profile;
    profile.maxWarpResidual = 5.f;
    profile.actions.push_back(low);
    profile.actions.push_back(vaultAction("parkour:vault_high", 0.79f, 1.2f, 1));
    REQUIRE(runtime.setProfile(profile).ok());
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root", -1);
    eve::animation::AnimClip     clip("vault_low_stale");
    clip.setDuration(0.6f);
    clip.setLoop(false);
    clip.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(root, 0.6f, 0.f, 0.6f, 1.8f);
    clip.addEvent(0.15f, "contact.left_hand");
    clip.addEvent(0.55f, "land");
    REQUIRE(runtime.validateAnimationBinding("parkour:vault_low", clip).ok());
    const eve::climbing::ClimbingPose pose{{0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, 4.5f, fixture.player->getId(), 0.f, true};
    auto                              candidates = runtime.probe(*fixture.world, pose);
    REQUIRE(candidates.ok());
    REQUIRE_EQ(candidates.value().size(), std::size_t{1});
    CHECK(candidates.value().front().obstacleBody == fixture.obstacle->runtimeHandle());
    REQUIRE(runtime.tryBegin(*fixture.world, pose, eve::SimulationTick(5)).ok());
    fixture.world->destroyBody(fixture.obstacle);
    fixture.obstacle = nullptr;

    auto advanced = runtime.advance(*fixture.world, {eve::SimulationTick(6), eve::Duration::fromNanoseconds(16666667)});
    CHECK(!advanced.ok());
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Cancelled));
    CHECK_EQ(runtime.inspect().terminalCode, std::string("climbing.anchor.stale"));
    auto events = runtime.drainEvents();
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), std::size_t{2});
    CHECK_EQ(static_cast<int>(events.value().back().kind),
             static_cast<int>(eve::climbing::ClimbingEventKind::Cancelled));
}
