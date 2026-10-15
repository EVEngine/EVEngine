#include "image/ImageData.h"
#include "procgen/GeneratedArtifact.h"
#include "procgen/ObjectBuildLayer.h"
#include "procgen/Params.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetwork.h"
#include "procgen/road/RoadRecipes.h"
#include "procgen/road/RoadTypes.h"
#include "procgen/spline/SplinePath.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/TextureRecipe.h"

#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace eve;
using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {

ArtifactId roadArtifactId() {
    auto id = ArtifactId::parse("018f0b7e-6e50-7a10-8c22-2c8f8e3dd071");
    REQUIRE(id.has_value());
    return *id;
}

void checkRoadScenario(RoadNetwork& network) {
    REQUIRE(network.validate().ok());
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 16;
    options.turnSamples         = 6;
    options.includePiers        = false;
    auto baked                  = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    CHECK_GT(baked.value().mesh.getVertexCount(), 0);
    CHECK_GT(baked.value().mesh.getIndexCount(), 0);
    for (float position : baked.value().mesh.positions()) CHECK(std::isfinite(position));
}

std::unique_ptr<MeshBuild> copyRoadGroup(const MeshBuild& mesh, const std::string& name) {
    for (int group = 0; group < mesh.getGroupCount(); ++group) {
        if (mesh.getGroupName(group) == name) return mesh.copyGroup(group);
    }
    return {};
}

bool roadTriangleCoversXZ(const MeshBuild& mesh, float x, float z) {
    for (int triangle = 0; triangle < mesh.getIndexCount() / 3; ++triangle) {
        const int   i0 = mesh.getIndex(triangle * 3);
        const int   i1 = mesh.getIndex(triangle * 3 + 1);
        const int   i2 = mesh.getIndex(triangle * 3 + 2);
        const float x0 = mesh.getPositionX(i0), z0 = mesh.getPositionZ(i0);
        const float x1 = mesh.getPositionX(i1), z1 = mesh.getPositionZ(i1);
        const float x2 = mesh.getPositionX(i2), z2 = mesh.getPositionZ(i2);
        const float denominator = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2);
        if (std::fabs(denominator) < 1e-8f) continue;
        const float a = ((z1 - z2) * (x - x2) + (x2 - x1) * (z - z2)) / denominator;
        const float b = ((z2 - z0) * (x - x2) + (x0 - x2) * (z - z2)) / denominator;
        const float c = 1.f - a - b;
        if (a >= -1e-4f && b >= -1e-4f && c >= -1e-4f) return true;
    }
    return false;
}

void checkNoDegenerateRoadTriangles(const MeshBuild& mesh, const std::string& groupName) {
    auto group = copyRoadGroup(mesh, groupName);
    REQUIRE(group);
    for (int triangle = 0; triangle < group->getIndexCount() / 3; ++triangle) {
        const int   a   = group->getIndex(triangle * 3);
        const int   b   = group->getIndex(triangle * 3 + 1);
        const int   c   = group->getIndex(triangle * 3 + 2);
        const float abx = group->getPositionX(b) - group->getPositionX(a);
        const float aby = group->getPositionY(b) - group->getPositionY(a);
        const float abz = group->getPositionZ(b) - group->getPositionZ(a);
        const float acx = group->getPositionX(c) - group->getPositionX(a);
        const float acy = group->getPositionY(c) - group->getPositionY(a);
        const float acz = group->getPositionZ(c) - group->getPositionZ(a);
        const float nx  = aby * acz - abz * acy;
        const float ny  = abz * acx - abx * acz;
        const float nz  = abx * acy - aby * acx;
        CHECK_GT(nx * nx + ny * ny + nz * nz, 1e-8f);
    }
}

void checkThreeArmMouthConstraints(const RoadNetwork& network, const RoadBakeResult& baked) {
    auto asphalt  = copyRoadGroup(baked.mesh, "asphalt");
    auto curb     = copyRoadGroup(baked.mesh, "curb");
    auto sidewalk = copyRoadGroup(baked.mesh, "sidewalk");
    REQUIRE(asphalt);
    REQUIRE(curb);
    REQUIRE(sidewalk);

    const RoadNode* hub = nullptr;
    for (const auto& node : network.nodes()) {
        const auto degree = std::count_if(network.edges().begin(), network.edges().end(), [&](const RoadEdge& edge) {
            return edge.from == node.id || edge.to == node.id;
        });
        if (degree == 3) hub = &node;
    }
    REQUIRE(hub != nullptr);

    auto seamDistance = [&](float x, float z) {
        float nearest = std::numeric_limits<float>::max();
        for (int a = 0; a < asphalt->getVertexCount(); ++a) {
            for (int b = a + 1; b < asphalt->getVertexCount(); ++b) {
                if (std::fabs(asphalt->getPositionX(a) - asphalt->getPositionX(b)) >= 0.002f ||
                    std::fabs(asphalt->getPositionY(a) - asphalt->getPositionY(b)) >= 0.002f ||
                    std::fabs(asphalt->getPositionZ(a) - asphalt->getPositionZ(b)) >= 0.002f)
                    continue;
                const float dx = asphalt->getPositionX(a) - x;
                const float dz = asphalt->getPositionZ(a) - z;
                nearest        = std::min(nearest, std::sqrt(dx * dx + dz * dz));
            }
        }
        return nearest;
    };
    for (const auto& edge : network.edges()) {
        if (edge.from != hub->id && edge.to != hub->id) continue;
        SplinePath path;
        REQUIRE(path.setKindResult("catmullRom").ok());
        for (const auto& point : edge.controlPoints)
            REQUIRE(path.addPointResult(SplinePoint{point.x, point.y, point.z}).ok());
        auto length = path.lengthResult(16);
        REQUIRE(length.ok());
        float       requiredTrim = hub->junctionRadius;
        const auto& adjacent =
            edge.from == hub->id ? edge.controlPoints[1] : edge.controlPoints[edge.controlPoints.size() - 2];
        const float dx = adjacent.x - hub->x, dz = adjacent.z - hub->z;
        const float dl = std::sqrt(dx * dx + dz * dz);
        for (const auto& other : network.edges()) {
            if (other.id == edge.id || (other.from != hub->id && other.to != hub->id)) continue;
            const auto& oa =
                other.from == hub->id ? other.controlPoints[1] : other.controlPoints[other.controlPoints.size() - 2];
            const float odx = oa.x - hub->x, odz = oa.z - hub->z;
            const float odl    = std::sqrt(odx * odx + odz * odz);
            const float cosine = std::clamp((dx * odx + dz * odz) / (dl * odl), -1.f, 1.f);
            const float sine   = std::sqrt(std::max(0.f, 1.f - cosine * cosine));
            if (sine < 0.08f) continue;
            const float half      = edge.style.laneWidth * (edge.lanesForward + edge.lanesBackward) * 0.5f;
            const float otherHalf = other.style.laneWidth * (other.lanesForward + other.lanesBackward) * 0.5f;
            requiredTrim          = std::max(requiredTrim, (otherHalf + half * std::fabs(cosine)) / sine + 0.35f);
        }
        const float trim     = std::min(requiredTrim, std::max(0.f, length.value() * 0.5f - 0.5f));
        const float distance = edge.to == hub->id ? length.value() - trim : trim;
        auto        frame    = path.travelFrameResult(distance, "clamp", 16);
        REQUIRE(frame.ok());
        const auto& mouth = frame.value();
        // No raised junction surface may cover the traffic opening.
        CHECK(!roadTriangleCoversXZ(*curb, mouth.sample.x, mouth.sample.z));
        CHECK(!roadTriangleCoversXZ(*sidewalk, mouth.sample.x, mouth.sample.z));

        const float asphaltHalf =
            edge.style.laneWidth * static_cast<float>(edge.lanesForward + edge.lanesBackward) * 0.5f;
        const float left =
            seamDistance(mouth.sample.x - mouth.sideX * asphaltHalf, mouth.sample.z - mouth.sideZ * asphaltHalf);
        const float right =
            seamDistance(mouth.sample.x + mouth.sideX * asphaltHalf, mouth.sample.z + mouth.sideZ * asphaltHalf);
        CHECK_LT(left, 0.18f);
        CHECK_LT(right, 0.18f);
    }
}

}  // namespace

TEST_CASE("procgen.road.profile.laneWidths") {
    RoadStyle style;
    style.laneWidth     = 3.5f;
    style.curbWidth     = 0.4f;
    style.sidewalkWidth = 1.5f;
    auto profile        = makeRoadProfile(style, 2, 0);
    REQUIRE(profile.ok());
    CHECK(std::fabs(profile.value().halfWidth - (3.5f + 0.4f + 1.5f)) < 1e-4f);
    CHECK_GE(profile.value().points.size(), 8u);
    CHECK_EQ(std::string(roadMaterialGroup(RoadMaterial::Asphalt)), "asphalt");
}

TEST_CASE("procgen.road.network.interchangeBakeHasGroupsAndOverlay") {
    auto network = RoadNetwork::makeInterchange(40.f, 7.f, 2, 3);
    REQUIRE(network.ok());
    CHECK_GE(network.value().nodeCount(), 8);
    CHECK_GE(network.value().edgeCount(), 6);
    CHECK_GT(network.value().laneLinkCount(), 0);

    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 24;
    options.turnSamples         = 8;
    auto baked                  = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    CHECK_GT(baked.value().mesh.getVertexCount(), 100);
    CHECK_GT(baked.value().mesh.getIndexCount(), 100);
    CHECK_GT(baked.value().mesh.getGroupCount(), 3);
    CHECK_GT(baked.value().overlay.lanes.size(), 0u);
    CHECK_GT(baked.value().overlay.turns.size(), 0u);
    CHECK_EQ(baked.value().mesh.getMeta("schema", ""), "eve.procgen.roadNetwork");
    const auto falseRoundabouts = filterPointStringAttribute(baked.value().placements, "road_role",
                                                             "junction.roundabout.center", false);
    CHECK_EQ(falseRoundabouts.getCount(), 0);

    bool sawAsphalt = false, sawPier = false, sawNav = false, sawMarking = false, sawCurb = false;
    for (int i = 0; i < baked.value().mesh.getGroupCount(); ++i) {
        const auto name = baked.value().mesh.getGroupName(i);
        if (name == "asphalt") sawAsphalt = true;
        if (name == "pier") sawPier = true;
        if (name == "nav") sawNav = true;
        if (name == "marking" || name == "markingYellow") sawMarking = true;
        if (name == "curb") sawCurb = true;
    }
    CHECK(sawAsphalt);
    CHECK(sawPier);
    CHECK(sawNav);
    CHECK(sawMarking);
    CHECK(sawCurb);
    CHECK(baked.value().mesh.hasVertexColors());
}

TEST_CASE("procgen.road.network.rejectsInvalidEdge") {
    RoadNetwork network;
    auto        a = network.addNode(0.f, 0.f, 0.f);
    auto        b = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto bad = network.addEdge(a.value(), b.value(), {RoadControlPoint{0.f, 0.f, 0.f}}, 2, 0);
    CHECK(!bad.ok());

    RoadNetwork coincident;
    auto        first  = coincident.addNode(2.f, 3.f, 4.f);
    auto        second = coincident.addNode(2.f, 3.f, 4.f);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    const auto stableRevision = coincident.revision();
    CHECK(!coincident.addEdge(first.value(), second.value(), {{}, {}}, 1, 0).ok());
    CHECK_EQ(coincident.edgeCount(), std::size_t{0});
    CHECK_EQ(coincident.revision(), stableRevision);
    RoadEdge restored;
    restored.id            = 9;
    restored.from          = first.value();
    restored.to            = second.value();
    restored.controlPoints = {{}, {}};
    restored.lanesForward  = 1;
    CHECK(!coincident.restoreEdge(std::move(restored)).ok());
    CHECK_EQ(coincident.edgeCount(), std::size_t{0});
    CHECK_EQ(coincident.revision(), stableRevision);
}

TEST_CASE("procgen.road.network.nodeMoveRejectsDegenerateIncidentEdgeAtomically") {
    auto network = RoadNetwork::makeStraight(20.f, 1);
    REQUIRE(network.ok());
    const auto edgeId = network.value().edges().front().id;
    const auto fromId = network.value().edges().front().from;
    const auto toNode = network.value().nodeResult(network.value().edges().front().to);
    REQUIRE(toNode.ok());
    const auto stableRevision = network.value().revision();
    const auto originalFrom = network.value().nodeResult(fromId);
    REQUIRE(originalFrom.ok());

    CHECK(!network.value().setNodePosition(fromId, toNode.value().x, toNode.value().y, toNode.value().z).ok());
    CHECK_EQ(network.value().revision(), stableRevision);
    auto unchangedNode = network.value().nodeResult(fromId);
    auto unchangedEdge = network.value().edgeResult(edgeId);
    REQUIRE(unchangedNode.ok());
    REQUIRE(unchangedEdge.ok());
    CHECK_EQ(unchangedNode.value().x, originalFrom.value().x);
    CHECK_EQ(unchangedEdge.value().controlPoints.front().x, originalFrom.value().x);
    REQUIRE(network.value().validate().ok());
}

TEST_CASE("procgen.road.network.junctionRadiusIsValidatedAndRevisioned") {
    RoadNetwork network;
    auto        node = network.addNode(0.f, 0.f, 0.f, 4.f);
    REQUIRE(node.ok());
    const auto initialRevision = network.revision();
    REQUIRE(network.setNodeJunctionRadius(node.value(), 8.f).ok());
    CHECK_EQ(network.nodeResult(node.value()).value().junctionRadius, 8.f);
    CHECK_EQ(network.revision(), initialRevision + 1);
    const auto stableRevision = network.revision();
    REQUIRE(network.setNodeJunctionRadius(node.value(), 8.f).ok());
    CHECK_EQ(network.revision(), stableRevision);
    CHECK(!network.setNodeJunctionRadius(node.value(), 0.f).ok());
    CHECK_EQ(network.revision(), stableRevision);
    CHECK_EQ(network.nodeResult(node.value()).value().junctionRadius, 8.f);
}

TEST_CASE("procgen.road.network.junctionControlIsAuthoritativeValidatedAndRevisioned") {
    RoadNetwork network;
    auto node = network.addNode(0.f, 0.f, 0.f);
    REQUIRE(node.ok());
    const auto initialRevision = network.revision();

    REQUIRE(network.setNodeJunctionControl(node.value(), RoadJunctionControl::Yield).ok());
    CHECK_EQ(network.revision(), initialRevision + 1);
    auto changed = network.nodeResult(node.value());
    REQUIRE(changed.ok());
    CHECK_EQ(changed.value().junctionControl, RoadJunctionControl::Yield);

    REQUIRE(network.setNodeJunctionControl(node.value(), RoadJunctionControl::Yield).ok());
    CHECK_EQ(network.revision(), initialRevision + 1);
    CHECK(!network.setNodeJunctionControl(node.value(), static_cast<RoadJunctionControl>(255)).ok());
    CHECK_EQ(network.revision(), initialRevision + 1);

    RoadNode invalid{99, 1.f, 0.f, 0.f, 3.f, static_cast<RoadJunctionControl>(255)};
    CHECK(!network.restoreNode(invalid).ok());
    CHECK_EQ(network.nodeCount(), 1);
}

TEST_CASE("procgen.road.network.rejectsSelfLoopWithoutMutation") {
    RoadNetwork network;
    auto        node = network.addNode(0.f, 0.f, 0.f);
    REQUIRE(node.ok());
    const auto stableRevision = network.revision();

    auto added = network.addEdge(node.value(), node.value(), {{}, {}}, 1, 0);
    CHECK(!added.ok());
    CHECK_EQ(network.edgeCount(), std::size_t{0});
    CHECK_EQ(network.revision(), stableRevision);

    RoadEdge restored;
    restored.id            = 37;
    restored.from          = node.value();
    restored.to            = node.value();
    restored.controlPoints = {{}, {}};
    restored.lanesForward  = 1;
    CHECK(!network.restoreEdge(std::move(restored)).ok());
    CHECK_EQ(network.edgeCount(), std::size_t{0});
    CHECK_EQ(network.revision(), stableRevision);
}

TEST_CASE("procgen.road.network.bidirectionalTurnsAndCanonicalEndpoints") {
    RoadNetwork network;
    auto        west  = network.addNode(-10.f, 0.f, 0.f);
    auto        hub   = network.addNode(0.f, 0.f, 0.f);
    auto        north = network.addNode(0.f, 0.f, -10.f);
    REQUIRE(west.ok());
    REQUIRE(hub.ok());
    REQUIRE(north.ok());
    auto horizontal = network.addEdge(west.value(), hub.value(), {{-9.f, 0.f, 1.f}, {-1.f, 0.f, 1.f}}, 1, 1);
    auto vertical   = network.addEdge(hub.value(), north.value(), {{1.f, 0.f, -1.f}, {1.f, 0.f, -9.f}}, 1, 1);
    REQUIRE(horizontal.ok());
    REQUIRE(vertical.ok());

    auto anchored = network.edgeResult(horizontal.value());
    REQUIRE(anchored.ok());
    CHECK_EQ(anchored.value().controlPoints.front().x, -10.f);
    CHECK_EQ(anchored.value().controlPoints.front().z, 0.f);
    CHECK_EQ(anchored.value().controlPoints.back().x, 0.f);
    CHECK_EQ(anchored.value().controlPoints.back().z, 0.f);

    auto connected = network.connectAllTurns(hub.value());
    REQUIRE(connected.ok());
    CHECK_EQ(connected.value(), 2);
    CHECK_EQ(network.laneLinkCount(), 2);
    auto idempotent = network.connectAllTurns(hub.value());
    REQUIRE(idempotent.ok());
    CHECK_EQ(idempotent.value(), 0);
    REQUIRE(network.validate().ok());

    bool forwardToForward   = false;
    bool backwardToBackward = false;
    for (const auto& link : network.laneLinks()) {
        forwardToForward |=
            link.inDirection == RoadLaneDirection::Forward && link.outDirection == RoadLaneDirection::Forward;
        backwardToBackward |=
            link.inDirection == RoadLaneDirection::Backward && link.outDirection == RoadLaneDirection::Backward;
    }
    CHECK(forwardToForward);
    CHECK(backwardToBackward);

    RoadStyle horizontalStyle = anchored.value().style;
    horizontalStyle.speedLimitMps   = 11.f;
    horizontalStyle.trafficPriority = 9;
    REQUIRE(network.setEdgeStyle(horizontal.value(), horizontalStyle).ok());
    auto verticalEdge = network.edgeResult(vertical.value());
    REQUIRE(verticalEdge.ok());
    RoadStyle verticalStyle = verticalEdge.value().style;
    verticalStyle.speedLimitMps   = 17.f;
    verticalStyle.trafficPriority = 3;
    REQUIRE(network.setEdgeStyle(vertical.value(), verticalStyle).ok());

    RoadBakeOptions options;
    options.includeJunctions  = true;
    options.includeNavigation = true;
    auto baked                = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    CHECK_EQ(baked.value().overlay.lanes.size(), 4u);
    CHECK_EQ(baked.value().overlay.turns.size(), 2u);
    for (const auto& lane : baked.value().overlay.lanes) {
        const bool knownEdge = lane.inEdge == horizontal.value() || lane.inEdge == vertical.value();
        CHECK(knownEdge);
        CHECK_GE(lane.inLane, 0);
        const bool horizontalLane = lane.inEdge == horizontal.value();
        CHECK_EQ(lane.speedLimitMps, horizontalLane ? 11.f : 17.f);
        CHECK_EQ(lane.trafficPriority, horizontalLane ? 9 : 3);
    }
    for (const auto& turn : baked.value().overlay.turns) {
        CHECK_NE(turn.inEdge, std::uint32_t(0));
        CHECK_NE(turn.outEdge, std::uint32_t(0));
        CHECK_EQ(turn.speedLimitMps, 11.f);
        CHECK_EQ(turn.trafficPriority, turn.inEdge == horizontal.value() ? 9 : 3);
    }
}

