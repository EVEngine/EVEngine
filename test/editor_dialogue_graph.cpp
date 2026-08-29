#include "editor/EditorDialogueGraph.h"

#include "dialogue/ConversationAuthoring.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {

GraphDocumentData conversationGraph(DialogueGraphDomain& domain) {
    auto line = domain.makeNode(GraphNodeId("greeting"), "line");
    auto choice = domain.makeNode(GraphNodeId("answer"), "choice");
    auto route = domain.makeRouteNode(GraphNodeId("answer.yes"), "Yes");
    auto end = domain.makeNode(GraphNodeId("done"), "end");
    REQUIRE(line.value);
    REQUIRE(choice.value);
    REQUIRE(route.value);
    REQUIRE(end.value);
    auto* lineProperties = line.value->properties.getIf<EditorValue::Object>();
    REQUIRE(lineProperties);
    (*lineProperties)["speaker"] = "guide";
    (*lineProperties)["text"] = "Continue?";
    (*lineProperties)["i18nKey"] = "dialogue.guide.continue";
    (*lineProperties)["voice"] = "asset://voice/continue.ogg";

    GraphDocument graph;
    REQUIRE(graph.createNode(*line.value).accepted());
    REQUIRE(graph.createNode(*choice.value).accepted());
    REQUIRE(graph.createNode(*route.value).accepted());
    REQUIRE(graph.createNode(*end.value).accepted());
    const auto connect = [&](const char* edge, const char* from, const char* to) {
        const auto* output = graph.findPin(GraphPinId(from));
        const auto* input = graph.findPin(GraphPinId(to));
        REQUIRE(output);
        REQUIRE(input);
        REQUIRE(graph.connect({StableId(edge), output->id, input->id},
                              domain.canConnect(*output, *input)).accepted());
    };
    connect("line-choice", "greeting.out", "answer.in");
    connect("choice-route", "answer.out", "answer.yes.in");
    connect("route-end", "answer.yes.out", "done.in");
    EditorValue::Array parameters{"has_met_guide"};
    REQUIRE(graph.setParameters(EditorValue::Object{{"id", "intro"},
                                                     {"version", int64_t{2}},
                                                     {"entry", "greeting"},
                                                     {"parameters", std::move(parameters)}}).accepted());
    return graph.snapshot(domain.domain());
}

}  // namespace

TEST_CASE("editor.dialogue.graph_compiles_nodes_routes_and_localization_metadata") {
    DialogueGraphDomain domain;
    const GraphDocumentData graph = conversationGraph(domain);
    const DialogueGraphCompileResult compiled = domain.compile(graph);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(compiled.documentRevision, graph.revision);
    const auto* definition = compiled.definition.getIf<EditorValue::Object>();
    REQUIRE(definition);
    CHECK_EQ(*definition->at("id").getIf<std::string>(), "intro");
    CHECK_EQ(*definition->at("version").getIf<int64_t>(), int64_t{2});
    CHECK_EQ(definition->at("nodes").getIf<EditorValue::Array>()->size(), size_t{3});
}

TEST_CASE("editor.dialogue.graph_rejects_bypassed_or_incomplete_routes") {
    DialogueGraphDomain domain;
    GraphDocumentData graph = conversationGraph(domain);
    graph.edges.erase(graph.edges.begin() + 1);
    const auto incomplete = domain.compile(graph);
    CHECK_EQ(static_cast<int>(incomplete.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!incomplete.diagnostics.empty());

    graph = conversationGraph(domain);
    graph.schemaVersion = 3;
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Unsupported));

    graph = conversationGraph(domain);
    auto* parameters = graph.parameters.getIf<EditorValue::Object>();
    REQUIRE(parameters);
    (*parameters)["entry"] = "answer.yes";
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Failed));
}

TEST_CASE("editor.dialogue.graph_builds_real_conversation_document") {
    DialogueGraphDomain domain;
    const GraphDocumentData graph = conversationGraph(domain);
    DialogueGraphRuntimeBuilder builder;
    auto built = builder.build(graph);
    REQUIRE(built.accepted());
    REQUIRE(built.value);
    auto* document = *built.value;
    CHECK_EQ(document->getId(), "intro");
    CHECK_EQ(document->getEntry(), "greeting");
    CHECK_EQ(document->getNodeCount(), 3);
    CHECK_EQ(document->getField("greeting", "next"), "answer");
    CHECK_EQ(document->getRouteLabel("answer", 0), "Yes");
    CHECK_EQ(document->getRouteTarget("answer", 0), "done");
    CHECK(document->validate());
    delete document;
}
