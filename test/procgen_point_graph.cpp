#include "procgen/Biome.h"
#include "procgen/MeshBuild.h"
#include "procgen/PointGraph.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/ShapeGrammar.h"

#include <cmath>
#include <memory>
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

TEST_CASE("procgen.pointGraph.executesDagAndCachesResults") {
    PointGraph graph;
    CHECK(graph.addNode("sample", "spatial.sample"));
    CHECK(graph.addNode("exclude", "spatial.filter"));
    CHECK(graph.addNode("prune", "self.prune"));
    CHECK(graph.connect("sample", "exclude"));
    CHECK(graph.connect("exclude", "prune"));

    SpatialData domain = SpatialData::box(0.f, 0.f, 0.f, 4.f, 0.f, 4.f);
    SpatialData hole   = SpatialData::sphere(2.f, 0.f, 2.f, 0.75f);
    CHECK(graph.setNodeSpatial("sample", &domain));
    CHECK(graph.setNodeFloat("sample", "spacing", 2.f));
    CHECK(graph.setNodeInt("sample", "seed", 42));
    CHECK(graph.setNodeSpatial("exclude", &hole));
    CHECK(graph.setNodeInt("exclude", "invert", 1));
    CHECK(graph.setNodeFloat("prune", "radius", 1.f));
    CHECK(graph.validate());

    PointSet* first = graph.execute("prune");
    REQUIRE(bool(first));
    CHECK_EQ(first->getCount(), 8);
    CHECK(graph.getMetricCount() >= 3);
    PointSet* debug = graph.getNodeOutput("sample");
    REQUIRE(bool(debug));
    CHECK_EQ(debug->getCount(), 9);

    PointSet* second = graph.execute("prune");
    REQUIRE(bool(second));
    CHECK_EQ(second->getCount(), 8);
    CHECK_EQ(graph.getCacheHitCount(), 1);
    CHECK(graph.isMetricCacheHit(0));

    delete second;
    delete debug;
    delete first;
}

TEST_CASE("procgen.pointGraph.rejectsCyclesAndBadInputs") {
    PointGraph graph;
    CHECK(graph.addNode("a", "self.prune"));
    CHECK(graph.addNode("b", "jitter"));
    CHECK(graph.connect("a", "b"));
    CHECK(graph.connect("b", "a"));
    CHECK(!graph.validate());
    CHECK(graph.getError().find("cycle") != std::string::npos);
    CHECK(!graph.execute("a"));
    CHECK(graph.getError().find("cycle") != std::string::npos);
    CHECK(!graph.addNode("unknown", "not-an-operation"));
}

TEST_CASE("procgen.pointGraph.filtersProjectedTerrainSlope") {
    PointSet source;
    source.add(0.f, 0.f, 0.f);
    source.add(1.f, 0.f, 0.f);
    source.setNormal(0, 0.f, 1.f, 0.f);
    source.setNormal(1, 1.f, 0.f, 0.f);
    PointGraph graph;
    CHECK(graph.addNode("source", "input"));
    CHECK(graph.addNode("slope", "filter.slope"));
    CHECK(graph.setNodePoints("source", &source));
    CHECK(graph.connect("source", "slope"));
    CHECK(graph.setNodeFloat("slope", "minDegrees", 0.f));
    CHECK(graph.setNodeFloat("slope", "maxDegrees", 45.f));
    CHECK(graph.validate());
    std::unique_ptr<PointSet> result(graph.execute("slope"));
    REQUIRE(bool(result));
    CHECK_EQ(result->getCount(), 1);
    CHECK_EQ(result->getNormalY(0), 1.f);
}

TEST_CASE("procgen.pointGraph.supportsBranchesAndNestedGraphs") {
    PointSet source;
    source.add(1.f, 0.f, 0.f);

    PointGraph nested;
    CHECK(nested.addNode("in", "input"));
    CHECK(nested.addNode("move", "transform"));
    CHECK(nested.setNodePoints("in", &source));
    CHECK(nested.connect("in", "move"));
    CHECK(nested.setNodeFloat("move", "x", 5.f));

    PointGraph graph;
    CHECK(graph.addNode("source", "input"));
    CHECK(graph.addNode("nested", "subgraph"));
    CHECK(graph.addNode("fallback", "transform"));
    CHECK(graph.addNode("choose", "branch"));
    CHECK(graph.setNodePoints("source", &source));
    CHECK(graph.connect("source", "nested"));
    CHECK(graph.setNodeSubgraph("nested", &nested, "in", "move"));
    CHECK(graph.connect("source", "fallback"));
    CHECK(graph.setNodeFloat("fallback", "x", -5.f));
    CHECK(graph.connect("nested", "choose", 0));
    CHECK(graph.connect("fallback", "choose", 1));
    CHECK(graph.setNodeInt("choose", "condition", 1));

    PointSet* result = graph.execute("choose");
    REQUIRE(bool(result));
    CHECK_EQ(result->getCount(), 1);
    CHECK_EQ(result->getX(0), 6.f);
    delete result;
}

TEST_CASE("procgen.pointGraph.reflectsOperationsForEditors") {
    CHECK(PointGraph::getOperationCount() >= 15);
    CHECK_EQ(PointGraph::getOperationInputCount("merge"), 2);
    CHECK_EQ(PointGraph::getOperationInputCount("spatial.sample"), 0);
    CHECK_EQ(PointGraph::getOperationParamCount("transform"), 7);
    CHECK_EQ(PointGraph::getOperationParamKey("spatial.sample", 0), std::string("spacing"));
    CHECK_EQ(PointGraph::getOperationParamKind("spatial.sample", 1), std::string("int"));
    CHECK_EQ(PointGraph::getOperationParamDefault("branch", 0), std::string("false"));
    CHECK_EQ(PointGraph::getOperationInputCount("filter.slope"), 1);
    CHECK_EQ(PointGraph::getOperationInputCount("missing"), -1);
}

TEST_CASE("procgen.pointGraph.enforcesDeclaredPinsAndValidatesExternalSubgraphInput") {
    PointGraph nested;
    CHECK(nested.addNode("in", "input"));
    CHECK(nested.addNode("move", "transform"));
    CHECK(nested.connect("in", "move"));

    PointGraph graph;
    CHECK(graph.addNode("source", "input"));
    CHECK(graph.addNode("nested", "subgraph"));
    CHECK(graph.connect("source", "nested"));
    CHECK(!graph.connect("source", "nested", 1));
    CHECK(!graph.connect("source", "source", 0));
    CHECK(graph.setNodeSubgraph("nested", &nested, "in", "move"));

    PointSet input;
    input.add(1.f, 2.f, 3.f);
    CHECK(graph.setNodePoints("source", &input));
    CHECK(graph.validate());
    PointSet* output = graph.execute("nested");
    REQUIRE(bool(output));
    CHECK_EQ(output->getCount(), 1);
    delete output;
}

