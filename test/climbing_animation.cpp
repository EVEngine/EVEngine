#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include "PathBesideSource.h"
#include "animation/AnimClip.h"
#include "animation/AnimGraph.h"
#include "animation/AnimImporter.h"
#include "animation/AnimPlayer.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "animation/MotionDatabase.h"
#include "animation/MotionMatcher.h"
#include "climbing/Climbing.h"
#include "climbing/ClimbingAnimation.h"
#include "climbing/ClimbingPose.h"
#include "common/Runtime.h"
#include "filesystem/Filesystem.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// climbing_animation.cpp
namespace {

eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return std::move(duration).takeValue();
}

}  // namespace

TEST_CASE("climbing.animation.extractsRootMotionAndSemanticNotifies") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root", -1);
    eve::animation::AnimClip     clip("mantle");
    clip.setDuration(1.f);
    clip.setLoop(false);
    clip.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(root, 1.f, 1.f, 0.f, 0.f);
    clip.addEvent(0.1f, "contact.left_hand");
    clip.addEvent(0.15f, "collision.compact");
    clip.addEvent(0.2f, "contact.right_hand");
    clip.addEvent(0.22f, "studio.camera_cue");

    eve::animation::AnimPlayer player(&skeleton);
    REQUIRE(eve::climbing::beginClimbingAnimation(player, clip, root).ok());
    auto frame =
        eve::climbing::advanceClimbingAnimation(player, {eve::SimulationTick(1), seconds(0.25)}, {0.f, 0.f, 1.f});
    REQUIRE(frame.ok());
    CHECK(std::fabs(frame.value().motion.rootTranslation.x - 0.25f) < 0.001f);
    CHECK(frame.value().motion.hasRootMotion);
    CHECK_EQ(frame.value().recognizedNotifyCount, std::uint32_t(3));
    CHECK_EQ(frame.value().ignoredNotifyCount, std::uint32_t(1));
    REQUIRE_EQ(frame.value().motion.notifies.size(), std::size_t(3));
}

TEST_CASE("climbing.animation.graphProjectionTriggersOnceAndDegradesExplicitly") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root", -1);
    eve::animation::AnimClip     idle("idle");
    idle.setDuration(1.f);
    idle.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    eve::animation::AnimClip shot("climb");
    shot.setDuration(0.5f);
    shot.setLoop(false);
    shot.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    shot.addPositionKey(root, 0.5f, 0.f, 1.f, 0.f);

    eve::animation::AnimGraph graph(&skeleton);
    const int                 base    = graph.addClip(&idle);
    const int                 action  = graph.addClip(&shot);
    const int                 oneShot = graph.addOneShot(base, action, 0.05f, 0.05f);
    graph.setRoot(oneShot);
    eve::climbing::ClimbingGraphState state;
    auto applied = eve::climbing::driveClimbingGraph(graph, oneShot, eve::climbing::ClimbingExecutionId(9), state);
    REQUIRE(applied.ok());
    CHECK(graph.isOneShotActive(oneShot));
    auto duplicate = eve::climbing::driveClimbingGraph(graph, oneShot, eve::climbing::ClimbingExecutionId(9), state);
    REQUIRE(duplicate.ok());
    CHECK_EQ(static_cast<int>(duplicate.code()), static_cast<int>(eve::StatusCode::NoOp));

    CHECK_EQ(static_cast<int>(eve::climbing::useDirectClimbingPose(eve::climbing::ClimbingExecutionId(10), state)),
             static_cast<int>(eve::climbing::ClimbingGraphProvider::DirectPoseConstraints));
    CHECK_EQ(state.activeExecutionId.value(), std::uint64_t(10));
}

TEST_CASE("climbing.animation.bindingValidatesClipIdentityAndResolvesRootBone") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("Root", -1);
    eve::animation::AnimClip     clip("anim:mantle");
    clip.setDuration(0.5f);
    clip.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    eve::animation::AnimPlayer player(&skeleton);

    eve::climbing::ClimbingAnimationBinding binding{"anim:mantle", "climbing.mantle", "Root", true};
    REQUIRE(eve::climbing::beginClimbingAnimation(player, clip, binding).ok());

    binding.clipId = "anim:other";
    auto wrongClip = eve::climbing::beginClimbingAnimation(player, clip, binding);
    CHECK(!wrongClip.ok());
    binding.clipId   = "anim:mantle";
    binding.rootBone = "Missing";
    auto missingRoot = eve::climbing::beginClimbingAnimation(player, clip, binding);
    CHECK(!missingRoot.ok());
}

