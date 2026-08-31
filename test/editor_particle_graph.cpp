#include "particles_editing/ParticleGraph.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>

using namespace eve::particles_editing;
using namespace eve::editing;

namespace {

GraphDocumentData particleGraph(ParticleGraphDomain& domain, int bufferSize = 1000) {
    auto emission = domain.makeNode(GraphNodeId("emission"), "emission");
    auto motion = domain.makeNode(GraphNodeId("motion"), "motion");
    auto collision = domain.makeNode(GraphNodeId("collision"), "collision");
    auto renderer = domain.makeNode(GraphNodeId("renderer"), "renderer");
    auto output = domain.makeNode(GraphNodeId("output"), "output");
    REQUIRE(emission.value);
    REQUIRE(motion.value);
    REQUIRE(collision.value);
    REQUIRE(renderer.value);
    REQUIRE(output.value);
    auto* emissionProperties = emission.value->properties.getIf<EditorValue::Object>();
    auto* outputProperties = output.value->properties.getIf<EditorValue::Object>();
    REQUIRE(emissionProperties);
    REQUIRE(outputProperties);
    (*emissionProperties)["rate"] = 100.0;
    (*emissionProperties)["lifetime"] = EditorValue::Array{1.0, 2.0};
    (*outputProperties)["bufferSize"] = bufferSize;

    GraphDocument graph;
    REQUIRE(graph.createNode(*emission.value).isAccepted());
    REQUIRE(graph.createNode(*motion.value).isAccepted());
    REQUIRE(graph.createNode(*collision.value).isAccepted());
    REQUIRE(graph.createNode(*renderer.value).isAccepted());
    REQUIRE(graph.createNode(*output.value).isAccepted());
    const std::vector<std::pair<const char*, const char*>> links = {
        {"emission.out", "motion.in"}, {"motion.out", "collision.in"},
        {"collision.out", "renderer.in"}, {"renderer.out", "output.in"}};
    int index = 0;
    for (const auto& [fromId, toId] : links) {
        const GraphPinRecord* from = graph.findPin(GraphPinId(fromId));
        const GraphPinRecord* to = graph.findPin(GraphPinId(toId));
        REQUIRE(from);
        REQUIRE(to);
        REQUIRE(graph.connect({StableId("edge-" + std::to_string(index++)), from->id, to->id},
                              domain.canConnect(*from, *to)).isAccepted());
    }
    return graph.snapshot(domain.domain());
}

GraphNodeRecord* findNode(GraphDocumentData& graph, const char* type) {
    const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(), [&](const GraphNodeRecord& node) {
        return node.type == type;
    });
    return found == graph.nodes.end() ? nullptr : &*found;
}

}  // namespace

TEST_CASE("editor.particles.graph_compiles_real_emitter_modules") {
    ParticleGraphDomain domain;
    const GraphDocumentData graph = particleGraph(domain);
    const ParticleGraphCompileResult compiled = domain.compile(graph);
    CHECK_EQ(static_cast<int>(compiled.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(compiled.documentRevision, graph.revision);
    const auto* config = compiled.configuration.getIf<EditorValue::Object>();
    REQUIRE(config);
    CHECK(config->contains("emission"));
    CHECK(config->contains("motion"));
    CHECK(config->contains("collision"));
    CHECK(config->contains("renderer"));
    CHECK(config->contains("output"));
}

TEST_CASE("editor.particles.preview_is_revision_tagged_and_budget_bounded") {
    ParticleGraphDomain domain;
    const GraphDocumentData graph = particleGraph(domain, 150);
    const auto preview = domain.preview(graph, 10.0, 1.0 / 60.0, 500, 5);
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(preview.documentRevision, graph.revision);
    CHECK_EQ(preview.spawnedParticles, 500);
    CHECK_EQ(preview.droppedParticles, 500);
    CHECK_EQ(preview.peakLiveParticles, 150);
    CHECK(!preview.diagnostics.empty());

    const auto invalid = domain.preview(graph, -1.0);
    CHECK_EQ(static_cast<int>(invalid.status), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(domain.preview(graph, 601.0).status),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(domain.preview(graph, 1.0, 1.0e-9).status),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.particles.graph_rejects_cycles_missing_modules_and_bad_properties") {
    ParticleGraphDomain domain;
    GraphDocumentData graph = particleGraph(domain);
    graph.schemaVersion = 99;
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Unsupported));

    graph = particleGraph(domain);
    graph.nodes.erase(std::remove_if(graph.nodes.begin(), graph.nodes.end(), [](const GraphNodeRecord& node) {
                          return node.type == "renderer";
                      }),
                      graph.nodes.end());
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Failed));

    graph = particleGraph(domain);
    GraphNodeRecord* emission = findNode(graph, "emission");
    REQUIRE(emission);
    auto* properties = emission->properties.getIf<EditorValue::Object>();
    REQUIRE(properties);
    (*properties)["rate"] = -1.0;
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Failed));

    graph = particleGraph(domain);
    graph.edges.push_back({StableId("cycle"), GraphPinId("renderer.out"), GraphPinId("motion.in")});
    CHECK_EQ(static_cast<int>(domain.compile(graph).status), static_cast<int>(EditorStatus::Failed));
}