TEST_CASE("procgen.pointGraph.definitionRoundTripsNestedTopologyAndParams") {
    PointSet source;
    source.add(1.f, 0.f, 0.f);

    PointGraph nested;
    nested.addNode("in", "input");
    nested.addNode("move", "transform");
    nested.setNodePoints("in", &source);
    nested.connect("in", "move");
    nested.setNodeFloat("move", "x", 5.f);

    PointGraph original;
    original.addNode("source", "input");
    original.addNode("nested", "subgraph");
    original.addNode("tag", "attribute.set.string");
    original.setNodePoints("source", &source);
    original.connect("source", "nested");
    original.setNodeSubgraph("nested", &nested, "in", "move");
    original.connect("nested", "tag");
    original.setNodeString("tag", "attribute", "asset");
    original.setNodeString("tag", "value", "oak \"old\"");

    const std::string definition = original.serializeDefinition();
    CHECK(definition.find("EVPCG_POINT_GRAPH 1") == 0);
    PointGraph loaded;
    CHECK(loaded.deserializeDefinition(definition));
    CHECK_EQ(loaded.getNodeCount(), 3);
    CHECK_EQ(loaded.getInputNode("tag", 0), std::string("nested"));
    CHECK(loaded.setNodePoints("source", &source));
    PointSet* result = loaded.execute("tag");
    REQUIRE(bool(result));
    CHECK_EQ(result->getX(0), 6.f);
    CHECK_EQ(result->getStringAttribute(0, "asset", ""), std::string("oak \"old\""));
    delete result;

    CHECK(!loaded.deserializeDefinition("broken"));
    CHECK(loaded.hasNode("source"));
    CHECK(!loaded.deserializeDefinition(
        "EVPCG_POINT_GRAPH 1\nNODE \"move\" \"transform\"\nS \"move\" \"x\" \"bad\"\nEND\n"));
    CHECK(loaded.hasNode("source"));
}

TEST_CASE("procgen.pointGraph.invalidatesOnlyChangedNodeDescendants") {
    PointSet points;
    points.add(1.f, 0.f, 0.f);
    PointGraph graph;
    CHECK(graph.addNode("source", "input"));
    CHECK(graph.addNode("left", "transform"));
    CHECK(graph.addNode("right", "transform"));
    CHECK(graph.connect("source", "left"));
    CHECK(graph.connect("source", "right"));
    CHECK(graph.setNodePoints("source", &points));
    PointSet* left = graph.execute("left");
    PointSet* right = graph.execute("right");
    REQUIRE(bool(left));
    REQUIRE(bool(right));
    delete right;
    delete left;

    const uint64_t revision = graph.getRevision();
    CHECK(graph.setNodeFloat("left", "x", 5.f));
    CHECK(graph.getRevision() > revision);
    PointSet* sourceCache = graph.getNodeOutput("source");
    PointSet* rightCache  = graph.getNodeOutput("right");
    CHECK(bool(sourceCache));
    CHECK(bool(rightCache));
    CHECK(!graph.getNodeOutput("left"));
    delete rightCache;
    delete sourceCache;
}

TEST_CASE("procgen.pointGraph.definitionIsStableAcrossParameterInsertionOrder") {
    PointGraph first;
    PointGraph second;
    CHECK(first.addNode("move", "transform"));
    CHECK(second.addNode("move", "transform"));
    CHECK(first.addNode("tag", "attribute.set.string"));
    CHECK(second.addNode("tag", "attribute.set.string"));
    CHECK(first.setNodeFloat("move", "z", 3.f));
    CHECK(first.setNodeFloat("move", "x", 1.f));
    CHECK(first.setNodeString("tag", "value", "oak"));
    CHECK(first.setNodeString("tag", "attribute", "asset"));
    CHECK(second.setNodeString("tag", "attribute", "asset"));
    CHECK(second.setNodeString("tag", "value", "oak"));
    CHECK(second.setNodeFloat("move", "x", 1.f));
    CHECK(second.setNodeFloat("move", "z", 3.f));
    CHECK_EQ(first.serializeDefinition(), second.serializeDefinition());
    CHECK(!first.setNodeString("move", "typo", "rejected"));
    CHECK(!first.setNodeInt("move", "x", 1));
    CHECK(!first.setNodeFloat("tag", "value", 1.f));
}

TEST_CASE("procgen.pointGraph.exposesTypedInstanceParameters") {
    PointSet points;
    points.add(1.f, 0.f, 0.f);
    PointGraph graph;
    CHECK(graph.addNode("source", "input"));
    CHECK(graph.addNode("move", "transform"));
    CHECK(graph.connect("source", "move"));
    CHECK(graph.setNodePoints("source", &points));
    CHECK(graph.setNodeFloat("move", "x", 2.f));
    CHECK(graph.exposeParameter("offset", "move", "x"));
    CHECK_EQ(graph.getParameterCount(), 1);
    CHECK_EQ(graph.getParameterName(0), std::string("offset"));
    CHECK_EQ(graph.getParameterKind("offset"), std::string("float"));
    CHECK_EQ(graph.getParameterFloat("offset", -1.f), 2.f);
    CHECK(!graph.exposeParameter("alias", "move", "x"));
    CHECK(!graph.exposeParameter("bad", "move", "missing"));
    CHECK(!graph.setParameterInt("offset", 7));

    PointSet* result = graph.execute("move");
    REQUIRE(bool(result));
    CHECK_EQ(result->getX(0), 3.f);
    delete result;

    CHECK(graph.setParameterFloat("offset", 5.f));
    CHECK(graph.hasParameterOverride("offset"));
    CHECK_EQ(graph.getParameterFloat("offset", -1.f), 5.f);
    result = graph.execute("move");
    REQUIRE(bool(result));
    CHECK_EQ(result->getX(0), 6.f);
    delete result;

    const std::string definition = graph.serializeDefinition();
    PointGraph loaded;
    CHECK(loaded.deserializeDefinition(definition));
    CHECK_EQ(loaded.getParameterKind("offset"), std::string("float"));
    CHECK(!loaded.hasParameterOverride("offset"));
    CHECK_EQ(loaded.getParameterFloat("offset", -1.f), 2.f);
    CHECK(loaded.setNodePoints("source", &points));
    result = loaded.execute("move");
    REQUIRE(bool(result));
    CHECK_EQ(result->getX(0), 3.f);
    delete result;

    CHECK(graph.clearParameterOverride("offset"));
    CHECK(!graph.hasParameterOverride("offset"));
    CHECK_EQ(graph.getParameterFloat("offset", -1.f), 2.f);
    CHECK(graph.removeNode("move"));
    CHECK_EQ(graph.getParameterCount(), 0);
}

TEST_CASE("procgen.pointGraph.serializesExposedParametersDeterministically") {
    PointGraph first;
    PointGraph second;
    for (PointGraph* graph : {&first, &second}) {
        CHECK(graph->addNode("move", "transform"));
        CHECK(graph->addNode("tag", "attribute.set.string"));
    }
    CHECK(first.exposeParameter("zOffset", "move", "z"));
    CHECK(first.exposeParameter("asset", "tag", "value"));
    CHECK(second.exposeParameter("asset", "tag", "value"));
    CHECK(second.exposeParameter("zOffset", "move", "z"));
    CHECK_EQ(first.serializeDefinition(), second.serializeDefinition());
}

