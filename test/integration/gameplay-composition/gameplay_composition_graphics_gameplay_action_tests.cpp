#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_spline_path_binder)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include "editing/EditingGraph.h"
#include "procgen/editing/MeshModifierGraph.h"
#include "procgen/editing/SplinePathBinder.h"
#include "procgen/editing/SplinePathDocument.h"
#include "procgen/editing/SplinePathGizmo.h"
#include "zeroerr/unittest.h"
// editor_spline_path_binder.cpp
namespace test_editor_spline_path_binder {
// editor_spline_path_binder.cpp
using namespace eve::procgen_editing;

namespace {

SplinePathControlPoint binderPoint(const char* id, std::int64_t order, double x) {
    SplinePathControlPoint point;
    point.id    = StableId(id);
    point.order = order;
    point.x     = x;
    return point;
}

class FakeTransformSource final : public ISplineBindingTransformSource {
public:
    std::map<std::pair<std::string, std::string>, SplineBindingPose> poses;

    Result<SplineBindingPose> resolve(const std::string& host, const std::string& object) const override {
        const auto found = poses.find({host, object});
        if (found == poses.end())
            return eve::editing::failed<SplineBindingPose>(EditorStatus::Conflict, RuleId("test.source-stale"),
                                                           "source is stale");
        return eve::editing::applied<SplineBindingPose>(found->second);
    }
};

}  // namespace

TEST_CASE("editor.splinePathBinder.refreshesAllBoundPointsAtomically") {
    SplinePathDocument document("path");
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(binderPoint("a", 0, 0.0)).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(binderPoint("b", 1, 1.0)).value()).ok());
    FakeTransformSource source;
    source.poses[{"world", "left"}]  = {3.0, 4.0, 5.0};
    source.poses[{"world", "right"}] = {-2.0, 1.0, 7.0};
    SplinePathBinder binder("path");
    REQUIRE(binder.bindResult(document, StableId("a"), "world", "left", source).ok());
    REQUIRE(binder.bindResult(document, StableId("b"), "world", "right", source).ok());

    const auto revision = document.revision();
    REQUIRE(binder.refreshResult(document, source).ok());
    CHECK_EQ(document.revision(), revision + 2u);
    const auto points = document.points();
    CHECK_EQ(points[0].x, 3.0);
    CHECK_EQ(points[0].y, 4.0);
    CHECK_EQ(points[1].x, -2.0);
    CHECK_EQ(points[1].z, 7.0);
}

TEST_CASE("editor.splinePathBinder.staleSourcePreservesDocumentAndLinkForReload") {
    SplinePathDocument document("path");
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(binderPoint("a", 0, 2.0)).value()).ok());
    FakeTransformSource source;
    source.poses[{"world", "anchor"}] = {8.0, 0.0, 0.0};
    SplinePathBinder binder("path");
    REQUIRE(binder.bindResult(document, StableId("a"), "world", "anchor", source).ok());
    const auto snapshot = binder.snapshotValue();
    source.poses.clear();
    const auto revision = document.revision();
    auto       stale    = binder.refreshResult(document, source);
    REQUIRE(!stale.ok());
    CHECK_EQ(stale.code(), EditorStatus::Conflict);
    CHECK_EQ(document.revision(), revision);
    CHECK_EQ(document.points()[0].x, 2.0);
    CHECK_EQ(binder.bindings().size(), 1u);

    SplinePathBinder rebuilt("path");
    REQUIRE(rebuilt.loadSnapshot(snapshot).ok());
    source.poses[{"world", "anchor"}] = {9.0, 1.0, 0.0};
    REQUIRE(rebuilt.refreshResult(document, source).ok());
    CHECK_EQ(document.points()[0].x, 9.0);
    CHECK_EQ(document.points()[0].y, 1.0);
}

TEST_CASE("editor.splinePathBinder.rejectsDestroyedPointAndWrongDocument") {
    SplinePathDocument document("path");
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(binderPoint("a", 0, 0.0)).value()).ok());
    FakeTransformSource source;
    source.poses[{"world", "anchor"}] = {1.0, 2.0, 3.0};
    SplinePathBinder binder("path");
    REQUIRE(binder.bindResult(document, StableId("a"), "world", "anchor", source).ok());
    REQUIRE(document.applyDomainOperation(document.makeDeletePoint(StableId("a")).value()).ok());
    const auto revision   = document.revision();
    auto       stalePoint = binder.refreshResult(document, source);
    REQUIRE(!stalePoint.ok());
    CHECK_EQ(document.revision(), revision);

    SplinePathDocument other("other");
    auto               wrong = binder.refreshResult(other, source);
    REQUIRE(!wrong.ok());
    CHECK_EQ(wrong.code(), EditorStatus::Rejected);
}

TEST_CASE("editor.splinePathBinder.providerAbsentIsObservable") {
    SceneQuerySplineBindingSource absent(nullptr);
    auto                          result = absent.resolve("world", "anchor");
    REQUIRE(!result.ok());
    CHECK_EQ(result.code(), EditorStatus::Unsupported);
}

// editor_spline_path_document.cpp
using namespace eve::editing;
using namespace eve::procgen_editing;

namespace {

SplinePathControlPoint point(const char* id, std::int64_t order, double x, double y, double z = 0.0) {
    SplinePathControlPoint result;
    result.id    = StableId(id);
    result.order = order;
    result.x     = x;
    result.y     = y;
    result.z     = z;
    return result;
}

eve::procgen::MeshBuild sourceTriangle() {
    eve::procgen::MeshBuild mesh;
    mesh.addVertex(0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f);
    mesh.addVertex(1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f);
    mesh.addVertex(0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    return mesh;
}

}  // namespace

TEST_CASE("editor.splinePath.operationsAreReversibleAndSnapshotIsAtomic") {
    SplinePathDocument document("road-path");
    auto               first  = document.makeSetPoint(point("a", 0, 0.0, 0.0));
    auto               second = document.makeSetPoint(point("b", 1, 2.0, 3.0));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(document.applyDomainOperation(first.value()).ok());
    REQUIRE(document.applyDomainOperation(second.value()).ok());

    auto moved = document.makeSetPoint(point("b", 1, 4.0, 3.0));
    REQUIRE(moved.ok());
    REQUIRE(document.applyDomainOperation(moved.value()).ok());
    DomainOperation undo = moved.value();
    undo.type            = moved.value().inverseType;
    undo.payload         = moved.value().inverse;
    REQUIRE(document.applyDomainOperation(undo).ok());
    CHECK(std::abs(document.points()[1].x - 2.0) < 1e-9);

    auto settings = document.makeSetSettings({"linear", true});
    REQUIRE(settings.ok());
    REQUIRE(document.applyDomainOperation(settings.value()).ok());
    auto  snapshot = document.snapshotValue();
    auto* object   = snapshot.getIf<EditorValue::Object>();
    REQUIRE(object != nullptr);
    (*object)["futureField"] = EditorValue("preserved-by-newer-writer");

    SplinePathDocument restored("road-path-copy");
    REQUIRE(restored.loadSnapshot(snapshot).ok());
    CHECK(restored.settings().kind == "linear");
    CHECK(restored.settings().closed);
    CHECK_EQ(restored.points().size(), std::size_t(2));
    REQUIRE(restored.compilePath().ok());

    const auto before          = restored.snapshotValue();
    (*object)["schemaVersion"] = EditorValue(std::int64_t{99});
    CHECK(!restored.loadSnapshot(snapshot).ok());
    CHECK(restored.snapshotValue() == before);
}

TEST_CASE("editor.splinePath.pointProfileRoundTripsAndOldSnapshotsDefault") {
    SplinePathDocument document("profile-path");
    auto               profiled = point("a", 0, 0.0, 0.0);
    profiled.rollDegrees        = 35.0;
    profiled.scaleX             = 1.75;
    profiled.scaleY             = 0.6;
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(profiled).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(point("b", 1, 0.0, 2.0)).value()).ok());
    SplinePathDocument restored("profile-restored");
    REQUIRE(restored.loadSnapshot(document.snapshotValue()).ok());
    CHECK(std::abs(restored.points().front().rollDegrees - 35.0) < 1e-9);
    CHECK(std::abs(restored.points().front().scaleX - 1.75) < 1e-9);

    auto  legacy = document.snapshotValue();
    auto* object = legacy.getIf<EditorValue::Object>();
    REQUIRE(object != nullptr);
    auto* points = (*object)["points"].getIf<EditorValue::Array>();
    REQUIRE(points != nullptr);
    for (auto& value : *points) {
        auto* fields = value.getIf<EditorValue::Object>();
        REQUIRE(fields != nullptr);
        fields->erase("rollDegrees");
        fields->erase("scaleX");
        fields->erase("scaleY");
    }
    REQUIRE(restored.loadSnapshot(legacy).ok());
    CHECK(std::abs(restored.points().front().rollDegrees) < 1e-9);
    CHECK(std::abs(restored.points().front().scaleX - 1.0) < 1e-9);
    CHECK(std::abs(restored.points().front().scaleY - 1.0) < 1e-9);
    CHECK(std::abs(restored.points().front().pitchDegrees) < 1e-9);
    CHECK(std::abs(restored.points().front().yawDegrees) < 1e-9);
}

TEST_CASE("editor.splinePath.bezierInsertionPreservesCurveAndIsOneReversibleOperation") {
    SplinePathDocument document("bezier-insert");
    REQUIRE(document.applyDomainOperation(document.makeSetSettings({"bezier", false}).value()).ok());
    auto a = point("a", 0, 0.0, 0.0);
    a.outX = 1.0;
    a.outY = 2.0;
    auto b = point("b", 1, 4.0, 0.0);
    b.inX  = -1.0;
    b.inY  = 2.0;
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(a).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(b).value()).ok());
    const auto beforePath = document.compilePath();
    REQUIRE(beforePath.ok());
    const auto beforeQuarter = beforePath.value().evaluateResult(0.25f);
    const auto beforeMiddle  = beforePath.value().evaluateResult(0.5f);
    REQUIRE(beforeQuarter.ok());
    REQUIRE(beforeMiddle.ok());

    auto split = document.makeInsertPointAt(0, 0.5, StableId("middle"));
    REQUIRE(split.ok());
    REQUIRE(document.applyDomainOperation(split.value()).ok());
    CHECK_EQ(document.points().size(), 3u);
    const auto afterPath = document.compilePath();
    REQUIRE(afterPath.ok());
    const auto afterQuarter = afterPath.value().evaluateResult(0.25f);
    const auto afterMiddle  = afterPath.value().evaluateResult(0.5f);
    REQUIRE(afterQuarter.ok());
    REQUIRE(afterMiddle.ok());
    CHECK(std::abs(afterQuarter.value().x - beforeQuarter.value().x) < 1e-5f);
    CHECK(std::abs(afterQuarter.value().y - beforeQuarter.value().y) < 1e-5f);
    CHECK(std::abs(afterMiddle.value().x - beforeMiddle.value().x) < 1e-5f);
    CHECK(std::abs(afterMiddle.value().y - beforeMiddle.value().y) < 1e-5f);

    DomainOperation undo = split.value();
    undo.type            = split.value().inverseType;
    undo.payload         = split.value().inverse;
    REQUIRE(document.applyDomainOperation(undo).ok());
    CHECK_EQ(document.points().size(), 2u);
}

TEST_CASE("editor.splinePath.snapAndFlipProduceReversibleAuthoringOperations") {
    SplinePathDocument document("snap-flip");
    auto               a = point("a", 0, 0.24, 0.76, -0.26);
    a.outX               = 0.7;
    auto b               = point("b", 1, 2.0, 1.0);
    b.inX                = -0.4;
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(a).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(b).value()).ok());
    auto snap = document.makeSnapPoint(StableId("a"), 0.5);
    REQUIRE(snap.ok());
    REQUIRE(document.applyDomainOperation(snap.value()).ok());
    CHECK(std::abs(document.points()[0].x) < 1e-9);
    CHECK(std::abs(document.points()[0].y - 1.0) < 1e-9);
    CHECK(std::abs(document.points()[0].z + 0.5) < 1e-9);

    auto flip = document.makeFlipDirection();
    REQUIRE(flip.ok());
    REQUIRE(document.applyDomainOperation(flip.value()).ok());
    CHECK_EQ(document.points()[0].id, StableId("b"));
    CHECK(std::abs(document.points()[0].outX + 0.4) < 1e-9);
    CHECK_EQ(document.points()[1].id, StableId("a"));
    CHECK(std::abs(document.points()[1].inX - 0.7) < 1e-9);
}

TEST_CASE("editor.splinePath.splitAndConnectChunksRoundTripWithoutGizmoBridge") {
    SplinePathDocument document("chunks");
    REQUIRE(document.applyDomainOperation(document.makeSetSettings({"linear", false}).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(point("a", 0, 0.0, 0.0)).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(point("b", 1, 1.0, 0.0)).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(point("c", 2, 10.0, 0.0)).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(point("d", 3, 11.0, 0.0)).value()).ok());
    auto split = document.makeSetChunkBreak(StableId("c"), true);
    REQUIRE(split.ok());
    REQUIRE(document.applyDomainOperation(split.value()).ok());
    auto path = document.compilePath();
    REQUIRE(path.ok());
    CHECK_EQ(path.value().chunkCount(), 2);
    auto gizmo = SplinePathGizmoBuilder().build(document, 12);
    CHECK(gizmo.status == EditorStatus::Applied);
    CHECK_EQ(std::count_if(gizmo.primitives.begin(), gizmo.primitives.end(),
                           [](const auto& primitive) { return primitive.id.find("spline.chunk.0.segment") == 0; }),
             5);
    CHECK_EQ(std::count_if(gizmo.primitives.begin(), gizmo.primitives.end(),
                           [](const auto& primitive) { return primitive.id.find("spline.chunk.1.segment") == 0; }),
             5);
    auto connect = document.makeSetChunkBreak(StableId("c"), false);
    REQUIRE(connect.ok());
    REQUIRE(document.applyDomainOperation(connect.value()).ok());
    CHECK_EQ(document.compilePath().value().chunkCount(), 1);
}

TEST_CASE("editor.splinePath.rotationResetCenterAndAxisMirrorAreReversible") {
    SplinePathDocument document("point-tools");
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(point("a", 0, 0.0, 0.0)).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(point("b", 1, 1.2, 3.0)).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(point("c", 2, 4.0, 2.0)).value()).ok());
    auto rotation = document.makeSetPointRotation(StableId("b"), 20.0, -35.0, 15.0);
    REQUIRE(rotation.ok());
    REQUIRE(document.applyDomainOperation(rotation.value()).ok());
    CHECK(std::abs(document.points()[1].pitchDegrees - 20.0) < 1e-9);
    CHECK(std::abs(document.points()[1].yawDegrees + 35.0) < 1e-9);
    auto reset = document.makeResetPointRotation(StableId("b"));
    REQUIRE(reset.ok());
    REQUIRE(document.applyDomainOperation(reset.value()).ok());
    CHECK(std::abs(document.points()[1].rollDegrees) < 1e-9);
    auto center = document.makeCenterPoint(StableId("b"));
    REQUIRE(center.ok());
    REQUIRE(document.applyDomainOperation(center.value()).ok());
    CHECK(std::abs(document.points()[1].x - 2.0) < 1e-9);
    CHECK(std::abs(document.points()[1].y - 1.0) < 1e-9);
    auto mirror = document.makeMirrorAxis("x");
    REQUIRE(mirror.ok());
    REQUIRE(document.applyDomainOperation(mirror.value()).ok());
    CHECK(std::abs(document.points()[1].x + 2.0) < 1e-9);
    DomainOperation undo = mirror.value();
    undo.type            = mirror.value().inverseType;
    undo.payload         = mirror.value().inverse;
    REQUIRE(document.applyDomainOperation(undo).ok());
    CHECK(std::abs(document.points()[1].x - 2.0) < 1e-9);
}

TEST_CASE("editor.splinePath.snapAllAppendAndDeleteChunkAreAtomicAndReversible") {
    SplinePathDocument document("chunk-tools");
    REQUIRE(document.applyDomainOperation(document.makeSetSettings({"linear", false}).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(point("a", 0, 0.24, 0.76)).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(point("b", 1, 1.26, 1.74)).value()).ok());
    auto snapAll = document.makeSnapAll(0.5);
    REQUIRE(snapAll.ok());
    REQUIRE(document.applyDomainOperation(snapAll.value()).ok());
    CHECK(std::abs(document.points()[0].x) < 1e-9);
    CHECK(std::abs(document.points()[0].y - 1.0) < 1e-9);
    CHECK(std::abs(document.points()[1].x - 1.5) < 1e-9);
    CHECK(std::abs(document.points()[1].y - 1.5) < 1e-9);

    auto append = document.makeAppendChunk(point("c", 90, 10.0, 0.0), point("d", 91, 12.0, 0.0));
    REQUIRE(append.ok());
    REQUIRE(document.applyDomainOperation(append.value()).ok());
    REQUIRE(document.compilePath().ok());
    CHECK_EQ(document.compilePath().value().chunkCount(), 2);
    CHECK(document.points()[2].breakBefore);

    auto remove = document.makeDeleteChunk(0);
    REQUIRE(remove.ok());
    REQUIRE(document.applyDomainOperation(remove.value()).ok());
    REQUIRE_EQ(document.points().size(), 2u);
    CHECK_EQ(document.points()[0].id, StableId("c"));
    CHECK(!document.points()[0].breakBefore);
    DomainOperation undo = remove.value();
    undo.type            = remove.value().inverseType;
    undo.payload         = remove.value().inverse;
    REQUIRE(document.applyDomainOperation(undo).ok());
    CHECK_EQ(document.compilePath().value().chunkCount(), 2);
}

TEST_CASE("editor.splinePath.snapshotFeedsModifierGraphPreview") {
    SplinePathDocument path("graph-path");
    REQUIRE(path.applyDomainOperation(path.makeSetSettings({"linear", false}).value()).ok());
    REQUIRE(path.applyDomainOperation(path.makeSetPoint(point("a", 0, 0.0, 0.0)).value()).ok());
    REQUIRE(path.applyDomainOperation(path.makeSetPoint(point("b", 1, 2.0, 2.0)).value()).ok());

    MeshModifierGraphDomain domain;
    auto                    input  = domain.makeNode(GraphNodeId("input"), "mesh.input");
    auto                    spline = domain.makeNode(GraphNodeId("spline"), "deform.splinePath");
    auto                    output = domain.makeNode(GraphNodeId("output"), "mesh.output");
    REQUIRE(input.ok());
    REQUIRE(spline.ok());
    REQUIRE(output.ok());
    auto* properties = spline.value().properties.getIf<EditorValue::Object>();
    REQUIRE(properties != nullptr);
    (*properties)["splinePath"] = path.snapshotValue();

    GraphDocument graph;
    REQUIRE(graph.createNode(input.value()).ok());
    REQUIRE(graph.createNode(spline.value()).ok());
    REQUIRE(graph.createNode(output.value()).ok());
    const auto connect = [&](const char* edge, const char* from, const char* to) {
        const auto* source = graph.findPin(GraphPinId(from));
        const auto* target = graph.findPin(GraphPinId(to));
        REQUIRE(source != nullptr);
        REQUIRE(target != nullptr);
        REQUIRE(graph.connect({StableId(edge), source->id, target->id}, domain.canConnect(*source, *target)).ok());
    };
    connect("e0", "input.out", "spline.in0");
    connect("e1", "spline.out", "output.in0");

    auto preview = domain.preview(graph.snapshot(domain.domain()), "input", sourceTriangle(), "output");
    CHECK(preview.status == EditorStatus::Applied);
    CHECK(std::abs(preview.mesh.getPositionX(2) - 2.f) < 1e-5f);
    CHECK(std::abs(preview.mesh.getPositionY(2) - 2.f) < 1e-5f);
}

TEST_CASE("editor.splinePath.snapshotGeneratesTubeWithoutSourceMesh") {
    SplinePathDocument path("tube-path");
    REQUIRE(path.applyDomainOperation(path.makeSetSettings({"linear", false}).value()).ok());
    REQUIRE(path.applyDomainOperation(path.makeSetPoint(point("a", 0, 0.0, 0.0)).value()).ok());
    REQUIRE(path.applyDomainOperation(path.makeSetPoint(point("b", 1, 0.0, 3.0)).value()).ok());

    MeshModifierGraphDomain domain;
    auto                    tube   = domain.makeNode(GraphNodeId("tube"), "mesh.splineTube");
    auto                    output = domain.makeNode(GraphNodeId("output"), "mesh.output");
    REQUIRE(tube.ok());
    REQUIRE(output.ok());
    auto* properties = tube.value().properties.getIf<EditorValue::Object>();
    REQUIRE(properties != nullptr);
    (*properties)["splinePath"]     = path.snapshotValue();
    (*properties)["pathSegments"]   = EditorValue(std::int64_t{6});
    (*properties)["radialSegments"] = EditorValue(std::int64_t{8});

    GraphDocument graph;
    REQUIRE(graph.createNode(tube.value()).ok());
    REQUIRE(graph.createNode(output.value()).ok());
    const auto* source = graph.findPin(GraphPinId("tube.out"));
    const auto* target = graph.findPin(GraphPinId("output.in0"));
    REQUIRE(source != nullptr);
    REQUIRE(target != nullptr);
    REQUIRE(graph.connect({StableId("tube-output"), source->id, target->id}, domain.canConnect(*source, *target)).ok());

    auto preview = domain.preview(graph.snapshot(domain.domain()), "", eve::procgen::MeshBuild{}, "output");
    CHECK(preview.status == EditorStatus::Applied);
    CHECK_EQ(preview.mesh.getVertexCount(), 83);
    CHECK_EQ(preview.mesh.getGroupCount(), 2);
}

TEST_CASE("editor.splinePath.snapshotGeneratesRibbonWithoutSourceMesh") {
    SplinePathDocument path("ribbon-path");
    REQUIRE(path.applyDomainOperation(path.makeSetSettings({"catmullRom", false}).value()).ok());
    REQUIRE(path.applyDomainOperation(path.makeSetPoint(point("a", 0, -2.0, 0.0)).value()).ok());
    REQUIRE(path.applyDomainOperation(path.makeSetPoint(point("b", 1, 0.0, 1.0)).value()).ok());
    REQUIRE(path.applyDomainOperation(path.makeSetPoint(point("c", 2, 2.0, 0.0)).value()).ok());

    MeshModifierGraphDomain domain;
    auto                    ribbon = domain.makeNode(GraphNodeId("ribbon"), "mesh.splineRibbon");
    REQUIRE(ribbon.ok());
    auto* properties = ribbon.value().properties.getIf<EditorValue::Object>();
    REQUIRE(properties != nullptr);
    (*properties)["splinePath"]   = path.snapshotValue();
    (*properties)["thickness"]    = EditorValue(0.2);
    (*properties)["pathSegments"] = EditorValue(std::int64_t{8});
    GraphDocument graph;
    REQUIRE(graph.createNode(ribbon.value()).ok());
    auto preview = domain.preview(graph.snapshot(domain.domain()), "", eve::procgen::MeshBuild{}, "ribbon");
    CHECK(preview.status == EditorStatus::Applied);
    CHECK_EQ(preview.mesh.getVertexCount(), 80);
    CHECK_EQ(preview.mesh.getGroupCount(), 4);
}

TEST_CASE("editor.splinePath.snapshotGeneratesOwningProfileExtrusion") {
    SplinePathDocument path("profile-path");
    REQUIRE(path.applyDomainOperation(path.makeSetSettings({"linear", false}).value()).ok());
    REQUIRE(path.applyDomainOperation(path.makeSetPoint(point("a", 0, 0.0, 0.0)).value()).ok());
    REQUIRE(path.applyDomainOperation(path.makeSetPoint(point("b", 1, 0.0, 3.0)).value()).ok());

    MeshModifierGraphDomain domain;
    auto                    extrusion = domain.makeNode(GraphNodeId("extrusion"), "mesh.splineExtrude");
    REQUIRE(extrusion.ok());
    auto* properties = extrusion.value().properties.getIf<EditorValue::Object>();
    REQUIRE(properties != nullptr);
    (*properties)["splinePath"]   = path.snapshotValue();
    (*properties)["pathSegments"] = EditorValue(std::int64_t{3});
    GraphDocument graph;
    REQUIRE(graph.createNode(extrusion.value()).ok());
    auto preview = domain.preview(graph.snapshot(domain.domain()), "", eve::procgen::MeshBuild{}, "extrusion");
    CHECK(preview.status == EditorStatus::Applied);
    CHECK_EQ(preview.mesh.getVertexCount(), 28);
    CHECK_EQ(preview.mesh.getIndexCount(), 84);
    CHECK_EQ(preview.mesh.getMeta("generator", ""), "mesh.splineExtrude");
}

