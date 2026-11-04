#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadGeometrySources.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace test_procgen_road_authoring_unit {
using namespace eve;
using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
RoadNetwork authoredRoad(const RoadStyle& style, float elevation = 0.f) {
    RoadNetwork network;
    auto        a = network.addNode(-20.f, elevation, 0.f), b = network.addNode(20.f, elevation, 0.f);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto edge = network.addEdge(a.value(), b.value(),
                                {{-20.f, elevation, 0.f}, {0.f, elevation, 0.f}, {20.f, elevation, 0.f}}, 1, 1, style);
    REQUIRE(edge.ok());
    return network;
}
RoadStyle flatStyle() {
    RoadStyle style;
    style.laneWidth = 3.f;
    style.curbWidth = style.curbHeight = style.sidewalkWidth = style.sidewalkHeight = 0.f;
    style.automaticStructures                                                       = false;
    return style;
}
RoadBakeResult bakeAuthoring(const RoadNetwork& network) {
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 64;
    options.includeMarkings     = false;
    options.includePiers        = false;
    auto baked                  = bakeRoadNetwork(network, options);
    if (!baked.ok()) std::cerr << baked.status().describe() << '\n';
    REQUIRE(baked.ok());
    return std::move(baked).takeValue();
}
int triangles(const MeshBuild& mesh, const std::string& group) {
    int result = 0;
    for (int i = 0; i < mesh.getIndexCount() / 3; ++i)
        if (mesh.getGroupName(mesh.getTriangleGroup(i)) == group) ++result;
    return result;
}
RoadContinuousFacility barrier() {
    RoadContinuousFacility result;
    result.id            = 7;
    result.start         = .2f;
    result.end           = .8f;
    result.lateralOffset = 4.f;
    result.profile       = {{-.2f, 0.f, RoadMaterial::Curb},
                            {.2f, 0.f, RoadMaterial::Curb},
                            {.2f, 1.f, RoadMaterial::Curb},
                            {-.2f, 1.f, RoadMaterial::Curb}};
    return result;
}
}  // namespace

TEST_CASE("procgen.road.authoring.customCrownedSectionDrivesMeshAndNavigation") {
    auto style         = flatStyle();
    style.crossSection = {
        {-4.f, 0.f, RoadMaterial::Asphalt}, {0.f, .3f, RoadMaterial::Asphalt}, {4.f, 0.f, RoadMaterial::Sidewalk}};
    auto network = authoredRoad(style);
    auto baked   = bakeAuthoring(network);
    REQUIRE(triangles(baked.mesh, "asphalt") > 0);
    REQUIRE(baked.overlay.lanes.size() == 2u);
    // Lane at 1.5m sits on crown 0.1875m, then gets the 0.08m navigation lift.
    REQUIRE(std::fabs(baked.overlay.lanes.front().xyz[1] - .2675f) < 1e-4f);
    auto invalid         = style;
    invalid.crossSection = {{-1.f, 0.f, RoadMaterial::Asphalt}, {1.f, 0.f, RoadMaterial::Asphalt}};
    const auto revision  = network.revision();
    REQUIRE(!network.setEdgeStyle(1, invalid).ok());
    REQUIRE(network.revision() == revision);
    invalid              = style;
    invalid.crossSection = {{-4.f, 0.f, RoadMaterial::Asphalt},
                            {4.f, 1.f, RoadMaterial::Curb},
                            {-4.f, 1.f, RoadMaterial::Curb},
                            {4.f, 0.f, RoadMaterial::Curb}};
    REQUIRE(!network.setEdgeStyle(1, invalid).ok());
    REQUIRE(network.revision() == revision);
}

TEST_CASE("procgen.road.authoring.continuousFacilitiesHaveBoundedCapsAndReversePhysicalSide") {
    auto style = flatStyle();
    style.facilities.push_back(barrier());
    auto network = authoredRoad(style);
    auto before  = bakeAuthoring(network);
    REQUIRE(triangles(before.mesh, "curb") > 20);
    REQUIRE(network.reverseEdge(1).ok());
    REQUIRE(network.edges().front().style.facilities.front().lateralOffset == -4.f);
    auto after = bakeAuthoring(network);
    REQUIRE(after.mesh.getVertexCount() == before.mesh.getVertexCount());
    float minZ = 100.f, maxZ = -100.f;
    for (int i = 0; i < after.mesh.getIndexCount() / 3; ++i) {
        if (after.mesh.getGroupName(after.mesh.getTriangleGroup(i)) != "curb") continue;
        for (int c = 0; c < 3; ++c) {
            const float z = after.mesh.getPositionZ(after.mesh.getIndex(i * 3 + c));
            minZ          = std::min(minZ, z);
            maxZ          = std::max(maxZ, z);
        }
    }
    REQUIRE(std::fabs(minZ + 4.2f) < 1e-4f);
    REQUIRE(std::fabs(maxZ + 3.8f) < 1e-4f);
    RoadBakeOptions tiny;
    tiny.maximumMeshElements = 20u;
    REQUIRE(!bakeRoadNetwork(network, tiny).ok());
}