TEST_CASE("procgen.road.overlay.shortAcuteTurnStaysLocalAndFinite") {
    RoadNetwork network;
    auto incomingStart = network.addNode(-1.f, 0.f, 0.f);
    auto hub = network.addNode(0.f, 0.f, 0.f, 6.f);
    auto outgoingEnd = network.addNode(-1.f, 0.f, 0.1f);
    REQUIRE(incomingStart.ok());
    REQUIRE(hub.ok());
    REQUIRE(outgoingEnd.ok());
    RoadStyle narrow;
    narrow.laneWidth = 0.2f;
    narrow.sidewalkWidth = 0.f;
    auto incoming = network.addEdge(incomingStart.value(), hub.value(), {{}, {}}, 1, 0, narrow);
    auto outgoing = network.addEdge(hub.value(), outgoingEnd.value(), {{}, {}}, 1, 0, narrow);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());
    REQUIRE(network.addLaneLink({incoming.value(), 0, outgoing.value(), 0, RoadLaneDirection::Forward,
                                 RoadLaneDirection::Forward}).ok());

    RoadBakeOptions options;
    options.includeNavigation = true;
    options.includeJunctions = false;
    auto baked = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    REQUIRE_EQ(baked.value().overlay.turns.size(), std::size_t{1});
    const auto& xyz = baked.value().overlay.turns.front().xyz;
    REQUIRE_EQ(xyz.size(), static_cast<std::size_t>((options.turnSamples + 1) * 3));
    for (std::size_t i = 0; i < xyz.size(); i += 3) {
        REQUIRE(std::isfinite(xyz[i]));
        REQUIRE(std::isfinite(xyz[i + 1]));
        REQUIRE(std::isfinite(xyz[i + 2]));
        const float radial = std::sqrt(xyz[i] * xyz[i] + xyz[i + 2] * xyz[i + 2]);
        CHECK_LE(radial, 1.25f);
    }
}

TEST_CASE("procgen.road.overlay.shortEdgeUsesSameBoundedJunctionTrim") {
    auto network = RoadNetwork::makeScene("tight-turn", 20.f, 4.f, 1, 1);
    REQUIRE(network.ok());
    RoadBakeOptions options;
    options.includeNavigation = true;
    options.includeJunctions = true;
    auto baked = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    REQUIRE_EQ(baked.value().overlay.turns.size(), std::size_t{1});

    const auto& xyz = baked.value().overlay.turns.front().xyz;
    REQUIRE_GE(xyz.size(), std::size_t{6});
    const auto radial = [&](std::size_t offset) {
        return std::sqrt(xyz[offset] * xyz[offset] + xyz[offset + 2] * xyz[offset + 2]);
    };
    // The authored radius is 8 m, but each ~9.5 m arm retains at least half
    // its usable strip. Junction mesh and turn anchors share this clamp.
    CHECK_LT(radial(0), 4.5f);
    CHECK_LT(radial(xyz.size() - 3), 4.5f);
}

TEST_CASE("procgen.road.bake.rejectsUnboundedOrNonFiniteOptions") {
    auto network = RoadNetwork::makeStraight(12.f, 1);
    REQUIRE(network.ok());

    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 4097;
    CHECK(!bakeRoadNetwork(network.value(), options).ok());
    options.pathSegmentsPerEdge = 12;
    options.turnSamples = 4097;
    CHECK(!bakeRoadNetwork(network.value(), options).ok());
    options.turnSamples = 8;
    options.navRibbonHalfWidth = std::numeric_limits<float>::quiet_NaN();
    CHECK(!bakeRoadNetwork(network.value(), options).ok());
    options.navRibbonHalfWidth = 0.22f;
    options.arrowSpacing = 0.f;
    CHECK(!bakeRoadNetwork(network.value(), options).ok());

    options.includeNavigation = false;
    options.junctionChordError = 0.f;
    CHECK(!bakeRoadNetwork(network.value(), options).ok());
    options.junctionChordError = 0.1f;
    REQUIRE(bakeRoadNetwork(network.value(), options).ok());
    options.maximumMeshElements = 1u;
    CHECK(!bakeRoadNetwork(network.value(), options).ok());
    options.maximumMeshElements = 0u;
    CHECK(!bakeRoadNetwork(network.value(), options).ok());
}

TEST_CASE("procgen.road.network.rejectsDuplicateAndInvalidDirectionalLinks") {
    RoadNetwork network;
    auto        a   = network.addNode(-10.f, 0.f, 0.f);
    auto        hub = network.addNode(0.f, 0.f, 0.f);
    auto        b   = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(a.ok());
    REQUIRE(hub.ok());
    REQUIRE(b.ok());
    auto incoming = network.addEdge(a.value(), hub.value(), {{-10.f, 0.f, 0.f}, {0.f, 0.f, 0.f}}, 1, 0);
    auto outgoing = network.addEdge(hub.value(), b.value(), {{0.f, 0.f, 0.f}, {10.f, 0.f, 0.f}}, 1, 0);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());
    RoadLaneConnection link{incoming.value(), 0, outgoing.value(), 0};
    REQUIRE(network.addLaneLink(link).ok());
    CHECK(!network.addLaneLink(link).ok());
    link.inDirection = RoadLaneDirection::Backward;
    CHECK(!network.addLaneLink(link).ok());
}

TEST_CASE("procgen.road.recipe.meshAndTextureDeterministic") {
    MeshRecipeRegistry::instance().registerBuiltins();
    TextureRecipeRegistry::instance().registerBuiltins();
    REQUIRE(MeshRecipeRegistry::instance().has("mesh.roadNetwork"));
    REQUIRE(TextureRecipeRegistry::instance().has("tex.roadMarkings"));

    Params params;
    params.setSeed(11);
    params.setFloat("span", 36.f);
    params.setFloat("bridgeHeight", 6.f);
    params.setInt("lanes", 2);
    params.setInt("pathSegments", 20);

    MeshBuild first, second;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.roadNetwork", params, first, error));
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.roadNetwork", params, second, error));
    CHECK_EQ(first.getVertexCount(), second.getVertexCount());
    CHECK_EQ(first.getIndexCount(), second.getIndexCount());

    params.setSize(128, 32);
    auto tex = TextureRecipeRegistry::instance().generate("tex.roadMarkings", params, error);
    REQUIRE(tex);
    CHECK_EQ(tex->getWidth(), 128);
    CHECK_EQ(tex->getHeight(), 32);
}

TEST_CASE("procgen.road.network.autoTurnsBalanceLaneCountChangesAndRespectOverrides") {
    RoadNetwork network;
    auto        west = network.addNode(-20.f, 0.f, 0.f);
    auto        hub  = network.addNode(0.f, 0.f, 0.f);
    auto        east = network.addNode(20.f, 0.f, 0.f);
    REQUIRE(west.ok());
    REQUIRE(hub.ok());
    REQUIRE(east.ok());
    auto incoming = network.addEdge(west.value(), hub.value(), {{-20.f, 0.f, 0.f}, {0.f, 0.f, 0.f}}, 4, 0);
    auto outgoing = network.addEdge(hub.value(), east.value(), {{0.f, 0.f, 0.f}, {20.f, 0.f, 0.f}}, 2, 0);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());

    const RoadLaneConnection authored{incoming.value(), 1, outgoing.value(), 0,
                                      RoadLaneDirection::Forward, RoadLaneDirection::Forward};
    REQUIRE(network.addLaneLink(authored).ok());
    const auto beforeConnectRevision = network.revision();
    auto connected = network.connectAllTurns(hub.value());
    REQUIRE(connected.ok());
    CHECK_EQ(connected.value(), 3);
    CHECK_EQ(network.revision(), beforeConnectRevision + 1);
    CHECK_EQ(network.laneLinkCount(), 4);

    int mapped[4] = {-1, -1, -1, -1};
    for (const auto& link : network.laneLinks()) {
        REQUIRE_EQ(link.inEdge, incoming.value());
        REQUIRE_EQ(link.outEdge, outgoing.value());
        mapped[link.inLane] = link.outLane;
    }
    CHECK_EQ(mapped[0], 0);
    CHECK_EQ(mapped[1], 0);  // authored override is retained without a parallel default link
    CHECK_EQ(mapped[2], 1);
    CHECK_EQ(mapped[3], 1);
    REQUIRE(network.validate().ok());
    const auto stableRevision = network.revision();
    auto idempotent = network.connectAllTurns(hub.value());
    REQUIRE(idempotent.ok());
    CHECK_EQ(idempotent.value(), 0);
    CHECK_EQ(network.revision(), stableRevision);

    RoadNetwork expansion;
    auto        a = expansion.addNode(-20.f, 0.f, 8.f);
    auto        b = expansion.addNode(0.f, 0.f, 8.f);
    auto        c = expansion.addNode(20.f, 0.f, 8.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(c.ok());
    auto twoLane  = expansion.addEdge(a.value(), b.value(), {{-20.f, 0.f, 8.f}, {0.f, 0.f, 8.f}}, 2, 0);
    auto fourLane = expansion.addEdge(b.value(), c.value(), {{0.f, 0.f, 8.f}, {20.f, 0.f, 8.f}}, 4, 0);
    REQUIRE(twoLane.ok());
    REQUIRE(fourLane.ok());
    REQUIRE(expansion.connectAllTurns(b.value()).ok());
    REQUIRE_EQ(expansion.laneLinkCount(), 2);
    CHECK_EQ(expansion.laneLinks()[0].outLane, 0);
    CHECK_EQ(expansion.laneLinks()[1].outLane, 3);
}

TEST_CASE("procgen.road.network.blockedTurnSurvivesAutomaticReconnectAndEdgeReverse") {
    RoadNetwork network;
    auto west = network.addNode(-10.f, 0.f, 0.f);
    auto hub = network.addNode(0.f, 0.f, 0.f);
    auto east = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(west.ok());
    REQUIRE(hub.ok());
    REQUIRE(east.ok());
    auto incoming = network.addEdge(west.value(), hub.value(), {{}, {}}, 1, 0);
    auto outgoing = network.addEdge(hub.value(), east.value(), {{}, {}}, 1, 0);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());
    auto connected = network.connectAllTurns(hub.value());
    REQUIRE(connected.ok());
    CHECK_EQ(connected.value(), 1);
    const RoadLaneConnection turn{incoming.value(), 0, outgoing.value(), 0, RoadLaneDirection::Forward,
                                  RoadLaneDirection::Forward};
    auto blocked = network.blockLaneLink(turn);
    REQUIRE(blocked.ok());
    CHECK(blocked.value());
    CHECK_EQ(network.laneLinkCount(), 0);
    CHECK_EQ(network.blockedLaneLinkCount(), 1);
    connected = network.connectAllTurns(hub.value());
    REQUIRE(connected.ok());
    CHECK_EQ(connected.value(), 0);

    REQUIRE(network.reverseEdge(incoming.value()).ok());
    REQUIRE_EQ(network.blockedLaneLinkCount(), 1);
    CHECK_EQ(network.blockedLaneLinks().front().inDirection, RoadLaneDirection::Backward);
    connected = network.connectAllTurns(hub.value());
    REQUIRE(connected.ok());
    CHECK_EQ(connected.value(), 0);
    REQUIRE(network.reverseEdge(incoming.value()).ok());
    CHECK_EQ(network.blockedLaneLinks().front().inDirection, RoadLaneDirection::Forward);
    REQUIRE(network.unblockLaneLink(turn).ok());
    connected = network.connectAllTurns(hub.value());
    REQUIRE(connected.ok());
    CHECK_EQ(connected.value(), 1);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.network.reverseEdgePreservesPhysicalConnectivity") {
    RoadNetwork network;
    auto west = network.addNode(-10.f, 0.f, 0.f);
    auto hub  = network.addNode(0.f, 0.f, 0.f);
    auto east = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(west.ok());
    REQUIRE(hub.ok());
    REQUIRE(east.ok());
    RoadStyle style;
    style.sideObjectStartOffset = 2.f;
    style.sideObjectEndOffset   = 5.f;
    style.sideObjectsLeft       = true;
    style.sideObjectsRight      = false;
    auto incoming = network.addEdge(west.value(), hub.value(), {{-10.f, 0.f, 0.f}, {-4.f, 1.f, 2.f}, {}}, 2, 1, style);
    auto outgoing = network.addEdge(hub.value(), east.value(), {{}, {10.f, 0.f, 0.f}}, 1, 0);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());
    RoadLaneConnection link{incoming.value(), 1, outgoing.value(), 0,
                            RoadLaneDirection::Forward, RoadLaneDirection::Forward};
    REQUIRE(network.addLaneLink(link).ok());
    const auto original = network.edgeResult(incoming.value()).value();
    const auto beforeRevision = network.revision();

    REQUIRE(network.reverseEdge(incoming.value()).ok());
    auto reversed = network.edgeResult(incoming.value());
    REQUIRE(reversed.ok());
    CHECK_EQ(network.revision(), beforeRevision + 1);
    CHECK_EQ(reversed.value().from, original.to);
    CHECK_EQ(reversed.value().to, original.from);
    CHECK_EQ(reversed.value().lanesForward, original.lanesBackward);
    CHECK_EQ(reversed.value().lanesBackward, original.lanesForward);
    CHECK_EQ(reversed.value().controlPoints.front().x, original.controlPoints.back().x);
    CHECK_EQ(reversed.value().style.sideObjectStartOffset, 5.f);
    CHECK_EQ(reversed.value().style.sideObjectEndOffset, 2.f);
    CHECK(!reversed.value().style.sideObjectsLeft);
    CHECK(reversed.value().style.sideObjectsRight);
    REQUIRE_EQ(network.laneLinks().size(), std::size_t{1});
    CHECK_EQ(static_cast<int>(network.laneLinks().front().inDirection),
             static_cast<int>(RoadLaneDirection::Backward));
    REQUIRE(network.validate().ok());

    REQUIRE(network.reverseEdge(incoming.value()).ok());
    auto restored = network.edgeResult(incoming.value());
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value().from, original.from);
    CHECK_EQ(restored.value().to, original.to);
    CHECK_EQ(restored.value().controlPoints[1].x, original.controlPoints[1].x);
    CHECK_EQ(static_cast<int>(network.laneLinks().front().inDirection),
             static_cast<int>(RoadLaneDirection::Forward));
    REQUIRE(network.validate().ok());

    const auto oneWayRevision = network.revision();
    REQUIRE(network.reverseEdge(outgoing.value()).ok());
    auto reversedOneWay = network.edgeResult(outgoing.value());
    REQUIRE(reversedOneWay.ok());
    CHECK_EQ(reversedOneWay.value().lanesForward, 0);
    CHECK_EQ(reversedOneWay.value().lanesBackward, 1);
    CHECK_EQ(network.revision(), oneWayRevision + 1);
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.network.reconnectEndpointReanchorsAndPrunesOldJunctionTurns") {
    RoadNetwork network;
    auto start = network.addNode(-12.f, 0.f, 0.f);
    auto oldHub = network.addNode(0.f, 0.f, 0.f);
    auto newHub = network.addNode(4.f, 1.f, 6.f);
    auto exit = network.addNode(0.f, 0.f, 12.f);
    REQUIRE(start.ok());
    REQUIRE(oldHub.ok());
    REQUIRE(newHub.ok());
    REQUIRE(exit.ok());
    auto road = network.addEdge(start.value(), oldHub.value(), {{}, {}}, 2, 0);
    auto branch = network.addEdge(oldHub.value(), exit.value(), {{}, {}}, 2, 0);
    REQUIRE(road.ok());
    REQUIRE(branch.ok());
    const RoadLaneConnection active{road.value(), 0, branch.value(), 0, RoadLaneDirection::Forward,
                                    RoadLaneDirection::Forward};
    const RoadLaneConnection blocked{road.value(), 1, branch.value(), 1, RoadLaneDirection::Forward,
                                     RoadLaneDirection::Forward};
    REQUIRE(network.addLaneLink(active).ok());
    REQUIRE(network.blockLaneLink(blocked).ok());

    const auto beforeRevision = network.revision();
    auto changed = network.reconnectEdgeEndpoint(road.value(), false, newHub.value());
    REQUIRE(changed.ok());
    CHECK_EQ(changed.value(), 2);
    CHECK_EQ(network.revision(), beforeRevision + 1);
    auto reconnected = network.edgeResult(road.value());
    REQUIRE(reconnected.ok());
    CHECK_EQ(reconnected.value().to, newHub.value());
    const auto& endpoint = reconnected.value().controlPoints.back();
    CHECK_EQ(endpoint.x, 4.f);
    CHECK_EQ(endpoint.y, 1.f);
    CHECK_EQ(endpoint.z, 6.f);
    CHECK_EQ(network.laneLinkCount(), 0);
    CHECK_EQ(network.blockedLaneLinkCount(), 0);
    REQUIRE(network.validate().ok());

    const auto stableRevision = network.revision();
    CHECK(!network.reconnectEdgeEndpoint(road.value(), false, start.value()).ok());
    CHECK_EQ(network.revision(), stableRevision);
    CHECK_EQ(network.edgeResult(road.value()).value().to, newHub.value());
}