TEST_CASE("editor.splinePath.gizmoDragPreviewsThenCommitsOneOperation") {
    SplinePathDocument document("viewport-path");
    REQUIRE(document.applyDomainOperation(document.makeSetSettings({"bezier", false}).value()).ok());
    auto first  = point("a", 0, 0.0, 0.0);
    first.outX  = 0.5;
    auto second = point("b", 1, 2.0, 3.0);
    second.inX  = -0.5;
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(first).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(second).value()).ok());

    const auto gizmo = SplinePathGizmoBuilder().build(document, 16);
    CHECK(gizmo.status == EditorStatus::Applied);
    CHECK_EQ(gizmo.targetRevision, document.revision());
    const auto anchor = std::find_if(gizmo.primitives.begin(), gizmo.primitives.end(),
                                     [](const auto& primitive) { return primitive.id == "spline.anchor.b"; });
    REQUIRE(anchor != gizmo.primitives.end());
    CHECK(anchor->kind == "sphere");

    const auto            beforeRevision = document.revision();
    SplinePathDragSession drag(&document, 16);
    auto                  begun = drag.beginDrag(2.0, 3.0, 5.0, 0.0, 0.0, -1.0);
    REQUIRE(begun.ok());
    CHECK(drag.activePoint() == StableId("b"));
    CHECK(drag.activeHandle() == "anchor");
    auto preview = drag.updateDrag(4.0, 3.0, 5.0, 0.0, 0.0, -1.0);
    REQUIRE(preview.ok());
    CHECK(std::abs(preview.value().draft.x - 4.0) < 1e-9);
    CHECK_EQ(document.revision(), beforeRevision);
    CHECK(std::abs(document.points()[1].x - 2.0) < 1e-9);

    auto operation = drag.finishDrag();
    REQUIRE(operation.ok());
    CHECK_EQ(document.revision(), beforeRevision);
    REQUIRE(document.applyDomainOperation(operation.value()).ok());
    CHECK(std::abs(document.points()[1].x - 4.0) < 1e-9);
}

TEST_CASE("editor.splinePath.gizmoDragRejectsConcurrentDocumentMutation") {
    SplinePathDocument document("conflicted-path");
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(point("a", 0, 0.0, 0.0)).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(point("b", 1, 0.0, 2.0)).value()).ok());
    SplinePathDragSession drag(&document, 8);
    REQUIRE(drag.beginDrag(0.0, 0.0, 5.0, 0.0, 0.0, -1.0).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetSettings({"linear", false}).value()).ok());
    auto stale = drag.updateDrag(1.0, 0.0, 5.0, 0.0, 0.0, -1.0);
    CHECK(!stale.ok());
    CHECK(stale.code() == EditorStatus::Conflict);
    CHECK(!drag.isDragging());
}

TEST_CASE("editor.splinePath.gizmoKeepsIncompleteAuthoringPointsVisible") {
    SplinePathDocument document("new-path");
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(point("first", 0, 1.0, 2.0)).value()).ok());
    const auto gizmo = SplinePathGizmoBuilder().build(document, 8);
    CHECK(gizmo.status == EditorStatus::Applied);
    CHECK(!gizmo.diagnostics.empty());
    const auto anchor = std::find_if(gizmo.primitives.begin(), gizmo.primitives.end(),
                                     [](const auto& primitive) { return primitive.id == "spline.anchor.first"; });
    REQUIRE(anchor != gizmo.primitives.end());
    CHECK(std::abs(anchor->position[0] - 1.0) < 1e-9);
    CHECK(std::abs(anchor->position[1] - 2.0) < 1e-9);
}

TEST_CASE("editor.splinePath.gizmoStyleControlsNodeLineAndTextPresentation") {
    SplinePathDocument document("styled-gizmo");
    REQUIRE(document.applyDomainOperation(document.makeSetSettings({"linear", false}).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(point("a", 0, 0.0, 0.0)).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(point("b", 1, 2.0, 0.0)).value()).ok());
    SplineGizmoStyle style;
    style.nodeSize  = 0.42;
    style.lineColor = {0.8, 0.1, 0.6, 1.0};
    style.textColor = {0.2, 1.0, 0.3, 1.0};
    auto gizmo      = SplinePathGizmoBuilder().build(document, 8, 64, style);
    CHECK(gizmo.status == EditorStatus::Applied);
    const auto anchor = std::find_if(gizmo.primitives.begin(), gizmo.primitives.end(),
                                     [](const auto& primitive) { return primitive.id == "spline.anchor.a"; });
    const auto label  = std::find_if(gizmo.primitives.begin(), gizmo.primitives.end(),
                                     [](const auto& primitive) { return primitive.id == "spline.label.a"; });
    const auto line   = std::find_if(gizmo.primitives.begin(), gizmo.primitives.end(), [](const auto& primitive) {
        return primitive.id.find("spline.chunk.0.segment") == 0;
    });
    REQUIRE(anchor != gizmo.primitives.end());
    REQUIRE(label != gizmo.primitives.end());
    REQUIRE(line != gizmo.primitives.end());
    CHECK(std::abs(anchor->radius - 0.42) < 1e-9);
    CHECK_EQ(label->text, "a");
    CHECK(label->color == style.textColor);
    CHECK(line->color == style.lineColor);
}
}  // namespace test_editor_spline_path_binder
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_stylize_mesh_vfx)
#include "PrivateTestScope.h"
#include "stylize/MeshVfxAsset.h"
#include "stylize/StyleInstance.h"
#include "stylize/editing/MeshVfxTarget.h"
#include "stylize/editing/StylizeTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_stylize_mesh_vfx.cpp
namespace test_editor_stylize_mesh_vfx {
// editor_stylize_mesh_vfx.cpp
using namespace eve::editing;
using namespace eve::stylize_editing;

namespace {
SelectionSnapshot meshVfxSelection(const char* target) {
    SelectionSnapshot value;
    value.items.push_back({SelectionDomain::Asset, TargetId(target), StableId("mesh-vfx"), "stylize.mesh-vfx"});
    return value;
}
}  // namespace

TEST_CASE("editor.stylize_mesh_vfx edits undoes and persists canonical assets") {
    MeshVfxAssetTarget target("slash");
    const std::string  json = R"({
      "schema":"eve.stylize.mesh-vfx","schemaVersion":1,
      "layers":[{"style":"rim","playback":{"hold":0.3}}],
      "events":[{"time":0.5,"name":"hit"}]
    })";
    auto               operation =
        target.makeSet(meshVfxSelection("slash"), PropertyPath("asset.json"), json, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE_EQ(target.asset().events.front().name, "hit");
    DomainOperation undo = operation.value();
    undo.payload         = operation.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    REQUIRE(target.asset().events.empty());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    MeshVfxAssetTarget restored("copy");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    REQUIRE_EQ(restored.asset().events.front().name, "hit");
}

TEST_CASE("editor.stylize_mesh_vfx rejects invalid edits atomically") {
    MeshVfxAssetTarget target("slash");
    const auto         before = target.snapshotValue();
    auto operation = target.makeSet(meshVfxSelection("slash"), PropertyPath("asset.json"), R"({"schemaVersion":99})",
                                    PropertySetMode::Absolute);
    REQUIRE(!operation.ok());
    REQUIRE_EQ(target.snapshotValue(), before);
}

TEST_CASE("editor.stylize_mesh_vfx preview publication preserves the prior generation") {
    MeshVfxAssetTarget    target("slash");
    MeshVfxPreviewRuntime preview;
    REQUIRE(preview.publish(target).ok());
    auto* first = preview.instance();
    REQUIRE(first != nullptr);
    const auto revision = preview.revision();
    auto       operation =
        target.makeSet(meshVfxSelection("slash"), PropertyPath("asset.json"),
                       R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"missing-style"}]})",
                       PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
    REQUIRE(!preview.publish(target).ok());
    REQUIRE(preview.instance() == first);
    REQUIRE_EQ(preview.revision(), revision);
}

// editor_stylize_target.cpp
using namespace eve::stylize_editing;
using namespace eve::editing;

namespace {
SelectionSnapshot select(const StylizeRecipeTarget& target, std::initializer_list<const char*> ids) {
    SelectionSnapshot s;
    s.channel = "stylize";
    for (const char* id : ids)
        s.items.push_back({SelectionDomain::Asset, TargetId(target.targetId()), StableId(id), "stylize.pass"});
    return s;
}
void apply(StylizeRecipeTarget& target, Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}
}  // namespace

TEST_CASE("editor.stylize.recipe_supports_dynamic_parameters_order_and_undo") {
    StylizeRecipeTarget target("look");
    apply(target, target.makeCreate(ObjectId("water"), "watercolor"));
    apply(target, target.makeCreate(ObjectId("pixels"), "pixel"));
    CHECK_EQ(target.passes().size(), static_cast<std::size_t>(2));
    const auto selection = select(target, {"pixels"});
    CHECK(target.schema(selection).find(PropertyPath("parameter.pixelSize")) != nullptr);
    auto parameter = target.makeSet(selection, PropertyPath("parameter.pixelSize"), 12.0, PropertySetMode::Absolute);
    REQUIRE(parameter.ok());
    REQUIRE(target.applyDomainOperation(parameter.value()).ok());
    CHECK_EQ(target.passes()[1].overrides.at("pixelSize"), 12.0);
    DomainOperation undo = parameter.value();
    undo.payload         = parameter.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK(target.passes()[1].overrides.empty());
    apply(target, target.makeMove(ObjectId("pixels"), 0));
    CHECK_EQ(target.passes()[0].id.value(), std::string("pixels"));
    CHECK_EQ(target.passes()[0].priority, 0);
    CHECK_EQ(target.passes()[1].priority, 1);
    CHECK_EQ(static_cast<int>(target
                                  .makeSet(select(target, {"pixels"}), PropertyPath("parameter.pixelSize"), 100.0,
                                           PropertySetMode::Absolute)
                                  .code()),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.stylize.recipe_rejects_mixed_stages_and_loads_atomically") {
    StylizeRecipeTarget target("look");
    apply(target, target.makeCreate(ObjectId("cartoon"), "cartoon"));
    CHECK_EQ(static_cast<int>(target.makeCreate(ObjectId("water"), "watercolor").code()),
             static_cast<int>(EditorStatus::Rejected));
    const auto  before  = target.snapshotValue();
    EditorValue invalid = before;
    auto*       root    = invalid.getIf<EditorValue::Object>();
    auto*       content = (*root)["content"].getIf<EditorValue::Object>();
    auto*       passes  = (*content)["passes"].getIf<EditorValue::Array>();
    auto*       pass    = (*passes)[0].getIf<EditorValue::Object>();
    (*pass)["style"]    = "missing";
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
    StylizeRecipeTarget restored("copy");
    REQUIRE(restored.loadSnapshot(before).ok());
    CHECK_EQ(restored.passes()[0].style, std::string("cartoon"));
}

TEST_CASE("stylize.instance_accepts_authored_priority_override") {
    eve::stylize::StyleInstance instance("pixel");
    const int                   builtIn = instance.getPriority();
    instance.setPriority(-7);
    CHECK_EQ(instance.getPriority(), -7);
    instance.resetPriority();
    CHECK_EQ(instance.getPriority(), builtIn);
}

TEST_CASE("editor.stylize.runtime_rejects_stale_generation_before_graphics_use") {
    StylizeRecipeRuntime runtime;
    CHECK_EQ(static_cast<int>(runtime.apply(nullptr, nullptr, nullptr, Revision(1)).code()),
             static_cast<int>(EditorStatus::Conflict));
}
}  // namespace test_editor_stylize_mesh_vfx
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_surface_fluid_preview)
#include "PrivateTestScope.h"
#include "fluids/SurfaceDropletSimulation.h"
#include "fluids/SurfaceFluidRenderData.h"
#include "fluids/SurfaceWetnessField.h"
#include "fluids/editing/SurfaceFluidTarget.h"
#include "fluids/editor/EditorSurfaceFluidPreview.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_surface_fluid_preview.cpp
namespace test_editor_surface_fluid_preview {
// editor_surface_fluid_preview.cpp
using namespace eve::editor;

namespace {
SurfaceFluidPreviewRequest request(const SurfaceFluidTarget& target, double seconds) {
    SurfaceFluidPreviewRequest value;
    value.previewId        = StableId("surface-fluid");
    value.documentRevision = target.revision();
    value.seconds          = seconds;
    value.fixedStep        = 1.0 / 60.0;
    value.positions        = {{{0.0, 0.0, 0.0}}, {{2.0, -1.0, 0.0}}, {{0.0, 0.0, 2.0}}};
    value.indices          = {0, 1, 2};
    value.uvs              = {{{0.0, 0.0}}, {{1.0, 0.0}}, {{0.0, 1.0}}};
    SurfaceFluidPreviewSeed seed;
    seed.barycentric = {0.8, 0.1, 0.1};
    seed.volume      = 0.02;
    value.seeds.push_back(seed);
    return value;
}
}  // namespace

TEST_CASE("editor.surface_fluid.preview_replays_forward_and_backward_scrub_deterministically") {
    SurfaceFluidTarget         target("waterfall");
    SurfaceFluidPreviewService previews;
    const auto                 later = previews.build(target, request(target, 0.8));
    REQUIRE_EQ(static_cast<int>(later.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE(!later.droplets.empty());
    CHECK_EQ(later.vertexWetness.size(), static_cast<std::size_t>(3));
    const auto earlier = previews.build(target, request(target, 0.2));
    REQUIRE_EQ(static_cast<int>(earlier.status), static_cast<int>(EditorStatus::Applied));
    const auto replayed = previews.build(target, request(target, 0.8));
    CHECK(replayed.droplets == later.droplets);
    CHECK(replayed.vertexWetness == later.vertexWetness);
    CHECK_EQ(replayed.simulatedSeconds, 0.8);
}

TEST_CASE("editor.surface_fluid.preview_rejects_stale_invalid_and_unbounded_requests") {
    SurfaceFluidTarget         target("waterfall");
    SurfaceFluidPreviewService previews;
    auto                       stale = request(target, 0.1);
    stale.documentRevision++;
    CHECK_EQ(static_cast<int>(previews.build(target, stale).status), static_cast<int>(EditorStatus::Conflict));
    auto vertices            = request(target, 0.1);
    vertices.maximumVertices = 2;
    CHECK_EQ(static_cast<int>(previews.build(target, vertices).status), static_cast<int>(EditorStatus::Rejected));
    auto steps      = request(target, 1.0);
    steps.fixedStep = 1.0e-9;
    CHECK_EQ(static_cast<int>(previews.build(target, steps).status), static_cast<int>(EditorStatus::Rejected));
    auto seed                 = request(target, 0.1);
    seed.seeds[0].barycentric = {0.8, 0.8, 0.0};
    CHECK_EQ(static_cast<int>(previews.build(target, seed).status), static_cast<int>(EditorStatus::Rejected));
}

// editor_surface_fluid_target.cpp
using namespace eve::fluids_editing;
using namespace eve::editing;

namespace {
SelectionSnapshot selection(const char* target) {
    SelectionSnapshot value;
    value.items.push_back({SelectionDomain::Asset, TargetId(target), StableId("surface"), "fluids.surface"});
    return value;
}
}  // namespace

TEST_CASE("editor.surface_fluid_exposes_reversible_droplet_wetness_and_material_properties") {
    SurfaceFluidTarget target("waterfall");
    const auto         selected = selection("waterfall");
    CHECK_EQ(target.schema(selected).properties.size(), static_cast<std::size_t>(22));
    auto contact = target.makeSet(selected, PropertyPath("contactAngleDegrees"), 105.0, PropertySetMode::Absolute);
    REQUIRE(contact.ok());
    REQUIRE(target.applyDomainOperation(contact.value()).ok());
    CHECK_EQ(target.settings().contactAngleDegrees, 105.0);
    auto wetness = target.makeSet(selected, PropertyPath("maxWetness"), 2.0, PropertySetMode::Absolute);
    REQUIRE(wetness.ok());
    REQUIRE(target.applyDomainOperation(wetness.value()).ok());
    CHECK_EQ(target.settings().maxWetness, 2.0);
    DomainOperation undo = wetness.value();
    undo.payload         = wetness.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(target.settings().maxWetness, 1.0);
    CHECK_EQ(static_cast<int>(
                 target.makeSet(selected, PropertyPath("maxAspectRatio"), 0.5, PropertySetMode::Absolute).code()),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.surface_fluid_snapshot_load_is_atomic_and_material_warnings_are_structured") {
    SurfaceFluidTarget source("source");
    const auto         selected = selection("source");
    auto roughness = source.makeSet(selected, PropertyPath("wetRoughness"), 0.8, PropertySetMode::Absolute);
    REQUIRE(roughness.ok());
    REQUIRE(source.applyDomainOperation(roughness.value()).ok());
    CHECK(!source.validate().empty());
    SurfaceFluidTarget restored("restored");
    REQUIRE(restored.loadSnapshot(source.snapshotValue()).ok());
    CHECK_EQ(restored.settings().wetRoughness, 0.8);
    const auto  before                 = restored.settings();
    EditorValue invalid                = source.snapshotValue();
    auto*       root                   = invalid.getIf<EditorValue::Object>();
    auto*       settings               = (*root)["settings"].getIf<EditorValue::Object>();
    (*settings)["contactAngleDegrees"] = 180.0;
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK(restored.settings() == before);
}

TEST_CASE("editor.surface_fluid_runtime_bridge_rejects_incomplete_live_targets") {
    SurfaceFluidTarget         target("waterfall");
    SurfaceFluidRuntimeApplier bridge;
    CHECK_EQ(static_cast<int>(bridge.apply(target, nullptr, nullptr, nullptr).code()),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.surface_fluid_runtime_bridge_publishes_all_parameter_groups") {
    SurfaceFluidTarget target("waterfall");
    const auto         selected = selection("waterfall");
    auto               friction = target.makeSet(selected, PropertyPath("friction"), 3.25, PropertySetMode::Absolute);
    REQUIRE(friction.ok());
    REQUIRE(target.applyDomainOperation(friction.value()).ok());
    auto diffusion = target.makeSet(selected, PropertyPath("diffusion"), 0.4, PropertySetMode::Absolute);
    REQUIRE(diffusion.ok());
    REQUIRE(target.applyDomainOperation(diffusion.value()).ok());
    auto darkening = target.makeSet(selected, PropertyPath("wetDarkening"), 0.35, PropertySetMode::Absolute);
    REQUIRE(darkening.ok());
    REQUIRE(target.applyDomainOperation(darkening.value()).ok());
    eve::fluids::SurfaceDropletSimulation simulation(nullptr);
    eve::fluids::SurfaceFluidRenderParams render;
    eve::fluids::SurfaceWetnessParams     wetness;
    REQUIRE(SurfaceFluidRuntimeApplier().apply(target, &simulation, &render, &wetness).ok());
    CHECK_EQ(simulation.params().friction, 3.25f);
    CHECK_EQ(wetness.diffusion, 0.4f);
    CHECK_EQ(render.wetMaterial.wetDarkening, 0.35f);
}
}  // namespace test_editor_surface_fluid_preview
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_task_diagnostic_v2)
#include "PrivateTestScope.h"
#include <chrono>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <thread>
#include "common/Module.h"
#include "editor/EditorDiagnostics.h"
#include "editor/EditorTaskService.h"
#include "editor/EditorValueJson.h"
#include "editor/EditorWorkspace.h"
#include "editor/VegetationConverterController.h"
#include "graphics/material/editor/EditorGraph.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_task_diagnostic_v2.cpp
namespace test_editor_task_diagnostic_v2 {
// editor_task_diagnostic_v2.cpp
using namespace eve::editor;
using namespace std::chrono_literals;

namespace {

GraphNodeRecord asyncOutputNode() {
    GraphNodeRecord node;
    node.id         = GraphNodeId("async-output");
    node.type       = "material.output";
    node.properties = EditorValue::Object{};
    return node;
}

}  // namespace

TEST_CASE("editor.v2.task_queue_is_queryable_and_cancellable") {
    EditorTaskService tasks;
    auto              blocker = tasks.submit("blocker", [](const EditorTaskContext& context) {
        while (!context.isCancellationRequested()) std::this_thread::sleep_for(1ms);
        EditorTaskOutcome result;
        result.status = EditorStatus::Cancelled;
        return result;
    });
    REQUIRE(blocker.ok());
    auto queued = tasks.submit("queued", [](const EditorTaskContext&) { return EditorTaskOutcome{}; });
    REQUIRE(queued.ok());
    CHECK(tasks.cancel(queued.value()).ok());
    CHECK(tasks.cancel(blocker.value()).ok());
    CHECK(tasks.waitIdle(2s).ok());
    CHECK_EQ(static_cast<int>(tasks.snapshot(queued.value()).value().state),
             static_cast<int>(EditorTaskState::Cancelled));
    CHECK_EQ(static_cast<int>(tasks.snapshot(blocker.value()).value().state),
             static_cast<int>(EditorTaskState::Cancelled));
}

TEST_CASE("editor.v2.material_compile_runs_on_background_task_service") {
    EditorTaskService     tasks;
    MaterialEditorService materials;
    MaterialGraphDomain   domain;
    GraphDocument         graph;
    REQUIRE(graph.createNode(asyncOutputNode()).ok());
    const DocumentId document("document:async-material");
    auto             task = materials.compileAsync(document, graph.snapshot(domain.domain()), domain, tasks);
    REQUIRE(task.ok());
    CHECK(tasks.waitIdle(2s).ok());
    auto result = materials.result(task.value());
    REQUIRE(result.ok());
    CHECK_EQ(static_cast<int>(result.value().status), static_cast<int>(EditorStatus::Applied));
    CHECK(materials.publishPreview(document, graph.revision(), task.value()).ok());
    CHECK(!materials.previewArtifact(document).empty());
}

TEST_CASE("editor.v2.validation_and_diagnostics_are_extension_owned") {
    EditorValidationService validation;
    REQUIRE(validation
                .registerRule("park.plugin", RuleId("park.name-required"),
                              [](const ValidationRequest& request) {
                                  if (request.subject.empty())
                                      return std::vector<EditorDiagnostic>{eve::editing::ruleDiagnostic(
                                          eve::DiagnosticCode::PreconditionViolation, RuleId("park.name-required"),
                                          DiagnosticSeverity::Error, "Name is required")};
                                  return std::vector<EditorDiagnostic>{};
                              })
                .ok());
    auto found = validation.validate({"", EditorValue()});
    CHECK_EQ(found.size(), static_cast<std::size_t>(1));

    EditorDiagnosticService diagnostics;
    diagnostics.publish("validation", found);
    diagnostics.publish("compiler",
                        {eve::editing::ruleDiagnostic(eve::DiagnosticCode::Failed, RuleId("park.compile-warning"),
                                                      DiagnosticSeverity::Warning, "Warning")});
    CHECK_EQ(diagnostics.snapshot().size(), static_cast<std::size_t>(2));
    diagnostics.clear("validation");
    CHECK_EQ(diagnostics.snapshot().size(), static_cast<std::size_t>(1));
    CHECK_EQ(validation.unregisterOwner("park.plugin"), static_cast<std::size_t>(1));
    CHECK(validation.validate({"", EditorValue()}).empty());
}

TEST_CASE("editor.v2.settlementRuleValidationUsesCanonicalDecoder") {
    EditorValidationService validation;
    const std::string valid = R"({"schema":"settlement.rules","version":1,"conditionLanguageVersion":1,"rules":[]})";
    CHECK(validation.validate({"settlement.rules", EditorValue(valid)}).empty());

    const std::string unknownVersion =
        R"({"schema":"settlement.rules","version":2,"conditionLanguageVersion":1,"rules":[]})";
    const auto diagnostics = validation.validate({"settlement.rules", EditorValue(unknownVersion)});
    REQUIRE_EQ(diagnostics.size(), std::size_t{1});
    CHECK_EQ(diagnostics.front().code(), eve::DiagnosticCode::UnknownVersion);
    CHECK_EQ(diagnostics.front().path(), std::string("document.version"));

    CHECK(validation.validate({"another.document", EditorValue(unknownVersion)}).empty());
    const auto wrongType = validation.validate({"settlement.rules", EditorValue(std::int64_t{1})});
    REQUIRE_EQ(wrongType.size(), std::size_t{1});
    CHECK_EQ(wrongType.front().code(), eve::DiagnosticCode::TypeMismatch);
}

// editor_tilemap_target_script.cpp
TEST_CASE("editor.script.composes_live_tile_layer_target_with_field_tool") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        tileTargets <- eve.TileLayerTargetModule();
        map <- eve.Map();
        layer <- map.newLayer(8, 8, 16.0, 16.0);
        target <- tileTargets.create("ground", layer);
        falloff <- editor.newConstantBrushFalloff();
        kernel <- editor.newBoxBrushKernel();
        kernel.setConstantFalloff(falloff);
        operation <- editor.newPaintIntFieldOperation(17);
        tool <- editor.newFieldBrushTool("tile.paint", "Paint Tile");
        tool.setBoxKernel(kernel);
        tool.setPaintIntOperation(operation);
        tool.setRadius(0.5);
        session <- editor.newSession();
        added <- session.addFieldTool(tool);
        tileTargets.bind(session, target);
        activated <- session.activateTool("tile.paint");
        beforeRevision <- target.getRevision();
        session.dispatchPointer(0, 1, 0, 3.0, 4.0, 0.0, 0.0, 1.0);
        session.dispatchPointer(2, 1, 0, 3.0, 4.0, 0.0, 0.0, 1.0);
        painted <- target.readInt(3, 4);
        targetRevision <- target.getRevision();
        undone <- session.undo();
        restored <- target.readInt(3, 4);
        redone <- session.redo();
        replayed <- target.readInt(3, 4);
    )"));

    CHECK(vm.find("added").toBool());
    CHECK(vm.find("activated").toBool());
    CHECK_EQ(vm.find("painted").toInt(), 17);
    CHECK_GT(vm.find("targetRevision").toInt(), vm.find("beforeRevision").toInt());
    CHECK(vm.find("undone").toBool());
    CHECK_EQ(vm.find("restored").toInt(), 0);
    CHECK(vm.find("redone").toBool());
    CHECK_EQ(vm.find("replayed").toInt(), 17);
}

// editor_value_json_v2.cpp
using namespace eve::editor;

TEST_CASE("editor.v2.json_round_trips_without_host_dependencies") {
    const std::string source = R"({"z":"quote\"\\","a":[null,true,-7,1.25,"line\n\u4e2d\ud83d\ude80"]})";
    const auto        parsed = editorValueFromJson(source);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.ok());

    const std::string encoded = editorValueToJson(parsed.value());
    CHECK(encoded.find(R"("a":[null,true,-7,1.25,"line\n中🚀"])") == 1);
    CHECK(encoded.find(R"("z":"quote\"\\")") != std::string::npos);

    const auto roundTrip = editorValueFromJson(encoded);
    REQUIRE(roundTrip.ok());
    REQUIRE(roundTrip.ok());
    CHECK(roundTrip.value() == parsed.value());
    CHECK_EQ(editorValueContentHash(roundTrip.value()), editorValueContentHash(parsed.value()));
}

