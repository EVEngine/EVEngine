#include "procgen/Biome.h"
#include "procgen/PointGraph.h"
#include "procgen/ShapeGrammar.h"

#include <memory>
#include <string>
#include <zeroerr.hpp>

using namespace eve::procgen;

namespace {

bool pointSetsEqual(const PointSet& first, const PointSet& second) {
    if (first.getCount() != second.getCount()) return false;
    for (int index = 0; index < first.getCount(); ++index) {
        const ProcgenPoint& a = first.points()[size_t(index)];
        const ProcgenPoint& b = second.points()[size_t(index)];
        if (a.x != b.x || a.y != b.y || a.z != b.z || a.normalX != b.normalX ||
            a.normalY != b.normalY || a.normalZ != b.normalZ || a.yaw != b.yaw ||
            a.scaleX != b.scaleX || a.scaleY != b.scaleY || a.scaleZ != b.scaleZ ||
            a.density != b.density || a.seed != b.seed ||
            a.floatAttributes != b.floatAttributes || a.stringAttributes != b.stringAttributes)
            return false;
    }
    return true;
}

}  // namespace

TEST_CASE("procgen.pointGraphStress.resumesDeepGraphAcrossBudgetWindows") {
    constexpr int nodeCount = 256;
    PointSet input;
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
    int budgetWindows = 0;
    while (!output && budgetWindows < 20) {
        output.reset(graph.execute(previous));
        ++budgetWindows;
        if (!output) CHECK(graph.wasCancelled());
    }
    REQUIRE(bool(output));
    CHECK_EQ(output->getX(0), 64.f);
    CHECK_EQ(budgetWindows, 16);
    CHECK(graph.getCacheHitCount() > 0);
}

TEST_CASE("procgen.pointGraphStress.isBitStableAcrossIndependentBiomeInstances") {
    SpatialData domain = SpatialData::box(-32.f, 0.f, -32.f, 32.f, 0.f, 32.f);
    SpatialData center = SpatialData::sphere(0.f, 0.f, 0.f, 12.f);
    BiomeRules rules;
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
