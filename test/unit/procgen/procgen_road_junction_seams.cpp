#include "zeroerr/unittest.h"

#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetwork.h"
#include "procgen/spline/SplinePath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

using namespace eve;
using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {

RoadBakeOptions geometryOnly() {
    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includePiers      = false;
    options.includePlacements = false;
    return options;
}

Result<RoadNetwork> slopedCross(bool planar) {
    RoadNetwork                               network;
    const std::array<std::array<float, 3>, 5> points{{
        {0.f, 4.f, 0.f},
        {18.f, 0.f, 0.f},
        {-18.f, planar ? 8.f : 2.f, 0.f},
        {0.f, 6.f, -18.f},
        {0.f, planar ? 2.f : 3.f, 18.f},
    }};
    std::array<std::uint32_t, 5>              ids{};
    for (std::size_t i = 0; i < points.size(); ++i) {
        auto node = network.addNode(points[i][0], points[i][1], points[i][2], i == 0 ? 6.f : 2.f);
        if (!node.ok()) return Result<RoadNetwork>::failure(node.status());
        ids[i] = node.value();
    }
    for (auto index : {1u, 2u, 3u, 4u}) {
        auto edge = network.addEdge(ids[0], ids[index], {{}, {}}, 1, 0);
        if (!edge.ok()) return Result<RoadNetwork>::failure(edge.status());
    }
    return Result<RoadNetwork>::success(std::move(network));
}

int groupIndex(const MeshBuild& mesh, const char* name) {
    for (int index = 0; index < mesh.getGroupCount(); ++index)
        if (mesh.getGroupName(index) == name) return index;
    return -1;
}

}  // namespace

TEST_CASE(
    "procgen.road.bakeRoadNetwork junction preflight: planar slopes bake and incompatible slopes fail atomically") {
    SUB_CASE("four coplanar sloped arms produce an apron on the authored plane") {
        auto network = slopedCross(true);
        REQUIRE(network.ok());
        auto baked = bakeRoadNetwork(network.value(), geometryOnly());
        REQUIRE(baked.ok());
        const int asphaltIndex = groupIndex(baked.value().mesh, "asphalt");
        REQUIRE_GE(asphaltIndex, 0);
        auto asphalt = baked.value().mesh.copyGroup(asphaltIndex);
        REQUIRE(asphalt);
        int   apronVertices = 0;
        float largestError  = 0.f;
        for (int vertex = 0; vertex < asphalt->getVertexCount(); ++vertex) {
            const float x = asphalt->getPositionX(vertex);
            const float z = asphalt->getPositionZ(vertex);
            if (std::fabs(x) > 4.f || std::fabs(z) > 4.f) continue;
            const float authoredY = 4.f - (4.f / 18.f) * x - (2.f / 18.f) * z;
            largestError          = std::max(largestError, std::fabs(asphalt->getPositionY(vertex) - authoredY));
            ++apronVertices;
        }
        REQUIRE_GT(apronVertices, 0);
        REQUIRE_LT(largestError, 1e-4f);
    };

    SUB_CASE("noncoplanar arms return a construction-plane diagnostic and do not edit the network") {
        auto network = slopedCross(false);
        REQUIRE(network.ok());
        const auto revision = network.value().revision();
        auto       baked    = bakeRoadNetwork(network.value(), geometryOnly());
        REQUIRE(!baked.ok());
        REQUIRE(!baked.status().diagnostics().empty());
        const auto& diagnostic = baked.status().diagnostics().front();
        CHECK_EQ(diagnostic.code(), DiagnosticCode::PreconditionViolation);
        CHECK_EQ(diagnostic.message(), "junction arm sockets cannot share one bounded construction plane");
        CHECK_EQ(network.value().revision(), revision);
        CHECK(network.value().validate().ok());
    };
}