TEST_CASE("editor.v2.json_rejects_invalid_and_out_of_range_input") {
    CHECK(!editorValueFromJson(R"({"value":01})").ok());
    CHECK(!editorValueFromJson(R"([1,])").ok());
    CHECK(!editorValueFromJson(R"("\ud800")").ok());
    CHECK(!editorValueFromJson("1e9999").ok());
    CHECK(!editorValueFromJson("true false").ok());
}

TEST_CASE("editor.v2.json_serializes_non_finite_numbers_as_null") {
    CHECK_EQ(editorValueToJson(EditorValue(std::numeric_limits<double>::infinity())), std::string("null"));
    CHECK_EQ(editorValueToJson(EditorValue(std::numeric_limits<double>::quiet_NaN())), std::string("null"));
}

// editor_vegetation_converter.cpp
using namespace eve;
using namespace eve::asset_import;
using namespace eve::editor;

namespace {
class Preparer final : public IVegetationConversionPreparer {
public:
    eve::Result<PreparedAssetImport> prepare(const UnityVegetationBatchImportRequest&) const override {
        if (reject)
            return eve::Result<PreparedAssetImport>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "injected prepare failure"));
        PreparedAssetImport result;
        for (const char* uri :
             {"asset://550e8400-e29b-41d4-a716-446655440001", "asset://550e8400-e29b-41d4-a716-446655440002",
              "asset://550e8400-e29b-41d4-a716-446655440003"}) {
            auto reference = AssetRef::parse(uri);
            REQUIRE(reference.ok());
            result.manifest.assets.push_back(
                {std::move(reference).takeValue(), "eve.test", SchemaVersion(1), "asset.json", "sha256:test", {}});
        }
        result.findings.resize(2);
        return eve::Result<PreparedAssetImport>::success(std::move(result));
    }
    bool reject = false;
};

class Publisher final : public IVegetationConversionPublisher {
public:
    eve::Result<std::uint64_t> publish(const PreparedAssetImport& candidate,
                                       std::uint64_t              expectedGeneration) override {
        ++calls;
        observedAssets = candidate.manifest.assets.size();
        if (reject)
            return editing::failed<std::uint64_t>(editing::Status::Conflict, editing::RuleId("test.converter.publish"),
                                                  "injected publish conflict");
        return editing::applied<std::uint64_t>(expectedGeneration + 1);
    }
    bool        reject         = false;
    int         calls          = 0;
    std::size_t observedAssets = 0;
};

UnityVegetationBatchImportRequest request(const char* name) {
    UnityVegetationBatchImportRequest result;
    result.package.packageId      = *PersistentId::parse("11111111-2222-4333-8444-555555555555");
    result.package.packageName    = name;
    result.package.packageVersion = "1";
    result.objects.emplace_back();
    return result;
}
}  // namespace

TEST_CASE("editor.vegetation_converter_prepares_and_publishes_revision_bound_candidate") {
    Preparer                      preparer;
    Publisher                     publisher;
    VegetationConverterController controller(preparer, publisher);
    REQUIRE(controller.setRequest(request("forest"), 7).ok());
    REQUIRE(controller.prepare().ok());
    auto state = controller.state();
    CHECK_EQ(static_cast<int>(state.phase), static_cast<int>(VegetationConverterState::Phase::Prepared));
    CHECK_EQ(state.assetCount, 3U);
    CHECK_EQ(state.findingCount, 2U);
    auto published = controller.publish();
    REQUIRE(published.ok());
    CHECK_EQ(published.value(), 8U);
    CHECK_EQ(publisher.calls, 1);
    CHECK_EQ(publisher.observedAssets, 3U);
}

TEST_CASE("editor.vegetation_converter_invalidates_old_candidate_and_retains_retry_on_failure") {
    Preparer                      preparer;
    Publisher                     publisher;
    VegetationConverterController controller(preparer, publisher);
    REQUIRE(controller.setRequest(request("forest"), 2).ok());
    REQUIRE(controller.prepare().ok());
    REQUIRE(controller.setRequest(request("meadow"), 2).ok());
    CHECK(!controller.publish().ok());
    publisher.reject = true;
    REQUIRE(controller.prepare().ok());
    CHECK(!controller.publish().ok());
    CHECK_EQ(publisher.calls, 1);
    publisher.reject = false;
    auto retried     = controller.publish();
    REQUIRE(retried.ok());
    CHECK_EQ(retried.value(), 3U);
}

TEST_CASE("editor.vegetation_converter_surfaces_prepare_diagnostics_without_candidate") {
    Preparer preparer;
    preparer.reject = true;
    Publisher                     publisher;
    VegetationConverterController controller(preparer, publisher);
    REQUIRE(controller.setRequest(request("forest"), 0).ok());
    CHECK(!controller.prepare().ok());
    const auto state = controller.state();
    CHECK_EQ(static_cast<int>(state.phase), static_cast<int>(VegetationConverterState::Phase::Error));
    CHECK(!state.diagnostics.empty());
    CHECK(!controller.publish().ok());
    CHECK_EQ(publisher.calls, 0);
}

// editor_workspace.cpp
using namespace eve::editor;

TEST_CASE("editor.workspace.composes_project_panels_without_ui_dependencies") {
    EditorWorkspace workspace("world-authoring", "World Authoring");
    CHECK_EQ(workspace.getId(), std::string("world-authoring"));
    CHECK_EQ(workspace.getMode(), std::string("edit"));

    REQUIRE(workspace.registerPanel("scene", "Scene", "center", 0));
    REQUIRE(workspace.registerPanel("hierarchy", "Hierarchy", "left", 10));
    REQUIRE(workspace.registerPanel("assets", "Assets", "bottom", 20));
    REQUIRE(workspace.registerPanel("inspector", "Inspector", "right", 30));
    CHECK(!workspace.registerPanel("scene", "Duplicate", "floating", 0));
    CHECK(!workspace.registerPanel("bad", "Bad", "unknown", 0));
    CHECK_EQ(workspace.getPanelCount(), 4);

    REQUIRE(workspace.setPanelCapability("scene", "scene.viewport"));
    REQUIRE(workspace.setPanelContext("scene", "scene"));
    REQUIRE(workspace.movePanel("assets", "left", 20));
    REQUIRE(workspace.setPanelSingleton("assets", false));
    REQUIRE(workspace.setPanelVisible("inspector", false));
    CHECK(!workspace.activatePanel("inspector"));
    REQUIRE(workspace.activatePanel("scene"));
    CHECK_EQ(workspace.getActivePanel(), std::string("scene"));
    CHECK(workspace.getRevision() > 0);

    workspace.setRegionSize("left", 240.f);
    workspace.setRegionSize("right", 300.f);
    workspace.setRegionSize("top", 40.f);
    workspace.setRegionSize("bottom", 180.f);
    workspace.layout(1280.f, 720.f);
    CHECK_EQ(workspace.getRegionX("center"), 240.f);
    CHECK_EQ(workspace.getRegionY("center"), 40.f);
    CHECK_EQ(workspace.getRegionW("center"), 740.f);
    CHECK_EQ(workspace.getRegionH("center"), 500.f);
}

TEST_CASE("editor.workspace.shares_selection_and_focus_channels_across_presenters") {
    EditorWorkspace workspace("runtime-builder", "Runtime Builder");
    REQUIRE(workspace.select("world", "scene", "level-1", "unit-1", "rts.unit", false));
    REQUIRE(workspace.select("world", "scene", "level-1", "unit-2", "rts.unit", true));
    CHECK_EQ(workspace.getSelectionCount("world"), 2);
    CHECK_EQ(workspace.getSelectionItem("world", 0), std::string("unit-1"));
    CHECK_EQ(workspace.getSelectionType("world", 1), std::string("rts.unit"));
    CHECK_EQ(workspace.getPrimarySelection("world"), std::string("unit-2"));
    CHECK(workspace.getSelectionSequence("world") >= static_cast<std::uint64_t>(2));

    REQUIRE(workspace.select("assets", "asset", "project", "mat-grass", "material", false));
    CHECK_EQ(workspace.getSelectionCount("world"), 2);
    CHECK_EQ(workspace.getSelectionCount("assets"), 1);
    CHECK(!workspace.select("world", "invalid", "level-1", "x", "bad", false));

    REQUIRE(workspace.focus("world", "scene-viewport", "unit-2"));
    CHECK_EQ(workspace.getFocusedSurface("world"), std::string("scene-viewport"));
    REQUIRE(workspace.clearSelection("world"));
    CHECK_EQ(workspace.getSelectionCount("world"), 0);
}
}  // namespace test_editor_task_diagnostic_v2
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_texture_recipe_target)
#include "PrivateTestScope.h"
#include "procgen/editing/TextureRecipeTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_texture_recipe_target.cpp
namespace test_editor_texture_recipe_target {
using namespace eve::procgen_editing;
using namespace eve::editing;
namespace {
SelectionSnapshot select(const TextureRecipeTarget& target) {
    SelectionSnapshot selection;
    selection.channel = "texture-recipe";
    selection.items.push_back(
        {SelectionDomain::Asset, TargetId(target.targetId()), StableId(target.targetId().value()), "texture-recipe"});
    return selection;
}
void apply(TextureRecipeTarget& target, Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}
}  // namespace
TEST_CASE("editor.texture_recipe.reflects_schema_and_reverses_parameters") {
    TextureRecipeTarget target("cloud.asset", "tex.cloud");
    const auto          selection = select(target);
    const auto          schema    = target.schema(selection);
    REQUIRE(schema.find(PropertyPath("param.width")));
    REQUIRE(schema.find(PropertyPath("param.seamless")));
    auto width = target.makeSet(selection, PropertyPath("param.width"), int64_t{16}, PropertySetMode::Absolute);
    REQUIRE(width.ok());
    REQUIRE(target.applyDomainOperation(width.value()).ok());
    CHECK_EQ(*target.read(selection, PropertyPath("param.width")).value.getIf<int64_t>(), int64_t{16});
    DomainOperation undo = width.value();
    undo.payload         = width.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_NE(*target.read(selection, PropertyPath("param.width")).value.getIf<int64_t>(), int64_t{16});
    CHECK_EQ(
        static_cast<int>(
            target.makeSet(selection, PropertyPath("param.width"), int64_t{5000}, PropertySetMode::Absolute).code()),
        static_cast<int>(EditorStatus::Rejected));
}
TEST_CASE("editor.texture_recipe_snapshot_is_atomic_and_preview_is_deterministic") {
    TextureRecipeTarget target("cloud.asset", "tex.cloud");
    const auto          selection = select(target);
    apply(target, target.makeSet(selection, PropertyPath("param.width"), int64_t{16}, PropertySetMode::Absolute));
    apply(target, target.makeSet(selection, PropertyPath("param.height"), int64_t{12}, PropertySetMode::Absolute));
    const auto  before  = target.snapshotValue();
    EditorValue invalid = before;
    auto*       root    = invalid.getIf<EditorValue::Object>();
    auto*       content = (*root)["content"].getIf<EditorValue::Object>();
    auto*       values  = (*content)["values"].getIf<EditorValue::Object>();
    (*values)["width"]  = int64_t{0};
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
    TextureRecipePreviewRuntime runtime;
    auto                        first = runtime.generate(target);
    REQUIRE(first.ok());
    CHECK_EQ(first.value().width, 16);
    CHECK_EQ(first.value().height, 12);
    CHECK_NE(first.value().checksum, std::uint64_t{0});
    auto second = runtime.generate(target);
    REQUIRE(second.ok());
    CHECK_EQ(second.value().checksum, first.value().checksum);
    CHECK_EQ(runtime.revision(), target.revision());
}
}  // namespace test_editor_texture_recipe_target
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_transaction_consumer)
#include "PrivateTestScope.h"
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "editor/EditorAuthority.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorSession.h"
#include "editor/EditorTransactionConsumer.h"
#include "editor/EditorTransactionService.h"
#include "map/level/editing/FieldTargets.h"
#include "map/level/editing/TileBuffer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_transaction_consumer.cpp
namespace test_editor_transaction_consumer {
// editor_transaction_consumer.cpp
namespace {

using eve::Diagnostic;
using eve::DiagnosticCode;
using eve::Result;
using eve::StatusCode;
using eve::editor::DomainOperation;
using eve::editor::EditorCommitState;
using eve::editor::EditorStatus;
using eve::editor::EditorTransactionConsumer;
using eve::editor::IEditAuthority;
using eve::editor::IEditCommand;
using eve::editor::TransactionId;
using eve::editor::TransactionReceipt;
using eve::editor::TransactionSpec;
using eve::transaction::ITransactionParticipant;
using eve::transaction::TransactionContext;

class FailingParticipant final : public ITransactionParticipant {
public:
    std::string_view name() const noexcept override { return "test.second-participant"; }

    Result<void> prepare(const TransactionContext&) override {
        ++prepareCount;
        prepared = true;
        return Result<void>::success();
    }

    Result<void> commit(const TransactionContext&) override {
        ++commitCount;
        if (failCommit)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Failed, "injected second participant failure"));
        prepared = false;
        return Result<void>::success();
    }

    Result<void> rollback(const TransactionContext&) override {
        ++rollbackCount;
        prepared = false;
        return Result<void>::success();
    }

    Result<void> compensate(const TransactionContext&) override {
        ++compensateCount;
        return Result<void>::success();
    }

    int  prepareCount    = 0;
    int  commitCount     = 0;
    int  rollbackCount   = 0;
    int  compensateCount = 0;
    bool prepared        = false;
    bool failCommit      = true;
};

class ThrowingMergeCommand final : public IEditCommand {
public:
    ThrowingMergeCommand(std::string name, int* value, int after)
        : name_(std::move(name)), value_(value), after_(after) {}

    const std::string& name() const override { return name_; }
    bool               apply() override {
        if (!value_) return false;
        before_ = *value_;
        *value_ = after_;
        return true;
    }
    void revert() override {
        if (value_) *value_ = before_;
    }
    [[nodiscard]] std::unique_ptr<IEditCommand> clone() const override {
        return std::make_unique<ThrowingMergeCommand>(*this);
    }
    bool mergeWith(const IEditCommand&) override { throw std::runtime_error("injected merge failure"); }
    eve::editor::EditRegion dirtyRegion() const override { return {}; }

private:
    std::string name_;
    int*        value_  = nullptr;
    int         before_ = 0;
    int         after_  = 0;
};

class RejectingAuthority final : public IEditAuthority {
public:
    Result<eve::editor::AuthorityPlan> preflight(const TransactionSpec&, std::span<const DomainOperation>) override {
        ++preflightCount;
        return eve::editing::failed<eve::editor::AuthorityPlan>(
            EditorStatus::Conflict, eve::editor::RuleId("test.preflight.rejected"), "injected preflight rejection");
    }

    Result<TransactionReceipt> commit(const eve::editor::AuthorityPlan&) override {
        ++commitCount;
        return eve::editing::failed<TransactionReceipt>(EditorStatus::Failed,
                                                        eve::editor::RuleId("test.commit.unexpected"),
                                                        "commit must not be called after preflight rejection");
    }

    Result<TransactionReceipt> compensate(const TransactionReceipt&) override {
        ++compensateCount;
        return eve::editing::failed<TransactionReceipt>(EditorStatus::Failed,
                                                        eve::editor::RuleId("test.compensate.unexpected"),
                                                        "compensation must not be called after preflight rejection");
    }

    int preflightCount  = 0;
    int commitCount     = 0;
    int compensateCount = 0;
};

std::unique_ptr<eve::editor::IntFieldEditCommand> makeTileCommand(eve::level_editing::TileBufferTarget& target,
                                                                  int                                   value) {
    auto command = std::make_unique<eve::editor::IntFieldEditCommand>("set-tile", &target);
    if (!command->record(0, 0, value)) return nullptr;
    return command;
}

TransactionSpec commandSpec(std::string id) {
    TransactionSpec specification;
    specification.id    = TransactionId(std::move(id));
    specification.label = "test command";
    return specification;
}

DomainOperation rejectedOperation() {
    DomainOperation operation;
    operation.type   = "test.operation";
    operation.target = eve::editor::TargetId("test-target");
    return operation;
}

}  // namespace

TEST_CASE("editor.transactionConsumer.commitUndoRedoAndDryRun") {
    eve::level_editing::TileBuffer       buffer(1, 1);
    eve::level_editing::TileBufferTarget target("tiles", &buffer);
    EditorTransactionConsumer            consumer;

    auto begun = consumer.begin(commandSpec("editor.command.1"));
    REQUIRE(begun.ok());
    auto command = makeTileCommand(target, 7);
    REQUIRE(static_cast<bool>(command));
    auto appended = consumer.append(std::move(command));
    REQUIRE(appended.ok());
    CHECK_EQ(buffer.getGid(0, 0), 0);

    auto dryRun = consumer.dryRun();
    REQUIRE(dryRun.ok());
    CHECK_EQ(dryRun.value().commandCount, size_t{1});
    CHECK_EQ(buffer.getGid(0, 0), 0);

    auto committed = consumer.commit();
    REQUIRE(committed.ok());
    CHECK_EQ(static_cast<int>(committed.value().coordinator.state),
             static_cast<int>(eve::transaction::CoordinatorState::Committed));
    CHECK_EQ(buffer.getGid(0, 0), 7);
    CHECK(consumer.canUndo());

    auto undone = consumer.undo();
    REQUIRE(undone.ok());
    CHECK_EQ(static_cast<int>(undone.value().coordinator.state),
             static_cast<int>(eve::transaction::CoordinatorState::Compensated));
    CHECK_EQ(buffer.getGid(0, 0), 0);
    CHECK(consumer.canRedo());

    auto redone = consumer.redo();
    REQUIRE(redone.ok());
    CHECK_EQ(static_cast<int>(redone.value().coordinator.state),
             static_cast<int>(eve::transaction::CoordinatorState::Committed));
    CHECK_EQ(buffer.getGid(0, 0), 7);
}

TEST_CASE("editor.transactionConsumer.authorityPreflightRejectsWithoutCommit") {
    RejectingAuthority        authority;
    EditorTransactionConsumer consumer(&authority);

    TransactionSpec specification;
    specification.id     = TransactionId("editor.authority.rejected");
    specification.target = eve::editor::TargetId("test-target");
    REQUIRE(consumer.begin(std::move(specification)).ok());
    REQUIRE(consumer.append(rejectedOperation()).ok());

    auto dryRun = consumer.dryRun();
    CHECK(!dryRun.ok());
    CHECK_EQ(static_cast<int>(dryRun.code()), static_cast<int>(StatusCode::Conflict));
    CHECK_EQ(authority.preflightCount, 1);
    CHECK_EQ(authority.commitCount, 0);

    auto committed = consumer.commit();
    CHECK(!committed.ok());
    CHECK_EQ(authority.preflightCount, 2);
    CHECK_EQ(authority.commitCount, 0);
    CHECK(!consumer.canUndo());
}

TEST_CASE("editor.transactionConsumer.secondParticipantFailureCompensatesEditor") {
    eve::level_editing::TileBuffer       buffer(1, 1);
    eve::level_editing::TileBufferTarget target("tiles", &buffer);
    EditorTransactionConsumer            consumer;
    REQUIRE(consumer.begin(commandSpec("editor.composed.failure")).ok());
    auto command = makeTileCommand(target, 9);
    REQUIRE(static_cast<bool>(command));
    REQUIRE(consumer.append(std::move(command)).ok());

    FailingParticipant                      second;
    std::array<ITransactionParticipant*, 1> additional{&second};
    auto result = consumer.commit(std::span<ITransactionParticipant*>(additional.data(), additional.size()));
    CHECK(!result.ok());
    CHECK_EQ(buffer.getGid(0, 0), 0);
    CHECK_EQ(second.prepareCount, 1);
    CHECK_EQ(second.commitCount, 1);
    CHECK_EQ(second.rollbackCount, 1);
    CHECK_EQ(second.compensateCount, 0);
    CHECK(!consumer.canUndo());
    CHECK(consumer.active());
    CHECK_EQ(static_cast<int>(consumer.state()), static_cast<int>(EditorCommitState::FailedRetryable));
    CHECK(!consumer.diagnostics().empty());

    second.failCommit = false;
    auto retried      = consumer.retry(std::span<ITransactionParticipant*>(additional.data(), additional.size()));
    REQUIRE(retried.ok());
    CHECK_EQ(buffer.getGid(0, 0), 9);
    CHECK_EQ(second.commitCount, 2);
    CHECK_EQ(static_cast<int>(consumer.state()), static_cast<int>(EditorCommitState::Committed));

    auto duplicateCommit = consumer.commit(std::span<ITransactionParticipant*>(additional.data(), additional.size()));
    CHECK(!duplicateCommit.ok());
    CHECK_EQ(second.commitCount, 2);
}

TEST_CASE("editor.transactionConsumer.previewMergeFailureDoesNotMutateTarget") {
    int                       value = 0;
    EditorTransactionConsumer consumer;
    REQUIRE(consumer.begin(commandSpec("editor.preview.merge-failure")).ok());

    auto first = std::make_unique<ThrowingMergeCommand>("preview", &value, 1);
    REQUIRE(consumer.appendPreview(std::move(first)).ok());
    CHECK_EQ(value, 1);

    auto second = std::make_unique<ThrowingMergeCommand>("preview", &value, 2);
    auto failed = consumer.appendPreview(std::move(second));
    CHECK(!failed.ok());
    CHECK_EQ(value, 1);

    auto discarded = consumer.discard();
    REQUIRE(discarded.ok());
    CHECK_EQ(value, 0);
    CHECK(!consumer.active());
    CHECK_EQ(static_cast<int>(consumer.state()), static_cast<int>(EditorCommitState::Discarded));
}

TEST_CASE("editor.transactionConsumer.explicitDiscardClearsPendingWork") {
    eve::level_editing::TileBuffer       buffer(1, 1);
    eve::level_editing::TileBufferTarget target("tiles", &buffer);
    EditorTransactionConsumer            consumer;
    REQUIRE(consumer.begin(commandSpec("editor.explicit-discard")).ok());
    auto command = makeTileCommand(target, 4);
    REQUIRE(static_cast<bool>(command));
    REQUIRE(consumer.appendPreview(std::move(command)).ok());
    CHECK_EQ(buffer.getGid(0, 0), 4);

    auto discarded = consumer.discard();
    REQUIRE(discarded.ok());
    CHECK_EQ(buffer.getGid(0, 0), 0);
    CHECK(!consumer.active());
    auto retry = consumer.retry();
    CHECK(!retry.ok());
}

// editor_transaction_v2.cpp
using namespace eve::editor;

namespace {

class IntegerOperationTarget final : public IDomainOperationTarget, public IDomainOperationTargetStaging {
public:
    explicit IntegerOperationTarget(std::string id, int value = 0) : id_(std::move(id)), value_(value) {}

    TargetId      targetId() const override { return TargetId(id_); }
    std::uint64_t revision() const override { return revision_; }
    EditRegion    dirtyRegion() const override { return dirty_; }
    void          clearDirtyRegion() override { dirty_.clear(); }

    Result<void> applyDomainOperation(const DomainOperation& operation) override {
        if (operation.type == "test.fail.v1")
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("test.operation.failure"),
                                              "Synthetic operation failure");
        if (operation.type != "test.integer.set.v1")
            return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("test.operation.unsupported"),
                                              "Unsupported operation type");
        const auto* value = operation.payload.getIf<std::int64_t>();
        if (!value)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("test.operation.payload"),
                                              "Integer payload required");
        value_ = static_cast<int>(*value);
        ++revision_;
        dirty_.include(0, 0);
        return eve::editing::applied<void>();
    }

    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override {
        return std::make_unique<IntegerOperationTarget>(*this);
    }

    Result<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override {
        if (failCandidateCommit)
            return eve::editing::failed<void>(EditorStatus::Failed, RuleId("test.candidate.publish"),
                                              "Injected candidate publish failure");
        auto* staged = dynamic_cast<IntegerOperationTarget*>(candidate.get());
        if (!staged || staged->id_ != id_)
            return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("test.candidate.identity"),
                                              "Candidate belongs to another target");
        value_    = staged->value_;
        revision_ = staged->revision_;
        dirty_    = staged->dirty_;
        return eve::editing::applied<void>();
    }

    int  value() const { return value_; }
    bool failCandidateCommit = false;

private:
    std::string        id_;
    int                value_    = 0;
    unsigned long long revision_ = 0;
    EditRegion         dirty_;
};

DomainOperation setInteger(const IntegerOperationTarget& target, int before, int after, std::string inverseType = {}) {
    DomainOperation operation;
    operation.type        = "test.integer.set.v1";
    operation.inverseType = std::move(inverseType);
    operation.target      = TargetId(target.targetId());
    operation.payload     = EditorValue(after);
    operation.inverse     = EditorValue(before);
    operation.hasInverse  = true;
    operation.affectedProperties.push_back("value");
    return operation;
}

TransactionSpec transaction(const IntegerOperationTarget& target, const char* id) {
    TransactionSpec specification;
    specification.id           = TransactionId(id);
    specification.label        = "Set integer";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    return specification;
}

}  // namespace

