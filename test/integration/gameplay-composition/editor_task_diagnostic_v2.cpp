#include <chrono>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <thread>
#include "common/Module.h"
#include "editor/EditorDiagnostics.h"
#include "editor/EditorTaskService.h"
#include "editor/EditorValueJson.h"
#include "editor/EditorWorkspace.h"
#include "editor/VegetationConverterController.h"
#include "graphics/material/editor/EditorGraph.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_task_diagnostic_v2.cpp
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
                                          eve::DiagnosticCode::PreconditionViolation, RuleId("park.name-required"),
                                          DiagnosticSeverity::Error, "Name is required")};
                                  return std::vector<EditorDiagnostic>{};
                              })
                .ok());
    auto found = validation.validate({"", EditorValue()});
    CHECK_EQ(found.size(), static_cast<std::size_t>(1));

    EditorDiagnosticService diagnostics;
    diagnostics.publish("validation", found);
    diagnostics.publish("compiler",
                        {eve::editing::ruleDiagnostic(eve::DiagnosticCode::Failed, RuleId("park.compile-warning"),
                                                      DiagnosticSeverity::Warning, "Warning")});
    CHECK_EQ(diagnostics.snapshot().size(), static_cast<std::size_t>(2));
    diagnostics.clear("validation");
    CHECK_EQ(diagnostics.snapshot().size(), static_cast<std::size_t>(1));
    CHECK_EQ(validation.unregisterOwner("park.plugin"), static_cast<std::size_t>(1));
    CHECK(validation.validate({"", EditorValue()}).empty());
}

TEST_CASE("editor.v2.settlementRuleValidationUsesCanonicalDecoder") {
    EditorValidationService validation;
    const std::string valid = R"({"schema":"settlement.rules","version":1,"conditionLanguageVersion":1,"rules":[]})";
    CHECK(validation.validate({"settlement.rules", EditorValue(valid)}).empty());

    const std::string unknownVersion =
        R"({"schema":"settlement.rules","version":2,"conditionLanguageVersion":1,"rules":[]})";
    const auto diagnostics = validation.validate({"settlement.rules", EditorValue(unknownVersion)});
    REQUIRE_EQ(diagnostics.size(), std::size_t{1});
    CHECK_EQ(diagnostics.front().code(), eve::DiagnosticCode::UnknownVersion);
    CHECK_EQ(diagnostics.front().path(), std::string("document.version"));

    CHECK(validation.validate({"another.document", EditorValue(unknownVersion)}).empty());
    const auto wrongType = validation.validate({"settlement.rules", EditorValue(std::int64_t{1})});
    REQUIRE_EQ(wrongType.size(), std::size_t{1});
    CHECK_EQ(wrongType.front().code(), eve::DiagnosticCode::TypeMismatch);
}

// editor_tilemap_target_script.cpp
TEST_CASE("editor.script.composes_live_tile_layer_target_with_field_tool") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        tileTargets <- eve.TileLayerTargetModule();
        map <- eve.Map();
        layer <- map.newLayer(8, 8, 16.0, 16.0);
        target <- tileTargets.create("ground", layer);
        falloff <- editor.newConstantBrushFalloff();
        kernel <- editor.newBoxBrushKernel();
        kernel.setConstantFalloff(falloff);
        operation <- editor.newPaintIntFieldOperation(17);
        tool <- editor.newFieldBrushTool("tile.paint", "Paint Tile");
        tool.setBoxKernel(kernel);
        tool.setPaintIntOperation(operation);
        tool.setRadius(0.5);
        session <- editor.newSession();
        added <- session.addFieldTool(tool);
        tileTargets.bind(session, target);
        activated <- session.activateTool("tile.paint");
        beforeRevision <- target.getRevision();
        session.dispatchPointer(0, 1, 0, 3.0, 4.0, 0.0, 0.0, 1.0);
        session.dispatchPointer(2, 1, 0, 3.0, 4.0, 0.0, 0.0, 1.0);
        painted <- target.readInt(3, 4);
        targetRevision <- target.getRevision();
        undone <- session.undo();
        restored <- target.readInt(3, 4);
        redone <- session.redo();
        replayed <- target.readInt(3, 4);
    )"));

    CHECK(vm.find("added").toBool());
    CHECK(vm.find("activated").toBool());
    CHECK_EQ(vm.find("painted").toInt(), 17);
    CHECK_GT(vm.find("targetRevision").toInt(), vm.find("beforeRevision").toInt());
    CHECK(vm.find("undone").toBool());
    CHECK_EQ(vm.find("restored").toInt(), 0);
    CHECK(vm.find("redone").toBool());
    CHECK_EQ(vm.find("replayed").toInt(), 17);
}

// editor_value_json_v2.cpp
using namespace eve::editor;

