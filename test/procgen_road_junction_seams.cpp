#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetwork.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace eve;
using namespace eve::procgen;
using namespace eve::procgen::road;

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
    auto mouthFrame = eastPath.travelFrameResult(6.f, "clamp", 24);
    REQUIRE(mouthFrame.ok());
    const float mouthX   = mouthFrame.value().sample.x + mouthFrame.value().sideX * 1.75f;
    const float mouthZ   = mouthFrame.value().sample.z + mouthFrame.value().sideZ * 1.75f;
    float       minimumY = 1e9f;
    float       maximumY = -1e9f;
    int         matches  = 0;
    for (int vertex = 0; vertex < asphalt->getVertexCount(); ++vertex) {
        if (std::fabs(asphalt->getPositionX(vertex) - mouthX) > 0.03f ||
            std::fabs(asphalt->getPositionZ(vertex) - mouthZ) > 0.03f)
            continue;
        minimumY = std::min(minimumY, asphalt->getPositionY(vertex));
        maximumY = std::max(maximumY, asphalt->getPositionY(vertex));
        ++matches;
    }
    REQUIRE_GE(matches, 2);
    REQUIRE_LT(maximumY - minimumY, 0.002f);
}

TEST_CASE("procgen.road.bake.smoothSplitIsNotAJunctionAndMarkingToggleIsGlobal") {
    auto network = RoadNetwork::makeStraight(24.f, 1);
    REQUIRE(network.ok());
    REQUIRE_EQ(network.value().edgeCount(), 1);
    const auto edgeId = network.value().edges().front().id;
    auto       split  = network.value().splitEdge(edgeId, 1, 4.f);
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
                if (std::fabs(part->getPositionY(vertex)) > 1e-4f) ++asphaltJunctionVertices;
        }
        if (baked.value().mesh.getGroupName(group) == "marking") markingVertices += part->getVertexCount();
    }
    REQUIRE_EQ(asphaltJunctionVertices, 0);
    REQUIRE_EQ(markingVertices, 0);

    auto cross = RoadNetwork::makeCross(28.f, 2);
    REQUIRE(cross.ok());
    auto unmarkedCross = bakeRoadNetwork(cross.value(), options);
    REQUIRE(unmarkedCross.ok());
    for (int group = 0; group < unmarkedCross.value().mesh.getGroupCount(); ++group) {
        if (unmarkedCross.value().mesh.getGroupName(group) != "marking") continue;
        auto part = unmarkedCross.value().mesh.copyGroup(group);
        REQUIRE(part);
        REQUIRE_EQ(part->getVertexCount(), 0);
    }
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
    narrow.curbWidth      = 0.15f;
    narrow.curbHeight     = 0.25f;
    narrow.sidewalkWidth  = 0.45f;
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
    auto mouthFrame = eastPath.travelFrameResult(6.f, "clamp", 24);
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
    const auto& frame    = mouthFrame.value();
    const float curbSide = 1.75f + narrow.curbWidth;
    const float walkSide = curbSide + narrow.sidewalkWidth;
    const float curbX    = frame.sample.x + frame.sideX * curbSide;
    const float curbZ    = frame.sample.z + frame.sideZ * curbSide;
    const float walkX    = frame.sample.x + frame.sideX * walkSide;
    const float walkZ    = frame.sample.z + frame.sideZ * walkSide;
    REQUIRE_GT(countNear(withJunction.value().mesh, "curb", curbX, narrow.curbHeight, curbZ),
               countNear(edgeOnly.value().mesh, "curb", curbX, narrow.curbHeight, curbZ));
    REQUIRE_GT(countNear(withJunction.value().mesh, "sidewalk", walkX, narrow.sidewalkHeight, walkZ),
               countNear(edgeOnly.value().mesh, "sidewalk", walkX, narrow.sidewalkHeight, walkZ));
}

namespace {
Result<RoadNetwork> makeSlopedCross(bool planar) {
    RoadNetwork                               network;
    const std::array<std::array<float, 3>, 5> positions{{{0.f, 4.f, 0.f},
                                                         {18.f, 0.f, 0.f},
                                                         {-18.f, planar ? 8.f : 2.f, 0.f},
                                                         {0.f, 6.f, -18.f},
                                                         {0.f, planar ? 2.f : 3.f, 18.f}}};
    std::array<std::uint32_t, 5>              ids{};
    for (std::size_t i = 0; i < positions.size(); ++i) {
        auto node = network.addNode(positions[i][0], positions[i][1], positions[i][2], i == 0 ? 6.f : 2.f);
        if (!node.ok()) return Result<RoadNetwork>::failure(node.status());
        ids[i] = node.value();
    }
    for (const auto i : {4u, 1u, 2u, 3u}) {
        auto edge = network.addEdge(ids[0], ids[i], {{}, {}}, 1, 0);
        if (!edge.ok()) return Result<RoadNetwork>::failure(edge.status());
    }
    return Result<RoadNetwork>::success(std::move(network));
}
}  // namespace

