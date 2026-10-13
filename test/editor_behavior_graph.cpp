#include "crowd/Crowd.h"
#include "crowd/editing/CrowdDocument.h"
#include "npc_ai/editing/BehaviorGraph.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_behavior_graph.cpp
using namespace eve::npc_ai_editing;
using namespace eve::editing;

namespace {
GraphNodeRecord node(const char* id, const char* type, bool input, bool output) {
    GraphNodeRecord result{GraphNodeId(id), type, EditorValue::Object{}, {}};
    if (input)
        result.pins.push_back({GraphPinId(std::string(id) + ".in"), result.id, "flow", GraphPinDirection::Input});
    if (output)
        result.pins.push_back({GraphPinId(std::string(id) + ".out"), result.id, "flow", GraphPinDirection::Output});
    return result;
}
}  // namespace

TEST_CASE("editor.behavior.graph_compiles_deterministic_tree") {
    GraphDocument       graph;
    BehaviorGraphDomain domain;
    CHECK(graph.createNode(node("root", "root", false, true)).ok());
    CHECK(graph.createNode(node("sequence", "sequence", true, true)).ok());
    CHECK(graph.createNode(node("condition", "condition", true, false)).ok());
    CHECK(graph.createNode(node("action", "action", true, false)).ok());
    CHECK(graph
              .connect(
                  {StableId("e1"), GraphPinId("root.out"), GraphPinId("sequence.in")},
                  domain.canConnect(*graph.findPin(GraphPinId("root.out")), *graph.findPin(GraphPinId("sequence.in"))))
              .ok());
    CHECK(graph
              .connect({StableId("e2"), GraphPinId("sequence.out"), GraphPinId("condition.in")},
                       domain.canConnect(*graph.findPin(GraphPinId("sequence.out")),
                                         *graph.findPin(GraphPinId("condition.in"))))
              .ok());
    CHECK(graph
              .connect({StableId("e3"), GraphPinId("sequence.out"), GraphPinId("action.in")},
                       domain.canConnect(*graph.findPin(GraphPinId("sequence.out")),
                                         *graph.findPin(GraphPinId("action.in"))))
              .ok());
    auto compiled = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(compiled.root.value(), std::string("root"));
    CHECK_EQ(compiled.instructions.size(), 4U);
    CHECK_EQ(compiled.instructions[1].children.size(), 2U);
}

TEST_CASE("editor.behavior.graph_rejects_cycles_or_unreachable_nodes") {
    BehaviorGraphDomain domain;
    GraphDocumentData   graph;
    graph.domain   = "behavior";
    graph.revision = 7;
    graph.nodes    = {node("root", "root", false, true), node("orphan", "action", true, false)};
    auto compiled  = domain.compile(graph);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Failed));
    CHECK(!compiled.diagnostics.empty());
    auto wrongDirection = domain.canConnect({GraphPinId("a"), GraphNodeId("a"), "flow", GraphPinDirection::Input},
                                            {GraphPinId("b"), GraphNodeId("b"), "flow", GraphPinDirection::Output});
    CHECK(!wrongDirection.allowed);
}

// editor_crowd_document.cpp
using namespace eve::crowd_editing;
using namespace eve::editing;

namespace {
CrowdPathRecord patrolPath() {
    return {
        StableId("patrol"), "Patrol", true, {{StableId("a"), 0.0, 0.0, 0.5, 0.0}, {StableId("b"), 5.0, 0.0, 0.5, 1.0}}};
}

CrowdAgentRecord guard() {
    CrowdAgentRecord result;
    result.id        = StableId("guard");
    result.archetype = "guard";
    result.x         = 1.0;
    result.y         = 2.0;
    result.behavior  = "path";
    result.path      = StableId("patrol");
    return result;
}
}  // namespace

