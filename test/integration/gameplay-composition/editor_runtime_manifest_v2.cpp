#include "PathBesideSource.h"
#include <filesystem>
#include <fstream>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include <string>
#include "common/Module.h"
#include "editor/EditCommand.h"
#include "editor/Editor.h"
#include "editor/EditorDiskDocumentStore.h"
#include "editor/EditorProtocol.h"
#include "editor/EditorRuntimeManifest.h"
#include "editor/EditorSelection.h"
#include "editor/EditorSession.h"
#include "editor/EditorSimulationPreview.h"
#include "editor/EditorValue.h"
#include "graphics/lighting/editing/SnowFieldTarget.h"
#include "map/level/editing/FieldTargets.h"
#include "weather/SnowField.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_runtime_manifest_v2.cpp
using namespace eve::editor;

namespace {

class RuntimeParkExtension final : public IGameEditorExtension {
public:
    std::string ownerModule() const override { return "park.runtime"; }
    void        registerEditor(IEditorExtensionRegistry& registry) override {
        CommandDescriptor command;
        command.id               = CommandId("park.place-tree");
        command.displayName      = "Place tree";
        command.requiredFeatures = HostFeature::RuntimeWorld;
        CHECK(registry
                  .registerCommand(
                      std::move(command),
                      [](const CommandContext&, const EditorValue&) {
                          return eve::editing::applied<EditorValue>(EditorValue("tree"));
                      },
                      ExtensionAudience::Player)
                  .ok());
        CHECK(
            registry.registerTool({ToolId("park.tree-tool"), {}, "Tree", "Build", {}, ExtensionAudience::Player}).ok());
        CHECK(registry.registerRule({RuleId("park.path-rule"), {}, ExtensionAudience::Player}).ok());
    }
};

AssetRecord runtimeAsset(const char* guid, const char* uri) {
    AssetRecord record;
    record.guid       = AssetGuid(guid);
    record.logicalUri = uri;
    record.typeId     = "park.asset";
    return record;
}

}  // namespace

TEST_CASE("editor.v2.runtime_manifest_is_explicit_and_closes_asset_dependencies") {
    EditorCommandService    commands;
    EditorExtensionRegistry extensions(&commands);
    RuntimeParkExtension    park;
    REQUIRE(extensions.load(park).ok());

    MemoryAssetDatabase assets;
    REQUIRE(assets
                .publish(runtimeAsset("park", "content://Park"),
                         {{AssetGuid("park"), AssetGuid("tree"), DependencyKind::Hard, {}},
                          {AssetGuid("park"), AssetGuid("source"), DependencyKind::EditorOnly, {}}})
                .ok());
    REQUIRE(assets.publish(runtimeAsset("tree", "content://Tree")).ok());
    REQUIRE(assets.publish(runtimeAsset("source", "content://TreeSource")).ok());

    RuntimeEditorManifest manifest;
    manifest.commands   = {CommandId("park.place-tree")};
    manifest.tools      = {ToolId("park.tree-tool")};
    manifest.rules      = {RuleId("park.path-rule")};
    manifest.rootAssets = {AssetGuid("park")};
    auto package        = RuntimeEditorPublisher().publish(manifest, commands, extensions, assets);
    REQUIRE(package.ok());
    CHECK(package.value().profile.allowsCommand(CommandId("park.place-tree")));
    CHECK(!package.value().profile.allowsCommand(CommandId("developer.delete-source")));
    CHECK_EQ(package.value().tools.size(), static_cast<std::size_t>(1));
    CHECK_EQ(package.value().rules.size(), static_cast<std::size_t>(1));
    CHECK_EQ(package.value().assetClosure.size(), static_cast<std::size_t>(2));
}