TEST_CASE("procgen.road.authoring.concaveClosedSectionCapsHaveOutwardWinding") {
    auto style               = flatStyle();
    style.crossSectionClosed = true;
    style.crossSection       = {{-4.f, 0.f, RoadMaterial::Asphalt}, {4.f, 0.f, RoadMaterial::Deck},
                                {4.f, -.5f, RoadMaterial::Deck},    {1.f, -.5f, RoadMaterial::Deck},
                                {1.f, -1.f, RoadMaterial::Deck},    {-1.f, -1.f, RoadMaterial::Deck},
                                {-1.f, -.5f, RoadMaterial::Deck},   {-4.f, -.5f, RoadMaterial::Deck}};
    auto network             = authoredRoad(style);
    auto baked               = bakeAuthoring(network);
    int  capTriangles        = 0;
    for (int triangle = 0; triangle < baked.mesh.getIndexCount() / 3; ++triangle) {
        const int a = baked.mesh.getIndex(triangle * 3), b = baked.mesh.getIndex(triangle * 3 + 1),
                  c = baked.mesh.getIndex(triangle * 3 + 2);
        if (std::fabs(baked.mesh.getNormalX(a)) < .9f) continue;
        const float ay = baked.mesh.getPositionY(a), az = baked.mesh.getPositionZ(a), by = baked.mesh.getPositionY(b),
                    bz = baked.mesh.getPositionZ(b), cy = baked.mesh.getPositionY(c), cz = baked.mesh.getPositionZ(c);
        const float areaX = (by - ay) * (cz - az) - (bz - az) * (cy - ay);
        REQUIRE(areaX * baked.mesh.getNormalX(a) > 0.f);
        ++capTriangles;
    }
    REQUIRE(capTriangles == 12);
}

TEST_CASE("procgen.road.authoring.customCrownMarkingsFollowActualSurfaceHeight") {
    auto style         = flatStyle();
    style.crossSection = {
        {-4.f, 0.f, RoadMaterial::Asphalt}, {0.f, .3f, RoadMaterial::Asphalt}, {4.f, 0.f, RoadMaterial::Sidewalk}};
    auto            network = authoredRoad(style);
    RoadBakeOptions options;
    options.includePiers      = false;
    options.includeNavigation = false;
    auto baked                = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    int samples = 0;
    for (int triangle = 0; triangle < baked.value().mesh.getIndexCount() / 3; ++triangle) {
        const auto& mesh = baked.value().mesh;
        const auto  name = mesh.getGroupName(mesh.getTriangleGroup(triangle));
        if (name != "marking" && name != "markingYellow") continue;
        for (int corner = 0; corner < 3; ++corner) {
            const auto  vertex  = mesh.getIndex(triangle * 3 + corner);
            const float surface = .3f * (1.f - std::fabs(mesh.getPositionZ(vertex)) / 4.f);
            REQUIRE(std::fabs(mesh.getPositionY(vertex) - surface - .035f) < 1e-4f);
            ++samples;
        }
    }
    REQUIRE(samples > 0);
}

TEST_CASE("procgen.road.authoring.closingDrivingSegmentControlsNavigationHeight") {
    auto style               = flatStyle();
    style.crossSectionClosed = true;
    style.crossSection       = {{4.f, .2f, RoadMaterial::Deck},
                                {4.f, -.5f, RoadMaterial::Deck},
                                {-4.f, -.5f, RoadMaterial::Deck},
                                {-4.f, .2f, RoadMaterial::Asphalt}};
    auto network             = authoredRoad(style);
    auto baked               = bakeAuthoring(network);
    REQUIRE(std::fabs(baked.overlay.lanes.front().xyz[1] - .28f) < 1e-4f);
    style.crossSection[1].material = RoadMaterial::Asphalt;
    const auto revision            = network.revision();
    REQUIRE(!network.setEdgeStyle(1, style).ok());
    REQUIRE(network.revision() == revision);
}
}  // namespace test_procgen_road_authoring_unit