TEST_CASE("procgen.pointGraph.cooperativelyBudgetsAndCancelsExecution") {
    PointSet points;
    points.add(0.f, 0.f, 0.f);
    PointGraph graph;
    CHECK(graph.addNode("source", "input"));
    CHECK(graph.addNode("first", "transform"));
    CHECK(graph.addNode("second", "transform"));
    CHECK(graph.connect("source", "first"));
    CHECK(graph.connect("first", "second"));
    CHECK(graph.setNodePoints("source", &points));
    graph.setExecutionNodeBudget(2);
    CHECK_EQ(graph.getExecutionNodeBudget(), 2);
    CHECK(!graph.execute("second"));
    CHECK(graph.wasCancelled());
    CHECK(graph.getError().find("budget exceeded") != std::string::npos);
    PointSet* partial = graph.getNodeOutput("first");
    REQUIRE(bool(partial));
    delete partial;

    graph.setExecutionNodeBudget(3);
    PointSet* completed = graph.execute("second");
    REQUIRE(bool(completed));
    CHECK(!graph.wasCancelled());
    CHECK(graph.getCacheHitCount() > 0);
    delete completed;

    graph.requestCancel();
    CHECK(!graph.execute("second"));
    CHECK(graph.wasCancelled());
    CHECK(graph.getError().find("cancelled") != std::string::npos);
    graph.resetCancellation();
    CHECK(!graph.wasCancelled());
    completed = graph.execute("second");
    REQUIRE(bool(completed));
    delete completed;
}

TEST_CASE("procgen.pointGraph.copiesSourcePointsAcrossTargetsWithBoundedOutput") {
    PointSet source;
    const int sourcePoint = source.add(1.f, 2.f, 0.f);
    source.setPointSeed(sourcePoint, 11);
    source.setFloatAttribute(sourcePoint, "source", 2.f);
    source.setStringAttribute(sourcePoint, "shared", "source");
    PointSet targets;
    const int firstTarget = targets.add(10.f, 20.f, 30.f);
    targets.setYaw(firstTarget, 90.f);
    targets.setScale(firstTarget, 2.f, 3.f, 4.f);
    targets.setDensity(firstTarget, 0.5f);
    targets.setPointSeed(firstTarget, 101);
    targets.setFloatAttribute(firstTarget, "target", 3.f);
    targets.setStringAttribute(firstTarget, "shared", "target");
    targets.add(-10.f, 0.f, 0.f);

    PointGraph graph;
    CHECK(graph.addNode("source", "input"));
    CHECK(graph.addNode("targets", "input"));
    CHECK(graph.addNode("copies", "copy.points"));
    CHECK(graph.setNodePoints("source", &source));
    CHECK(graph.setNodePoints("targets", &targets));
    CHECK(graph.connect("source", "copies", 0));
    CHECK(graph.connect("targets", "copies", 1));
    CHECK(graph.setNodeInt("copies", "maxPoints", 2));
    PointSet* result = graph.execute("copies");
    REQUIRE(bool(result));
    CHECK_EQ(result->getCount(), 2);
    CHECK(std::abs(result->getX(0) - 10.f) < 0.0001f);
    CHECK_EQ(result->getY(0), 26.f);
    CHECK(std::abs(result->getZ(0) - 32.f) < 0.0001f);
    CHECK_EQ(result->getYaw(0), 90.f);
    CHECK_EQ(result->getScaleX(0), 2.f);
    CHECK_EQ(result->getDensity(0), 0.5f);
    CHECK_EQ(result->getFloatAttribute(0, "target", -1.f), 3.f);
    CHECK_EQ(result->getFloatAttribute(0, "source", -1.f), 2.f);
    CHECK_EQ(result->getStringAttribute(0, "shared", ""), std::string("source"));
    delete result;

    CHECK(graph.setNodeInt("copies", "maxPoints", 1));
    CHECK(!graph.execute("copies"));
    CHECK(graph.getError().find("maxPoints") != std::string::npos);
}

TEST_CASE("procgen.pointGraph.validatesEveryRequiredInput") {
    PointSet   points;
    PointGraph graph;
    CHECK(graph.addNode("source", "input"));
    CHECK(graph.addNode("copies", "copy.points"));
    CHECK(graph.setNodePoints("source", &points));
    CHECK(graph.connect("source", "copies", 0));
    CHECK(!graph.validate());
    CHECK(graph.getError().find("input 1") != std::string::npos);
}

TEST_CASE("procgen.pointGraph.remapsDensityAndComputesFloatMetadata") {
    PointSet points;
    const int first = points.add(0.f, 0.f, 0.f);
    const int second = points.add(1.f, 0.f, 0.f);
    points.setDensity(first, -1.f);
    points.setDensity(second, 0.5f);
    points.setFloatAttribute(first, "age", 3.f);

    PointGraph graph;
    CHECK(graph.addNode("source", "input"));
    CHECK(graph.addNode("density", "density.remap"));
    CHECK(graph.addNode("math", "attribute.math.float"));
    CHECK(graph.setNodePoints("source", &points));
    CHECK(graph.connect("source", "density"));
    CHECK(graph.connect("density", "math"));
    CHECK(graph.setNodeFloat("density", "inputMin", 0.f));
    CHECK(graph.setNodeFloat("density", "inputMax", 1.f));
    CHECK(graph.setNodeFloat("density", "outputMin", 10.f));
    CHECK(graph.setNodeFloat("density", "outputMax", 20.f));
    CHECK(graph.setNodeString("math", "attribute", "age"));
    CHECK(graph.setNodeString("math", "outputAttribute", "score"));
    CHECK(graph.setNodeString("math", "operation", "multiply"));
    CHECK(graph.setNodeFloat("math", "operand", 2.f));
    CHECK(graph.setNodeFloat("math", "defaultValue", 4.f));
    PointSet* result = graph.execute("math");
    REQUIRE(bool(result));
    CHECK_EQ(result->getDensity(0), 10.f);
    CHECK_EQ(result->getDensity(1), 15.f);
    CHECK_EQ(result->getFloatAttribute(0, "score", -1.f), 6.f);
    CHECK_EQ(result->getFloatAttribute(1, "score", -1.f), 8.f);
    delete result;

    CHECK(graph.setNodeString("math", "operation", "divide"));
    CHECK(graph.setNodeFloat("math", "operand", 0.f));
    CHECK(!graph.execute("math"));
    CHECK(graph.getError().find("divide by zero") != std::string::npos);
    CHECK(graph.setNodeFloat("density", "inputMax", 0.f));
    CHECK(!graph.execute("density"));
    CHECK(graph.getError().find("non-zero input range") != std::string::npos);
}