TEST_CASE("editor.v2.authority_preflight_commit_and_compensate") {
    IntegerOperationTarget target("integer", 1);
    LocalWorldAuthority    authority(&target);
    DomainOperation        operation     = setInteger(target, 1, 5);
    TransactionSpec        specification = transaction(target, "transaction.one");

    auto plan = authority.preflight(specification, std::span<const DomainOperation>(&operation, 1));
    CHECK(plan.ok());
    CHECK_EQ(target.value(), 1);
    auto committed = authority.commit(plan.value());
    CHECK(committed.ok());
    CHECK_EQ(target.value(), 5);
    CHECK_EQ(committed.value().beforeRevision, static_cast<Revision>(0));
    CHECK_EQ(committed.value().afterRevision, static_cast<Revision>(1));

    target.failCandidateCommit                  = true;
    const auto valueBeforeFailedCompensation    = target.value();
    const auto revisionBeforeFailedCompensation = target.revision();
    auto       failedCompensation               = authority.compensate(committed.value());
    CHECK(!failedCompensation.ok());
    CHECK_EQ(target.value(), valueBeforeFailedCompensation);
    CHECK_EQ(target.revision(), revisionBeforeFailedCompensation);

    target.failCandidateCommit = false;
    auto compensated           = authority.compensate(committed.value());
    CHECK(compensated.ok());
    CHECK_EQ(target.value(), 1);
    CHECK_EQ(compensated.value().afterRevision, static_cast<Revision>(2));

    auto conflict = authority.preflight(specification, std::span<const DomainOperation>(&operation, 1));
    CHECK_EQ(static_cast<int>(conflict.code()), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.v2.authority_compensation_validates_full_inverse_on_candidate") {
    IntegerOperationTarget target("integer", 0);
    LocalWorldAuthority    authority(&target);
    const DomainOperation  operations[] = {
        setInteger(target, 0, 9, "test.fail.v1"),
        setInteger(target, 9, 5),
    };

    auto plan = authority.preflight(transaction(target, "transaction.candidate-failure"), operations);
    CHECK(plan.ok());
    auto committed = authority.commit(plan.value());
    CHECK(committed.ok());
    CHECK_EQ(target.value(), 5);
    const auto revisionAfterCommit = target.revision();

    auto compensation = authority.compensate(committed.value());
    CHECK(!compensation.ok());
    CHECK_EQ(static_cast<int>(compensation.code()), static_cast<int>(EditorStatus::Failed));
    CHECK_EQ(target.value(), 5);
    CHECK_EQ(target.revision(), revisionAfterCommit);
}

TEST_CASE("editor.v2.authority_rolls_back_partial_commit") {
    IntegerOperationTarget target("integer");
    LocalWorldAuthority    authority(&target);
    DomainOperation        first = setInteger(target, 0, 9);
    DomainOperation        failure;
    failure.type                       = "test.fail.v1";
    failure.target                     = TargetId(target.targetId());
    const DomainOperation operations[] = {first, failure};

    auto plan = authority.preflight(transaction(target, "transaction.rollback"), operations);
    CHECK(plan.ok());
    auto result = authority.commit(plan.value());
    CHECK(!result.ok());
    CHECK_EQ(target.value(), 0);
    CHECK(result.ok());
    CHECK_EQ(static_cast<int>(result.value().state), static_cast<int>(TransactionState::Rejected));
}

TEST_CASE("editor.v2.local_transaction_backend_undo_and_redo") {
    IntegerOperationTarget  target("integer");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend backend(&authority);

    CHECK(backend.begin(transaction(target, "transaction.history")).ok());
    CHECK(backend.append(setInteger(target, 0, 7)).ok());
    auto committed = backend.commit();
    CHECK(committed.ok());
    CHECK_EQ(target.value(), 7);
    CHECK(backend.canUndo());

    auto undone = backend.undo();
    CHECK(undone.ok());
    CHECK_EQ(target.value(), 0);
    CHECK(backend.canRedo());

    auto redone = backend.redo();
    CHECK(redone.ok());
    CHECK_EQ(target.value(), 7);
}

TEST_CASE("editor.v2.planned_command_is_dry_run_and_revision_safe") {
    IntegerOperationTarget  target("integer", 2);
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend backend(&authority);
    EditorCommandService    commands;

    CommandDescriptor descriptor;
    descriptor.id          = CommandId("test.integer.set");
    descriptor.ownerModule = "test";
    CHECK(commands
              .registerPlannedCommand(
                  descriptor,
                  [&](const CommandRequest& request) {
                      const auto* after = request.payload.getIf<std::int64_t>();
                      if (!after)
                          return eve::editing::failed<CommandPlan>(
                              EditorStatus::Rejected, RuleId("test.command.payload"), "Integer payload required");
                      CommandPlan plan;
                      plan.operations.push_back(setInteger(target, target.value(), static_cast<int>(*after)));
                      return eve::editing::applied<CommandPlan>(std::move(plan));
                  },
                  [&](const CommandRequest&, const CommandPlan& plan) {
                      TransactionSpec specification;
                      specification.id           = TransactionId(plan.id.value());
                      specification.label        = "Planned integer change";
                      specification.target       = plan.target;
                      specification.baseRevision = plan.baseRevision;
                      auto begun                 = backend.begin(std::move(specification));
                      if (!begun.ok())
                          return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("test.command.begin"),
                                                                          "Could not begin transaction");
                      for (const DomainOperation& operation : plan.operations) {
                          auto appended = backend.append(operation);
                          if (!appended.ok()) {
                              auto rolledBack = backend.rollback();
                              if (!rolledBack.ok())
                                  return eve::editing::failed<TransactionReceipt>(rolledBack.code(),
                                                                                  RuleId("test.command.rollback"),
                                                                                  "Could not roll back transaction");
                              return eve::editing::failed<TransactionReceipt>(
                                  appended.code(), RuleId("test.command.append"), "Could not append operation");
                          }
                      }
                      return backend.commit();
                  })
              .ok());

    EditorSession session;
    session.setSessionId(SessionId("test-session"));
    session.setCommandService(&commands);
    session.bindTarget(&target);

    auto plan = session.planCommand(descriptor.id, EditorValue(11), CommandSource::Script, target.revision());
    CHECK(plan.ok());
    CHECK_EQ(target.value(), 2);
    auto executed = session.executePlan(plan.value(), EditorValue(11), CommandSource::Script);
    CHECK(executed.ok());
    CHECK_EQ(target.value(), 11);
    CHECK_EQ(static_cast<int>(executed.value().state), static_cast<int>(TransactionState::Committed));

    auto stalePlan = session.planCommand(descriptor.id, EditorValue(15));
    CHECK(stalePlan.ok());
    DomainOperation external = setInteger(target, 11, 12);
    CHECK(target.applyDomainOperation(external).ok());
    auto stale = session.executePlan(stalePlan.value(), EditorValue(15));
    CHECK_EQ(static_cast<int>(stale.code()), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(target.value(), 12);
}

TEST_CASE("editor.v2.plans_bind_payload_target_lifetime_and_command_registration") {
    IntegerOperationTarget target("planned.identity", 1);
    IntegerOperationTarget replacement("planned.identity", 1);
    EditorCommandService   commands;

    auto registerCommand = [&](bool replace) {
        CommandDescriptor descriptor;
        descriptor.id          = CommandId("test.plan.identity");
        descriptor.ownerModule = "test";
        return commands.registerPlannedCommand(
            descriptor, [](const CommandRequest&) { return eve::editing::applied<CommandPlan>(CommandPlan{}); },
            [](const CommandRequest&, const CommandPlan&) {
                TransactionReceipt receipt;
                receipt.state = TransactionState::Committed;
                return eve::editing::applied<TransactionReceipt>(std::move(receipt));
            },
            replace);
    };
    REQUIRE(registerCommand(false).ok());

    EditorSession session;
    session.setCommandService(&commands);
    session.bindTarget(&target);
    auto plan = session.planCommand(CommandId("test.plan.identity"), EditorValue(3));
    REQUIRE(plan.ok());
    REQUIRE(plan.ok());

    auto changedPayload = session.executePlan(plan.value(), EditorValue(4));
    CHECK_EQ(static_cast<int>(changedPayload.code()), static_cast<int>(EditorStatus::Rejected));

    session.bindTarget(&replacement);
    auto replacedTarget = session.executePlan(plan.value(), EditorValue(3));
    CHECK_EQ(static_cast<int>(replacedTarget.code()), static_cast<int>(EditorStatus::Conflict));

    auto replacementPlan = session.planCommand(CommandId("test.plan.identity"), EditorValue(5));
    REQUIRE(replacementPlan.ok());
    REQUIRE(registerCommand(true).ok());
    auto replacedCommand = session.executePlan(replacementPlan.value(), EditorValue(5));
    CHECK_EQ(static_cast<int>(replacedCommand.code()), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.v2.read_only_authority_rejects_mutation") {
    ReadOnlyAuthority authority;
    TransactionSpec   specification;
    specification.id     = TransactionId("read-only");
    specification.target = TargetId("target");
    auto result          = authority.preflight(specification, {});
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(EditorStatus::Rejected));
}
}  // namespace test_editor_transaction_consumer
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_ui_document)
#include "PrivateTestScope.h"
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "common/Module.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorIds.h"
#include "editor/EditorTransactionService.h"
#include "ui/Theme.h"
#include "ui/editing/UiDocument.h"
#include "ui/editing/UiTheme.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_ui_document.cpp
namespace test_editor_ui_document {
// editor_ui_document.cpp
using namespace eve::ui_editing;
using namespace eve::editing;
using eve::editor::LocalTransactionBackend;

TEST_CASE("editor.ui.style_inspector_preview_pick_and_anchor_gizmo_are_revision_safe") {
    UiDocumentTarget document("hud");
    UiLayoutValue    rootLayout;
    rootLayout.width  = 800.0;
    rootLayout.height = 600.0;
    auto root         = document.makeCreate({ObjectId("root"), ObjectId(), "window", "Root", rootLayout});
    REQUIRE(root.ok());
    CHECK(document.applyDomainOperation(root.value()).ok());
    UiLayoutValue childLayout;
    childLayout.x      = 20.0;
    childLayout.y      = 30.0;
    childLayout.width  = 100.0;
    childLayout.height = 40.0;
    auto child         = document.makeCreate({ObjectId("play"), ObjectId("root"), "button", "Play", childLayout});
    REQUIRE(child.ok());
    CHECK(document.applyDomainOperation(child.value()).ok());
    UiStyleValue style;
    style.paddingLeft  = 10.0;
    style.paddingTop   = 5.0;
    style.tintR        = 0.25;
    style.cornerRadius = 6.0;
    style.direction    = "column";
    auto styled        = document.makeSetStyle(ObjectId("play"), style);
    REQUIRE(styled.ok());
    CHECK(document.applyDomainOperation(styled.value()).ok());

    UiDocumentPreviewService previewService;
    const auto               preview = previewService.build(document, 800.0, 600.0);
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    auto picked = previewService.pick(preview, 25.0, 35.0);
    REQUIRE(picked.ok());
    CHECK_EQ(picked.value().value(), "play");
    auto gizmo = previewService.anchorGizmo(document, preview, ObjectId("play"));
    CHECK(gizmo.ok());
    auto rename = document.makeRename(ObjectId("play"), "Play now");
    REQUIRE(rename.ok());
    CHECK(document.applyDomainOperation(rename.value()).ok());
    CHECK_EQ(static_cast<int>(previewService.anchorGizmo(document, preview, ObjectId("play")).code()),
             static_cast<int>(EditorStatus::Conflict));
}

namespace {

Result<TransactionReceipt> commitUi(UiDocumentTarget& target, LocalTransactionBackend& transactions,
                                    const DomainOperation& operation, const std::string& id) {
    TransactionSpec specification;
    specification.id           = TransactionId(id);
    specification.label        = "Edit UI";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("test.ui.begin"),
                                                        "Could not begin UI transaction");
    auto appended = transactions.append(operation);
    if (!appended.ok()) {
        [[maybe_unused]] const auto rolledBack = transactions.rollback();
        return eve::editing::failed<TransactionReceipt>(appended.code(), RuleId("test.ui.append"),
                                                        "Could not append UI operation");
    }
    return transactions.commit();
}

SelectionSnapshot uiSelection(const UiDocumentTarget& target, std::initializer_list<const char*> ids) {
    SelectionSnapshot selection;
    selection.channel = "ui";
    for (const char* id : ids) {
        SelectionItem item;
        item.domain = SelectionDomain::UI;
        item.target = TargetId(target.targetId());
        item.item   = StableId(id);
        item.type   = "ui.widget";
        selection.items.push_back(item);
    }
    if (!selection.items.empty()) selection.primary = selection.items.front();
    return selection;
}

}  // namespace

TEST_CASE("editor.ui.document_hierarchy_operations_are_reversible_and_cycle_safe") {
    UiDocumentTarget        target("hud");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    for (const CreateUiWidgetRequest& request :
         {CreateUiWidgetRequest{ObjectId("root"), {}, "column", "Root", {0, 0, 1280, 720}},
          CreateUiWidgetRequest{ObjectId("panel"), ObjectId("root"), "panel", "Panel", {10, 20, 300, 200}},
          CreateUiWidgetRequest{ObjectId("label"), ObjectId("panel"), "text", "Label", {4, 4, 100, 24}}}) {
        auto operation = target.makeCreate(request);
        REQUIRE(operation.ok());
        REQUIRE(commitUi(target, transactions, operation.value(), "ui.create." + request.id.value()).ok());
    }
    CHECK_EQ(target.children(ObjectId("panel")).size(), static_cast<std::size_t>(1));
    CHECK_EQ(static_cast<int>(target.makeDelete(ObjectId("panel")).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(target.makeReparent(ObjectId("root"), ObjectId("label")).code()),
             static_cast<int>(EditorStatus::Rejected));

    auto rename = target.makeRename(ObjectId("label"), "Title");
    REQUIRE(rename.ok());
    REQUIRE(commitUi(target, transactions, rename.value(), "ui.rename").ok());
    CHECK_EQ(target.widget(ObjectId("label")).value().name, std::string("Title"));
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(target.widget(ObjectId("label")).value().name, std::string("Label"));
    REQUIRE(transactions.redo().ok());

    auto reparent = target.makeReparent(ObjectId("label"), ObjectId("root"));
    REQUIRE(reparent.ok());
    REQUIRE(commitUi(target, transactions, reparent.value(), "ui.reparent").ok());
    CHECK(target.widget(ObjectId("label")).value().parent == ObjectId("root"));
    auto remove = target.makeDelete(ObjectId("label"));
    REQUIRE(remove.ok());
    REQUIRE(commitUi(target, transactions, remove.value(), "ui.delete").ok());
    CHECK_EQ(static_cast<int>(target.widget(ObjectId("label")).code()), static_cast<int>(EditorStatus::NotFound));
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(target.widget(ObjectId("label")).value().name, std::string("Title"));
}

TEST_CASE("editor.ui.multi_selection_layout_inspector_is_atomic_and_reports_mixed_values") {
    UiDocumentTarget        target("hud");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    for (const char* id : {"left", "right"}) {
        CreateUiWidgetRequest request{ObjectId(id), {}, "button", id, {}};
        auto                  operation = target.makeCreate(request);
        REQUIRE(operation.ok());
        REQUIRE(commitUi(target, transactions, operation.value(), std::string("ui.create.") + id).ok());
    }
    const SelectionSnapshot selection = uiSelection(target, {"left", "right"});
    CHECK_EQ(target.schema(selection).properties.size(), static_cast<std::size_t>(25));
    auto size = target.makeSet(selection, PropertyPath("layout.size"), EditorValue::Array{180.0, 42.0},
                               PropertySetMode::Absolute);
    REQUIRE(size.ok());
    REQUIRE(commitUi(target, transactions, size.value(), "ui.multi-size").ok());
    CHECK_EQ(target.widget(ObjectId("left")).value().layout.width, 180.0);
    CHECK_EQ(target.widget(ObjectId("right")).value().layout.height, 42.0);
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(target.widget(ObjectId("left")).value().layout.width, 0.0);

    UiLayoutValue moved;
    moved.x   = 15.0;
    auto move = target.makeSetLayout(ObjectId("left"), moved);
    REQUIRE(move.ok());
    REQUIRE(commitUi(target, transactions, move.value(), "ui.move-one").ok());
    CHECK_EQ(static_cast<int>(target.read(selection, PropertyPath("layout.position")).state),
             static_cast<int>(PropertyReadState::Mixed));
    CHECK_EQ(static_cast<int>(target
                                  .makeSet(selection, PropertyPath("layout.anchor"), EditorValue::Array{1.5, 0.0},
                                           PropertySetMode::Absolute)
                                  .code()),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.ui.snapshot_load_is_atomic_and_rejects_invalid_hierarchies") {
    UiDocumentTarget source("source");
    auto             root = source.makeCreate({ObjectId("root"), {}, "column", "Root", {0, 0, 640, 480}});
    REQUIRE(root.ok());
    REQUIRE(source.applyDomainOperation(root.value()).ok());
    auto child = source.makeCreate({ObjectId("child"), ObjectId("root"), "text", "Child", {8, 8, 100, 20}});
    REQUIRE(child.ok());
    REQUIRE(source.applyDomainOperation(child.value()).ok());

    UiDocumentTarget restored("restored");
    REQUIRE(restored.loadSnapshot(source.snapshotValue()).ok());
    CHECK_EQ(restored.children(ObjectId("root")).size(), static_cast<std::size_t>(1));

    EditorValue::Object invalidWidget;
    invalidWidget["id"]      = "orphan";
    invalidWidget["parent"]  = "missing";
    invalidWidget["type"]    = "text";
    invalidWidget["name"]    = "Orphan";
    invalidWidget["text"]    = "";
    invalidWidget["visible"] = true;
    invalidWidget["enabled"] = true;
    invalidWidget["layout"] = EditorValue::Object{{"x", 0.0},       {"y", 0.0},       {"width", 10.0}, {"height", 10.0},
                                                  {"anchorX", 0.0}, {"anchorY", 0.0}, {"pivotX", 0.0}, {"pivotY", 0.0}};
    EditorValue::Object invalidRoot;
    invalidRoot["schemaVersion"] = 1;
    invalidRoot["widgets"]       = EditorValue::Array{EditorValue(std::move(invalidWidget))};
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(EditorValue(std::move(invalidRoot))).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK(restored.widget(ObjectId("child")).ok());
}

// editor_ui_theme.cpp
using namespace eve::ui_editing;
using namespace eve::editing;
using eve::editor::LocalTransactionBackend;
using eve::editor::LocalWorldAuthority;

namespace {

Result<eve::editor::TransactionReceipt> commitTheme(UiThemeCatalogTarget& target, LocalTransactionBackend& transactions,
                                                    const DomainOperation& operation, const std::string& id) {
    eve::editor::TransactionSpec specification;
    specification.id           = eve::editor::TransactionId(id);
    specification.label        = "Edit theme";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<eve::editor::TransactionReceipt>(begun.code(), RuleId("test.ui-theme.begin"),
                                                                     "Could not begin theme transaction");
    auto appended = transactions.append(operation);
    if (!appended.ok()) {
        auto discarded = transactions.discard();
        if (!discarded.ok()) discarded.ignore("theme test transaction already inactive");
        return eve::editing::failed<eve::editor::TransactionReceipt>(appended.code(), RuleId("test.ui-theme.append"),
                                                                     "Could not append theme operation");
    }
    return transactions.commit();
}

SelectionSnapshot themeSelection(const UiThemeCatalogTarget& target, const char* id) {
    SelectionSnapshot selection;
    selection.channel = "ui-theme";
    SelectionItem item;
    item.domain = SelectionDomain::Asset;
    item.target = TargetId(target.targetId());
    item.item   = StableId(id);
    item.type   = "ui.theme";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

}  // namespace

TEST_CASE("editor.ui.theme_catalog_duplicate_recolor_and_undo_are_revision_safe") {
    UiThemeCatalogTarget    target("styles");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    CHECK_EQ(target.themes().size(), static_cast<std::size_t>(2));

    auto duplicated = target.makeDuplicate(ObjectId("dark"), ObjectId("studio"), "Studio");
    REQUIRE(duplicated.ok());
    REQUIRE(commitTheme(target, transactions, duplicated.value(), "theme.duplicate").ok());
    CHECK_EQ(target.themes().size(), static_cast<std::size_t>(3));

    const SelectionSnapshot selection = themeSelection(target, "studio");
    auto recolor = target.makeSet(selection, PropertyPath("color.button"), EditorValue::Array{0.1, 0.2, 0.3, 1.0},
                                  PropertySetMode::Absolute);
    REQUIRE(recolor.ok());
    REQUIRE(commitTheme(target, transactions, recolor.value(), "theme.recolor").ok());
    auto studio = target.theme(ObjectId("studio"));
    REQUIRE(studio.ok());
    CHECK(std::abs(studio.value().tokens.button[0] - 0.1f) < 0.0001f);
    REQUIRE(transactions.undo().ok());
    studio = target.theme(ObjectId("studio"));
    REQUIRE(studio.ok());
    CHECK(studio.value().tokens.button[0] > 0.15f);
}

TEST_CASE("editor.ui.theme_catalog_rejects_deleting_the_last_asset_and_loads_snapshots_atomically") {
    UiThemeCatalogTarget    target("styles");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    auto                    removeLight = target.makeDelete(ObjectId("light"));
    REQUIRE(removeLight.ok());
    REQUIRE(commitTheme(target, transactions, removeLight.value(), "theme.delete-light").ok());
    CHECK_EQ(static_cast<int>(target.makeDelete(ObjectId("dark")).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.themes().size(), static_cast<std::size_t>(1));

    UiThemeCatalogTarget    source("source");
    LocalWorldAuthority     sourceAuthority(&source);
    LocalTransactionBackend sourceTx(&sourceAuthority);
    auto                    copied = source.makeDuplicate(ObjectId("dark"), ObjectId("copy"), "Copy");
    REQUIRE(copied.ok());
    REQUIRE(commitTheme(source, sourceTx, copied.value(), "theme.copy").ok());

    UiThemeCatalogTarget restored("restored");
    REQUIRE(restored.loadSnapshot(source.snapshotValue()).ok());
    CHECK_EQ(restored.themes().size(), source.themes().size());

    EditorValue::Object invalid;
    invalid["schemaVersion"] = int64_t{1};
    invalid["content"] = EditorValue::Object{{"activeId", std::string("missing")}, {"themes", EditorValue::Array{}}};
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(EditorValue(std::move(invalid))).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(restored.themes().size(), source.themes().size());
}

TEST_CASE("editor.ui.theme_preview_rejects_stale_revision_and_publish_updates_global_theme") {
    UiThemeCatalogTarget  target("styles");
    UiThemePreviewService previews;
    const auto            current = previews.build(target, ObjectId("dark"), target.revision());
    CHECK_EQ(static_cast<int>(current.status), static_cast<int>(EditorStatus::Applied));
    const auto stale = previews.build(target, ObjectId("dark"), target.revision() + 1);
    CHECK_EQ(static_cast<int>(stale.status), static_cast<int>(EditorStatus::Conflict));

    const eve::ui::Theme previous     = eve::ui::globalTheme();
    const std::string    previousName = eve::ui::globalThemeName();
    REQUIRE(target.applyDomainOperation(target.makeSetActive(ObjectId("light")).value()).ok());
    REQUIRE(UiThemeRuntimePublisher().publish(target).ok());
    CHECK_EQ(eve::ui::globalThemeName(), std::string("light"));
    eve::ui::setGlobalTheme(previous, previousName);
}

// editor_ui_theme_script.cpp
TEST_CASE("editor.ui.theme_editor_script_composes_workspace_and_transactions") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.ui-theme", "Test Theme Editor");
        created <- eve.UiEditorModule().create("test.asset.themes");
        themeEditor <- created.value;
        configured <- themeEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx1 <- workspace.getPanelContext(1);
        createdCustom <- themeEditor.createFromPreset("studio", "Studio", "dark");
        beforeRounding <- themeEditor.getFloat("geometry.frameRounding");
        roundingResult <- themeEditor.setFloat("geometry.frameRounding", 9.0);
        afterRounding <- themeEditor.getFloat("geometry.frameRounding");
        undoResult <- themeEditor.undo();
        restoredRounding <- themeEditor.getFloat("geometry.frameRounding");
        themeCount <- themeEditor.getThemeCount();
        selected <- themeEditor.getSelectedId();
        state <- themeEditor.getState();
        stateSelected <- state.value.selectedId;
        stateCount <- state.value.themeCount;
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 3);
    CHECK_EQ(vm.find("panel0").toString(), std::string("ui.themes"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("ui.theme"));
    CHECK_EQ(vm.find("ctx1").toString(), std::string("preview"));
    CHECK(vm.find("createdCustom").toTable().get<bool>("ok"));
    CHECK(vm.find("roundingResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("afterRounding").toFloat() - 9.0F), 0.0001F);
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("restoredRounding").toFloat() - vm.find("beforeRounding").toFloat()), 0.0001F);
    CHECK_EQ(vm.find("themeCount").toInt(), 3);
    CHECK_EQ(vm.find("selected").toString(), std::string("studio"));
    CHECK(vm.find("state").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("stateSelected").toString(), std::string("studio"));
    CHECK_EQ(vm.find("stateCount").toInt(), 3);
}
}  // namespace test_editor_ui_document
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_ui_skin_preview)
#include "PrivateTestScope.h"
#include <map>
#include "ui/editor/EditorUiSkinPreview.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_ui_skin_preview.cpp
namespace test_editor_ui_skin_preview {
using namespace eve::editor;

namespace {
class SkinAssets final : public IUiSkinAssetResolver {
public:
    std::map<std::string, UiTextureMetadata> textures;
    std::map<std::string, bool>              fonts;

    Result<UiTextureMetadata> texture(const std::string& asset) const override {
        auto found = textures.find(asset);
        if (found == textures.end())
            return eve::editing::failed<UiTextureMetadata>(EditorStatus::NotFound, RuleId("test.ui.texture"),
                                                           "missing texture");
        return eve::editing::applied<UiTextureMetadata>(found->second);
    }
    Result<void> font(const std::string& asset) const override {
        if (!fonts.contains(asset))
            return eve::editing::failed<void>(EditorStatus::NotFound, RuleId("test.ui.font"), "missing font");
        return eve::editing::applied<void>();
    }
};
}  // namespace

TEST_CASE("editor.ui.skin_content_is_reversible_and_snapshot_v2_round_trips") {
    UiDocumentTarget document("hud");
    UiLayoutValue    layout;
    layout.width  = 200.0;
    layout.height = 100.0;
    auto create   = document.makeCreate({ObjectId("card"), {}, "image", "Card", layout});
    REQUIRE(create.ok());
    REQUIRE(document.applyDomainOperation(create.value()).ok());
    UiContentValue content;
    content.fontAsset       = "fonts/ui.ttf";
    content.fontSize        = 24.0;
    content.textureAsset    = "images/card.png";
    content.imageFit        = "cover";
    content.horizontalAlign = "center";
    content.verticalAlign   = "end";
    content.textR           = 0.25;
    auto set                = document.makeSetContent(ObjectId("card"), content);
    REQUIRE(set.ok());
    REQUIRE(document.applyDomainOperation(set.value()).ok());
    CHECK_EQ(document.widget(ObjectId("card")).value().content.textureAsset, std::string("images/card.png"));
    REQUIRE(set.value().hasInverse);
    DomainOperation undo = set.value();
    undo.payload         = set.value().inverse;
    REQUIRE(document.applyDomainOperation(undo).ok());
    CHECK(document.widget(ObjectId("card")).value().content.textureAsset.empty());
    REQUIRE(document.applyDomainOperation(set.value()).ok());

    UiDocumentTarget restored("copy");
    REQUIRE(restored.loadSnapshot(document.snapshotValue()).ok());
    CHECK_EQ(restored.widget(ObjectId("card")).value().content.fontSize, 24.0);
}

TEST_CASE("editor.ui.skin_preview_resolves_assets_crops_cover_and_clips_to_parent") {
    UiDocumentTarget document("hud");
    UiLayoutValue    rootLayout;
    rootLayout.width  = 100.0;
    rootLayout.height = 80.0;
    auto root         = document.makeCreate({ObjectId("root"), {}, "panel", "Root", rootLayout});
    REQUIRE(root.ok());
    REQUIRE(document.applyDomainOperation(root.value()).ok());
    UiLayoutValue childLayout;
    childLayout.x      = 80.0;
    childLayout.y      = 10.0;
    childLayout.width  = 60.0;
    childLayout.height = 40.0;
    auto child         = document.makeCreate({ObjectId("hero"), ObjectId("root"), "image", "Hero", childLayout});
    REQUIRE(child.ok());
    REQUIRE(document.applyDomainOperation(child.value()).ok());
    UiContentValue content;
    content.textureAsset = "hero.png";
    content.imageFit     = "cover";
    auto skin            = document.makeSetContent(ObjectId("hero"), content);
    REQUIRE(skin.ok());
    REQUIRE(document.applyDomainOperation(skin.value()).ok());

    SkinAssets assets;
    assets.textures["hero.png"] = {200.0, 100.0};
    UiDocumentPreviewService layouts;
    const auto               preview = layouts.build(document, 100.0, 80.0);
    const auto               plan    = UiSkinPreviewPlanner().build(document, preview, assets);
    CHECK_EQ(static_cast<int>(plan.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE_EQ(plan.commands.size(), static_cast<std::size_t>(1));
    CHECK(plan.commands[0].u0 > 0.0);
    CHECK_EQ(plan.commands[0].clipWidth, 20.0);

    SkinAssets missing;
    const auto rejected = UiSkinPreviewPlanner().build(document, preview, missing);
    CHECK_EQ(static_cast<int>(rejected.status), static_cast<int>(EditorStatus::Rejected));
    CHECK(!rejected.diagnostics.empty());
}
}  // namespace test_editor_ui_skin_preview
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_voxel_catalog)
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"
#include "voxel/CubeTypeRegistry.h"
#include "voxel/editing/VoxelCatalog.h"
#include "voxel/editing/VoxelPaletteTarget.h"
#include "voxel/editor/VoxelCatalogEditor.h"
#include "voxel/editor/VoxelEditorModule.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_voxel_catalog.cpp
namespace test_editor_voxel_catalog {
// editor_voxel_catalog.cpp
using eve::voxel_editing::DomainOperation;
using eve::voxel_editing::ObjectId;
using eve::voxel_editing::Result;
using eve::voxel_editing::VoxelCatalogTarget;
using eve::voxel_editing::VoxelCellFill;
using eve::voxel_editing::VoxelModelValue;
using eve::voxel_editing::VoxelSocketKind;

namespace {
void apply(VoxelCatalogTarget& target, Result<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}

VoxelModelValue cube(const std::string& id, int n) {
    VoxelModelValue model;
    model.id    = ObjectId(id);
    model.name  = id;
    model.sizeX = n;
    model.sizeY = n;
    model.sizeZ = n;
    for (int z = 0; z < n; ++z)
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) model.voxels.push_back({x, y, z});
    return model;
}
}  // namespace

TEST_CASE("editor.voxel.catalog_classifies_filled_and_partial_occupancy") {
    VoxelCatalogTarget target("project");
    apply(target, target.makeCreateModel(cube("block", 4)));
    CHECK_EQ(eve::voxel_editing::voxelClassifyModelFill(*target.findModel(ObjectId("block"))), VoxelCellFill::Filled);

    VoxelModelValue bed;
    bed.id    = ObjectId("bed");
    bed.name  = "bed";
    bed.sizeX = 8;
    bed.sizeY = 4;
    bed.sizeZ = 6;
    for (int x = 0; x < 8; ++x)
        for (int z = 0; z < 6; ++z) bed.voxels.push_back({x, 0, z});
    apply(target, target.makeCreateModel(bed));
    CHECK_EQ(eve::voxel_editing::voxelClassifyModelFill(*target.findModel(ObjectId("bed"))), VoxelCellFill::Partial);

    const auto pick =
        eve::voxel_editing::pickVoxelModel(*target.findModel(ObjectId("block")), -2.f, 1.5f, 1.5f, 1.f, 0.f, 0.f, 16.f);
    CHECK(pick.hit);
    CHECK(pick.canAttach);
    CHECK_EQ(pick.hitX, 0);
    CHECK_EQ(pick.prevX, -1);
}

TEST_CASE("editor.voxel.catalog_snapshot_roundtrip_preserves_occupancy") {
    VoxelCatalogTarget target("project");
    apply(target, target.makeCreateModel(cube("block", 2)));
    VoxelCatalogTarget restored("copy");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    REQUIRE(restored.findModel(ObjectId("block")));
    CHECK_EQ(restored.findModel(ObjectId("block"))->voxels.size(), static_cast<std::size_t>(8));
}

TEST_CASE("editor.voxel.catalog_join_sockets_match_opposite_faces") {
    CHECK(eve::voxel_editing::canJoinVoxelSockets({"bed-rail", VoxelSocketKind::Male},
                                                  {"bed-rail", VoxelSocketKind::Female}));
    VoxelCatalogTarget target("project");
    VoxelModelValue    head = cube("head", 2);
    head.sockets[0]         = {"bed-rail", VoxelSocketKind::Male};
    VoxelModelValue foot    = cube("foot", 2);
    foot.sockets[1]         = {"bed-rail", VoxelSocketKind::Female};
    apply(target, target.makeCreateModel(head));
    apply(target, target.makeCreateModel(foot));
    const auto partners = target.hullJoinPartners(ObjectId("head"), 0);
    REQUIRE_EQ(partners.size(), static_cast<std::size_t>(1));
    CHECK_EQ(partners.front().value(), std::string("foot"));
}

TEST_CASE("editor.voxel.catalog_empty_canvas_attaches_first_in_bounds_cell") {
    VoxelModelValue empty;
    empty.id        = ObjectId("empty");
    empty.name      = "empty";
    empty.sizeX     = 4;
    empty.sizeY     = 4;
    empty.sizeZ     = 4;
    const auto pick = eve::voxel_editing::pickVoxelModel(empty, -2.f, 1.5f, 1.5f, 1.f, 0.f, 0.f, 16.f);
    CHECK(!pick.hit);
    CHECK(pick.canAttach);
    CHECK_EQ(pick.prevX, 0);
    CHECK_EQ(pick.prevY, 1);
    CHECK_EQ(pick.prevZ, 1);
}

// editor_voxel_editor.cpp
using eve::editor::Editor;

TEST_CASE("editor.voxel.catalog_automation_owns_headless_targets") {
    Editor                               editor;
    eve::voxel_editor::VoxelEditorModule adapter;
    auto*                                automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("voxel.model.create.v1") != std::string::npos);
    CHECK(commands.find("voxel.model.voxel.set.v1") != std::string::npos);

    const std::string created =
        automation->invoke("target-create", R"({"target":"agent.voxel","type":"voxel-catalog"})");
    REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);

    const std::string model = automation->invoke(
        "execute",
        R"({"target":"agent.voxel","command":"voxel.model.create.v1","payload":{"id":"brick","name":"brick","sizeX":4,"sizeY":4,"sizeZ":4}})");
    REQUIRE(model.find("\"status\":\"applied\"") != std::string::npos);

    const std::string voxel = automation->invoke(
        "execute",
        R"({"target":"agent.voxel","command":"voxel.model.voxel.set.v1","payload":{"item":"brick","x":1,"y":2,"z":3,"occupied":true}})");
    REQUIRE(voxel.find("\"status\":\"applied\"") != std::string::npos);

    const std::string inspected = automation->invoke("inspect", R"({"target":"agent.voxel"})");
    CHECK(inspected.find("voxel-catalog") != std::string::npos);

    const std::string undone = automation->invoke("undo", R"({"target":"agent.voxel"})");
    CHECK(undone.find("\"status\":\"applied\"") != std::string::npos);

    REQUIRE(automation->invoke("target-close", R"({"target":"agent.voxel"})").find("\"status\":\"applied\"") !=
            std::string::npos);
}

