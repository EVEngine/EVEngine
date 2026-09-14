#include <future>
#include <memory>
#include "procgen/PointGraph.h"
#include "procgen/Procgen.h"
#include "procgen/RuntimeGeneration.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::procgen;

namespace {
void configure(PointGraph& graph) {
    REQUIRE(graph.addNode("source", "input"));
    REQUIRE(graph.addNode("out", "transform"));
    REQUIRE(graph.connect("source", "out"));
    REQUIRE(graph.setComputePolicy("cpu"));
    PointSet points;
    points.add(1.f, 0.f, 2.f);
    REQUIRE(graph.setNodePoints("source", &points));
}
template <class T>
void requireFailure(const Result<T>& result, DiagnosticCode code, const std::string& path) {
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

TEST_CASE("procgen.result.cleanupDistinguishesEmptyThreadAndStale") {
    RuntimeGeneration runtime(17), other(17);
    auto              empty = runtime.nextCleanupRequest();
    REQUIRE(empty.ok());
    REQUIRE(!empty.value().has_value());
    const int level = runtime.addLevel(10.f, 4.f, 2.f);
    REQUIRE_EQ(level, 0);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    auto issued = runtime.nextGenerationJob();
    REQUIRE(issued.ok());
    REQUIRE(issued.value().has_value());
    auto completed = runtime.completeGenerationJob({*issued.value(), PointSet{}});
    REQUIRE(completed.ok());
    runtime.updateSource(1000.f, 1000.f, 1.f, 0.f);
    auto wrongThread = std::async(std::launch::async, [&] { return runtime.nextCleanupRequest(); });
    auto rejected    = wrongThread.get();
    requireFailure(rejected, DiagnosticCode::Conflict, "thread");
    auto cleanup = runtime.nextCleanupRequest();
    REQUIRE(cleanup.ok());
    REQUIRE(cleanup.value().has_value());
    const auto request = *cleanup.value();
    auto       foreign = other.completeCleanupRequest(request);
    REQUIRE(!foreign.ok());
    auto cleaned = runtime.completeCleanupRequest(request);
    REQUIRE(cleaned.ok());
    REQUIRE_EQ(cleaned.value(), uint64_t(1));
    auto replay = runtime.completeCleanupRequest(request);
    REQUIRE(!replay.ok());
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

TEST_CASE("procgen.result.workerGraphResultCommitsOnOwner") {
    RuntimeGeneration runtime(41);
    const int         level = runtime.addLevel(10.f, 4.f, 2.f);
    REQUIRE_EQ(level, 0);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    auto issued = runtime.nextGenerationJob();
    REQUIRE(issued.ok());
    REQUIRE(issued.value().has_value());
    const auto job       = *issued.value();
    auto       worker    = std::async(std::launch::async, [job] {
        PointGraph graph;
        const bool configured = graph.addNode("source", "input") && graph.setComputePolicy("cpu");
        if (!configured)
            return Result<PointSet>::failure(Diagnostic::error(DiagnosticCode::Failed, "worker setup failed"));
        PointSet points;
        points.add(job.getMinX(), 0.f, job.getMinZ());
        if (!graph.setNodePoints("source", &points))
            return Result<PointSet>::failure(Diagnostic::error(DiagnosticCode::Failed, "worker input failed"));
        return graph.executeResult("source");
    });
    auto       generated = worker.get();
    REQUIRE(generated.ok());
    auto committed = runtime.completeGenerationJob({job, std::move(generated).takeValue()});
    REQUIRE(committed.ok());
    REQUIRE_EQ(committed.value(), uint64_t(1));
    REQUIRE_EQ(runtime.getActiveCellCount(), 1);
}

TEST_CASE("procgen.result.squirrelExecutionAndSchedulerShareProjection") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        local procgen = eve.Procgen();
        local graph = procgen.newPointGraph().value;
        local points = procgen.sampleGrid(1, 1, 1.0, 18, 0.0).value;
        assert(graph.addNode("source", "input"));
        assert(graph.setNodePoints("source", points));
        assert(graph.validateResult().ok);
        local result = graph.executeResult("source");
        assert(result.ok && result.hasValue && result.value.getCount() == 1);
        local missing = graph.executeResult("missing");
        assert(!missing.ok && missing.value == null && missing.diagnostics[0].path == "missing");
        local runtime = procgen.newRuntimeGeneration(19).value;
        local empty = runtime.nextCleanupRequest();
        assert(empty.ok && empty.value == null);
        runtime.addLevel(10.0, 4.0, 2.0);
        runtime.updateSource(5.0, 5.0, 1.0, 0.0);
        local job = runtime.nextGenerationJob();
        assert(job.ok && job.value != null);
        assert(runtime.completeGenerationJob(job.value, result.value).ok);
        assert(!runtime.completeGenerationJob(job.value, result.value).ok);
        runtime.updateSource(1000.0, 1000.0, 1.0, 0.0);
        local cleanup = runtime.nextCleanupRequest();
        assert(cleanup.ok && cleanup.value != null);
        assert(runtime.completeCleanupRequest(cleanup.value).ok);
        assert(!runtime.completeCleanupRequest(cleanup.value).ok);
        runtime.updateSource(1005.0, 1005.0, 1.0, 0.0);
        local retry = runtime.nextGenerationJob();
        assert(retry.ok && retry.value != null);
        assert(runtime.failGenerationJob(retry.value).ok);
        assert(!runtime.failGenerationJob(retry.value).ok);
    )"));
}