namespace test_procgen_road_connections_unit {
using namespace eve::procgen::road;

namespace {
RoadNetwork connectionRoad() {
    RoadNetwork network;
    auto        a = network.addNode(-20.f, 0.f, 0.f), hub = network.addNode(0.f, 0.f, 0.f),
         b = network.addNode(20.f, 0.f, 0.f);
    REQUIRE(a.ok());
    REQUIRE(hub.ok());
    REQUIRE(b.ok());
    RoadStyle style;
    style.laneWidth           = 3.f;
    style.automaticStructures = false;
    style.crossSection        = {{-3.f, 0.f, RoadMaterial::Asphalt}, {3.f, 0.f, RoadMaterial::Asphalt}};
    auto first =
        network.addEdge(a.value(), hub.value(), {{-20.f, 0.f, 0.f}, {-10.f, 0.f, 0.f}, {0.f, 0.f, 0.f}}, 1, 1, style);
    auto second =
        network.addEdge(hub.value(), b.value(), {{0.f, 0.f, 0.f}, {10.f, 0.f, 0.f}, {20.f, 0.f, 0.f}}, 1, 1, style);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(network.connectAllTurns(hub.value()).ok());
    return network;
}
RoadConnection squareConnection() {
    RoadConnection connection;
    connection.assetId   = "test.square-road-connection";
    connection.vertices  = {{-3.f, 0.f, -3.f, 0.f, 1.f, 0.f, 0.f, 0.f},
                            {-3.f, 0.f, 3.f, 0.f, 1.f, 0.f, 0.f, 1.f},
                            {3.f, 0.f, 3.f, 0.f, 1.f, 0.f, 1.f, 1.f},
                            {3.f, 0.f, -3.f, 0.f, 1.f, 0.f, 1.f, 0.f}};
    connection.triangles = {{0, 1, 2, RoadMaterial::Asphalt}, {0, 2, 3, RoadMaterial::Asphalt}};
    RoadConnectionPort left;
    left.id          = 1;
    left.edgeId      = 1;
    left.x           = -3.f;
    left.forwardX    = -1.f;
    left.section     = {{-3.f, 0.f, RoadMaterial::Asphalt}, {3.f, 0.f, RoadMaterial::Asphalt}};
    auto right       = left;
    right.id         = 2;
    right.edgeId     = 2;
    right.x          = 3.f;
    right.forwardX   = 1.f;
    connection.ports = {left, right};
    return connection;
}
}  // namespace

TEST_CASE("procgen.road.authoring.customConnectionMeshesSocketsAndLaneTurnsSharePorts") {
    auto network = connectionRoad();
    REQUIRE(network.setNodeConnection(2, squareConnection()).ok());
    RoadBakeOptions options;
    options.includeMarkings = false;
    options.includePiers    = false;
    auto baked              = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    REQUIRE(!baked.value().overlay.turns.empty());
    const auto& lane = baked.value().overlay.lanes.front();
    REQUIRE(std::fabs(lane.xyz[lane.xyz.size() - 3] + 3.f) < .01f);
    const auto& turn = baked.value().overlay.turns.front();
    REQUIRE(std::fabs(std::fabs(turn.xyz.front()) - 3.f) < .01f);
    REQUIRE(std::fabs(std::fabs(turn.xyz[turn.xyz.size() - 3]) - 3.f) < .01f);
    REQUIRE(network.reverseEdge(1).ok());
    REQUIRE(bakeRoadNetwork(network, options).ok());
    const auto revision = network.revision();
    auto       wrong    = squareConnection();
    wrong.ports[0].x    = -2.f;
    REQUIRE(!network.setNodeConnection(2, wrong).ok());
    REQUIRE(network.revision() == revision);
    auto widened         = network.edges().front().style;
    widened.crossSection = {{-4.f, 0.f, RoadMaterial::Asphalt}, {4.f, 0.f, RoadMaterial::Asphalt}};
    // Bound asset ports reject incompatible authoring without publishing a partial network.
    REQUIRE(!network.setEdgeStyle(1, widened).ok());
    REQUIRE(network.revision() == revision);
    REQUIRE(bakeRoadNetwork(network, options).ok());
}

TEST_CASE("procgen.road.authoring.customConnectionRejectsMalformedGeometryAndStaleBindingsAtomically") {
    auto       network   = connectionRoad();
    const auto revision  = network.revision();
    auto       wrong     = squareConnection();
    wrong.triangles[0].c = 999;
    REQUIRE(!network.setNodeConnection(2, wrong).ok());
    REQUIRE(network.revision() == revision);
    wrong                 = squareConnection();
    wrong.ports[0].edgeId = 77;
    REQUIRE(!network.setNodeConnection(2, wrong).ok());
    REQUIRE(network.revision() == revision);
    wrong             = squareConnection();
    wrong.ports[1].id = 1;
    REQUIRE(!network.setNodeConnection(2, wrong).ok());
    wrong                        = squareConnection();
    wrong.ports[0].sectionClosed = true;
    REQUIRE(!network.setNodeConnection(2, wrong).ok());
    wrong               = squareConnection();
    wrong.vertices[0].u = .5f;
    wrong.vertices[0].v = .5f;
    REQUIRE(!network.setNodeConnection(2, wrong).ok());
    REQUIRE(network.setNodeConnection(2, squareConnection()).ok());
    const auto attachedRevision = network.revision();
    REQUIRE(!network.removeEdge(1).ok());
    REQUIRE(network.revision() == attachedRevision);
    REQUIRE(network.setNodeConnection(2, {}).ok());
    REQUIRE(network.removeEdge(1).ok());
}

TEST_CASE("procgen.road.authoring.continuousFacilityCrossesCustomConnectionWithoutSocketGap") {
    auto network = connectionRoad();
    for (const auto& edge : network.edges()) {
        auto                   style = edge.style;
        RoadContinuousFacility facility;
        facility.id            = 9;
        facility.lateralOffset = 4.f;
        facility.profile       = {{-.2f, 0.f, RoadMaterial::Curb},
                                  {.2f, 0.f, RoadMaterial::Curb},
                                  {.2f, 1.f, RoadMaterial::Curb},
                                  {-.2f, 1.f, RoadMaterial::Curb}};
        style.facilities.push_back(facility);
        REQUIRE(network.setEdgeStyle(edge.id, style).ok());
    }
    REQUIRE(network.setNodeConnection(2, squareConnection()).ok());
    RoadBakeOptions options;
    options.includeNavigation = false;
    options.includeMarkings   = false;
    options.includePiers      = false;
    auto baked                = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    bool centerPresent = false;
    for (int vertex = 0; vertex < baked.value().mesh.getVertexCount(); ++vertex) {
        const auto& mesh = baked.value().mesh;
        if (std::fabs(mesh.getPositionX(vertex)) < 1e-4f && std::fabs(mesh.getPositionZ(vertex) + 4.2f) < 1e-4f)
            centerPresent = true;
    }
    REQUIRE(centerPresent);
    REQUIRE(network.reverseEdge(1).ok());
    REQUIRE(bakeRoadNetwork(network, options).ok());
}
}  // namespace test_procgen_road_connections_unit

