#include <cstdint>
#include <memory>
#include <string>
#include "procgen/Biome.h"
#include "procgen/PointDelta.h"
#include "procgen/PointGraph.h"
#include "procgen/ShapeGrammar.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// procgen_point_delta.cpp
using namespace eve::procgen;

namespace {

PointSet makePointSet(int count, std::uint64_t identityNamespace = 91) {
    PointSet points;
    for (int index = 0; index < count; ++index) {
        ProcgenPoint point;
        point.x            = float(index);
        point.seed         = std::uint32_t(index + 1);
        const int appended = points.appendPoint(point);
        (void)appended;
        points.trySetIntAttribute(std::size_t(index), "kind", index % 3).expect("point delta fixture metadata");
    }
    points.assignPointIds(identityNamespace).expect("point delta fixture ids");
    return points;
}

}  // namespace

TEST_CASE("procgen.pointIdentity.isDeterministicAndPreservedByTransforms") {
    const PointSet first  = sampleGridPoints(8, 8, 1.f, 42, 0.1f);
    const PointSet second = sampleGridPoints(8, 8, 1.f, 42, 0.1f);
    REQUIRE_EQ(first.getCount(), second.getCount());
    for (int index = 0; index < first.getCount(); ++index) {
        CHECK_NE(first.getPointId(index), std::uint64_t(0));
        CHECK_EQ(first.getPointId(index), second.getPointId(index));
    }
    const PointSet transformed = transformPointSet3D(first, 10.f, 2.f, -4.f, 0.f, 30.f, 0.f, 2.f, 2.f, 2.f);
    REQUIRE_EQ(transformed.getCount(), first.getCount());
    for (int index = 0; index < first.getCount(); ++index)
        CHECK_EQ(transformed.getPointId(index), first.getPointId(index));
}

TEST_CASE("procgen.pointIdentity.assignmentIsTransactional") {
    PointSet     legacy = makePointSet(3);
    ProcgenPoint duplicate;
    duplicate.id             = legacy.getPointId(0);
    const int duplicateIndex = legacy.appendPoint(duplicate);
    (void)duplicateIndex;
    const auto before = legacy.points();
    auto       failed = legacy.assignPointIds(33);
    CHECK(!failed.ok());
    REQUIRE_EQ(legacy.points().size(), before.size());
    for (std::size_t index = 0; index < before.size(); ++index) CHECK_EQ(legacy.points()[index].id, before[index].id);

    PointSet unassigned;
    unassigned.add(0.f, 0.f, 0.f);
    unassigned.add(1.f, 0.f, 0.f);
    PointSet repeated = unassigned;
    unassigned.assignPointIds(44).expect("assign legacy ids");
    repeated.assignPointIds(44).expect("repeat legacy ids");
    CHECK_EQ(unassigned.getPointId(0), repeated.getPointId(0));
    CHECK_EQ(unassigned.getPointId(1), repeated.getPointId(1));
}

TEST_CASE("procgen.pointIdentity.setOperationsPreferStableIdentity") {
    PointSet first = makePointSet(2);
    PointSet moved = first;
    moved.setPosition(0, 100.f, 0.f, 0.f);
    CHECK_EQ(unionPointSets(first, moved).getCount(), 2);
    CHECK_EQ(intersectPointSets(first, moved).getCount(), 2);

    PointSet     distinct;
    ProcgenPoint samePosition = first.points()[0];
    samePosition.id           = derivePointId(999, 1);
    const int distinctIndex   = distinct.appendPoint(samePosition);
    (void)distinctIndex;
    CHECK_EQ(unionPointSets(first, distinct).getCount(), 3);
}

TEST_CASE("procgen.pointDelta.roundTripsAddUpdateRemoveAndReorder") {
    PointSet before = makePointSet(4);
    PointSet after;
    after.appendPointFrom(before, 2).expect("reorder point");
    after.appendPointFrom(before, 0).expect("reorder point");
    after.setPosition(1, 20.f, 3.f, -2.f);
    after.trySetStringAttribute(1, "asset", "oak").expect("updated metadata");
    ProcgenPoint added;
    added.id             = derivePointId(92, 10);
    added.x              = 9.f;
    added.seed           = 88;
    const int addedIndex = after.appendPoint(added);
    (void)addedIndex;
    after.trySetStringAttribute(2, "asset", "rock").expect("added metadata");

    auto deltaResult = diffPointSets(before, after);
    REQUIRE(deltaResult.ok());
    PointDelta delta = std::move(deltaResult).takeValue();
    CHECK_EQ(delta.added.getCount(), 1);
    CHECK_EQ(delta.updated.getCount(), 2);
    CHECK_EQ(delta.removed.size(), std::size_t(2));
    auto applied = applyPointDelta(before, delta);
    REQUIRE(applied.ok());
    auto expectedFingerprint = fingerprintPointSet(after);
    auto actualFingerprint   = fingerprintPointSet(applied.value());
    REQUIRE(expectedFingerprint.ok());
    REQUIRE(actualFingerprint.ok());
    CHECK_EQ(actualFingerprint.value(), expectedFingerprint.value());
}

