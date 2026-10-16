#include "editing/EditingAuthority.h"
#include "procgen/editing/RoadNetworkEditTarget.h"
#include "procgen/road/RoadBake.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadTerrainWorkspace.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

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

TEST_CASE("procgen.road.authoring.referenceWorldUpFramePreservesHorizontalSectionsAndMigrates") {
    using namespace eve::procgen;
    SplinePath transport, worldUp;
    for (const auto& p : std::vector<SplinePoint>{{-18, 0, 0}, {0, 3, 7}, {18, 6, 0}}) {
        REQUIRE(transport.addPointResult(p).ok());
        auto point         = p;
        point.worldUpFrame = true;
        REQUIRE(worldUp.addPointResult(point).ok());
    }
    auto oldFrames = transport.sampleFramesResult(96, true), frames = worldUp.sampleFramesResult(96, true);
    REQUIRE(oldFrames.ok());
    REQUIRE(frames.ok());
    float transportedTilt = 0.f;
    for (std::size_t i = 0; i < frames.value().size(); ++i) {
        const auto& frame = frames.value()[i];
        REQUIRE(std::fabs(frame.sideY) < 1e-6f);
        REQUIRE(std::fabs(frame.sideX * frame.forwardX + frame.sideZ * frame.forwardZ) < 1e-6f);
        transportedTilt = std::max(transportedTilt, std::fabs(oldFrames.value()[i].sideY));
    }
    REQUIRE(transportedTilt > .05f);
    auto network = RoadNetwork::makeStraight(36.f, 2);
    REQUIRE(network.ok());
    eve::procgen_editing::RoadNetworkEditTarget target("road:frame", network.value());
    const auto                                  saved = target.snapshotValue();
    RoadNetwork                                 restored;
    eve::procgen_editing::RoadNetworkEditTarget copy("road:frame", restored);
    REQUIRE(copy.loadSnapshot(saved).ok());
    REQUIRE(restored.edges()[0].style.frameOrientation == RoadFrameOrientation::WorldUp);
    auto  legacy = saved;
    auto& style  = *legacy.getIf<eve::editing::Value::Object>()
                       ->at("edges")
                       .getIf<eve::editing::Value::Array>()
                       ->at(0)
                       .getIf<eve::editing::Value::Object>()
                       ->at("style")
                       .getIf<eve::editing::Value::Object>();
    style.erase("frameOrientation");
    REQUIRE(copy.loadSnapshot(legacy).ok());
    REQUIRE(restored.edges()[0].style.frameOrientation == RoadFrameOrientation::ParallelTransport);
    const auto migrated = copy.snapshotValue();
    style.emplace("frameOrientation", std::int64_t{8});
    REQUIRE(!copy.loadSnapshot(legacy).ok());
    REQUIRE(copy.snapshotValue() == migrated);
    auto changed             = network.value().edges()[0].style;
    changed.frameOrientation = RoadFrameOrientation::ParallelTransport;
    auto operation           = target.makeSetEdgeStyle(network.value().edges()[0].id, changed);
    REQUIRE(operation.ok());
    eve::editing::LocalWorldAuthority authority(&target);
    eve::editing::TransactionSpec     transaction;
    transaction.id           = eve::editing::TransactionId("road.frame.edit");
    transaction.target       = target.targetId();
    transaction.baseRevision = target.revision();
    auto plan = authority.preflight(transaction, std::span<const eve::editing::DomainOperation>(&operation.value(), 1));
    REQUIRE(plan.ok());
    auto receipt = authority.commit(plan.value());
    REQUIRE(receipt.ok());
    REQUIRE(network.value().edges()[0].style.frameOrientation == RoadFrameOrientation::ParallelTransport);
    REQUIRE(authority.compensate(receipt.value()).ok());
    REQUIRE(target.snapshotValue() == saved);
}
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

