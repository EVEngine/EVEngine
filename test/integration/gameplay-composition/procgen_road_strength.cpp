#include <cmath>
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/road/RoadRecipes.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.splineStrengthChangesOnlyItsOutgoingSpanAndSurvivesReverse") {
    auto source = RoadNetwork::makeStraight(48.f, 2);
    REQUIRE(source.ok());
    auto                          network = std::move(source).takeValue();
    std::vector<RoadControlPoint> points  = {{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {24, 1, 0}};
    points[0].splineStrength              = .75f;
    points[1].splineStrength              = .25f;
    REQUIRE(network.setNodePosition(2, 24.f, 1.f, 0.f).ok());
    REQUIRE(network.setEdgeControlPoints(1, points).ok());
    auto authored = captureRoadNetwork(network);
    REQUIRE(authored.ok());
    auto makePath = [](const std::vector<RoadControlPoint>& points) {
        SplinePath path;
        auto       kind = path.setKindResult("catmullRom");
        if (!kind.ok()) throw std::runtime_error(kind.status().describe());
        for (const auto& p : points) {
            SplinePoint q;
            q.x            = p.x;
            q.y            = p.y;
            q.z            = p.z;
            q.spanStrength = p.splineStrength;
            auto added     = path.addPointResult(q);
            if (!added.ok()) throw std::runtime_error(added.status().describe());
        }
        return path;
    };
    auto path   = makePath(network.edges()[0].controlPoints);
    auto middle = path.evaluateResult(.5f);
    REQUIRE(middle.ok());
    REQUIRE(std::fabs(middle.value().z - 8.5f) < 1e-6f);
    REQUIRE(std::fabs(middle.value().y - 3.6875f) < 1e-6f);
    REQUIRE(network.reverseEdge(1).ok());
    REQUIRE(network.edges()[0].controlPoints[1].splineStrength == .25f);
    REQUIRE(network.edges()[0].controlPoints[2].splineStrength == .75f);
    auto reversed = makePath(network.edges()[0].controlPoints);
    for (int i = 0; i <= 96; ++i) {
        auto a = path.evaluateResult(static_cast<float>(i) / 96.f);
        auto b = reversed.evaluateResult(1.f - static_cast<float>(i) / 96.f);
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        REQUIRE(std::fabs(a.value().x - b.value().x) < 1e-4f);
        REQUIRE(std::fabs(a.value().y - b.value().y) < 1e-4f);
        REQUIRE(std::fabs(a.value().z - b.value().z) < 1e-4f);
    }
    REQUIRE(network.reverseEdge(1).ok());
    REQUIRE(captureRoadNetwork(network).value() == authored.value());
    auto encoded = captureRoadNetwork(network);
    REQUIRE(encoded.ok());
    auto restored = restoreRoadNetwork(encoded.value());
    REQUIRE(restored.ok());
    REQUIRE(restored.value().edges()[0].controlPoints[1].splineStrength == .25f);
    Params params;
    REQUIRE(setRoadRecipeNetwork(params, network).ok());
    REQUIRE(bakeRoadNetworkRecipe(params).ok());
    auto invalid              = network.edges()[0].controlPoints;
    invalid[1].splineStrength = 0.f;
    REQUIRE(!network.setEdgeControlPoints(1, invalid).ok());
    REQUIRE(captureRoadNetwork(network).value() == encoded.value());
    auto  legacy     = encoded.value();
    auto& legacyEdge = legacy.getIf<eve::Value::Object>()->at("edges").getIf<eve::Value::Array>()->at(0);
    for (auto& profile : *legacyEdge.getIf<eve::Value::Object>()->at("pointProfiles").getIf<eve::Value::Array>())
        profile.getIf<eve::Value::Object>()->erase("splineStrength");
    auto migrated = restoreRoadNetwork(legacy);
    REQUIRE(migrated.ok());
    REQUIRE(migrated.value().edges()[0].controlPoints[1].splineStrength == .5f);
    auto split = network.splitEdge(1, 1, 4.f);
    REQUIRE(split.ok());
    REQUIRE(network.edges()[0].controlPoints[0].splineStrength == .75f);
    REQUIRE(network.edges()[1].controlPoints[0].splineStrength == .25f);
}

TEST_CASE("procgen.road.authoring.splineStrengthPresetOverrideSaveAndUndo") {
    using namespace eve::procgen_editing;
    RoadPresetDraft draft;
    auto            source = RoadNetwork::makeCurve(36.f, 2);
    REQUIRE(source.ok());
    draft.source                                = std::move(source).takeValue();
    draft.types                                 = {{"curve", "", {}}};
    draft.types[0].pointDefaults.splineStrength = .25f;
    draft.bindings                              = {{1, "curve", {}}};
    auto created                                = RoadPresetDocument::create(draft);
    REQUIRE(created.ok());
    auto doc = std::move(created).takeValue();
    REQUIRE(doc->network().edges()[0].controlPoints[0].splineStrength == .25f);
    auto edit                                  = doc->beginEdit();
    edit.types[0].pointDefaults.splineStrength = 1.f;
    auto prepared                              = doc->prepare(std::move(edit));
    REQUIRE(prepared.ok());
    REQUIRE(doc->commit(std::move(prepared).takeValue()).ok());
    REQUIRE(doc->undo().ok());
    REQUIRE(doc->network().edges()[0].controlPoints[0].splineStrength == .25f);
    auto state = doc->capture();
    REQUIRE(state.ok());
    auto token = doc->prepareRestore(state.value());
    REQUIRE(token.ok());
    REQUIRE(doc->commit(std::move(token).takeValue()).ok());
    REQUIRE(doc->redo().ok());
    REQUIRE(doc->network().edges()[0].controlPoints[0].splineStrength == 1.f);
}