namespace test_procgen_road_junction_style_unit {
using namespace eve::procgen;
using namespace eve::procgen::road;

namespace {
template <class T>
T checked(eve::Result<T> result) {
    if (!result.ok()) throw std::runtime_error(result.status().describe());
    return std::move(result).takeValue();
}
RoadNetwork cross(RoadStyle style, bool varied = false) {
    RoadNetwork network;
    const auto  hub = checked(network.addNode(0, 0, 0, 6));
    for (int i = 0; i < 4; ++i) {
        const float x   = i == 0 ? 20.f : i == 1 ? -20.f : 0.f;
        const float z   = i == 2 ? 20.f : i == 3 ? -20.f : 0.f;
        const auto  end = checked(network.addNode(x, 0, z));
        auto        arm = style;
        if (varied && i == 0) {
            arm.curbWidth  = .2f;
            arm.curbHeight = .4f;
        }
        (void)checked(network.addEdge(hub, end, {{0, 0, 0}, {x, 0, z}}, 1, 1, arm));
    }
    return network;
}
RoadBakeResult bake(const RoadNetwork& network) {
    RoadBakeOptions options;
    options.includeMarkings = options.includeNavigation = options.includePiers = options.includePlacements = false;
    return checked(bakeRoadNetwork(network, options));
}
int triangles(const MeshBuild& mesh, const std::string& group, bool junctionOnly = false) {
    auto sources = checked(PreparedRoadGeometrySources::prepare(mesh));
    int  count   = 0;
    for (int t = 0; t < mesh.getIndexCount() / 3; ++t)
        if (mesh.getGroupName(mesh.getTriangleGroup(t)) == group) {
            if (!junctionOnly || std::any_of(sources.spans().begin(), sources.spans().end(), [&](const auto& span) {
                    return span.kind == RoadGeometrySourceKind::Junction && t >= static_cast<int>(span.firstTriangle) &&
                           t < static_cast<int>(span.firstTriangle + span.triangleCount);
                }))
                ++count;
        }
    return count;
}
}  // namespace

TEST_CASE("procgen.road.junctionStyle.bareCustomCrossDoesNotInventCurbsOrWalks") {
    RoadStyle style;
    style.laneWidth           = 3;
    style.automaticStructures = false;
    style.crossSection        = {{-3, 0, RoadMaterial::Asphalt}, {3, 0, RoadMaterial::Asphalt}};
    auto result               = bake(cross(style));
    REQUIRE_GT(triangles(result.mesh, "asphalt"), 0);
    REQUIRE_EQ(triangles(result.mesh, "curb"), 0);
    REQUIRE_EQ(triangles(result.mesh, "sidewalk"), 0);
}

TEST_CASE("procgen.road.junctionStyle.narrowCornerBandsRespectAuthorWidths") {
    RoadStyle style;
    style.laneWidth           = 3;
    style.curbWidth           = .08f;
    style.sidewalkWidth       = .12f;
    style.curbHeight          = .18f;
    style.sidewalkHeight      = .07f;
    style.automaticStructures = false;
    auto  result              = bake(cross(style));
    auto  sources             = checked(PreparedRoadGeometrySources::prepare(result.mesh));
    float curbMinimum = 100, walkMinimum = 100;
    int   curbCount = 0, walkCount = 0;
    for (const auto& source : sources.spans()) {
        if (source.kind != RoadGeometrySourceKind::Junction) continue;
        for (std::uint32_t t = source.firstTriangle; t < source.firstTriangle + source.triangleCount; ++t) {
            const auto group = result.mesh.getGroupName(result.mesh.getTriangleGroup(static_cast<int>(t)));
            if (group != "curb" && group != "sidewalk") continue;
            for (int corner = 0; corner < 3; ++corner) {
                const auto  v      = result.mesh.getIndex(static_cast<int>(t) * 3 + corner);
                const float dx     = 6 - std::fabs(result.mesh.getPositionX(static_cast<int>(v)));
                const float dz     = 6 - std::fabs(result.mesh.getPositionZ(static_cast<int>(v)));
                const float radius = std::hypot(dx, dz);
                if (group == "curb") {
                    curbMinimum = std::min(curbMinimum, radius);
                    ++curbCount;
                } else {
                    walkMinimum = std::min(walkMinimum, radius);
                    ++walkCount;
                }
            }
        }
    }
    REQUIRE_GT(curbCount, 0);
    REQUIRE_GT(walkCount, 0);
    REQUIRE_GT(curbMinimum, 3 - .08f - .021f);
    REQUIRE_LT(curbMinimum, 3 - .08f + .001f);
    REQUIRE_GT(walkMinimum, 3 - .08f - .12f - .061f);
    REQUIRE_LT(walkMinimum, 3 - .08f - .12f + .001f);
}

TEST_CASE("procgen.road.junctionStyle.independentlyDisabledBandsStayAbsent") {
    for (int mode = 0; mode < 2; ++mode) {
        RoadStyle style;
        style.laneWidth           = 3;
        style.automaticStructures = false;
        style.curbWidth           = mode == 0 ? 0.f : .12f;
        style.sidewalkWidth       = mode == 0 ? .2f : 0.f;
        auto result               = bake(cross(style));
        REQUIRE_EQ(triangles(result.mesh, mode == 0 ? "curb" : "sidewalk", true), 0);
        REQUIRE_GT(triangles(result.mesh, mode == 0 ? "sidewalk" : "curb", true), 0);
    }
}

TEST_CASE("procgen.road.junctionStyle.unequalArmsKeepTheirOwnMouthProfile") {
    RoadStyle style;
    style.laneWidth           = 3;
    style.curbWidth           = .1f;
    style.curbHeight          = .2f;
    style.sidewalkWidth       = .2f;
    style.sidewalkHeight      = .07f;
    style.automaticStructures = false;
    auto result               = bake(cross(style, true));
    auto sources              = checked(PreparedRoadGeometrySources::prepare(result.mesh));
    bool east = false, west = false;
    for (const auto& source : sources.spans()) {
        if (source.kind != RoadGeometrySourceKind::Junction) continue;
        for (std::uint32_t t = source.firstTriangle; t < source.firstTriangle + source.triangleCount; ++t) {
            if (result.mesh.getGroupName(result.mesh.getTriangleGroup(static_cast<int>(t))) != "curb") continue;
            for (int corner = 0; corner < 3; ++corner) {
                const int   v = static_cast<int>(result.mesh.getIndex(static_cast<int>(t) * 3 + corner));
                const float x = result.mesh.getPositionX(v), y = result.mesh.getPositionY(v),
                            z = result.mesh.getPositionZ(v);
                east          = east || (std::fabs(x - 6) < .003f && std::fabs(y - .4f) < 1e-4f &&
                                std::fabs(std::fabs(z) - 3.2f) < 1e-4f);
                west          = west || (std::fabs(x + 6) < .003f && std::fabs(y - .2f) < 1e-4f &&
                                std::fabs(std::fabs(z) - 3.1f) < 1e-4f);
            }
        }
    }
    REQUIRE(east);
    REQUIRE(west);
}
}  // namespace test_procgen_road_junction_style_unit

