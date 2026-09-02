#include "procgen_editing/PcgGraph.h"
#include "procgen/PointGraph.h"

#include "zeroerr/unittest.h"

#include <algorithm>

using namespace eve::procgen_editing;
using namespace eve::editing;

TEST_CASE("editor.pcgGraph.reflectsNodesAndCompilesPointGraphAsset") {
    PcgPointGraphDomain domain;
    auto input = domain.makeNode(GraphNodeId("input"), "input");
    auto prune = domain.makeNode(GraphNodeId("prune"), "self.prune");
    REQUIRE(input.ok());
    REQUIRE(prune.ok());
    CHECK_EQ(input.value().pins.size(), size_t(1));
    CHECK_EQ(prune.value().pins.size(), size_t(2));
    const auto* properties = prune.value().properties.getIf<EditorValue::Object>();
    REQUIRE(bool(properties));
    CHECK_EQ(*properties->at("radius").getIf<double>(), 1.0);

    GraphDocument graph;
    CHECK(graph.createNode(input.value()).ok());
    CHECK(graph.createNode(prune.value()).ok());
    EditorValue::Object radiusBinding;
    radiusBinding["node"] = EditorValue("prune");
    radiusBinding["key"]  = EditorValue("radius");
    EditorValue::Object parameters;
    parameters["pruneRadius"] = EditorValue(std::move(radiusBinding));
    CHECK(graph.setParameters(EditorValue(std::move(parameters))).ok());
    const GraphPinRecord* from = graph.findPin(GraphPinId("input.out"));
    const GraphPinRecord* to   = graph.findPin(GraphPinId("prune.in0"));
    REQUIRE(bool(from));
    REQUIRE(bool(to));
    CHECK(graph.connect({StableId("edge"), from->id, to->id}, domain.canConnect(*from, *to))
              .ok());

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

    const auto preview = domain.preview(graph.snapshot(domain.domain()), "input", &points,
                                        "prune");
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(preview.outputCount, 1);
    CHECK_EQ(preview.metrics.size(), size_t(2));
    const auto inputMetric = std::find_if(
        preview.metrics.begin(), preview.metrics.end(),
        [](const PcgGraphPreviewMetric& metric) { return metric.nodeId == "input"; });
    REQUIRE(inputMetric != preview.metrics.end());
    CHECK_EQ(inputMetric->outputCount, 2);
    CHECK_EQ(inputMetric->minX, 0.f);
    CHECK_EQ(inputMetric->maxX, 0.5f);
    CHECK_EQ(inputMetric->averageDensity, 1.f);
}

TEST_CASE("editor.pcgGraph.rejectsInvalidBlackboardParameterBindings") {
    PcgPointGraphDomain domain;
    auto node = domain.makeNode(GraphNodeId("move"), "transform");
    REQUIRE(node.ok());
    GraphDocument graph;
    CHECK(graph.createNode(node.value()).ok());
    const Revision before = graph.revision();
    CHECK(!graph.setParameters(EditorValue("not-an-object")).ok());
    CHECK_EQ(graph.revision(), before);

    EditorValue::Object binding;
    binding["node"] = EditorValue("move");
    binding["key"]  = EditorValue("missing");
    EditorValue::Object parameters;
    parameters["bad"] = EditorValue(std::move(binding));
    CHECK(graph.setParameters(EditorValue(std::move(parameters))).ok());
    const auto compiled = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!compiled.diagnostics.empty());
    CHECK_EQ(diagnosticRule(compiled.diagnostics[0]).value(),
             std::string("editor.pcg.invalid-parameter-binding"));
}

TEST_CASE("editor.pcgGraph.rejectsCyclesAndWrongPinTypes") {
    PcgPointGraphDomain domain;
    auto a = domain.makeNode(GraphNodeId("a"), "jitter");
    auto b = domain.makeNode(GraphNodeId("b"), "self.prune");
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    GraphDocument graph;
    CHECK(graph.createNode(a.value()).ok());
    CHECK(graph.createNode(b.value()).ok());
    const auto* aOut = graph.findPin(GraphPinId("a.out"));
    const auto* aIn  = graph.findPin(GraphPinId("a.in0"));
    const auto* bOut = graph.findPin(GraphPinId("b.out"));
    const auto* bIn  = graph.findPin(GraphPinId("b.in0"));
    REQUIRE(bool(aOut));
    REQUIRE(bool(aIn));
    REQUIRE(bool(bOut));
    REQUIRE(bool(bIn));
    CHECK(graph.connect({StableId("ab"), aOut->id, bIn->id}, domain.canConnect(*aOut, *bIn))
              .ok());
    CHECK(graph.connect({StableId("ba"), bOut->id, aIn->id}, domain.canConnect(*bOut, *aIn))
              .ok());
    const auto compiled = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Failed));
    CHECK(!compiled.diagnostics.empty());

    GraphPinRecord wrong{GraphPinId("wrong"), GraphNodeId("x"), "float",
                         GraphPinDirection::Output};
    CHECK(!domain.canConnect(wrong, *aIn).allowed);
    CHECK(!domain.makeNode(GraphNodeId("bad"), "missing").ok());
}