TEST_CASE("climbing.animation.rejectsInvalidGraphAndDuplicatePlayerTicks") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root", -1);
    eve::animation::AnimClip     clip("climb");
    clip.setDuration(1.f);
    clip.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    eve::animation::AnimPlayer player(&skeleton);
    REQUIRE(eve::climbing::beginClimbingAnimation(player, clip, root).ok());
    REQUIRE(
        eve::climbing::advanceClimbingAnimation(player, {eve::SimulationTick(3), seconds(0.1)}, {0.f, 0.f, 1.f}).ok());
    auto duplicate =
        eve::climbing::advanceClimbingAnimation(player, {eve::SimulationTick(3), seconds(0.1)}, {0.f, 0.f, 1.f});
    CHECK(!duplicate.ok());

    eve::animation::AnimGraph graph(&skeleton);
    const int                 clipNode = graph.addClip(&clip);
    graph.setRoot(clipNode);
    eve::climbing::ClimbingGraphState state;
    auto invalid = eve::climbing::driveClimbingGraph(graph, clipNode, eve::climbing::ClimbingExecutionId(1), state);
    CHECK(!invalid.ok());
    CHECK(state.activeExecutionId.isZero());
}

TEST_CASE("climbing.animation.motionMatchingYieldsToActionAndResumesByTick") {
    eve::animation::AnimSkeleton skeleton;
    const int                    root = skeleton.addBone("root", -1);
    eve::animation::AnimClip     locomotion("run");
    locomotion.setDuration(1.f);
    locomotion.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    locomotion.addPositionKey(root, 1.f, 0.f, 0.f, 2.f);
    eve::animation::MotionDatabase database(&skeleton);
    database.addClip(&locomotion);
    database.bake();
    eve::animation::MotionMatcher     matcher(&skeleton, &database);
    eve::climbing::ClimbingGraphState state;
    auto                              locomotionProvider = eve::climbing::driveClimbingMotionMatching(
        matcher, {eve::SimulationTick(1), seconds(0.1)}, {0.f, 0.f, 2.f}, 0.f, state);
    REQUIRE(locomotionProvider.ok());
    CHECK_EQ(static_cast<int>(state.provider), static_cast<int>(eve::climbing::ClimbingGraphProvider::MotionMatching));
    CHECK(state.activeExecutionId.isZero());

    eve::animation::AnimGraph graph(&skeleton);
    const int                 base    = graph.addClip(&locomotion);
    const int                 action  = graph.addClip(&locomotion);
    const int                 oneShot = graph.addOneShot(base, action, 0.05f, 0.05f);
    graph.setRoot(oneShot);
    REQUIRE(eve::climbing::driveClimbingGraph(graph, oneShot, eve::climbing::ClimbingExecutionId(42), state).ok());
    CHECK_EQ(static_cast<int>(state.provider), static_cast<int>(eve::climbing::ClimbingGraphProvider::AnimGraph));

    auto resumed = eve::climbing::driveClimbingMotionMatching(matcher, {eve::SimulationTick(3), seconds(0.1)},
                                                              {1.f, 0.f, 0.f}, 1.5707963f, state);
    REQUIRE(resumed.ok());
    CHECK_EQ(static_cast<int>(state.provider), static_cast<int>(eve::climbing::ClimbingGraphProvider::MotionMatching));
    CHECK(state.activeExecutionId.isZero());
    CHECK(std::fabs(matcher.getDesiredVelocityX() - 1.f) < 0.0001f);
}

// climbing_motion_matching_demo.cpp
namespace {
int animationIndex(const eve::model3d::ModelData& model, std::string_view name) {
    for (int index = 0; index < model.getAnimationCount(); ++index)
        if (model.getAnimationName(index) == name) return index;
    return -1;
}
}  // namespace

