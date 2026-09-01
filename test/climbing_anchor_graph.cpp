#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "climbing/ClimbingAnchorGraph.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

#include <cmath>
#include <memory>

namespace {

eve::climbing::ClimbingAnchorGraphDefinition graphDefinition() {
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
    eve::physics::Body3D*                  body  = nullptr;
    eve::physics::Shape3D*                 shape = nullptr;
};

}  // namespace

TEST_CASE("climbing.anchorGraph.codecIsCanonicalAndPreservesUnknownFields") {
    auto graph = graphDefinition();
    graph.extensionMetadata.emplace("editorColor", eve::Value("blue"));
    graph.nodes.front().extensionMetadata.emplace("designerNote", eve::Value("right end"));
    auto encoded = eve::climbing::encodeClimbingAnchorGraphDefinition(graph);
    REQUIRE(encoded.ok());
    auto decoded = eve::climbing::decodeClimbingAnchorGraphDefinition(encoded.value());
    REQUIRE(decoded.ok());
    CHECK_EQ(decoded.value().nodes.front().id, std::string("ledge:left"));
    auto reencoded = eve::climbing::encodeClimbingAnchorGraphDefinition(decoded.value());
    REQUIRE(reencoded.ok());
    CHECK(reencoded.value() == encoded.value());

    eve::Value future = encoded.value();
    future.set("schemaVersion", eve::Value(eve::climbing::ClimbingAnchorGraphDefinition::SchemaVersion + 1));
    auto rejected = eve::climbing::decodeClimbingAnchorGraphDefinition(future);
    CHECK(!rejected.ok());
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::UnknownVersion));
}

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
