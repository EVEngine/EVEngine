#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetwork.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <utility>

using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
bool coversXZ(const MeshBuild& mesh, float x, float z) {
    for (int triangle = 0; triangle < mesh.getIndexCount() / 3; ++triangle) {
        const auto  a  = mesh.getIndex(triangle * 3);
        const auto  b  = mesh.getIndex(triangle * 3 + 1);
        const auto  c  = mesh.getIndex(triangle * 3 + 2);
        const float ax = mesh.getPositionX(a), az = mesh.getPositionZ(a);
        const float bx = mesh.getPositionX(b), bz = mesh.getPositionZ(b);
        const float cx = mesh.getPositionX(c), cz = mesh.getPositionZ(c);
        const float denominator = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
        if (std::fabs(denominator) < 1e-8f) continue;
        const float u = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / denominator;
        const float v = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / denominator;
        if (u >= -1e-4f && v >= -1e-4f && 1.f - u - v >= -1e-4f) return true;
    }
    return false;
}

RoadNetwork validRoundabout(float span = 48.f, int lanes = 1) {
    auto created = RoadNetwork::makeRoundabout(span, lanes);
    REQUIRE(created.ok());
    REQUIRE(created.value().validate().ok());
    return std::move(created).takeValue();
}
}  // namespace

TEST_CASE("procgen.road.roundabout.spec.factory") {
    SUB_CASE("minimum span and maximum lane count are accepted") {
        auto minimum = validRoundabout(32.f, 1);
        auto maximum = validRoundabout(48.f, 3);
        REQUIRE(minimum.edges().size() == 12);
        REQUIRE(maximum.edges().size() == 12);
        REQUIRE(maximum.edges().front().lanesForward == 3);
    };
    SUB_CASE("nonfinite and out-of-range arguments fail") {
        for (float span :
             {-1.f, 0.f, 31.999f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
            REQUIRE(!RoadNetwork::makeRoundabout(span, 1).ok());
        for (int lanes : {-1, 0, 4}) REQUIRE(!RoadNetwork::makeRoundabout(48.f, lanes).ok());
    };
    SUB_CASE("eight one-way ring edges, four two-way approaches, and yield-controlled entries") {
        auto network = validRoundabout();
        REQUIRE(network.nodes().size() == 12);
        REQUIRE(network.edges().size() == 12);
        for (int i = 0; i < 8; ++i) {
            const auto& edge = network.edges()[static_cast<std::size_t>(i)];
            REQUIRE(edge.lanesForward == 1);
            REQUIRE(edge.lanesBackward == 0);
            REQUIRE(edge.controlPoints.size() == 4);
            REQUIRE(std::fabs(edge.style.laneWidth - 3.25f) < 1e-5f);
        }
        for (int i = 8; i < 12; ++i) {
            const auto& edge = network.edges()[static_cast<std::size_t>(i)];
            REQUIRE(edge.lanesForward == 1);
            REQUIRE(edge.lanesBackward == 1);
            REQUIRE(std::fabs(edge.style.laneWidth - 3.f) < 1e-5f);
        }
        for (int i = 0; i < 8; i += 2)
            REQUIRE(network.nodes()[static_cast<std::size_t>(i)].junctionControl == RoadJunctionControl::Yield);
    };
}

TEST_CASE("procgen.road.roundabout.spec.asphaltAnnulus") {
    auto            network = validRoundabout();
    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includePiers      = false;
    auto baked                = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    int asphaltGroup = -1;
    for (int i = 0; i < baked.value().mesh.getGroupCount(); ++i)
        if (baked.value().mesh.getGroupName(i) == "asphalt") asphaltGroup = i;
    REQUIRE(asphaltGroup >= 0);
    auto asphalt = baked.value().mesh.copyGroup(asphaltGroup);
    REQUIRE(asphalt);
    const auto* asphaltView = asphalt.get();
    SUB_CASE("central island and inner margin stay unpaved") {
        REQUIRE(!coversXZ(*asphaltView, 0.f, 0.f));
        for (float z : {9.f, 10.f})
            for (float x : {-2.f, -1.f, 0.f, 1.f, 2.f}) REQUIRE(!coversXZ(*asphaltView, x, z));
    };
    SUB_CASE("driving ring has continuous asphalt across representative arc samples") {
        for (float z : {11.f, 12.f, 13.f})
            for (float x : {-2.f, -1.f, 0.f, 1.f, 2.f}) REQUIRE(coversXZ(*asphaltView, x, z));
    };
}