TEST_CASE("procgen.road.network.detachEndpointCreatesCoincidentStableNodeAtomically") {
    RoadNetwork network;
    auto start = network.addNode(-8.f, 1.f, 0.f);
    auto hub = network.addNode(0.f, 2.f, 0.f, 3.f);
    auto exit = network.addNode(0.f, 2.f, 8.f);
    REQUIRE(start.ok());
    REQUIRE(hub.ok());
    REQUIRE(exit.ok());
    auto road = network.addEdge(start.value(), hub.value(), {{}, {}}, 1, 0);
    auto branch = network.addEdge(hub.value(), exit.value(), {{}, {}}, 1, 0);
    REQUIRE(road.ok());
    REQUIRE(branch.ok());
    REQUIRE(network.addLaneLink({road.value(), 0, branch.value(), 0, RoadLaneDirection::Forward,
                                 RoadLaneDirection::Forward}).ok());
    const auto beforeRevision = network.revision();

    auto detached = network.detachEdgeEndpoint(road.value(), false);
    REQUIRE(detached.ok());
    CHECK_EQ(network.revision(), beforeRevision + 1);
    CHECK_EQ(network.nodeCount(), 4);
    CHECK_EQ(network.edgeResult(road.value()).value().to, detached.value());
    auto newNode = network.nodeResult(detached.value());
    REQUIRE(newNode.ok());
    CHECK_EQ(newNode.value().x, 0.f);
    CHECK_EQ(newNode.value().y, 2.f);
    CHECK_EQ(newNode.value().z, 0.f);
    CHECK_EQ(newNode.value().junctionRadius, 3.f);
    CHECK_EQ(network.laneLinkCount(), 0);
    REQUIRE(network.validate().ok());

    RoadNetwork isolated;
    auto isolatedStart = isolated.addNode(0.f, 0.f, 0.f);
    auto isolatedEnd = isolated.addNode(4.f, 0.f, 0.f);
    REQUIRE(isolatedStart.ok());
    REQUIRE(isolatedEnd.ok());
    auto isolatedRoad = isolated.addEdge(isolatedStart.value(), isolatedEnd.value(), {{}, {}}, 1, 0);
    REQUIRE(isolatedRoad.ok());
    const auto isolatedRevision = isolated.revision();
    CHECK(!isolated.detachEdgeEndpoint(isolatedRoad.value(), false).ok());
    CHECK_EQ(isolated.revision(), isolatedRevision);
    CHECK_EQ(isolated.nodeCount(), 2);
    CHECK_EQ(isolated.edgeResult(isolatedRoad.value()).value().to, isolatedEnd.value());
}

TEST_CASE("procgen.road.network.splitEdgeMigratesEndpointLinksAndConnectsHalves") {
    RoadNetwork network;
    auto        a = network.addNode(-20.f, 0.f, 0.f);
    auto        b = network.addNode(20.f, 0.f, 0.f);
    auto        c = network.addNode(20.f, 0.f, -20.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(c.ok());
    auto road = network.addEdge(a.value(), b.value(),
                                {{-20.f, 0.f, 0.f}, {0.f, 1.f, 3.f}, {20.f, 0.f, 0.f}}, 2, 1);
    auto exit = network.addEdge(b.value(), c.value(), {{20.f, 0.f, 0.f}, {20.f, 0.f, -20.f}}, 1, 0);
    REQUIRE(road.ok());
    REQUIRE(exit.ok());
    REQUIRE(network.addLaneLink({road.value(), 0, exit.value(), 0, RoadLaneDirection::Forward,
                                 RoadLaneDirection::Forward})
                .ok());

    const auto beforeRevision = network.revision();
    auto       split          = network.splitEdge(road.value(), 1, 4.f);
    REQUIRE(split.ok());
    CHECK_EQ(network.revision(), beforeRevision + 1);
    CHECK_EQ(split.value().firstEdgeId, road.value());
    CHECK_NE(split.value().secondEdgeId, road.value());
    auto first  = network.edgeResult(split.value().firstEdgeId);
    auto second = network.edgeResult(split.value().secondEdgeId);
    auto node   = network.nodeResult(split.value().nodeId);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(node.ok());
    CHECK_EQ(first.value().to, split.value().nodeId);
    CHECK_EQ(second.value().from, split.value().nodeId);
    CHECK_EQ(second.value().to, b.value());
    CHECK_EQ(first.value().controlPoints.size(), std::size_t{2});
    CHECK_EQ(second.value().controlPoints.size(), std::size_t{2});
    CHECK_EQ(node.value().x, 0.f);
    CHECK_EQ(node.value().y, 1.f);

    bool migratedEndpoint = false;
    int  forwardInternal = 0, backwardInternal = 0;
    for (const auto& link : network.laneLinks()) {
        migratedEndpoint |= link.inEdge == split.value().secondEdgeId && link.outEdge == exit.value();
        if (link.inEdge == split.value().firstEdgeId && link.outEdge == split.value().secondEdgeId)
            ++forwardInternal;
        if (link.inEdge == split.value().secondEdgeId && link.outEdge == split.value().firstEdgeId)
            ++backwardInternal;
    }
    CHECK(migratedEndpoint);
    CHECK_EQ(forwardInternal, 2);
    CHECK_EQ(backwardInternal, 1);
    REQUIRE(network.validate().ok());

    const auto stableRevision = network.revision();
    CHECK(!network.splitEdge(split.value().firstEdgeId, 0).ok());
    CHECK_EQ(network.revision(), stableRevision);
}

TEST_CASE("procgen.road.network.positionSplitSnapsIn3dAndRejectsEndpoints") {
    RoadNetwork network;
    auto from = network.addNode(-10.f, 5.f, 0.f);
    auto to = network.addNode(10.f, 5.f, 0.f);
    REQUIRE(from.ok());
    REQUIRE(to.ok());
    auto road = network.addEdge(from.value(), to.value(), {{-10.f, 5.f, 0.f}, {10.f, 5.f, 0.f}}, 1, 0);
    REQUIRE(road.ok());

    const auto unchangedRevision = network.revision();
    CHECK(!network.splitEdgeAtPosition(road.value(), {0.f, 0.f, 0.f}, 2.f).ok());
    CHECK_EQ(network.revision(), unchangedRevision);
    CHECK(!network.splitEdgeAtPosition(road.value(), {-10.f, 5.f, 0.f}, 0.1f).ok());
    CHECK_EQ(network.revision(), unchangedRevision);

    auto split = network.splitEdgeAtPosition(road.value(), {2.f, 7.f, 1.f}, 3.f, 3.5f);
    REQUIRE(split.ok());
    auto node = network.nodeResult(split.value().nodeId);
    REQUIRE(node.ok());
    CHECK(std::fabs(node.value().x - 2.f) < 1e-5f);
    CHECK(std::fabs(node.value().y - 5.f) < 1e-5f);
    CHECK(std::fabs(node.value().z) < 1e-5f);
    CHECK_EQ(network.edgeResult(split.value().firstEdgeId).value().controlPoints.size(), std::size_t{2});
    CHECK_EQ(network.edgeResult(split.value().secondEdgeId).value().controlPoints.size(), std::size_t{2});
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.network.splineParameterSplitUsesBakedCatmullCenterline") {
    RoadNetwork network;
    auto start = network.addNode(-10.f, 0.f, -10.f);
    auto end = network.addNode(10.f, 0.f, -10.f);
    REQUIRE(start.ok());
    REQUIRE(end.ok());
    auto edge = network.addEdge(start.value(), end.value(),
                                {{-10.f, 0.f, -10.f}, {-3.f, 0.f, 10.f},
                                 {3.f, 0.f, 10.f}, {10.f, 0.f, -10.f}}, 1, 0);
    REQUIRE(edge.ok());
    const auto revision = network.revision();
    auto split = network.splitEdgeAtSplineParameter(edge.value(), 0.5f, 4.f);
    REQUIRE(split.ok());
    CHECK_EQ(network.revision(), revision + 1);
    auto node = network.nodeResult(split.value().nodeId);
    REQUIRE(node.ok());
    CHECK(std::fabs(node.value().x) < 1e-5f);
    CHECK(node.value().z > 10.f);
    CHECK_EQ(network.edgeResult(edge.value()).value().to, split.value().nodeId);
    CHECK_EQ(network.edgeResult(split.value().secondEdgeId).value().from, split.value().nodeId);
    REQUIRE(network.validate().ok());

    const auto unchangedRevision = network.revision();
    CHECK(!network.splitEdgeAtSplineParameter(edge.value(), 0.f).ok());
    CHECK_EQ(network.revision(), unchangedRevision);
}

TEST_CASE("procgen.road.network.mergeNodesReanchorsEndpointsAndEnablesTurns") {
    RoadNetwork network;
    auto west = network.addNode(-10.f, 0.f, 0.f);
    auto keep = network.addNode(0.f, 0.f, 0.f);
    auto nearby = network.addNode(0.25f, 0.f, 0.f);
    auto east = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(west.ok());
    REQUIRE(keep.ok());
    REQUIRE(nearby.ok());
    REQUIRE(east.ok());
    auto incoming = network.addEdge(west.value(), keep.value(), {{}, {}}, 1, 0);
    auto outgoing = network.addEdge(nearby.value(), east.value(), {{}, {}}, 1, 0);
    REQUIRE(incoming.ok());
    REQUIRE(outgoing.ok());

    const auto beforeRevision = network.revision();
    CHECK(!network.mergeNodes(keep.value(), nearby.value(), 0.1f).ok());
    CHECK_EQ(network.revision(), beforeRevision);
    auto merged = network.mergeNodes(keep.value(), nearby.value(), 0.5f);
    REQUIRE(merged.ok());
    CHECK_EQ(merged.value(), 1);
    CHECK_EQ(network.nodeCount(), 3);
    CHECK(!network.nodeResult(nearby.value()).ok());
    auto rewired = network.edgeResult(outgoing.value());
    REQUIRE(rewired.ok());
    CHECK_EQ(rewired.value().from, keep.value());
    CHECK_EQ(rewired.value().controlPoints.front().x, 0.f);
    auto turns = network.connectAllTurns(keep.value());
    REQUIRE(turns.ok());
    CHECK_EQ(turns.value(), 1);
    REQUIRE(network.validate().ok());

    RoadNetwork selfLoop;
    auto a = selfLoop.addNode(0.f, 0.f, 0.f);
    auto b = selfLoop.addNode(0.1f, 0.f, 0.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(selfLoop.addEdge(a.value(), b.value(), {{}, {}}, 1, 0).ok());
    const auto stableRevision = selfLoop.revision();
    CHECK(!selfLoop.mergeNodes(a.value(), b.value(), 1.f).ok());
    CHECK_EQ(selfLoop.revision(), stableRevision);
}

TEST_CASE("procgen.road.asphaltPbr.reproducibleAndBounded") {
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    REQUIRE(PbrRecipeRegistry::instance().has("pbr.asphalt"));

    Params params;
    params.setSeed(17);
    params.setSize(64, 64);
    std::string error;
    auto first  = PbrRecipeRegistry::instance().generate("pbr.asphalt", params, error);
    auto second = PbrRecipeRegistry::instance().generate("pbr.asphalt", params, error);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first->albedo != nullptr);
    REQUIRE(first->normal != nullptr);
    REQUIRE(first->height != nullptr);
    CHECK_EQ(first->albedo->getWidth(), 64);
    CHECK_EQ(first->albedo->getHeight(), 64);
    CHECK(std::memcmp(first->albedo->getData(), second->albedo->getData(), first->albedo->getSize()) == 0);

    const auto* pixels = static_cast<const std::uint8_t*>(first->albedo->getData());
    std::uint8_t minimum = 255;
    std::uint8_t maximum = 0;
    for (std::size_t i = 0; i < first->albedo->getSize(); i += 4u) {
        minimum = std::min(minimum, pixels[i]);
        maximum = std::max(maximum, pixels[i]);
    }
    CHECK_GE(minimum, std::uint8_t{120});
    CHECK_GT(maximum, minimum);
}

TEST_CASE("procgen.road.overlay.fromParams") {
    auto network = RoadNetwork::makeInterchange(40.f, 7.f, 2, 3);
    REQUIRE(network.ok());
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 24;
    options.turnSamples         = 8;
    options.includeNavigation   = true;
    auto baked                  = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    CHECK_GT(baked.value().overlay.lanes.size(), 0u);
    CHECK_GT(baked.value().overlay.turns.size(), 0u);
}

TEST_CASE("procgen.road.network.seedSevenNavigationTerminates") {
    // Seed 7 previously produced large frame steps that hung the nav arrow loop.
    auto network = RoadNetwork::makeInterchange(40.f, 7.f, 2, 7);
    REQUIRE(network.ok());
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 24;
    options.turnSamples         = 8;
    options.includeNavigation   = true;
    auto baked                  = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    CHECK_GT(baked.value().mesh.getVertexCount(), 100);
    CHECK_GT(baked.value().overlay.lanes.size(), 0u);
}

TEST_CASE("procgen.road.placements.exposeReusableTransitionAndSideObjectAnchors") {
    auto network = RoadNetwork::makeStraight(24.f, 2);
    REQUIRE(network.ok());
    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includeJunctions  = false;
    options.sideObjectSpacing = 8.f;
    options.sideObjectOffset  = 0.75f;
    auto first                = bakeRoadNetwork(network.value(), options);
    auto second               = bakeRoadNetwork(network.value(), options);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE_EQ(first.value().placements.getCount(), 8);
    REQUIRE_EQ(second.value().placements.getCount(), first.value().placements.getCount());

    int startCount = 0, endCount = 0, leftCount = 0, rightCount = 0;
    std::vector<std::uint64_t> ids;
    for (int i = 0; i < first.value().placements.getCount(); ++i) {
        const auto role = first.value().placements.getStringAttribute(i, "road_role", "");
        if (role == "transition.start") ++startCount;
        if (role == "transition.end") ++endCount;
        if (role == "side.left") ++leftCount;
        if (role == "side.right") ++rightCount;
        CHECK_EQ(first.value().placements.getIntAttribute(i, "road_edge_id", 0), 1);
        CHECK(first.value().placements.hasVectorAttribute(i, "road_direction"));
        CHECK(std::isfinite(first.value().placements.getYaw(i)));
        CHECK_NE(first.value().placements.getPointId(i), std::uint64_t(0));
        CHECK_EQ(first.value().placements.getPointId(i), second.value().placements.getPointId(i));
        CHECK_EQ(first.value().placements.getX(i), second.value().placements.getX(i));
        CHECK_EQ(first.value().placements.getZ(i), second.value().placements.getZ(i));
        ids.push_back(first.value().placements.getPointId(i));
    }
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    CHECK_EQ(startCount, 1);
    CHECK_EQ(endCount, 1);
    CHECK_EQ(leftCount, 3);
    CHECK_EQ(rightCount, 3);

    const auto leftAnchors = filterPointStringAttribute(first.value().placements, "road_role", "side.left", false);
    REQUIRE_EQ(leftAnchors.getCount(), 3);
    ObjectBuildLayer objects;
    REQUIRE(objects.addAsset("asset://road/guardrail-a", 3.f).ok());
    REQUIRE(objects.addAsset("asset://road/guardrail-b", 1.f).ok());
    objects.setSeed(17);
    REQUIRE(objects.setRandomRotation(0.f, 0.f, -4.f, 4.f, 0.f, 0.f).ok());
    auto instancesA = objects.build(leftAnchors);
    auto instancesB = objects.build(leftAnchors);
    REQUIRE(instancesA.ok());
    REQUIRE(instancesB.ok());
    REQUIRE_EQ(instancesA.value().getCount(), 3);
    for (int i = 0; i < instancesA.value().getCount(); ++i) {
        CHECK_EQ(instancesA.value().getStringAttribute(i, "road_role", ""), std::string("side.left"));
        CHECK(!instancesA.value().getStringAttribute(i, "asset", "").empty());
        CHECK_EQ(instancesA.value().getStringAttribute(i, "asset", ""),
                 instancesB.value().getStringAttribute(i, "asset", ""));
        CHECK_EQ(instancesA.value().getYaw(i), instancesB.value().getYaw(i));
    }

    options.maximumPlacements = 7;
    auto overBudget           = bakeRoadNetwork(network.value(), options);
    CHECK(!overBudget.ok());
}

TEST_CASE("procgen.road.placements.classifyReusableJunctionPrefabAnchors") {
    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includeJunctions  = false;
    options.sideObjectSpacing = 1000.f;

    auto cross = RoadNetwork::makeCross(32.f, 1);
    REQUIRE(cross.ok());
    auto crossBake = bakeRoadNetwork(cross.value(), options);
    REQUIRE(crossBake.ok());
    const auto xAnchors = filterPointStringAttribute(crossBake.value().placements, "road_role", "junction.x", false);
    REQUIRE_EQ(xAnchors.getCount(), 1);
    CHECK_EQ(xAnchors.getIntAttribute(0, "road_arm_count", 0), 4);
    CHECK_NE(xAnchors.getIntAttribute(0, "road_node_id", 0), 0);

    RoadNetwork tNetwork;
    auto        tHub   = tNetwork.addNode(0.f, 0.f, 0.f, 4.f);
    auto        tWest  = tNetwork.addNode(-16.f, 0.f, 0.f, 2.f);
    auto        tEast  = tNetwork.addNode(16.f, 0.f, 0.f, 2.f);
    auto        tNorth = tNetwork.addNode(0.f, 0.f, -16.f, 2.f);
    REQUIRE(tHub.ok());
    REQUIRE(tWest.ok());
    REQUIRE(tEast.ok());
    REQUIRE(tNorth.ok());
    REQUIRE(tNetwork.addEdge(tWest.value(), tHub.value(), {{}, {}}, 1).ok());
    REQUIRE(tNetwork.addEdge(tHub.value(), tEast.value(), {{}, {}}, 1).ok());
    REQUIRE(tNetwork.addEdge(tHub.value(), tNorth.value(), {{}, {}}, 1).ok());
    auto tBake = bakeRoadNetwork(tNetwork, options);
    REQUIRE(tBake.ok());
    const auto tAnchors = filterPointStringAttribute(tBake.value().placements, "road_role", "junction.t", false);
    REQUIRE_EQ(tAnchors.getCount(), 1);
    CHECK(tAnchors.hasVectorAttribute(0, "road_direction"));
    const auto tIslands = filterPointStringAttribute(tBake.value().placements, "road_role",
                                                     "junction.channelizing.island", false);
    REQUIRE_EQ(tIslands.getCount(), 1);
    CHECK_EQ(tIslands.getIntAttribute(0, "road_node_id", 0), tHub.value());
    CHECK_EQ(tIslands.getIntAttribute(0, "road_arm_count", 0), 3);
    CHECK_GT(std::hypot(tIslands.getX(0), tIslands.getZ(0)), 1.f);
    CHECK(tIslands.hasVectorAttribute(0, "road_direction"));

    RoadNetwork yNetwork;
    auto        yHub = yNetwork.addNode(0.f, 0.f, 0.f, 4.f);
    auto        yA   = yNetwork.addNode(0.f, 0.f, -16.f, 2.f);
    auto        yB   = yNetwork.addNode(-14.f, 0.f, 8.f, 2.f);
    auto        yC   = yNetwork.addNode(14.f, 0.f, 8.f, 2.f);
    REQUIRE(yHub.ok());
    REQUIRE(yA.ok());
    REQUIRE(yB.ok());
    REQUIRE(yC.ok());
    REQUIRE(yNetwork.addEdge(yA.value(), yHub.value(), {{}, {}}, 1).ok());
    // A duplicated endpoint control sample must not corrupt arm classification.
    REQUIRE(yNetwork.addEdge(yHub.value(), yB.value(), {{}, {}, {}}, 1).ok());
    REQUIRE(yNetwork.addEdge(yHub.value(), yC.value(), {{}, {}}, 1).ok());
    auto yBake = bakeRoadNetwork(yNetwork, options);
    REQUIRE(yBake.ok());
    const auto yAnchors = filterPointStringAttribute(yBake.value().placements, "road_role", "junction.y", false);
    REQUIRE_EQ(yAnchors.getCount(), 1);
    CHECK_EQ(yAnchors.getIntAttribute(0, "road_arm_count", 0), 3);
    const auto yIslands = filterPointStringAttribute(yBake.value().placements, "road_role",
                                                     "junction.channelizing.island", false);
    REQUIRE_EQ(yIslands.getCount(), 1);
    CHECK_EQ(yIslands.getIntAttribute(0, "road_node_id", 0), yHub.value());
    CHECK_GT(yIslands.getFloatAttribute(0, "road_distance", 0.f), 1.f);

    options.maximumPlacements = 8;
    CHECK(!bakeRoadNetwork(cross.value(), options).ok());
}

TEST_CASE("procgen.road.placements.bakePriorityAwareJunctionControlAnchors") {
    RoadNetwork network;
    auto hub   = network.addNode(0.f, 0.f, 0.f, 5.f);
    auto west  = network.addNode(-20.f, 0.f, 0.f, 2.f);
    auto east  = network.addNode(20.f, 0.f, 0.f, 2.f);
    auto south = network.addNode(0.f, 0.f, 20.f, 2.f);
    REQUIRE(hub.ok());
    REQUIRE(west.ok());
    REQUIRE(east.ok());
    REQUIRE(south.ok());
    RoadStyle major;
    major.trafficPriority = 9;
    RoadStyle minor = major;
    minor.trafficPriority = 2;
    auto westEdge = network.addEdge(west.value(), hub.value(), {{}, {}}, 1, 1, major);
    auto eastEdge = network.addEdge(east.value(), hub.value(), {{}, {}}, 1, 1, major);
    auto minorEdge = network.addEdge(south.value(), hub.value(), {{}, {}}, 1, 1, minor);
    REQUIRE(westEdge.ok());
    REQUIRE(eastEdge.ok());
    REQUIRE(minorEdge.ok());
    REQUIRE(network.setNodeJunctionControl(hub.value(), RoadJunctionControl::Yield).ok());

    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includeJunctions  = false;
    options.sideObjectSpacing = 1000.f;
    auto first = bakeRoadNetwork(network, options);
    auto second = bakeRoadNetwork(network, options);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    const auto yield = filterPointStringAttribute(first.value().placements, "road_role",
                                                  "junction.control.yield", false);
    REQUIRE_EQ(yield.getCount(), 1);
    CHECK_EQ(yield.getIntAttribute(0, "road_node_id", 0), hub.value());
    CHECK_EQ(yield.getIntAttribute(0, "road_edge_id", 0), minorEdge.value());
    CHECK_EQ(yield.getIntAttribute(0, "traffic_priority", -1), 2);
    CHECK_EQ(yield.getIntAttribute(0, "road_lane_direction", -1),
             static_cast<int>(RoadLaneDirection::Forward));
    CHECK(yield.hasVectorAttribute(0, "road_direction"));
    const auto yieldAgain = filterPointStringAttribute(second.value().placements, "road_role",
                                                       "junction.control.yield", false);
    REQUIRE_EQ(yieldAgain.getCount(), 1);
    CHECK_EQ(yield.getPointId(0), yieldAgain.getPointId(0));
    CHECK_GT(std::hypot(yield.getX(0), yield.getZ(0)), 4.f);

    REQUIRE(network.setNodeJunctionControl(hub.value(), RoadJunctionControl::Signal).ok());
    auto signalled = bakeRoadNetwork(network, options);
    REQUIRE(signalled.ok());
    const auto signals = filterPointStringAttribute(signalled.value().placements, "road_role",
                                                    "junction.control.signal", false);
    REQUIRE_EQ(signals.getCount(), 3);
    std::vector<std::int64_t> controlledEdges;
    for (int row = 0; row < signals.getCount(); ++row)
        controlledEdges.push_back(signals.getIntAttribute(row, "road_edge_id", 0));
    std::sort(controlledEdges.begin(), controlledEdges.end());
    CHECK_EQ(controlledEdges[0], std::int64_t{westEdge.value()});
    CHECK_EQ(controlledEdges[1], std::int64_t{eastEdge.value()});
    CHECK_EQ(controlledEdges[2], std::int64_t{minorEdge.value()});
}

TEST_CASE("procgen.road.placements.respectPerEdgeSideIntervals") {
    auto network = RoadNetwork::makeStraight(24.f, 1);
    REQUIRE(network.ok());
    const auto edgeId = network.value().edges().front().id;
    auto       style  = network.value().edges().front().style;
    style.sideObjectStartOffset = 5.f;
    style.sideObjectEndOffset   = 3.f;
    style.sideObjectsLeft       = true;
    style.sideObjectsRight      = false;
    REQUIRE(network.value().setEdgeStyle(edgeId, style).ok());

    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includeJunctions  = false;
    options.sideObjectSpacing = 8.f;
    auto baked                = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    REQUIRE_EQ(baked.value().placements.getCount(), 4);

    const auto left = filterPointStringAttribute(baked.value().placements, "road_role", "side.left", false);
    const auto right = filterPointStringAttribute(baked.value().placements, "road_role", "side.right", false);
    REQUIRE_EQ(left.getCount(), 2);
    CHECK_EQ(right.getCount(), 0);
    CHECK_EQ(left.getFloatAttribute(0, "road_distance", -1.f), 9.f);
    CHECK_EQ(left.getFloatAttribute(1, "road_distance", -1.f), 17.f);

    options.maximumPlacements = 3;
    CHECK(!bakeRoadNetwork(network.value(), options).ok());

    style.sideObjectStartOffset = -1.f;
    const auto stableRevision = network.value().revision();
    CHECK(!network.value().setEdgeStyle(edgeId, style).ok());
    CHECK_EQ(network.value().revision(), stableRevision);
}

TEST_CASE("procgen.road.placements.keepSideObjectsOutsideJunctionSockets") {
    auto network = RoadNetwork::makeCross(40.f, 2);
    REQUIRE(network.ok());
    std::uint32_t hubId = 0;
    float         hubRadius = 0.f;
    for (const auto& node : network.value().nodes()) {
        if (std::fabs(node.x) > 1e-4f || std::fabs(node.z) > 1e-4f) continue;
        hubId     = node.id;
        hubRadius = node.junctionRadius;
    }
    REQUIRE_NE(hubId, std::uint32_t{0});

    RoadBakeOptions options;
    options.includeNavigation = false;
    options.sideObjectSpacing = 2.f;
    auto baked                = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());

    int sidePoints = 0;
    for (int row = 0; row < baked.value().placements.getCount(); ++row) {
        const auto role = baked.value().placements.getStringAttribute(row, "road_role", "");
        if (role != "side.left" && role != "side.right") continue;
        const auto edgeId = static_cast<std::uint32_t>(
            baked.value().placements.getIntAttribute(row, "road_edge_id", 0));
        const auto edge = network.value().edgeResult(edgeId);
        REQUIRE(edge.ok());
        const float distance = baked.value().placements.getFloatAttribute(row, "road_distance", -1.f);
        if (edge.value().from == hubId) CHECK_GE(distance, hubRadius);
        if (edge.value().to == hubId) CHECK_LE(distance, 20.f - hubRadius);
        ++sidePoints;
    }
    CHECK_GT(sidePoints, 0);
}

