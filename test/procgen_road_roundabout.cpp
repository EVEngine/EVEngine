#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetwork.h"
#include "zeroerr/unittest.h"

#include <cmath>

using namespace eve;
using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
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
}  // namespace

TEST_CASE("procgen.road.roundabout.drivingRingCoveredAndCentralIslandOpen") {
    auto network = RoadNetwork::makeRoundabout(48.f, 1);
    REQUIRE(network.ok());
    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includePiers      = false;
    auto baked                = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    int asphaltGroup = -1;
    for (int group = 0; group < baked.value().mesh.getGroupCount(); ++group)
        if (baked.value().mesh.getGroupName(group) == "asphalt") asphaltGroup = group;
    REQUIRE_GE(asphaltGroup, 0);
    auto asphalt = baked.value().mesh.copyGroup(asphaltGroup);
    REQUIRE(asphalt);
    // The factory authors radius 12m and a 3.25m one-lane ring. These probes
    // avoid the approximate arc boundary and distinguish island from asphalt.
    REQUIRE(!roadTriangleCoversXZ(*asphalt, 0.f, 0.f));
    for (float z : {9.f, 10.f})
        for (float x : {-2.f, -1.f, 0.f, 1.f, 2.f}) REQUIRE(!roadTriangleCoversXZ(*asphalt, x, z));
    for (float z : {11.f, 12.f, 13.f})
        for (float x : {-2.f, -1.f, 0.f, 1.f, 2.f}) REQUIRE(roadTriangleCoversXZ(*asphalt, x, z));
}