TEST_CASE("climbing.motionMatchingDemoLoadsRedistributableMannequinAndActionClips") {
    const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path example    = sourceRoot / "examples" / "climbing-motion-matching";
    const std::filesystem::path assets     = example / "assets" / "kaykit";

    auto* filesystem = eve::filesystem::Filesystem::create();
    REQUIRE(filesystem != nullptr);
    REQUIRE(filesystem->setIdentity("ev_ut_climbing_motion_matching", true));
    REQUIRE(filesystem->setupWriteDirectory());
    filesystem->allowMountingForPath(assets.string());
    REQUIRE(filesystem->mount(assets.string(), "", false));

    auto* models = eve::model3d::Model3D::create();
    REQUIRE(models != nullptr);
    std::unique_ptr<eve::model3d::ModelData> basic(models->newModelDataFromFile("Rig_Medium_MovementBasic.glb"));
    std::unique_ptr<eve::model3d::ModelData> advanced(models->newModelDataFromFile("Rig_Medium_MovementAdvanced.glb"));
    REQUIRE(basic.get() != nullptr);
    REQUIRE(advanced.get() != nullptr);
    REQUIRE(basic->getMeshCount() >= 6);
    REQUIRE(basic->getAnimationCount() >= 10);
    REQUIRE(advanced->getAnimationCount() >= 10);

    std::unique_ptr<eve::animation::AnimSkeleton> skeleton(
        eve::animation::AnimImporter::loadSkeletonFromModel(basic.get()));
    REQUIRE(skeleton.get() != nullptr);
    CHECK(skeleton->findBone("root") >= 0);
    CHECK(skeleton->findBone("foot.l") >= 0);
    CHECK(skeleton->findBone("foot.r") >= 0);

    for (const auto name : {"Walking_A", "Running_A", "Jump_Full_Short", "Jump_Full_Long"}) {
        const int index = animationIndex(*basic, name);
        REQUIRE(index >= 0);
        std::unique_ptr<eve::animation::AnimClip> clip(
            eve::animation::AnimImporter::loadClipFromModel(basic.get(), skeleton.get(), index));
        REQUIRE(clip.get() != nullptr);
        CHECK(clip->getDuration() > 0.0f);
    }
    for (const auto name : {"Running_Strafe_Left", "Running_Strafe_Right", "Dodge_Forward"})
        CHECK(animationIndex(*advanced, name) >= 0);

    CHECK(std::filesystem::exists(example / "assets" / "kaykit" / "LICENSE-KAYKIT-ANIMATIONS.txt"));
    CHECK(!std::filesystem::exists(example / "assets" / "ue-only" / "GameAnimationSample.uproject"));
    CHECK(filesystem->unmount(assets.string()));
}

