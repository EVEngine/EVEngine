#include "dialogue/ConversationAuthoring.h"
#include "dialogue/editing/DialogueGraph.h"
#include "dialogue/editor/EditorDialoguePreview.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_dialogue_graph.cpp
using namespace eve::dialogue_editing;
using namespace eve::editing;

namespace {

GraphDocumentData conversationGraph(DialogueGraphDomain& domain) {
    auto line   = domain.makeNode(GraphNodeId("greeting"), "line");
    auto choice = domain.makeNode(GraphNodeId("answer"), "choice");
    auto route  = domain.makeRouteNode(GraphNodeId("answer.yes"), "Yes");
    auto end    = domain.makeNode(GraphNodeId("done"), "end");
    REQUIRE(line.ok());
    REQUIRE(choice.ok());
    REQUIRE(route.ok());
    REQUIRE(end.ok());
    auto* lineProperties = line.value().properties.getIf<EditorValue::Object>();
    REQUIRE(lineProperties);
    (*lineProperties)["speaker"] = "guide";
    (*lineProperties)["text"]    = "Continue?";
    (*lineProperties)["i18nKey"] = "dialogue.guide.continue";
    (*lineProperties)["voice"]   = "asset://voice/continue.ogg";

    GraphDocument graph;
    REQUIRE(graph.createNode(line.value()).ok());
    REQUIRE(graph.createNode(choice.value()).ok());
    REQUIRE(graph.createNode(route.value()).ok());
    REQUIRE(graph.createNode(end.value()).ok());
    const auto connect = [&](const char* edge, const char* from, const char* to) {
        const auto* output = graph.findPin(GraphPinId(from));
        const auto* input  = graph.findPin(GraphPinId(to));
        REQUIRE(output);
        REQUIRE(input);
        REQUIRE(graph.connect({StableId(edge), output->id, input->id}, domain.canConnect(*output, *input)).ok());
    };
    connect("line-choice", "greeting.out", "answer.in");
    connect("choice-route", "answer.out", "answer.yes.in");
    connect("route-end", "answer.yes.out", "done.in");
    EditorValue::Array parameters{"has_met_guide"};
    REQUIRE(
        graph
            .setParameters(EditorValue::Object{
                {"id", "intro"}, {"version", int64_t{2}}, {"entry", "greeting"}, {"parameters", std::move(parameters)}})
            .ok());
    return graph.snapshot(domain.domain());
}

}  // namespace

TEST_CASE("editor.dialogue.graph_compiles_nodes_routes_and_localization_metadata") {
    DialogueGraphDomain              domain;
    const GraphDocumentData          graph    = conversationGraph(domain);
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
    GraphDocumentData   graph = conversationGraph(domain);
    graph.edges.erase(graph.edges.begin() + 1);
    const auto incomplete = domain.compile(graph);
    CHECK_EQ(static_cast<int>(incomplete.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!incomplete.diagnostics.empty());

    graph               = conversationGraph(domain);
    graph.schemaVersion = 3;
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Unsupported));

    graph            = conversationGraph(domain);
    auto* parameters = graph.parameters.getIf<EditorValue::Object>();
    REQUIRE(parameters);
    (*parameters)["entry"] = "answer.yes";
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Failed));
}

TEST_CASE("editor.dialogue.graph_builds_real_conversation_document") {
    DialogueGraphDomain         domain;
    const GraphDocumentData     graph = conversationGraph(domain);
    DialogueGraphRuntimeBuilder builder;
    auto                        built = builder.build(graph);
    REQUIRE(built.ok());
    auto* document = built.value();
    REQUIRE(document);
    CHECK_EQ(document->getId(), "intro");
    CHECK_EQ(document->getEntry(), "greeting");
    CHECK_EQ(document->getNodeCount(), 3);
    CHECK_EQ(document->getField("greeting", "next"), "answer");
    CHECK_EQ(document->getRouteLabel("answer", 0), "Yes");
    CHECK_EQ(document->getRouteTarget("answer", 0), "done");
    CHECK(document->validate());
    delete document;
}

// editor_dialogue_preview.cpp
using namespace eve::editor;

TEST_CASE("editor.dialogue.subtitle_preview_checks_safe_area_readability_and_lip_envelope") {
    EditorAudioPcm          pcm{4, 1, {0.0F, 0.5F, 1.0F, 0.5F}};
    AudioWaveformService    waveformService;
    const auto              waveform = waveformService.generate({"voice", 9, 4, 0.0, 0.0}, pcm);
    DialogueSubtitleRequest request;
    request.lineId                   = "line-1";
    request.locale                   = "en";
    request.speaker                  = "Ada";
    request.text                     = "This sentence wraps into a deterministic subtitle preview.";
    request.sourceRevision           = 9;
    request.viewportWidth            = 1920.0;
    request.viewportHeight           = 1080.0;
    request.maximumCharactersPerLine = 24;
    request.maximumLines             = 3;
    request.voiceDuration            = 1.0;
    DialogueSubtitlePreviewService service;
    const auto                     preview = service.build(request, &waveform);
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(preview.safeX, 96.0);
    CHECK_EQ(preview.safeWidth, 1728.0);
    CHECK(preview.lines.size() > 1U);
    CHECK_EQ(preview.lipEnvelope.size(), 4U);
    REQUIRE(!preview.diagnostics.empty());

    request.maximumLines = 1;
    CHECK_EQ(static_cast<int>(service.build(request).status), static_cast<int>(EditorStatus::Failed));
    request.maximumLines   = 3;
    request.sourceRevision = 10;
    const auto stale       = service.build(request, &waveform);
    CHECK_EQ(static_cast<int>(stale.status), static_cast<int>(EditorStatus::Applied));
    CHECK(stale.lipEnvelope.empty());
}