TEST_CASE("editor.crowd.agent_zone_path_operations_are_reversible") {
    CrowdDocumentTarget document("level-ai");
    auto                path = document.makeSetPath(patrolPath());
    REQUIRE(path.ok());
    CHECK(document.applyDomainOperation(path.value()).ok());

    auto agent = document.makeSetAgent(guard());
    REQUIRE(agent.ok());
    CHECK(document.applyDomainOperation(agent.value()).ok());
    CHECK_EQ(document.agents().size(), 1U);
    CHECK_EQ(document.paths().size(), 1U);
    CHECK_EQ(static_cast<int>(document.makeDeletePath(StableId("patrol")).code()),
             static_cast<int>(EditorStatus::Conflict));

    DomainOperation undoAgent = agent.value();
    undoAgent.type            = agent.value().inverseType;
    undoAgent.payload         = agent.value().inverse;
    CHECK(document.applyDomainOperation(undoAgent).ok());
    CHECK(document.agents().empty());
    CHECK(document.makeDeletePath(StableId("patrol")).ok());

    CrowdZoneRecord zone{
        StableId("gate"), "Gate", "sense", {{{0.0, 0.0}}, {{3.0, 0.0}}, {{3.0, 2.0}}, {{0.0, 2.0}}}, 2.0, true};
    auto setZone = document.makeSetZone(zone);
    REQUIRE(setZone.ok());
    CHECK(document.applyDomainOperation(setZone.value()).ok());
    CHECK_EQ(document.zones().front().points.size(), 4U);
}

TEST_CASE("editor.crowd.snapshot_and_overlay_are_revision_and_budget_safe") {
    CrowdDocumentTarget source("source");
    auto                path = source.makeSetPath(patrolPath());
    REQUIRE(path.ok());
    CHECK(source.applyDomainOperation(path.value()).ok());
    auto agent = source.makeSetAgent(guard());
    REQUIRE(agent.ok());
    CHECK(source.applyDomainOperation(agent.value()).ok());

    CrowdDocumentTarget loaded("loaded");
    CHECK(loaded.loadSnapshot(source.snapshotValue()).ok());
    CHECK(loaded.validate().empty());
    auto overlay = loaded.overlay();
    CHECK_EQ(static_cast<int>(overlay.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(overlay.revision, loaded.revision());
    CHECK_EQ(overlay.primitives.size(), 2U);
    CHECK_EQ(static_cast<int>(loaded.overlay(1).status), static_cast<int>(EditorStatus::Rejected));

    EditorValue invalid = EditorValue::Object{{"schemaVersion", int64_t{1}},
                                              {"paths", EditorValue::Array{}},
                                              {"zones", EditorValue::Array{}},
                                              {"agents", EditorValue::Array{EditorValue::Object{{"id", "lost"},
                                                                                                {"archetype", "guard"},
                                                                                                {"x", 0.0},
                                                                                                {"y", 0.0},
                                                                                                {"heading", 0.0},
                                                                                                {"radius", 0.5},
                                                                                                {"maximumSpeed", 1.0},
                                                                                                {"behavior", "path"},
                                                                                                {"path", "missing"}}}}};
    CHECK_EQ(static_cast<int>(loaded.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(loaded.agents().size(), 1U);
}

TEST_CASE("editor.crowd.document_applies_to_named_runtime_agents") {
    CrowdDocumentTarget document("runtime-source");
    auto                path = document.makeSetPath(patrolPath());
    REQUIRE(path.ok());
    CHECK(document.applyDomainOperation(path.value()).ok());
    auto agent = document.makeSetAgent(guard());
    REQUIRE(agent.ok());
    CHECK(document.applyDomainOperation(agent.value()).ok());
    eve::crowd::Crowd   runtime;
    CrowdRuntimeApplier applier;
    CHECK(applier.apply(document, &runtime).ok());
    CHECK_EQ(runtime.getAgentCount(), 1);
    CHECK(runtime.hasNamedAgent("guard"));
    const int index = runtime.getNamedAgentIndex("guard");
    CHECK_EQ(runtime.getAgentAction(index), std::string("seek"));
}
