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

int __cdecl climbingAllocationHook(int allocationType, void*, std::size_t, int, long, const unsigned char*, int) {
    if (trackHeapAllocations && allocationType == _HOOK_ALLOC) ++trackedHeapAllocations;
    return 1;
}
#endif

eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return std::move(duration).takeValue();
}

eve::climbing::ClimbingActionDefinition mantleAction(std::string id = "core:mantle") {
    return {std::move(id), 0.4f, 1.2f, 0.f, seconds(0.6), 0.6f, 0.7f, 0};
}

eve::climbing::ClimbingActionDefinition ledgeGrabAction() {
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
    const auto& candidate = candidates.value().front();
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
    const auto& warp = advanced.value().appliedWarp;
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

    for (std::uint64_t tick = 11; tick <= 16; ++tick) {
        auto advanced = runtime.advance(*fixture.world, {eve::SimulationTick(tick), seconds(0.1)});
        REQUIRE(advanced.ok());
    }
    auto events = runtime.drainEvents();
    REQUIRE(events.ok());
    const bool sawHangingEvent = std::any_of(events.value().begin(), events.value().end(), [](const auto& event) {
        return event.kind == eve::climbing::ClimbingEventKind::Hanging;
    });
    CHECK_EQ(static_cast<int>(runtime.phase()), static_cast<int>(eve::climbing::ClimbingPhase::Hanging));
    CHECK(sawHangingEvent);
    auto debug = runtime.inspect();
    CHECK_EQ(debug.candidates.size(), std::size_t{1});
    CHECK(debug.queryCount >= 4);
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