TEST_CASE("procgen.road.placements.avoidAdjacentRoadsAndEachOther") {
    RoadNetwork network;
    auto a0 = network.addNode(-20.f, 0.f, -7.f, 2.f);
    auto a1 = network.addNode(20.f, 0.f, -7.f, 2.f);
    auto b0 = network.addNode(-20.f, 0.f, 7.f, 2.f);
    auto b1 = network.addNode(20.f, 0.f, 7.f, 2.f);
    REQUIRE(a0.ok());
    REQUIRE(a1.ok());
    REQUIRE(b0.ok());
    REQUIRE(b1.ok());
    REQUIRE(network.addEdge(a0.value(), a1.value(), {{}, {}}, 1, 1).ok());
    REQUIRE(network.addEdge(b0.value(), b1.value(), {{}, {}}, 1, 1).ok());

    RoadBakeOptions options;
    options.includeNavigation   = false;
    options.includeJunctions    = false;
    options.sideObjectSpacing   = 8.f;
    options.sideObjectOffset    = 0.5f;
    options.sideObjectClearance = 2.f;
    auto baked = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    int sideAnchors = 0;
    for (int row = 0; row < baked.value().placements.getCount(); ++row) {
        const auto role = baked.value().placements.getStringAttribute(row, "road_role", "");
        if (role != "side.left" && role != "side.right") continue;
        ++sideAnchors;
        CHECK_EQ(baked.value().placements.getFloatAttribute(row, "road_clearance", -1.f), 2.f);
        for (const auto& edge : network.edges()) {
            if (baked.value().placements.getIntAttribute(row, "road_edge_id", 0) == edge.id) continue;
            SplinePath path;
            REQUIRE(path.setKindResult("catmullRom").ok());
            for (const auto& point : edge.controlPoints)
                REQUIRE(path.addPointResult(SplinePoint{point.x, point.y, point.z}).ok());
            auto closest = path.closestPointResult(baked.value().placements.getX(row),
                                                   baked.value().placements.getY(row),
                                                   baked.value().placements.getZ(row), 32);
            REQUIRE(closest.ok());
            const float dx = baked.value().placements.getX(row) - closest.value().x;
            const float dy = baked.value().placements.getY(row) - closest.value().y;
            const float dz = baked.value().placements.getZ(row) - closest.value().z;
            CHECK_GE(std::sqrt(dx * dx + dy * dy + dz * dz), 7.45f);
        }
    }
    CHECK_EQ(sideAnchors, 10);
}

TEST_CASE("procgen.road.terrainConformFlattensGroundAndRejectsBridgeDeck") {
    auto ground = RoadNetwork::makeStraight(24.f, 1);
    REQUIRE(ground.ok());
    RoadBakeOptions bakeOptions;
    bakeOptions.includeNavigation = false;
    bakeOptions.includePlacements = false;
    auto groundBake = bakeRoadNetwork(ground.value(), bakeOptions);
    REQUIRE(groundBake.ok());

    Heightmap terrain(33, 33);
    for (float& value : terrain.data()) value = 0.1f;
    RoadTerrainConformOptions conform;
    conform.originX          = -16.f;
    conform.originZ          = -16.f;
    conform.cellSize         = 1.f;
    conform.heightScale      = 10.f;
    conform.blendDistance    = 2.f;
    conform.maxVerticalDelta = 2.f;
    auto changed = conformHeightmapToRoad(terrain, groundBake.value().mesh, conform);
    REQUIRE(changed.ok());
    CHECK_GT(changed.value(), 0);
    CHECK_LT(terrain.height(16, 16), 0.01f);
    CHECK_EQ(terrain.height(0, 0), 0.1f);
    CHECK_GT(terrain.height(16, 20), 0.09f);

    Heightmap restorable(33, 33);
    for (float& value : restorable.data()) value = 0.1f;
    const auto baseline = restorable.data();
    auto receipt = conformHeightmapToRoadWithReceipt(restorable, groundBake.value().mesh, conform);
    REQUIRE(receipt.ok());
    REQUIRE(!receipt.value().sampleIndices.empty());
    CHECK_EQ(receipt.value().sampleIndices.size(), receipt.value().before.size());
    CHECK_EQ(receipt.value().sampleIndices.size(), receipt.value().after.size());
    auto restored = restoreHeightmapFromRoad(restorable, receipt.value());
    REQUIRE(restored.ok());
    CHECK_EQ(static_cast<std::size_t>(restored.value()), receipt.value().sampleIndices.size());
    CHECK_EQ(restorable.data(), baseline);

    auto staleReceipt = conformHeightmapToRoadWithReceipt(restorable, groundBake.value().mesh, conform);
    REQUIRE(staleReceipt.ok());
    REQUIRE(!staleReceipt.value().sampleIndices.empty());
    const auto changedIndex = staleReceipt.value().sampleIndices.front();
    const int  changedX     = static_cast<int>(changedIndex % 33u);
    const int  changedZ     = static_cast<int>(changedIndex / 33u);
    restorable.setHeight(changedX, changedZ, restorable.height(changedX, changedZ) + 0.01f);
    const auto newerTerrain = restorable.data();
    CHECK(!restoreHeightmapFromRoad(restorable, staleReceipt.value()).ok());
    CHECK_EQ(restorable.data(), newerTerrain);

    auto bridge = RoadNetwork::makeBridge(24.f, 6.f, 1);
    REQUIRE(bridge.ok());
    auto bridgeBake = bakeRoadNetwork(bridge.value(), bakeOptions);
    REQUIRE(bridgeBake.ok());
    Heightmap belowBridge(33, 33);
    for (float& value : belowBridge.data()) value = 0.f;
    auto bridgeReceipt = conformHeightmapToRoadWithReceipt(belowBridge, bridgeBake.value().mesh, conform);
    REQUIRE(bridgeReceipt.ok());
    CHECK(bridgeReceipt.value().sampleIndices.empty());
    CHECK_GT(bridgeReceipt.value().skippedBridgeSamples, 0);
    CHECK_EQ(bridgeReceipt.value().skippedTunnelSamples, 0);
    CHECK_EQ(belowBridge.height(16, 16), 0.f);

    RoadNetwork tunnel;
    const auto  tunnelStart = tunnel.addNode(-12.f, -5.f, 0.f);
    const auto  tunnelEnd   = tunnel.addNode(12.f, -5.f, 0.f);
    REQUIRE(tunnelStart.ok());
    REQUIRE(tunnelEnd.ok());
    REQUIRE(tunnel.addEdge(tunnelStart.value(), tunnelEnd.value(), {{}, {}}, 1, 1).ok());
    auto tunnelBake = bakeRoadNetwork(tunnel, bakeOptions);
    REQUIRE(tunnelBake.ok());
    Heightmap aboveTunnel(33, 33);
    for (float& value : aboveTunnel.data()) value = 0.f;
    auto tunnelReceipt = conformHeightmapToRoadWithReceipt(aboveTunnel, tunnelBake.value().mesh, conform);
    REQUIRE(tunnelReceipt.ok());
    CHECK(tunnelReceipt.value().sampleIndices.empty());
    CHECK_EQ(tunnelReceipt.value().skippedBridgeSamples, 0);
    CHECK_GT(tunnelReceipt.value().skippedTunnelSamples, 0);
    CHECK_EQ(aboveTunnel.height(16, 16), 0.f);

    Heightmap budgeted = terrain;
    conform.maximumWork = 1u;
    auto overBudget = conformHeightmapToRoad(budgeted, groundBake.value().mesh, conform);
    CHECK(!overBudget.ok());
    CHECK_EQ(budgeted.data(), terrain.data());
}

TEST_CASE("procgen.road.network.seedWobbleStaysBounded") {
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 20;
    options.turnSamples         = 8;
    options.includeNavigation   = false;
    for (std::uint32_t seed = 0; seed < 7; ++seed) {
        auto network = RoadNetwork::makeInterchange(48.f, 8.f, 2, seed);
        REQUIRE(network.ok());
        auto baked = bakeRoadNetwork(network.value(), options);
        REQUIRE(baked.ok());
        CHECK_LT(baked.value().mesh.getVertexCount(), 100000);
        for (float coordinate : baked.value().mesh.positions()) {
            CHECK(std::isfinite(coordinate));
            CHECK_LT(std::fabs(coordinate), 200.f);
        }
    }
}

