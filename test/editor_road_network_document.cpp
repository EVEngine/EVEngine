#include "procgen/editing/RoadNetworkDocument.h"
#include "procgen/editing/RoadNetworkGizmo.h"

#include "editing/EditingGraph.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>

using namespace eve::editing;
using namespace eve::procgen_editing;

namespace {

RoadNodeRecord node(const char* id, double x, double y, double z, double radius = 6.0) {
    return {StableId(id), x, y, z, radius};
}

RoadEdgeRecord edge(const char* id, const char* from, const char* to,
                    std::vector<RoadControlPointRecord> points, int lanes = 2) {
    RoadEdgeRecord result;
    result.id            = StableId(id);
    result.from          = StableId(from);
    result.to            = StableId(to);
    result.controlPoints = std::move(points);
    result.lanesForward  = lanes;
    result.lanesBackward = 0;
    result.laneWidth     = 3.5;
    return result;
}

}  // namespace

TEST_CASE("editor.roadNetwork.operationsAreReversibleAndSnapshotIsAtomic") {
    RoadNetworkDocument document("city-roads");
    REQUIRE(document.applyDomainOperation(document.makeSetNode(node("a", 0.0, 0.0, 0.0)).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetNode(node("b", 20.0, 0.0, 0.0)).value()).ok());
    REQUIRE(document
                .applyDomainOperation(document
                                          .makeSetEdge(edge("ab", "a", "b",
                                                            {{0.0, 0.0, 0.0}, {10.0, 0.0, 2.0}, {20.0, 0.0, 0.0}}))
                                          .value())
                .ok());

    auto moved = document.makeMoveNode(StableId("b"), 24.0, 0.0, 1.0);
    REQUIRE(moved.ok());
    REQUIRE(document.applyDomainOperation(moved.value()).ok());
    CHECK(std::abs(document.nodes()[1].x - 24.0) < 1e-9);
    CHECK(std::abs(document.edges()[0].controlPoints.back().x - 24.0) < 1e-9);

    DomainOperation undo = moved.value();
    undo.type            = moved.value().inverseType;
    undo.payload         = moved.value().inverse;
    REQUIRE(document.applyDomainOperation(undo).ok());
    CHECK(std::abs(document.nodes()[1].x - 20.0) < 1e-9);
    CHECK(std::abs(document.edges()[0].controlPoints.back().x - 20.0) < 1e-9);

    auto  snapshot = document.snapshotValue();
    auto* object   = snapshot.getIf<EditorValue::Object>();
    REQUIRE(object != nullptr);
    (*object)["futureField"] = EditorValue("preserved-by-newer-writer");

    RoadNetworkDocument restored("city-roads-copy");
    REQUIRE(restored.loadSnapshot(snapshot).ok());
    CHECK_EQ(restored.nodes().size(), std::size_t(2));
    CHECK_EQ(restored.edges().size(), std::size_t(1));
    REQUIRE(restored.compileNetwork().ok());
    REQUIRE(restored.previewBake().ok());

    const auto before          = restored.snapshotValue();
    (*object)["schemaVersion"] = EditorValue(std::int64_t{99});
    CHECK(!restored.loadSnapshot(snapshot).ok());
    CHECK(restored.snapshotValue() == before);
}

TEST_CASE("editor.roadNetwork.rejectDeleteNodeWhileReferenced") {
    RoadNetworkDocument document("guarded");
    REQUIRE(document.applyDomainOperation(document.makeSetNode(node("a", 0.0, 0.0, 0.0)).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetNode(node("b", 10.0, 0.0, 0.0)).value()).ok());
    REQUIRE(document
                .applyDomainOperation(
                    document.makeSetEdge(edge("ab", "a", "b", {{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}})).value())
                .ok());
    const auto before = document.snapshotValue();
    auto       blocked = document.makeDeleteNode(StableId("a"));
    CHECK(!blocked.ok());
    CHECK(document.snapshotValue() == before);
    REQUIRE(document.applyDomainOperation(document.makeDeleteEdge(StableId("ab")).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeDeleteNode(StableId("a")).value()).ok());
    CHECK_EQ(document.nodes().size(), std::size_t(1));
}

TEST_CASE("editor.roadNetwork.gizmoShowsNodesEdgesAndInteriorHandles") {
    RoadNetworkDocument document("overlay");
    REQUIRE(document.applyDomainOperation(document.makeSetNode(node("a", 0.0, 0.0, 0.0)).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetNode(node("b", 16.0, 0.0, 0.0)).value()).ok());
    REQUIRE(document
                .applyDomainOperation(document
                                          .makeSetEdge(edge("ab", "a", "b",
                                                            {{0.0, 0.0, 0.0}, {8.0, 0.0, 4.0}, {16.0, 0.0, 0.0}}))
                                          .value())
                .ok());
    const auto gizmo = RoadNetworkGizmoBuilder().build(document);
    CHECK(gizmo.status == EditorStatus::Applied);
    CHECK_EQ(gizmo.targetRevision, document.revision());
    CHECK(std::any_of(gizmo.primitives.begin(), gizmo.primitives.end(),
                      [](const auto& primitive) { return primitive.id == "road.node.a"; }));
    CHECK(std::any_of(gizmo.primitives.begin(), gizmo.primitives.end(),
                      [](const auto& primitive) { return primitive.id == "road.cp.ab.1"; }));
    CHECK(std::any_of(gizmo.primitives.begin(), gizmo.primitives.end(), [](const auto& primitive) {
        return primitive.id.find("road.edge.ab.segment") == 0;
    }));
}

