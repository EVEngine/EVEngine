#include "editor/EditorAnimationGraph.h"

#include "animation/AnimClip.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimStateMachine.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>

using namespace eve::editor;

namespace {

GraphDocumentData basicGraph(AnimationStateGraphDomain& domain) {
    auto idle = domain.makeStateNode(GraphNodeId("idle"), "asset://animations/idle.eva");
    auto run = domain.makeStateNode(GraphNodeId("run"), "asset://animations/run.eva");
    auto transition = domain.makeTransitionNode(GraphNodeId("idle-to-run"));
    REQUIRE(idle.value);
    REQUIRE(run.value);
    REQUIRE(transition.value);

    EditorValue::Object transitionProperties = *transition.value->properties.getIf<EditorValue::Object>();
    transitionProperties["blendSeconds"] = 0.15;
    transitionProperties["conditionKind"] = "float";
    transitionProperties["parameter"] = "speed";
    transitionProperties["operator"] = ">";
    transitionProperties["threshold"] = 0.1;
    transition.value->properties = EditorValue(std::move(transitionProperties));

    GraphDocument graph;
    REQUIRE(graph.createNode(*idle.value).isAccepted());
    REQUIRE(graph.createNode(*run.value).isAccepted());
    REQUIRE(graph.createNode(*transition.value).isAccepted());
    const GraphPinRecord* idleOut = graph.findPin(GraphPinId("idle.out"));
    const GraphPinRecord* transitionIn = graph.findPin(GraphPinId("idle-to-run.in"));
    const GraphPinRecord* transitionOut = graph.findPin(GraphPinId("idle-to-run.out"));
    const GraphPinRecord* runIn = graph.findPin(GraphPinId("run.in"));
    REQUIRE(idleOut);
    REQUIRE(transitionIn);
    REQUIRE(transitionOut);
    REQUIRE(runIn);
    REQUIRE(graph.connect({StableId("edge-in"), idleOut->id, transitionIn->id},
                          domain.canConnect(*idleOut, *transitionIn)).isAccepted());
    REQUIRE(graph.connect({StableId("edge-out"), transitionOut->id, runIn->id},
                          domain.canConnect(*transitionOut, *runIn)).isAccepted());
    EditorValue::Object parameters;
    parameters["entry"] = "idle";
    REQUIRE(graph.setParameters(EditorValue(std::move(parameters))).isAccepted());
    return graph.snapshot(domain.domain());
}

}  // namespace

TEST_CASE("editor.animation.state_graph_compiles_stable_clip_references_and_conditions") {
    AnimationStateGraphDomain domain;
    const auto graph = basicGraph(domain);
    const auto compiled = domain.compile(graph);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(compiled.documentRevision, graph.revision);
    CHECK(compiled.definition.find("EVANIM_STATE_GRAPH 1\n") == 0);
    CHECK(compiled.definition.find("state \"idle\" \"asset://animations/idle.eva\"") != std::string::npos);
    CHECK(compiled.definition.find("transition \"idle-to-run\" \"idle\" \"run\" 0.15") !=
          std::string::npos);
    CHECK(compiled.definition.find("\"float\" \"speed\" \">\" 0.1") != std::string::npos);
}

TEST_CASE("editor.animation.state_graph_rejects_incomplete_or_invalid_authoring_data") {
    AnimationStateGraphDomain domain;
    GraphDocumentData graph = basicGraph(domain);
    graph.schemaVersion = 99;
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Unsupported));

    graph = basicGraph(domain);
    graph.edges.pop_back();
    auto result = domain.compile(graph);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!result.diagnostics.empty());

    graph = basicGraph(domain);
    auto transition = std::find_if(graph.nodes.begin(), graph.nodes.end(), [](const GraphNodeRecord& node) {
        return node.type == "animation.transition";
    });
    REQUIRE(transition != graph.nodes.end());
    auto* properties = transition->properties.getIf<EditorValue::Object>();
    REQUIRE(properties);
    (*properties)["blendSeconds"] = -1.0;
    result = domain.compile(graph);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Failed));

    graph = basicGraph(domain);
    auto* parameters = graph.parameters.getIf<EditorValue::Object>();
    REQUIRE(parameters);
    (*parameters)["entry"] = "missing";
    result = domain.compile(graph);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Failed));
}

TEST_CASE("editor.animation.state_graph_rejects_direct_state_connections") {
    AnimationStateGraphDomain domain;
    auto idle = domain.makeStateNode(GraphNodeId("idle"), "idle.eva");
    auto run = domain.makeStateNode(GraphNodeId("run"), "run.eva");
    REQUIRE(idle.value);
    REQUIRE(run.value);
    CHECK(domain.canConnect(idle.value->pins[1], run.value->pins[0]).allowed);

    GraphDocumentData invalid;
    invalid.domain = domain.domain();
    invalid.nodes = {*idle.value, *run.value};
    invalid.edges = {{StableId("direct"), idle.value->pins[1].id, run.value->pins[0].id}};
    invalid.parameters = EditorValue::Object{{"entry", "idle"}};
    const auto compiled = domain.compile(invalid);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Failed));
}

TEST_CASE("editor.animation.state_graph_builds_real_runtime_state_machine") {
    AnimationStateGraphDomain domain;
    const GraphDocumentData graph = basicGraph(domain);
    eve::animation::AnimSkeleton skeleton;
    skeleton.addBone("root");
    eve::animation::AnimClip idle("idle");
    eve::animation::AnimClip run("run");
    idle.setDuration(1.0F);
    run.setDuration(1.0F);

    AnimationStateGraphRuntimeBuilder builder;
    auto built = builder.build(graph, &skeleton, [&](const std::string& asset) {
        if (asset == "asset://animations/idle.eva") return &idle;
        if (asset == "asset://animations/run.eva") return &run;
        return static_cast<eve::animation::AnimClip*>(nullptr);
    });
    REQUIRE(built.isAccepted());
    REQUIRE(built.value);
    CHECK_EQ((*built.value)->getStateCount(), 2);
    delete *built.value;

    auto missing = builder.build(graph, &skeleton, [](const std::string&) {
        return static_cast<eve::animation::AnimClip*>(nullptr);
    });
    CHECK_EQ(static_cast<int>(missing.status), static_cast<int>(EditorStatus::NotFound));
}