TEST_CASE("procgen.road.authoring.referenceCubicWidthIsExplicitAndLegacySnapshotsRetainLinear") {
    using namespace eve::procgen;
    SplinePath linear, cubic;
    for (int i = 0; i < 3; ++i) {
        SplinePoint point;
        point.x      = static_cast<float>(i) * 18.f - 18.f;
        point.scaleX = i == 0 ? .5f : i == 1 ? 1.5f : 1.f;
        REQUIRE(linear.addPointResult(point).ok());
        point.scaleXInterpolation = SplineScaleInterpolation::CatmullRom;
        REQUIRE(cubic.addPointResult(point).ok());
    }
    auto l = linear.evaluateResult(.25f), c = cubic.evaluateResult(.25f);
    REQUIRE(l.ok());
    REQUIRE(c.ok());
    REQUIRE(std::fabs(l.value().scaleX - 1.f) < 1e-6f);
    REQUIRE(std::fabs(c.value().scaleX - 1.03125f) < 1e-6f);
    REQUIRE(l.value().x == c.value().x);
    SplinePoint invalid;
    invalid.scaleXInterpolation = static_cast<SplineScaleInterpolation>(2);
    REQUIRE(!cubic.addPointResult(invalid).ok());
    REQUIRE(cubic.pointCount() == 3);
    auto network = RoadNetwork::makeStraight(36.f, 2);
    REQUIRE(network.ok());
    eve::procgen_editing::RoadNetworkEditTarget target("road:reference-width", network.value());
    const auto                                  saved = target.snapshotValue();
    RoadNetwork                                 restored;
    eve::procgen_editing::RoadNetworkEditTarget copy("road:reference-width", restored);
    REQUIRE(copy.loadSnapshot(saved).ok());
    REQUIRE(restored.edges()[0].style.widthInterpolation == RoadWidthInterpolation::CatmullRom);
    auto  legacy = saved;
    auto& edges  = *legacy.getIf<eve::editing::Value::Object>()->at("edges").getIf<eve::editing::Value::Array>();
    for (auto& edge : edges)
        edge.getIf<eve::editing::Value::Object>()->at("style").getIf<eve::editing::Value::Object>()->erase(
            "widthInterpolation");
    REQUIRE(copy.loadSnapshot(legacy).ok());
    REQUIRE(restored.edges()[0].style.widthInterpolation == RoadWidthInterpolation::Linear);
    const auto migrated = copy.snapshotValue();
    edges[0].getIf<eve::editing::Value::Object>()->at("style").getIf<eve::editing::Value::Object>()->emplace(
        "widthInterpolation", std::int64_t{9});
    REQUIRE(!copy.loadSnapshot(legacy).ok());
    REQUIRE(copy.snapshotValue() == migrated);
    auto points          = network.value().edges()[0].controlPoints;
    points[0].widthScale = .5f;
    points[1].widthScale = 1.5f;
    points[2].widthScale = 1.f;
    REQUIRE(network.value().setEdgeControlPoints(network.value().edges()[0].id, points).ok());
    REQUIRE(network.value().splitEdgeAtSplineParameter(network.value().edges()[0].id, .25f, 1.f).ok());
    REQUIRE(std::fabs(network.value().edges()[0].controlPoints.back().widthScale - 1.03125f) < 1e-6f);
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

TEST_CASE("procgen.road.network.nodeMoveRejectsDegenerateIncidentEdgeAtomically") {
    RoadNetwork network;
    auto        from = network.addNode(-10.f, 0.f, 0.f);
    auto        to   = network.addNode(10.f, 0.f, 0.f);
    REQUIRE(from.ok());
    REQUIRE(to.ok());
    auto edge = network.addEdge(from.value(), to.value(), {{}, {}}, 1, 0);
    REQUIRE(edge.ok());
    REQUIRE_EQ(network.edges().front().controlPoints.size(), 2u);
    auto before = captureRoadNetwork(network);
    REQUIRE(before.ok());
    const auto revision = network.revision();
    auto       moved    = network.setNodePosition(from.value(), 10.f, 0.f, 0.f);
    REQUIRE(!moved.ok());
    REQUIRE_EQ(network.revision(), revision);
    auto after = captureRoadNetwork(network);
    REQUIRE(after.ok());
    REQUIRE(after.value() == before.value());
    eve::procgen_editing::RoadNetworkEditTarget target("road:collapse", network);
    auto                                        planned = target.makeMoveNode(from.value(), 10.f, 0.f, 0.f);
    REQUIRE(!planned.ok());
    REQUIRE_EQ(network.revision(), revision);
    auto afterPlan = captureRoadNetwork(network);
    REQUIRE(afterPlan.ok());
    REQUIRE(afterPlan.value() == before.value());
    REQUIRE(network.validate().ok());
}

TEST_CASE("procgen.road.network.coincidentEndpointPositionsDoNotMergeNodeIdentity") {
    RoadNetwork network;
    auto        from = network.addNode(0.f, 0.f, 0.f);
    auto        to   = network.addNode(20.f, 0.f, 0.f);
    REQUIRE(from.ok());
    REQUIRE(to.ok());
    auto edge = network.addEdge(from.value(), to.value(),
                                {{}, {10.f, 0.f, 0.f}, {10.f, 0.f, 10.f}, {0.f, 0.f, 10.f}, {}}, 1, 0);
    REQUIRE(edge.ok());
    auto before = captureRoadNetwork(network);
    REQUIRE(before.ok());
    eve::procgen_editing::RoadNetworkEditTarget target("road:coincident-loop", network);
    auto                                        planned = target.makeMoveNode(to.value(), 0.f, 0.f, 0.f);
    REQUIRE(planned.ok());
    REQUIRE(target.applyDomainOperation(planned.value()).ok());
    REQUIRE_EQ(network.nodeCount(), 2);
    REQUIRE_EQ(network.edges().front().from, from.value());
    REQUIRE_EQ(network.edges().front().to, to.value());
    REQUIRE(network.validate().ok());
    auto saved = captureRoadNetwork(network);
    REQUIRE(saved.ok());
    auto restored = restoreRoadNetwork(saved.value());
    REQUIRE(restored.ok());
    REQUIRE_EQ(restored.value().nodeCount(), 2);
    REQUIRE(restored.value().validate().ok());
    eve::editing::DomainOperation undo = planned.value();
    undo.type                          = undo.inverseType;
    undo.payload                       = undo.inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    auto undone = captureRoadNetwork(network);
    REQUIRE(undone.ok());
    REQUIRE(undone.value() == before.value());
    REQUIRE(target.applyDomainOperation(planned.value()).ok());
    auto redone = captureRoadNetwork(network);
    REQUIRE(redone.ok());
    REQUIRE(redone.value() == saved.value());
}

namespace {
RoadTerrainTile elevatedRoadTile() {
    RoadTerrainTile tile;
    tile.id      = "elevated";
    tile.originX = tile.originZ = -2.f;
    tile.originY                = 100.f;
    tile.sizeX = tile.sizeZ = 4.f;
    tile.heightScale        = 10.f;
    tile.heights            = eve::procgen::Heightmap(5, 5);
    tile.holes              = eve::procgen::Heightmap(5, 5);
    for (auto& sample : tile.heights.data()) sample = .5f;
    tile.splatLayers = {eve::procgen::Heightmap(5, 5), eve::procgen::Heightmap(5, 5)};
    for (auto& sample : tile.splatLayers[0].data()) sample = 1.f;
    tile.details.push_back({"grass", 5, 5, std::vector<int>(25u, 4)});
    tile.trees.push_back({"tree", "oak", 0.f, 105.f, 0.f});
    return tile;
}
MeshBuild elevatedRoadMesh(float worldY = 106.f) {
    MeshBuild mesh;
    mesh.setActiveGroup("asphalt");
    for (const auto p : {RoadControlPoint{-1.f, 106.f, -1.f}, RoadControlPoint{1.f, 106.f, -1.f},
                         RoadControlPoint{1.f, 106.f, 1.f}, RoadControlPoint{-1.f, 106.f, 1.f}})
        mesh.addVertex(p.x, worldY, p.z, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addTriangle(0, 1, 2);
    mesh.addTriangle(0, 2, 3);
    return mesh;
}
}  // namespace
TEST_CASE("procgen.road.terrain.elevatedOriginMapsNormalizedHeightsAndAllChannels") {
    RoadTerrainWorkspace workspace;
    REQUIRE(workspace.initialize({elevatedRoadTile()}).ok());
    RoadTerrainWriteOptions options;
    options.blendDistance = 0.f;
    options.splatLayer    = 1;
    auto mesh             = elevatedRoadMesh();
    REQUIRE(workspace.apply(mesh, options).ok());
    auto tile = workspace.copyTile("elevated");
    REQUIRE(tile.ok());
    REQUIRE_EQ(tile.value().originY, 100.f);
    REQUIRE(std::fabs(tile.value().heights.height(2, 2) - .6f) < 1e-6f);
    REQUIRE_EQ(tile.value().heights.height(0, 0), .5f);
    REQUIRE_EQ(tile.value().splatLayers[1].height(2, 2), 1.f);
    REQUIRE_EQ(tile.value().details[0].density[12], 0);
    REQUIRE(tile.value().trees.empty());
    REQUIRE(workspace.undo().ok());
    auto saved = workspace.capture();
    REQUIRE(saved.ok());
    auto json = saved.value().toJson();
    REQUIRE(json.ok());
    auto parsed = eve::Value::fromJson(json.value());
    REQUIRE(parsed.ok());
    RoadTerrainWorkspace restored;
    REQUIRE(restored.restore(parsed.value()).ok());
    REQUIRE(restored.redo().ok());
    auto redone = restored.copyTile("elevated");
    REQUIRE(redone.ok());
    REQUIRE_EQ(redone.value().originY, 100.f);
    REQUIRE(std::fabs(redone.value().heights.height(2, 2) - .6f) < 1e-6f);
    REQUIRE(redone.value().trees.empty());
    REQUIRE(restored.undo().ok());
    auto undone = restored.copyTile("elevated");
    REQUIRE(undone.ok());
    REQUIRE_EQ(undone.value().heights.height(2, 2), .5f);
    REQUIRE_EQ(undone.value().trees.size(), 1u);
    auto  invalid = saved.value();
    auto& history = std::get<eve::Value::Array>(invalid.find("history")->storage());
    std::get<eve::Value::Array>(history.front().storage()).front().set("originY", "invalid");
    auto stable = restored.capture();
    REQUIRE(stable.ok());
    REQUIRE(!restored.restore(invalid).ok());
    auto after = restored.capture();
    REQUIRE(after.ok());
    REQUIRE(after.value() == stable.value());
    auto  mismatched = saved.value();
    auto& frames     = std::get<eve::Value::Array>(mismatched.find("history")->storage());
    std::get<eve::Value::Array>(frames.front().storage()).front().set("originY", 0.f);
    REQUIRE(!restored.restore(mismatched).ok());
    auto afterMismatch = restored.capture();
    REQUIRE(afterMismatch.ok());
    REQUIRE(afterMismatch.value() == stable.value());
    auto invalidTile    = elevatedRoadTile();
    invalidTile.originY = std::numeric_limits<float>::infinity();
    REQUIRE(!restored.initialize({invalidTile}).ok());
    auto afterInitialize = restored.capture();
    REQUIRE(afterInitialize.ok());
    REQUIRE(afterInitialize.value() == stable.value());
}
TEST_CASE("procgen.road.terrain.originYLegacyMigrationAndDirectHeightmapReceipt") {
    auto                      tile = elevatedRoadTile();
    RoadTerrainConformOptions options;
    options.originX = options.originZ = -2.f;
    options.originY                   = 100.f;
    options.heightScale               = 10.f;
    options.blendDistance             = 0.f;
    auto receipt                      = conformHeightmapToRoadWithReceipt(tile.heights, elevatedRoadMesh(), options);
    REQUIRE(receipt.ok());
    REQUIRE(!receipt.value().sampleIndices.empty());
    REQUIRE(std::fabs(tile.heights.height(2, 2) - .6f) < 1e-6f);
    REQUIRE(restoreHeightmapFromRoad(tile.heights, receipt.value()).ok());
    REQUIRE_EQ(tile.heights.height(2, 2), .5f);
    const auto stableSamples = tile.heights.data();
    options.originY          = std::numeric_limits<float>::infinity();
    auto invalid             = conformHeightmapToRoadWithReceipt(tile.heights, elevatedRoadMesh(), options);
    REQUIRE(!invalid.ok());
    REQUIRE(tile.heights.data() == stableSamples);
    tile.originY = 0.f;
    RoadTerrainWorkspace workspace;
    REQUIRE(workspace.initialize({tile}).ok());
    RoadTerrainWriteOptions write;
    write.blendDistance = 0.f;
    write.splatLayer    = 1;
    REQUIRE(workspace.apply(elevatedRoadMesh(6.f), write).ok());
    auto snapshot = workspace.capture();
    REQUIRE(snapshot.ok());
    auto legacy = snapshot.value();
    for (auto& frame : std::get<eve::Value::Array>(legacy.find("history")->storage()))
        for (auto& encoded : std::get<eve::Value::Array>(frame.storage()))
            std::get<eve::Value::Object>(encoded.storage()).erase("originY");
    REQUIRE(!workspace.restore(legacy).ok());  // v2 requires an explicit mapping baseline.
    legacy.set("version", 1);
    REQUIRE(workspace.restore(legacy).ok());
    auto migrated = workspace.copyTile("elevated");
    REQUIRE(migrated.ok());
    REQUIRE_EQ(migrated.value().originY, 0.f);
    REQUIRE(workspace.undo().ok());
    auto previous = workspace.copyTile("elevated");
    REQUIRE(previous.ok());
    REQUIRE_EQ(previous.value().originY, 0.f);
    REQUIRE_EQ(previous.value().heights.height(2, 2), .5f);
    REQUIRE(workspace.redo().ok());
    auto next = workspace.copyTile("elevated");
    REQUIRE(next.ok());
    REQUIRE_EQ(next.value().originY, 0.f);
    REQUIRE(std::fabs(next.value().heights.height(2, 2) - .6f) < 1e-6f);
}