TEST_CASE("editor.v2.json_round_trips_without_host_dependencies") {
    const std::string source = R"({"z":"quote\"\\","a":[null,true,-7,1.25,"line\n\u4e2d\ud83d\ude80"]})";
    const auto        parsed = editorValueFromJson(source);
    REQUIRE(parsed.ok());
    REQUIRE(parsed.ok());

    const std::string encoded = editorValueToJson(parsed.value());
    CHECK(encoded.find(R"("a":[null,true,-7,1.25,"line\n中🚀"])") == 1);
    CHECK(encoded.find(R"("z":"quote\"\\")") != std::string::npos);

    const auto roundTrip = editorValueFromJson(encoded);
    REQUIRE(roundTrip.ok());
    REQUIRE(roundTrip.ok());
    CHECK(roundTrip.value() == parsed.value());
    CHECK_EQ(editorValueContentHash(roundTrip.value()), editorValueContentHash(parsed.value()));
}

TEST_CASE("editor.v2.json_rejects_invalid_and_out_of_range_input") {
    CHECK(!editorValueFromJson(R"({"value":01})").ok());
    CHECK(!editorValueFromJson(R"([1,])").ok());
    CHECK(!editorValueFromJson(R"("\ud800")").ok());
    CHECK(!editorValueFromJson("1e9999").ok());
    CHECK(!editorValueFromJson("true false").ok());
}

TEST_CASE("editor.v2.json_serializes_non_finite_numbers_as_null") {
    CHECK_EQ(editorValueToJson(EditorValue(std::numeric_limits<double>::infinity())), std::string("null"));
    CHECK_EQ(editorValueToJson(EditorValue(std::numeric_limits<double>::quiet_NaN())), std::string("null"));
}

// editor_vegetation_converter.cpp
using namespace eve;
using namespace eve::asset_import;
using namespace eve::editor;

namespace {
class Preparer final : public IVegetationConversionPreparer {
public:
    eve::Result<PreparedAssetImport> prepare(const UnityVegetationBatchImportRequest&) const override {
        if (reject)
            return eve::Result<PreparedAssetImport>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "injected prepare failure"));
        PreparedAssetImport result;
        for (const char* uri :
             {"asset://550e8400-e29b-41d4-a716-446655440001", "asset://550e8400-e29b-41d4-a716-446655440002",
              "asset://550e8400-e29b-41d4-a716-446655440003"}) {
            auto reference = AssetRef::parse(uri);
            REQUIRE(reference.ok());
            result.manifest.assets.push_back(
                {std::move(reference).takeValue(), "eve.test", SchemaVersion(1), "asset.json", "sha256:test", {}});
        }
        result.findings.resize(2);
        return eve::Result<PreparedAssetImport>::success(std::move(result));
    }
    bool reject = false;
};

class Publisher final : public IVegetationConversionPublisher {
public:
    eve::Result<std::uint64_t> publish(const PreparedAssetImport& candidate,
                                       std::uint64_t              expectedGeneration) override {
        ++calls;
        observedAssets = candidate.manifest.assets.size();
        if (reject)
            return editing::failed<std::uint64_t>(editing::Status::Conflict, editing::RuleId("test.converter.publish"),
                                                  "injected publish conflict");
        return editing::applied<std::uint64_t>(expectedGeneration + 1);
    }
    bool        reject         = false;
    int         calls          = 0;
    std::size_t observedAssets = 0;
};

UnityVegetationBatchImportRequest request(const char* name) {
    UnityVegetationBatchImportRequest result;
    result.package.packageId      = *PersistentId::parse("11111111-2222-4333-8444-555555555555");
    result.package.packageName    = name;
    result.package.packageVersion = "1";
    result.objects.emplace_back();
    return result;
}
}  // namespace

TEST_CASE("editor.vegetation_converter_prepares_and_publishes_revision_bound_candidate") {
    Preparer                      preparer;
    Publisher                     publisher;
    VegetationConverterController controller(preparer, publisher);
    REQUIRE(controller.setRequest(request("forest"), 7).ok());
    REQUIRE(controller.prepare().ok());
    auto state = controller.state();
    CHECK_EQ(static_cast<int>(state.phase), static_cast<int>(VegetationConverterState::Phase::Prepared));
    CHECK_EQ(state.assetCount, 3U);
    CHECK_EQ(state.findingCount, 2U);
    auto published = controller.publish();
    REQUIRE(published.ok());
    CHECK_EQ(published.value(), 8U);
    CHECK_EQ(publisher.calls, 1);
    CHECK_EQ(publisher.observedAssets, 3U);
}

