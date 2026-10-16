#include <cmath>
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.outgoingStraightControllersRetainHeightContractAndReverse") {
    auto source = RoadNetwork::makeStraight(48.f, 2);
    REQUIRE(source.ok());
    auto network = std::move(source).takeValue();
    REQUIRE(network.setNodePosition(2, 24.f, 1.f, 0.f).ok());
    std::vector<RoadControlPoint> points = {{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {24, 1, 0}};
    points[0].pathControl                = RoadPathControl::StraightXYZ;
    points[1].pathControl                = RoadPathControl::StraightXZ;
    REQUIRE(network.setEdgeControlPoints(1, points).ok());
    auto makePath = [](const std::vector<RoadControlPoint>& points) {
        SplinePath path;
        if (!path.setKindResult("catmullRom").ok()) throw std::runtime_error("invalid kind");
        for (const auto& p : points) {
            SplinePoint q;
            q.x                  = p.x;
            q.y                  = p.y;
            q.z                  = p.z;
            q.alignStraightStart = p.alignStraightStart;
            q.alignStraightEnd   = p.alignStraightEnd;
            q.spanControl        = static_cast<SplineSpanControl>(p.pathControl);
            if (!path.addPointResult(q).ok()) throw std::runtime_error("invalid point");
        }
        return path;
    };
    auto before = captureRoadNetwork(network);
    REQUIRE(before.ok());
    auto path   = makePath(points);
    auto middle = path.evaluateResult(.5f);
    REQUIRE(middle.ok());
    REQUIRE(std::fabs(middle.value().x) < 1e-6f);
    REQUIRE(std::fabs(middle.value().z - 8.f) < 1e-6f);
    REQUIRE(std::fabs(middle.value().y - 3.875f) < 1e-6f);
    REQUIRE(network.reverseEdge(1).ok());
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
    REQUIRE(captureRoadNetwork(network).value() == before.value());
    auto restored = restoreRoadNetwork(before.value());
    REQUIRE(restored.ok());
    REQUIRE(restored.value().edges()[0].controlPoints[1].pathControl == RoadPathControl::StraightXZ);
    points[1].pathControl = RoadPathControl::StraightXYZ;
    auto straight         = makePath(points).evaluateResult(.5f);
    REQUIRE(straight.ok());
    REQUIRE(std::fabs(straight.value().y - 3.5f) < 1e-6f);
    points[1].pathControl = static_cast<RoadPathControl>(9);
    REQUIRE(!network.setEdgeControlPoints(1, points).ok());
    REQUIRE(captureRoadNetwork(network).value() == before.value());
    auto  legacy = before.value();
    auto& e      = legacy.getIf<eve::Value::Object>()->at("edges").getIf<eve::Value::Array>()->at(0);
    for (auto& p : *e.getIf<eve::Value::Object>()->at("pointProfiles").getIf<eve::Value::Array>())
        p.getIf<eve::Value::Object>()->erase("pathControl");
    auto old = restoreRoadNetwork(legacy);
    REQUIRE(old.ok());
    REQUIRE(old.value().edges()[0].controlPoints[0].pathControl == RoadPathControl::Spline);
    auto unequal           = network.edges()[0].controlPoints;
    unequal[0].pathControl = RoadPathControl::Spline;
    unequal[2].x           = 0.f;
    auto joined            = makePath(unequal).evaluateResult(5.f / 6.f);
    REQUIRE(joined.ok());
    REQUIRE(std::fabs(joined.value().x - 13.5f) < 1e-4f);
    REQUIRE(std::fabs(joined.value().z - 4.5f) < 1e-4f);
    REQUIRE(std::fabs(joined.value().y - 3.25f) < 1e-4f);
}

TEST_CASE("procgen.road.authoring.outgoingControllerPresetSaveAndUndo") {
    using namespace eve::procgen_editing;
    RoadPresetDraft draft;
    auto            source = RoadNetwork::makeCurve(36.f, 2);
    REQUIRE(source.ok());
    draft.source                             = std::move(source).takeValue();
    draft.types                              = {{"type", "", {}}};
    draft.bindings                           = {{1, "type", {}}};
    draft.types[0].pointDefaults.pathControl = RoadPathControl::StraightXZ;
    auto created                             = RoadPresetDocument::create(draft);
    REQUIRE(created.ok());
    auto doc                                = std::move(created).takeValue();
    auto edit                               = doc->beginEdit();
    edit.types[0].pointDefaults.pathControl = RoadPathControl::StraightXYZ;
    auto token                              = doc->prepare(std::move(edit));
    REQUIRE(token.ok());
    REQUIRE(doc->commit(std::move(token).takeValue()).ok());
    REQUIRE(doc->undo().ok());
    auto state = doc->capture();
    REQUIRE(state.ok());
    auto restore = doc->prepareRestore(state.value());
    REQUIRE(restore.ok());
    REQUIRE(doc->commit(std::move(restore).takeValue()).ok());
    REQUIRE(doc->network().edges()[0].controlPoints[0].pathControl == RoadPathControl::StraightXZ);
    REQUIRE(doc->redo().ok());
    REQUIRE(doc->network().edges()[0].controlPoints[0].pathControl == RoadPathControl::StraightXYZ);
}
