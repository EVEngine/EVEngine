#include <cmath>
#include "procgen/editing/RoadPresetDocument.h"
#include "procgen/road/RoadNetworkCodec.h"
#include "procgen/spline/SplineCircularArc.h"
#include "procgen/spline/SplinePath.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;
using namespace eve::procgen::road;

TEST_CASE("procgen.road.authoring.circularReferenceArcAndReversePreserveGeometry") {
    RoadNetwork network;
    auto        a = network.addNode(-24, 0, 0), b = network.addNode(24, 1, 0);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    std::vector<RoadControlPoint> points = {{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {24, 1, 0}};
    points[1].pathControl                = RoadPathControl::Circular;
    auto edge                            = network.addEdge(a.value(), b.value(), points, 2);
    REQUIRE(edge.ok());
    auto makePath = [](const std::vector<RoadControlPoint>& points) {
        SplinePath path;
        if (!path.setKindResult("catmullRom").ok()) throw std::runtime_error("invalid kind");
        for (const auto& p : points) {
            SplinePoint q;
            q.x                  = p.x;
            q.y                  = p.y;
            q.z                  = p.z;
            q.spanControl        = static_cast<SplineSpanControl>(p.pathControl);
            q.circularFromEnd    = p.circularFromEnd;
            q.alignStraightStart = p.alignStraightStart;
            q.alignStraightEnd   = p.alignStraightEnd;
            if (!path.addPointResult(q).ok()) throw std::runtime_error("invalid point");
        }
        return path;
    };
    auto path   = makePath(points);
    auto middle = path.evaluateResult(.5f);
    REQUIRE(middle.ok());
    REQUIRE(std::fabs(middle.value().x) < 1e-5f);
    REQUIRE(std::fabs(middle.value().z - (std::sqrt(320.f) - 8.f)) < 1e-5f);
    auto before = captureRoadNetwork(network);
    REQUIRE(before.ok());
    REQUIRE(network.reverseEdge(edge.value()).ok());
    REQUIRE(network.edges()[0].controlPoints[1].circularFromEnd);
    auto reversed = makePath(network.edges()[0].controlPoints);
    for (int i = 0; i <= 192; ++i) {
        auto p = path.evaluateResult(static_cast<float>(i) / 192.f),
             q = reversed.evaluateResult(1.f - static_cast<float>(i) / 192.f);
        REQUIRE(p.ok());
        REQUIRE(q.ok());
        REQUIRE(std::fabs(p.value().x - q.value().x) < 1e-4f);
        REQUIRE(std::fabs(p.value().y - q.value().y) < 1e-4f);
        REQUIRE(std::fabs(p.value().z - q.value().z) < 1e-4f);
    }
    auto state = captureRoadNetwork(network);
    REQUIRE(state.ok());
    auto restored = restoreRoadNetwork(state.value());
    REQUIRE(restored.ok());
    REQUIRE(restored.value().edges()[0].controlPoints[1].circularFromEnd);
    REQUIRE(network.reverseEdge(edge.value()).ok());
    REQUIRE(captureRoadNetwork(network).value() == before.value());
    // Use an internal predecessor change; authoritative endpoint positions remain untouched.
    std::vector<RoadControlPoint> invalid = {{-24, 0, 0}, {-8, 2, 8}, {8, 5, 8}, {0, 1, 8}, {24, 1, 0}};
    invalid[2].pathControl                = RoadPathControl::Circular;
    REQUIRE(!network.setEdgeControlPoints(edge.value(), invalid).ok());
    REQUIRE(captureRoadNetwork(network).value() == before.value());
}

TEST_CASE("procgen.road.authoring.circularArcLimitsAndPresetUndo") {
    auto arc = detail::makeCircularArcXZ(0, 0, 8, 0, 1, 0, detail::CircularArcReference::Start);
    REQUIRE(arc.ok());
    REQUIRE(arc.value().line);
    REQUIRE(!detail::makeCircularArcXZ(0, 0, 8, 0, -1, 0, detail::CircularArcReference::Start).ok());
    REQUIRE(!detail::makeCircularArcXZ(0, 0, 0, 0, 1, 0, detail::CircularArcReference::Start).ok());
    using namespace eve::procgen_editing;
    RoadPresetDraft draft;
    auto            source = RoadNetwork::makeStraight(36.f, 2);
    REQUIRE(source.ok());
    draft.source                             = std::move(source).takeValue();
    draft.types                              = {{"circle", "", {}}};
    draft.bindings                           = {{1, "circle", {}}};
    draft.types[0].pointDefaults.pathControl = RoadPathControl::Circular;
    auto created                             = RoadPresetDocument::create(draft);
    REQUIRE(created.ok());
    auto doc                                    = std::move(created).takeValue();
    auto edit                                   = doc->beginEdit();
    edit.types[0].pointDefaults.circularFromEnd = true;
    auto token                                  = doc->prepare(std::move(edit));
    REQUIRE(token.ok());
    REQUIRE(doc->commit(std::move(token).takeValue()).ok());
    REQUIRE(doc->undo().ok());
    auto saved = doc->capture();
    REQUIRE(saved.ok());
    auto restore = doc->prepareRestore(saved.value());
    REQUIRE(restore.ok());
    REQUIRE(doc->commit(std::move(restore).takeValue()).ok());
    REQUIRE(!doc->network().edges()[0].controlPoints[0].circularFromEnd);
    REQUIRE(doc->redo().ok());
    REQUIRE(doc->network().edges()[0].controlPoints[0].circularFromEnd);
}
