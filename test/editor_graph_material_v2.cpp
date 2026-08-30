#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorGraph.h"

#include <string>

using namespace eve::editor;

namespace {

GraphNodeRecord constantNode(const char* id) {
    GraphNodeRecord node;
    node.id         = GraphNodeId(id);
    node.type       = "material.constant";
    node.properties = EditorValue::Object{{"value", EditorValue(1.0)}};
    node.pins.push_back({GraphPinId(std::string(id) + ".out"), node.id, "float", GraphPinDirection::Output});
    return node;
}

GraphNodeRecord outputNode(const char* id) {
    GraphNodeRecord node;
    node.id         = GraphNodeId(id);
    node.type       = "material.output";
    node.properties = EditorValue::Object{};
    node.pins.push_back({GraphPinId(std::string(id) + ".roughness"), node.id, "float", GraphPinDirection::Input});
    return node;
}

}  // namespace

TEST_CASE("editor.v2.graph_core_is_domain_neutral_and_revisioned") {
    GraphDocument       graph;
    MaterialGraphDomain material;
    CHECK(graph.createNode(constantNode("constant")).isAccepted());
    CHECK(graph.createNode(outputNode("output")).isAccepted());
    CHECK_EQ(graph.revision(), static_cast<Revision>(2));

    const GraphPinRecord* from = graph.findPin(GraphPinId("constant.out"));
    const GraphPinRecord* to   = graph.findPin(GraphPinId("output.roughness"));
    CHECK(from != nullptr);
    CHECK(to != nullptr);
    GraphConnectionDecision allowed = material.canConnect(*from, *to);
    CHECK(allowed.allowed);
    CHECK(graph.connect({StableId("edge-1"), from->id, to->id}, allowed).isAccepted());
    CHECK_EQ(graph.revision(), static_cast<Revision>(3));
    CHECK_EQ(graph.snapshot(material.domain()).edges.size(), static_cast<std::size_t>(1));

    GraphConnectionDecision rejected = material.canConnect(*to, *from);
    CHECK(!rejected.allowed);
    CHECK_EQ(static_cast<int>(graph.connect({StableId("edge-2"), to->id, from->id}, rejected).status),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(graph.revision(), static_cast<Revision>(3));
}

TEST_CASE("editor.v2.material_compile_failure_keeps_last_successful_preview") {
    GraphDocument         graph;
    MaterialGraphDomain   domain;
    MaterialEditorService materials;
    DocumentId            document("document:material");
    CHECK(graph.createNode(outputNode("output")).isAccepted());

    auto firstTask = materials.compile(document, graph.snapshot(domain.domain()), domain);
    CHECK(firstTask.isAccepted());
    auto firstResult = materials.result(*firstTask.value);
    CHECK(firstResult.isAccepted());
    CHECK_EQ(static_cast<int>(firstResult.value->status), static_cast<int>(EditorStatus::Applied));
    CHECK(materials.publishPreview(document, graph.revision(), *firstTask.value).isAccepted());
    const std::string firstPreview = materials.previewArtifact(document);
    CHECK(!firstPreview.empty());

    GraphNodeRecord invalid;
    invalid.id         = GraphNodeId("error");
    invalid.type       = "material.error";
    invalid.properties = EditorValue::Object{};
    CHECK(graph.createNode(std::move(invalid)).isAccepted());
    auto failedTask = materials.compile(document, graph.snapshot(domain.domain()), domain);
    CHECK(failedTask.isAccepted());
    CHECK_EQ(static_cast<int>(materials.result(*failedTask.value).value->status),
             static_cast<int>(EditorStatus::Failed));
    CHECK_EQ(static_cast<int>(materials.publishPreview(document, graph.revision(), *failedTask.value).status),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(materials.previewArtifact(document), firstPreview);
}

TEST_CASE("editor.v2.material_stale_compile_cannot_replace_preview") {
    GraphDocument         graph;
    MaterialGraphDomain   domain;
    MaterialEditorService materials;
    DocumentId            document("document:material-stale");
    CHECK(graph.createNode(outputNode("output")).isAccepted());
    auto task = materials.compile(document, graph.snapshot(domain.domain()), domain);
    CHECK(task.isAccepted());

    CHECK(
        graph.setNodeProperties(GraphNodeId("output"), EditorValue::Object{{"opacity", EditorValue(0.5)}}).isAccepted());
    auto stale = materials.publishPreview(document, graph.revision(), *task.value);
    CHECK_EQ(static_cast<int>(stale.status), static_cast<int>(EditorStatus::Conflict));
    CHECK(materials.previewArtifact(document).empty());

    auto currentTask = materials.compile(document, graph.snapshot(domain.domain()), domain);
    CHECK(materials.publishPreview(document, graph.revision(), *currentTask.value).isAccepted());
    CHECK(materials.previewArtifact(document).find("r2") != std::string::npos);
}