TEST_CASE("procgen.pointGraph.cachesSpatialMetricsForDebugVisualization") {
    PointSet points;
    const int first = points.add(-2.f, 3.f, 4.f);
    const int second = points.add(6.f, -1.f, 8.f);
    points.setDensity(first, 0.25f);
    points.setDensity(second, 0.75f);
    PointGraph graph;
    CHECK(graph.addNode("source", "input"));
    CHECK(graph.setNodePoints("source", &points));
    PointSet* output = graph.execute("source");
    REQUIRE(bool(output));
    delete output;
    CHECK_EQ(graph.getMetricCount(), 1);
    CHECK_EQ(graph.getMetricMinX(0), -2.f);
    CHECK_EQ(graph.getMetricMinY(0), -1.f);
    CHECK_EQ(graph.getMetricMinZ(0), 4.f);
    CHECK_EQ(graph.getMetricMaxX(0), 6.f);
    CHECK_EQ(graph.getMetricMaxY(0), 3.f);
    CHECK_EQ(graph.getMetricMaxZ(0), 8.f);
    CHECK_EQ(graph.getMetricAverageDensity(0), 0.5f);

    output = graph.execute("source");
    REQUIRE(bool(output));
    delete output;
    CHECK(graph.isMetricCacheHit(0));
    CHECK_EQ(graph.getMetricMinX(0), -2.f);
    CHECK_EQ(graph.getMetricAverageDensity(0), 0.5f);
}

TEST_CASE("procgen.pointGraph.instantiatesIsolatedRuntimeState") {
    PointSet points;
    points.add(1.f, 0.f, 0.f);
    PointGraph asset;
    CHECK(asset.addNode("source", "input"));
    CHECK(asset.addNode("move", "transform"));
    CHECK(asset.connect("source", "move"));
    CHECK(asset.setNodePoints("source", &points));
    CHECK(asset.setNodeFloat("move", "x", 2.f));
    CHECK(asset.exposeParameter("offset", "move", "x"));
    CHECK(asset.setParameterFloat("offset", 9.f));

    PointGraph* instance = asset.instantiate();
    REQUIRE(bool(instance));
    CHECK(!instance->hasParameterOverride("offset"));
    CHECK_EQ(instance->getParameterFloat("offset", -1.f), 2.f);
    CHECK(!instance->execute("move"));
    CHECK(instance->getError().find("no points") != std::string::npos);
    CHECK(instance->setNodePoints("source", &points));
    CHECK(instance->setParameterFloat("offset", 4.f));
    PointSet* output = instance->execute("move");
    REQUIRE(bool(output));
    CHECK_EQ(output->getX(0), 5.f);
    delete output;
    CHECK_EQ(asset.getParameterFloat("offset", -1.f), 9.f);
    delete instance;
}

TEST_CASE("procgen.pointGraph.generatesBiomeThroughReflectedNode") {
    SpatialData domain = SpatialData::box(0.f, 0.f, 0.f, 4.f, 0.f, 4.f);
    BiomeRules rules;
    CHECK(rules.addLayer("forest", &domain, 1, 1.f));
    CHECK(rules.addAsset("forest", "oak", 1.f, 1.f, 1.f, false));

    PointGraph graph;
    CHECK(graph.addNode("biome", "biome.generate"));
    CHECK(graph.setNodeSpatial("biome", &domain));
    CHECK(graph.setNodeBiomeRules("biome", &rules));
    CHECK(graph.setNodeFloat("biome", "spacing", 2.f));
    CHECK(graph.setNodeInt("biome", "seed", 42));
    CHECK(graph.validate());
    PointSet* output = graph.execute("biome");
    REQUIRE(bool(output));
    CHECK_EQ(output->getCount(), 9);
    CHECK_EQ(output->getStringAttribute(0, "biome", ""), std::string("forest"));
    CHECK_EQ(output->getStringAttribute(0, "asset", ""), std::string("oak"));
    delete output;
}

TEST_CASE("procgen.pointGraph.expandsShapeGrammarAndRebindsExternalAssets") {
    PointSet controlPoints;
    controlPoints.add(0.f, 0.f, 0.f);
    controlPoints.add(6.f, 0.f, 0.f);
    ShapeGrammar grammar;
    CHECK(grammar.addModule("A", "wall", 2.f, 1.f));

    PointGraph asset;
    CHECK(asset.addNode("path", "input"));
    CHECK(asset.addNode("facade", "grammar.generate"));
    CHECK(asset.connect("path", "facade"));
    CHECK(asset.setNodePoints("path", &controlPoints));
    CHECK(asset.setNodeShapeGrammar("facade", &grammar));
    CHECK(asset.setNodeString("facade", "grammar", "A+"));
    CHECK(asset.validate());
    PointSet* output = asset.execute("facade");
    REQUIRE(bool(output));
    CHECK_EQ(output->getCount(), 3);
    CHECK_EQ(output->getStringAttribute(0, "asset", ""), std::string("wall"));
    delete output;

    PointGraph* instance = asset.instantiate();
    REQUIRE(bool(instance));
    CHECK(instance->setNodePoints("path", &controlPoints));
    CHECK(!instance->validate());
    CHECK(instance->getError().find("shape grammar") != std::string::npos);
    CHECK(instance->setNodeShapeGrammar("facade", &grammar));
    CHECK(instance->validate());
    output = instance->execute("facade");
    REQUIRE(bool(output));
    CHECK_EQ(output->getCount(), 3);
    delete output;
    delete instance;
}

TEST_CASE("procgen.pointGraph.exposesUeStyleBreadthNodes") {
    CHECK(PointGraph::getOperationInputCount("mesh.sample") == 0);
    CHECK(PointGraph::getOperationInputCount("grid.sample") == 0);
    CHECK(PointGraph::getOperationInputCount("poisson.sample") == 0);
    CHECK(PointGraph::getOperationInputCount("spline.sample") == 1);
    CHECK(PointGraph::getOperationInputCount("spline.filter.distance") == 2);
    CHECK(PointGraph::getOperationInputCount("points.union") == 2);
    CHECK(PointGraph::getOperationInputCount("points.intersect") == 2);
    CHECK(PointGraph::getOperationInputCount("points.difference") == 2);
    CHECK(PointGraph::getOperationInputCount("density.from.normal") == 1);
    CHECK(PointGraph::getOperationInputCount("bounds.modify") == 1);
    CHECK(PointGraph::getOperationInputCount("spawn.mesh") == 1);
    CHECK(PointGraph::getOperationInputCount("debug.disable") == 1);
    CHECK(PointGraph::getOperationInputCount("debug.inspect") == 1);
    CHECK_EQ(PointGraph::getOperationParamKey("spline.sample", 2), std::string("lateralJitter"));
    CHECK_EQ(PointGraph::getOperationParamKey("spawn.mesh", 1), std::string("attribute"));
    CHECK_EQ(PointGraph::getOperationParamKey("grid.sample", 0), std::string("width"));
    CHECK_EQ(PointGraph::getOperationParamKey("poisson.sample", 2), std::string("radius"));
    CHECK(PointGraph::getOperationInputCount("landscape.sample") == 1);
    CHECK(PointGraph::getOperationInputCount("texture.sample") == 1);
    CHECK_EQ(PointGraph::getOperationParamKey("landscape.sample", 1), std::string("project"));
    CHECK_EQ(PointGraph::getOperationParamKey("texture.sample", 1), std::string("attribute"));
}