namespace test_procgen_road_reference_unit {
using namespace eve::procgen::road;
using eve::procgen::MeshBuild;
namespace {
bool asphaltContains(const eve::procgen::MeshBuild& mesh, float x, float z) {
    for (int t = 0; t < mesh.getIndexCount() / 3; ++t) {
        if (mesh.getGroupName(mesh.getTriangleGroup(t)) != "asphalt") continue;
        const int   a = mesh.getIndex(3 * t), b = mesh.getIndex(3 * t + 1), c = mesh.getIndex(3 * t + 2);
        const float ax = mesh.getPositionX(a), az = mesh.getPositionZ(a), bx = mesh.getPositionX(b),
                    bz = mesh.getPositionZ(b), cx = mesh.getPositionX(c), cz = mesh.getPositionZ(c);
        const float d = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
        if (std::fabs(d) < 1e-7f) continue;
        const float u = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / d;
        const float v = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / d;
        if (u >= 0.f && v >= 0.f && u + v <= 1.f) return true;
    }
    return false;
}

}  // namespace

namespace {

}  // namespace

TEST_CASE("procgen.road.authoring.referenceTeeConcaveReturnsExcludeStraightMiterWedges") {
    auto network = RoadNetwork::makeScene("tee", 36.f, 6.f, 2, 1);
    REQUIRE(network.ok());
    const auto edges = network.value().edges();
    for (const auto& edge : edges) {
        auto style      = edge.style;
        style.laneWidth = 3.f;
        style.curbWidth = style.curbHeight = style.sidewalkWidth = style.sidewalkHeight = 0.f;
        style.automaticStructures                                                       = false;
        REQUIRE(network.value().setEdgeStyle(edge.id, style).ok());
    }
    RoadBakeOptions options;
    options.includeNavigation = options.includeMarkings = options.includePlacements = false;
    auto baked = bakeRoadNetwork(network.value(), options);
    REQUIRE(baked.ok());
    REQUIRE(asphaltContains(baked.value().mesh, 3.2f, 4.2f));
    REQUIRE(asphaltContains(baked.value().mesh, -3.2f, 4.2f));
    REQUIRE(!asphaltContains(baked.value().mesh, 4.2f, 4.2f));
    REQUIRE(!asphaltContains(baked.value().mesh, -4.2f, 4.2f));
    REQUIRE(asphaltContains(baked.value().mesh, 0.f, 0.f));
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
            REQUIRE(std::fabs(z + 3.5f) < 1e-4f);
        } else {
            sawBackward = true;
            REQUIRE(std::fabs(z - (3.5f - 3.5f * static_cast<float>(lane.inLane))) < 1e-4f);
        }
    }
    REQUIRE(sawForward);
    REQUIRE(sawBackward);
}