TEST_CASE("editor.voxel.sculpt_editor_attaches_inside_canvas_and_undoes") {
    eve::voxel_editor::VoxelCatalogEditor editor("asset.preview.sculpt");
    CHECK_EQ(editor.modelSizeX(), 8);
    CHECK_EQ(editor.voxelCount(), 64);
    REQUIRE(editor.setVoxel(1, 2, 2, true).ok());
    CHECK_EQ(editor.voxelCount(), 65);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.voxelCount(), 64);
    REQUIRE(editor.setViewport(640.f, 400.f).ok());
    REQUIRE(editor.setTool("erase").ok());
    REQUIRE(editor.pointerDown(320.f, 200.f).ok());
    CHECK_EQ(editor.voxelCount(), 63);
    REQUIRE(editor.undo().ok());
    CHECK_EQ(editor.voxelCount(), 64);
    REQUIRE(editor.setTool("erase").ok());
    REQUIRE(editor.pointerWorldRay(-2.f, 4.f, 4.f, 1.f, 0.f, 0.f).ok());
    CHECK_EQ(editor.voxelCount(), 63);
}

// editor_voxel_palette.cpp
using namespace eve::voxel_editing;
using namespace eve::editing;
namespace {
void apply(VoxelPaletteTarget& t, Result<DomainOperation> op) {
    REQUIRE(op.ok());
    REQUIRE(t.applyDomainOperation(op.value()).ok());
}
VoxelPaletteEntryValue cube(const std::string& id, bool directional = false) {
    VoxelPaletteEntryValue v;
    v.id               = ObjectId("editor-" + id);
    v.type.name        = id;
    v.type.directional = directional;
    for (int i = 0; i < 6; ++i) v.type.faceTex[i] = static_cast<std::uint8_t>(i + 1);
    return v;
}
SelectionSnapshot select(const VoxelPaletteTarget& t, const char* id) {
    SelectionSnapshot s;
    s.channel = "voxel-palette";
    s.items.push_back({SelectionDomain::Asset, TargetId(t.targetId()), StableId(id), "voxel.cube-type"});
    return s;
}
}  // namespace
TEST_CASE("editor.voxel_palette.face_inspector_is_reversible_and_persistent") {
    VoxelPaletteTarget target("blocks");
    apply(target, target.makeCreate(cube("furnace", true)));
    const auto         selection = select(target, "editor-furnace");
    EditorValue::Array faces{int64_t{9}, int64_t{8}, int64_t{7}, int64_t{6}, int64_t{5}, int64_t{4}};
    auto               edit = target.makeSet(selection, PropertyPath("cube.faces"), faces, PropertySetMode::Absolute);
    REQUIRE(edit.ok());
    REQUIRE(target.applyDomainOperation(edit.value()).ok());
    CHECK_EQ(int(target.entries().front().type.faceTex[0]), 9);
    DomainOperation undo = edit.value();
    undo.payload         = edit.value().inverse;
    REQUIRE(target.applyDomainOperation(undo).ok());
    CHECK_EQ(int(target.entries().front().type.faceTex[0]), 1);
    VoxelPaletteTarget restored("copy");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    CHECK(restored.entries().front().type.directional);
}
TEST_CASE("editor.voxel_palette.enforces_variant_capacity_before_runtime") {
    VoxelPaletteTarget target("blocks");
    for (int i = 0; i < 63; ++i) apply(target, target.makeCreate(cube("directional-" + std::to_string(i), true)));
    CHECK_EQ(static_cast<int>(target.makeCreate(cube("overflow", true)).code()),
             static_cast<int>(EditorStatus::Rejected));
    apply(target, target.makeCreate(cube("single", false)));
    CHECK_EQ(target.entries().size(), static_cast<std::size_t>(64));
}
TEST_CASE("editor.voxel_palette.publication_is_atomic_and_reports_variant_ids") {
    VoxelPaletteTarget target("blocks");
    apply(target, target.makeCreate(cube("stone")));
    apply(target, target.makeCreate(cube("furnace", true)));
    VoxelPaletteRuntime runtime;
    auto                result = runtime.publish(target);
    REQUIRE(result.ok());
    CHECK_EQ(result.value().size(), static_cast<std::size_t>(2));
    CHECK_EQ(result.value()[0].baseId, 1);
    CHECK_EQ(result.value()[1].baseId, 2);
    CHECK_EQ(result.value()[1].variants, 4);
    REQUIRE(runtime.registry());
    CHECK_EQ(int(runtime.registry()->variantId("furnace", 3)), 5);
    CHECK(runtime.registry()->find("furnace") != nullptr);
}
TEST_CASE("editor.voxel_palette.invalid_snapshot_does_not_partially_replace") {
    VoxelPaletteTarget target("blocks");
    apply(target, target.makeCreate(cube("stone")));
    const auto  before  = target.snapshotValue();
    EditorValue invalid = before;
    auto*       root    = invalid.getIf<EditorValue::Object>();
    auto*       content = (*root)["content"].getIf<EditorValue::Object>();
    auto*       entries = (*content)["entries"].getIf<EditorValue::Array>();
    auto*       entry   = (*entries)[0].getIf<EditorValue::Object>();
    auto*       faces   = (*entry)["faces"].getIf<EditorValue::Array>();
    (*faces)[0]         = int64_t{999};
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
}
}  // namespace test_editor_voxel_catalog
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editor_workspace_layout_shell)
#include "PrivateTestScope.h"
#include "editor/EditorWorkspace.h"
#include "ui/UIHost.h"
#include "ui/Widget.h"
#include "ui/editor/WorkspaceLayoutShell.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editor_workspace_layout_shell.cpp
namespace test_editor_workspace_layout_shell {
using eve::editor::EditorWorkspace;
using eve::editor::WorkspacePanelDescriptor;
using eve::ui::NodeType;
using eve::ui::UIHost;
using eve::ui_editor::WorkspaceLayoutShell;
using eve::ui_editor::WorkspaceShellOptions;

namespace {

const eve::ui::UINode* findById(const eve::ui::UIHost::Tree& tree, const std::string& id) {
    for (const auto& node : tree.nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

bool hasType(const eve::ui::UIHost::Tree& tree, NodeType type) {
    for (const auto& node : tree.nodes) {
        if (node.type == type) return true;
    }
    return false;
}

int countType(const eve::ui::UIHost::Tree& tree, NodeType type) {
    int count = 0;
    for (const auto& node : tree.nodes) {
        if (node.type == type) ++count;
    }
    return count;
}

}  // namespace

TEST_CASE("editor.ui.layout.widgets_compose_fixed_chrome") {
    auto root = eve::ui::window(
        "Kit",
        {eve::ui::appFrame(
            eve::ui::splitPane(
                eve::ui::FlexDirection::Row,
                eve::ui::dockArea("left", {eve::ui::panelFrame("Assets", {eve::ui::text("list", "list")}, "assets")}),
                eve::ui::dockArea(
                    "center",
                    {eve::ui::tabContainer(
                        {eve::ui::panelFrame("Viewport", {eve::ui::text("view", "view")}, "viewport")
                             .withText("Viewport"),
                         eve::ui::panelFrame("Graph", {eve::ui::text("graph", "graph")}, "graph").withText("Graph")},
                        "viewport", "center-tabs")}),
                0.25f, "main-split"),
            eve::ui::toolbar({eve::ui::button("Save", "save")}, "toolbar"),
            eve::ui::statusBar({eve::ui::text("Ready", "ready")}, "status"), "frame")},
        "root");

    CHECK_EQ(root.type, NodeType::Window);
    REQUIRE_EQ(root.children.size(), 1u);
    CHECK_EQ(root.children[0].type, NodeType::AppFrame);

    auto host = UIHost::resolve(UIHost::createHost("layout-widgets"));
    REQUIRE(host);
    host->get().setTree(std::move(root));
    const auto& tree = *host->get().tree();
    CHECK(hasType(tree, NodeType::DockArea));
    CHECK(hasType(tree, NodeType::TabContainer));
    CHECK(hasType(tree, NodeType::PanelFrame));
    CHECK(hasType(tree, NodeType::AppFrame));
}

TEST_CASE("editor.ui.layout.workspace_shell_maps_regions_to_dock_areas") {
    EditorWorkspace workspace("demo", "Demo Editor");
    REQUIRE(workspace
                .registerPanel(WorkspacePanelDescriptor{.id         = "assets",
                                                        .title      = "Assets",
                                                        .region     = "left",
                                                        .capability = "demo",
                                                        .context    = "list",
                                                        .order      = 0})
                .ok());
    REQUIRE(workspace
                .registerPanel(WorkspacePanelDescriptor{.id         = "viewport",
                                                        .title      = "Viewport",
                                                        .region     = "center",
                                                        .capability = "demo",
                                                        .context    = "preview",
                                                        .order      = 0})
                .ok());
    REQUIRE(workspace
                .registerPanel(WorkspacePanelDescriptor{.id         = "inspector",
                                                        .title      = "Inspector",
                                                        .region     = "right",
                                                        .capability = "demo",
                                                        .context    = "inspector",
                                                        .order      = 0})
                .ok());
    REQUIRE(workspace
                .registerPanel(WorkspacePanelDescriptor{.id         = "timeline",
                                                        .title      = "Timeline",
                                                        .region     = "bottom",
                                                        .capability = "demo",
                                                        .context    = "timeline",
                                                        .order      = 0})
                .ok());
    REQUIRE(workspace
                .registerPanel(WorkspacePanelDescriptor{.id         = "history",
                                                        .title      = "History",
                                                        .region     = "bottom",
                                                        .capability = "demo",
                                                        .context    = "history",
                                                        .order      = 1})
                .ok());
    REQUIRE(workspace.activatePanel("viewport"));

    CHECK_EQ(WorkspaceLayoutShell::panelsInRegion(workspace, "bottom").size(), 2u);
    CHECK_EQ(WorkspaceLayoutShell::panelsInRegion(workspace, "left").size(), 1u);
    CHECK_EQ(WorkspaceLayoutShell::panelsInRegion(workspace, "floating").size(), 0u);

    WorkspaceShellOptions options;
    options.toolbar   = eve::ui::toolbar({eve::ui::text("Demo", "title")}, "toolbar");
    options.statusBar = eve::ui::statusBar({eve::ui::text("ok", "ok")}, "status");
    auto treeDesc     = WorkspaceLayoutShell::buildWindow(
        workspace,
        [](const WorkspacePanelDescriptor& panel) {
            return std::vector<eve::ui::WidgetDesc>{eve::ui::text(panel.context, panel.id + "/body")};
        },
        options);

    auto host = UIHost::resolve(UIHost::createHost("layout-shell"));
    REQUIRE(host);
    host->get().setTree(std::move(treeDesc));
    const auto& tree = *host->get().tree();

    CHECK(hasType(tree, NodeType::AppFrame));
    CHECK(hasType(tree, NodeType::Toolbar));
    CHECK(hasType(tree, NodeType::StatusBar));
    CHECK_EQ(countType(tree, NodeType::DockArea), 4);
    CHECK(hasType(tree, NodeType::TabContainer));
    CHECK(findById(tree, "assets") != nullptr);
    CHECK(findById(tree, "viewport") != nullptr);
    CHECK(findById(tree, "inspector") != nullptr);
    const auto* tabs = findById(tree, "bottom/tabs");
    REQUIRE(tabs != nullptr);
    CHECK_EQ(tabs->type, NodeType::TabContainer);
    // Active workspace panel is center; bottom tab stack defaults to first bottom panel.
    CHECK_EQ(tabs->valueText, std::string("timeline"));
}

TEST_CASE("editor.ui.layout.workspace_shell_single_panel_skips_tabs") {
    EditorWorkspace workspace("single", "Single");
    REQUIRE(
        workspace.registerPanel(WorkspacePanelDescriptor{.id = "only", .title = "Only", .region = "center", .order = 0})
            .ok());
    auto desc = WorkspaceLayoutShell::buildDockBody(workspace);
    auto host = UIHost::resolve(UIHost::createHost("layout-single"));
    REQUIRE(host);
    host->get().setTree(eve::ui::window("Single", {std::move(desc)}, "root"));
    const auto& tree = *host->get().tree();
    CHECK_EQ(countType(tree, NodeType::TabContainer), 0);
    CHECK(findById(tree, "only") != nullptr);
    CHECK_EQ(findById(tree, "only")->type, NodeType::PanelFrame);
}
}  // namespace test_editor_workspace_layout_shell
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_effects_domain_adapters)
#include "PrivateTestScope.h"
#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include "card/CardEffects.h"
#include "rts/RTSEffects.h"
#include "vehicle/VehicleEffects.h"
#include "weapon/WeaponEffects.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// effects_domain_adapters.cpp
namespace test_effects_domain_adapters {
namespace {

eve::SubjectRef subject() {
    eve::PersistentId::Bytes bytes{};
    bytes[0]  = 0x42;
    bytes[15] = 0x17;
    return eve::SubjectRef::fromPersistentId(eve::PersistentId(bytes));
}

eve::SimulationStep step(std::uint64_t tick, double seconds) {
    auto delta = eve::Duration::fromSeconds(seconds);
    REQUIRE(delta.ok());
    return {eve::SimulationTick(tick), std::move(delta).takeValue()};
}

eve::effects::EffectPolicy oncePolicy() {
    eve::effects::EffectPolicy policy;
    policy.stackMode  = eve::effects::StackMode::NewInstance;
    policy.stackCount = eve::effects::StackCountPolicy::Keep;
    policy.maxStacks  = 1;
    return policy;
}

}  // namespace

TEST_CASE("effects.domainAdapters.shareLifecycleAndPeriodicParity") {
    eve::card::CardEffectAdapter       card;
    eve::rts::RTSEffectAdapter         rts;
    eve::vehicle::VehicleEffectAdapter vehicle;
    eve::weapon::WeaponEffectAdapter   weapon;

    eve::card::CardEffectDefinition cardDefinition;
    cardDefinition.id        = "periodic.damage";
    cardDefinition.source    = "spell.card";
    cardDefinition.duration  = 3.0;
    cardDefinition.period    = 1.0;
    cardDefinition.magnitude = 5.0;
    cardDefinition.policy    = oncePolicy();
    auto cardHandle          = card.apply(cardDefinition, subject());
    REQUIRE(cardHandle.ok());

    eve::rts::RTSEffectDefinition rtsDefinition;
    rtsDefinition.id        = cardDefinition.id;
    rtsDefinition.source    = cardDefinition.source;
    rtsDefinition.duration  = cardDefinition.duration;
    rtsDefinition.period    = cardDefinition.period;
    rtsDefinition.magnitude = cardDefinition.magnitude;
    rtsDefinition.policy    = cardDefinition.policy;
    auto rtsHandle          = rts.apply(rtsDefinition, subject());
    REQUIRE(rtsHandle.ok());

    eve::vehicle::VehicleEffectDefinition vehicleDefinition;
    vehicleDefinition.id        = cardDefinition.id;
    vehicleDefinition.source    = cardDefinition.source;
    vehicleDefinition.duration  = cardDefinition.duration;
    vehicleDefinition.period    = cardDefinition.period;
    vehicleDefinition.magnitude = cardDefinition.magnitude;
    vehicleDefinition.policy    = cardDefinition.policy;
    vehicleDefinition.armorZone = "side";
    auto vehicleHandle          = vehicle.apply(vehicleDefinition, subject());
    REQUIRE(vehicleHandle.ok());

    eve::weapon::WeaponEffectDefinition weaponDefinition;
    weaponDefinition.id        = cardDefinition.id;
    weaponDefinition.source    = cardDefinition.source;
    weaponDefinition.duration  = cardDefinition.duration;
    weaponDefinition.period    = cardDefinition.period;
    weaponDefinition.magnitude = cardDefinition.magnitude;
    weaponDefinition.policy    = cardDefinition.policy;
    auto weaponHandle          = weapon.apply(weaponDefinition, subject());
    REQUIRE(weaponHandle.ok());

    const auto checkCommon = [&](const auto& adapter, const auto& handle) {
        auto resolved = adapter.resolve(handle);
        REQUIRE(resolved.ok());
        REQUIRE(resolved.value() != nullptr);
        CHECK_EQ(resolved.value()->subject, subject().format());
        CHECK_EQ(resolved.value()->source, std::string("spell.card"));
        CHECK_EQ(resolved.value()->period, 1.0);
        CHECK_EQ(resolved.value()->duration, 3.0);
        CHECK_EQ(resolved.value()->stackCount, 1u);
    };
    checkCommon(card, std::move(cardHandle).takeValue());
    checkCommon(rts, std::move(rtsHandle).takeValue());
    checkCommon(vehicle, std::move(vehicleHandle).takeValue());
    checkCommon(weapon, std::move(weaponHandle).takeValue());

    auto cardUpdate    = card.advance(step(1, 1.0));
    auto rtsUpdate     = rts.advance(step(1, 1.0));
    auto vehicleUpdate = vehicle.advance(step(1, 1.0));
    auto weaponUpdate  = weapon.advance(step(1, 1.0));
    REQUIRE(cardUpdate.ok());
    REQUIRE(rtsUpdate.ok());
    REQUIRE(vehicleUpdate.ok());
    REQUIRE(weaponUpdate.ok());
    CHECK_EQ(std::move(cardUpdate).takeValue().settled, 1u);
    CHECK_EQ(std::move(rtsUpdate).takeValue().settled, 1u);
    CHECK_EQ(std::move(vehicleUpdate).takeValue().settled, 1u);
    CHECK_EQ(std::move(weaponUpdate).takeValue().settled, 1u);
    CHECK_EQ(card.target().health, 95);
    CHECK_EQ(rts.target().morale, 95.0);
    CHECK_EQ(vehicle.target().hull, 95.0);
    CHECK_EQ(weapon.target().heat, 5.0);
}

TEST_CASE("effects.domainAdapters.failureIsAtomicAndRestoreMakesHandlesStale") {
    eve::card::CardEffectAdapter       card;
    eve::rts::RTSEffectAdapter         rts;
    eve::vehicle::VehicleEffectAdapter vehicle;
    eve::weapon::WeaponEffectAdapter   weapon;

    eve::card::CardEffectDefinition cardDefinition;
    cardDefinition.id        = "failure.damage";
    cardDefinition.period    = 1.0;
    cardDefinition.magnitude = -2.0;
    cardDefinition.policy    = oncePolicy();
    auto cardHandle          = card.apply(cardDefinition, subject());
    REQUIRE(cardHandle.ok());
    const auto cardReference = std::move(cardHandle).takeValue();
    const auto cardHealth    = card.target().health;
    auto       cardFailed    = card.advance(step(1, 1.0));
    CHECK(!cardFailed.ok());
    CHECK_EQ(card.target().health, cardHealth);
    CHECK(card.resolve(cardReference).ok());

    eve::rts::RTSEffectDefinition rtsDefinition;
    rtsDefinition.id        = "failure.morale";
    rtsDefinition.period    = 1.0;
    rtsDefinition.magnitude = -2.0;
    rtsDefinition.policy    = oncePolicy();
    auto rtsHandle          = rts.apply(rtsDefinition, subject());
    REQUIRE(rtsHandle.ok());
    const auto rtsReference = std::move(rtsHandle).takeValue();
    const auto rtsMorale    = rts.target().morale;
    auto       rtsFailed    = rts.advance(step(1, 1.0));
    CHECK(!rtsFailed.ok());
    CHECK_EQ(rts.target().morale, rtsMorale);
    CHECK(rts.resolve(rtsReference).ok());

    eve::vehicle::VehicleEffectDefinition vehicleDefinition;
    vehicleDefinition.id        = "failure.hull";
    vehicleDefinition.period    = 1.0;
    vehicleDefinition.magnitude = -2.0;
    vehicleDefinition.policy    = oncePolicy();
    auto vehicleHandle          = vehicle.apply(vehicleDefinition, subject());
    REQUIRE(vehicleHandle.ok());
    const auto vehicleReference = std::move(vehicleHandle).takeValue();
    const auto vehicleHull      = vehicle.target().hull;
    auto       vehicleFailed    = vehicle.advance(step(1, 1.0));
    CHECK(!vehicleFailed.ok());
    CHECK_EQ(vehicle.target().hull, vehicleHull);
    CHECK(vehicle.resolve(vehicleReference).ok());

    eve::weapon::WeaponEffectDefinition weaponDefinition;
    weaponDefinition.id        = "failure.heat";
    weaponDefinition.period    = 1.0;
    weaponDefinition.magnitude = -2.0;
    weaponDefinition.policy    = oncePolicy();
    auto weaponHandle          = weapon.apply(weaponDefinition, subject());
    REQUIRE(weaponHandle.ok());
    const auto weaponReference = std::move(weaponHandle).takeValue();
    const auto weaponHeat      = weapon.target().heat;
    auto       weaponFailed    = weapon.advance(step(1, 1.0));
    CHECK(!weaponFailed.ok());
    CHECK_EQ(weapon.target().heat, weaponHeat);
    CHECK(weapon.resolve(weaponReference).ok());

    const auto snapshot = weapon.snapshot();
    auto       extra    = weapon.apply(weaponDefinition, subject());
    REQUIRE(extra.ok());
    const auto stale = std::move(extra).takeValue();
    REQUIRE(weapon.restore(snapshot).ok());
    CHECK(!weapon.resolve(stale).ok());
    CHECK_EQ(static_cast<int>(weapon.resolve(stale).code()), static_cast<int>(eve::StatusCode::Rejected));
    CHECK(!weapon.resolve(weaponReference).ok());
}

TEST_CASE("effects.domainAdapters.keepDomainStrategies") {
    eve::card::CardEffectAdapter    card;
    eve::card::CardEffectDefinition shield;
    shield.id        = "card.shield";
    shield.magnitude = 8.0;
    shield.kind      = eve::card::CardEffectKind::Shield;
    shield.policy    = oncePolicy();
    REQUIRE(card.apply(shield, subject()).ok());
    CHECK_EQ(card.target().barrier, 8);

    eve::rts::RTSEffectAdapter    rts;
    eve::rts::RTSEffectDefinition lock;
    lock.id     = "rts.production-lock";
    lock.kind   = eve::rts::RTSEffectKind::ProductionLock;
    lock.policy = oncePolicy();
    REQUIRE(rts.apply(lock, subject()).ok());
    CHECK(rts.target().productionLocked);

    eve::vehicle::VehicleEffectAdapter    vehicle;
    eve::vehicle::VehicleEffectDefinition hit;
    hit.id        = "vehicle.rear-hit";
    hit.armorZone = "rear";
    hit.period    = 1.0;
    hit.magnitude = 10.0;
    hit.policy    = oncePolicy();
    REQUIRE(vehicle.apply(hit, subject()).ok());
    REQUIRE(vehicle.advance(step(1, 1.0)).ok());
    CHECK_EQ(vehicle.target().hull, 87.5);
    CHECK_EQ(vehicle.target().criticalHits, 0u);

    eve::weapon::WeaponEffectAdapter    weapon;
    eve::weapon::WeaponEffectDefinition heat;
    heat.id        = "weapon.overheat";
    heat.period    = 1.0;
    heat.duration  = 2.0;
    heat.magnitude = 60.0;
    heat.policy    = oncePolicy();
    REQUIRE(weapon.apply(heat, subject()).ok());
    REQUIRE(weapon.advance(step(1, 1.0)).ok());
    REQUIRE(weapon.advance(step(2, 1.0)).ok());
    CHECK(weapon.target().jammed);
    CHECK_EQ(weapon.target().blockedShots, 1u);
}

TEST_CASE("effects.card.immediateDamageAndHealUseSettlementWithoutAdvancingPeriodicEffects") {
    eve::card::CardEffectAdapter card;
    REQUIRE(card.initializeTarget({50, 100, 0, 0}).ok());

    eve::settlement::SettlementRule resistance;
    resistance.id                  = "card.immediate.resistance";
    resistance.stage               = eve::settlement::StageKind::TargetMitigation;
    resistance.operation           = eve::settlement::RuleOperation::ResistPercent;
    resistance.value               = 0.5;
    resistance.filter.kinds        = {"damage"};
    resistance.filter.requiredTags = {"card:damage"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({resistance}).ok());
    REQUIRE(card.configureSettlementRules(rules).ok());

    eve::card::CardEffectDefinition damage;
    damage.id        = "card.immediate.damage";
    damage.magnitude = 20.0;
    damage.policy    = oncePolicy();
    REQUIRE(card.apply(damage, subject()).ok());
    CHECK_EQ(card.target().health, 40);

    eve::card::CardEffectDefinition heal;
    heal.id        = "card.immediate.heal";
    heal.kind      = eve::card::CardEffectKind::Heal;
    heal.magnitude = 10.0;
    heal.policy    = oncePolicy();
    REQUIRE(card.apply(heal, subject()).ok());
    CHECK_EQ(card.target().health, 50);

    damage.id       = "card.periodic.not-immediate";
    damage.duration = 2.0;
    damage.period   = 1.0;
    REQUIRE(card.apply(damage, subject()).ok());
    CHECK_EQ(card.target().health, 50);
    auto advanced = card.advance(step(1, 1.0));
    REQUIRE(advanced.ok());
    CHECK_EQ(card.target().health, 40);
}

TEST_CASE("effects.card.periodicDamageUsesSharedSettlementRules") {
    eve::card::CardEffectAdapter    card;
    eve::settlement::SettlementRule resistance;
    resistance.id                  = "card.ward";
    resistance.source              = "card:ward";
    resistance.stage               = eve::settlement::StageKind::TargetMitigation;
    resistance.operation           = eve::settlement::RuleOperation::ResistPercent;
    resistance.value               = 0.5;
    resistance.filter.kinds        = {"damage"};
    resistance.filter.requiredTags = {"card:damage"};
    eve::settlement::SettlementRuleSet rules;
    REQUIRE(rules.configure({resistance}).ok());
    REQUIRE(card.configureSettlementRules(rules).ok());

    eve::card::CardEffectDefinition damage;
    damage.id        = "card.burn";
    damage.period    = 1.0;
    damage.magnitude = 20.0;
    damage.policy    = oncePolicy();
    REQUIRE(card.apply(damage, subject()).ok());
    auto update = card.advance(step(1, 1.0));
    REQUIRE(update.ok());
    CHECK_EQ(card.target().health, 90);
}

TEST_CASE("effects.card.deathTransitionFiresOnlyOnTheAliveToDeadEdge") {
    eve::card::CardEffectAdapter card;
    REQUIRE(card.initializeTarget({10, 10, 0, 0}).ok());

    eve::card::CardEffectDefinition damage;
    damage.id        = "card.lethal-dot";
    damage.duration  = 3.0;
    damage.period    = 1.0;
    damage.magnitude = 20.0;
    damage.policy    = oncePolicy();
    REQUIRE(card.apply(damage, subject()).ok());

    auto lethal = card.advance(step(1, 1.0));
    REQUIRE(lethal.ok());
    CHECK(lethal.value().deathTriggered);
    CHECK_EQ(card.target().deathTriggers, 1u);

    auto afterDeath = card.advance(step(2, 1.0));
    REQUIRE(afterDeath.ok());
    CHECK(!afterDeath.value().deathTriggered);
    CHECK_EQ(card.target().deathTriggers, 1u);
}

TEST_CASE("effects.card.activeRuleProjectionTracksRemoveAndRestore") {
    eve::card::CardEffectAdapter card;

    eve::card::CardEffectDefinition ward;
    ward.id       = "card.ward.active";
    ward.duration = 10.0;
    ward.kind     = eve::card::CardEffectKind::Heal;
    ward.policy   = oncePolicy();
    REQUIRE(ward.payload
                .setJson("settlement.rule",
                         R"({"stage":"target_mitigation","operation":"resist_percent","value":0.5,"kinds":["damage"]})")
                .ok());
    auto wardHandle = card.apply(ward, subject());
    REQUIRE(wardHandle.ok());

    eve::card::CardEffectDefinition burn;
    burn.id        = "card.burn.projected";
    burn.duration  = 10.0;
    burn.period    = 1.0;
    burn.magnitude = 20.0;
    burn.policy    = oncePolicy();
    REQUIRE(card.apply(burn, subject()).ok());

    REQUIRE(card.advance(step(1, 1.0)).ok());
    CHECK_EQ(card.target().health, 90);
    const auto snapshot = card.snapshot();

    REQUIRE(card.remove(wardHandle.value()).ok());
    REQUIRE(card.advance(step(2, 1.0)).ok());
    CHECK_EQ(card.target().health, 70);

    REQUIRE(card.restore(snapshot).ok());
    CHECK(!card.resolve(wardHandle.value()).ok());
    REQUIRE(card.advance(step(2, 1.0)).ok());
    CHECK_EQ(card.target().health, 80);
}

TEST_CASE("effects.card.activeRuleProjectionTracksStacksAndExpiry") {
    eve::card::CardEffectAdapter card;

    eve::card::CardEffectDefinition ward;
    ward.id                = "card.ward.stacking";
    ward.duration          = 5.0;
    ward.kind              = eve::card::CardEffectKind::Heal;
    ward.policy.stackMode  = eve::effects::StackMode::Accumulate;
    ward.policy.stackCount = eve::effects::StackCountPolicy::Increment;
    ward.policy.duration   = eve::effects::DurationPolicy::Replace;
    ward.policy.magnitude  = eve::effects::MagnitudePolicy::Keep;
    ward.policy.maxStacks  = 3;
    REQUIRE(
        ward.payload
            .setJson(
                "settlement.rule",
                R"({"stage":"target_mitigation","operation":"resist_percent","value":0.1,"value_per_extra_stack":0.1,"kinds":["damage"]})")
            .ok());
    REQUIRE(card.apply(ward, subject()).ok());
    REQUIRE(card.apply(ward, subject()).ok());

    eve::card::CardEffectDefinition burn;
    burn.id        = "card.burn.stacking";
    burn.duration  = 5.0;
    burn.period    = 1.0;
    burn.magnitude = 20.0;
    burn.policy    = oncePolicy();
    REQUIRE(card.apply(burn, subject()).ok());
    REQUIRE(card.advance(step(1, 1.0)).ok());
    CHECK_EQ(card.target().health, 84);

    eve::card::CardEffectAdapter expiring;
    ward.id               = "card.ward.expiring";
    ward.duration         = 1.0;
    ward.policy.stackMode = eve::effects::StackMode::NewInstance;
    REQUIRE(expiring.apply(ward, subject()).ok());
    burn.id = "card.burn.expiring";
    REQUIRE(expiring.apply(burn, subject()).ok());
    REQUIRE(expiring.advance(step(1, 1.0)).ok());
    CHECK_EQ(expiring.target().health, 80);
}

TEST_CASE("effects.card.finiteDecisionRuleProvidesImmunityWindow") {
    eve::card::CardEffectAdapter card;

    eve::card::CardEffectDefinition immunity;
    immunity.id       = "card.fire-immunity";
    immunity.duration = 2.0;
    immunity.kind     = eve::card::CardEffectKind::Heal;
    immunity.policy   = oncePolicy();
    REQUIRE(
        immunity.payload
            .setJson("settlement.rule",
                     R"({"stage":"decision","operation":"immune","kinds":["damage"],"required_tags":["card:damage"]})")
            .ok());
    REQUIRE(card.apply(immunity, subject()).ok());

    eve::card::CardEffectDefinition burn;
    burn.id        = "card.burn.immunity-window";
    burn.duration  = 3.0;
    burn.period    = 1.0;
    burn.magnitude = 20.0;
    burn.policy    = oncePolicy();
    REQUIRE(card.apply(burn, subject()).ok());

    REQUIRE(card.advance(step(1, 1.0)).ok());
    CHECK_EQ(card.target().health, 100);
    REQUIRE(card.advance(step(2, 1.0)).ok());
    CHECK_EQ(card.target().health, 80);
}
}  // namespace test_effects_domain_adapters
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_game_save_integration)
#include "PrivateTestScope.h"
#include <cstdint>
#include <string_view>
#include <utility>
#include "common/ECS.h"
#include "common/GameSaveSession.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"
#include "rpg/GameState.h"
#include "rpg/Quest.h"
#include "rpg/RPGActor.h"
#include "rpg/RPGGameSaveParticipant.h"
#include "rpg/RPGSaveSession.h"
#include "rpg/Tracker.h"
#include "tactics/Tactics.h"
#include "tactics/TacticsGameSaveParticipant.h"
#include "zeroerr/unittest.h"
// game_save_integration.cpp
namespace test_game_save_integration {
namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::SnapshotHashProvider testHash() {
    return [](std::string_view input) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        std::uint64_t         hash = 14695981039346656037ull;
        for (const unsigned char byte : input) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::uint8_t>(hash >> ((index % 8) * 8));
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
}

}  // namespace

