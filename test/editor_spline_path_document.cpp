#include "procgen/editing/MeshModifierGraph.h"
#include "procgen/editing/SplinePathDocument.h"
#include "procgen/editing/SplinePathGizmo.h"

#include "editing/EditingGraph.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>

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
