#include "procgen/PointGraph.h"

#include <cmath>
#include <zeroerr.hpp>

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
    CHECK(first.setNodeFloat("move", "z", 3.f));
    CHECK(first.setNodeFloat("move", "x", 1.f));
    CHECK(first.setNodeString("move", "beta", "b"));
    CHECK(first.setNodeString("move", "alpha", "a"));
    CHECK(second.setNodeString("move", "alpha", "a"));
    CHECK(second.setNodeString("move", "beta", "b"));
    CHECK(second.setNodeFloat("move", "x", 1.f));
    CHECK(second.setNodeFloat("move", "z", 3.f));
    CHECK_EQ(first.serializeDefinition(), second.serializeDefinition());
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