TEST_CASE("procgen.pointGraph.samplesGridAndPoissonDisk") {
    PointGraph gridGraph;
    CHECK(gridGraph.addNode("grid", "grid.sample"));
    CHECK(gridGraph.setNodeInt("grid", "width", 4));
    CHECK(gridGraph.setNodeInt("grid", "depth", 3));
    CHECK(gridGraph.setNodeFloat("grid", "spacing", 2.f));
    CHECK(gridGraph.setNodeInt("grid", "seed", 11));
    CHECK(gridGraph.setNodeFloat("grid", "jitter", 0.f));
    CHECK(gridGraph.setNodeFloat("grid", "originX", 10.f));
    CHECK(gridGraph.setNodeFloat("grid", "originY", 1.f));
    CHECK(gridGraph.setNodeFloat("grid", "originZ", -5.f));
    CHECK(gridGraph.validate());
    std::unique_ptr<PointSet> grid(gridGraph.execute("grid"));
    REQUIRE(bool(grid));
    CHECK_EQ(grid->getCount(), 12);
    CHECK_EQ(grid->getX(0), 10.f);
    CHECK_EQ(grid->getY(0), 1.f);
    CHECK_EQ(grid->getZ(0), -5.f);

    PointGraph again;
    CHECK(again.addNode("grid", "grid.sample"));
    CHECK(again.setNodeInt("grid", "width", 4));
    CHECK(again.setNodeInt("grid", "depth", 3));
    CHECK(again.setNodeFloat("grid", "spacing", 2.f));
    CHECK(again.setNodeInt("grid", "seed", 11));
    CHECK(again.setNodeFloat("grid", "originX", 10.f));
    CHECK(again.setNodeFloat("grid", "originY", 1.f));
    CHECK(again.setNodeFloat("grid", "originZ", -5.f));
    std::unique_ptr<PointSet> gridB(again.execute("grid"));
    REQUIRE(bool(gridB));
    CHECK_EQ(gridB->getCount(), grid->getCount());
    for (int i = 0; i < grid->getCount(); ++i) {
        CHECK_EQ(gridB->getX(i), grid->getX(i));
        CHECK_EQ(gridB->getZ(i), grid->getZ(i));
        CHECK_EQ(gridB->getPointSeed(i), grid->getPointSeed(i));
    }

    PointGraph badGrid;
    CHECK(badGrid.addNode("grid", "grid.sample"));
    CHECK(badGrid.setNodeInt("grid", "width", 0));
    CHECK(!badGrid.validate());

    PointGraph poissonGraph;
    CHECK(poissonGraph.addNode("scatter", "poisson.sample"));
    CHECK(poissonGraph.setNodeInt("scatter", "width", 20));
    CHECK(poissonGraph.setNodeInt("scatter", "depth", 20));
    CHECK(poissonGraph.setNodeFloat("scatter", "radius", 2.f));
    CHECK(poissonGraph.setNodeInt("scatter", "seed", 99));
    CHECK(poissonGraph.setNodeInt("scatter", "maxPoints", 80));
    CHECK(poissonGraph.setNodeFloat("scatter", "originX", 100.f));
    CHECK(poissonGraph.validate());
    std::unique_ptr<PointSet> scatter(poissonGraph.execute("scatter"));
    REQUIRE(bool(scatter));
    CHECK(scatter->getCount() > 0);
    CHECK(scatter->getCount() <= 80);
    CHECK(scatter->getX(0) >= 100.f);

    PointGraph poissonBudget;
    CHECK(poissonBudget.addNode("scatter", "poisson.sample"));
    CHECK(poissonBudget.setNodeInt("scatter", "width", 50));
    CHECK(poissonBudget.setNodeInt("scatter", "depth", 50));
    CHECK(poissonBudget.setNodeFloat("scatter", "radius", 0.5f));
    CHECK(poissonBudget.setNodeInt("scatter", "maxPoints", 200));
    poissonBudget.setMaxNodeOutputPoints(50);
    CHECK(poissonBudget.validate());
    CHECK(!poissonBudget.execute("scatter"));
    CHECK(poissonBudget.getError().find("budget") != std::string::npos);
}

TEST_CASE("procgen.pointGraph.samplesLandscapeAndTextureOntoChannels") {
    Heightmap heights(3, 3);
    for (int z = 0; z < 3; ++z)
        for (int x = 0; x < 3; ++x) heights.setHeight(x, z, float(x));
    SpatialData landscape = SpatialData::heightfield(heights, 0.f, 0.f, 1.f, 1.f);

    PointSet candidates;
    candidates.add(0.f, 10.f, 1.f);
    candidates.add(2.f, 10.f, 1.f);
    candidates.setDensity(0, 0.f);
    candidates.setDensity(1, 0.f);

    PointGraph landscapeGraph;
    CHECK(landscapeGraph.addNode("in", "input"));
    CHECK(landscapeGraph.addNode("sample", "landscape.sample"));
    CHECK(landscapeGraph.setNodePoints("in", &candidates));
    CHECK(landscapeGraph.connect("in", "sample"));
    CHECK(landscapeGraph.setNodeSpatial("sample", &landscape));
    CHECK(landscapeGraph.setNodeInt("sample", "project", 1));
    CHECK(landscapeGraph.setNodeString("sample", "attribute", "$Density"));
    CHECK(landscapeGraph.setNodeFloat("sample", "inputMin", 0.f));
    CHECK(landscapeGraph.setNodeFloat("sample", "inputMax", 2.f));
    CHECK(landscapeGraph.validate());
    std::unique_ptr<PointSet> projected(landscapeGraph.execute("sample"));
    REQUIRE(bool(projected));
    CHECK_EQ(projected->getCount(), 2);
    CHECK_EQ(projected->getY(0), 0.f);
    CHECK_EQ(projected->getY(1), 2.f);
    CHECK_EQ(projected->getDensity(0), 0.f);
    CHECK_EQ(projected->getDensity(1), 1.f);

    Heightmap mask(2, 2);
    mask.setHeight(0, 0, 0.f);
    mask.setHeight(1, 0, 1.f);
    mask.setHeight(0, 1, 0.25f);
    mask.setHeight(1, 1, 0.75f);
    SpatialData texture = SpatialData::textureMask(mask, 0.f, 0.f, 1.f, 0.f, 1.f, -1.f, 1.f);

    PointSet maskPoints;
    maskPoints.add(0.f, 0.f, 0.f);
    maskPoints.add(1.f, 0.f, 0.f);

    PointGraph textureGraph;
    CHECK(textureGraph.addNode("in", "input"));
    CHECK(textureGraph.addNode("sample", "texture.sample"));
    CHECK(textureGraph.setNodePoints("in", &maskPoints));
    CHECK(textureGraph.connect("in", "sample"));
    CHECK(textureGraph.setBindingSpatial("mask", &texture).ok());
    CHECK(textureGraph.setNodeString("sample", "binding", "mask"));
    CHECK(textureGraph.setNodeString("sample", "attribute", "coverage"));
    CHECK(textureGraph.validate());
    std::unique_ptr<PointSet> covered(textureGraph.execute("sample"));
    REQUIRE(bool(covered));
    CHECK_EQ(covered->getFloatAttribute(0, "coverage", -1.f), 0.f);
    CHECK_EQ(covered->getFloatAttribute(1, "coverage", -1.f), 1.f);

    PointGraph wrongKind;
    CHECK(wrongKind.addNode("in", "input"));
    CHECK(wrongKind.addNode("sample", "texture.sample"));
    CHECK(wrongKind.setNodePoints("in", &maskPoints));
    CHECK(wrongKind.connect("in", "sample"));
    CHECK(wrongKind.setNodeSpatial("sample", &landscape));
    CHECK(wrongKind.validate());
    CHECK(!wrongKind.execute("sample"));
    CHECK(wrongKind.getError().find("texture_mask") != std::string::npos);
}