TEST_CASE("procgen.road.scenes.interchangeGroundCrossAndPiers") {
    auto network = RoadNetwork::makeInterchange(48.f, 8.f, 2, 1);
    REQUIRE(network.ok());
    CHECK_EQ(network.value().nodeCount(), 8);
    CHECK_EQ(network.value().edgeCount(), 10);

    int groundCrossing = 0, elevatedCrossing = 0;
    for (const auto& n : network.value().nodes()) {
        if (std::fabs(n.x) > 1e-3f || std::fabs(n.z) > 1e-3f) continue;
        if (std::fabs(n.y) < 1e-3f) ++groundCrossing;
        if (std::fabs(n.y - 8.f) < 1e-3f) ++elevatedCrossing;
    }
    CHECK_EQ(groundCrossing, 1);
    CHECK_EQ(elevatedCrossing, 1);
    CHECK_GT(network.value().laneLinkCount(), 0);
    CHECK(std::any_of(network.value().laneLinks().begin(), network.value().laneLinks().end(),
                      [](const RoadLaneConnection& link) { return link.inEdge > 6u || link.outEdge > 6u; }));

    float hubJr = 0.f;
    for (const auto& n : network.value().nodes()) {
        if (std::fabs(n.x) < 1e-3f && std::fabs(n.z) < 1e-3f && std::fabs(n.y) < 1e-3f) hubJr = n.junctionRadius;
    }
    // Same fillet sizing as makeCross: asphaltHalf + cornerR.
    CHECK_GT(hubJr, 5.5f);
    CHECK_LT(hubJr, 7.5f);

    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 28;
    options.turnSamples         = 8;
    options.includeJunctions    = true;
    options.includeNavigation   = false;
    options.includePiers        = true;
    options.includeMarkings     = true;
    auto baked                  = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    CHECK_GT(baked.value().mesh.getVertexCount(), 2000);

    bool sawPier = false, sawSidewalk = false, sawDeck = false;
    for (int i = 0; i < baked.value().mesh.getGroupCount(); ++i) {
        const auto name = baked.value().mesh.getGroupName(i);
        if (name == "pier") sawPier = true;
        if (name == "sidewalk") sawSidewalk = true;
        if (name == "deck") sawDeck = true;
    }
    CHECK(sawPier);
    CHECK(sawSidewalk);
    CHECK(sawDeck);

    // Ground-hub sidewalks should sit on outward-center curb returns (near jr,jr disk).
    int sidewalkGroup = -1;
    for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g) {
        if (baked.value().mesh.getGroupName(g) == "sidewalk") sidewalkGroup = g;
    }
    REQUIRE(sidewalkGroup >= 0);
    auto sw = baked.value().mesh.copyGroup(sidewalkGroup);
    REQUIRE(sw);
    int arcHits = 0;
    for (int i = 0; i < sw->getVertexCount(); ++i) {
        if (std::fabs(sw->getPositionY(i)) > 0.5f) continue;  // ground level only
        const float x  = std::fabs(sw->getPositionX(i));
        const float z  = std::fabs(sw->getPositionZ(i));
        const float dx = hubJr - x;
        const float dz = hubJr - z;
        if (dx < 0.05f || dz < 0.05f) continue;
        const float r = std::sqrt(dx * dx + dz * dz);
        if (r > 0.9f && r < 2.9f) ++arcHits;
    }
    CHECK_GT(arcHits, 16);
}

TEST_CASE("procgen.road.scenes.fourSimpleBakeClean") {
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 20;
    options.turnSamples         = 6;
    options.includeNavigation   = false;

    auto straight = RoadNetwork::makeStraight(28.f, 2);
    REQUIRE(straight.ok());
    CHECK_EQ(straight.value().edgeCount(), 1);
    options.includeJunctions = false;
    auto bakedStraight = bakeRoadNetwork(straight.value(), options);
    REQUIRE(bakedStraight.ok());
    CHECK_GT(bakedStraight.value().mesh.getVertexCount(), 50);

    auto curve = RoadNetwork::makeCurve(16.f, 2);
    REQUIRE(curve.ok());
    CHECK_EQ(curve.value().edgeCount(), 1);
    auto bakedCurve = bakeRoadNetwork(curve.value(), options);
    REQUIRE(bakedCurve.ok());
    CHECK_GT(bakedCurve.value().mesh.getVertexCount(), 50);

    auto bridge = RoadNetwork::makeBridge(30.f, 5.f, 2);
    REQUIRE(bridge.ok());
    options.includePiers = true;
    auto bakedBridge = bakeRoadNetwork(bridge.value(), options);
    REQUIRE(bakedBridge.ok());
    bool sawPier = false;
    for (int i = 0; i < bakedBridge.value().mesh.getGroupCount(); ++i) {
        if (bakedBridge.value().mesh.getGroupName(i) == "pier") sawPier = true;
    }
    CHECK(sawPier);

    auto cross = RoadNetwork::makeCross(28.f, 2);
    REQUIRE(cross.ok());
    CHECK_EQ(cross.value().edgeCount(), 4);
    CHECK_GT(cross.value().laneLinkCount(), 0);
    options.includeJunctions  = true;
    options.includeNavigation = false;
    auto bakedCross = bakeRoadNetwork(cross.value(), options);
    REQUIRE(bakedCross.ok());
    CHECK_GT(bakedCross.value().mesh.getVertexCount(), 100);

    MeshRecipeRegistry::instance().registerBuiltins();
    Params params;
    params.setString("scene", "straight");
    params.setFloat("span", 24.f);
    params.setInt("lanes", 2);
    params.setInt("pathSegments", 16);
    params.setBool("navigation", false);
    params.setBool("junctions", false);
    MeshBuild mesh;
    std::string error;
    REQUIRE(MeshRecipeRegistry::instance().generate("mesh.roadNetwork", params, mesh, error));
    CHECK_GT(mesh.getVertexCount(), 40);
    auto tightTurn = RoadNetwork::makeScene("tight-turn", 20.f, 4.f, 2, 1);
    REQUIRE(tightTurn.ok());
    CHECK_EQ(tightTurn.value().edgeCount(), 2);
    CHECK_EQ(tightTurn.value().laneLinkCount(), 1);
    CHECK(!RoadNetwork::makeScene("nope", 20.f, 4.f, 2, 1).ok());
}


TEST_CASE("procgen.road.scenes.crossJunctionHasCenterAsphalt") {
    auto cross = RoadNetwork::makeCross(28.f, 2);
    REQUIRE(cross.ok());
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 20;
    options.includeJunctions = true;
    options.includeNavigation = false;
    options.includePiers = false;
    auto baked = bakeRoadNetwork(cross.value(), options);
    REQUIRE(baked.ok());
    const auto& m = baked.value().mesh;
    int asphaltGroup = -1;
    for (int g = 0; g < m.getGroupCount(); ++g) {
        if (m.getGroupName(g) == "asphalt") asphaltGroup = g;
    }
    CHECK_GE(asphaltGroup, 0);
    auto asphalt = m.copyGroup(asphaltGroup);
    REQUIRE(asphalt);
    int near = 0;
    float minX=1e9,maxX=-1e9,minZ=1e9,maxZ=-1e9;
    for (int i = 0; i < asphalt->getVertexCount(); ++i) {
        const float x = asphalt->getPositionX(i);
        const float z = asphalt->getPositionZ(i);
        if (std::fabs(x) < 3.f && std::fabs(z) < 3.f) {
            ++near;
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minZ = std::min(minZ, z);
            maxZ = std::max(maxZ, z);
        }
    }
    // Does any asphalt triangle cover the origin in XZ?
    int cover = 0;
    for (int t = 0; t < asphalt->getIndexCount() / 3; ++t) {
        const int i0 = asphalt->getIndex(t * 3 + 0);
        const int i1 = asphalt->getIndex(t * 3 + 1);
        const int i2 = asphalt->getIndex(t * 3 + 2);
        const float x0 = asphalt->getPositionX(i0), z0 = asphalt->getPositionZ(i0);
        const float x1 = asphalt->getPositionX(i1), z1 = asphalt->getPositionZ(i1);
        const float x2 = asphalt->getPositionX(i2), z2 = asphalt->getPositionZ(i2);
        // barycentric in XZ for (0,0)
        const float den = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2);
        if (std::fabs(den) < 1e-8f) continue;
        const float a = ((z1 - z2) * (0.f - x2) + (x2 - x1) * (0.f - z2)) / den;
        const float b = ((z2 - z0) * (0.f - x2) + (x0 - x2) * (0.f - z2)) / den;
        const float c = 1.f - a - b;
        if (a >= -1e-4f && b >= -1e-4f && c >= -1e-4f) {
            ++cover;
        }
    }
    CHECK_GT(cover, 0);
}

TEST_CASE("procgen.road.junction.tShapeUsesArmHullNotOversizedDisc") {
    RoadNetwork network;
    auto        hub   = network.addNode(0.f, 0.f, 0.f, 6.f);
    auto        east  = network.addNode(18.f, 0.f, 0.f, 2.f);
    auto        west  = network.addNode(-18.f, 0.f, 0.f, 2.f);
    auto        north = network.addNode(0.f, 0.f, -18.f, 2.f);
    REQUIRE(hub.ok());
    REQUIRE(east.ok());
    REQUIRE(west.ok());
    REQUIRE(north.ok());
    REQUIRE(network.addEdge(hub.value(), east.value(), {{0.f, 0.f, 0.f}, {18.f, 0.f, 0.f}}, 1, 0).ok());
    REQUIRE(network.addEdge(hub.value(), west.value(), {{0.f, 0.f, 0.f}, {-18.f, 0.f, 0.f}}, 1, 0).ok());
    REQUIRE(network.addEdge(hub.value(), north.value(), {{0.f, 0.f, 0.f}, {0.f, 0.f, -18.f}}, 1, 0).ok());

    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includePiers      = false;
    auto baked                = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    int asphaltGroup = -1;
    for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g)
        if (baked.value().mesh.getGroupName(g) == "asphalt") asphaltGroup = g;
    REQUIRE(asphaltGroup >= 0);
    auto asphalt = baked.value().mesh.copyGroup(asphaltGroup);
    REQUIRE(asphalt);

    float junctionMaxZ     = -1e9f;
    int   junctionVertices = 0;
    for (int i = 0; i < asphalt->getVertexCount(); ++i) {
        const float x = asphalt->getPositionX(i);
        const float z = asphalt->getPositionZ(i);
        if (std::fabs(x) <= 6.1f && z >= -6.1f && z <= 7.f) {
            junctionMaxZ = std::max(junctionMaxZ, z);
            ++junctionVertices;
        }
    }
    CHECK_GT(junctionVertices, 3);
    CHECK_LT(junctionMaxZ, 2.f);

    options.includeJunctions = false;
    auto edgeOnly = bakeRoadNetwork(network, options);
    REQUIRE(edgeOnly.ok());
    auto groupVertexCount = [](const MeshBuild& mesh, const char* name) {
        for (int group = 0; group < mesh.getGroupCount(); ++group) {
            if (mesh.getGroupName(group) != name) continue;
            auto part = mesh.copyGroup(group);
            return part ? part->getVertexCount() : 0;
        }
        return 0;
    };
    CHECK_GT(groupVertexCount(baked.value().mesh, "curb"), groupVertexCount(edgeOnly.value().mesh, "curb"));
    CHECK_GT(groupVertexCount(baked.value().mesh, "sidewalk"), groupVertexCount(edgeOnly.value().mesh, "sidewalk"));
}

TEST_CASE("procgen.road.scenes.threeArmJunctionsHaveCoveredCentersAndProfiles") {
    for (const std::string scene : {"t-junction", "y-junction"}) {
        auto network = RoadNetwork::makeScene(scene, 36.f, 6.f, 2, 1);
        REQUIRE(network.ok());
        REQUIRE_EQ(network.value().edgeCount(), std::size_t{3});
        CHECK_GT(network.value().laneLinkCount(), 0);

        RoadBakeOptions options;
        options.pathSegmentsPerEdge = 32;
        options.turnSamples         = 12;
        options.includePiers        = false;
        auto baked                  = bakeRoadNetwork(network.value(), options);
        REQUIRE(baked.ok());
        auto asphalt  = copyRoadGroup(baked.value().mesh, "asphalt");
        auto curb     = copyRoadGroup(baked.value().mesh, "curb");
        auto sidewalk = copyRoadGroup(baked.value().mesh, "sidewalk");
        REQUIRE(asphalt);
        REQUIRE(curb);
        REQUIRE(sidewalk);
        CHECK(roadTriangleCoversXZ(*asphalt, 0.f, 0.f));
        CHECK_GT(curb->getVertexCount(), 0);
        CHECK_GT(sidewalk->getVertexCount(), 0);
        checkNoDegenerateRoadTriangles(baked.value().mesh, "asphalt");
        checkThreeArmMouthConstraints(network.value(), baked.value());
        for (const auto& turn : baked.value().overlay.turns) {
            REQUIRE_GE(turn.xyz.size(), std::size_t{6});
            for (const float coordinate : turn.xyz) CHECK(std::isfinite(coordinate));
        }
    }
}

TEST_CASE("procgen.road.scenes.slopedAndCurvedUphillJunctionsStayContinuous") {
    for (const std::string scene : {"sloped-t", "curve-uphill"}) {
        auto network = RoadNetwork::makeScene(scene, 36.f, 6.f, 2, 1);
        REQUIRE(network.ok());
        REQUIRE_EQ(network.value().edgeCount(), std::size_t{3});
        CHECK_GT(network.value().laneLinkCount(), 0);

        RoadBakeOptions options;
        options.pathSegmentsPerEdge = 48;
        options.turnSamples         = 16;
        options.includePiers        = false;
        auto baked                  = bakeRoadNetwork(network.value(), options);
        REQUIRE(baked.ok());
        auto asphalt = copyRoadGroup(baked.value().mesh, "asphalt");
        REQUIRE(asphalt);
        CHECK(roadTriangleCoversXZ(*asphalt, 0.f, 0.f));
        checkNoDegenerateRoadTriangles(baked.value().mesh, "asphalt");
        float minimumY = std::numeric_limits<float>::max();
        float maximumY = std::numeric_limits<float>::lowest();
        for (int vertex = 0; vertex < asphalt->getVertexCount(); ++vertex) {
            minimumY = std::min(minimumY, asphalt->getPositionY(vertex));
            maximumY = std::max(maximumY, asphalt->getPositionY(vertex));
        }
        CHECK_LT(minimumY, 0.2f);
        CHECK_GT(maximumY, 6.8f);
        checkThreeArmMouthConstraints(network.value(), baked.value());
        float sumX = 0.f, sumY = 0.f, sumZ = 0.f;
        int   pointCount = 0;
        for (const auto& turn : baked.value().overlay.turns) {
            REQUIRE_GE(turn.xyz.size(), std::size_t{6});
            for (std::size_t coordinate = 0; coordinate < turn.xyz.size(); coordinate += 3) {
                const float x = turn.xyz[coordinate];
                const float y = turn.xyz[coordinate + 1];
                const float z = turn.xyz[coordinate + 2];
                CHECK(std::isfinite(x));
                CHECK(std::isfinite(y));
                CHECK(std::isfinite(z));
                sumX += x;
                sumY += y;
                sumZ += z;
                ++pointCount;
            }
        }
        REQUIRE_GT(pointCount, 2);
        const float meanX = sumX / static_cast<float>(pointCount);
        const float meanY = sumY / static_cast<float>(pointCount);
        const float meanZ = sumZ / static_cast<float>(pointCount);
        float       xx = 0.f, xz = 0.f, zz = 0.f, xy = 0.f, zy = 0.f;
        for (const auto& turn : baked.value().overlay.turns)
            for (std::size_t coordinate = 0; coordinate < turn.xyz.size(); coordinate += 3) {
                const float x = turn.xyz[coordinate] - meanX;
                const float y = turn.xyz[coordinate + 1] - meanY;
                const float z = turn.xyz[coordinate + 2] - meanZ;
                xx += x * x;
                xz += x * z;
                zz += z * z;
                xy += x * y;
                zy += z * y;
            }
        const float determinant = xx * zz - xz * xz;
        REQUIRE_GT(std::fabs(determinant), 1e-4f);
        const float gradeX    = (xy * zz - zy * xz) / determinant;
        const float gradeZ    = (zy * xx - xy * xz) / determinant;
        const float intercept = meanY - gradeX * meanX - gradeZ * meanZ;
        for (const auto& turn : baked.value().overlay.turns)
            for (std::size_t coordinate = 0; coordinate < turn.xyz.size(); coordinate += 3)
                CHECK_LT(std::fabs(turn.xyz[coordinate + 1] -
                                   (intercept + gradeX * turn.xyz[coordinate] + gradeZ * turn.xyz[coordinate + 2])),
                         0.002f);
    }
}

TEST_CASE("procgen.road.junction.preflightRejectsUnsolvableGeometry") {
    {
        RoadNetwork network;
        auto hub   = network.addNode(0.f, 0.f, 0.f, 4.f);
        auto eastA = network.addNode(20.f, 0.f, 0.f, 2.f);
        auto eastB = network.addNode(20.f, 0.f, 0.05f, 2.f);
        auto west  = network.addNode(-20.f, 0.f, 0.f, 2.f);
        REQUIRE(hub.ok());
        REQUIRE(eastA.ok());
        REQUIRE(eastB.ok());
        REQUIRE(west.ok());
        REQUIRE(network.addEdge(hub.value(), eastA.value(), {{}, {}}, 1, 1).ok());
        REQUIRE(network.addEdge(hub.value(), eastB.value(), {{}, {}}, 1, 1).ok());
        REQUIRE(network.addEdge(west.value(), hub.value(), {{}, {}}, 1, 1).ok());
        auto baked = bakeRoadNetwork(network);
        CHECK(!baked.ok());
    }
    {
        RoadNetwork network;
        auto hub   = network.addNode(0.f, 0.f, 0.f, 5.f);
        auto east  = network.addNode(20.f, 0.f, 0.f, 2.f);
        auto west  = network.addNode(-20.f, 0.f, 0.f, 2.f);
        auto north = network.addNode(0.f, 0.f, -20.f, 2.f);
        auto south = network.addNode(0.f, 16.f, 20.f, 2.f);
        REQUIRE(hub.ok());
        REQUIRE(east.ok());
        REQUIRE(west.ok());
        REQUIRE(north.ok());
        REQUIRE(south.ok());
        REQUIRE(network.addEdge(hub.value(), east.value(), {{}, {}}, 1, 1).ok());
        REQUIRE(network.addEdge(west.value(), hub.value(), {{}, {}}, 1, 1).ok());
        REQUIRE(network.addEdge(north.value(), hub.value(), {{}, {}}, 1, 1).ok());
        REQUIRE(network.addEdge(hub.value(), south.value(), {{}, {}}, 1, 1).ok());
        auto baked = bakeRoadNetwork(network);
        CHECK(!baked.ok());
    }
    {
        RoadNetwork network;
        auto a     = network.addNode(-4.f, 0.f, 0.f, 4.f);
        auto b     = network.addNode(4.f, 0.f, 0.f, 4.f);
        auto aSide = network.addNode(-4.f, 0.f, -12.f, 2.f);
        auto bSide = network.addNode(4.f, 0.f, 12.f, 2.f);
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        REQUIRE(aSide.ok());
        REQUIRE(bSide.ok());
        RoadStyle wide;
        wide.laneWidth = 4.2f;
        REQUIRE(network.addEdge(a.value(), b.value(), {{}, {}}, 4, 4, wide).ok());
        REQUIRE(network.addEdge(aSide.value(), a.value(), {{}, {}}, 1, 1).ok());
        REQUIRE(network.addEdge(b.value(), bSide.value(), {{}, {}}, 1, 1).ok());
        auto baked = bakeRoadNetwork(network);
        CHECK(!baked.ok());
    }
}

