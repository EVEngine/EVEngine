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
    "procgen_road_sampling_defaults.cpp"
  ],
  "module": "procgen"
}
*/
// clang-format on

#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.newSamplingDefaultsMatchMeasuredReferenceAndLegacyNetworkRetainsCounts") {
    auto created = RoadNetwork::makeStraight(40.f, 2);
    REQUIRE(created.ok());
    auto        network = std::move(created).takeValue();
    const auto& style   = network.edges()[0].style;
    // Actual CreateRoad readbacks before any setter: resolution 2, angle 45, subSegments 1.
    REQUIRE(style.samplingSpacingMeters == 2.f);
    REQUIRE(style.samplingAngleDegrees == 45.f);
    REQUIRE(style.crossSectionSubdivisions == 1);
    REQUIRE(style.samplingPolicy == RoadSamplingPolicy::ParametricDistanceTurn);
    RoadBakeOptions coarse, dense;
    coarse.pathSegmentsPerEdge = 4;
    dense.pathSegmentsPerEdge  = 400;
    auto first = bakeRoadNetwork(network, coarse), second = bakeRoadNetwork(network, dense);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(first.value().mesh.positions() == second.value().mesh.positions());
    REQUIRE(first.value().mesh.indices() == second.value().mesh.indices());
    Params params;
    REQUIRE(setRoadRecipeNetwork(params, network).ok());
    params.setInt("pathSegments", 4);
    auto recipe = bakeRoadNetworkRecipe(params);
    REQUIRE(recipe.ok());
    REQUIRE(recipe.value().mesh.positions() == first.value().mesh.positions());
    auto captured = captureRoadNetwork(network);
    REQUIRE(captured.ok());
    auto& value = captured.value()
                      .getIf<eve::Value::Object>()
                      ->at("edges")
                      .getIf<eve::Value::Array>()
                      ->at(0)
                      .getIf<eve::Value::Object>()
                      ->at("style");
    for (const char* key :
         {"samplingSpacingMeters", "samplingAngleDegrees", "samplingPolicy", "crossSectionSubdivisions"})
        value.getIf<eve::Value::Object>()->erase(key);
    auto restored = restoreRoadNetwork(captured.value());
    REQUIRE(restored.ok());
    REQUIRE(restored.value().edges()[0].style.samplingSpacingMeters == 0.f);
    REQUIRE(restored.value().edges()[0].style.samplingAngleDegrees == 4.f);
    REQUIRE(restored.value().edges()[0].style.samplingPolicy == RoadSamplingPolicy::MaximumIntervals);
    auto oldCoarse = bakeRoadNetwork(restored.value(), coarse), oldDense = bakeRoadNetwork(restored.value(), dense);
    REQUIRE(oldCoarse.ok());
    REQUIRE(oldDense.ok());
    REQUIRE(oldCoarse.value().mesh.getVertexCount() < oldDense.value().mesh.getVertexCount());
}

TEST_CASE("procgen.road.authoring.presetV2MigratesEveryHistoryRootWithoutChangingChildInheritance") {
    using namespace eve::procgen_editing;
    RoadPresetDraft draft;
    auto            created = RoadNetwork::makeStraight(40.f, 2);
    REQUIRE(created.ok());
    draft.source   = std::move(created).takeValue();
    draft.types    = {{"root", "", {}}, {"child", "root", {}}};
    draft.bindings = {{1, "child", {}}};
    auto doc       = RoadPresetDocument::create(draft);
    REQUIRE(doc.ok());
    REQUIRE(doc.value()->network().edges()[0].style.samplingSpacingMeters == 2.f);
    auto edit = doc.value()->beginEdit();
    for (auto& type : edit.types)
        if (type.id == "root") type.overrides.samplingSpacingMeters = 4.f;
    auto prepared = doc.value()->prepare(std::move(edit));
    REQUIRE(prepared.ok());
    REQUIRE(doc.value()->commit(std::move(prepared).takeValue()).ok());
    REQUIRE(doc.value()->undo().ok());
    auto captured = doc.value()->capture();
    REQUIRE(captured.ok());
    REQUIRE(*captured.value().getIf<eve::Value::Object>()->at("schemaVersion").getIf<std::int64_t>() == 3);
    captured.value().getIf<eve::Value::Object>()->at("schemaVersion") = 2;
    auto token                                                        = doc.value()->prepareRestore(captured.value());
    REQUIRE(token.ok());
    REQUIRE(doc.value()->commit(std::move(token).takeValue()).ok());
    REQUIRE(doc.value()->network().edges()[0].style.samplingSpacingMeters == 0.f);
    REQUIRE(doc.value()->network().edges()[0].style.samplingPolicy == RoadSamplingPolicy::MaximumIntervals);
    REQUIRE(doc.value()->network().edges()[0].style.samplingAngleDegrees == 4.f);
    for (const auto& type : doc.value()->beginEdit().types)
        if (type.id == "child") REQUIRE(!type.overrides.samplingSpacingMeters.has_value());
    REQUIRE(doc.value()->redo().ok());
    REQUIRE(doc.value()->network().edges()[0].style.samplingSpacingMeters == 4.f);
    REQUIRE(doc.value()->network().edges()[0].style.samplingPolicy == RoadSamplingPolicy::MaximumIntervals);
    auto saved = doc.value()->capture();
    REQUIRE(saved.ok());
    auto again = doc.value()->prepareRestore(saved.value());
    REQUIRE(again.ok());
    REQUIRE(doc.value()->commit(std::move(again).takeValue()).ok());
    REQUIRE(doc.value()->undo().ok());
    REQUIRE(doc.value()->network().edges()[0].style.samplingSpacingMeters == 0.f);
}

TEST_CASE("procgen.road.authoring.presetV2ExplicitSamplingOverridesRetainPresence") {
    using namespace eve::procgen_editing;
    RoadPresetDraft draft;
    auto            created = RoadNetwork::makeStraight(40.f, 2);
    REQUIRE(created.ok());
    draft.source                                   = std::move(created).takeValue();
    draft.types                                    = {{"root", "", {}}};
    draft.types[0].overrides.samplingSpacingMeters = 0.f;
    draft.types[0].overrides.samplingAngleDegrees  = 20.f;
    draft.types[0].overrides.samplingPolicy        = RoadSamplingPolicy::ParametricDistanceTurn;
    draft.bindings                                 = {{1, "root", {}}};
    auto doc                                       = RoadPresetDocument::create(draft);
    REQUIRE(doc.ok());
    auto saved = doc.value()->capture();
    REQUIRE(saved.ok());
    saved.value().getIf<eve::Value::Object>()->at("schemaVersion") = 2;
    auto token                                                     = doc.value()->prepareRestore(saved.value());
    REQUIRE(token.ok());
    REQUIRE(doc.value()->commit(std::move(token).takeValue()).ok());
    const auto& style = doc.value()->network().edges()[0].style;
    REQUIRE(style.samplingSpacingMeters == 0.f);
    REQUIRE(style.samplingAngleDegrees == 20.f);
    REQUIRE(style.samplingPolicy == RoadSamplingPolicy::ParametricDistanceTurn);
    REQUIRE(doc.value()->beginEdit().types[0].overrides.samplingSpacingMeters.has_value());
}