TEST_CASE("editor.v2.runtime_manifest_rejects_developer_features") {
    EditorCommandService    commands;
    EditorExtensionRegistry extensions(&commands);
    MemoryAssetDatabase     assets;
    RuntimeEditorManifest   manifest;
    manifest.features = HostFeature::RuntimeWorld | HostFeature::ArbitraryScript;
    auto result       = RuntimeEditorPublisher().publish(manifest, commands, extensions, assets);
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(EditorStatus::Rejected));
}

// editor_script_v2.cpp
using namespace eve::editor;

TEST_CASE("editor.v2.script_discovers_plans_and_executes_registered_command") {
    Editor* editor   = Editor::create();
    auto&   commands = editor->commandService();

    const CommandId commandId("test.script.place-attraction");
    commands.unregisterCommand(commandId);
    CommandDescriptor descriptor;
    descriptor.id          = commandId;
    descriptor.ownerModule = "test.script";
    descriptor.displayName = "Place Attraction";
    descriptor.category    = "Build";

    bool plannerSawScriptSource = false;
    int  executedAmount         = 0;
    CHECK(commands
              .registerPlannedCommand(
                  descriptor,
                  [&](const CommandRequest& request) {
                      plannerSawScriptSource = request.source == CommandSource::Script;
                      const auto* object     = request.payload.getIf<EditorValue::Object>();
                      if (!object)
                          return eve::editing::failed<CommandPlan>(
                              EditorStatus::Rejected, RuleId("test.script.payload"), "Object payload required");
                      const auto amount = object->find("amount");
                      if (amount == object->end() || !amount->second.getIf<int64_t>())
                          return eve::editing::failed<CommandPlan>(EditorStatus::Rejected, RuleId("test.script.amount"),
                                                                   "Integer amount required");
                      CommandPlan plan;
                      plan.summary = "Place an attraction from a script-authored request";
                      DomainOperation operation;
                      operation.type        = "test.place";
                      operation.inverseType = "test.unplace";
                      operation.payload     = request.payload;
                      operation.hasInverse  = true;
                      plan.operations.push_back(std::move(operation));
                      return eve::editing::applied<CommandPlan>(std::move(plan));
                  },
                  [&](const CommandRequest& request, const CommandPlan& plan) {
                      const auto& object = *request.payload.getIf<EditorValue::Object>();
                      executedAmount     = static_cast<int>(*object.at("amount").getIf<int64_t>());
                      TransactionReceipt receipt;
                      receipt.id               = TransactionId(plan.id.value());
                      receipt.state            = TransactionState::Committed;
                      receipt.beforeRevision   = plan.baseRevision;
                      receipt.afterRevision    = plan.baseRevision + 1;
                      receipt.authorityReceipt = "local:test.script";
                      return eve::editing::applied<TransactionReceipt>(std::move(receipt));
                  })
              .ok());

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        session <- editor.newSession();
        found <- false;
        for (local i = 0; i < session.getCommandCount(); ++i) {
            if (session.getCommandId(i) == "test.script.place-attraction" &&
                session.getCommandName(i) == "Place Attraction" &&
                session.getCommandCategory(i) == "Build") found = true;
        }
        planned <- session.planCommand("test.script.place-attraction", {
            amount = 7,
            placement = { x = 12.5, y = 4.0 },
            tags = ["ride", "family"]
        });
        executed <- session.executePlan(planned.planId, {});
    )"));

    CHECK(vm.find("found").toBool());
    ssq::Table planned(vm.find("planned"));
    ssq::Table executed(vm.find("executed"));
    CHECK(planned.get<bool>("accepted"));
    CHECK_EQ(planned.get<std::string>("status"), std::string("applied"));
    CHECK(executed.get<bool>("accepted"));
    CHECK_EQ(executed.get<std::string>("transactionState"), std::string("committed"));
    CHECK_EQ(executed.get<int64_t>("afterRevision"), int64_t{1});
    CHECK(plannerSawScriptSource);
    CHECK_EQ(executedAmount, 7);

    CHECK(commands.unregisterCommand(commandId, "test.script"));
}