TEST_CASE("procgen.road.junction.nonplanarCrossRejectsWithConstructionPlaneDiagnostic") {
    auto network = makeSlopedCross(false);
    REQUIRE(network.ok());
    const auto      revision = network.value().revision();
    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includePiers      = false;
    auto baked                = bakeRoadNetwork(network.value(), options);
    REQUIRE(!baked.ok());
    REQUIRE(!baked.status().diagnostics().empty());
    const auto& diagnostic = baked.status().diagnostics().front();
    REQUIRE_EQ(diagnostic.code(), DiagnosticCode::PreconditionViolation);
    REQUIRE_EQ(diagnostic.message(), "junction arm sockets cannot share one bounded construction plane");
    REQUIRE_EQ(network.value().revision(), revision);
    REQUIRE(network.value().validate().ok());
}

TEST_CASE("procgen.road.junction.planarSlopedCrossApronStaysOnAuthorPlane") {
    auto network = makeSlopedCross(true);
    REQUIRE(network.ok());
    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includePiers      = false;
    auto baked                = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    int   vertices     = 0;
    float maximumError = 0.f;
    for (int group = 0; group < baked.value().mesh.getGroupCount(); ++group) {
        if (baked.value().mesh.getGroupName(group) != "asphalt") continue;
        auto asphalt = baked.value().mesh.copyGroup(group);
        REQUIRE(asphalt);
        for (int vertex = 0; vertex < asphalt->getVertexCount(); ++vertex) {
            if (std::fabs(asphalt->getPositionX(vertex)) > 4.f || std::fabs(asphalt->getPositionZ(vertex)) > 4.f)
                continue;
            ++vertices;
            const float expected =
                4.f - (4.f / 18.f) * asphalt->getPositionX(vertex) - (2.f / 18.f) * asphalt->getPositionZ(vertex);
            maximumError = std::max(maximumError, std::fabs(asphalt->getPositionY(vertex) - expected));
        }
    }
    REQUIRE_GT(vertices, 0);
    REQUIRE_LT(maximumError, 1e-4f);
}

TEST_CASE("procgen.road.scenes.shortCrossFallsBackToClampedArmHull") {
    auto cross = RoadNetwork::makeCross(16.f, 2);
    REQUIRE(cross.ok());
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 20;
    options.includeJunctions    = true;
    options.includeNavigation   = false;
    options.includePiers        = false;
    auto baked                  = bakeRoadNetwork(cross.value(), options);
    REQUIRE(baked.ok());

    int asphaltGroup = -1;
    for (int g = 0; g < baked.value().mesh.getGroupCount(); ++g)
        if (baked.value().mesh.getGroupName(g) == "asphalt") asphaltGroup = g;
    REQUIRE_GE(asphaltGroup, 0);
    auto asphalt = baked.value().mesh.copyGroup(asphaltGroup);
    REQUIRE(asphalt);
    float maximumJunctionRadius = 0.f;
    float maximumPlaneError     = 0.f;
    int   junctionVertices      = 0;
    for (int triangle = 0; triangle < asphalt->getIndexCount() / 3; ++triangle) {
        std::array<int, 3> corners{asphalt->getIndex(triangle * 3), asphalt->getIndex(triangle * 3 + 1),
                                   asphalt->getIndex(triangle * 3 + 2)};
        const bool         touchesHub = std::any_of(corners.begin(), corners.end(), [&](int vertex) {
            return std::fabs(asphalt->getPositionX(vertex)) < 1e-5f && std::fabs(asphalt->getPositionZ(vertex)) < 1e-5f;
        });
        if (!touchesHub) continue;
        for (const int vertex : corners) {
            const float x         = asphalt->getPositionX(vertex);
            const float z         = asphalt->getPositionZ(vertex);
            maximumJunctionRadius = std::max(maximumJunctionRadius, std::sqrt(x * x + z * z));
            maximumPlaneError     = std::max(maximumPlaneError, std::fabs(asphalt->getPositionY(vertex)));
            ++junctionVertices;
        }
    }
    REQUIRE_LT(maximumPlaneError, 1e-4f);
    REQUIRE_GT(junctionVertices, 0);
    REQUIRE_LT(maximumJunctionRadius, 5.5f);
}
