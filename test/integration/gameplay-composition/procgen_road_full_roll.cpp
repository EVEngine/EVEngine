// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "modules": [
    "procgen",
    "procgen_editing"
  ],
  "origins": [
    "procgen_road_full_roll.cpp"
  ],
  "module": "procgen"
}
*/
// clang-format on

#include <cmath>
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.twistedProfileUsesMeasuredReferencePhysicalDiagonal") {
    auto created = RoadNetwork::makeStraight(36.f, 2);
    REQUIRE(created.ok());
    auto network = std::move(created).takeValue();
    REQUIRE(network.setEdgeControlPoints(1, {{-18, 0, 0}, {0, 0, 0, 1, 270}, {18, 0, 0}}).ok());
    auto style      = network.edges()[0].style;
    style.laneWidth = 3.f;
    style.curbWidth = style.curbHeight = style.sidewalkWidth = style.sidewalkHeight = 0.f;
    style.deckThickness                                                             = 0.f;
    style.samplingPolicy        = RoadSamplingPolicy::ParametricDistanceTurn;
    style.samplingSpacingMeters = .375f;
    style.samplingAngleDegrees  = 4.f;
    style.uvMeters              = 2.f;
    REQUIRE(network.setEdgeStyle(1, style).ok());
    RoadBakeOptions options;
    options.includeMarkings = options.includePiers = options.includeNavigation = options.includePlacements = false;
    auto baked = bakeRoadNetwork(network, options);
    REQUIRE(baked.ok());
    std::unique_ptr<MeshBuild> surface;
    for (int group = 0; group < baked.value().mesh.getGroupCount(); ++group)
        if (baked.value().mesh.getGroupName(group) == "asphalt") surface = baked.value().mesh.copyGroup(group);
    REQUIRE(surface != nullptr);
    const auto& mesh = *surface;
    REQUIRE(mesh.getIndexCount() == 162 * 3);
    // Reflected reference indices [0,3,1,2,3,0] connect previous -Z to next +Z.
    std::vector<int> shared;
    for (int i = 0; i < 3; ++i)
        for (int j = 3; j < 6; ++j)
            if (mesh.getIndex(i) == mesh.getIndex(j)) shared.push_back(mesh.getIndex(i));
    REQUIRE(shared.size() == 2u);
    bool previous = false, next = false;
    for (int index : shared) {
        if (std::fabs(mesh.getPositionX(index) + 18.f) < 1e-5f) {
            previous = true;
            REQUIRE(std::fabs(mesh.getPositionZ(index) + 3.f) < 1e-5f);
            REQUIRE(mesh.getUvU(index) == 0.f);
            REQUIRE(mesh.getUvV(index) == 1.5f);
        } else {
            next = true;
            REQUIRE(std::fabs(mesh.getPositionX(index) + 17.565235f) < 1e-4f);
            REQUIRE(std::fabs(mesh.getPositionZ(index) - 2.9988985f) < 1e-4f);
            REQUIRE(mesh.getUvU(index) > 0.f);
            REQUIRE(mesh.getUvV(index) == -1.5f);
        }
    }
    REQUIRE(previous);
    REQUIRE(next);
}

TEST_CASE("procgen.road.authoring.fullRollPreservesReferenceRotationAndAtomicHistory") {
    auto created = RoadNetwork::makeStraight(36.f, 2);
    REQUIRE(created.ok());
    auto             network = std::move(created).takeValue();
    auto             points  = network.edges()[0].controlPoints;
    RoadControlPoint middle;
    middle.x           = (points.front().x + points.back().x) * .5f;
    middle.rollDegrees = 270.f;
    points             = {points.front(), middle, points.back()};
    REQUIRE(network.setEdgeControlPoints(1, points).ok());
    auto captured = captureRoadNetwork(network);
    REQUIRE(captured.ok());
    auto restored = restoreRoadNetwork(captured.value());
    REQUIRE(restored.ok());
    REQUIRE(captureRoadNetwork(restored.value()).value() == captured.value());
    auto split = restored.value().splitEdgeAtSplineParameter(1, .125f);
    REQUIRE(split.ok());
    REQUIRE(std::fabs(restored.value().edges()[0].controlPoints.back().rollDegrees - 42.1875f) < 1e-5f);
    REQUIRE(network.reverseEdge(1).ok());
    REQUIRE(network.edges()[0].controlPoints[1].rollDegrees == -270.f);
    REQUIRE(network.reverseEdge(1).ok());
    REQUIRE(captureRoadNetwork(network).value() == captured.value());
    auto invalid           = points;
    invalid[1].rollDegrees = 360.01f;
    REQUIRE(!network.setEdgeControlPoints(1, invalid).ok());
    REQUIRE(captureRoadNetwork(network).value() == captured.value());
    Params params;
    REQUIRE(setRoadRecipeNetwork(params, network).ok());
    auto baked = bakeRoadNetworkRecipe(params);
    REQUIRE(baked.ok());
    REQUIRE(baked.value().mesh.getVertexCount() > 0);

    SplinePath path;
    REQUIRE(path.setKindResult("catmullRom").ok());
    for (int i = 0; i < 3; ++i) {
        SplinePoint point;
        point.x                 = -18.f + 18.f * static_cast<float>(i);
        point.rollDegrees       = i == 1 ? 270.f : 0.f;
        point.rollInterpolation = SplineRollInterpolation::SmoothStep;
        REQUIRE(path.addPointResult(point).ok());
    }
    auto sample = path.evaluateResult(.25f);
    REQUIRE(sample.ok());
    REQUIRE(std::fabs(sample.value().rollDegrees - 135.f) < 1e-5f);
    // Actual EasyRoads varying-270 probe row 20: paired offsets remove its small height perturbation.
    auto referenceSample = path.evaluateResult(.5555557f * .5f);
    REQUIRE(referenceSample.ok());
    const float angle = referenceSample.value().rollDegrees * .01745329251994329577f;
    REQUIRE(std::fabs(3.f * std::sin(angle) - 1.152525663f) < 1e-4f);
    REQUIRE(std::fabs(-3.f * std::cos(angle) - 2.769780874f) < 1e-4f);

    using namespace eve::procgen_editing;
    RoadPresetDraft draft;
    draft.source                             = network;
    draft.types                              = {{"full-roll", "", {}}};
    draft.bindings                           = {{1, "full-roll", {}}};
    draft.types[0].pointDefaults.rollDegrees = 360.f;
    auto doc                                 = RoadPresetDocument::create(draft);
    REQUIRE(doc.ok());
    auto edit                               = doc.value()->beginEdit();
    edit.types[0].pointDefaults.rollDegrees = -360.f;
    auto prepared                           = doc.value()->prepare(std::move(edit));
    REQUIRE(prepared.ok());
    REQUIRE(doc.value()->commit(std::move(prepared).takeValue()).ok());
    REQUIRE(doc.value()->undo().ok());
    REQUIRE(doc.value()->network().edges()[0].controlPoints[1].rollDegrees == 360.f);
    auto saved = doc.value()->capture();
    REQUIRE(saved.ok());
    auto restore = doc.value()->prepareRestore(saved.value());
    REQUIRE(restore.ok());
    REQUIRE(doc.value()->commit(std::move(restore).takeValue()).ok());
    REQUIRE(doc.value()->redo().ok());
    REQUIRE(doc.value()->network().edges()[0].controlPoints[1].rollDegrees == -360.f);
}
