#include <memory>
#include "procgen/PointGraph.h"
#include "zeroerr/unittest.h"

using namespace eve::procgen;

namespace {
void makePlanFork(PointGraph& graph) {
    graph.setComputePolicy("cpu");
    PointSet points;
    points.add(1.f, 0.f, 0.f);
    REQUIRE(graph.addNode("source", "input"));
    REQUIRE(graph.setNodePoints("source", &points));
    for (const auto* name : {"left", "right"}) {
        REQUIRE(graph.addNode(name, "transform"));
        REQUIRE(graph.connect("source", name));
    }
}

void requirePlanOutput(PointGraph& graph, const std::string& id) {
    std::unique_ptr<PointSet> result(graph.execute(id));
    REQUIRE(bool(result));
    REQUIRE_EQ(result->getCount(), 1);
}
}  // namespace

TEST_CASE("procgen.pointGraphPlan.reusesAlternatingOutputsAndParameterChanges") {
    PointGraph graph;
    makePlanFork(graph);
    requirePlanOutput(graph, "left");
    requirePlanOutput(graph, "right");
    requirePlanOutput(graph, "left");
    REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(2));
    REQUIRE(graph.setNodeFloat("left", "x", 5.f));
    requirePlanOutput(graph, "left");
    requirePlanOutput(graph, "right");
    REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(2));
}

TEST_CASE("procgen.pointGraphPlan.invalidatesOnlyAffectedOutputsAndRecoversFromFailure") {
    PointGraph graph;
    makePlanFork(graph);
    requirePlanOutput(graph, "left");
    requirePlanOutput(graph, "right");
    REQUIRE(graph.disconnect("left", 0));
    requirePlanOutput(graph, "right");
    REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(2));
    std::unique_ptr<PointSet> invalid(graph.execute("left"));
    REQUIRE(!invalid);
    REQUIRE_EQ(graph.getCompiledSegmentCount(), 0);
    requirePlanOutput(graph, "right");
    REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(2));
    REQUIRE(graph.connect("source", "left"));
    requirePlanOutput(graph, "left");
    REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(3));
    REQUIRE(graph.removeNode("left"));
    requirePlanOutput(graph, "right");
    REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(3));
    REQUIRE(graph.removeNode("source"));
    std::unique_ptr<PointSet> stale(graph.execute("right"));
    REQUIRE(!stale);
    REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(3));
}

TEST_CASE("procgen.pointGraphPlan.boundsCacheWithLeastRecentlyUsedEviction") {
    PointGraph graph;
    graph.setComputePolicy("cpu");
    PointSet points;
    points.add(0.f, 0.f, 0.f);
    for (int index = 0; index < 16; ++index) {
        const auto name = "output" + std::to_string(index);
        REQUIRE(graph.addNode(name, "input"));
        REQUIRE(graph.setNodePoints(name, &points));
        requirePlanOutput(graph, name);
    }
    requirePlanOutput(graph, "output0");
    REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(16));
    REQUIRE(graph.addNode("extra", "input"));
    REQUIRE(graph.setNodePoints("extra", &points));
    requirePlanOutput(graph, "extra");
    requirePlanOutput(graph, "output0");
    REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(17));
    requirePlanOutput(graph, "output1");
    REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(18));
}

TEST_CASE("procgen.pointGraphPlan.copyInvalidationIsIndependentAndRestoreDropsPlans") {
    PointGraph graph;
    makePlanFork(graph);
    requirePlanOutput(graph, "left");
    requirePlanOutput(graph, "right");
    PointGraph copy = graph;
    REQUIRE(copy.disconnect("left", 0));
    requirePlanOutput(graph, "left");
    REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(2));
    requirePlanOutput(copy, "right");
    REQUIRE_EQ(copy.getExecutionPlanBuildCount(), uint64_t(2));
    const auto definition = graph.serializeDefinition();
    REQUIRE(copy.deserializeDefinition(definition));
    REQUIRE_EQ(copy.getExecutionPlanBuildCount(), uint64_t(0));
    REQUIRE_EQ(copy.getCompiledSegmentCount(), 0);
    PointSet points;
    points.add(1.f, 0.f, 0.f);
    REQUIRE(copy.setNodePoints("source", &points));
    requirePlanOutput(copy, "left");
    REQUIRE_EQ(copy.getExecutionPlanBuildCount(), uint64_t(1));
}

TEST_CASE("procgen.pointGraph.compilesAndReusesOutputExecutionPlans") {
    PointSet points;
    points.add(1.f, 0.f, 0.f);
    PointGraph graph;
    REQUIRE(graph.addNode("source", "input"));
    REQUIRE(graph.addNode("first", "transform"));
    REQUIRE(graph.addNode("second", "transform"));
    REQUIRE(graph.connect("source", "first"));
    REQUIRE(graph.connect("first", "second"));
    REQUIRE(graph.setNodePoints("source", &points));
    graph.setComputePolicy("cpu");

    std::unique_ptr<PointSet> first(graph.execute("second"));
    REQUIRE(bool(first));
    REQUIRE_EQ(graph.getCompiledSegmentCount(), 2);
    REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(1));

    REQUIRE(graph.setNodeFloat("second", "x", 4.f));
    std::unique_ptr<PointSet> parameterChange(graph.execute("second"));
    REQUIRE(bool(parameterChange));
    REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(1));

    REQUIRE(graph.addNode("third", "transform"));
    REQUIRE(graph.connect("second", "third"));
    std::unique_ptr<PointSet> topologyChange(graph.execute("third"));
    REQUIRE(bool(topologyChange));
    REQUIRE_EQ(graph.getExecutionPlanBuildCount(), uint64_t(2));
    REQUIRE_EQ(graph.getCompiledSegmentCount(), 2);
}

TEST_CASE("procgen.pointGraph.plansBranchBoundariesBeforeGpuExecution") {
    PointSet points;
    points.add(1.f, 0.f, 0.f);
    PointGraph graph;
    REQUIRE(graph.addNode("source", "input"));
    REQUIRE(graph.addNode("shared", "transform"));
    REQUIRE(graph.addNode("left", "transform"));
    REQUIRE(graph.addNode("right", "transform"));
    REQUIRE(graph.addNode("merged", "merge"));
    REQUIRE(graph.connect("source", "shared"));
    REQUIRE(graph.connect("shared", "left"));
    REQUIRE(graph.connect("shared", "right"));
    REQUIRE(graph.connect("left", "merged", 0));
    REQUIRE(graph.connect("right", "merged", 1));
    graph.setComputePolicy("cpu");
    REQUIRE(graph.setNodePoints("source", &points));

    std::unique_ptr<PointSet> output(graph.execute("merged"));
    REQUIRE(bool(output));
    REQUIRE_EQ(output->getCount(), 2);
    REQUIRE_EQ(graph.getCompiledSegmentCount(), 5);
}