TEST_CASE("procgen.road.junction.slopedAxisCrossUsesFittedPlane") {
    RoadNetwork network;
    auto        hub   = network.addNode(0.f, 0.f, 0.f, 6.f);
    auto        east  = network.addNode(20.f, 2.f, 0.f, 2.f);
    auto        west  = network.addNode(-20.f, -2.f, 0.f, 2.f);
    auto        north = network.addNode(0.f, -1.f, -20.f, 2.f);
    auto        south = network.addNode(0.f, 1.f, 20.f, 2.f);
    REQUIRE(hub.ok());
    REQUIRE(east.ok());
    REQUIRE(west.ok());
    REQUIRE(north.ok());
    REQUIRE(south.ok());
    REQUIRE(network.addEdge(hub.value(), east.value(), {{}, {}}, 1, 1).ok());
    REQUIRE(network.addEdge(west.value(), hub.value(), {{}, {}}, 1, 1).ok());
    REQUIRE(network.addEdge(north.value(), hub.value(), {{}, {}}, 1, 1).ok());
    REQUIRE(network.addEdge(hub.value(), south.value(), {{}, {}}, 1, 1).ok());

    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 32;
    options.includeNavigation   = false;
    options.includePiers        = false;
    auto baked                  = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    auto asphalt = copyRoadGroup(baked.value().mesh, "asphalt");
    REQUIRE(asphalt);

    float minimumJunctionY = std::numeric_limits<float>::max();
    float maximumJunctionY = std::numeric_limits<float>::lowest();
    int   junctionVertices = 0;
    for (int vertex = 0; vertex < asphalt->getVertexCount(); ++vertex) {
        const float x = asphalt->getPositionX(vertex);
        const float z = asphalt->getPositionZ(vertex);
        if (std::fabs(x) > 5.f || std::fabs(z) > 5.f) continue;
        const float y = asphalt->getPositionY(vertex);
        minimumJunctionY = std::min(minimumJunctionY, y);
        maximumJunctionY = std::max(maximumJunctionY, y);
        ++junctionVertices;
    }
    REQUIRE_GT(junctionVertices, 8);
    CHECK_GT(maximumJunctionY - minimumJunctionY, 0.5f);
    CHECK_LT(maximumJunctionY - minimumJunctionY, 2.f);
    checkNoDegenerateRoadTriangles(baked.value().mesh, "asphalt");

    auto markings = copyRoadGroup(baked.value().mesh, "marking");
    REQUIRE(markings);
    for (int vertex = 0; vertex < markings->getVertexCount(); ++vertex) {
        CHECK_GT(markings->getNormalY(vertex), 0.95f);
        CHECK(std::isfinite(markings->getNormalX(vertex)));
        CHECK(std::isfinite(markings->getNormalZ(vertex)));
    }
}

TEST_CASE("procgen.road.junction.slopedArmMouthSharesEdgeElevation") {
    RoadNetwork network;
    auto        hub   = network.addNode(0.f, 4.f, 0.f, 6.f);
    auto        east  = network.addNode(18.f, 0.f, 0.f, 2.f);
    auto        west  = network.addNode(-18.f, 2.f, 0.f, 2.f);
    auto        north = network.addNode(0.f, 6.f, -18.f, 2.f);
    REQUIRE(hub.ok());
    REQUIRE(east.ok());
    REQUIRE(west.ok());
    REQUIRE(north.ok());
    REQUIRE(network.addEdge(hub.value(), east.value(), {{}, {}}, 1, 0).ok());
    REQUIRE(network.addEdge(hub.value(), west.value(), {{}, {}}, 1, 0).ok());
    REQUIRE(network.addEdge(hub.value(), north.value(), {{}, {}}, 1, 0).ok());

    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includePiers      = false;
    auto baked                = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    int asphaltGroup = -1;
    for (int group = 0; group < baked.value().mesh.getGroupCount(); ++group)
        if (baked.value().mesh.getGroupName(group) == "asphalt") asphaltGroup = group;
    REQUIRE(asphaltGroup >= 0);
    auto asphalt = baked.value().mesh.copyGroup(asphaltGroup);
    REQUIRE(asphalt);

    SplinePath eastPath;
    REQUIRE(eastPath.setKindResult("catmullRom").ok());
    REQUIRE(eastPath.addPointResult(SplinePoint{0.f, 4.f, 0.f}).ok());
    REQUIRE(eastPath.addPointResult(SplinePoint{18.f, 0.f, 0.f}).ok());
    auto mouthFrame = eastPath.travelFrameResult(6.f, "clamp", 16);
    REQUIRE(mouthFrame.ok());
    const float mouthX = mouthFrame.value().sample.x + mouthFrame.value().sideX * 1.75f;
    const float mouthZ = mouthFrame.value().sample.z + mouthFrame.value().sideZ * 1.75f;
    float       minimumY   = 1e9f;
    float       maximumY   = -1e9f;
    int         matches    = 0;
    for (int vertex = 0; vertex < asphalt->getVertexCount(); ++vertex) {
        if (std::fabs(asphalt->getPositionX(vertex) - mouthX) > 0.03f ||
            std::fabs(asphalt->getPositionZ(vertex) - mouthZ) > 0.03f)
            continue;
        minimumY = std::min(minimumY, asphalt->getPositionY(vertex));
        maximumY = std::max(maximumY, asphalt->getPositionY(vertex));
        ++matches;
    }
    REQUIRE_GE(matches, 2);
    CHECK_LT(maximumY - minimumY, 0.002f);
}

TEST_CASE("procgen.road.junction.mixedStylesKeepEachMouthProfile") {
    RoadNetwork network;
    auto        hub   = network.addNode(0.f, 0.f, 0.f, 6.f);
    auto        east  = network.addNode(18.f, 0.f, 0.f, 2.f);
    auto        west  = network.addNode(-18.f, 0.f, 0.f, 2.f);
    auto        north = network.addNode(0.f, 0.f, -18.f, 2.f);
    REQUIRE(hub.ok());
    REQUIRE(east.ok());
    REQUIRE(west.ok());
    REQUIRE(north.ok());
    RoadStyle narrow;
    narrow.curbWidth     = 0.15f;
    narrow.curbHeight    = 0.25f;
    narrow.sidewalkWidth = 0.45f;
    narrow.sidewalkHeight = 0.1f;
    RoadStyle wide;
    wide.curbWidth      = 0.8f;
    wide.curbHeight     = 0.65f;
    wide.sidewalkWidth  = 2.2f;
    wide.sidewalkHeight = 0.3f;
    REQUIRE(network.addEdge(hub.value(), east.value(), {{}, {}}, 1, 0, narrow).ok());
    REQUIRE(network.addEdge(hub.value(), west.value(), {{}, {}}, 1, 0, wide).ok());
    REQUIRE(network.addEdge(hub.value(), north.value(), {{}, {}}, 1, 0, wide).ok());

    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includePiers      = false;
    auto withJunction         = bakeRoadNetwork(network, options);
    REQUIRE(withJunction.ok());
    options.includeJunctions = false;
    auto edgeOnly            = bakeRoadNetwork(network, options);
    REQUIRE(edgeOnly.ok());

    SplinePath eastPath;
    REQUIRE(eastPath.setKindResult("catmullRom").ok());
    REQUIRE(eastPath.addPointResult(SplinePoint{0.f, 0.f, 0.f}).ok());
    REQUIRE(eastPath.addPointResult(SplinePoint{18.f, 0.f, 0.f}).ok());
    auto mouthFrame = eastPath.travelFrameResult(6.f, "clamp", 16);
    REQUIRE(mouthFrame.ok());
    auto countNear = [](const MeshBuild& mesh, const char* groupName, float x, float y, float z) {
        for (int group = 0; group < mesh.getGroupCount(); ++group) {
            if (mesh.getGroupName(group) != groupName) continue;
            auto part = mesh.copyGroup(group);
            if (!part) return 0;
            int matches = 0;
            for (int vertex = 0; vertex < part->getVertexCount(); ++vertex) {
                if (std::fabs(part->getPositionX(vertex) - x) < 1e-3f &&
                    std::fabs(part->getPositionY(vertex) - y) < 1e-3f &&
                    std::fabs(part->getPositionZ(vertex) - z) < 1e-3f)
                    ++matches;
            }
            return matches;
        }
        return 0;
    };
    const auto& frame = mouthFrame.value();
    const float curbSide = 1.75f + narrow.curbWidth;
    const float walkSide = curbSide + narrow.sidewalkWidth;
    const float curbX = frame.sample.x + frame.sideX * curbSide;
    const float curbZ = frame.sample.z + frame.sideZ * curbSide;
    const float walkX = frame.sample.x + frame.sideX * walkSide;
    const float walkZ = frame.sample.z + frame.sideZ * walkSide;
    CHECK_GT(countNear(withJunction.value().mesh, "curb", curbX, narrow.curbHeight, curbZ),
             countNear(edgeOnly.value().mesh, "curb", curbX, narrow.curbHeight, curbZ));
    CHECK_GT(countNear(withJunction.value().mesh, "sidewalk", walkX, narrow.sidewalkHeight, walkZ),
             countNear(edgeOnly.value().mesh, "sidewalk", walkX, narrow.sidewalkHeight, walkZ));
}

TEST_CASE("procgen.road.bake.onlyTrimsActiveJunctions") {
    auto straight = RoadNetwork::makeStraight(20.f, 1);
    REQUIRE(straight.ok());
    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includePiers      = false;
    options.includeMarkings   = false;
    auto terminalBake         = bakeRoadNetwork(straight.value(), options);
    REQUIRE(terminalBake.ok());
    int asphaltGroup = -1;
    for (int group = 0; group < terminalBake.value().mesh.getGroupCount(); ++group)
        if (terminalBake.value().mesh.getGroupName(group) == "asphalt") asphaltGroup = group;
    REQUIRE(asphaltGroup >= 0);
    auto terminalAsphalt = terminalBake.value().mesh.copyGroup(asphaltGroup);
    REQUIRE(terminalAsphalt);
    float minimumX = 1e9f;
    float maximumX = -1e9f;
    for (int vertex = 0; vertex < terminalAsphalt->getVertexCount(); ++vertex) {
        minimumX = std::min(minimumX, terminalAsphalt->getPositionX(vertex));
        maximumX = std::max(maximumX, terminalAsphalt->getPositionX(vertex));
    }
    CHECK_LT(std::fabs(minimumX + 10.f), 1e-3f);
    CHECK_LT(std::fabs(maximumX - 10.f), 1e-3f);

    RoadNetwork connected;
    auto        west = connected.addNode(-10.f, 0.f, 0.f, 2.f);
    auto        hub  = connected.addNode(0.f, 0.f, 0.f, 4.f);
    auto        east = connected.addNode(10.f, 0.f, 0.f, 2.f);
    REQUIRE(west.ok());
    REQUIRE(hub.ok());
    REQUIRE(east.ok());
    REQUIRE(connected.addEdge(west.value(), hub.value(), {{}, {}}, 1, 0).ok());
    REQUIRE(connected.addEdge(hub.value(), east.value(), {{}, {}}, 1, 0).ok());
    options.includeJunctions = false;
    auto untrimmed           = bakeRoadNetwork(connected, options);
    REQUIRE(untrimmed.ok());
    asphaltGroup = -1;
    for (int group = 0; group < untrimmed.value().mesh.getGroupCount(); ++group)
        if (untrimmed.value().mesh.getGroupName(group) == "asphalt") asphaltGroup = group;
    REQUIRE(asphaltGroup >= 0);
    auto untrimmedAsphalt = untrimmed.value().mesh.copyGroup(asphaltGroup);
    REQUIRE(untrimmedAsphalt);
    int hubVertices = 0;
    for (int vertex = 0; vertex < untrimmedAsphalt->getVertexCount(); ++vertex)
        if (std::fabs(untrimmedAsphalt->getPositionX(vertex)) < 1e-3f) ++hubVertices;
    CHECK_GE(hubVertices, 4);
}

TEST_CASE("procgen.road.bake.smoothSplitIsNotAJunctionAndMarkingToggleIsGlobal") {
    auto network = RoadNetwork::makeStraight(24.f, 1);
    REQUIRE(network.ok());
    REQUIRE_EQ(network.value().edgeCount(), 1);
    const auto edgeId = network.value().edges().front().id;
    auto split = network.value().splitEdge(edgeId, 1, 4.f);
    REQUIRE(split.ok());

    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includePiers      = false;
    options.includeMarkings   = false;
    auto baked                = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    int asphaltJunctionVertices = 0;
    int markingVertices         = 0;
    for (int group = 0; group < baked.value().mesh.getGroupCount(); ++group) {
        auto part = baked.value().mesh.copyGroup(group);
        REQUIRE(part);
        if (baked.value().mesh.getGroupName(group) == "asphalt") {
            for (int vertex = 0; vertex < part->getVertexCount(); ++vertex)
                if (std::fabs(part->getPositionY(vertex) - 0.01f) < 1e-4f) ++asphaltJunctionVertices;
        }
        if (baked.value().mesh.getGroupName(group) == "marking") markingVertices += part->getVertexCount();
    }
    CHECK_EQ(asphaltJunctionVertices, 0);
    CHECK_EQ(markingVertices, 0);

    auto cross = RoadNetwork::makeCross(28.f, 2);
    REQUIRE(cross.ok());
    auto unmarkedCross = bakeRoadNetwork(cross.value(), options);
    REQUIRE(unmarkedCross.ok());
    for (int group = 0; group < unmarkedCross.value().mesh.getGroupCount(); ++group) {
        if (unmarkedCross.value().mesh.getGroupName(group) != "marking") continue;
        auto part = unmarkedCross.value().mesh.copyGroup(group);
        REQUIRE(part);
        CHECK_EQ(part->getVertexCount(), 0);
    }
}

TEST_CASE("procgen.road.scenarios.useOneGraphModel") {
    // Unequal-width Y junction. The generic arm hull must accept independent
    // edge profiles instead of assuming a symmetric cross prefab.
    RoadNetwork yJunction;
    auto        yHub   = yJunction.addNode(0.f, 0.f, 0.f, 5.f);
    auto        ySouth = yJunction.addNode(0.f, 0.f, 24.f, 2.f);
    auto        yWest  = yJunction.addNode(-18.f, 0.f, -18.f, 2.f);
    auto        yEast  = yJunction.addNode(18.f, 0.f, -18.f, 2.f);
    REQUIRE(yHub.ok());
    REQUIRE(ySouth.ok());
    REQUIRE(yWest.ok());
    REQUIRE(yEast.ok());
    RoadStyle narrow;
    narrow.laneWidth = 3.f;
    RoadStyle wide;
    wide.laneWidth = 4.2f;
    REQUIRE(yJunction.addEdge(ySouth.value(), yHub.value(), {{}, {}}, 1, 1, wide).ok());
    REQUIRE(yJunction.addEdge(yHub.value(), yWest.value(), {{}, {}}, 1, 1, narrow).ok());
    REQUIRE(yJunction.addEdge(yHub.value(), yEast.value(), {{}, {}}, 2, 1, wide).ok());
    REQUIRE(yJunction.connectAllTurns(yHub.value()).ok());
    checkRoadScenario(yJunction);

    // A roundabout remains ordinary graph data; the factory only authors that
    // graph consistently and introduces no dedicated runtime type.
    auto roundabout = RoadNetwork::makeRoundabout(48.f, 1);
    REQUIRE(roundabout.ok());
    REQUIRE_EQ(roundabout.value().nodeCount(), 12);
    REQUIRE_EQ(roundabout.value().edgeCount(), 12);
    checkRoadScenario(roundabout.value());
    RoadBakeOptions roundaboutOptions;
    roundaboutOptions.includeNavigation = false;
    roundaboutOptions.includeJunctions  = false;
    auto roundaboutBake = bakeRoadNetwork(roundabout.value(), roundaboutOptions);
    REQUIRE(roundaboutBake.ok());
    auto roundaboutAsphalt = copyRoadGroup(roundaboutBake.value().mesh, "asphalt");
    REQUIRE(roundaboutAsphalt);
    for (float z : {9.f, 10.f, 11.f, 12.f, 13.f})
        for (float x : {-2.f, -1.f, 0.f, 1.f, 2.f}) CHECK(roadTriangleCoversXZ(*roundaboutAsphalt, x, z));
    const auto centers = filterPointStringAttribute(roundaboutBake.value().placements, "road_role",
                                                    "junction.roundabout.center", false);
    const auto entries = filterPointStringAttribute(roundaboutBake.value().placements, "road_role",
                                                    "junction.roundabout.entry", false);
    const auto yields = filterPointStringAttribute(roundaboutBake.value().placements, "road_role",
                                                   "junction.control.yield", false);
    REQUIRE_EQ(centers.getCount(), 1);
    REQUIRE_EQ(entries.getCount(), 4);
    REQUIRE_EQ(yields.getCount(), 4);
    for (int row = 0; row < yields.getCount(); ++row)
        CHECK_EQ(yields.getIntAttribute(row, "traffic_priority", -1), 1);
    CHECK_EQ(centers.getIntAttribute(0, "road_arm_count", 0), 4);
    const auto roundaboutId = centers.getIntAttribute(0, "road_roundabout_id", 0);
    CHECK_NE(roundaboutId, 0);
    for (int i = 0; i < entries.getCount(); ++i) {
        CHECK_EQ(entries.getIntAttribute(i, "road_roundabout_id", 0), roundaboutId);
        CHECK_NE(entries.getIntAttribute(i, "road_node_id", 0), 0);
        CHECK(entries.hasVectorAttribute(i, "road_direction"));
    }

    // Merge/ramp: two feeders of different elevation converge into one edge.
    RoadNetwork rampMerge;
    const auto  low   = rampMerge.addNode(-20.f, 0.f, 12.f, 2.f);
    const auto  high  = rampMerge.addNode(-20.f, 5.f, -12.f, 2.f);
    const auto  merge = rampMerge.addNode(0.f, 3.f, 0.f, 4.f);
    const auto  exit  = rampMerge.addNode(24.f, 3.f, 0.f, 2.f);
    REQUIRE(low.ok());
    REQUIRE(high.ok());
    REQUIRE(merge.ok());
    REQUIRE(exit.ok());
    REQUIRE(rampMerge.addEdge(low.value(), merge.value(), {{}, {-8.f, 2.f, 7.f}, {}}, 1).ok());
    REQUIRE(rampMerge.addEdge(high.value(), merge.value(), {{}, {-8.f, 4.f, -7.f}, {}}, 1).ok());
    REQUIRE(rampMerge.addEdge(merge.value(), exit.value(), {{}, {}}, 2).ok());
    REQUIRE(rampMerge.connectAllTurns(merge.value()).ok());
    checkRoadScenario(rampMerge);

    // Bridge/tunnel-style and grade-separated crossings remain disconnected
    // topology at different Y levels, so proximity never creates a turn.
    RoadNetwork gradeSeparated;
    const auto  groundWest  = gradeSeparated.addNode(-24.f, 0.f, 0.f, 2.f);
    const auto  groundEast  = gradeSeparated.addNode(24.f, 0.f, 0.f, 2.f);
    const auto  bridgeNorth = gradeSeparated.addNode(0.f, 7.f, -24.f, 2.f);
    const auto  bridgeSouth = gradeSeparated.addNode(0.f, 7.f, 24.f, 2.f);
    const auto  tunnelNorth = gradeSeparated.addNode(10.f, -5.f, -24.f, 2.f);
    const auto  tunnelSouth = gradeSeparated.addNode(10.f, -5.f, 24.f, 2.f);
    REQUIRE(groundWest.ok());
    REQUIRE(groundEast.ok());
    REQUIRE(bridgeNorth.ok());
    REQUIRE(bridgeSouth.ok());
    REQUIRE(tunnelNorth.ok());
    REQUIRE(tunnelSouth.ok());
    REQUIRE(gradeSeparated.addEdge(groundWest.value(), groundEast.value(), {{}, {}}, 1, 1).ok());
    REQUIRE(gradeSeparated.addEdge(bridgeNorth.value(), bridgeSouth.value(), {{}, {}}, 1, 1).ok());
    REQUIRE(gradeSeparated.addEdge(tunnelNorth.value(), tunnelSouth.value(), {{}, {}}, 1, 1).ok());
    CHECK_EQ(gradeSeparated.laneLinkCount(), 0);
    checkRoadScenario(gradeSeparated);
}