TEST_CASE("editor.v2.script_game_injects_command_into_the_same_session_protocol") {
    Editor* editor = Editor::create();
    editor->commandService().unregisterCommand(CommandId("test.script.spawn-tree"));

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        placed <- 0;
        registered <- editor.registerScriptCommand(
            "test.script.spawn-tree", "Spawn Tree", "Build",
            function(payload) {
                if (!("count" in payload) || payload.count <= 0) return false;
                placed += payload.count;
                return true;
            });
        session <- editor.newSession();
        planned <- session.planCommand("test.script.spawn-tree", { count = 3 });
        executed <- session.executePlan(planned.planId, {});
    )"));

    CHECK(vm.find("registered").toBool());
    CHECK_EQ(vm.find("placed").toInt(), 3);
    ssq::Table executed(vm.find("executed"));
    CHECK(executed.get<bool>("accepted"));
    CHECK_EQ(executed.get<std::string>("authorityReceipt"), std::string("script:local"));

    CHECK(editor->commandService().unregisterCommand(CommandId("test.script.spawn-tree"),
                                                     "script:test.script.spawn-tree"));
}

TEST_CASE("editor.v2.runtime_builder_quick_add_click_executes_command") {
    const std::filesystem::path sourceRoot = std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path().parent_path();
    const std::filesystem::path scriptPath = sourceRoot / "examples" / "editor-api-v2" / "main.nut";
    std::ifstream               input(scriptPath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();

    const std::string harness = R"(
        eve_init <- null;
        eve_update <- null;
        eve_render <- null;
        persist <- function(key, factory) { return factory(); };
        gfx <- {
            rects = [],
            canvasDraws = 0,
            newCanvas = function(w, h) { return { width=w, height=h }; },
            setCanvas = function(canvas) {},
            drawCanvas = function(canvas, x, y, w, h) { canvasDraws += 1; },
            setBackgroundColor = function(r, g, b, a) {},
            clear = function() {},
            drawSolidRect = function(x, y, w, h, r, g, b, a) {
                rects.append({ x=x, y=y, w=w, h=h, r=r, g=g, b=b, a=a });
            }
        };
        mouse <- {
            isDown = function(button) { return false; },
            getX = function() { return 0.0; },
            getY = function() { return 0.0; }
        };
        ui <- {
            clicks = [],
            setTheme = function(name) {},
            beginBuild = function() {},
            beginWindow = function(title, id) {},
            text = function(value, id) {},
            beginRow = function(id, gap) {},
            button = function(label, id) {},
            end = function() {},
            mountBuildAs = function(name) {},
            select = function(name) {},
            setHostOverlay = function(enabled) {},
            setHostPos = function(x, y, z, w) {},
            setHostSize = function(w, h) {},
            setText = function(id, value) {},
            wantCaptureMouse = function() { return false; },
            beginFrameAndRender = function() {},
            consumeClick = function() {
                if (clicks.len() == 0) return "";
                local value = clicks[0];
                clicks.remove(0);
                return value;
            }
        };
    )";

    ssq::VM vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    const std::string executable = harness + source.str() + R"(
        eve_init();
        ui.clicks.append("editor-v2/asset-tree");
        eve_update(0.016);
        eve_render();
        quickAddObjectCount <- editorV2.objects.len();
        quickAddCredits <- editorV2.credits;
        quickAddAsset <- editorV2.objects[0].asset;
        quickAddCanvasDraws <- gfx.canvasDraws;
        quickAddRenderedTree <- false;
        foreach (rect in gfx.rects) {
            if (rect.w >= 18.0 && rect.h >= 12.0 && rect.g > 0.6)
                quickAddRenderedTree = true;
        }
        cleanupResult <- editorV2.editor.unregisterScriptCommand("park.scene.place-asset");
    )";
    vm.run(vm.compileSource(executable.c_str(), "examples/editor-api-v2/main.nut"));

    CHECK_EQ(vm.find("quickAddObjectCount").toInt(), 1);
    CHECK_EQ(vm.find("quickAddCredits").toInt(), 580);
    CHECK_EQ(vm.find("quickAddAsset").toString(), std::string("park.asset.tree"));
    CHECK_EQ(vm.find("quickAddCanvasDraws").toInt(), 1);
    CHECK(vm.find("quickAddRenderedTree").toBool());
    CHECK(vm.find("cleanupResult").toBool());
}

