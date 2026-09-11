#include "procgen/editing/MeshModifierGraph.h"

#include "editing/EditingGraph.h"
#include "zeroerr/unittest.h"

#include <cmath>

using namespace eve::editing;
using namespace eve::procgen_editing;

namespace {

eve::procgen::MeshBuild triangle() {
    eve::procgen::MeshBuild mesh;
    mesh.addVertex(0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f);
    mesh.addVertex(1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f);
    mesh.addVertex(0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    return mesh;
}

}  // namespace

TEST_CASE("editor.meshModifierGraph.reflectsCompilesAndPreviewsFusedGraph") {
    MeshModifierGraphDomain domain;
    auto                    input  = domain.makeNode(GraphNodeId("input"), "mesh.input");
    auto                    move   = domain.makeNode(GraphNodeId("move"), "deform.transform");
    auto                    twist  = domain.makeNode(GraphNodeId("twist"), "deform.twist");
    auto                    output = domain.makeNode(GraphNodeId("output"), "mesh.output");
    REQUIRE(input.ok());
    REQUIRE(move.ok());
    REQUIRE(twist.ok());
    REQUIRE(output.ok());
    auto* moveProperties = move.value().properties.getIf<EditorValue::Object>();
    REQUIRE(moveProperties != nullptr);
    (*moveProperties)["x"] = EditorValue(3.0);

    GraphDocument document;
    CHECK(document.createNode(input.value()).ok());
    CHECK(document.createNode(move.value()).ok());
    CHECK(document.createNode(twist.value()).ok());
    CHECK(document.createNode(output.value()).ok());
    const auto connect = [&](const char* edge, const char* from, const char* to) {
        const auto* source = document.findPin(GraphPinId(from));
        const auto* target = document.findPin(GraphPinId(to));
        REQUIRE(source != nullptr);
        REQUIRE(target != nullptr);
        CHECK(document.connect({StableId(edge), source->id, target->id}, domain.canConnect(*source, *target)).ok());
    };
    connect("e0", "input.out", "move.in0");
    connect("e1", "move.out", "twist.in0");
    connect("e2", "twist.out", "output.in0");
    const auto snapshot = document.snapshot(domain.domain());
    const auto compiled = domain.compile(snapshot);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Applied));

    const auto preview = domain.preview(snapshot, "input", triangle(), "output");
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(preview.mesh.getVertexCount(), 3);
    CHECK(std::abs(preview.mesh.getPositionX(0) - 3.f) < 1e-6f);
    CHECK_EQ(preview.fusedOperationCount, 2);
}

TEST_CASE("editor.meshModifierGraph.rejectsWrongDomainAndPinTypes") {
    MeshModifierGraphDomain domain;
    GraphDocumentData       wrong;
    wrong.domain        = "procgen.point";
    const auto compiled = domain.compile(wrong);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Rejected));

    GraphPinRecord scalar{GraphPinId("scalar"), GraphNodeId("a"), "float", GraphPinDirection::Output};
    GraphPinRecord mesh{GraphPinId("mesh"), GraphNodeId("b"), "mesh", GraphPinDirection::Input};
    const auto     decision = domain.canConnect(scalar, mesh);
    CHECK(!decision.allowed);
    CHECK(!decision.diagnostics.empty());
}

TEST_CASE("editor.meshModifierGraph.compileRejectsDisconnectedInputsAndCycles") {
    MeshModifierGraphDomain domain;
    GraphDocument           disconnected;
    auto                    output = domain.makeNode(GraphNodeId("output"), "mesh.output");
    REQUIRE(output.ok());
    CHECK(disconnected.createNode(output.value()).ok());
    auto compiled = domain.compile(disconnected.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Rejected));

    GraphDocument cyclic;
    auto          a = domain.makeNode(GraphNodeId("a"), "deform.bend");
    auto          b = domain.makeNode(GraphNodeId("b"), "deform.twist");
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK(cyclic.createNode(a.value()).ok());
    CHECK(cyclic.createNode(b.value()).ok());
    const auto* aOut = cyclic.findPin(GraphPinId("a.out"));
    const auto* aIn  = cyclic.findPin(GraphPinId("a.in0"));
    const auto* bOut = cyclic.findPin(GraphPinId("b.out"));
    const auto* bIn  = cyclic.findPin(GraphPinId("b.in0"));
    REQUIRE(aOut != nullptr);
    REQUIRE(aIn != nullptr);
    REQUIRE(bOut != nullptr);
    REQUIRE(bIn != nullptr);
    CHECK(cyclic.connect({StableId("e0"), aOut->id, bIn->id}, domain.canConnect(*aOut, *bIn)).ok());
    CHECK(cyclic.connect({StableId("e1"), bOut->id, aIn->id}, domain.canConnect(*bOut, *aIn)).ok());
    compiled = domain.compile(cyclic.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Rejected));
}