TEST_CASE("procgen.pointDelta.rejectsStaleBaseWithoutMutation") {
    PointSet before = makePointSet(3);
    PointSet after  = before;
    after.setPosition(0, 5.f, 0.f, 0.f);
    auto delta = diffPointSets(before, after);
    REQUIRE(delta.ok());
    before.setPosition(1, 7.f, 0.f, 0.f);
    auto applied = applyPointDelta(before, delta.value());
    CHECK(!applied.ok());
    CHECK_EQ(before.getX(1), 7.f);
}

TEST_CASE("procgen.pointSet.largeIdentityUnionAvoidsQuadraticScan") {
    PointSet first  = makePointSet(10000, 700);
    PointSet second = makePointSet(10000, 701);
    CHECK_EQ(unionPointSets(first, second).getCount(), 20000);
}

// procgen_point_graph_plan.cpp
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

// procgen_point_graph_stress.cpp
using namespace eve::procgen;

namespace {

bool pointSetsEqual(const PointSet& first, const PointSet& second) {
    if (first.getCount() != second.getCount()) return false;
    if (first.attributes().columnCount() != second.attributes().columnCount()) return false;
    for (int index = 0; index < first.getCount(); ++index) {
        const ProcgenPoint& a = first.points()[size_t(index)];
        const ProcgenPoint& b = second.points()[size_t(index)];
        if (a.x != b.x || a.y != b.y || a.z != b.z || a.normalX != b.normalX || a.normalY != b.normalY ||
            a.normalZ != b.normalZ || a.yaw != b.yaw || a.scaleX != b.scaleX || a.scaleY != b.scaleY ||
            a.scaleZ != b.scaleZ || a.density != b.density || a.seed != b.seed)
            return false;
        for (size_t column = 0; column < first.attributes().columnCount(); ++column) {
            const std::string name(first.attributes().columnName(column));
            if (first.attributes().typeOf(name) != second.attributes().typeOf(name) ||
                first.getAttributeType(index, name) != second.getAttributeType(index, name))
                return false;
            switch (*first.attributes().typeOf(name)) {
                case ProcgenAttributeType::Float:
                    if (first.attributes().getFloat(index, name) != second.attributes().getFloat(index, name))
                        return false;
                    break;
                case ProcgenAttributeType::Int:
                    if (first.attributes().getInt(index, name) != second.attributes().getInt(index, name)) return false;
                    break;
                case ProcgenAttributeType::Bool:
                    if (first.attributes().getBool(index, name) != second.attributes().getBool(index, name))
                        return false;
                    break;
                case ProcgenAttributeType::String:
                    if (first.getStringAttribute(index, name, {}) != second.getStringAttribute(index, name, {}))
                        return false;
                    break;
                case ProcgenAttributeType::Vector: {
                    const auto firstValue  = first.attributes().getVector(index, name);
                    const auto secondValue = second.attributes().getVector(index, name);
                    if (firstValue.has_value() != secondValue.has_value()) return false;
                    if (firstValue && (firstValue->x != secondValue->x || firstValue->y != secondValue->y ||
                                       firstValue->z != secondValue->z))
                        return false;
                    break;
                }
            }
        }
    }
    return true;
}

}  // namespace

TEST_CASE("procgen.pointGraphStress.resumesDeepGraphAcrossBudgetWindows") {
    constexpr int nodeCount = 64;
    PointSet      input;
    input.add(0.f, 0.f, 0.f);
    PointGraph graph;
    CHECK(graph.addNode("source", "input"));
    CHECK(graph.setNodePoints("source", &input));
    std::string previous = "source";
    for (int index = 0; index < nodeCount; ++index) {
        const std::string id = "move-" + std::to_string(index);
        CHECK(graph.addNode(id, "transform"));
        CHECK(graph.connect(previous, id));
        CHECK(graph.setNodeFloat(id, "x", 0.25f));
        previous = id;
    }
    CHECK(graph.validate());
    graph.setExecutionNodeBudget(17);

    std::unique_ptr<PointSet> output;
    int                       budgetWindows = 0;
    while (!output && budgetWindows < 20) {
        output.reset(graph.execute(previous));
        ++budgetWindows;
        if (!output) CHECK(graph.wasCancelled());
    }
    REQUIRE(bool(output));
    CHECK_EQ(output->getX(0), 16.f);
    CHECK_EQ(budgetWindows, 4);
    CHECK(graph.getCacheHitCount() > 0);
}

