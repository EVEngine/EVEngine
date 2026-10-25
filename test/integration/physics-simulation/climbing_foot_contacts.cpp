#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "climbing/Climbing.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

#include <cmath>
#include <memory>

namespace {
using namespace eve::climbing;

eve::Duration contactDuration() {
    auto value = eve::Duration::fromSeconds(0.1);
    REQUIRE(value.ok());
    return std::move(value).takeValue();
}

struct GraphOwner {
    ClimbingAnchorGraphHandleRef handle;
    ~GraphOwner() {
        if (handle.isValid()) {
            auto released = Climbing::releaseAnchorGraph(handle);
            released.ignore("contact fixture releases graph after runtime destruction");
        }
    }
};

struct FootFixture {
    std::unique_ptr<eve::physics::World3D> world;
    eve::physics::Body3D*                  body;
    GraphOwner                             graph;
    ClimbingRuntime                        runtime;

    FootFixture(float width, float height, ClimbingAnchorKind kind = ClimbingAnchorKind::Ledge) {
        world.reset(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
        body = world->newBody("kinematic", -0.15f, 1.f, 1.f);
        body->newBoxShape(width, height, 0.5f);
        ClimbingAnchorNodeDefinition node;
        node.id              = "contact";
        node.kind            = kind;
        node.localPosition   = {0.15f, 1.f, -0.25f};
        node.leftHandSocket  = {-0.05f, 1.f, -0.25f};
        node.rightHandSocket = {0.35f, 1.f, -0.25f};
        node.feetSocket      = {0.15f, -0.3f, -0.25f};
        ClimbingAnchorGraphDefinition definition;
        definition.id                      = "test:foot-contact";
        definition.sourceGeometryContentId = "test:wall";
        definition.buildSettingsHash       = "test:foot-contact-v1";
        auto next                          = node;
        next.id                            = "next";
        next.localPosition.x += 1.f;
        next.leftHandSocket.x += 1.f;
        next.rightHandSocket.x += 1.f;
        next.feetSocket.x += 1.f;
        definition.nodes = {node, next};
        ClimbingAnchorEdgeDefinition edge;
        edge.from        = node.id;
        edge.to          = next.id;
        edge.kind        = ClimbingAnchorEdgeKind::Shimmy;
        definition.edges = {edge};
        auto created     = Climbing::newAnchorGraph(definition, *world, body->runtimeHandle());
        REQUIRE(created.ok());
        graph.handle  = std::move(created).takeValue();
        auto resolved = Climbing::resolveAnchorGraph(graph.handle);
        REQUIRE(resolved.isBound());
        auto reference = resolved->nodeRef("contact");
        REQUIRE(reference.ok());
        ClimbingActionDefinition action;
        action.id = "test:grab";
        action.kind =
            kind == ClimbingAnchorKind::LadderRung ? ClimbingActionKind::LadderMount : ClimbingActionKind::LedgeGrab;
        action.minHeight          = 0.f;
        action.maxHeight          = 3.f;
        action.duration           = contactDuration();
        action.contactConstraints = {{ClimbingContactTarget::LeftFoot, 0.f, 1.f, 1.f},
                                     {ClimbingContactTarget::RightFoot, 0.f, 1.f, 1.f}};
        ClimbingProfile profile;
        profile.actions = {action};
        REQUIRE(runtime.setProfile(profile).ok());
        ClimbingPose pose;
        pose.feet     = {0.f, 2.f - action.hangFeetBelowLedge,
                         0.75f - profile.capsuleRadius - profile.skin - action.hangBodyOffset};
        pose.forward  = {0.f, 0.f, 1.f};
        pose.grounded = false;
        auto started  = runtime.tryBeginAnchor(*world, graph.handle, reference.value(), ClimbingAnchorAgentId(1),
                                               action.id, pose, eve::SimulationTick(1));
        REQUIRE(started.ok());
    }

    ClimbingAdvance advance(std::uint64_t tick) {
        auto value = runtime.advance(*world, {eve::SimulationTick(tick), contactDuration()});
        REQUIRE(value.ok());
        return std::move(value).takeValue();
    }
};
}  // namespace

TEST_CASE("climbing.footContacts.narrowWallSupportsOnlyOneFoot") {
    FootFixture fixture(0.2f, 4.f);
    auto        step = fixture.advance(2);
    CHECK(step.hasLeftFootAnchor);
    CHECK(!step.hasRightFootAnchor);
    CHECK(std::fabs(step.leftFootAnchor.x + 0.15f) < 1e-4f);
    CHECK(std::fabs(step.leftFootAnchor.z - 0.75f) < 1e-4f);
}

TEST_CASE("climbing.footContacts.overheadLedgeLeavesFeetFree") {
    FootFixture fixture(4.f, 0.1f);
    auto        step = fixture.advance(2);
    CHECK(!step.hasLeftFootAnchor);
    CHECK(!step.hasRightFootAnchor);
}

TEST_CASE("climbing.footContacts.ladderSocketsFollowMovingBody") {
    FootFixture fixture(4.f, 4.f, ClimbingAnchorKind::LadderRung);
    auto        before = fixture.advance(2);
    REQUIRE(before.hasLeftFootAnchor);
    REQUIRE(before.hasRightFootAnchor);
    fixture.body->setPosition(0.85f, 1.f, 1.f);
    auto after = fixture.advance(3);
    CHECK(std::fabs(after.leftFootAnchor.x - before.leftFootAnchor.x - 1.f) < 1e-4f);
    CHECK(std::fabs(after.rightFootAnchor.x - before.rightFootAnchor.x - 1.f) < 1e-4f);
}

TEST_CASE("climbing.footContacts.ladderSocketsFollowBodyRotation") {
    FootFixture fixture(0.2f, 4.f, ClimbingAnchorKind::LadderRung);
    fixture.advance(2);
    const float halfTurn = std::sqrt(0.5f);
    fixture.body->setRotation(0.f, halfTurn, 0.f, halfTurn);
    auto after = fixture.advance(3);
    REQUIRE(after.hasLeftFootAnchor);
    REQUIRE(after.hasRightFootAnchor);
    auto left  = fixture.body->localToWorldPointOwned(0.f, -0.3f, -0.25f);
    auto right = fixture.body->localToWorldPointOwned(0.3f, -0.3f, -0.25f);
    REQUIRE(left.ok());
    REQUIRE(right.ok());
    CHECK(std::fabs(after.leftFootAnchor.x - left.value().x) < 1e-4f);
    CHECK(std::fabs(after.leftFootAnchor.z - left.value().z) < 1e-4f);
    CHECK(std::fabs(after.rightFootAnchor.x - right.value().x) < 1e-4f);
    CHECK(std::fabs(after.rightFootAnchor.z - right.value().z) < 1e-4f);
}

TEST_CASE("climbing.footContacts.destroyedBodyDoesNotPublishStaleContacts") {
    FootFixture fixture(4.f, 4.f);
    fixture.advance(2);
    fixture.world->destroyBody(fixture.body);
    auto result = fixture.runtime.advance(*fixture.world, {eve::SimulationTick(3), contactDuration()});
    REQUIRE(!result.ok());
    auto anchor = fixture.runtime.currentAnchor();
    CHECK(!anchor.ok());
}

TEST_CASE("climbing.contactTransfer.staggeredHandsRestoreTheirSource") {
    FootFixture              fixture(4.f, 4.f);
    auto                     initial = fixture.advance(2);
    ClimbingActionDefinition action;
    action.id        = "test:shimmy";
    action.kind      = ClimbingActionKind::Shimmy;
    action.minHeight = 0.f;
    action.maxHeight = 3.f;
    auto duration    = eve::Duration::fromSeconds(1.0);
    REQUIRE(duration.ok());
    action.duration           = std::move(duration).takeValue();
    action.apexHeight         = 0.f;
    action.contactConstraints = {{ClimbingContactTarget::RightHand, 0.f, 0.5f, 1.f},
                                 {ClimbingContactTarget::LeftHand, 0.5f, 1.f, 1.f}};
    REQUIRE(fixture.runtime.upsertAction(action).ok());
    auto graph = Climbing::resolveAnchorGraph(fixture.graph.handle);
    REQUIRE(graph.isBound());
    auto target = graph->nodeRef("next");
    REQUIRE(target.ok());
    REQUIRE(fixture.runtime
                .transitionAnchor(*fixture.world, target.value(), ClimbingAnchorEdgeKind::Shimmy, action.id,
                                  eve::SimulationTick(3))
                .ok());
    auto first = fixture.advance(4);
    CHECK(std::fabs(first.leftHandAnchor.x - initial.leftHandAnchor.x) < 1e-4f);
    CHECK(first.rightHandAnchor.x > initial.rightHandAnchor.x);
    CHECK(first.rightHandAnchor.x < initial.rightHandAnchor.x + 1.f);
    // Reload changes future actions, never the transfer windows pinned by this execution.
    action.contactConstraints = {{ClimbingContactTarget::LeftHand, 0.f, 0.5f, 1.f},
                                 {ClimbingContactTarget::RightHand, 0.5f, 1.f, 1.f}};
    REQUIRE(fixture.runtime.upsertAction(action).ok());
    auto snapshot = fixture.runtime.snapshot();
    REQUIRE(snapshot.ok());
    auto  corrupt                                    = snapshot.value();
    auto& state                                      = corrupt.getIf<eve::Value::Object>()->at("execution");
    auto& source                                     = state.getIf<eve::Value::Object>()->at("sourceAnchor");
    source.getIf<eve::Value::Object>()->at("nodeId") = eve::Value("missing-node");
    auto rejected                                    = fixture.runtime.restore(corrupt, *fixture.world);
    REQUIRE(!rejected.ok());
    auto stillAtTarget = fixture.runtime.currentAnchor();
    REQUIRE(stillAtTarget.ok());
    CHECK_EQ(stillAtTarget.value().nodeId, std::string("next"));
    ClimbingRuntime restored;
    REQUIRE(restored.restore(snapshot.value(), *fixture.world).ok());
    for (std::uint64_t tick = 5; tick <= 13; ++tick) {
        auto result = restored.advance(*fixture.world, {eve::SimulationTick(tick), contactDuration()});
        REQUIRE(result.ok());
        CHECK(result.value().leftHandAnchor.x < result.value().rightHandAnchor.x);
        if (tick == 9) {
            CHECK(std::fabs(result.value().rightHandAnchor.x - initial.rightHandAnchor.x - 1.f) < 1e-4f);
            CHECK(result.value().leftHandAnchor.x > initial.leftHandAnchor.x);
        }
        if (tick == 13) {
            CHECK(std::fabs(result.value().leftHandAnchor.x - initial.leftHandAnchor.x - 1.f) < 1e-4f);
            CHECK_EQ(result.value().phase, ClimbingPhase::Hanging);
        }
    }
    auto completed = restored.snapshot();
    REQUIRE(completed.ok());
    const auto& completedExecution = completed.value().getIf<eve::Value::Object>()->at("execution");
    CHECK(completedExecution.getIf<eve::Value::Object>()->at("sourceAnchor").isNull());

    // Release the current owner before replaying its older reservation credential.
    REQUIRE(restored.cancel(ClimbingCancelReason::PlayerRequest, eve::SimulationTick(14)).ok());
    // v4 had no transition source. Migrate it with the historical target-only projection.
    auto  legacy                   = snapshot.value();
    auto& legacyRoot               = *legacy.getIf<eve::Value::Object>();
    legacyRoot.at("schemaVersion") = eve::Value(std::int64_t{4});
    legacyRoot.at("execution").getIf<eve::Value::Object>()->erase("sourceAnchor");
    ClimbingRuntime migrated;
    auto            legacyRestore = migrated.restore(legacy, *fixture.world);
    REQUIRE(legacyRestore.ok(), "{}", legacyRestore.status().describe());
    auto legacyStep = migrated.advance(*fixture.world, {eve::SimulationTick(14), contactDuration()});
    REQUIRE(legacyStep.ok());
    CHECK(std::fabs(legacyStep.value().leftHandAnchor.x - initial.leftHandAnchor.x - 1.f) < 1e-4f);
}

namespace {
void verifyRightAngleCorner(bool inner) {
    std::unique_ptr<eve::physics::World3D> world(eve::physics::Physics::create()->newWorld3D(0.f, 0.f, 0.f, false));
    auto*                                  body = world->newBody("static", 0.f, 1.f, 0.f);
    if (inner) {
        body->newBoxShape(6.f, 3.f, 2.f);
        body->newBoxShape(2.f, 3.f, 4.f)->setLocalPosition(2.f, 0.f, -3.f);
    } else
        body->newBoxShape(2.f, 3.f, 2.f);
    ClimbingAnchorNodeDefinition front;
    front.id              = "front";
    front.localPosition   = {0.8f, 1.5f, -1.f};
    front.leftHandSocket  = {0.7f, 1.5f, -1.f};
    front.rightHandSocket = {0.9f, 1.5f, -1.f};
    front.feetSocket      = {0.8f, 0.3f, -1.f};
    auto side             = front;
    side.id               = "side";
    side.kind             = ClimbingAnchorKind::CornerOuter;
    side.localPosition    = {1.f, 1.5f, -0.8f};
    side.localNormal      = {1.f, 0.f, 0.f};
    side.localTangent     = {0.f, 0.f, 1.f};
    side.leftHandSocket   = {1.f, 1.5f, -0.9f};
    side.rightHandSocket  = {1.f, 1.5f, -0.7f};
    side.feetSocket       = {1.f, 0.3f, -0.8f};
    if (inner) {
        front.localPosition.x   = 0.5f;
        front.leftHandSocket.x  = 0.4f;
        front.rightHandSocket.x = 0.6f;
        front.feetSocket.x      = 0.5f;
        side.kind               = ClimbingAnchorKind::CornerInner;
        side.localPosition.z    = -1.5f;
        side.localNormal        = {-1.f, 0.f, 0.f};
        side.localTangent       = {0.f, 0.f, -1.f};
        side.leftHandSocket.z   = -1.4f;
        side.rightHandSocket.z  = -1.6f;
        side.feetSocket.z       = -1.5f;
    }
    ClimbingAnchorGraphDefinition definition;
    definition.id                      = "test:outer-corner";
    definition.sourceGeometryContentId = "test:box";
    definition.buildSettingsHash       = "test:right-angle";
    definition.nodes                   = {front, side};
    ClimbingAnchorEdgeDefinition edge;
    edge.from        = front.id;
    edge.to          = side.id;
    edge.kind        = ClimbingAnchorEdgeKind::Corner;
    definition.edges = {edge};
    auto created     = Climbing::newAnchorGraph(definition, *world, body->runtimeHandle());
    REQUIRE(created.ok());
    GraphOwner owner{std::move(created).takeValue()};
    auto       graph = Climbing::resolveAnchorGraph(owner.handle);
    REQUIRE(graph.isBound());
    auto source = graph->nodeRef(front.id);
    auto target = graph->nodeRef(side.id);
    REQUIRE(source.ok());
    REQUIRE(target.ok());
    ClimbingActionDefinition grab;
    grab.id             = "test:grab";
    grab.kind           = ClimbingActionKind::LedgeGrab;
    grab.minHeight      = 0.f;
    grab.maxHeight      = 4.f;
    grab.hangBodyOffset = 0.f;
    grab.duration       = contactDuration();
    auto corner         = grab;
    corner.id           = "test:corner";
    corner.kind         = inner ? ClimbingActionKind::CornerInner : ClimbingActionKind::CornerOuter;
    auto duration       = eve::Duration::fromSeconds(1.0);
    REQUIRE(duration.ok());
    corner.duration = duration.value();
    ClimbingProfile profile;
    profile.capsuleRadius = 0.2f;
    profile.actions       = {grab, corner};
    ClimbingRuntime runtime;
    REQUIRE(runtime.setProfile(profile).ok());
    ClimbingPose pose;
    pose.feet     = {inner ? 0.5f : 0.8f, 0.95f, -1.23f};
    pose.forward  = {0.f, 0.f, 1.f};
    pose.grounded = false;
    REQUIRE(runtime
                .tryBeginAnchor(*world, owner.handle, source.value(), ClimbingAnchorAgentId(1), grab.id, pose,
                                eve::SimulationTick(1))
                .ok());
    REQUIRE(runtime.advance(*world, {eve::SimulationTick(2), contactDuration()}).ok());
    auto transitioned = runtime.transitionAnchor(*world, target.value(), ClimbingAnchorEdgeKind::Corner, corner.id,
                                                 eve::SimulationTick(3));
    REQUIRE(transitioned.ok());
    for (std::uint64_t tick = 4; tick <= 13; ++tick) {
        auto advanced = runtime.advance(*world, {eve::SimulationTick(tick), contactDuration()});
        REQUIRE(advanced.ok());
        const auto  feet = advanced.value().feet;
        const float dx   = std::max(feet.x - 1.f, 0.f);
        const float dz   = std::max(-1.f - feet.z, 0.f);
        if (inner) {
            CHECK(feet.x <= 0.8f + 1e-4f);
            CHECK(feet.z <= -1.2f + 1e-4f);
        } else
            CHECK(dx * dx + dz * dz >= 0.2f * 0.2f - 1e-4f);
        for (const auto hand : {advanced.value().leftHandAnchor, advanced.value().rightHandAnchor}) {
            if (inner)
                CHECK((hand.x <= 1.f + 1e-4f && hand.z <= -1.f + 1e-4f));
            else
                CHECK((hand.x >= 1.f - 1e-4f || hand.z <= -1.f + 1e-4f));
        }
    }
    CHECK_EQ(runtime.phase(), ClimbingPhase::Hanging);
}

}  // namespace

TEST_CASE("climbing.corner.outerRightAngleKeepsCapsuleOutsideWall") { verifyRightAngleCorner(false); }
TEST_CASE("climbing.corner.innerRightAngleKeepsCapsuleOutsideBothWalls") { verifyRightAngleCorner(true); }