TEST_CASE("procgen.road.bakeRoadNetwork junction socket: edge and apron share mouth elevation") {
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

    auto baked = bakeRoadNetwork(network, geometryOnly());
    REQUIRE(baked.ok());
    const int asphaltIndex = groupIndex(baked.value().mesh, "asphalt");
    REQUIRE_GE(asphaltIndex, 0);
    auto asphalt = baked.value().mesh.copyGroup(asphaltIndex);
    REQUIRE(asphalt);

    SplinePath eastPath;
    REQUIRE(eastPath.setKindResult("catmullRom").ok());
    REQUIRE(eastPath.addPointResult(SplinePoint{0.f, 4.f, 0.f}).ok());
    REQUIRE(eastPath.addPointResult(SplinePoint{18.f, 0.f, 0.f}).ok());
    auto frame = eastPath.travelFrameResult(6.f, "clamp", 24);
    REQUIRE(frame.ok());
    const float mouthX = frame.value().sample.x + frame.value().sideX * 1.75f;
    const float mouthZ = frame.value().sample.z + frame.value().sideZ * 1.75f;
    float       low = 1e9f, high = -1e9f;
    int         matches = 0;
    for (int vertex = 0; vertex < asphalt->getVertexCount(); ++vertex) {
        if (std::fabs(asphalt->getPositionX(vertex) - mouthX) > 0.03f ||
            std::fabs(asphalt->getPositionZ(vertex) - mouthZ) > 0.03f)
            continue;
        low  = std::min(low, asphalt->getPositionY(vertex));
        high = std::max(high, asphalt->getPositionY(vertex));
        ++matches;
    }
    REQUIRE_GE(matches, 2);
    REQUIRE_LT(high - low, 0.002f);
}

TEST_CASE(
    "procgen.road.bakeRoadNetwork options: a smooth split needs no apron and disabled markings emit no vertices") {
    auto network = RoadNetwork::makeStraight(24.f, 1);
    REQUIRE(network.ok());
    auto split = network.value().splitEdge(network.value().edges().front().id, 1, 4.f);
    REQUIRE(split.ok());
    auto options            = geometryOnly();
    options.includeMarkings = false;
    auto baked              = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    const int asphaltIndex = groupIndex(baked.value().mesh, "asphalt");
    REQUIRE_GE(asphaltIndex, 0);
    auto asphalt = baked.value().mesh.copyGroup(asphaltIndex);
    REQUIRE(asphalt);
    for (int vertex = 0; vertex < asphalt->getVertexCount(); ++vertex)
        CHECK_LT(std::fabs(asphalt->getPositionY(vertex)), 1e-4f);
    const int markingIndex = groupIndex(baked.value().mesh, "marking");
    if (markingIndex >= 0) {
        auto marking = baked.value().mesh.copyGroup(markingIndex);
        REQUIRE(marking);
        CHECK_EQ(marking->getVertexCount(), 0);
    }
}

TEST_CASE("procgen.road.bakeRoadNetwork short cross: clamped sockets stay on a bounded planar apron") {
    auto network = RoadNetwork::makeCross(16.f, 2);
    REQUIRE(network.ok());
    auto options                = geometryOnly();
    options.pathSegmentsPerEdge = 20;
    auto baked                  = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    const int asphaltIndex = groupIndex(baked.value().mesh, "asphalt");
    REQUIRE_GE(asphaltIndex, 0);
    auto asphalt = baked.value().mesh.copyGroup(asphaltIndex);
    REQUIRE(asphalt);
    int   apronVertices = 0;
    float largestRadius = 0.f;
    for (int triangle = 0; triangle < asphalt->getIndexCount() / 3; ++triangle) {
        const std::array<int, 3> corners{asphalt->getIndex(triangle * 3), asphalt->getIndex(triangle * 3 + 1),
                                         asphalt->getIndex(triangle * 3 + 2)};
        const bool               touchesHub = std::any_of(corners.begin(), corners.end(), [&](int vertex) {
            return std::fabs(asphalt->getPositionX(vertex)) < 1e-5f && std::fabs(asphalt->getPositionZ(vertex)) < 1e-5f;
        });
        if (!touchesHub) continue;
        for (int vertex : corners) {
            const float x = asphalt->getPositionX(vertex);
            const float z = asphalt->getPositionZ(vertex);
            largestRadius = std::max(largestRadius, std::sqrt(x * x + z * z));
            CHECK_LT(std::fabs(asphalt->getPositionY(vertex)), 1e-4f);
            ++apronVertices;
        }
    }
    REQUIRE_GT(apronVertices, 0);
    REQUIRE_LT(largestRadius, 5.5f);
}