TEST_CASE("procgen.road.uvUsesWorldMetresAndIgnoresTessellation") {
    auto bakeAtResolution = [&](int segments) {
        RoadNetwork network;
        auto        from = network.addNode(0.f, 0.f, 0.f, 4.f);
        auto        to   = network.addNode(20.f, 0.f, 0.f, 4.f);
        REQUIRE(from.ok());
        REQUIRE(to.ok());
        RoadStyle style;
        style.uvMeters = 4.f;
        REQUIRE(network.addEdge(from.value(), to.value(), {{0.f, 0.f, 0.f}, {20.f, 0.f, 0.f}}, 1, 0, style).ok());
        RoadBakeOptions options;
        options.pathSegmentsPerEdge = segments;
        options.includePiers        = false;
        options.includeMarkings     = false;
        options.includeNavigation   = false;
        options.includeJunctions    = false;
        return bakeRoadNetwork(network, options);
    };

    auto coarse = bakeAtResolution(4);
    auto fine   = bakeAtResolution(20);
    REQUIRE(coarse.ok());
    REQUIRE(fine.ok());
    REQUIRE(!coarse.value().mesh.uvs().empty());
    REQUIRE(!fine.value().mesh.uvs().empty());
    float coarseMaxU = -1e9f;
    float fineMaxU   = -1e9f;
    for (std::size_t i = 0; i < coarse.value().mesh.uvs().size(); i += 2)
        coarseMaxU = std::max(coarseMaxU, coarse.value().mesh.uvs()[i]);
    for (std::size_t i = 0; i < fine.value().mesh.uvs().size(); i += 2)
        fineMaxU = std::max(fineMaxU, fine.value().mesh.uvs()[i]);
    // Degree-one endpoints have no junction apron, so the visible strip spans
    // the full 20 metres and ends at 20 / 4 = 5 texture repeats.
    CHECK_LT(std::fabs(coarseMaxU - 5.f), 1e-3f);
    CHECK_LT(std::fabs(fineMaxU - coarseMaxU), 1e-3f);
}

TEST_CASE("procgen.road.junctionAsphaltUsesPlanarWorldUvs") {
    auto network = RoadNetwork::makeCross(36.f, 2);
    REQUIRE(network.ok());
    RoadBakeOptions options;
    options.includeNavigation = false;
    auto baked                = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());

    std::unique_ptr<MeshBuild> asphalt;
    for (int group = 0; group < baked.value().mesh.getGroupCount(); ++group) {
        if (baked.value().mesh.getGroupName(group) == "asphalt") asphalt = baked.value().mesh.copyGroup(group);
    }
    REQUIRE(asphalt);
    REQUIRE_GE(asphalt->uvs().size(), std::size_t{6});
    float minU = asphalt->uvs()[0], maxU = minU;
    float minV = asphalt->uvs()[1], maxV = minV;
    for (std::size_t i = 0; i + 1 < asphalt->uvs().size(); i += 2) {
        minU = std::min(minU, asphalt->uvs()[i]);
        maxU = std::max(maxU, asphalt->uvs()[i]);
        minV = std::min(minV, asphalt->uvs()[i + 1]);
        maxV = std::max(maxV, asphalt->uvs()[i + 1]);
    }
    CHECK_GT(maxU - minU, 1.f);
    CHECK_GT(maxV - minV, 1.f);
}

TEST_CASE("procgen.road.junctionCurveTessellationHonorsChordError") {
    auto network = RoadNetwork::makeCross(36.f, 2);
    REQUIRE(network.ok());
    auto bakeAtError = [&](float error) {
        RoadBakeOptions options;
        options.includeNavigation  = false;
        options.includeMarkings    = false;
        options.includePiers       = false;
        options.junctionChordError = error;
        return bakeRoadNetwork(network.value(), options);
    };
    auto coarse = bakeAtError(1.f);
    auto fine   = bakeAtError(0.01f);
    REQUIRE(coarse.ok());
    REQUIRE(fine.ok());
    auto coarseCurb = copyRoadGroup(coarse.value().mesh, "curb");
    auto fineCurb   = copyRoadGroup(fine.value().mesh, "curb");
    REQUIRE(coarseCurb);
    REQUIRE(fineCurb);
    CHECK_GT(fineCurb->getVertexCount(), coarseCurb->getVertexCount());
    checkNoDegenerateRoadTriangles(coarse.value().mesh, "asphalt");
    checkNoDegenerateRoadTriangles(fine.value().mesh, "asphalt");
}

TEST_CASE("procgen.road.markings.dashesUseWorldDistanceAcrossTessellation") {
    auto bakeAtResolution = [&](int segments) {
        RoadNetwork network;
        auto        from = network.addNode(0.f, 0.f, 0.f, 2.f);
        auto        to   = network.addNode(40.f, 0.f, 0.f, 2.f);
        REQUIRE(from.ok());
        REQUIRE(to.ok());
        REQUIRE(network.addEdge(from.value(), to.value(), {{}, {}}, 2, 0).ok());
        RoadBakeOptions options;
        options.pathSegmentsPerEdge = segments;
        options.includeJunctions    = false;
        options.includeNavigation   = false;
        options.includePiers        = false;
        return bakeRoadNetwork(network, options);
    };
    auto markingArea = [](const MeshBuild& mesh) {
        for (int group = 0; group < mesh.getGroupCount(); ++group) {
            if (mesh.getGroupName(group) != "marking") continue;
            auto part = mesh.copyGroup(group);
            if (!part) return 0.f;
            float area = 0.f;
            for (int triangle = 0; triangle < part->getIndexCount() / 3; ++triangle) {
                const int ia = part->getIndex(triangle * 3);
                const int ib = part->getIndex(triangle * 3 + 1);
                const int ic = part->getIndex(triangle * 3 + 2);
                const float centerZ = (part->getPositionZ(ia) + part->getPositionZ(ib) +
                                       part->getPositionZ(ic)) /
                                      3.f;
                if (std::fabs(centerZ) > 1.f) continue;
                const float abx = part->getPositionX(ib) - part->getPositionX(ia);
                const float aby = part->getPositionY(ib) - part->getPositionY(ia);
                const float abz = part->getPositionZ(ib) - part->getPositionZ(ia);
                const float acx = part->getPositionX(ic) - part->getPositionX(ia);
                const float acy = part->getPositionY(ic) - part->getPositionY(ia);
                const float acz = part->getPositionZ(ic) - part->getPositionZ(ia);
                const float cx = aby * acz - abz * acy;
                const float cy = abz * acx - abx * acz;
                const float cz = abx * acy - aby * acx;
                area += 0.5f * std::sqrt(cx * cx + cy * cy + cz * cz);
            }
            return area;
        }
        return 0.f;
    };

    auto coarse = bakeAtResolution(4);
    auto fine   = bakeAtResolution(40);
    REQUIRE(coarse.ok());
    REQUIRE(fine.ok());
    const float coarseArea = markingArea(coarse.value().mesh);
    const float fineArea   = markingArea(fine.value().mesh);
    CHECK_LT(std::fabs(coarseArea - 4.84f), 1e-3f);
    CHECK_LT(std::fabs(fineArea - coarseArea), 1e-3f);

    RoadNetwork pathological;
    auto        from = pathological.addNode(0.f, 0.f, 0.f, 2.f);
    auto        to   = pathological.addNode(40.f, 0.f, 0.f, 2.f);
    REQUIRE(from.ok());
    REQUIRE(to.ok());
    RoadStyle tinyPattern;
    tinyPattern.dashLength = 1e-4f;
    tinyPattern.dashGap    = 1e-4f;
    REQUIRE(pathological.addEdge(from.value(), to.value(), {{}, {}}, 2, 0, tinyPattern).ok());
    RoadBakeOptions bounded;
    bounded.pathSegmentsPerEdge = 4;
    bounded.includeJunctions    = false;
    bounded.includeNavigation   = false;
    bounded.includePiers        = false;
    bounded.maximumMeshElements = 1000;
    auto rejected = bakeRoadNetwork(pathological, bounded);
    CHECK(!rejected.ok());
}

TEST_CASE("procgen.road.markings.separateBothDirectionsAndOpposingTraffic") {
    RoadNetwork network;
    const auto  from = network.addNode(0.f, 0.f, 0.f, 2.f);
    const auto  to   = network.addNode(40.f, 0.f, 0.f, 2.f);
    REQUIRE(from.ok());
    REQUIRE(to.ok());
    REQUIRE(network.addEdge(from.value(), to.value(), {{}, {}}, 2, 2).ok());
    RoadBakeOptions options;
    options.includeJunctions  = false;
    options.includeNavigation = false;
    options.includePiers      = false;
    auto baked = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    auto white  = copyRoadGroup(baked.value().mesh, "marking");
    auto yellow = copyRoadGroup(baked.value().mesh, "markingYellow");
    REQUIRE(white);
    REQUIRE(yellow);

    bool whiteForwardBoundary = false, whiteBackwardBoundary = false;
    for (int triangle = 0; triangle < white->getIndexCount() / 3; ++triangle) {
        float centerZ = 0.f;
        for (int corner = 0; corner < 3; ++corner)
            centerZ += white->getPositionZ(white->getIndex(triangle * 3 + corner)) / 3.f;
        whiteForwardBoundary |= centerZ < -3.f && centerZ > -4.f;
        whiteBackwardBoundary |= centerZ > 3.f && centerZ < 4.f;
    }
    CHECK(whiteForwardBoundary);
    CHECK(whiteBackwardBoundary);

    bool yellowLeft = false, yellowRight = false;
    for (int vertex = 0; vertex < yellow->getVertexCount(); ++vertex) {
        yellowLeft |= yellow->getPositionZ(vertex) < -0.05f;
        yellowRight |= yellow->getPositionZ(vertex) > 0.05f;
        CHECK_LT(std::fabs(yellow->getPositionZ(vertex)), 0.5f);
    }
    CHECK(yellowLeft);
    CHECK(yellowRight);
}

TEST_CASE("procgen.road.artifactReusesCompositePublicationBoundary") {
    Params params;
    params.setString("scene", "curve");
    params.setFloat("span", 24.f);
    params.setInt("lanes", 2);
    params.setInt("pathSegments", 12);
    params.setBool("piers", false);
    params.setBool("markings", true);
    params.setBool("navigation", true);
    params.setBool("junctions", true);
    params.setFloat("maskCellSize", 1.f);
    params.setFloat("terrainBlendDistance", 3.f);
    params.setFloat("chunkSize", 16.f);
    params.setInt("lodCount", 3);

    auto generated = generateMeshArtifact("mesh.roadNetwork", params, roadArtifactId());
    REQUIRE(generated.ok());
    REQUIRE_EQ(static_cast<int>(generated.value().type), static_cast<int>(ArtifactType::Composite));
    const auto& composite = std::get<CompositeArtifact>(generated.value().payload);
    const auto* mesh      = composite.find("mesh");
    const auto* collider  = composite.find("collider");
    const auto* drivableCollider = composite.find("collider/drivable");
    const auto* footprint = composite.find("topology");
    const auto* surface   = composite.find("terrain/surface-weight");
    const auto* placements = composite.find("placements");
    const auto* lane      = composite.find("navigation/lane/0");
    REQUIRE(mesh != nullptr);
    REQUIRE(collider != nullptr);
    REQUIRE(drivableCollider != nullptr);
    REQUIRE(footprint != nullptr);
    REQUIRE(surface != nullptr);
    REQUIRE(placements != nullptr);
    REQUIRE(lane != nullptr);
    const auto& meshData     = std::get<MeshData>(mesh->payload);
    const auto& colliderData = std::get<Collider>(collider->payload);
    const auto& drivableColliderData = std::get<Collider>(drivableCollider->payload);
    const auto& maskData     = std::get<Grid2D>(footprint->payload);
    const auto& surfaceData  = std::get<Grid2D>(surface->payload);
    CHECK(colliderData.isValid());
    CHECK(drivableColliderData.isValid());
    CHECK_LT(colliderData.indices.size(), meshData.indices().size());
    CHECK_LT(drivableColliderData.indices.size(), colliderData.indices.size());
    CHECK_EQ(generated.value().schemaVersion.value(), std::uint64_t{2});
    std::array<int, 3> lodTriangleCounts{};
    std::array<int, 3> lodChunkCounts{};
    const ArtifactPart* stableChunk = nullptr;
    for (const auto& part : composite.children) {
        if (!part.role.starts_with("render/chunk/")) continue;
        const auto marker = part.role.rfind("/lod/");
        REQUIRE(marker != std::string::npos);
        const int lod = std::stoi(part.role.substr(marker + 5));
        REQUIRE_GE(lod, 0);
        REQUIRE_LT(lod, 3);
        ++lodChunkCounts[static_cast<std::size_t>(lod)];
        lodTriangleCounts[static_cast<std::size_t>(lod)] += std::get<MeshData>(part.payload).getIndexCount() / 3;
        if (lod == 0 && stableChunk == nullptr) stableChunk = &part;
    }
    REQUIRE(stableChunk != nullptr);
    for (int lod = 0; lod < 3; ++lod) {
        CHECK_GT(lodChunkCounts[static_cast<std::size_t>(lod)], 0);
        CHECK_GT(lodTriangleCounts[static_cast<std::size_t>(lod)], 0);
    }
    CHECK_GT(lodTriangleCounts[0], lodTriangleCounts[1]);
    CHECK_GE(lodTriangleCounts[1], lodTriangleCounts[2]);
    CHECK_GT(maskData.getWidth(), 0);
    CHECK_GT(maskData.getHeight(), 0);
    CHECK(std::find(maskData.cells().begin(), maskData.cells().end(), 1u) != maskData.cells().end());
    CHECK_GT(surfaceData.getWidth(), maskData.getWidth());
    CHECK_GT(surfaceData.getHeight(), maskData.getHeight());
    CHECK_EQ(surfaceData.getMeta("semantics", ""), "road_surface_weight");
    CHECK_EQ(surfaceData.getMeta("weightChannel", ""), "detail");
    CHECK(std::find(surfaceData.detail().begin(), surfaceData.detail().end(), std::uint8_t{0}) !=
          surfaceData.detail().end());
    CHECK(std::find(surfaceData.detail().begin(), surfaceData.detail().end(), std::uint8_t{255}) !=
          surfaceData.detail().end());
    CHECK(std::any_of(surfaceData.detail().begin(), surfaceData.detail().end(),
                      [](std::uint8_t value) { return value > 0 && value < 255; }));
    const float maskOriginX = std::stof(maskData.getMeta("originX", "0"));
    const float maskOriginZ = std::stof(maskData.getMeta("originZ", "0"));
    const float maskCellSize = std::stof(maskData.getMeta("cellSize", "1"));
    int occupiedX = -1, occupiedZ = -1, adjacentX = -1, adjacentZ = -1;
    for (int z = 0; z < maskData.getHeight() && adjacentX < 0; ++z) {
        for (int x = 0; x < maskData.getWidth() && adjacentX < 0; ++x) {
            if (maskData.getCell(x, z) != 1) continue;
            occupiedX = x;
            occupiedZ = z;
            constexpr int offsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
            for (const auto& offset : offsets) {
                const int nx = x + offset[0], nz = z + offset[1];
                if (nx >= 0 && nz >= 0 && nx < maskData.getWidth() && nz < maskData.getHeight() &&
                    maskData.getCell(nx, nz) == 0) {
                    adjacentX = nx;
                    adjacentZ = nz;
                    break;
                }
            }
        }
    }
    REQUIRE(occupiedX >= 0);
    REQUIRE(adjacentX >= 0);
    PointSet vegetation;
    const int inside = vegetation.add(maskOriginX + (static_cast<float>(occupiedX) + 0.5f) * maskCellSize, 0.f,
                                      maskOriginZ + (static_cast<float>(occupiedZ) + 0.5f) * maskCellSize);
    const int adjacent = vegetation.add(maskOriginX + (static_cast<float>(adjacentX) + 0.5f) * maskCellSize, 0.f,
                                        maskOriginZ + (static_cast<float>(adjacentZ) + 0.5f) * maskCellSize);
    const int outside = vegetation.add(maskOriginX - 100.f, 0.f, maskOriginZ - 100.f);
    REQUIRE(vegetation.trySetPointId(inside, 1001u).ok());
    REQUIRE(vegetation.trySetPointId(adjacent, 1002u).ok());
    REQUIRE(vegetation.trySetPointId(outside, 1003u).ok());
    REQUIRE(vegetation.trySetStringAttribute(outside, "species", "oak").ok());
    auto directExclusion = excludePointsByGridMask(vegetation, maskData, maskOriginX, maskOriginZ, maskCellSize, 1,
                                                   0.f, 10000u);
    REQUIRE(directExclusion.ok());
    REQUIRE_EQ(directExclusion.value().getCount(), 2);
    auto cleared = excludePointsByGridMask(vegetation, maskData, maskOriginX, maskOriginZ, maskCellSize, 1,
                                           maskCellSize * 1.1f, 10000u);
    REQUIRE(cleared.ok());
    REQUIRE_EQ(cleared.value().getCount(), 1);
    CHECK_EQ(cleared.value().getPointId(0), std::uint64_t(1003));
    CHECK_EQ(cleared.value().getStringAttribute(0, "species", ""), std::string("oak"));
    auto overBudget = excludePointsByGridMask(vegetation, maskData, maskOriginX, maskOriginZ, maskCellSize, 1,
                                              maskCellSize * 1.1f, 1u);
    CHECK(!overBudget.ok());
    CHECK_GT(std::get<PointSet>(lane->payload).getCount(), 1);
    const auto& placementData = std::get<PointSet>(placements->payload);
    CHECK_GT(placementData.getCount(), 1);
    CHECK(placementData.hasStringAttribute(0, "road_role"));
    const auto& laneData = std::get<PointSet>(lane->payload);
    const auto  laneEdge = laneData.dataAttributes().getInt(0, "in_edge");
    const auto  laneIndex = laneData.dataAttributes().getInt(0, "in_lane");
    const auto  speedLimit = laneData.dataAttributes().getFloat(0, "speed_limit_mps");
    REQUIRE(laneEdge.has_value());
    REQUIRE(laneIndex.has_value());
    REQUIRE(speedLimit.has_value());
    CHECK_GT(*laneEdge, std::int64_t(0));
    CHECK_GE(*laneIndex, std::int64_t(0));
    CHECK_GT(*speedLimit, 0.f);

    Params surfaceOnlyChange = params;
    surfaceOnlyChange.setFloat("terrainBlendDistance", 4.f);
    auto regenerated = generateMeshArtifact("mesh.roadNetwork", surfaceOnlyChange, roadArtifactId());
    REQUIRE(regenerated.ok());
    const auto& regeneratedComposite = std::get<CompositeArtifact>(regenerated.value().payload);
    const auto* regeneratedChunk = regeneratedComposite.find(stableChunk->role);
    REQUIRE(regeneratedChunk != nullptr);
    CHECK_EQ(regeneratedChunk->buildKey, stableChunk->buildKey);
    CHECK_NE(regenerated.value().buildKey, generated.value().buildKey);

    Params invalidBlend = params;
    invalidBlend.setFloat("terrainBlendDistance", 65.f);
    CHECK(!generateMeshArtifact("mesh.roadNetwork", invalidBlend, roadArtifactId()).ok());
}