TEST_CASE("gameSave.rpgAndTacticsRestoreTogether") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);
    eve::rpg::QuestRegistry::clear();
    eve::rpg::GameState state;
    state.setVariable("gold", 25.0);
    eve::rpg::Tracker            tracker;
    auto*                        actor = eve::rpg::RPGActor::createActor();
    eve::inventory::Bag          bag(2);
    eve::inventory::EquipmentSet equipment;
    eve::rpg::RPGSaveSession     rpgSession;
    rpgSession.bind(state, tracker, *actor, bag, equipment);
    REQUIRE(rpgSession.setContentVersion("game-content-v1").ok());

    eve::tactics::Tactics tactics;
    auto                  battleCreated = tactics.newBattle(subject("00000000-0000-0000-0000-000000000080"), 91);
    REQUIRE(battleCreated.ok());
    const auto battleHandle = std::move(battleCreated).takeValue();
    REQUIRE(tactics.addCell(battleHandle, {0, 0, 0}).ok());
    REQUIRE(tactics.addCell(battleHandle, {1, 0, 0}).ok());
    auto sideCreated = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000081"));
    REQUIRE(sideCreated.ok());
    const auto side      = std::move(sideCreated).takeValue();
    auto       otherSide = tactics.newSide(battleHandle, subject("00000000-0000-0000-0000-000000000083"));
    REQUIRE(otherSide.ok());
    const auto definition = eve::LogicalId::parse("test:scout");
    REQUIRE(definition.has_value());
    const auto unitSubject = subject("00000000-0000-0000-0000-000000000082");
    auto       unitCreated = tactics.newUnit(battleHandle, side, unitSubject, *definition, {0, 0, 0}, {1, 100, 1, 10});
    REQUIRE(unitCreated.ok());
    const auto unitHandle = std::move(unitCreated).takeValue();
    REQUIRE(tactics.start(battleHandle, eve::tactics::TurnPolicyKind::Initiative).ok());
    for (std::uint64_t tick = 1; tick <= 3; ++tick)
        REQUIRE(tactics.advance(battleHandle, {eve::SimulationTick(tick), eve::Duration::fromNanoseconds(1)}).ok());
    auto* battle = dynamic_cast<eve::tactics::Battle*>(ecs::try_get(battleHandle));
    auto* unit   = dynamic_cast<eve::tactics::TacticalUnit*>(ecs::try_get(unitHandle));
    REQUIRE(battle != nullptr);
    REQUIRE(unit != nullptr);

    eve::rpg::RPGGameSaveParticipant         rpgParticipant(rpgSession);
    eve::tactics::TacticsGameSaveParticipant tacticsParticipant(*battle, testHash());
    eve::GameSaveSession                     gameSave("game-content-v1");
    REQUIRE(gameSave.add(rpgParticipant).ok());
    REQUIRE(gameSave.add(tacticsParticipant).ok());
    auto saved = gameSave.snapshotJson(testHash());
    REQUIRE(saved.ok());

    state.setVariable("gold", 99.0);
    REQUIRE(tactics.moveUnit(battleHandle, unitSubject, {1, 0, 0}).ok());
    CHECK_EQ(unit->position()->cell, (eve::tactics::Cell{1, 0, 0}));
    REQUIRE(gameSave.restoreJson(saved.value(), testHash()).ok());
    CHECK_EQ(state.getVariable("gold"), 25.0);
    CHECK_EQ(unit->position()->cell, (eve::tactics::Cell{0, 0, 0}));
    actor->release();
}
}  // namespace test_game_save_integration
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gameplay_action)
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include "PrivateTestScope.h"
#include "action/Action.h"
#include "common/ConditionKeys.h"
#include "common/Identity.h"
#include "rpg/SkillAction.h"
#include "weapon/WeaponAction.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// gameplay_action.cpp
namespace test_gameplay_action {
namespace {

using eve::action::ActionDefinition;
using eve::action::ActionExecutionId;
using eve::action::ActionPhase;
using eve::action::ActionRequest;
using eve::action::ActionRuntime;
using eve::action::ActionServices;
using eve::action::IActionConditionEvaluator;
using eve::action::IActionEffectExecutor;
using eve::action::IActionEffectOperation;
using eve::action::IActionResourceProvider;
using eve::action::IActionTargetResolver;
using eve::action::TargetingMode;
using eve::condition::Condition;
using eve::condition::ConditionResult;
using eve::resource::AccountNonce;
using eve::resource::Affordability;
using eve::resource::CostSpec;
using eve::resource::IResourceAccount;
using eve::resource::Receipt;
using eve::resource::ReceiptId;
using eve::resource::ReceiptOperation;
using eve::resource::Reservation;
using eve::resource::ReservationId;

ActionDefinition definition(const char* name) {
    ActionDefinition result;
    const auto       id = eve::LogicalId::fromParts("test", name);
    REQUIRE(id.has_value());
    result.id = *id;
    return result;
}

ActionRequest requestFor(const ActionDefinition& definition) {
    ActionRequest result;
    result.actionId = definition.id;
    return result;
}

class TestConditionEvaluator final : public IActionConditionEvaluator {
public:
    bool        pass  = true;
    mutable int calls = 0;

    eve::Result<ConditionResult> evaluate(const ActionDefinition&, const ActionRequest&) const override {
        ++calls;
        if (pass) return eve::Result<ConditionResult>::success(ConditionResult::success());
        return eve::Result<ConditionResult>::success(
            ConditionResult::failed(eve::condition::ConditionReasonCode::TagMissing));
    }
};

class TestTargetResolver final : public IActionTargetResolver {
public:
    bool        fail  = true;
    mutable int calls = 0;

    eve::Result<eve::sensing::TargetSet> resolve(const eve::sensing::TargetingQuery&) const override {
        ++calls;
        if (fail)
            return eve::Result<eve::sensing::TargetSet>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "test target selection failed"));
        return eve::Result<eve::sensing::TargetSet>::success(eve::sensing::TargetSet{});
    }
};

class TestEffectOperation final : public IActionEffectOperation {
public:
    TestEffectOperation(int& commitCalls, int& rollbackCalls)
        : commitCalls_(commitCalls), rollbackCalls_(rollbackCalls) {}

    void commit() noexcept override { ++commitCalls_; }
    void rollback() noexcept override { ++rollbackCalls_; }

private:
    int& commitCalls_;
    int& rollbackCalls_;
};

class TestEffectExecutor final : public IActionEffectExecutor {
public:
    bool succeed       = true;
    int  prepareCalls  = 0;
    int  commitCalls   = 0;
    int  rollbackCalls = 0;

    eve::Result<std::unique_ptr<IActionEffectOperation>> prepare(const ActionDefinition&, const ActionRequest&,
                                                                 const eve::sensing::TargetSet*,
                                                                 eve::SimulationTick) override {
        ++prepareCalls;
        if (!succeed)
            return eve::Result<std::unique_ptr<IActionEffectOperation>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "test active operation failed"));
        std::unique_ptr<IActionEffectOperation> operation(new TestEffectOperation(commitCalls, rollbackCalls));
        return eve::Result<std::unique_ptr<IActionEffectOperation>>::success(std::move(operation));
    }
};

class TestResourceAccount final : public IResourceAccount {
public:
    TestResourceAccount() {
        auto nonce = eve::resource::allocateAccountNonce();
        nonce_     = std::move(nonce).expect("test resource account nonce");
    }

    bool        allowReserve   = true;
    bool        reserveMutated = false;
    bool        failCommit     = false;
    bool        debited        = false;
    int         reserveCalls   = 0;
    mutable int canAffordCalls = 0;
    int         rollbackCalls  = 0;
    bool        active         = false;
    Reservation activeReservation;

    eve::Result<Affordability> canAfford(const CostSpec&) const override {
        ++canAffordCalls;
        return eve::Result<Affordability>::success(Affordability{allowReserve, {}});
    }