TEST_CASE("procgen.pointGraph.samplesSplinesFiltersDistanceAndBooleans") {
    PointSet controls;
    controls.add(0.f, 0.f, 0.f);
    controls.add(10.f, 0.f, 0.f);

    PointGraph graph;
    CHECK(graph.addNode("path", "input"));
    CHECK(graph.addNode("lamps", "spline.sample"));
    CHECK(graph.addNode("candidates", "input"));
    CHECK(graph.addNode("near", "spline.filter.distance"));
    CHECK(graph.addNode("keep", "points.difference"));
    CHECK(graph.setNodePoints("path", &controls));
    CHECK(graph.connect("path", "lamps"));
    CHECK(graph.setNodeFloat("lamps", "spacing", 5.f));
    CHECK(graph.setNodeInt("lamps", "seed", 7));

    PointSet candidates;
    candidates.add(0.f, 0.f, 0.f);
    candidates.add(5.f, 0.f, 0.5f);
    candidates.add(5.f, 0.f, 8.f);
    candidates.add(20.f, 0.f, 0.f);
    CHECK(graph.setNodePoints("candidates", &candidates));
    CHECK(graph.connect("candidates", "near", 0));
    CHECK(graph.connect("path", "near", 1));
    CHECK(graph.setNodeFloat("near", "minDistance", 0.f));
    CHECK(graph.setNodeFloat("near", "maxDistance", 2.f));
    CHECK(graph.connect("candidates", "keep", 0));
    CHECK(graph.connect("near", "keep", 1));
    CHECK(graph.validate());

    std::unique_ptr<PointSet> lamps(graph.execute("lamps"));
    REQUIRE(bool(lamps));
    CHECK(lamps->getCount() >= 2);

    std::unique_ptr<PointSet> near(graph.execute("near"));
    REQUIRE(bool(near));
    CHECK_EQ(near->getCount(), 2);

    std::unique_ptr<PointSet> keep(graph.execute("keep"));
    REQUIRE(bool(keep));
    CHECK_EQ(keep->getCount(), 2);
}