TEST_CASE("procgen.road.scenes.shortCrossFallsBackToClampedArmHull") {
    auto cross = RoadNetwork::makeCross(16.f, 2);
    REQUIRE(cross.ok());
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 20;
    options.includeJunctions = true;
    options.includeNavigation = false;
    options.includePiers = false;
    auto baked = bakeRoadNetwork(cross.value(), options);
    REQUIRE(baked.ok());

    int asphaltGroup = -1;
    for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g)
        if (baked.value().mesh.getGroupName(g) == "asphalt") asphaltGroup = g;
    REQUIRE_GE(asphaltGroup, 0);
    auto asphalt = baked.value().mesh.copyGroup(asphaltGroup);
    REQUIRE(asphalt);
    float maximumJunctionRadius = 0.f;
    int junctionVertices = 0;
    for (int i = 0; i < asphalt->getVertexCount(); ++i) {
        if (std::fabs(asphalt->getPositionY(i) - 0.01f) > 1e-3f) continue;
        const float x = asphalt->getPositionX(i);
        const float z = asphalt->getPositionZ(i);
        maximumJunctionRadius = std::max(maximumJunctionRadius, std::sqrt(x * x + z * z));
        ++junctionVertices;
    }
    REQUIRE_GT(junctionVertices, 0);
    CHECK_LT(maximumJunctionRadius, 5.5f);
}


TEST_CASE("procgen.road.scenes.pierUprightFootprint") {
    auto bridge = RoadNetwork::makeBridge(30.f, 5.f, 2);
    REQUIRE(bridge.ok());
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 24;
    options.includePiers = true;
    options.includeJunctions = false;
    options.includeNavigation = false;
    auto baked = bakeRoadNetwork(bridge.value(), options);
    REQUIRE(baked.ok());
    int pierGroup = -1;
    for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g)
        if (baked.value().mesh.getGroupName(g) == "pier") pierGroup = g;
    REQUIRE(pierGroup >= 0);
    auto pier = baked.value().mesh.copyGroup(pierGroup);
    REQUIRE(pier);
    // Cluster by X into piers; for each pier check top/bottom XZ extents match
    struct Vert {
        float x, y, z;
    };
    std::vector<Vert> verts;
    for (int i = 0; i < pier->getVertexCount(); ++i)
        verts.push_back({pier->getPositionX(i), pier->getPositionY(i), pier->getPositionZ(i)});
    // unique pier centers roughly by rounding X
    std::vector<float> centers;
    for (const auto& v : verts) {
        bool found = false;
        for (float c : centers)
            if (std::fabs(c - v.x) < 1.5f) {
                found = true;
                break;
            }
        if (!found) centers.push_back(v.x);
    }
    for (float cx : centers) {
        float minXb=1e9,maxXb=-1e9,minZb=1e9,maxZb=-1e9;
        float minXt=1e9,maxXt=-1e9,minZt=1e9,maxZt=-1e9;
        float minY=1e9,maxY=-1e9;
        for (const auto& v : verts) {
            if (std::fabs(v.x - cx) > 2.0f) continue;
            minY=std::min(minY,v.y); maxY=std::max(maxY,v.y);
        }
        const float yCut = minY + 0.15f * (maxY - minY);
        const float yTop = maxY - 0.15f * (maxY - minY);
        for (const auto& v : verts) {
            if (std::fabs(v.x - cx) > 2.0f) continue;
            if (v.y <= yCut) {
                minXb=std::min(minXb,v.x); maxXb=std::max(maxXb,v.x);
                minZb=std::min(minZb,v.z); maxZb=std::max(maxZb,v.z);
            }
            if (v.y >= yTop) {
                minXt=std::min(minXt,v.x); maxXt=std::max(maxXt,v.x);
                minZt=std::min(minZt,v.z); maxZt=std::max(maxZt,v.z);
            }
        }
        const float dx = std::fabs((minXb+maxXb)*0.5f - (minXt+maxXt)*0.5f);
        const float dz = std::fabs((minZb+maxZb)*0.5f - (minZt+maxZt)*0.5f);
        CHECK_LT(dx, 0.05f);
        CHECK_LT(dz, 0.05f);
    }
}


TEST_CASE("procgen.road.scenes.crossJunctionCornerSidewalks") {
    auto cross = RoadNetwork::makeCross(28.f, 2);
    REQUIRE(cross.ok());
    // Arms trim past asphaltHalf so corner arcs own the ring (jr ≈ 3.5 + 2.8).
    float hubJr = 0.f;
    for (const auto& n : cross.value().nodes()) {
        if (std::fabs(n.x) < 1e-3f && std::fabs(n.z) < 1e-3f) hubJr = n.junctionRadius;
    }
    CHECK_GT(hubJr, 5.5f);
    CHECK_LT(hubJr, 7.5f);

    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 20;
    options.includeJunctions    = true;
    options.includeNavigation   = false;
    options.includePiers        = false;
    auto baked                  = bakeRoadNetwork(cross.value(), options);
    REQUIRE(baked.ok());
    int sidewalkGroup = -1;
    for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g) {
        if (baked.value().mesh.getGroupName(g) == "sidewalk") sidewalkGroup = g;
    }
    CHECK_GE(sidewalkGroup, 0);
    auto sw = baked.value().mesh.copyGroup(sidewalkGroup);
    REQUIRE(sw);
    // Outward-center curb returns: sidewalk ring around (jr,jr), no tip disk to the center.
    const float jr = hubJr;
    int         arcHits = 0;
    int         tipHits = 0;
    int         outerSkirtHits = 0;
    for (int i = 0; i < sw->getVertexCount(); ++i) {
        const float x  = std::fabs(sw->getPositionX(i));
        const float z  = std::fabs(sw->getPositionZ(i));
        const float dx = jr - x;
        const float dz = jr - z;
        if (dx < 0.05f || dz < 0.05f) continue;
        const float r = std::sqrt(dx * dx + dz * dz);
        // Sidewalk annulus between outer skirt and curb (no fan into r≈0 tip).
        if (r > 0.9f && r < 2.9f) ++arcHits;
        if (r < 0.55f) ++tipHits;
        const float nx = sw->getNormalX(i);
        const float nz = sw->getNormalZ(i);
        // Outer skirt faces the hub (away from property corner) so overhead views see it.
        if (r > 0.7f && r < 1.4f && (nx * dx + nz * dz) < -0.3f) ++outerSkirtHits;
    }
    CHECK_GT(arcHits, 16);
    CHECK_EQ(tipHits, 0);
    CHECK_GT(outerSkirtHits, 8);

    // Top faces must carry +Y normals (mesh3D CCW / lighting); flipped windings show dark.
    int upHits = 0, downHits = 0;
    for (int i = 0; i < sw->getVertexCount(); ++i) {
        const float ny = sw->getNormalY(i);
        if (ny > 0.5f) ++upHits;
        if (ny < -0.5f) ++downHits;
    }
    CHECK_GT(upHits, 16);
    CHECK_EQ(downHits, 0);
}


TEST_CASE("procgen.road.scenes.crossJunctionArmApronSeam") {
    auto cross = RoadNetwork::makeCross(28.f, 2);
    REQUIRE(cross.ok());
    float hubJr = 0.f;
    for (const auto& n : cross.value().nodes()) {
        if (std::fabs(n.x) < 1e-3f && std::fabs(n.z) < 1e-3f) hubJr = n.junctionRadius;
    }
    REQUIRE(hubJr > 5.f);

    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 28;
    options.includeJunctions    = true;
    options.includeNavigation   = false;
    options.includePiers        = false;
    auto baked                  = bakeRoadNetwork(cross.value(), options);
    REQUIRE(baked.ok());
    int asphaltGroup = -1;
    for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g) {
        if (baked.value().mesh.getGroupName(g) == "asphalt") asphaltGroup = g;
    }
    REQUIRE(asphaltGroup >= 0);
    auto asphalt = baked.value().mesh.copyGroup(asphaltGroup);
    REQUIRE(asphalt);

    // Along +Z arm centerline, asphalt should cover continuously across jr (no gap).
    const float ah = 3.5f;
    auto covers = [&](float x, float z) {
        for (int t = 0; t < asphalt->getIndexCount() / 3; ++t) {
            const int i0 = asphalt->getIndex(t * 3 + 0);
            const int i1 = asphalt->getIndex(t * 3 + 1);
            const int i2 = asphalt->getIndex(t * 3 + 2);
            const float x0 = asphalt->getPositionX(i0), z0 = asphalt->getPositionZ(i0);
            const float x1 = asphalt->getPositionX(i1), z1 = asphalt->getPositionZ(i1);
            const float x2 = asphalt->getPositionX(i2), z2 = asphalt->getPositionZ(i2);
            const float den = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2);
            if (std::fabs(den) < 1e-8f) continue;
            const float a = ((z1 - z2) * (x - x2) + (x2 - x1) * (z - z2)) / den;
            const float b = ((z2 - z0) * (x - x2) + (x0 - x2) * (z - z2)) / den;
            const float c = 1.f - a - b;
            if (a >= -1e-3f && b >= -1e-3f && c >= -1e-3f) return true;
        }
        return false;
    };
    // Probe just inside apron, on the seam, and just onto the arm.
    CHECK(covers(0.f, hubJr - 0.25f));
    CHECK(covers(0.f, hubJr));
    CHECK(covers(0.f, hubJr + 0.25f));
    CHECK(covers(0.f, -(hubJr)));
    CHECK(covers(hubJr, 0.f));
    CHECK(covers(-hubJr, 0.f));
    // Stay within asphalt half-width when probing the seam.
    CHECK(covers(ah * 0.5f, hubJr));
}

TEST_CASE("procgen.road.scenes.crosswalksFaceUpOnEveryArm") {
    auto cross = RoadNetwork::makeCross(28.f, 2);
    REQUIRE(cross.ok());
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 20;
    options.includeNavigation   = false;
    options.includePiers        = false;
    auto baked                  = bakeRoadNetwork(cross.value(), options);
    REQUIRE(baked.ok());

    int markingGroup = -1;
    for (int group = 0; group < baked.value().mesh.getGroupCount(); ++group) {
        if (baked.value().mesh.getGroupName(group) == "marking") markingGroup = group;
    }
    REQUIRE(markingGroup >= 0);
    auto markings = baked.value().mesh.copyGroup(markingGroup);
    REQUIRE(markings);

    int crosswalkTriangles = 0;
    for (int triangle = 0; triangle < markings->getIndexCount() / 3; ++triangle) {
        const int i0 = markings->getIndex(triangle * 3);
        const int i1 = markings->getIndex(triangle * 3 + 1);
        const int i2 = markings->getIndex(triangle * 3 + 2);
        const float ax = markings->getPositionX(i1) - markings->getPositionX(i0);
        const float az = markings->getPositionZ(i1) - markings->getPositionZ(i0);
        const float bx = markings->getPositionX(i2) - markings->getPositionX(i0);
        const float bz = markings->getPositionZ(i2) - markings->getPositionZ(i0);
        const float geometricNormalY = az * bx - ax * bz;
        CHECK_GT(geometricNormalY, 0.f);
        ++crosswalkTriangles;
    }
    CHECK_GE(crosswalkTriangles, 32);
}

TEST_CASE("procgen.road.scenes.latestCommonAngleFactoriesUseUnifiedBake") {
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 28;
    options.includeNavigation   = false;
    options.includePiers        = false;
    options.includeMarkings     = false;

    std::vector<Result<RoadNetwork>> scenes;
    scenes.push_back(RoadNetwork::makeTee(28.f, 2));
    scenes.push_back(RoadNetwork::makeY(28.f, 2));
    scenes.push_back(RoadNetwork::makeFork(28.f, 2));
    scenes.push_back(RoadNetwork::makeSkew(28.f, 2));
    for (float gap : {60.f, 90.f, 120.f, 135.f, 28.f})
        scenes.push_back(RoadNetwork::makeFan(28.f, 2, {0.f, gap, 180.f + gap * 0.5f}));

    for (auto& scene : scenes) {
        REQUIRE(scene.ok());
        REQUIRE_EQ(scene.value().edgeCount(), 3);
        auto baked = bakeRoadNetwork(scene.value(), options);
        REQUIRE(baked.ok());
        auto asphalt = copyRoadGroup(baked.value().mesh, "asphalt");
        REQUIRE(asphalt);
        CHECK(roadTriangleCoversXZ(*asphalt, 0.f, 0.f));
        checkNoDegenerateRoadTriangles(baked.value().mesh, "asphalt");
    }
    CHECK(RoadNetwork::makeScene("tee", 24.f, 4.f, 2, 1).ok());
    CHECK(RoadNetwork::makeScene("y", 24.f, 4.f, 2, 1).ok());
    CHECK(RoadNetwork::makeScene("fork", 24.f, 4.f, 2, 1).ok());
    CHECK(RoadNetwork::makeScene("skew", 24.f, 4.f, 2, 1).ok());
}

TEST_CASE("procgen.road.scenes.commonConnectionsEmitNoDegenerateTriangles") {
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 20;
    options.turnSamples         = 8;
    options.includePiers        = false;

    for (const std::string scene : {"cross", "tee", "y", "fork", "skew", "sloped-t", "curve-uphill",
                                    "y-junction", "tight-turn", "roundabout"}) {
        auto network = RoadNetwork::makeScene(scene, 48.f, 6.f, 2, 1);
        REQUIRE(network.ok());
        auto baked = bakeRoadNetwork(network.value(), options);
        REQUIRE(baked.ok());
        for (const auto& groupName : baked.value().mesh.groupNames())
            checkNoDegenerateRoadTriangles(baked.value().mesh, groupName);
    }
}

TEST_CASE("procgen.road.bidirectional.asymmetricLaneCentersFollowLatestConvention") {
    RoadNetwork network;
    const auto  from = network.addNode(-20.f, 0.f, 0.f, 2.f);
    const auto  to   = network.addNode(20.f, 0.f, 0.f, 2.f);
    REQUIRE(from.ok());
    REQUIRE(to.ok());
    REQUIRE(network.addEdge(from.value(), to.value(), {{}, {}}, 1, 2).ok());
    RoadBakeOptions options;
    options.includeJunctions = false;
    options.includePiers     = false;
    auto baked               = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    REQUIRE_EQ(baked.value().overlay.lanes.size(), 3u);

    bool sawForward = false, sawBackward = false;
    for (const auto& lane : baked.value().overlay.lanes) {
        REQUIRE_GE(lane.xyz.size(), 3u);
        const float z = lane.xyz[2];
        if (lane.inDirection == RoadLaneDirection::Forward) {
            sawForward = true;
            CHECK_GT(z, 3.2f);
            CHECK_LT(z, 3.8f);
        } else {
            sawBackward = true;
            CHECK_LT(z, 0.25f);
        }
    }
    CHECK(sawForward);
    CHECK(sawBackward);
}

TEST_CASE("procgen.road.scenes.latestYFactoryConnectsBothExits") {
    auto network = RoadNetwork::makeY(28.f, 2);
    REQUIRE(network.ok());
    std::uint32_t hub = 0, incoming = 0;
    std::vector<std::uint32_t> outgoing;
    for (const auto& node : network.value().nodes())
        if (std::hypot(node.x, node.z) < 1e-4f) hub = node.id;
    REQUIRE_NE(hub, 0u);
    for (const auto& edge : network.value().edges()) {
        if (edge.to == hub) incoming = edge.id;
        if (edge.from == hub) outgoing.push_back(edge.id);
    }
    REQUIRE_NE(incoming, 0u);
    REQUIRE_EQ(outgoing.size(), 2u);
    for (const auto outEdge : outgoing) {
        const auto found = std::find_if(network.value().laneLinks().begin(), network.value().laneLinks().end(),
                                        [&](const RoadLaneConnection& link) {
                                            return link.inEdge == incoming && link.outEdge == outEdge;
                                        });
        CHECK(found != network.value().laneLinks().end());
    }
}
