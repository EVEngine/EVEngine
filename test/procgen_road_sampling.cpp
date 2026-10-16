#include <cmath>
#include <limits>
#include <set>
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;
TEST_CASE("procgen.road.authoring.distanceSamplingStaysContinuousAcrossMarkers") {
    SplinePath path;
    REQUIRE(path.setKindResult("catmullRom").ok());
    for (const auto& p : std::vector<RoadControlPoint>{{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {24, 1, 0}}) {
        SplinePoint point;
        point.x = p.x;
        point.y = p.y;
        point.z = p.z;
        REQUIRE(path.addPointResult(point).ok());
    }
    auto projected = path.closestPointResult(-8, 2, 8, 24);
    REQUIRE(projected.ok());
    const float distance = projected.value().normalizedDistance * path.lengthResult(24).value();
    for (float offset : {-.001f, .001f}) {
        auto sample = path.evaluateDistanceResult(distance + offset, 24);
        REQUIRE(sample.ok());
        REQUIRE(std::hypot(sample.value().x + 8.f, sample.value().y - 2.f, sample.value().z - 8.f) < .004f);
    }
    auto frames = path.sampleFramesResult(7, true, 0.f, 24, true);
    REQUIRE(frames.ok());
    std::set<float> markers;
    for (const auto& frame : frames.value())
        if (frame.authoredMarker) markers.insert(frame.sample.x);
    const std::set<float> expected = {-24, -8, 8, 24};
    REQUIRE(markers == expected);
}
namespace {
RoadNetwork sampledRoad(float length, bool curve = false) {
    RoadNetwork network;
    auto        a = network.addNode(-length * .5f, 0, 0), b = network.addNode(length * .5f, 0, 0);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    RoadStyle style;
    style.laneWidth                      = 3.f;
    style.crossSection                   = {{-3, 0, RoadMaterial::Asphalt}, {3, 0, RoadMaterial::Asphalt}};
    style.automaticStructures            = false;
    style.samplingSpacingMeters          = 2.f;
    style.samplingPolicy                 = RoadSamplingPolicy::MaximumIntervals;
    style.samplingAngleDegrees           = 4.f;
    std::vector<RoadControlPoint> points = {{-length * .5f, 0, 0}, {0, 0, curve ? 12.f : 0.f}, {length * .5f, 0, 0}};
    REQUIRE(network.addEdge(a.value(), b.value(), points, 2, 0, style).ok());
    return network;
}
RoadBakeOptions isolated() {
    RoadBakeOptions options;
    options.includePiers      = false;
    options.includeJunctions  = false;
    options.includeNavigation = false;
    options.includePlacements = false;
    options.includeMarkings   = false;
    return options;
}
}  // namespace
TEST_CASE("procgen.road.authoring.metricSamplingUsesMetresAnglesAndSectionSubdivisions") {
    auto network = sampledRoad(40);
    auto options = isolated();
    auto first   = bakeRoadNetwork(network, options);
    REQUIRE(first.ok());
    std::set<float> rings;
    for (int i = 0; i < first.value().mesh.getVertexCount(); ++i) rings.insert(first.value().mesh.getPositionX(i));
    REQUIRE(*rings.begin() == -20.f);
    REQUIRE(*rings.rbegin() == 20.f);
    float previous = *rings.begin();
    for (float x : rings) {
        REQUIRE(x - previous <= 2.03f);
        previous = x;
    }
    auto doubled = sampledRoad(80);
    auto longer  = bakeRoadNetwork(doubled, options);
    REQUIRE(longer.ok());
    REQUIRE(longer.value().mesh.getVertexCount() > first.value().mesh.getVertexCount() * 1.8f);
    options.pathSegmentsPerEdge = 96;
    REQUIRE(bakeRoadNetwork(network, options).value().mesh.positions() == first.value().mesh.positions());
    options.samplingScale = 2.f;
    auto coarse           = bakeRoadNetwork(network, options);
    REQUIRE(coarse.ok());
    REQUIRE(coarse.value().mesh.getVertexCount() < first.value().mesh.getVertexCount());
    auto style                     = network.edges()[0].style;
    style.crossSectionSubdivisions = 3;
    REQUIRE(network.setEdgeStyle(1, style).ok());
    options         = isolated();
    auto transverse = bakeRoadNetwork(network, options);
    REQUIRE(transverse.ok());
    REQUIRE(transverse.value().mesh.getVertexCount() == first.value().mesh.getVertexCount() * 3);
    REQUIRE(transverse.value().mesh.getIndexCount() == first.value().mesh.getIndexCount() * 3);
    auto curved                 = sampledRoad(48, true);
    style                       = curved.edges()[0].style;
    style.samplingSpacingMeters = 10.f;
    style.samplingAngleDegrees  = 20.f;
    REQUIRE(curved.setEdgeStyle(1, style).ok());
    auto low = bakeRoadNetwork(curved, options);
    REQUIRE(low.ok());
    style.samplingAngleDegrees = 1.f;
    REQUIRE(curved.setEdgeStyle(1, style).ok());
    auto high = bakeRoadNetwork(curved, options);
    REQUIRE(high.ok());
    REQUIRE(high.value().mesh.getVertexCount() > low.value().mesh.getVertexCount());
    Params params;
    REQUIRE(setRoadRecipeNetwork(params, curved).ok());
    params.setBool("piers", false);
    params.setBool("junctions", false);
    params.setBool("navigation", false);
    params.setBool("placements", false);
    params.setBool("markings", false);
    REQUIRE(bakeRoadNetworkRecipe(params).value().mesh.positions() == high.value().mesh.positions());
}
TEST_CASE("procgen.road.authoring.samplingSettingsSaveUndoMigrateAndRejectBudgets") {
    auto network  = sampledRoad(40);
    auto captured = captureRoadNetwork(network);
    REQUIRE(captured.ok());
    REQUIRE(captureRoadNetwork(restoreRoadNetwork(captured.value()).value()).value() == captured.value());
    auto  legacy     = captured.value();
    auto& styleValue = legacy.getIf<eve::Value::Object>()
                           ->at("edges")
                           .getIf<eve::Value::Array>()
                           ->at(0)
                           .getIf<eve::Value::Object>()
                           ->at("style");
    for (const char* field : {"samplingSpacingMeters", "samplingAngleDegrees", "crossSectionSubdivisions"})
        styleValue.getIf<eve::Value::Object>()->erase(field);
    auto old = restoreRoadNetwork(legacy);
    REQUIRE(old.ok());
    REQUIRE(old.value().edges()[0].style.samplingSpacingMeters == 0.f);
    REQUIRE(old.value().edges()[0].style.crossSectionSubdivisions == 1);
    auto invalid                     = network.edges()[0].style;
    invalid.crossSectionSubdivisions = 33;
    REQUIRE(!network.setEdgeStyle(1, invalid).ok());
    REQUIRE(captureRoadNetwork(network).value() == captured.value());
    auto dense                  = network.edges()[0].style;
    dense.samplingSpacingMeters = .0001f;
    REQUIRE(network.setEdgeStyle(1, dense).ok());
    auto before = captureRoadNetwork(network);
    REQUIRE(!bakeRoadNetwork(network, isolated()).ok());
    REQUIRE(captureRoadNetwork(network).value() == before.value());
    REQUIRE(network.setEdgeStyle(1, old.value().edges()[0].style).ok());
    auto options          = isolated();
    options.samplingScale = 0.f;
    REQUIRE(!bakeRoadNetwork(network, options).ok());
    Params malformed;
    REQUIRE(setRoadRecipeNetwork(malformed, network).ok());
    malformed.setString("samplingScale", "bad");
    REQUIRE(!bakeRoadNetworkRecipe(malformed).ok());
    malformed.setFloat("samplingScale", std::numeric_limits<float>::quiet_NaN());
    REQUIRE(!bakeRoadNetworkRecipe(malformed).ok());
    options                     = isolated();
    options.maximumMeshElements = 20;
    REQUIRE(!bakeRoadNetwork(network, options).ok());
    using namespace eve::procgen_editing;
    RoadPresetDraft draft;
    draft.source                                      = network;
    draft.types                                       = {{"metric", "", {}}};
    draft.bindings                                    = {{1, "metric", {}}};
    draft.types[0].overrides.samplingSpacingMeters    = 1.f;
    draft.types[0].overrides.crossSectionSubdivisions = 3;
    auto doc                                          = RoadPresetDocument::create(draft);
    REQUIRE(doc.ok());
    auto edit                                     = doc.value()->beginEdit();
    edit.types[0].overrides.samplingSpacingMeters = 4.f;
    auto token                                    = doc.value()->prepare(std::move(edit));
    REQUIRE(token.ok());
    REQUIRE(doc.value()->commit(std::move(token).takeValue()).ok());
    REQUIRE(doc.value()->undo().ok());
    REQUIRE(doc.value()->network().edges()[0].style.samplingSpacingMeters == 1.f);
    auto saved = doc.value()->capture();
    REQUIRE(saved.ok());
    auto restore = doc.value()->prepareRestore(saved.value());
    REQUIRE(restore.ok());
    REQUIRE(doc.value()->commit(std::move(restore).takeValue()).ok());
    REQUIRE(doc.value()->redo().ok());
    REQUIRE(doc.value()->network().edges()[0].style.samplingSpacingMeters == 4.f);
    REQUIRE(doc.value()->network().edges()[0].style.crossSectionSubdivisions == 3);
}
