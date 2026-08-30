#include "editor/EditorBehaviorGraph.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {
GraphNodeRecord node(const char* id, const char* type, bool input, bool output) {
    GraphNodeRecord result{GraphNodeId(id), type, EditorValue::Object{}, {}};
    if (input) result.pins.push_back({GraphPinId(std::string(id) + ".in"), result.id,
                                     "flow", GraphPinDirection::Input});
    if (output) result.pins.push_back({GraphPinId(std::string(id) + ".out"), result.id,
                                      "flow", GraphPinDirection::Output});
    return result;
}
}  // namespace

TEST_CASE("editor.behavior.graph_compiles_deterministic_tree") {
    GraphDocument graph;
    BehaviorGraphDomain domain;
    CHECK(graph.createNode(node("root", "root", false, true)).isAccepted());
    CHECK(graph.createNode(node("sequence", "sequence", true, true)).isAccepted());
    CHECK(graph.createNode(node("condition", "condition", true, false)).isAccepted());
    CHECK(graph.createNode(node("action", "action", true, false)).isAccepted());
    CHECK(graph.connect({StableId("e1"), GraphPinId("root.out"), GraphPinId("sequence.in")},
                        domain.canConnect(*graph.findPin(GraphPinId("root.out")),
                                          *graph.findPin(GraphPinId("sequence.in")))).isAccepted());
    CHECK(graph.connect({StableId("e2"), GraphPinId("sequence.out"), GraphPinId("condition.in")},
                        domain.canConnect(*graph.findPin(GraphPinId("sequence.out")),
                                          *graph.findPin(GraphPinId("condition.in")))).isAccepted());
    CHECK(graph.connect({StableId("e3"), GraphPinId("sequence.out"), GraphPinId("action.in")},
                        domain.canConnect(*graph.findPin(GraphPinId("sequence.out")),
                                          *graph.findPin(GraphPinId("action.in")))).isAccepted());
    auto compiled = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(compiled.root.value(), std::string("root"));
    CHECK_EQ(compiled.instructions.size(), 4U);
    CHECK_EQ(compiled.instructions[1].children.size(), 2U);
}

TEST_CASE("editor.behavior.graph_rejects_cycles_or_unreachable_nodes") {
    BehaviorGraphDomain domain;
    GraphDocumentData graph;
    graph.domain = "behavior";
    graph.revision = 7;
    graph.nodes = {node("root", "root", false, true), node("orphan", "action", true, false)};
    auto compiled = domain.compile(graph);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Failed));
    CHECK(!compiled.diagnostics.empty());
    auto wrongDirection = domain.canConnect({GraphPinId("a"), GraphNodeId("a"), "flow", GraphPinDirection::Input},
                                            {GraphPinId("b"), GraphNodeId("b"), "flow", GraphPinDirection::Output});
    CHECK(!wrongDirection.allowed);
}