TEST_CASE("procgen.road.bidirectional.asymmetricLaneCentersRotateWithAuthoredFrame") {
    for (const auto direction : {RoadControlPoint{1.f, 0.f, 0.f}, RoadControlPoint{0.f, 0.f, 1.f},
                                 RoadControlPoint{-1.f, 0.f, 0.f}, RoadControlPoint{0.f, 0.f, -1.f}}) {
        RoadNetwork network;
        auto        from = network.addNode(-20.f * direction.x, 0.f, -20.f * direction.z);
        auto        to   = network.addNode(20.f * direction.x, 0.f, 20.f * direction.z);
        REQUIRE(from.ok());
        REQUIRE(to.ok());
        REQUIRE(network.addEdge(from.value(), to.value(), {{}, {}}, 1, 2).ok());
        RoadBakeOptions options;
        options.includeJunctions = false;
        options.includePiers     = false;
        auto baked               = bakeRoadNetwork(network, options);
        REQUIRE(baked.ok());
        REQUIRE_EQ(baked.value().overlay.lanes.size(), 3u);
        for (const auto& lane : baked.value().overlay.lanes) {
            REQUIRE_GE(lane.xyz.size(), 6u);
            const float expected =
                lane.inDirection == RoadLaneDirection::Forward ? 3.5f : -3.5f + 3.5f * static_cast<float>(lane.inLane);
            // Horizontal authored side is world-up cross forward, independent
            // of a global left/right traffic policy.
            for (std::size_t i = 0; i < lane.xyz.size(); i += 3) {
                const float lateral = lane.xyz[i] * direction.z - lane.xyz[i + 2] * direction.x;
                REQUIRE(std::fabs(lateral - expected) < 1e-4f);
            }
        }
    }
}

TEST_CASE("procgen.road.markings.dashesUseWorldDistanceAcrossTessellation") {
    auto bakeAtResolution = [&](int segments, bool metric, float height) {
        RoadNetwork network;
        auto        from = network.addNode(0.f, 0.f, 0.f, 2.f);
        auto        to   = network.addNode(40.f, height, 0.f, 2.f);
        if (!from.ok()) return eve::Result<RoadBakeResult>::failure(from.status());
        if (!to.ok()) return eve::Result<RoadBakeResult>::failure(to.status());
        RoadStyle style;
        style.samplingSpacingMeters = metric ? 2.f : 0.f;
        auto edge                   = network.addEdge(from.value(), to.value(), {{}, {}}, 2, 0, style);
        if (!edge.ok()) return eve::Result<RoadBakeResult>::failure(edge.status());
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
                const int   ia      = part->getIndex(triangle * 3);
                const int   ib      = part->getIndex(triangle * 3 + 1);
                const int   ic      = part->getIndex(triangle * 3 + 2);
                const float centerZ = (part->getPositionZ(ia) + part->getPositionZ(ib) + part->getPositionZ(ic)) / 3.f;
                if (std::fabs(centerZ) > 1.f) continue;
                const float abx = part->getPositionX(ib) - part->getPositionX(ia);
                const float aby = part->getPositionY(ib) - part->getPositionY(ia);
                const float abz = part->getPositionZ(ib) - part->getPositionZ(ia);
                const float acx = part->getPositionX(ic) - part->getPositionX(ia);
                const float acy = part->getPositionY(ic) - part->getPositionY(ia);
                const float acz = part->getPositionZ(ic) - part->getPositionZ(ia);
                const float cx  = aby * acz - abz * acy;
                const float cy  = abz * acx - abx * acz;
                const float cz  = abx * acy - aby * acx;
                area += 0.5f * std::sqrt(cx * cx + cy * cy + cz * cz);
            }
            return area;
        }
        return 0.f;
    };

    for (float height : {0.f, 30.f}) {
        for (bool metric : {false, true}) {
            auto coarse = bakeAtResolution(4, metric, height);
            auto fine   = bakeAtResolution(40, metric, height);
            REQUIRE(coarse.ok());
            REQUIRE(fine.ok());
            const float coarseArea = markingArea(coarse.value().mesh);
            const float fineArea   = markingArea(fine.value().mesh);
            // 40m flat: six full 3.5m dashes plus a 1m tail.
            // 50m graded (40,30,0): eight full 3.5m dashes.
            const float expectedArea = height == 0.f ? 4.84f : 6.16f;
            REQUIRE_LT(std::fabs(coarseArea - expectedArea), 1e-3f);
            REQUIRE_LT(std::fabs(fineArea - coarseArea), 1e-3f);
            if (!metric) REQUIRE_NE(coarse.value().mesh.getVertexCount(), fine.value().mesh.getVertexCount());
        }
    }

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
    auto rejected               = bakeRoadNetwork(pathological, bounded);
    REQUIRE(!rejected.ok());
}