TEST_CASE("procgen.pointGraphStress.computePolicyPreservesDeterministicCpuPath") {
    PointSet input;
    input.add(2.f, 3.f, 4.f);
    PointGraph graph;
    CHECK(graph.addNode("source", "input"));
    CHECK(graph.addNode("move", "transform"));
    CHECK(graph.connect("source", "move"));
    CHECK(graph.setNodePoints("source", &input));
    CHECK(graph.setNodeFloat("move", "x", 5.f));
    CHECK(!graph.setComputePolicy("invalid"));
    CHECK(graph.setComputePolicy("cpu"));
    graph.setComputeMinimumPoints(0);

    std::unique_ptr<PointSet> output(graph.execute("move"));
    REQUIRE(bool(output));
    CHECK_EQ(output->getX(0), 7.f);
    CHECK_EQ(graph.getComputePolicy(), "cpu");
    CHECK_EQ(graph.getComputeMinimumPoints(), 0);
    CHECK_EQ(graph.getMetricBackend(graph.getMetricCount() - 1), "cpu");
    CHECK(graph.getComputeFallbackReason().empty());
    CHECK_EQ(graph.getComputeUploadCount(), uint64_t(0));
    CHECK_EQ(graph.getComputeDispatchCount(), uint64_t(0));
    CHECK_EQ(graph.getComputeReadbackCount(), uint64_t(0));
    CHECK_EQ(graph.getComputeBufferReuseCount(), uint64_t(0));
    CHECK_EQ(graph.getComputePeakBufferBytes(), uint64_t(0));
    CHECK_EQ(graph.getLastFusedTransformCount(), 0);
    CHECK(graph.debugReport().find("backend=cpu") != std::string::npos);
}

TEST_CASE("procgen.pointGraphStress.isBitStableAcrossIndependentBiomeInstances") {
    SpatialData domain = SpatialData::box(-32.f, 0.f, -32.f, 32.f, 0.f, 32.f);
    SpatialData center = SpatialData::sphere(0.f, 0.f, 0.f, 12.f);
    BiomeRules  rules;
    CHECK(rules.addLayer("forest", &domain, 1, 0.85f));
    CHECK(rules.addLayer("clearing", &center, 5, 0.35f));
    CHECK(rules.addAsset("forest", "oak", 3.f, 0.8f, 1.3f, true));
    CHECK(rules.addAsset("forest", "pine", 1.f, 0.9f, 1.4f, true));
    CHECK(rules.addAsset("clearing", "rock", 1.f, 0.7f, 1.1f, true));

    PointGraph asset;
    CHECK(asset.addNode("biome", "biome.generate"));
    CHECK(asset.setNodeFloat("biome", "spacing", 1.f));
    CHECK(asset.setNodeFloat("biome", "jitter", 0.75f));
    CHECK(asset.setNodeInt("biome", "seed", 0x5a17));
    std::unique_ptr<PointGraph> first(asset.instantiate());
    std::unique_ptr<PointGraph> second(asset.instantiate());
    REQUIRE(bool(first));
    REQUIRE(bool(second));
    CHECK(first->setNodeSpatial("biome", &domain));
    CHECK(first->setNodeBiomeRules("biome", &rules));
    CHECK(second->setNodeSpatial("biome", &domain));
    CHECK(second->setNodeBiomeRules("biome", &rules));

    std::unique_ptr<PointSet> firstOutput(first->execute("biome"));
    std::unique_ptr<PointSet> secondOutput(second->execute("biome"));
    REQUIRE(bool(firstOutput));
    REQUIRE(bool(secondOutput));
    CHECK(firstOutput->getCount() > 3000);
    CHECK(firstOutput->getCount() < 4225);
    CHECK(pointSetsEqual(*firstOutput, *secondOutput));
}