TEST_CASE("procgen.pointGraph.mapsNormalsBoundsSpawnAndDebug") {
    PointSet source;
    const int flat = source.add(0.f, 0.f, 0.f);
    const int steep = source.add(1.f, 0.f, 0.f);
    source.setNormal(flat, 0.f, 1.f, 0.f);
    source.setNormal(steep, 1.f, 0.f, 0.f);
    source.setBounds(flat, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    source.setPointSeed(flat, 11);
    source.setPointSeed(steep, 22);

    PointGraph graph;
    CHECK(graph.addNode("source", "input"));
    CHECK(graph.addNode("density", "density.from.normal"));
    CHECK(graph.addNode("bounds", "bounds.modify"));
    CHECK(graph.addNode("spawn", "spawn.mesh"));
    CHECK(graph.addNode("inspect", "debug.inspect"));
    CHECK(graph.addNode("disable", "debug.disable"));
    CHECK(graph.setNodePoints("source", &source));
    CHECK(graph.connect("source", "density"));
    CHECK(graph.setNodeFloat("density", "minDegrees", 0.f));
    CHECK(graph.setNodeFloat("density", "maxDegrees", 90.f));
    CHECK(graph.connect("density", "bounds"));
    CHECK(graph.setNodeFloat("bounds", "scaleX", 2.f));
    CHECK(graph.setNodeFloat("bounds", "scaleY", 2.f));
    CHECK(graph.setNodeFloat("bounds", "scaleZ", 2.f));
    CHECK(graph.connect("bounds", "spawn"));
    CHECK(graph.setNodeString("spawn", "mesh0", "tree.oak"));
    CHECK(graph.setNodeFloat("spawn", "weight0", 1.f));
    CHECK(graph.setNodeString("spawn", "mesh1", "tree.pine"));
    CHECK(graph.setNodeFloat("spawn", "weight1", 1.f));
    CHECK(graph.setNodeInt("spawn", "seed", 99));
    CHECK(graph.connect("spawn", "inspect"));
    CHECK(graph.connect("inspect", "disable"));
    CHECK(graph.setNodeInt("disable", "enabled", 1));
    CHECK(graph.validate());

    std::unique_ptr<PointSet> enabled(graph.execute("disable"));
    REQUIRE(bool(enabled));
    CHECK_EQ(enabled->getCount(), 2);
    CHECK(enabled->getDensity(0) < enabled->getDensity(1));
    CHECK(enabled->getBoundsMaxX(0) > 0.f);
    CHECK(enabled->hasStringAttribute(0, "mesh"));
    CHECK(enabled->hasStringAttribute(1, "mesh"));

    CHECK(graph.setNodeInt("disable", "enabled", 0));
    std::unique_ptr<PointSet> disabled(graph.execute("disable"));
    REQUIRE(bool(disabled));
    CHECK_EQ(disabled->getCount(), 0);
}

TEST_CASE("procgen.pointGraph.meshSampleRequiresMeshSurface") {
    PointGraph graph;
    CHECK(graph.addNode("sample", "mesh.sample"));
    SpatialData box = SpatialData::box(0.f, 0.f, 0.f, 2.f, 0.f, 2.f);
    CHECK(graph.setNodeSpatial("sample", &box));
    CHECK(graph.setNodeFloat("sample", "spacing", 1.f));
    CHECK(graph.validate());
    PointSet* rejected = graph.execute("sample");
    CHECK(!rejected);
    CHECK(graph.getError().find("surface.mesh") != std::string::npos);

    MeshBuild mesh;
    mesh.addVertex(0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f);
    mesh.addVertex(4.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f);
    mesh.addVertex(4.f, 0.f, 4.f, 0.f, 1.f, 0.f, 1.f, 1.f);
    mesh.addVertex(0.f, 0.f, 4.f, 0.f, 1.f, 0.f, 0.f, 1.f);
    mesh.addTriangle(0, 1, 2);
    mesh.addTriangle(0, 2, 3);
    SpatialData surface = SpatialData::meshSurface(mesh, 0.25f);
    CHECK(graph.setNodeSpatial("sample", &surface));
    std::unique_ptr<PointSet> sampled(graph.execute("sample"));
    REQUIRE(bool(sampled));
    CHECK(sampled->getCount() > 0);
}


TEST_CASE("procgen.pointGraph.exposesAttributeOps") {
    CHECK(PointGraph::getOperationInputCount("attribute.copy") == 1);
    CHECK(PointGraph::getOperationInputCount("attribute.rename") == 1);
    CHECK(PointGraph::getOperationInputCount("attribute.delete") == 1);
    CHECK(PointGraph::getOperationInputCount("attribute.transfer") == 2);
    CHECK(PointGraph::getOperationInputCount("attribute.set.int") == 1);
    CHECK(PointGraph::getOperationInputCount("attribute.set.bool") == 1);
    CHECK(PointGraph::getOperationInputCount("attribute.set.vector") == 1);
    CHECK(PointGraph::getOperationInputCount("attribute.compare.float") == 1);
    CHECK(PointGraph::getOperationInputCount("attribute.select.float") == 1);
    CHECK(PointGraph::getOperationInputCount("filter.int") == 1);
    CHECK(PointGraph::getOperationInputCount("filter.bool") == 1);
    CHECK_EQ(PointGraph::getOperationParamKey("attribute.transfer", 2), std::string("mode"));
    CHECK_EQ(PointGraph::getOperationParamKey("attribute.compare.float", 1), std::string("comparison"));
}

TEST_CASE("procgen.pointGraph.copiesRenamesTransfersAndSelectsAttributes") {
    PointSet source;
    const int a = source.add(0.f, 0.f, 0.f);
    const int b = source.add(10.f, 0.f, 0.f);
    source.setDensity(a, 0.25f);
    source.setDensity(b, 0.75f);
    CHECK(source.trySetFloatAttribute(a, "age", 3.f).ok());
    CHECK(source.trySetFloatAttribute(b, "age", 9.f).ok());
    CHECK(source.trySetPointId(a, 11).ok());
    CHECK(source.trySetPointId(b, 22).ok());

    PointGraph graph;
    CHECK(graph.addNode("source", "input"));
    CHECK(graph.addNode("copyDensity", "attribute.copy"));
    CHECK(graph.addNode("renameAge", "attribute.rename"));
    CHECK(graph.addNode("tagLayer", "attribute.set.int"));
    CHECK(graph.addNode("mark", "attribute.set.bool"));
    CHECK(graph.addNode("compare", "attribute.compare.float"));
    CHECK(graph.addNode("select", "attribute.select.float"));
    CHECK(graph.addNode("filterHot", "filter.bool"));
    CHECK(graph.setNodePoints("source", &source));
    CHECK(graph.connect("source", "copyDensity"));
    CHECK(graph.setNodeString("copyDensity", "source", "$Density"));
    CHECK(graph.setNodeString("copyDensity", "target", "densityCopy"));
    CHECK(graph.connect("copyDensity", "renameAge"));
    CHECK(graph.setNodeString("renameAge", "from", "age"));
    CHECK(graph.setNodeString("renameAge", "to", "years"));
    CHECK(graph.connect("renameAge", "tagLayer"));
    CHECK(graph.setNodeString("tagLayer", "attribute", "layer"));
    CHECK(graph.setNodeInt("tagLayer", "value", 4));
    CHECK(graph.connect("tagLayer", "mark"));
    CHECK(graph.setNodeString("mark", "attribute", "keep"));
    CHECK(graph.setNodeInt("mark", "value", 1));
    CHECK(graph.connect("mark", "compare"));
    CHECK(graph.setNodeString("compare", "attribute", "$Density"));
    CHECK(graph.setNodeString("compare", "comparison", "gt"));
    CHECK(graph.setNodeFloat("compare", "operand", 0.5f));
    CHECK(graph.setNodeString("compare", "outputAttribute", "hot"));
    CHECK(graph.connect("compare", "select"));
    CHECK(graph.setNodeString("select", "conditionAttribute", "hot"));
    CHECK(graph.setNodeString("select", "trueAttribute", "years"));
    CHECK(graph.setNodeString("select", "falseAttribute", "densityCopy"));
    CHECK(graph.setNodeString("select", "outputAttribute", "chosen"));
    CHECK(graph.connect("select", "filterHot"));
    CHECK(graph.setNodeString("filterHot", "attribute", "hot"));
    CHECK(graph.setNodeInt("filterHot", "value", 1));
    CHECK(graph.validate());

    std::unique_ptr<PointSet> filtered(graph.execute("filterHot"));
    REQUIRE(bool(filtered));
    CHECK_EQ(filtered->getCount(), 1);
    CHECK_EQ(filtered->getFloatAttribute(0, "densityCopy", -1.f), 0.75f);
    CHECK_EQ(filtered->getFloatAttribute(0, "years", -1.f), 9.f);
    CHECK_EQ(filtered->getIntAttribute(0, "layer", -1), 4);
    CHECK(filtered->getBoolAttribute(0, "hot", false));
    CHECK_EQ(filtered->getFloatAttribute(0, "chosen", -1.f), 9.f);
    CHECK(!filtered->hasFloatAttribute(0, "age"));

    PointSet donors;
    const int d0 = donors.add(0.f, 0.f, 0.f);
    const int d1 = donors.add(10.f, 0.f, 0.f);
    CHECK(donors.trySetPointId(d0, 11).ok());
    CHECK(donors.trySetPointId(d1, 22).ok());
    CHECK(donors.trySetStringAttribute(d0, "biome", "meadow").ok());
    CHECK(donors.trySetStringAttribute(d1, "biome", "forest").ok());

    PointGraph transfer;
    CHECK(transfer.addNode("targets", "input"));
    CHECK(transfer.addNode("donors", "input"));
    CHECK(transfer.addNode("paint", "attribute.transfer"));
    CHECK(transfer.addNode("drop", "attribute.delete"));
    CHECK(transfer.setNodePoints("targets", &source));
    CHECK(transfer.setNodePoints("donors", &donors));
    CHECK(transfer.connect("targets", "paint", 0));
    CHECK(transfer.connect("donors", "paint", 1));
    CHECK(transfer.setNodeString("paint", "attribute", "biome"));
    CHECK(transfer.setNodeString("paint", "mode", "id"));
    CHECK(transfer.connect("paint", "drop"));
    CHECK(transfer.setNodeString("drop", "attribute", "age"));
    CHECK(transfer.validate());
    std::unique_ptr<PointSet> painted(transfer.execute("drop"));
    REQUIRE(bool(painted));
    CHECK_EQ(painted->getCount(), 2);
    CHECK_EQ(painted->getStringAttribute(0, "biome", ""), std::string("meadow"));
    CHECK_EQ(painted->getStringAttribute(1, "biome", ""), std::string("forest"));
    CHECK(!painted->hasFloatAttribute(0, "age"));
}

TEST_CASE("procgen.pointGraph.writesBuiltinDensitySelector") {
    PointSet points;
    points.add(1.f, 2.f, 3.f);
    PointGraph graph;
    CHECK(graph.addNode("source", "input"));
    CHECK(graph.addNode("set", "attribute.set.float"));
    CHECK(graph.addNode("math", "attribute.math.float"));
    CHECK(graph.setNodePoints("source", &points));
    CHECK(graph.connect("source", "set"));
    CHECK(graph.setNodeString("set", "attribute", "$Density"));
    CHECK(graph.setNodeFloat("set", "value", 0.4f));
    CHECK(graph.connect("set", "math"));
    CHECK(graph.setNodeString("math", "attribute", "$Density"));
    CHECK(graph.setNodeString("math", "outputAttribute", "scaled"));
    CHECK(graph.setNodeString("math", "operation", "multiply"));
    CHECK(graph.setNodeFloat("math", "operand", 2.f));
    CHECK(graph.validate());
    std::unique_ptr<PointSet> result(graph.execute("math"));
    REQUIRE(bool(result));
    CHECK_EQ(result->getDensity(0), 0.4f);
    CHECK_EQ(result->getFloatAttribute(0, "scaled", -1.f), 0.8f);
}


TEST_CASE("procgen.pointGraph.bindingsResolveGetNodesAndInspect") {
    PointGraph graph;
    CHECK(graph.addNode("landscape", "get.landscape"));
    CHECK(graph.addNode("actors", "get.actor"));
    CHECK(graph.setNodeFloat("landscape", "spacing", 2.f));
    CHECK(graph.setNodeString("landscape", "binding", "terrain"));
    CHECK(graph.setNodeString("actors", "binding", "proxies"));

    Heightmap heightmap(4, 4);
    for (int z = 0; z < 4; ++z)
        for (int x = 0; x < 4; ++x) heightmap.setHeight(x, z, float(x + z) * 0.25f);
    SpatialData terrain = SpatialData::heightfield(heightmap, 0.f, 0.f, 1.f, 1.f);
    CHECK(graph.setBindingSpatial("terrain", &terrain).ok());

    PointSet proxies;
    proxies.add(1.f, 0.f, 2.f);
    proxies.add(3.f, 0.f, 4.f);
    CHECK(graph.setBindingPoints("proxies", &proxies).ok());
    CHECK_EQ(graph.getBindingCount(), 2);
    CHECK_EQ(graph.getBindingType("terrain"), std::string("spatial"));
    CHECK_EQ(graph.getBindingType("proxies"), std::string("points"));

    CHECK(graph.validate());
    std::unique_ptr<PointSet> landscape(graph.execute("landscape"));
    REQUIRE(bool(landscape));
    CHECK(landscape->getCount() > 0);

    std::unique_ptr<PointSet> actors(graph.execute("actors"));
    REQUIRE(bool(actors));
    CHECK_EQ(actors->getCount(), 2);

    auto inspect = graph.inspectNode("actors", 2);
    REQUIRE(inspect.ok());
    CHECK_EQ(inspect.value().pointCount, 2);
    CHECK(inspect.value().sampleCount > 0);
    bool sawBuiltin = false;
    for (const auto& column : inspect.value().columns)
        if (column.domain == "builtin") sawBuiltin = true;
    CHECK(sawBuiltin);
}

TEST_CASE("procgen.pointGraph.attributeDataDomainPartitionNoiseAndSelectors") {
    PointSet points;
    points.add(0.f, 0.f, 0.f);
    points.add(1.f, 0.f, 0.f);
    points.setYaw(0, 90.f);
    auto written = setPointDataFloatAttribute(points, "biomeSeed", 7.5f);
    REQUIRE(written.ok());
    CHECK_EQ(points.dataAttributes().rowCount(), 1);
    CHECK_EQ(points.dataAttributes().getFloat(0, "biomeSeed").value_or(-1.f), 7.5f);
    CHECK(points.trySetStringAttribute(0, "biome", "forest").ok());
    CHECK(points.trySetStringAttribute(1, "biome", "forest").ok());

    PointSet partitioned = partitionPointAttribute(points, "biome", "partition", "hash");
    CHECK(partitioned.hasIntAttribute(0, "partition"));
    PointSet noisy = noisePointFloatAttribute(partitioned, "noise", 11u, 1.f, 1.f, 0.f);
    CHECK(noisy.hasFloatAttribute(0, "noise"));

    PointGraph graph;
    CHECK(graph.addNode("input", "input"));
    CHECK(graph.addNode("swizzle", "attribute.math.float"));
    CHECK(graph.connect("input", "swizzle"));
    CHECK(graph.setNodePoints("input", &noisy));
    CHECK(graph.setNodeString("swizzle", "attribute", "$Position.ZYX.X"));
    CHECK(graph.setNodeString("swizzle", "outputAttribute", "swizzledZ"));
    CHECK(graph.setNodeString("swizzle", "operation", "add"));
    CHECK(graph.setNodeFloat("swizzle", "operand", 0.f));
    CHECK(graph.validate());
    std::unique_ptr<PointSet> result(graph.execute("swizzle"));
    REQUIRE(bool(result));
    CHECK_EQ(result->getFloatAttribute(0, "swizzledZ", -1.f), 0.f);
    // Data domain survives graph copies from the input PointSet.
    CHECK_EQ(result->dataAttributes().rowCount(), 1);
    auto dataValue = readPointFloatChannel(*result, 0, "@Data.biomeSeed", -1.f);
    REQUIRE(dataValue.ok());
    CHECK_EQ(dataValue.value(), 7.5f);
    auto lastNoise = readPointFloatChannel(*result, 0, "@Last.noise", -1.f);
    REQUIRE(lastNoise.ok());
    CHECK_EQ(lastNoise.value(), result->getFloatAttribute(0, "noise", -2.f));
    auto forward = readPointFloatChannel(*result, 0, "$Rotation.Forward.X", 0.f);
    REQUIRE(forward.ok());
}

TEST_CASE("procgen.pointGraph.spatialSampleAcceptsNamedBinding") {
    PointGraph graph;
    CHECK(graph.addNode("sample", "spatial.sample"));
    CHECK(graph.setNodeString("sample", "binding", "volume"));
    CHECK(graph.setNodeFloat("sample", "spacing", 2.f));
    SpatialData volume = SpatialData::box(0.f, 0.f, 0.f, 2.f, 0.f, 2.f);
    CHECK(graph.setBindingSpatial("volume", &volume).ok());
    CHECK(graph.validate());
    std::unique_ptr<PointSet> result(graph.execute("sample"));
    REQUIRE(bool(result));
    CHECK(result->getCount() > 0);
}