TEST_CASE("procgen.road.scenes.commonConnectionsEmitNoDegenerateTriangles") {
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = 20;
    options.turnSamples         = 8;
    options.includePiers        = false;

    int invalidTriangles = 0;
    for (const std::string scene :
         {"cross", "tee", "y", "fork", "skew", "sloped-t", "curve-uphill", "y-junction", "tight-turn", "roundabout"}) {
        auto network = RoadNetwork::makeScene(scene, 48.f, 6.f, 2, 1);
        REQUIRE(network.ok());
        auto baked = bakeRoadNetwork(network.value(), options);
        REQUIRE(baked.ok());
        for (int triangle = 0; triangle < baked.value().mesh.getIndexCount() / 3; ++triangle) {
            const auto& mesh = baked.value().mesh;
            const int   a    = mesh.getIndex(triangle * 3);
            const int   b    = mesh.getIndex(triangle * 3 + 1);
            const int   c    = mesh.getIndex(triangle * 3 + 2);
            const float abx  = mesh.getPositionX(b) - mesh.getPositionX(a);
            const float aby  = mesh.getPositionY(b) - mesh.getPositionY(a);
            const float abz  = mesh.getPositionZ(b) - mesh.getPositionZ(a);
            const float acx  = mesh.getPositionX(c) - mesh.getPositionX(a);
            const float acy  = mesh.getPositionY(c) - mesh.getPositionY(a);
            const float acz  = mesh.getPositionZ(c) - mesh.getPositionZ(a);
            const float nx = aby * acz - abz * acy, ny = abz * acx - abx * acz, nz = abx * acy - aby * acx;
            const float areaSquared = nx * nx + ny * ny + nz * nz;
            if (areaSquared > 1e-8f) continue;
            ++invalidTriangles;
            std::cerr << "DEGENERATE scene=" << scene << " group=" << mesh.getGroupName(mesh.getTriangleGroup(triangle))
                      << " areaSquared=" << areaSquared;
            for (int vertex : {a, b, c})
                std::cerr << " (" << mesh.getPositionX(vertex) << "," << mesh.getPositionY(vertex) << ","
                          << mesh.getPositionZ(vertex) << ")";
            std::cerr << '\n';
        }
    }
    REQUIRE_EQ(invalidTriangles, 0);
}

TEST_CASE("procgen.road.sections.noncollinearTerminalCapsSurviveRotationAndTranslation") {
    int baselineCount = -1;
    for (const auto direction : {RoadControlPoint{1.f, 0.f, 0.f}, RoadControlPoint{0.f, 0.f, 1.f},
                                 RoadControlPoint{-1.f, 0.f, 0.f}, RoadControlPoint{0.f, 0.f, -1.f}}) {
        RoadNetwork network;
        auto        from = network.addNode(100.f - 20.f * direction.x, -12.f, 75.f - 20.f * direction.z);
        auto        to   = network.addNode(100.f + 20.f * direction.x, -12.f, 75.f + 20.f * direction.z);
        REQUIRE(from.ok());
        REQUIRE(to.ok());
        REQUIRE(network.addEdge(from.value(), to.value(), {{}, {}}, 1, 0).ok());
        RoadBakeOptions options;
        options.includeJunctions  = false;
        options.includeNavigation = false;
        options.includePiers      = false;
        auto baked                = bakeRoadNetwork(network, options);
        REQUIRE(baked.ok());
        const auto& mesh         = baked.value().mesh;
        int         capTriangles = 0;
        for (int triangle = 0; triangle < mesh.getIndexCount() / 3; ++triangle) {
            if (mesh.getGroupName(mesh.getTriangleGroup(triangle)) != "curb") continue;
            const int a = mesh.getIndex(triangle * 3), b = mesh.getIndex(triangle * 3 + 1),
                      c = mesh.getIndex(triangle * 3 + 2);
            auto along  = [&](int vertex) {
                return (mesh.getPositionX(vertex) - 100.f) * direction.x +
                       (mesh.getPositionZ(vertex) - 75.f) * direction.z;
            };
            const float distance = along(a);
            if (std::fabs(std::fabs(distance) - 20.f) > 1e-4f || std::fabs(along(b) - distance) > 1e-4f ||
                std::fabs(along(c) - distance) > 1e-4f)
                continue;
            const float ax = mesh.getPositionX(b) - mesh.getPositionX(a),
                        ay = mesh.getPositionY(b) - mesh.getPositionY(a),
                        az = mesh.getPositionZ(b) - mesh.getPositionZ(a);
            const float bx = mesh.getPositionX(c) - mesh.getPositionX(a),
                        by = mesh.getPositionY(c) - mesh.getPositionY(a),
                        bz = mesh.getPositionZ(c) - mesh.getPositionZ(a);
            const float nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
            REQUIRE_GT(nx * nx + ny * ny + nz * nz, 1e-8f);
            ++capTriangles;
        }
        REQUIRE_GT(capTriangles, 0);
        if (baselineCount < 0) baselineCount = capTriangles;
        REQUIRE_EQ(capTriangles, baselineCount);
    }
}
}  // namespace test_procgen_road_reference_unit

