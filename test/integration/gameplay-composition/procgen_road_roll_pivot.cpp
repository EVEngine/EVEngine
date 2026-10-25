#include <cmath>
#include <limits>
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.rollPivotRecipeSnapshotReverseSplitAndUndo") {
    auto source = RoadNetwork::makeStraight(36.f, 2);
    REQUIRE(source.ok());
    auto network    = std::move(source).takeValue();
    auto style      = network.edges()[0].style;
    style.laneWidth = 3.f;
    REQUIRE(network.setEdgeStyle(1, style).ok());
    auto points = network.edges()[0].controlPoints;
    for (auto& point : points) {
        point.rollDegrees      = -10.f;
        point.rollCenterOffset = -.5f;
    }
    REQUIRE(network.setEdgeControlPoints(1, points).ok());
    auto captured = captureRoadNetwork(network);
    REQUIRE(captured.ok());
    Params params;
    REQUIRE(setRoadRecipeNetwork(params, network).ok());
    auto baked = bakeRoadNetworkRecipe(params);
    REQUIRE(baked.ok());
    bool heldRail = false;
    for (int i = 0; i < baked.value().mesh.getVertexCount(); ++i)
        heldRail = heldRail || (std::fabs(baked.value().mesh.getPositionZ(i) + 3.f) < 1e-4f &&
                                std::fabs(baked.value().mesh.getPositionY(i)) < 1e-4f);
    REQUIRE(heldRail);
    auto restored = restoreRoadNetwork(captured.value());
    REQUIRE(restored.ok());
    REQUIRE(captureRoadNetwork(restored.value()).value() == captured.value());
    REQUIRE(network.reverseEdge(1).ok());
    REQUIRE(network.edges()[0].controlPoints[0].rollCenterOffset == .5f);
    REQUIRE(network.reverseEdge(1).ok());
    REQUIRE(captureRoadNetwork(network).value() == captured.value());
    auto invalid                = points;
    invalid[0].rollCenterOffset = .51f;
    REQUIRE(!network.setEdgeControlPoints(1, invalid).ok());
    REQUIRE(captureRoadNetwork(network).value() == captured.value());
    auto  legacy   = captured.value();
    auto& profiles = legacy.getIf<eve::Value::Object>()
                         ->at("edges")
                         .getIf<eve::Value::Array>()
                         ->at(0)
                         .getIf<eve::Value::Object>()
                         ->at("pointProfiles");
    for (auto& profile : *profiles.getIf<eve::Value::Array>())
        profile.getIf<eve::Value::Object>()->erase("rollCenterOffset");
    auto old = restoreRoadNetwork(legacy);
    REQUIRE(old.ok());
    REQUIRE(old.value().edges()[0].controlPoints[0].rollCenterOffset == 0.f);
    using namespace eve::procgen_editing;
    RoadPresetDraft draft;
    draft.source                                  = network;
    draft.types                                   = {{"bank", "", {}}};
    draft.bindings                                = {{1, "bank", {}}};
    draft.types[0].pointDefaults.rollCenterOffset = .3f;
    auto doc                                      = RoadPresetDocument::create(draft);
    REQUIRE(doc.ok());
    auto rejected                                    = doc.value()->beginEdit();
    rejected.types[0].pointDefaults.rollCenterOffset = .6f;
    REQUIRE(!doc.value()->prepare(std::move(rejected)).ok());
    REQUIRE(doc.value()->network().edges()[0].controlPoints[0].rollCenterOffset == .3f);
    auto edit                                    = doc.value()->beginEdit();
    edit.types[0].pointDefaults.rollCenterOffset = -.3f;
    auto prepared                                = doc.value()->prepare(std::move(edit));
    REQUIRE(prepared.ok());
    REQUIRE(doc.value()->commit(std::move(prepared).takeValue()).ok());
    REQUIRE(doc.value()->undo().ok());
    REQUIRE(doc.value()->network().edges()[0].controlPoints[0].rollCenterOffset == .3f);
    auto saved = doc.value()->capture();
    REQUIRE(saved.ok());
    auto token = doc.value()->prepareRestore(saved.value());
    REQUIRE(token.ok());
    REQUIRE(doc.value()->commit(std::move(token).takeValue()).ok());
    REQUIRE(doc.value()->redo().ok());
    REQUIRE(doc.value()->network().edges()[0].controlPoints[0].rollCenterOffset == -.3f);
    points[0].rollCenterOffset     = -.3f;
    points[1].rollCenterOffset     = 0.f;
    points.back().rollCenterOffset = .3f;
    REQUIRE(network.setEdgeControlPoints(1, points).ok());
    auto split = network.splitEdgeAtSplineParameter(1, .125f);
    REQUIRE(split.ok());
    REQUIRE(std::fabs(network.edges()[0].controlPoints.back().rollCenterOffset + .253125f) < 1e-5f);
}