TEST_CASE("editor.vegetation_converter_invalidates_old_candidate_and_retains_retry_on_failure") {
    Preparer                      preparer;
    Publisher                     publisher;
    VegetationConverterController controller(preparer, publisher);
    REQUIRE(controller.setRequest(request("forest"), 2).ok());
    REQUIRE(controller.prepare().ok());
    REQUIRE(controller.setRequest(request("meadow"), 2).ok());
    CHECK(!controller.publish().ok());
    publisher.reject = true;
    REQUIRE(controller.prepare().ok());
    CHECK(!controller.publish().ok());
    CHECK_EQ(publisher.calls, 1);
    publisher.reject = false;
    auto retried     = controller.publish();
    REQUIRE(retried.ok());
    CHECK_EQ(retried.value(), 3U);
}

TEST_CASE("editor.vegetation_converter_surfaces_prepare_diagnostics_without_candidate") {
    Preparer preparer;
    preparer.reject = true;
    Publisher                     publisher;
    VegetationConverterController controller(preparer, publisher);
    REQUIRE(controller.setRequest(request("forest"), 0).ok());
    CHECK(!controller.prepare().ok());
    const auto state = controller.state();
    CHECK_EQ(static_cast<int>(state.phase), static_cast<int>(VegetationConverterState::Phase::Error));
    CHECK(!state.diagnostics.empty());
    CHECK(!controller.publish().ok());
    CHECK_EQ(publisher.calls, 0);
}

// editor_workspace.cpp
using namespace eve::editor;

TEST_CASE("editor.workspace.composes_project_panels_without_ui_dependencies") {
    EditorWorkspace workspace("world-authoring", "World Authoring");
    CHECK_EQ(workspace.getId(), std::string("world-authoring"));
    CHECK_EQ(workspace.getMode(), std::string("edit"));

    REQUIRE(workspace.registerPanel("scene", "Scene", "center", 0));
    REQUIRE(workspace.registerPanel("hierarchy", "Hierarchy", "left", 10));
    REQUIRE(workspace.registerPanel("assets", "Assets", "bottom", 20));
    REQUIRE(workspace.registerPanel("inspector", "Inspector", "right", 30));
    CHECK(!workspace.registerPanel("scene", "Duplicate", "floating", 0));
    CHECK(!workspace.registerPanel("bad", "Bad", "unknown", 0));
    CHECK_EQ(workspace.getPanelCount(), 4);

    REQUIRE(workspace.setPanelCapability("scene", "scene.viewport"));
    REQUIRE(workspace.setPanelContext("scene", "scene"));
    REQUIRE(workspace.movePanel("assets", "left", 20));
    REQUIRE(workspace.setPanelSingleton("assets", false));
    REQUIRE(workspace.setPanelVisible("inspector", false));
    CHECK(!workspace.activatePanel("inspector"));
    REQUIRE(workspace.activatePanel("scene"));
    CHECK_EQ(workspace.getActivePanel(), std::string("scene"));
    CHECK(workspace.getRevision() > 0);

    workspace.setRegionSize("left", 240.f);
    workspace.setRegionSize("right", 300.f);
    workspace.setRegionSize("top", 40.f);
    workspace.setRegionSize("bottom", 180.f);
    workspace.layout(1280.f, 720.f);
    CHECK_EQ(workspace.getRegionX("center"), 240.f);
    CHECK_EQ(workspace.getRegionY("center"), 40.f);
    CHECK_EQ(workspace.getRegionW("center"), 740.f);
    CHECK_EQ(workspace.getRegionH("center"), 500.f);
}

TEST_CASE("editor.workspace.shares_selection_and_focus_channels_across_presenters") {
    EditorWorkspace workspace("runtime-builder", "Runtime Builder");
    REQUIRE(workspace.select("world", "scene", "level-1", "unit-1", "rts.unit", false));
    REQUIRE(workspace.select("world", "scene", "level-1", "unit-2", "rts.unit", true));
    CHECK_EQ(workspace.getSelectionCount("world"), 2);
    CHECK_EQ(workspace.getSelectionItem("world", 0), std::string("unit-1"));
    CHECK_EQ(workspace.getSelectionType("world", 1), std::string("rts.unit"));
    CHECK_EQ(workspace.getPrimarySelection("world"), std::string("unit-2"));
    CHECK(workspace.getSelectionSequence("world") >= static_cast<std::uint64_t>(2));

    REQUIRE(workspace.select("assets", "asset", "project", "mat-grass", "material", false));
    CHECK_EQ(workspace.getSelectionCount("world"), 2);
    CHECK_EQ(workspace.getSelectionCount("assets"), 1);
    CHECK(!workspace.select("world", "invalid", "level-1", "x", "bad", false));

    REQUIRE(workspace.focus("world", "scene-viewport", "unit-2"));
    CHECK_EQ(workspace.getFocusedSurface("world"), std::string("scene-viewport"));
    REQUIRE(workspace.clearSelection("world"));
    CHECK_EQ(workspace.getSelectionCount("world"), 0);
}