// editor_selection_v2.cpp
using namespace eve::editor;

TEST_CASE("editor.v2.selection_and_focus_are_presentation_neutral_channels") {
    EditorSelectionService selection;
    int                    notifications = 0;
    REQUIRE(selection.subscribe("park.hud", [&](const SelectionSnapshot&) { ++notifications; }).ok());
    SelectionItem tree{SelectionDomain::Scene, TargetId("park"), StableId("tree-1"), "park.tree"};
    auto          selected = selection.set("world", {tree}, tree);
    REQUIRE(selected.ok());
    CHECK_EQ(selected.value().sequence, static_cast<std::uint64_t>(1));
    CHECK(selection.snapshot("world").primary == tree);
    CHECK_EQ(notifications, 1);
    CHECK_EQ(static_cast<int>(selection.set("world", {}, tree).code()), static_cast<int>(EditorStatus::Rejected));
    REQUIRE(selection.clear("world").ok());
    CHECK_EQ(notifications, 2);
    CHECK(selection.unsubscribe("park.hud"));

    EditorFocusService focus;
    auto               focused = focus.focus("world", StableId("viewport"), StableId("tree-1"));
    REQUIRE(focused.ok());
    CHECK(focus.snapshot("world").surface == StableId("viewport"));
}

// editor_session_persistence.cpp
using namespace eve::editor;

namespace {

EditorValue versionValue(int version) { return EditorValue(EditorValue::Object{{"version", EditorValue(version)}}); }

}  // namespace

TEST_CASE("editor.session.autosavesActiveDocument") {
    MemoryAtomicDocumentStore store;
    DocumentService           documents(&store);
    AutosaveService           autosave(&store, "live-project");
    const DocumentKey         key{DocumentKind::Scene, AssetGuid("live-scene")};
    auto opened = documents.open(key, "Live Scene", "content://World/Live.evscene", versionValue(0));
    REQUIRE(opened.ok());

    EditorSession session;
    session.setDocumentServices(&documents, &autosave);
    REQUIRE(session.bindDocument(opened.value().id).ok());
    session.setAutosaveInterval(0.1f);
    session.setExternalPollInterval(0.f);
    REQUIRE(session.editDocument(versionValue(1)).ok());
    session.update(0.11f);

    CHECK_EQ(session.lastAutosavedRevision(), Revision(1));
    auto draft = autosave.readDraft(opened.value().id);
    REQUIRE(draft.ok());
    CHECK(draft.value().content == versionValue(1));
    CHECK(documents.snapshot(opened.value().id).value().dirty());
}

