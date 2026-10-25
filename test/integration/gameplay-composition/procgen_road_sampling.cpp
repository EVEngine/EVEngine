#include <cmath>
#include <limits>
#include <set>
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;

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
