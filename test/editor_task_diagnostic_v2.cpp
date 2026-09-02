#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorDiagnostics.h"
#include "material_editor/EditorGraph.h"
#include "editor/EditorTaskService.h"

#include <chrono>
#include <thread>

using namespace eve::editor;
using namespace std::chrono_literals;

namespace {

GraphNodeRecord asyncOutputNode() {
    GraphNodeRecord node;
    node.id         = GraphNodeId("async-output");
    node.type       = "material.output";
    node.properties = EditorValue::Object{};
    return node;
}

}  // namespace

TEST_CASE("editor.v2.task_queue_is_queryable_and_cancellable") {
    EditorTaskService tasks;
    auto              blocker = tasks.submit("blocker", [](const EditorTaskContext& context) {
        while (!context.isCancellationRequested()) std::this_thread::sleep_for(1ms);
        EditorTaskOutcome result;
        result.status = EditorStatus::Cancelled;
        return result;
    });
    REQUIRE(blocker.ok());
    auto queued = tasks.submit("queued", [](const EditorTaskContext&) { return EditorTaskOutcome{}; });
    REQUIRE(queued.ok());
    CHECK(tasks.cancel(queued.value()).ok());
    CHECK(tasks.cancel(blocker.value()).ok());
    CHECK(tasks.waitIdle(2s).ok());
    CHECK_EQ(static_cast<int>(tasks.snapshot(queued.value()).value().state),
             static_cast<int>(EditorTaskState::Cancelled));
    CHECK_EQ(static_cast<int>(tasks.snapshot(blocker.value()).value().state),
             static_cast<int>(EditorTaskState::Cancelled));
}

TEST_CASE("editor.v2.material_compile_runs_on_background_task_service") {
    EditorTaskService     tasks;
    MaterialEditorService materials;
    MaterialGraphDomain   domain;
    GraphDocument         graph;
    REQUIRE(graph.createNode(asyncOutputNode()).ok());
    const DocumentId document("document:async-material");
    auto             task = materials.compileAsync(document, graph.snapshot(domain.domain()), domain, tasks);
    REQUIRE(task.ok());
    CHECK(tasks.waitIdle(2s).ok());
    auto result = materials.result(task.value());
    REQUIRE(result.ok());
    CHECK_EQ(static_cast<int>(result.value().status), static_cast<int>(EditorStatus::Applied));
    CHECK(materials.publishPreview(document, graph.revision(), task.value()).ok());
    CHECK(!materials.previewArtifact(document).empty());
}

TEST_CASE("editor.v2.validation_and_diagnostics_are_extension_owned") {
    EditorValidationService validation;
    REQUIRE(validation
                .registerRule("park.plugin", RuleId("park.name-required"),
                              [](const ValidationRequest& request) {
                                  if (request.subject.empty())
                                      return std::vector<EditorDiagnostic>{eve::editing::ruleDiagnostic(
                                          eve::DiagnosticCode::PreconditionViolation, RuleId("park.name-required"), DiagnosticSeverity::Error,
                                          "Name is required")};
                                  return std::vector<EditorDiagnostic>{};
                              })
                .ok());
    auto found = validation.validate({"", EditorValue()});
    CHECK_EQ(found.size(), static_cast<std::size_t>(1));

    EditorDiagnosticService diagnostics;
    diagnostics.publish("validation", found);
    diagnostics.publish("compiler", {eve::editing::ruleDiagnostic(
                                        eve::DiagnosticCode::Failed, RuleId("park.compile-warning"), DiagnosticSeverity::Warning, "Warning")});
    CHECK_EQ(diagnostics.snapshot().size(), static_cast<std::size_t>(2));
    diagnostics.clear("validation");
    CHECK_EQ(diagnostics.snapshot().size(), static_cast<std::size_t>(1));
    CHECK_EQ(validation.unregisterOwner("park.plugin"), static_cast<std::size_t>(1));
    CHECK(validation.validate({"", EditorValue()}).empty());
}