TEST_CASE("editor.session.adoptsCleanExternalChangesAndProtectsDirtyEdits") {
    MemoryAtomicDocumentStore store;
    DocumentService           documents(&store);
    const DocumentKey         key{DocumentKind::Scene, AssetGuid("conflict-scene")};
    auto opened = documents.open(key, "Conflict Scene", "content://World/Conflict.evscene", versionValue(0));
    REQUIRE(opened.ok());

    EditorSession session;
    session.setDocumentServices(&documents);
    REQUIRE(session.bindDocument(opened.value().id).ok());
    REQUIRE(session.editDocument(versionValue(1)).ok());
    REQUIRE(session.saveDocument().ok());

    auto disk = store.read("content://World/Conflict.evscene");
    REQUIRE(disk.ok());
    REQUIRE(store
                .compareAndSwap("content://World/Conflict.evscene", disk.value().revision, disk.value().contentHash,
                                versionValue(2))
                .ok());
    REQUIRE(session.pollDocumentChanges().ok());
    CHECK(documents.content(opened.value().id).value() == versionValue(2));
    CHECK(!documents.snapshot(opened.value().id).value().dirty());

    REQUIRE(session.editDocument(versionValue(3)).ok());
    disk = store.read("content://World/Conflict.evscene");
    REQUIRE(store
                .compareAndSwap("content://World/Conflict.evscene", disk.value().revision, disk.value().contentHash,
                                versionValue(4))
                .ok());
    const auto conflict = session.pollDocumentChanges();
    CHECK_EQ(static_cast<int>(conflict.code()), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(static_cast<int>(documents.snapshot(opened.value().id).value().state),
             static_cast<int>(DocumentState::Conflict));
    CHECK(documents.content(opened.value().id).value() == versionValue(3));
}

// editor_simulation_preview.cpp
using namespace eve::editor;

namespace {
class FakeSimulation final : public IEditorSimulationBackend {
public:
    std::unique_ptr<IEditorSimulationBackend> cloneForPreview() const override {
        return std::make_unique<FakeSimulation>(*this);
    }
    Result<void> step(std::uint64_t tick, double fixedDelta) override {
        if (tick <= tick_)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("fake.tick"), "Ticks must increase");
        tick_ = tick;
        position_ += fixedDelta * 3.0;
        return eve::editing::applied<void>();
    }
    Result<std::vector<SimulationObjectSample>> capture() const override {
        SimulationObjectSample sample;
        sample.object    = "body-1";
        sample.positionX = position_;
        return eve::editing::applied<std::vector<SimulationObjectSample>>({sample});
    }
    double position() const { return position_; }

private:
    std::uint64_t tick_     = 0;
    double        position_ = 0.0;
};
}  // namespace

TEST_CASE("editor.simulation.preview_pauses_steps_and_bakes_without_mutating_source") {
    FakeSimulation              source;
    SimulationPreviewController controller;
    CHECK_EQ(static_cast<int>(controller.update(source).code()), static_cast<int>(EditorStatus::NoOp));
    REQUIRE(controller.setFixedDelta(0.5).ok());
    auto frame = controller.singleStep(source);
    REQUIRE(frame.ok());
    CHECK_EQ(frame.value().objects[0].positionX, 1.5);
    frame = controller.singleStep(source);
    REQUIRE(frame.ok());
    CHECK_EQ(frame.value().objects[0].positionX, 3.0);
    CHECK_EQ(source.position(), 0.0);
    auto baked = controller.bake(source, 42, 3, 10);
    CHECK_EQ(static_cast<int>(baked.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(baked.sourceRevision, 42U);
    CHECK_EQ(baked.frames.size(), 4U);
    CHECK_EQ(baked.frames.back().objects[0].positionX, 4.5);
    CHECK_EQ(source.position(), 0.0);
    CHECK_EQ(static_cast<int>(controller.bake(source, 42, 11, 10).status), static_cast<int>(EditorStatus::Rejected));
}

// editor_snow_target.cpp
using namespace eve::editor;
using namespace eve::level_editing;
using eve::lighting_editing::SnowFieldTarget;

TEST_CASE("editor.snow.live_scalar_target_supports_bilinear_preview_and_reversible_brush_edits") {
    eve::weather::SnowField field(2, 2);
    field.fill(1.0F);
    SnowFieldTarget        target("snow-depth", &field);
    ScalarFieldEditCommand command("footprint", &target);
    REQUIRE(command.record(0, 0, 0.0F));
    REQUIRE(command.record(1, 0, 0.5F));
    REQUIRE(command.apply());
    CHECK_EQ(field.height(0, 0), 0.0F);
    CHECK_EQ(target.sampleScalar(0.5F, 0.0F), 0.25F);
    CHECK(!target.dirtyRegion().empty());
    command.revert();
    CHECK_EQ(field.height(0, 0), 1.0F);
    CHECK_EQ(field.height(1, 0), 1.0F);
}