    eve::Result<Reservation> reserve(const CostSpec& cost) override {
        ++reserveCalls;
        if (!allowReserve)
            return eve::Result<Reservation>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation, "test account cannot reserve cost"));
        activeReservation = Reservation{nonce_, nextReservation_, cost};
        nextReservation_  = ReservationId(nextReservation_.value() + 1);
        active            = true;
        reserveMutated    = true;
        return eve::Result<Reservation>::success(activeReservation, eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<Receipt> debit(const CostSpec& cost) override {
        return eve::Result<Receipt>::success(
            Receipt{nonce_, nextReceipt_, ReservationId{}, ReceiptOperation::Debit, cost},
            eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<Receipt> credit(const CostSpec& cost) override {
        return eve::Result<Receipt>::success(
            Receipt{nonce_, nextReceipt_, ReservationId{}, ReceiptOperation::Credit, cost},
            eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<Receipt> commit(const Reservation& reservation) override {
        if (!active || reservation.id != activeReservation.id)
            return eve::Result<Receipt>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "test reservation is not active"));
        if (failCommit)
            return eve::Result<Receipt>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "test account commit failed"));
        active  = false;
        debited = true;
        return eve::Result<Receipt>::success(
            Receipt{nonce_, nextReceipt_, reservation.id, ReceiptOperation::Debit, reservation.cost},
            eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<void> rollback(const Reservation& reservation) override {
        if (!active || reservation.id != activeReservation.id)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "test reservation is not active"));
        active = false;
        ++rollbackCalls;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    AccountNonce  nonce_;
    ReservationId nextReservation_{1};
    ReceiptId     nextReceipt_{1};
};

class TestResourceProvider final : public IActionResourceProvider {
public:
    explicit TestResourceProvider(TestResourceAccount& account) : currentAccount_(&account) {}

    bool available = true;

    eve::Result<Affordability> canAfford(const ActionDefinition&, const ActionRequest&,
                                         const CostSpec& cost) const override {
        if (!available)
            return eve::Result<Affordability>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "test resource provider is unavailable"));
        return currentAccount_->canAfford(cost);
    }

    eve::Result<Reservation> reserve(const ActionDefinition&, const ActionRequest&, const CostSpec& cost) override {
        if (!available)
            return eve::Result<Reservation>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "test resource provider is unavailable"));
        auto result = currentAccount_->reserve(cost);
        if (!result) return eve::Result<Reservation>::failure(result.status());
        auto credential = std::move(result).takeValue();
        if (!routeNonce_) {
            routeNonce_   = credential.account;
            routeAccount_ = currentAccount_;
        }
        if (switchAfterReserve_ && alternate_) currentAccount_ = alternate_;
        return eve::Result<Reservation>::success(std::move(credential), eve::Status::success(eve::StatusCode::Applied));
    }

    eve::Result<Receipt> commit(const Reservation& reservation) override {
        if (!available)
            return eve::Result<Receipt>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "test resource provider is unavailable"));
        TestResourceAccount* account = routeFor(reservation);
        if (!account)
            return eve::Result<Receipt>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "test provider lost reservation route"));
        return account->commit(reservation);
    }

    eve::Result<void> rollback(const Reservation& reservation) override {
        if (!available)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "test resource provider is unavailable"));
        TestResourceAccount* account = routeFor(reservation);
        if (!account)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "test provider lost reservation route"));
        return account->rollback(reservation);
    }

    void switchTo(TestResourceAccount& account) { currentAccount_ = &account; }
    void switchAfterReserveTo(TestResourceAccount& account) {
        alternate_          = &account;
        switchAfterReserve_ = true;
    }
    void unload() { available = false; }

private:
    TestResourceAccount* routeFor(const Reservation& reservation) const {
        if (routeAccount_ && routeNonce_ && reservation.account == *routeNonce_) return routeAccount_;
        return nullptr;
    }

    TestResourceAccount*        currentAccount_ = nullptr;
    TestResourceAccount*        alternate_      = nullptr;
    TestResourceAccount*        routeAccount_   = nullptr;
    std::optional<AccountNonce> routeNonce_;
    bool                        switchAfterReserve_ = false;
};

eve::sensing::SubjectRef testSubject(const char* value) {
    auto id = eve::PersistentId::parse(value);
    REQUIRE(id.has_value());
    return eve::sensing::SubjectRef::fromPersistentId(*id);
}

eve::sensing::WorldPoint testPoint() {
    auto point = eve::sensing::WorldPoint::world2D(0.f, 0.f);
    return std::move(point).expect("test action point");
}

}  // namespace

TEST_CASE("gameplayAction.phasePipelineUsesInjectedSimulationTime") {
    ActionDefinition def = definition("timed");
    def.timing.windup    = eve::Duration::fromNanoseconds(1);
    def.timing.active    = eve::Duration::fromNanoseconds(2);
    def.timing.recover   = eve::Duration::fromNanoseconds(3);
    TestEffectExecutor effects;
    ActionRuntime      runtime(ActionServices{nullptr, nullptr, nullptr, &effects});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id = std::move(submitted).takeValue();
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Requested));

    auto requested = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    REQUIRE(requested.ok());
    CHECK_EQ(static_cast<int>(requested.value().phase), static_cast<int>(ActionPhase::Windup));
    CHECK_EQ(requested.value().transitions.size(), 2u);

    auto active = runtime.advance(id, eve::SimulationTick{2}, eve::Duration::fromNanoseconds(1));
    REQUIRE(active.ok());
    CHECK_EQ(static_cast<int>(active.value().phase), static_cast<int>(ActionPhase::Active));
    CHECK_EQ(effects.prepareCalls, 1);
    CHECK_EQ(effects.commitCalls, 1);

    auto recovering = runtime.advance(id, eve::SimulationTick{3}, eve::Duration::fromNanoseconds(2));
    REQUIRE(recovering.ok());
    CHECK_EQ(static_cast<int>(recovering.value().phase), static_cast<int>(ActionPhase::Recover));

    auto completed = runtime.advance(id, eve::SimulationTick{4}, eve::Duration::fromNanoseconds(3));
    REQUIRE(completed.ok());
    CHECK_EQ(static_cast<int>(completed.value().phase), static_cast<int>(ActionPhase::Completed));
    CHECK_EQ(static_cast<int>(runtime.find(id)->status().code()), static_cast<int>(eve::StatusCode::Applied));
}

TEST_CASE("gameplayAction.conditionFailureOwnsFailedTransition") {
    ActionDefinition def = definition("condition");
    def.condition        = eve::condition::tagPresent("silenced");
    TestConditionEvaluator conditions;
    conditions.pass = false;
    ActionRuntime runtime(ActionServices{&conditions, nullptr, nullptr, nullptr});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id       = std::move(submitted).takeValue();
    auto                    advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    CHECK(!advanced.ok());
    CHECK_EQ(conditions.calls, 1);
    const auto* execution = runtime.find(id);
    REQUIRE(execution != nullptr);
    CHECK_EQ(static_cast<int>(execution->phase()), static_cast<int>(ActionPhase::Failed));
    REQUIRE(execution->status().primaryDiagnostic() != nullptr);
    CHECK_EQ(static_cast<int>(execution->status().primaryDiagnostic()->code()),
             static_cast<int>(eve::DiagnosticCode::PreconditionViolation));
}

TEST_CASE("gameplayAction.targetFailureStopsBeforeActive") {
    ActionDefinition def   = definition("target");
    def.targetingMode      = TargetingMode::Query;
    def.targetingSpec      = eve::sensing::TargetingSpec{};
    ActionRequest request  = requestFor(def);
    request.targetingQuery = eve::sensing::TargetingQuery{testSubject("00000000-0000-7000-8000-000000000001"),
                                                          testPoint(), *def.targetingSpec};
    TestTargetResolver targeting;
    ActionRuntime      runtime(ActionServices{nullptr, &targeting, nullptr, nullptr});

    auto submitted = runtime.submit(def, std::move(request));
    REQUIRE(submitted.ok());
    const ActionExecutionId id       = std::move(submitted).takeValue();
    auto                    advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    CHECK(!advanced.ok());
    CHECK_EQ(targeting.calls, 1);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Failed));
    REQUIRE(runtime.find(id)->status().primaryDiagnostic() != nullptr);
    CHECK_EQ(static_cast<int>(runtime.find(id)->status().primaryDiagnostic()->code()),
             static_cast<int>(eve::DiagnosticCode::PreconditionViolation));
}

TEST_CASE("gameplayAction.costFailureIsAtomicBeforeActive") {
    ActionDefinition def  = definition("cost");
    auto             cost = CostSpec::single("mana", 5);
    REQUIRE(cost.ok());
    def.cost = std::move(cost).takeValue();
    TestResourceAccount account;
    account.allowReserve = false;
    TestResourceProvider resources(account);
    TestEffectExecutor   effects;
    ActionRuntime        runtime(ActionServices{nullptr, nullptr, &resources, &effects});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id       = std::move(submitted).takeValue();
    auto                    advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    CHECK(!advanced.ok());
    CHECK_EQ(account.canAffordCalls, 1);
    CHECK_EQ(account.reserveCalls, 0);
    CHECK(!account.reserveMutated);
    CHECK_EQ(account.rollbackCalls, 0);
    CHECK_EQ(effects.prepareCalls, 0);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Failed));
}

TEST_CASE("gameplayAction.cancelDuringWindupHasNoCrossFrameReservation") {
    ActionDefinition def = definition("cancel");
    def.timing.windup    = eve::Duration::fromNanoseconds(10);
    auto cost            = CostSpec::single("mana", 2);
    REQUIRE(cost.ok());
    def.cost = std::move(cost).takeValue();
    TestResourceAccount  account;
    TestResourceProvider resources(account);
    ActionRuntime        runtime(ActionServices{nullptr, nullptr, &resources, nullptr});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id      = std::move(submitted).takeValue();
    auto                    started = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    REQUIRE(started.ok());
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Windup));
    CHECK_EQ(account.canAffordCalls, 1);
    CHECK_EQ(account.reserveCalls, 0);
    CHECK(!account.active);

    auto cancelled = runtime.cancel(id, eve::SimulationTick{2});
    REQUIRE(cancelled.ok());
    CHECK_EQ(account.rollbackCalls, 0);
    CHECK(!account.active);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Cancelled));
    CHECK_EQ(static_cast<int>(runtime.find(id)->status().code()), static_cast<int>(eve::StatusCode::Cancelled));
}

TEST_CASE("gameplayAction.effectPrepareFailureDoesNotReserveCost") {
    ActionDefinition def  = definition("prepare-failure");
    auto             cost = CostSpec::single("mana", 3);
    REQUIRE(cost.ok());
    def.cost                    = std::move(cost).takeValue();
    def.activeExecutionRequired = true;
    TestResourceAccount  account;
    TestResourceProvider resources(account);
    TestEffectExecutor   effects;
    effects.succeed = false;
    ActionRuntime runtime(ActionServices{nullptr, nullptr, &resources, &effects});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id       = std::move(submitted).takeValue();
    auto                    advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    CHECK(!advanced.ok());
    CHECK_EQ(effects.prepareCalls, 1);
    CHECK_EQ(effects.commitCalls, 0);
    CHECK_EQ(effects.rollbackCalls, 0);
    CHECK_EQ(account.canAffordCalls, 1);
    CHECK_EQ(account.reserveCalls, 0);
    CHECK(!account.active);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Failed));
}

TEST_CASE("gameplayAction.commitFailureCompensatesResourceAndPreparedEffect") {
    ActionDefinition def  = definition("commit-failure");
    auto             cost = CostSpec::single("mana", 4);
    REQUIRE(cost.ok());
    def.cost                    = std::move(cost).takeValue();
    def.activeExecutionRequired = true;
    TestResourceAccount account;
    account.failCommit = true;
    TestResourceProvider resources(account);
    TestEffectExecutor   effects;
    ActionRuntime        runtime(ActionServices{nullptr, nullptr, &resources, &effects});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id       = std::move(submitted).takeValue();
    auto                    advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    CHECK(!advanced.ok());
    CHECK_EQ(effects.prepareCalls, 1);
    CHECK_EQ(effects.commitCalls, 0);
    CHECK_EQ(effects.rollbackCalls, 1);
    CHECK_EQ(account.reserveCalls, 1);
    CHECK_EQ(account.rollbackCalls, 1);
    CHECK(!account.active);
    CHECK(!account.debited);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Failed));
}

TEST_CASE("gameplayAction.successCommitsCostBeforeNonfailableEffect") {
    ActionDefinition def  = definition("transaction-success");
    auto             cost = CostSpec::single("mana", 6);
    REQUIRE(cost.ok());
    def.cost                    = std::move(cost).takeValue();
    def.activeExecutionRequired = true;
    TestResourceAccount  account;
    TestResourceProvider resources(account);
    TestEffectExecutor   effects;
    ActionRuntime        runtime(ActionServices{nullptr, nullptr, &resources, &effects});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id       = std::move(submitted).takeValue();
    auto                    advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    REQUIRE(advanced.ok());
    CHECK_EQ(account.reserveCalls, 1);
    CHECK(account.debited);
    CHECK(!account.active);
    CHECK_EQ(effects.prepareCalls, 1);
    CHECK_EQ(effects.commitCalls, 1);
    CHECK_EQ(effects.rollbackCalls, 0);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Completed));
}

TEST_CASE("gameplayAction.providerRoutesCommitByReservationNonceAfterAccountSwitch") {
    ActionDefinition def  = definition("nonce-route");
    auto             cost = CostSpec::single("mana", 1);
    REQUIRE(cost.ok());
    def.cost = std::move(cost).takeValue();
    TestResourceAccount  original;
    TestResourceAccount  replacement;
    TestResourceProvider resources(original);
    resources.switchAfterReserveTo(replacement);
    ActionRuntime runtime(ActionServices{nullptr, nullptr, &resources, nullptr});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id       = std::move(submitted).takeValue();
    auto                    advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    REQUIRE(advanced.ok());
    CHECK(original.debited);
    CHECK(!original.active);
    CHECK(!replacement.debited);
    CHECK_EQ(replacement.reserveCalls, 0);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Completed));
}

TEST_CASE("gameplayAction.providerRoutesRollbackByReservationNonceAfterAccountSwitch") {
    ActionDefinition def  = definition("nonce-rollback");
    auto             cost = CostSpec::single("mana", 1);
    REQUIRE(cost.ok());
    def.cost = std::move(cost).takeValue();
    TestResourceAccount original;
    original.failCommit = true;
    TestResourceAccount  replacement;
    TestResourceProvider resources(original);
    resources.switchAfterReserveTo(replacement);
    ActionRuntime runtime(ActionServices{nullptr, nullptr, &resources, nullptr});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id       = std::move(submitted).takeValue();
    auto                    advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    CHECK(!advanced.ok());
    CHECK(!original.active);
    CHECK_EQ(original.rollbackCalls, 1);
    CHECK(!replacement.active);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Failed));
}

TEST_CASE("gameplayAction.providerUnavailableBeforeActiveLeavesNoReservation") {
    ActionDefinition def  = definition("provider-unloaded");
    auto             cost = CostSpec::single("mana", 1);
    REQUIRE(cost.ok());
    def.cost = std::move(cost).takeValue();
    TestResourceAccount  account;
    TestResourceProvider resources(account);
    resources.unload();
    ActionRuntime runtime(ActionServices{nullptr, nullptr, &resources, nullptr});

    auto submitted = runtime.submit(def, requestFor(def));
    REQUIRE(submitted.ok());
    const ActionExecutionId id       = std::move(submitted).takeValue();
    auto                    advanced = runtime.advance(id, eve::SimulationTick{1}, eve::Duration::zero());
    CHECK(!advanced.ok());
    CHECK_EQ(account.canAffordCalls, 0);
    CHECK_EQ(account.reserveCalls, 0);
    CHECK(!account.active);
    CHECK_EQ(static_cast<int>(runtime.find(id)->phase()), static_cast<int>(ActionPhase::Failed));
}

TEST_CASE("gameplayAction.skillAndWeaponAdaptersShareTheSamePipelineShape") {
    eve::rpg::SkillDefinition skill;
    skill.id       = "fireball";
    skill.castTime = 0.25f;
    skill.cooldown = 4.f;
    skill.grantedEffects.push_back("burning");
    auto skillCost = CostSpec::single("mana", 10);
    REQUIRE(skillCost.ok());
    skill.cost = std::move(skillCost).takeValue();

    eve::weapon::WeaponDefinition weapon;
    weapon.id                 = "rifle";
    weapon.stages.windupTime  = 0.25f;
    weapon.stages.activeTime  = 0.05f;
    weapon.stages.recoverTime = 0.4f;
    weapon.resource.kind      = eve::weapon::ResourceKind::Ammo;
    eve::weapon::AttackRequest attack;

    auto skillDefinition  = eve::rpg::SkillActionAdapter::makeDefinition(skill);
    auto weaponDefinition = eve::weapon::WeaponActionAdapter::makeDefinition(weapon, attack);
    REQUIRE(skillDefinition.ok());
    REQUIRE(weaponDefinition.ok());
    CHECK_EQ(skillDefinition.value().id.namespaceName(), std::string_view("rpg"));
    CHECK_EQ(weaponDefinition.value().id.namespaceName(), std::string_view("weapon"));
    CHECK_EQ(skillDefinition.value().id.name(), std::string_view("skill.fireball"));
    CHECK_EQ(weaponDefinition.value().id.name(), std::string_view("attack.rifle"));
    CHECK(skillDefinition.value().activeExecutionRequired);
    CHECK(weaponDefinition.value().activeExecutionRequired);
    CHECK(skillDefinition.value().cost.has_value());
    CHECK(weaponDefinition.value().cost.has_value());
    CHECK_EQ(skillDefinition.value().timing.windup, weaponDefinition.value().timing.windup);
    CHECK_EQ(static_cast<int>(skillDefinition.value().targetingMode), static_cast<int>(TargetingMode::None));
    CHECK_EQ(static_cast<int>(weaponDefinition.value().targetingMode), static_cast<int>(TargetingMode::None));

    auto skillRequest  = eve::rpg::SkillActionAdapter::makeRequest(skill);
    auto weaponRequest = eve::weapon::WeaponActionAdapter::makeRequest(weapon, attack);
    REQUIRE(skillRequest.ok());
    REQUIRE(weaponRequest.ok());
    CHECK_EQ(skillRequest.value().actionId, skillDefinition.value().id);
    CHECK_EQ(weaponRequest.value().actionId, weaponDefinition.value().id);
    CHECK(weaponRequest.value().parameters.find("pelletCount") != weaponRequest.value().parameters.end());
}
}  // namespace test_gameplay_action
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gameplay_control_rpg_product)
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/GameplayInstanceCatalog.h"
#include "inventory/Bag.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "rpg/GameState.h"
#include "rpg/ProductControl.h"
#include "rpg/Quest.h"
#include "rpg/RPG.h"
#include "rpg/ShopCatalogue.h"
#include "rpg/Tracker.h"
#include "rpg/WorldState.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// gameplay_control_rpg_product.cpp
namespace test_gameplay_control_rpg_product {
namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

void installProductFixture() {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::InventorySystem::clearEvents();
    eve::rpg::QuestRegistry::clear();
    eve::rpg::ShopCatalogue::clear();
    eve::inventory::ItemDefinition item;
    item.id       = "cache_item";
    item.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(item);
    auto shop = eve::rpg::ShopCatalogue::replaceFromJsonStrict(
        R"([{"id":"village.cache","itemId":"cache_item","name":"Cache Item","desc":"","buyPrice":20,"sellPrice":10}])");
    REQUIRE(shop.ok());
    eve::rpg::QuestDefinition quest;
    quest.id             = "quest.cache";
    quest.startPolicy    = "manual";
    quest.completePolicy = "claim";
    quest.objectives.push_back({"recover", "interact", "cache", 1});
    quest.rewards.push_back({"item", "cache_item", 2.0});
    quest.rewards.push_back({"attribute", "gold", 5.0});
    eve::rpg::QuestRegistry::registerQuest(quest);
}

void clearProductFixture() {
    eve::inventory::InventorySystem::clearEvents();
    eve::inventory::ItemRegistry::clear();
    eve::rpg::QuestRegistry::clear();
    eve::rpg::ShopCatalogue::clear();
}

eve::GameplayCommand command(const char* id, const char* actionId, eve::SubjectRef instance,
                             const eve::GameplayObservation& observation, eve::Value parameters) {
    eve::GameplayCommand result;
    result.id               = id;
    result.action           = action(actionId);
    result.subject          = instance;
    result.observedTick     = observation.tick;
    result.expectedRevision = observation.revision;
    result.parameters       = std::move(parameters);
    return result;
}

}  // namespace

TEST_CASE("gameplay.control.rpgProductRunsShopLootAndQuestThroughCanonicalTransactions") {
    installProductFixture();
    const auto          instance = subject("00000000-0000-7000-8000-000000000701");
    eve::rpg::GameState state;
    state.setVariable("gold", 100.0);
    eve::rpg::Tracker tracker;
    REQUIRE(tracker.activate("quest.cache"));
    eve::inventory::Bag bag(4);
    bag.setId("hero");
    eve::rpg::ProductControl control(instance, state, tracker, bag);
    eve::GameplaySession     player{"player", eve::GameplayAccess::PlayerEquivalent, {instance}};
    eve::GameplaySession     automation{"automation", eve::GameplayAccess::TestDriver, {instance}};

    bool discovered = false;
    eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* provider) {
        if (provider == &control && provider->gameplayDomain() == "rpg.product") discovered = true;
    });
    CHECK(discovered);
    auto playerActions     = control.availableGameplayActions(player, instance, instance);
    auto automationActions = control.availableGameplayActions(automation, instance, instance);
    REQUIRE(playerActions.ok());
    REQUIRE(automationActions.ok());
    const auto playerDescriptors     = std::move(playerActions).takeValue();
    const auto automationDescriptors = std::move(automationActions).takeValue();
    REQUIRE_EQ(playerDescriptors.size(), std::size_t{4});
    REQUIRE_EQ(automationDescriptors.size(), playerDescriptors.size());
    for (std::size_t index = 0; index < playerDescriptors.size(); ++index)
        CHECK_EQ(automationDescriptors[index].id, playerDescriptors[index].id);

    auto observed = control.observeGameplay(player, instance);
    REQUIRE(observed.ok());
    const auto beforeBuy = std::move(observed).takeValue();
    auto       buy       = command("product-buy-1", "rpg:buy-offer", instance, beforeBuy,
                                   eve::Value(eve::Value::Object{{"currencyId", eve::Value("gold")},
                                                                 {"offerId", eve::Value("village.cache")},
                                                                 {"quantity", eve::Value(2)}}));
    auto       bought    = control.submitGameplay(player, instance, buy);
    REQUIRE(bought.ok());
    CHECK_EQ(state.getVariable("gold"), 60.0);
    CHECK_EQ(bag.countItem("cache_item"), 2);

    auto stale       = command("product-stale", "rpg:sell-offer", instance, beforeBuy,
                               eve::Value(eve::Value::Object{{"currencyId", eve::Value("gold")},
                                                             {"offerId", eve::Value("village.cache")},
                                                             {"quantity", eve::Value(1)}}));
    auto staleResult = control.submitGameplay(player, instance, stale);
    CHECK(!staleResult.ok());
    CHECK_EQ(staleResult.code(), eve::StatusCode::Conflict);
    CHECK_EQ(state.getVariable("gold"), 60.0);
    CHECK_EQ(bag.countItem("cache_item"), 2);

    observed = control.observeGameplay(player, instance);
    REQUIRE(observed.ok());
    const auto beforeSell = std::move(observed).takeValue();
    auto       sell       = command("product-sell-1", "rpg:sell-offer", instance, beforeSell,
                                    eve::Value(eve::Value::Object{{"currencyId", eve::Value("gold")},
                                                                  {"offerId", eve::Value("village.cache")},
                                                                  {"quantity", eve::Value(1)}}));
    REQUIRE(control.submitGameplay(player, instance, sell).ok());
    CHECK_EQ(state.getVariable("gold"), 70.0);
    CHECK_EQ(bag.countItem("cache_item"), 1);

    observed = control.observeGameplay(player, instance);
    REQUIRE(observed.ok());
    const auto beforeLoot = std::move(observed).takeValue();
    auto       loot       = command("product-loot-1", "rpg:collect-loot", instance, beforeLoot,
                                    eve::Value(eve::Value::Object{{"attributeAmount", eve::Value(7.0)},
                                                                  {"attributeId", eve::Value("gold")},
                                                                  {"itemId", eve::Value("cache_item")},
                                                                  {"itemQuantity", eve::Value(1)},
                                                                  {"mapId", eve::Value("forest")},
                                                                  {"notifyAmount", eve::Value(1)},
                                                                  {"notifyTarget", eve::Value("cache")},
                                                                  {"notifyTopic", eve::Value("interact")},
                                                                  {"objectId", eve::Value("cache")},
                                                                  {"requiredQuestId", eve::Value("quest.cache")}}));
    REQUIRE(control.submitGameplay(automation, instance, loot).ok());
    eve::rpg::WorldState world(state);
    CHECK(world.isObjectConsumed("forest", "cache"));
    CHECK_EQ(tracker.getState("quest.cache"), std::string("ready"));
    CHECK_EQ(state.getVariable("gold"), 77.0);
    CHECK_EQ(bag.countItem("cache_item"), 2);

    observed = control.observeGameplay(player, instance);
    REQUIRE(observed.ok());
    const auto beforeClaim = std::move(observed).takeValue();
    auto       claim       = command("product-claim-1", "rpg:claim-quest", instance, beforeClaim,
                                     eve::Value(eve::Value::Object{{"questId", eve::Value("quest.cache")}}));
    REQUIRE(control.submitGameplay(player, instance, claim).ok());
    CHECK_EQ(tracker.getState("quest.cache"), std::string("completed"));
    CHECK_EQ(state.getVariable("gold"), 82.0);
    CHECK_EQ(bag.countItem("cache_item"), 4);

    // 目录能力：该适配器服务的就是这一个实例，`instances` 因此能列出它。
    const auto catalogInstances = control.gameplayInstances();
    REQUIRE_EQ(catalogInstances.size(), std::size_t{1});
    CHECK_EQ(catalogInstances[0].format(), instance.format());
    eve::IGameplayInstanceCatalog* catalog = nullptr;
    eve::cap::forEach<eve::IGameplayInstanceCatalog>([&](auto* candidate) {
        if (candidate != nullptr && candidate->gameplayDomain() == "rpg.product") catalog = candidate;
    });
    REQUIRE(catalog != nullptr);
    CHECK_EQ(catalog->gameplayInstances().size(), std::size_t{1});

    auto events = control.gameplayEvents(player, instance, 0);
    REQUIRE(events.ok());
    const auto batch = std::move(events).takeValue();
    REQUIRE_EQ(batch.size(), std::size_t{4});
    CHECK_EQ(batch.front().causationCommandId, std::string("product-buy-1"));
    CHECK_EQ(batch.back().type, std::string("rpg.product.claim-quest"));
    clearProductFixture();
}

