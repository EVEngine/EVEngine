#include "editor/EditorPcgGraph.h"
#include "procgen/PointGraph.h"

#include <zeroerr.hpp>

using namespace eve::editor;

TEST_CASE("editor.pcgGraph.reflectsNodesAndCompilesPointGraphAsset") {
    PcgPointGraphDomain domain;
    auto input = domain.makeNode(GraphNodeId("input"), "input");
    auto prune = domain.makeNode(GraphNodeId("prune"), "self.prune");
    REQUIRE(input.accepted());
    REQUIRE(prune.accepted());
    REQUIRE(input.value);
    REQUIRE(prune.value);
    CHECK_EQ(input.value->pins.size(), size_t(1));
    CHECK_EQ(prune.value->pins.size(), size_t(2));
    const auto* properties = prune.value->properties.getIf<EditorValue::Object>();
    REQUIRE(bool(properties));
    CHECK_EQ(*properties->at("radius").getIf<double>(), 1.0);

    GraphDocument graph;
    CHECK(graph.createNode(*input.value).accepted());
    CHECK(graph.createNode(*prune.value).accepted());
    EditorValue::Object radiusBinding;
    radiusBinding["node"] = EditorValue("prune");
    radiusBinding["key"]  = EditorValue("radius");
    EditorValue::Object parameters;
    parameters["pruneRadius"] = EditorValue(std::move(radiusBinding));
    CHECK(graph.setParameters(EditorValue(std::move(parameters))).accepted());
    const GraphPinRecord* from = graph.findPin(GraphPinId("input.out"));
    const GraphPinRecord* to   = graph.findPin(GraphPinId("prune.in0"));
    REQUIRE(bool(from));
    REQUIRE(bool(to));
    CHECK(graph.connect({StableId("edge"), from->id, to->id}, domain.canConnect(*from, *to))
              .accepted());

    const PcgGraphCompileResult compiled = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Applied));
    CHECK(compiled.definition.find("EVPCG_POINT_GRAPH 1") == 0);

    eve::procgen::PointGraph runtime;
    CHECK(runtime.deserializeDefinition(compiled.definition));
    CHECK_EQ(runtime.getParameterCount(), 1);
    CHECK_EQ(runtime.getParameterName(0), std::string("pruneRadius"));
    CHECK_EQ(runtime.getParameterKind("pruneRadius"), std::string("float"));
    eve::procgen::PointSet points;
    points.add(0.f, 0.f, 0.f);
    points.add(0.5f, 0.f, 0.f);
    CHECK(runtime.setNodePoints("input", &points));
    eve::procgen::PointSet* result = runtime.execute("prune");
    REQUIRE(bool(result));
    CHECK_EQ(result->getCount(), 1);
    delete result;
}

TEST_CASE("editor.pcgGraph.rejectsInvalidBlackboardParameterBindings") {
    PcgPointGraphDomain domain;
    auto node = domain.makeNode(GraphNodeId("move"), "transform");
    REQUIRE(node.value);
    GraphDocument graph;
    CHECK(graph.createNode(*node.value).accepted());
    const Revision before = graph.revision();
    CHECK(!graph.setParameters(EditorValue("not-an-object")).accepted());
    CHECK_EQ(graph.revision(), before);

    EditorValue::Object binding;
    binding["node"] = EditorValue("move");
    binding["key"]  = EditorValue("missing");
    EditorValue::Object parameters;
    parameters["bad"] = EditorValue(std::move(binding));
    CHECK(graph.setParameters(EditorValue(std::move(parameters))).accepted());
    const auto compiled = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!compiled.diagnostics.empty());
    CHECK_EQ(compiled.diagnostics[0].rule.value(),
             std::string("editor.pcg.invalid-parameter-binding"));
}

TEST_CASE("editor.pcgGraph.rejectsCyclesAndWrongPinTypes") {
    PcgPointGraphDomain domain;
    auto a = domain.makeNode(GraphNodeId("a"), "jitter");
    auto b = domain.makeNode(GraphNodeId("b"), "self.prune");
    REQUIRE(a.value);
    REQUIRE(b.value);
    GraphDocument graph;
    CHECK(graph.createNode(*a.value).accepted());
    CHECK(graph.createNode(*b.value).accepted());
    const auto* aOut = graph.findPin(GraphPinId("a.out"));
    const auto* aIn  = graph.findPin(GraphPinId("a.in0"));
    const auto* bOut = graph.findPin(GraphPinId("b.out"));
    const auto* bIn  = graph.findPin(GraphPinId("b.in0"));
    REQUIRE(bool(aOut));
    REQUIRE(bool(aIn));
    REQUIRE(bool(bOut));
    REQUIRE(bool(bIn));
    CHECK(graph.connect({StableId("ab"), aOut->id, bIn->id}, domain.canConnect(*aOut, *bIn))
              .accepted());
    CHECK(graph.connect({StableId("ba"), bOut->id, aIn->id}, domain.canConnect(*bOut, *aIn))
              .accepted());
    const auto compiled = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Failed));
    CHECK(!compiled.diagnostics.empty());

    GraphPinRecord wrong{GraphPinId("wrong"), GraphNodeId("x"), "float",
                         GraphPinDirection::Output};
    CHECK(!domain.canConnect(wrong, *aIn).allowed);
    CHECK(!domain.makeNode(GraphNodeId("bad"), "missing").accepted());
}

TEST_CASE("editor.pcgGraph.strictlyValidatesSchemaAndNodeProperties") {
    PcgPointGraphDomain domain;
    auto node = domain.makeNode(GraphNodeId("move"), "transform");
    REQUIRE(node.value);

    GraphDocumentData unsupported;
    unsupported.domain = domain.domain();
    unsupported.schemaVersion = 99;
    auto result = domain.compile(unsupported);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(!result.diagnostics.empty());
    CHECK_EQ(result.diagnostics[0].rule.value(), std::string("editor.pcg.unsupported-schema"));

    GraphDocument graph;
    CHECK(graph.createNode(*node.value).accepted());
    EditorValue::Object properties = *node.value->properties.getIf<EditorValue::Object>();
    properties["x"] = EditorValue("wrong-type");
    CHECK(graph.setNodeProperties(node.value->id, EditorValue(properties)).accepted());
    result = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!result.diagnostics.empty());
    CHECK_EQ(result.diagnostics[0].rule.value(),
             std::string("editor.pcg.property-type-mismatch"));

    properties["x"] = EditorValue(0.0);
    properties["typo"] = EditorValue(1.0);
    CHECK(graph.setNodeProperties(node.value->id, EditorValue(properties)).accepted());
    result = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!result.diagnostics.empty());
    CHECK_EQ(result.diagnostics[0].rule.value(), std::string("editor.pcg.unknown-property"));
}