TEST_CASE("climbing.motionMatchingDemoScriptCompilesAndDocumentsUeOnlyBoundary") {
    const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path scriptPath = sourceRoot / "examples" / "climbing-motion-matching" / "main.nut";
    std::ifstream               input(scriptPath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();
    CHECK(source.str().find("eve.Climbing()") != std::string::npos);
    CHECK(source.str().find("newMotionMatcher") != std::string::npos);
    CHECK(source.str().find("OrientationWarping") != std::string::npos);
    CHECK(source.str().find("displayPose.copyFrom") != std::string::npos);
    CHECK(source.str().find("assets/ue-only") == std::string::npos);

    eve::Runtime runtime(4096, ssq::Libs::ALL);
    bool         compiled = true;
    try {
        runtime.compileSource(source.str(), "examples/climbing-motion-matching/main.nut");
    } catch (...) {
        compiled = false;
    }
    CHECK(compiled);
}

// climbing_pose.cpp
TEST_CASE("climbing.pose.appliesBuiltInTwoBoneHandContacts") {
    eve::animation::AnimSkeleton skeleton;
    const int                    leftRoot  = skeleton.addBone("left_upper", -1);
    const int                    leftMid   = skeleton.addBone("left_lower", leftRoot);
    const int                    leftTip   = skeleton.addBone("left_hand", leftMid);
    const int                    rightRoot = skeleton.addBone("right_upper", -1);
    const int                    rightMid  = skeleton.addBone("right_lower", rightRoot);
    const int                    rightTip  = skeleton.addBone("right_hand", rightMid);
    skeleton.setBindPosition(leftMid, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(leftTip, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(rightMid, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(rightTip, 0.f, 0.5f, 0.f);

    eve::animation::AnimPose pose(skeleton.getBoneCount());
    skeleton.applyBindPose(&pose);
    eve::climbing::ClimbingAdvance advance;
    advance.phase           = eve::climbing::ClimbingPhase::Hanging;
    advance.leftHandAnchor  = {-0.2f, 0.9f, 0.2f};
    advance.rightHandAnchor = {0.2f, 0.9f, 0.2f};
    advance.contactWeight   = 1.f;
    eve::climbing::ClimbingPoseBinding binding;
    binding.leftArm  = {leftRoot, leftMid, leftTip};
    binding.rightArm = {rightRoot, rightMid, rightTip};

    auto applied = eve::climbing::applyClimbingPose(skeleton, pose, advance, binding);
    REQUIRE(applied.ok());
    CHECK(applied.value().leftHandApplied);
    CHECK(applied.value().rightHandApplied);
    CHECK_EQ(applied.value().provider, std::string("animation.two_bone"));
    CHECK(std::fabs(applied.value().weight - 1.f) < 1e-6f);
}

TEST_CASE("climbing.pose.reportsInvalidBoneChainsAndNoOpWeight") {
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root", -1);
    eve::animation::AnimPose pose(skeleton.getBoneCount());
    skeleton.applyBindPose(&pose);
    eve::climbing::ClimbingAdvance     advance;
    eve::climbing::ClimbingPoseBinding invalid;
    invalid.leftArm         = {0, 1, 2};
    invalid.enableRightHand = false;
    auto rejected           = eve::climbing::applyClimbingPose(skeleton, pose, advance, invalid);
    CHECK(!rejected.ok());

    invalid.enableLeftHand = false;
    auto noOp              = eve::climbing::applyClimbingPose(skeleton, pose, advance, invalid);
    REQUIRE(noOp.ok());
    CHECK_EQ(static_cast<int>(noOp.code()), static_cast<int>(eve::StatusCode::NoOp));
}

TEST_CASE("climbing.pose.appliesOnlyTheHandWhoseContactWindowIsActive") {
    eve::animation::AnimSkeleton skeleton;
    const int                    leftRoot  = skeleton.addBone("left_upper", -1);
    const int                    leftMid   = skeleton.addBone("left_lower", leftRoot);
    const int                    leftTip   = skeleton.addBone("left_hand", leftMid);
    const int                    rightRoot = skeleton.addBone("right_upper", -1);
    const int                    rightMid  = skeleton.addBone("right_lower", rightRoot);
    const int                    rightTip  = skeleton.addBone("right_hand", rightMid);
    skeleton.setBindPosition(leftMid, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(leftTip, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(rightMid, 0.f, 0.5f, 0.f);
    skeleton.setBindPosition(rightTip, 0.f, 0.5f, 0.f);
    eve::animation::AnimPose pose(skeleton.getBoneCount());
    skeleton.applyBindPose(&pose);

    eve::climbing::ClimbingAdvance advance;
    advance.leftHandAnchor  = {-0.2f, 0.9f, 0.2f};
    advance.rightHandAnchor = {0.2f, 0.9f, 0.2f};
    advance.leftHandWeight  = 1.f;
    advance.rightHandWeight = 0.f;
    eve::climbing::ClimbingPoseBinding binding;
    binding.leftArm  = {leftRoot, leftMid, leftTip};
    binding.rightArm = {rightRoot, rightMid, rightTip};
    auto applied     = eve::climbing::applyClimbingPose(skeleton, pose, advance, binding);
    REQUIRE(applied.ok());
    CHECK(applied.value().leftHandApplied);
    CHECK(!applied.value().rightHandApplied);
}

// climbing_vault.cpp
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