TEST_CASE("editor.pcgGraph.strictlyValidatesSchemaAndNodeProperties") {
    PcgPointGraphDomain domain;
    auto node = domain.makeNode(GraphNodeId("move"), "transform");
    REQUIRE(node.ok());

    GraphDocumentData unsupported;
    unsupported.domain = domain.domain();
    unsupported.schemaVersion = 99;
    auto result = domain.compile(unsupported);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Unsupported));
    REQUIRE(!result.diagnostics.empty());
    CHECK_EQ(diagnosticRule(result.diagnostics[0]).value(), std::string("editor.pcg.unsupported-schema"));

    GraphDocument graph;
    CHECK(graph.createNode(node.value()).ok());
    EditorValue::Object properties = *node.value().properties.getIf<EditorValue::Object>();
    properties["x"] = EditorValue("wrong-type");
    CHECK(graph.setNodeProperties(node.value().id, EditorValue(properties)).ok());
    result = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!result.diagnostics.empty());
    CHECK_EQ(diagnosticRule(result.diagnostics[0]).value(),
             std::string("editor.pcg.property-type-mismatch"));

    properties["x"] = EditorValue(0.0);
    properties["typo"] = EditorValue(1.0);
    CHECK(graph.setNodeProperties(node.value().id, EditorValue(properties)).ok());
    result = domain.compile(graph.snapshot(domain.domain()));
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!result.diagnostics.empty());
    CHECK_EQ(diagnosticRule(result.diagnostics[0]).value(), std::string("editor.pcg.unknown-property"));
}

TEST_CASE("editor.pcgGraph.transactionallyMigratesLegacyPinsAndDefaults") {
    PcgPointGraphDomain domain;
    auto input = domain.makeNode(GraphNodeId("source"), "input");
    auto prune = domain.makeNode(GraphNodeId("prune"), "self.prune");
    REQUIRE(input.ok());
    REQUIRE(prune.ok());
    input.value().pins[0].id = GraphPinId("legacy-source-output");
    prune.value().pins[0].id = GraphPinId("legacy-prune-input");
    prune.value().pins[1].id = GraphPinId("legacy-prune-output");
    prune.value().properties = EditorValue(EditorValue::Object{});

    GraphDocumentData legacy;
    legacy.domain        = domain.domain();
    legacy.schemaVersion = 0;
    legacy.revision      = 17;
    legacy.nodes         = {input.value(), prune.value()};
    legacy.edges.push_back({StableId("stable-edge"), GraphPinId("legacy-source-output"),
                            GraphPinId("legacy-prune-input")});

    const auto migrated = domain.migrate(legacy);
    CHECK_EQ(static_cast<int>(migrated.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(migrated.fromVersion, uint32_t(0));
    CHECK_EQ(migrated.graph.schemaVersion, uint32_t(1));
    CHECK_EQ(migrated.graph.revision, Revision(17));
    REQUIRE_EQ(migrated.graph.nodes.size(), size_t(2));
    const auto* properties = migrated.graph.nodes[1].properties.getIf<EditorValue::Object>();
    REQUIRE(bool(properties));
    CHECK_EQ(*properties->at("radius").getIf<double>(), 1.0);
    REQUIRE_EQ(migrated.graph.edges.size(), size_t(1));
    CHECK_EQ(migrated.graph.edges[0].id.value(), std::string("stable-edge"));
    CHECK_EQ(migrated.graph.edges[0].from.value(), std::string("source.out"));
    CHECK_EQ(migrated.graph.edges[0].to.value(), std::string("prune.in0"));

    const auto compiled = domain.compile(legacy);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Applied));
    CHECK(compiled.definition.find("EVPCG_POINT_GRAPH 1") == 0);
    REQUIRE(!compiled.diagnostics.empty());
    CHECK_EQ(diagnosticRule(compiled.diagnostics[0]).value(), std::string("editor.pcg.migrated-v0-v1"));

    GraphDocumentData invalid = legacy;
    invalid.nodes[1].pins[0].id = GraphPinId("legacy-source-output");
    const auto rejected = domain.migrate(invalid);
    CHECK_EQ(static_cast<int>(rejected.status), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(!rejected.diagnostics.empty());
    CHECK_EQ(diagnosticRule(rejected.diagnostics[0]).value(),
             std::string("editor.pcg.migration-duplicate-pin"));
    CHECK_EQ(legacy.schemaVersion, uint32_t(0));
}

TEST_CASE("editor.pcgGraph.boundsIsolatedPreviewPointOutputs") {
    PcgPointGraphDomain domain;
    auto input = domain.makeNode(GraphNodeId("input"), "input");
    REQUIRE(input.ok());
    GraphDocument graph;
    CHECK(graph.createNode(input.value()).ok());
    eve::procgen::PointSet points;
    for (int index = 0; index < 1001; ++index) points.add(float(index), 0.f, 0.f);

    const auto rejected = domain.preview(graph.snapshot(domain.domain()), "input", &points,
                                         "input", 0, 1000);
    CHECK_EQ(static_cast<int>(rejected.status), static_cast<int>(EditorStatus::Failed));
    REQUIRE(!rejected.diagnostics.empty());
    CHECK(rejected.diagnostics.back().message().find("point budget") != std::string::npos);

    const auto accepted = domain.preview(graph.snapshot(domain.domain()), "input", &points,
                                         "input", 0, 1001);
    CHECK_EQ(static_cast<int>(accepted.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(accepted.outputCount, 1001);
}