TEST_CASE("procgen.pointGraphStress.recoversAfterExternalRuleAndDataErrors") {
    PointSet path;
    path.add(0.f, 0.f, 0.f);
    path.add(10.f, 0.f, 0.f);
    ShapeGrammar grammar;
    CHECK(grammar.addModule("A", "wall", 2.f));
    PointGraph graph;
    CHECK(graph.addNode("path", "input"));
    CHECK(graph.addNode("facade", "grammar.generate"));
    CHECK(graph.connect("path", "facade"));
    CHECK(graph.setNodeString("facade", "grammar", "Missing+"));
    CHECK(!graph.execute("facade"));
    CHECK(graph.getError().find("no points") != std::string::npos);

    CHECK(graph.setNodePoints("path", &path));
    CHECK(graph.setNodeShapeGrammar("facade", &grammar));
    CHECK(!graph.execute("facade"));
    CHECK(graph.getError().find("unknown module") != std::string::npos);
    CHECK(graph.setNodeString("facade", "grammar", "A+"));
    std::unique_ptr<PointSet> recovered(graph.execute("facade"));
    REQUIRE(bool(recovered));
    CHECK_EQ(recovered->getCount(), 5);
    CHECK_EQ(recovered->getStringAttribute(0, "asset", ""), std::string("wall"));
}

TEST_CASE("procgen.pointGraphStress.rejectsOversizedOutputsBeforeSpatialAllocation") {
    SpatialData domain = SpatialData::box(0.f, 0.f, 0.f, 100.f, 0.f, 100.f);
    PointGraph  graph;
    CHECK(graph.addNode("sample", "spatial.sample"));
    CHECK(graph.setNodeSpatial("sample", &domain));
    CHECK(graph.setNodeFloat("sample", "spacing", 1.f));
    graph.setMaxNodeOutputPoints(1000);
    CHECK_EQ(graph.getMaxNodeOutputPoints(), 1000);
    CHECK(!graph.execute("sample"));
    CHECK(graph.getError().find("point budget") != std::string::npos);
    CHECK(!graph.getNodeOutput("sample"));

    graph.setMaxNodeOutputPoints(11000);
    std::unique_ptr<PointSet> output(graph.execute("sample"));
    REQUIRE(bool(output));
    CHECK_EQ(output->getCount(), 10201);

    graph.setMaxNodeOutputPoints(1000);
    CHECK(!graph.execute("sample"));
    CHECK(graph.getError().find("point budget") != std::string::npos);

    Heightmap heightmap(11, 11);
    heightmap.setHeight(10, 10, 100.f);
    SpatialData terrain = SpatialData::heightfield(heightmap, 0.f, 0.f, 1.f, 1.f);
    PointGraph  terrainGraph;
    CHECK(terrainGraph.addNode("sample", "spatial.sample"));
    CHECK(terrainGraph.setNodeSpatial("sample", &terrain));
    terrainGraph.setMaxNodeOutputPoints(121);
    output.reset(terrainGraph.execute("sample"));
    REQUIRE(bool(output));
    CHECK_EQ(output->getCount(), 121);
}

TEST_CASE("procgen.pointGraphStress.preflightsInputMergeAndCopyAllocations") {
    PointSet first;
    PointSet second;
    for (int index = 0; index < 60; ++index) {
        first.add(float(index), 0.f, 0.f);
        second.add(0.f, 0.f, float(index));
    }
    PointGraph graph;
    CHECK(graph.addNode("first", "input"));
    CHECK(graph.addNode("second", "input"));
    CHECK(graph.addNode("merge", "merge"));
    CHECK(graph.addNode("copy", "copy.points"));
    graph.setMaxNodeOutputPoints(100);
    CHECK(graph.setNodePoints("first", &first));
    CHECK(graph.setNodePoints("second", &second));
    CHECK(graph.connect("first", "merge", 0));
    CHECK(graph.connect("second", "merge", 1));
    CHECK(graph.connect("first", "copy", 0));
    CHECK(graph.connect("second", "copy", 1));
    CHECK(graph.setNodeInt("copy", "maxPoints", 10000));
    CHECK(!graph.execute("merge"));
    CHECK(graph.getError().find("point budget") != std::string::npos);
    CHECK(!graph.execute("copy"));
    CHECK(graph.getError().find("point budget") != std::string::npos);

    PointSet oversized;
    for (int index = 0; index < 101; ++index) oversized.add(float(index), 0.f, 0.f);
    CHECK(!graph.setNodePoints("first", &oversized));
    CHECK(graph.getError().find("input exceeds") != std::string::npos);
}