namespace test_procgen_road_recipe_snapshot_unit {
using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.recipeSnapshotRejectsInvalidWithoutSceneFallback") {
    Params params;
    REQUIRE(resolveRoadRecipeNetwork(params).ok());
    params.setBool("networkSnapshot", true);
    REQUIRE(!bakeRoadNetworkRecipe(params).ok());
    params.setString("networkSnapshot", "{}");
    REQUIRE(!resolveRoadRecipeNetwork(params).ok());
    params.setString("networkSnapshot", "");
    REQUIRE(!resolveRoadRecipeNetwork(params).ok());
    auto source = RoadNetwork::makeStraight(36.f, 2);
    REQUIRE(source.ok());
    auto snapshot = captureRoadNetwork(source.value());
    REQUIRE(snapshot.ok());
    auto value                                               = snapshot.value();
    (*value.getIf<eve::Value::Object>())["blockedLaneLinks"] = true;
    REQUIRE(!restoreRoadNetwork(value).ok());
    value                                                 = snapshot.value();
    (*value.getIf<eve::Value::Object>())["schemaVersion"] = 2;
    REQUIRE(!restoreRoadNetwork(value).ok());
    value                                               = snapshot.value();
    (*value.getIf<eve::Value::Object>())["futureField"] = "ignored";
    REQUIRE(restoreRoadNetwork(value).ok());
    auto before = params.canonicalString();
    REQUIRE(!restoreRoadNetwork(eve::Value{}).ok());
    REQUIRE(params.canonicalString() == before);
    eve::Value deep;
    for (int i = 0; i < 70; ++i) deep = eve::Value::Array{std::move(deep)};
    (*value.getIf<eve::Value::Object>())["futureField"] = std::move(deep);
    REQUIRE(!restoreRoadNetwork(value).ok());
    auto                          tooManyMarkers = source.value();
    std::vector<RoadControlPoint> many;
    for (int i = 0; i < 4097; ++i) many.push_back({-18.f + 36.f * static_cast<float>(i) / 4096.f, 0.f, 0.f});
    REQUIRE(tooManyMarkers.setEdgeControlPoints(1, std::move(many)).ok());
    REQUIRE(!setRoadRecipeNetwork(params, tooManyMarkers).ok());
    REQUIRE(params.canonicalString() == before);
}
}  // namespace test_procgen_road_recipe_snapshot_unit

namespace test_procgen_road_side_placement_unit {
using namespace eve::procgen;
using namespace eve::procgen::road;
namespace {
RoadNetwork placementNetwork(RoadStyle style) {
    style.laneWidth           = 3;
    style.automaticStructures = false;
    RoadNetwork network;
    auto        a = network.addNode(0, 0, 0), b = network.addNode(0, 0, 30);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(network.addEdge(a.value(), b.value(), {{0, 0, 0}, {0, 0, 15}, {0, 0, 30}}, 1, 1, style).ok());
    return network;
}
}  // namespace

TEST_CASE("procgen.road.authoring.sidePlacementMatchesMeasuredOffsetsAndSingleCenterBudget") {
    struct Sample {
        RoadSideReference reference;
        bool              left;
        float             lateral, height, yaw, expectedX;
    };
    const Sample samples[] = {{RoadSideReference::Center, true, 0, .02f, 0, 0},
                              {RoadSideReference::Center, true, 2, .02f, 0, 2},
                              {RoadSideReference::Center, true, 0, 1.02f, 0, 0},
                              {RoadSideReference::AsphaltEdges, true, 0, .02f, 0, -3},
                              {RoadSideReference::AsphaltEdges, false, 0, .02f, 0, 3},
                              {RoadSideReference::Center, true, 0, .02f, 45, 0}};
    for (const auto& sample : samples) {
        RoadStyle style;
        style.sidePlacement = {sample.reference, sample.lateral, sample.height, sample.yaw};
        // Center with both streams enabled must remain one stream.
        style.sideObjectsLeft   = sample.left;
        style.sideObjectsRight  = !sample.left || sample.reference == RoadSideReference::Center;
        auto            network = placementNetwork(style);
        RoadBakeOptions options;
        options.includeMarkings = options.includeNavigation = false;
        options.sideObjectSpacing                           = 5;
        options.sideObjectClearance                         = 0;
        options.sideObjectOffset                            = 123;  // Explicit placement origin owns lateral position.
        options.maximumPlacements                           = 9;
        auto baked                                          = bakeRoadNetwork(network, options);
        REQUIRE(baked.ok());
        int count = 0;
        for (int i = 0; i < baked.value().placements.getCount(); ++i) {
            const auto& points = baked.value().placements;
            if (points.getStringAttribute(i, "road_role", "").find("side.") != 0) continue;
            REQUIRE(std::fabs(points.points()[i].x - sample.expectedX) < 1e-4f);
            REQUIRE(std::fabs(points.points()[i].y - sample.height) < 1e-4f);
            REQUIRE(std::fabs(points.points()[i].z - count * 5.005f) < 1e-4f);
            REQUIRE(std::fabs(points.points()[i].yaw - sample.yaw) < 1e-4f);
            ++count;
        }
        REQUIRE_EQ(count, 7);
        options.maximumPlacements = 8;
        REQUIRE(!bakeRoadNetwork(network, options).ok());
        Params params;
        params.setBool("navigation", false);
        params.setBool("markings", false);
        params.setFloat("sideObjectSpacing", 5);
        params.setFloat("sideObjectClearance", 0);
        REQUIRE(setRoadRecipeNetwork(params, network).ok());
        auto recipe = bakeRoadNetworkRecipe(params);
        REQUIRE(recipe.ok());
        REQUIRE_EQ(recipe.value().placements.getCount(), 9);
        for (int i = 0; i < 9; ++i) {
            REQUIRE(std::fabs(recipe.value().placements.points()[i].x - baked.value().placements.points()[i].x) <
                    1e-6f);
            REQUIRE(std::fabs(recipe.value().placements.points()[i].y - baked.value().placements.points()[i].y) <
                    1e-6f);
            REQUIRE(std::fabs(recipe.value().placements.points()[i].yaw - baked.value().placements.points()[i].yaw) <
                    1e-6f);
        }
    }
}
}  // namespace test_procgen_road_side_placement_unit
