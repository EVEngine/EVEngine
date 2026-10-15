#include <future>
#include <memory>
#include "procgen/PointGraph.h"
#include "procgen/Procgen.h"
#include "procgen/RuntimeGeneration.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::procgen;

namespace {
[[maybe_unused]] void configure(PointGraph& graph) {
    REQUIRE(graph.addNode("source", "input"));
    REQUIRE(graph.addNode("out", "transform"));
    REQUIRE(graph.connect("source", "out"));
    REQUIRE(graph.setComputePolicy("cpu"));
    PointSet points;
    points.add(1.f, 0.f, 2.f);
    REQUIRE(graph.setNodePoints("source", &points));
}
template <class T>
[[maybe_unused]] void requireFailure(const Result<T>& result, DiagnosticCode code, const std::string& path) {
    REQUIRE(!result.ok());
    const auto* diagnostic = result.status().primaryDiagnostic();
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->code() == code);
    REQUIRE_EQ(diagnostic->path(), path);
}
}  // namespace

TEST_CASE("procgen.result.executionOwnsDiagnosticsAndOutput") {
    PointGraph graph;
    configure(graph);
    auto missing = graph.executeResult("missing");
    requireFailure(missing, DiagnosticCode::NotFound, "missing");
    auto output = graph.executeResult("out");
    REQUIRE(output.ok());
    auto points = std::move(output).takeValue();
    REQUIRE_EQ(points.getCount(), 1);
    REQUIRE(graph.removeNode("source"));
    auto invalid = graph.executeResult("out");
    requireFailure(invalid, DiagnosticCode::InvalidArgument, "out");
    requireFailure(missing, DiagnosticCode::NotFound, "missing");
    REQUIRE_EQ(points.getX(0), 1.f);
    std::unique_ptr<PointSet> legacy(graph.execute("out"));
    REQUIRE(!legacy);
    REQUIRE(!graph.getError().empty());
}

TEST_CASE("procgen.result.validationCycleCancellationAndBudgets") {
    PointGraph graph;
    configure(graph);
    auto valid = graph.validateResult();
    REQUIRE(valid.ok());
    graph.requestCancel();
    auto cancelled = graph.executeResult("out");
    requireFailure(cancelled, DiagnosticCode::Cancelled, "out");
    graph.resetCancellation();
    graph.setExecutionNodeBudget(1);
    auto budget = graph.executeResult("out");
    requireFailure(budget, DiagnosticCode::Cancelled, "out");
    graph.setExecutionNodeBudget(0);
    REQUIRE(graph.addNode("loop", "transform"));
    REQUIRE(graph.connect("out", "loop"));
    REQUIRE(graph.connect("loop", "out"));
    auto cycle = graph.executeResult("out");
    requireFailure(cycle, DiagnosticCode::Conflict, "out");
    auto invalid = graph.validateResult();
    requireFailure(invalid, DiagnosticCode::Conflict, "out");
}

TEST_CASE("procgen.result.nodeFailuresAndPointBudgetHavePaths") {
    PointGraph graph;
    REQUIRE(graph.addNode("source", "input"));
    auto unset = graph.executeResult("source");
    requireFailure(unset, DiagnosticCode::InvalidArgument, "source");
    PointSet points;
    points.add(1.f, 0.f, 0.f);
    points.add(2.f, 0.f, 0.f);
    REQUIRE(graph.setNodePoints("source", &points));
    graph.setMaxNodeOutputPoints(1);
    auto budget = graph.executeResult("source");
    requireFailure(budget, DiagnosticCode::InvalidArgument, "source");
    graph.setMaxNodeOutputPoints(0);
    auto recovered = graph.executeResult("source");
    REQUIRE(recovered.ok());
    REQUIRE_EQ(recovered.value().getCount(), 2);
}


TEST_CASE("procgen.result.nestedFailurePreservesNodePath") {
    PointGraph nested;
    configure(nested);
    REQUIRE(nested.addNode("math", "density.remap"));
    REQUIRE(nested.connect("out", "math"));
    REQUIRE(nested.setNodeFloat("math", "inputMax", 0.f));
    PointGraph graph;
    configure(graph);
    REQUIRE(graph.addNode("nested", "subgraph"));
    REQUIRE(graph.connect("out", "nested"));
    REQUIRE(graph.setNodeSubgraph("nested", &nested, "source", "math"));
    auto failed = graph.executeResult("nested");
    requireFailure(failed, DiagnosticCode::InvalidArgument, "nested/math");
}