TEST_CASE("gameplay.control.rpgProductDetectsMutationOutsideTheAdapter") {
    installProductFixture();
    const auto          instance = subject("00000000-0000-7000-8000-000000000711");
    eve::rpg::GameState state;
    state.setVariable("gold", 100.0);
    eve::rpg::Tracker        tracker;
    eve::inventory::Bag      bag(2);
    eve::rpg::ProductControl control(instance, state, tracker, bag);
    eve::GameplaySession     player{"player", eve::GameplayAccess::PlayerEquivalent, {instance}};
    auto                     observed = control.observeGameplay(player, instance);
    REQUIRE(observed.ok());
    const auto before = std::move(observed).takeValue();

    state.addVariable("gold", 1.0);
    auto buy      = command("external-stale", "rpg:buy-offer", instance, before,
                            eve::Value(eve::Value::Object{{"currencyId", eve::Value("gold")},
                                                          {"offerId", eve::Value("village.cache")},
                                                          {"quantity", eve::Value(1)}}));
    auto rejected = control.submitGameplay(player, instance, buy);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Conflict);
    CHECK_EQ(state.getVariable("gold"), 101.0);
    CHECK_EQ(bag.countItem("cache_item"), 0);
    clearProductFixture();
}

TEST_CASE("gameplay.control.rpgControlFactoryReturnsStructuredOwnership") {
    const auto          instance = subject("00000000-0000-7000-8000-000000000721");
    eve::rpg::RPG       module;
    eve::rpg::GameState state;
    eve::rpg::Tracker   tracker;
    eve::inventory::Bag bag(1);
    auto                rejected = module.newProductControl(instance, nullptr, &tracker, &bag);
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Rejected);

    auto created = module.newProductControl(instance, &state, &tracker, &bag);
    REQUIRE(created.ok());
    auto control = std::move(created).takeValue();
    REQUIRE(static_cast<bool>(control));
    CHECK_EQ(control->gameplayDomain(), std::string_view("rpg.product"));
}
}  // namespace test_gameplay_control_rpg_product
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gameplay_control_weapon)
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/GameplayControlJson.h"
#include "common/GameplayInstanceCatalog.h"
#include "inventory/Bag.h"
#include "inventory/InventoryResourceAccount.h"
#include "inventory/Item.h"
#include "transaction/Transaction.h"
#include "weapon/WeaponControl.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// gameplay_control_weapon.cpp
namespace test_gameplay_control_weapon {
namespace {

eve::SubjectRef subject(const char* text) {
    const auto id = eve::PersistentId::parse(text);
    REQUIRE(id.has_value());
    return eve::SubjectRef::fromPersistentId(*id);
}

eve::LogicalId action(const char* text) {
    const auto id = eve::LogicalId::parse(text);
    REQUIRE(id.has_value());
    return *id;
}

class Effect final : public eve::transaction::ITransactionParticipant {
public:
    [[nodiscard]] eve::Result<void> prepare(const eve::transaction::TransactionContext&) override {
        prepared = true;
        return eve::Result<void>::success();
    }
    [[nodiscard]] eve::Result<void> commit(const eve::transaction::TransactionContext&) override {
        committed = true;
        return eve::Result<void>::success();
    }
    [[nodiscard]] eve::Result<void> rollback(const eve::transaction::TransactionContext&) override {
        prepared = false;
        return eve::Result<void>::success();
    }
    [[nodiscard]] eve::Result<void> compensate(const eve::transaction::TransactionContext&) override {
        committed = false;
        prepared  = false;
        return eve::Result<void>::success();
    }
    bool prepared  = false;
    bool committed = false;
};

eve::GameplayCommand fireCommand(const char* commandId, eve::SubjectRef wielder,
                                 const eve::GameplayObservation& observed) {
    eve::GameplayCommand command;
    command.id               = commandId;
    command.action           = action("weapon:fire");
    command.subject          = wielder;
    command.observedTick     = observed.tick;
    command.expectedRevision = observed.revision;
    command.parameters       = eve::Value(eve::Value::Object{{"shooterId", eve::Value(7)},
                                                             {"targetX", eve::Value(4.0)},
                                                             {"targetY", eve::Value(5.0)},
                                                             {"targetZ", eve::Value(6.0)}});
    return command;
}

}  // namespace

TEST_CASE("gameplay.control.weaponUsesCanonicalActionAndAtomicAmmoPayment") {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition ammo;
    ammo.id       = "ammo";
    ammo.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(ammo);
    eve::inventory::Bag bag(1);
    // 先存到局部变量再断言：REQUIRE_EQ/CHECK_EQ 会求值两次，直接写 `REQUIRE_EQ(bag.addItem(...), n)`
    // 会真的加两次弹药，后面的扣减断言便永远不成立。
    const int stocked = bag.addItem("ammo", 2);
    REQUIRE_EQ(stocked, 2);
    eve::inventory::InventoryResourceAccount account(bag);
    Effect                                   effect;
    eve::weapon::WeaponDefinition            definition;
    definition.id                       = "player-rifle";
    definition.resource.kind            = eve::weapon::ResourceKind::Ammo;
    definition.resource.cost            = 1.0f;
    const auto                 instance = subject("00000000-0000-7000-8000-000000000801");
    const auto                 wielder  = subject("00000000-0000-7000-8000-000000000802");
    eve::weapon::WeaponControl control(instance, wielder, definition, account, effect);
    eve::GameplaySession       player{"player", eve::GameplayAccess::PlayerEquivalent, {wielder}};
    eve::GameplaySession       automation{"automation", eve::GameplayAccess::TestDriver, {wielder}};

    bool discovered = false;
    eve::cap::forEach<eve::IGameplayControlProvider>([&](auto* provider) {
        if (provider == &control && provider->gameplayDomain() == "weapon") discovered = true;
    });
    CHECK(discovered);
    auto playerActions     = control.availableGameplayActions(player, instance, wielder);
    auto automationActions = control.availableGameplayActions(automation, instance, wielder);
    REQUIRE(playerActions.ok());
    REQUIRE(automationActions.ok());
    CHECK_EQ(playerActions.value().front().id, automationActions.value().front().id);

    auto observed = control.observeGameplay(player, instance);
    REQUIRE(observed.ok());
    const auto before = std::move(observed).takeValue();
    auto       fired  = control.submitGameplay(player, instance, fireCommand("weapon-fire-1", wielder, before));
    REQUIRE(fired.ok());
    CHECK_EQ(bag.countItem("ammo"), 1);
    CHECK(effect.committed);

    // 目录能力：该适配器服务的就是这一个实例，`instances` 因此能列出它。
    const auto catalogInstances = control.gameplayInstances();
    REQUIRE_EQ(catalogInstances.size(), std::size_t{1});
    CHECK_EQ(catalogInstances[0].format(), instance.format());
    eve::IGameplayInstanceCatalog* catalog = nullptr;
    eve::cap::forEach<eve::IGameplayInstanceCatalog>([&](auto* candidate) {
        if (candidate != nullptr && candidate->gameplayDomain() == "weapon") catalog = candidate;
    });
    REQUIRE(catalog != nullptr);
    CHECK_EQ(catalog->gameplayInstances().size(), std::size_t{1});

    auto scoped = eve::executeGameplayControlJson(
        R"({"schemaId":"evengine.gameplay-control-request","schemaVersion":1,"op":"instances","domain":"weapon"})");
    REQUIRE(scoped.ok());
    CHECK(scoped.value().find(instance.format()) != std::string::npos);

    auto stale = control.submitGameplay(player, instance, fireCommand("weapon-fire-stale", wielder, before));
    CHECK(!stale.ok());
    CHECK_EQ(stale.code(), eve::StatusCode::Conflict);
    CHECK_EQ(bag.countItem("ammo"), 1);
    auto events = control.gameplayEvents(player, instance, 0);
    REQUIRE(events.ok());
    REQUIRE_EQ(events.value().size(), std::size_t{1});
    CHECK_EQ(events.value().front().causationCommandId, std::string("weapon-fire-1"));
    eve::inventory::ItemRegistry::clear();
}

TEST_CASE("gameplay.control.routerDispatchesAmongTwoWeaponInstances") {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition ammo;
    ammo.id       = "ammo";
    ammo.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(ammo);
    eve::inventory::Bag firstBag(1);
    eve::inventory::Bag secondBag(1);
    const int           firstStock  = firstBag.addItem("ammo", 2);
    const int           secondStock = secondBag.addItem("ammo", 2);
    REQUIRE_EQ(firstStock, 2);
    REQUIRE_EQ(secondStock, 2);
    eve::inventory::InventoryResourceAccount firstAccount(firstBag);
    eve::inventory::InventoryResourceAccount secondAccount(secondBag);
    Effect                                   firstEffect;
    Effect                                   secondEffect;
    eve::weapon::WeaponDefinition            firstDefinition;
    firstDefinition.id                             = "rifle-alpha";
    firstDefinition.resource.kind                  = eve::weapon::ResourceKind::Ammo;
    firstDefinition.resource.cost                  = 1.0f;
    eve::weapon::WeaponDefinition secondDefinition = firstDefinition;
    secondDefinition.id                            = "rifle-beta";
    const auto                 firstInstance       = subject("00000000-0000-7000-8000-000000000831");
    const auto                 firstWielder        = subject("00000000-0000-7000-8000-000000000832");
    const auto                 secondInstance      = subject("00000000-0000-7000-8000-000000000833");
    const auto                 secondWielder       = subject("00000000-0000-7000-8000-000000000834");
    eve::weapon::WeaponControl first(firstInstance, firstWielder, firstDefinition, firstAccount, firstEffect);
    eve::weapon::WeaponControl second(secondInstance, secondWielder, secondDefinition, secondAccount, secondEffect);

    // 两把武器 = 两个同域 provider：路由器按实例目录分派，两边都能观察。
    auto observeFirst = eve::executeGameplayControlJson(
        "{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"observe\","
        "\"domain\":\"weapon\",\"instance\":\"" +
        firstInstance.format() + "\",\"session\":{\"id\":\"probe\",\"access\":\"player\",\"controlledSubjects\":[\"" +
        firstWielder.format() + "\"]}}");
    REQUIRE(observeFirst.ok());
    CHECK(observeFirst.value().find("rifle-alpha") != std::string::npos);

    auto observeSecond = eve::executeGameplayControlJson(
        "{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"observe\","
        "\"domain\":\"weapon\",\"instance\":\"" +
        secondInstance.format() + "\",\"session\":{\"id\":\"probe\",\"access\":\"player\",\"controlledSubjects\":[\"" +
        secondWielder.format() + "\"]}}");
    REQUIRE(observeSecond.ok());
    CHECK(observeSecond.value().find("rifle-beta") != std::string::npos);

    // 开火只落到被路由到的第二把武器：只有它的弹药被扣。
    auto fireSecond = eve::executeGameplayControlJson(
        "{\"schemaId\":\"evengine.gameplay-control-request\",\"schemaVersion\":1,\"op\":\"submit\","
        "\"domain\":\"weapon\",\"instance\":\"" +
        secondInstance.format() + "\",\"session\":{\"id\":\"probe\",\"access\":\"player\",\"controlledSubjects\":[\"" +
        secondWielder.format() + "\"]},\"command\":{\"id\":\"routed-fire\",\"action\":\"weapon:fire\",\"subject\":\"" +
        secondWielder.format() +
        "\",\"observedTick\":0,\"expectedRevision\":1,\"parameters\":{\"shooterId\":7,\"targetX\":1.0,"
        "\"targetY\":0.0,\"targetZ\":0.0}}}");
    REQUIRE(fireSecond.ok());
    CHECK_EQ(secondBag.countItem("ammo"), 1);
    CHECK_EQ(firstBag.countItem("ammo"), 2);
    CHECK(secondEffect.committed);
    CHECK(!firstEffect.committed);
    eve::inventory::ItemRegistry::clear();
}

TEST_CASE("gameplay.control.weaponRejectsUnauthorizedWielderWithoutCharging") {
    eve::inventory::ItemRegistry::clear();
    eve::inventory::ItemDefinition ammo;
    ammo.id       = "ammo";
    ammo.maxStack = 20;
    eve::inventory::ItemRegistry::registerItem(ammo);
    eve::inventory::Bag bag(1);
    const int           stockedGuard = bag.addItem("ammo", 1);
    REQUIRE_EQ(stockedGuard, 1);
    eve::inventory::InventoryResourceAccount account(bag);
    Effect                                   effect;
    eve::weapon::WeaponDefinition            definition;
    definition.id                       = "guard-rifle";
    definition.resource.kind            = eve::weapon::ResourceKind::Ammo;
    definition.resource.cost            = 1.0f;
    const auto                 instance = subject("00000000-0000-7000-8000-000000000811");
    const auto                 wielder  = subject("00000000-0000-7000-8000-000000000812");
    const auto                 stranger = subject("00000000-0000-7000-8000-000000000813");
    eve::weapon::WeaponControl control(instance, wielder, definition, account, effect);
    eve::GameplaySession       unauthorized{"other", eve::GameplayAccess::PlayerEquivalent, {stranger}};
    auto                       observed = control.observeGameplay(unauthorized, instance);
    CHECK(!observed.ok());
    CHECK_EQ(bag.countItem("ammo"), 1);
    CHECK(!effect.committed);
    eve::inventory::ItemRegistry::clear();
}
}  // namespace test_gameplay_control_weapon
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gpuagents)
#include "PrivateTestScope.h"
#include <cmath>
#include <memory>
#include <vector>
#include "gpuagents/AgentInstanceRenderer.h"
#include "gpuagents/EffectBackend.h"
#include "gpuagents/GpuAgentWorld.h"
#include "gpuagents/GpuAgents.h"
#include "gpuagents/ObstacleField.h"
#include "gpuagents/solvers/FishSolver.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// gpuagents.cpp
namespace test_gpuagents {
using namespace eve::gpuagents;

namespace {

float meanNeighborDistance(const std::vector<AgentState>& states) {
    double sum   = 0.0;
    int    pairs = 0;
    for (size_t i = 0; i < states.size(); ++i) {
        if (!states[i].alive) continue;
        for (size_t j = i + 1; j < states.size(); ++j) {
            if (!states[j].alive) continue;
            sum += glm::length(states[i].position - states[j].position);
            ++pairs;
        }
    }
    return pairs > 0 ? static_cast<float>(sum / pairs) : 0.f;
}

}  // namespace

TEST_CASE("gpuagents.obstacle_resolve_keeps_tangential") {
    ObstacleField field;
    field.bakeEmpty(glm::vec3(-5.f), glm::ivec3(32, 32, 32), 0.35f);
    field.carveSphere(glm::vec3(0.f), 1.5f);

    // Offset from the exact center so the SDF gradient is well-defined.
    glm::vec3   pos(0.15f, 0.05f, -0.1f);
    glm::vec3   vel(1.f, 0.f, 0.5f);
    const float beforeDist = field.sample(pos).distance;
    field.resolve(pos, vel, 0.25f, 0.3f);
    field.resolve(pos, vel, 0.25f, 0.3f);  // second pass for discrete-grid residual

    const auto sample = field.sample(pos);
    CHECK(sample.distance > beforeDist);
    CHECK(sample.distance >= 0.24f);
    // Tangential component should remain after canceling inward normal.
    CHECK(glm::length(glm::vec2(vel.x, vel.z)) > 0.2f);
}

TEST_CASE("gpuagents.fish_separation_and_cohesion") {
    EffectBackend backend;
    auto          profile         = EffectProfile::makeFish(32);
    profile.fish.separationWeight = 4.0f;
    profile.fish.cohesionWeight   = 0.0f;
    profile.fish.alignmentWeight  = 0.0f;
    profile.fish.goalWeight       = 0.0f;
    profile.fish.depthWeight      = 0.0f;
    profile.fish.currentWeight    = 0.0f;
    profile.fish.maxSpeed         = 3.f;
    REQUIRE(backend.configure(profile, "fish").ok());

    REQUIRE(backend.simulation()
                .initialize(16,
                            [](int i) {
                                AgentState s;
                                // Tight cluster to trigger separation.
                                s.position = glm::vec3((i % 4) * 0.05f, 0.f, (i / 4) * 0.05f);
                                s.velocity = glm::vec3(0.05f, 0.f, 0.02f);
                                return s;
                            })
                .ok());

    const float         meanBeforeSep = meanNeighborDistance(backend.simulation().states());
    EnvironmentSnapshot env;
    ObstacleField       empty;
    env.obstacles = &empty;
    for (int i = 0; i < 120; ++i) {
        REQUIRE(backend.step(1.f / 60.f, env).ok());
    }
    const float meanAfterSep = meanNeighborDistance(backend.simulation().states());
    CHECK(meanAfterSep > meanBeforeSep);

    // Reconfigure with cohesion-dominant weights on a spread flock.
    profile.fish.separationWeight = 0.2f;
    profile.fish.cohesionWeight   = 2.0f;
    profile.fish.alignmentWeight  = 0.2f;
    REQUIRE(backend.configure(profile, "fish-cohesion").ok());
    REQUIRE(backend.simulation()
                .initialize(16,
                            [](int i) {
                                AgentState s;
                                s.position = glm::vec3((i % 4) * 3.f, 0.f, (i / 4) * 3.f);
                                s.velocity = glm::vec3(0.5f, 0.f, 0.f);
                                return s;
                            })
                .ok());
    const float meanBefore = meanNeighborDistance(backend.simulation().states());
    for (int i = 0; i < 180; ++i) {
        REQUIRE(backend.step(1.f / 60.f, env).ok());
    }
    const float meanAfter = meanNeighborDistance(backend.simulation().states());
    CHECK(meanAfter < meanBefore);
}

TEST_CASE("gpuagents.life_trail_deposit_and_follow") {
    GpuAgentWorld world;
    world.surface().initFlat(glm::vec3(-8.f, 0.f, -8.f), 16.f, 32, 0.f);

    auto backend                 = std::make_unique<EffectBackend>();
    auto profile                 = EffectProfile::makeLife(8, 32);
    profile.life.depositStrength = 0.8f;
    profile.life.trailDecayRate  = 0.02f;
    profile.life.trailFollow     = 2.0f;
    profile.life.moveSpeed       = 1.2f;
    REQUIRE(backend->configure(profile, "life").ok());
    REQUIRE(backend->simulation()
                .initialize(4,
                            [](int i) {
                                AgentState s;
                                s.position = glm::vec3(-2.f + i * 0.5f, 0.f, 0.f);
                                s.velocity = glm::vec3(1.f, 0.f, 0.f);
                                return s;
                            })
                .ok());

    EffectBackend* raw = backend.get();
    REQUIRE(world.registerBackend("life", raw).ok());

    for (int i = 0; i < 60; ++i) {
        REQUIRE(world.stepAll(1.f / 60.f).ok());
    }
    float trailSum = 0.f;
    for (const auto& c : world.surface().lifeField) trailSum += c.r;
    CHECK(trailSum > 0.5f);
    CHECK(raw->simulation().aliveCount() == 4);
    CHECK(raw->renderer().instanceCount() == 4);
}

TEST_CASE("gpuagents.bird_speed_band_and_stall_recovery") {
    EffectBackend backend;
    auto          profile    = EffectProfile::makeBird(8);
    profile.bird.stallSpeed  = 2.5f;
    profile.bird.cruiseSpeed = 6.f;
    profile.bird.maxSpeed    = 9.f;
    REQUIRE(backend.configure(profile).ok());
    REQUIRE(backend.simulation()
                .initialize(4,
                            [](int i) {
                                AgentState s;
                                s.position = glm::vec3(i * 2.f, 5.f, 0.f);
                                // Start nearly stalled.
                                s.velocity = glm::vec3(0.4f, -0.2f, 0.f);
                                return s;
                            })
                .ok());

    EnvironmentSnapshot env;
    env.groundY  = 0.f;
    env.ceilingY = 40.f;
    ObstacleField empty;
    env.obstacles = &empty;
    for (int i = 0; i < 120; ++i) {
        REQUIRE(backend.step(1.f / 60.f, env).ok());
    }
    for (const auto& s : backend.simulation().states()) {
        if (!s.alive) continue;
        const float speed = glm::length(s.velocity);
        CHECK(speed >= profile.bird.stallSpeed * 0.45f);
        CHECK(speed <= profile.bird.maxSpeed + 0.05f);
        // Stall recovery should not leave birds buried under ground clearance forever.
        CHECK(s.position.y > env.groundY);
    }
}

TEST_CASE("gpuagents.petal_settles_on_ground") {
    EffectBackend backend;
    auto          profile     = EffectProfile::makePetal(16);
    profile.petal.groundY     = 0.f;
    profile.petal.lifetime    = 30.f;
    profile.petal.settleSpeed = 0.5f;
    REQUIRE(backend.configure(profile).ok());
    REQUIRE(backend.simulation()
                .initialize(8,
                            [](int i) {
                                AgentState s;
                                s.position      = glm::vec3((i % 4) * 0.5f, 1.5f + 0.2f * i, (i / 4) * 0.5f);
                                s.velocity      = glm::vec3(0.f, -0.2f, 0.f);
                                s.customData[1] = 1.f;
                                return s;
                            })
                .ok());

    EnvironmentSnapshot env;
    env.windVelocity = glm::vec3(0.2f, 0.f, 0.f);
    ObstacleField empty;
    env.obstacles = &empty;
    for (int i = 0; i < 240; ++i) {
        REQUIRE(backend.step(1.f / 60.f, env).ok());
    }
    int settled = 0;
    for (const auto& s : backend.simulation().states()) {
        if (!s.alive) continue;
        if (s.customData[0] > 0.5f) ++settled;
    }
    CHECK(settled >= 1);
}

TEST_CASE("gpuagents.world_registers_and_module_spawn") {
    GpuAgents module;
    auto*     world = module.newWorld();
    REQUIRE(world != nullptr);
    world->obstacles().bakeEmpty(glm::vec3(-10.f), glm::ivec3(16, 16, 16), 1.f);
    world->obstacles().carveSphere(glm::vec3(0.f, 0.f, 0.f), 2.f);
    world->setWaterCurrent(0.2f, 0.f, 0.f);

    std::unique_ptr<EffectBackend> backend(module.newBackend(0, 32));
    REQUIRE(backend != nullptr);
    REQUIRE(module.spawnCloud(backend.get(), 12, 0.f, 1.f, 5.f, 1.5f).ok());
    REQUIRE(world->registerBackend("school", backend.get()).ok());
    CHECK(world->backendCount() == 1);

    for (int i = 0; i < 30; ++i) {
        REQUIRE(world->stepAll(1.f / 60.f).ok());
    }
    auto* found = world->find("school");
    REQUIRE(found != nullptr);
    CHECK(found->simulation().aliveCount() == 12);
    CHECK(found->renderer().instanceCount() == 12);

    // Agents that start inside the carved sphere must be pushed out.
    EffectBackend trapped;
    auto          trappedProfile = EffectProfile::makeFish(4);
    REQUIRE(trapped.configure(trappedProfile).ok());
    REQUIRE(trapped.simulation()
                .initialize(2,
                            [](int i) {
                                AgentState s;
                                s.position = glm::vec3(0.1f * i, 0.f, 0.f);
                                s.velocity = glm::vec3(0.5f, 0.f, 0.f);
                                return s;
                            })
                .ok());
    REQUIRE(world->registerBackend("trapped", &trapped).ok());
    for (int i = 0; i < 30; ++i) {
        REQUIRE(world->stepAll(1.f / 60.f).ok());
    }
    for (const auto& s : trapped.simulation().states()) {
        if (!s.alive) continue;
        CHECK(glm::length(s.position) > 1.7f);
    }
    delete world;
}

TEST_CASE("gpuagents.reset_clears_alive") {
    EffectBackend backend;
    REQUIRE(backend.configure(EffectProfile::makeFish(8)).ok());
    REQUIRE(backend.simulation()
                .initialize(5,
                            [](int i) {
                                AgentState s;
                                s.position = glm::vec3(i, 0, 0);
                                s.velocity = glm::vec3(1, 0, 0);
                                return s;
                            })
                .ok());
    CHECK(backend.simulation().aliveCount() == 5);
    backend.reset();
    CHECK(backend.simulation().aliveCount() == 0);
    CHECK(backend.renderer().sync(backend.simulation().states()) == 0);
}
}  // namespace test_gpuagents
#endif
