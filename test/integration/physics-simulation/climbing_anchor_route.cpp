#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "climbing/ClimbingAnchorGraph.h"
#include "physics/Body3D.h"
#include "physics/Physics.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

#include <memory>

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
    eve::physics::Body3D*                                       body  = nullptr;
    eve::physics::Shape3D*                                      shape = nullptr;
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
