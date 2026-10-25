#include <array>
#include <cmath>
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;
namespace {
RoadStyle scanStyle() {
    RoadStyle style;
    style.laneWidth             = 3.f;
    style.crossSection          = {{-3, 0, RoadMaterial::Asphalt}, {3, 0, RoadMaterial::Asphalt}};
    style.automaticStructures   = false;
    style.samplingSpacingMeters = 10.f;
    style.samplingAngleDegrees  = 20.f;
    style.samplingPolicy        = RoadSamplingPolicy::ParametricDistanceTurn;
    return style;
}

RoadNetwork referenceScanRoad() {
    RoadNetwork network;
    auto        a = network.addNode(-24, 0, 0), b = network.addNode(24, 1, 0);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(network.addEdge(a.value(), b.value(), {{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {24, 1, 0}}, 2, 0, scanStyle())
                .ok());
    return network;
}
}  // namespace

TEST_CASE("procgen.road.authoring.parameterScanPolicySaveUndoAndLegacyMigration") {
    auto network  = referenceScanRoad();
    auto captured = captureRoadNetwork(network);
    REQUIRE(captured.ok());
    REQUIRE(captureRoadNetwork(restoreRoadNetwork(captured.value()).value()).value() == captured.value());
    auto  legacy = captured.value();
    auto& style  = legacy.getIf<eve::Value::Object>()
                      ->at("edges")
                      .getIf<eve::Value::Array>()
                      ->at(0)
                      .getIf<eve::Value::Object>()
                      ->at("style");
    style.getIf<eve::Value::Object>()->erase("samplingPolicy");
    REQUIRE(restoreRoadNetwork(legacy).value().edges()[0].style.samplingPolicy == RoadSamplingPolicy::MaximumIntervals);
    (*style.getIf<eve::Value::Object>())["samplingPolicy"] = 7;
    REQUIRE(!restoreRoadNetwork(legacy).ok());
    auto invalid                 = network.edges()[0].style;
    invalid.samplingAngleDegrees = 0.f;
    REQUIRE(!network.setEdgeStyle(1, invalid).ok());
    REQUIRE(captureRoadNetwork(network).value() == captured.value());
    using namespace eve::procgen_editing;
    RoadPresetDraft draft;
    draft.source                                   = network;
    draft.types                                    = {{"scan", "", {}}};
    draft.bindings                                 = {{1, "scan", {}}};
    draft.types[0].overrides.samplingPolicy        = RoadSamplingPolicy::ParametricDistanceTurn;
    draft.types[0].overrides.samplingSpacingMeters = 10.f;
    draft.types[0].overrides.samplingAngleDegrees  = 20.f;
    auto doc                                       = RoadPresetDocument::create(draft);
    REQUIRE(doc.ok());
    auto edit                              = doc.value()->beginEdit();
    edit.types[0].overrides.samplingPolicy = RoadSamplingPolicy::MaximumIntervals;
    auto token                             = doc.value()->prepare(std::move(edit));
    REQUIRE(token.ok());
    REQUIRE(doc.value()->commit(std::move(token).takeValue()).ok());
    REQUIRE(doc.value()->undo().ok());
    auto saved = doc.value()->capture();
    REQUIRE(saved.ok());
    auto restore = doc.value()->prepareRestore(saved.value());
    REQUIRE(restore.ok());
    REQUIRE(doc.value()->commit(std::move(restore).takeValue()).ok());
    REQUIRE(doc.value()->network().edges()[0].style.samplingPolicy == RoadSamplingPolicy::ParametricDistanceTurn);
    REQUIRE(doc.value()->redo().ok());
    REQUIRE(doc.value()->network().edges()[0].style.samplingPolicy == RoadSamplingPolicy::MaximumIntervals);
}