TEST_CASE("editor.roadNetwork.gizmoDragPreviewsThenCommitsOneOperation") {
    RoadNetworkDocument document("drag-road");
    REQUIRE(document.applyDomainOperation(document.makeSetNode(node("a", 0.0, 0.0, 0.0)).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetNode(node("b", 12.0, 0.0, 0.0)).value()).ok());
    REQUIRE(document
                .applyDomainOperation(document
                                          .makeSetEdge(edge("ab", "a", "b",
                                                            {{0.0, 0.0, 0.0}, {6.0, 0.0, 3.0}, {12.0, 0.0, 0.0}}))
                                          .value())
                .ok());

    const auto            beforeRevision = document.revision();
    RoadNetworkDragSession drag(&document);
    auto                   begun = drag.beginDrag(12.0, 5.0, 0.0, 0.0, -1.0, 0.0);
    REQUIRE(begun.ok());
    CHECK(drag.activeHandle() == "node");
    CHECK(drag.activeNode() == StableId("b"));
    auto preview = drag.updateDrag(18.0, 5.0, 2.0, 0.0, -1.0, 0.0);
    REQUIRE(preview.ok());
    CHECK(std::abs(preview.value().nodeDraft.x - 18.0) < 1e-9);
    CHECK_EQ(document.revision(), beforeRevision);
    CHECK(std::abs(document.nodes()[1].x - 12.0) < 1e-9);

    auto operation = drag.finishDrag();
    REQUIRE(operation.ok());
    REQUIRE(document.applyDomainOperation(operation.value()).ok());
    CHECK(std::abs(document.nodes()[1].x - 18.0) < 1e-9);
    CHECK(std::abs(document.edges()[0].controlPoints.back().x - 18.0) < 1e-9);
}

TEST_CASE("editor.roadNetwork.gizmoDragRejectsConcurrentDocumentMutation") {
    RoadNetworkDocument document("conflicted-road");
    REQUIRE(document.applyDomainOperation(document.makeSetNode(node("a", 0.0, 0.0, 0.0)).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetNode(node("b", 8.0, 0.0, 0.0)).value()).ok());
    RoadNetworkDragSession drag(&document);
    REQUIRE(drag.beginDrag(0.0, 5.0, 0.0, 0.0, -1.0, 0.0).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetNode(node("c", 4.0, 0.0, 4.0)).value()).ok());
    auto stale = drag.updateDrag(1.0, 5.0, 0.0, 0.0, -1.0, 0.0);
    CHECK(!stale.ok());
    CHECK(stale.code() == EditorStatus::Conflict);
    CHECK(!drag.isDragging());
}

TEST_CASE("editor.roadNetwork.laySessionChainsNodesAndEdges") {
    RoadNetworkDocument document("lay-road");
    RoadLayDefaults     defaults;
    defaults.snapRadius = 2.5;
    RoadLaySession lay(&document, defaults);
    REQUIRE(lay.begin().ok());

    auto first = lay.commitClick(0.0, 0.0, 0.0, StableId("n0"));
    REQUIRE(first.ok());
    CHECK_EQ(first.value().operations.size(), std::size_t(1));
    for (const auto& operation : first.value().operations)
        REQUIRE(document.applyDomainOperation(operation).ok());
    CHECK(lay.activeFrom() == StableId("n0"));

    auto hover = lay.updateHover(20.0, 0.0, 0.0);
    REQUIRE(hover.ok());
    CHECK(hover.value().hasHover);
    CHECK(std::any_of(hover.value().gizmo.primitives.begin(), hover.value().gizmo.primitives.end(),
                      [](const auto& primitive) { return primitive.id == "road.lay.preview"; }));

    auto second = lay.commitClick(20.0, 0.0, 0.0, StableId("n1"), StableId("e0"));
    REQUIRE(second.ok());
    CHECK_EQ(second.value().operations.size(), std::size_t(2));
    for (const auto& operation : second.value().operations)
        REQUIRE(document.applyDomainOperation(operation).ok());
    CHECK_EQ(document.nodes().size(), std::size_t(2));
    CHECK_EQ(document.edges().size(), std::size_t(1));
    REQUIRE(document.compileNetwork().ok());
    REQUIRE(document.previewBake().ok());

    auto snapped = lay.commitClick(20.5, 0.0, 0.2, StableId("unused"), StableId("e1"));
    // tip snaps to n1 == activeFrom → rejected zero-length
    CHECK(!snapped.ok());

    REQUIRE(document.applyDomainOperation(document.makeSetNode(node("n2", 20.0, 0.0, 20.0)).value()).ok());
    auto branch = lay.commitClick(20.0, 0.0, 20.0, StableId("unused2"), StableId("e1"));
    REQUIRE(branch.ok());
    CHECK(branch.value().createdNode.empty());
    CHECK_EQ(branch.value().operations.size(), std::size_t(1));
    for (const auto& operation : branch.value().operations)
        REQUIRE(document.applyDomainOperation(operation).ok());
    CHECK_EQ(document.edges().size(), std::size_t(2));
    CHECK(lay.activeFrom() == StableId("n2"));
}
