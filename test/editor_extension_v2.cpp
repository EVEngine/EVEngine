#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include <string>
#include "audio/editing/AudioTarget.h"
#include "common/Module.h"
#include "editor/EditorAssetDatabase.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorExtension.h"
#include "editor/gizmo/editing/DomainGizmoPreview.h"
#include "editor/input/editing/InputMapTarget.h"
#include "graphics/lighting/editing/LightingTarget.h"
#include "graphics/material/editor/EditorMaterialStudio.h"
#include "physics/editing/PhysicsTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_extension_v2.cpp
using namespace eve::editor;

namespace {

class ParkBuilderExtension final : public IGameEditorExtension {
public:
    std::string ownerModule() const override { return "park"; }

    void registerEditor(IEditorExtensionRegistry& registry) override {
        CommandDescriptor append;
        append.id               = CommandId("park.track.append-segment");
        append.ownerModule      = "spoofed-owner";
        append.displayName      = "Append Track Segment";
        append.category         = "park.track";
        append.requiredFeatures = HostFeature::RuntimeWorld;
        CHECK(registry
                  .registerCommand(
                      append,
                      [&](const CommandContext&, const EditorValue& payload) {
                          ++calls;
                          return eve::editing::applied<EditorValue>(payload);
                      },
                      ExtensionAudience::Developer | ExtensionAudience::Player | ExtensionAudience::Automation)
                  .ok());

        ExtensionToolDescriptor tool;
        tool.id                 = ToolId("park.track.build");
        tool.labelKey           = "editor.park.track.build";
        tool.category           = "park.track";
        tool.targetRequirements = {CapabilityId("park.target.track-network")};
        tool.audiences          = ExtensionAudience::Developer | ExtensionAudience::Player;
        CHECK(registry.registerTool(std::move(tool)).ok());

        ExtensionPaletteDescriptor developer;
        developer.id        = StableId("park.level-design-palette");
        developer.titleKey  = "editor.palette.level.rides";
        developer.tools     = {ToolId("park.track.build")};
        developer.commands  = {CommandId("park.track.append-segment")};
        developer.audiences = ExtensionAudience::Developer;
        CHECK(registry.registerPalette(std::move(developer)).ok());

        ExtensionPaletteDescriptor player;
        player.id        = StableId("park.build-rides-palette");
        player.titleKey  = "game.build.rides";
        player.tools     = {ToolId("park.track.build")};
        player.commands  = {CommandId("park.track.append-segment")};
        player.audiences = ExtensionAudience::Player;
        CHECK(registry.registerPalette(std::move(player)).ok());

        ExtensionRuleDescriptor budget;
        budget.id        = RuleId("park.policy.build-cost");
        budget.audiences = ExtensionAudience::Player | ExtensionAudience::Automation;
        CHECK(registry.registerRule(std::move(budget)).ok());
    }

    int calls = 0;
};

class ThrowingExtension final : public IGameEditorExtension {
public:
    std::string ownerModule() const override { return "throwing"; }
    void        registerEditor(IEditorExtensionRegistry& registry) override {
        CommandDescriptor command;
        command.id = CommandId("throwing.partial");
        CHECK(registry
                  .registerCommand(
                      command,
                      [](const CommandContext&, const EditorValue&) {
                          return eve::editing::applied<EditorValue>(EditorValue{});
                      },
                      ExtensionAudience::Developer)
                  .ok());
        throw std::runtime_error("extension load failed");
    }
};

}  // namespace

TEST_CASE("editor.v2.game_extension_injects_same_tool_into_editor_and_game") {
    EditorCommandService    commands;
    EditorExtensionRegistry extensions(&commands);
    ParkBuilderExtension    park;
    CHECK(extensions.load(park).ok());

    HostProfile developer = HostProfile::developer();
    HostProfile runtime   = HostProfile::runtimeBuilder();
    extensions.configureProfile(runtime);
    CHECK(runtime.allowsCommand(CommandId("park.track.append-segment")));

    auto developerTools = extensions.tools(developer);
    auto runtimeTools   = extensions.tools(runtime);
    CHECK_EQ(developerTools.size(), static_cast<std::size_t>(1));
    CHECK_EQ(runtimeTools.size(), static_cast<std::size_t>(1));
    CHECK(developerTools.front().id == runtimeTools.front().id);
    CHECK_EQ(extensions.palettes(developer).front().id.value(), std::string("park.level-design-palette"));
    CHECK_EQ(extensions.palettes(runtime).front().id.value(), std::string("park.build-rides-palette"));
    CHECK_EQ(extensions.rules(developer).size(), static_cast<std::size_t>(0));
    CHECK_EQ(extensions.rules(runtime).size(), static_cast<std::size_t>(1));

    const CommandDescriptor* registered = commands.find(CommandId("park.track.append-segment"));
    CHECK(registered != nullptr);
    CHECK_EQ(registered->ownerModule, std::string("park"));

    CommandContext context;
    context.profile = &runtime;
    auto result     = commands.execute(CommandId("park.track.append-segment"), context, EditorValue("segment"));
    CHECK(result.ok());
    CHECK_EQ(park.calls, 1);

    CHECK(extensions.unload("park") > 0);
    CHECK(commands.find(CommandId("park.track.append-segment")) == nullptr);
    CHECK(extensions.tools(developer).empty());
}

TEST_CASE("editor.v2.extension_load_exception_rolls_back_partial_registration") {
    EditorCommandService    commands;
    EditorExtensionRegistry extensions(&commands);
    ThrowingExtension       throwing;
    auto                    result = extensions.load(throwing);
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(EditorStatus::Rejected));
    CHECK(commands.find(CommandId("throwing.partial")) == nullptr);
}

// editor_field_tool_script.cpp
TEST_CASE("editor.script.composes_heightmap_target_field_tool_and_transaction") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        heightmapTargets <- eve.HeightmapTargetModule();
        procgen <- eve.Procgen();
        heightmapResult <- procgen.newHeightmap(8, 8);
        heightmap <- heightmapResult.ok ? heightmapResult.value : null;
        target <- heightmapTargets.create("terrain", heightmap);
        falloff <- editor.newSmoothBrushFalloff();
        kernel <- editor.newCircleBrushKernel();
        kernel.setSmoothFalloff(falloff);
        operation <- editor.newAddScalarFieldOperation();
        tool <- editor.newFieldBrushTool("terrain.raise", "Raise Terrain");
        tool.setCircleKernel(kernel);
        tool.setAddScalarOperation(operation);
        tool.setRadius(1.5);
        tool.setStrength(0.25);
        session <- editor.newSession();
        added <- session.addFieldTool(tool);
        heightmapTargets.bind(session, target);
        activated <- session.activateTool("terrain.raise");
        down <- session.dispatchPointer(0, 1, 0, 3.0, 4.0, 0.0, 0.0, 1.0);
        up <- session.dispatchPointer(2, 1, 0, 3.0, 4.0, 0.0, 0.0, 1.0);
        raised <- target.readScalar(3, 4);
        revisionAfterStroke <- target.getRevision();
        undone <- session.undo();
        restored <- target.readScalar(3, 4);
        redone <- session.redo();
        replayed <- target.readScalar(3, 4);
        encoded <- heightmapTargets.encodeDocument(heightmap, 0.5, 0.5);
        target.writeScalar(3, 4, -2.0);
        decoded <- heightmapTargets.decodeDocument(encoded.value, heightmap);
        documentRestored <- target.readScalar(3, 4);
    )"));

    CHECK(vm.find("added").toBool());
    CHECK(vm.find("activated").toBool());
    CHECK((vm.find("down").toInt() & 2) != 0);
    CHECK((vm.find("up").toInt() & 4) != 0);
    CHECK_GT(vm.find("raised").toFloat(), 0.2f);
    CHECK_GT(vm.find("revisionAfterStroke").toInt(), 0);
    CHECK(vm.find("undone").toBool());
    CHECK_EQ(vm.find("restored").toFloat(), 0.f);
    CHECK(vm.find("redone").toBool());
    CHECK_GT(vm.find("replayed").toFloat(), 0.2f);
    CHECK(vm.find("encoded").toTable().find("ok").toBool());
    CHECK(vm.find("decoded").toTable().find("ok").toBool());
    CHECK_GT(vm.find("documentRestored").toFloat(), 0.2f);
}

// editor_gizmo_preview.cpp
using namespace eve::domain_gizmo_editing;
using namespace eve::editing;

namespace {
SelectionSnapshot selection(const char* target) {
    SelectionSnapshot result;
    result.items.push_back({SelectionDomain::Custom, TargetId(target), StableId("self"), "object"});
    return result;
}

void set(IPropertyProvider& provider, IDomainOperationTarget& target, const SelectionSnapshot& selected,
         const char* path, EditorValue value) {
    auto operation = provider.makeSet(selected, PropertyPath(path), value, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    CHECK(target.applyDomainOperation(operation.value()).ok());
}
}  // namespace

TEST_CASE("editor.gizmo.builds_physics_audio_and_light_overlays") {
    eve::domain_gizmo_editing::EditorGizmoPreviewBuilder builder;
    PhysicsColliderTarget                                collider("shape", 3);
    set(collider, collider, selection("shape"), "shape.kind", "capsule");
    set(collider, collider, selection("shape"), "shape.radius", 0.75);
    set(collider, collider, selection("shape"), "shape.capsule-height", 2.0);
    auto colliderGizmo = builder.collider(collider);
    CHECK_EQ(static_cast<int>(colliderGizmo.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE_EQ(colliderGizmo.primitives.size(), 1U);
    CHECK_EQ(colliderGizmo.primitives[0].kind, "wire-capsule");

    PhysicsJointTarget joint("hinge");
    set(joint, joint, selection("hinge"), "body.a", "body-a");
    set(joint, joint, selection("hinge"), "body.b", "body-b");
    auto jointGizmo = builder.joint(joint, [](const std::string& body) {
        return eve::editing::applied<std::array<double, 3>>(body == "body-a" ? std::array<double, 3>{0.0, 0.0, 0.0}
                                                                             : std::array<double, 3>{2.0, 0.0, 0.0});
    });
    CHECK_EQ(static_cast<int>(jointGizmo.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(jointGizmo.primitives.size(), 4U);
    CHECK_EQ(jointGizmo.primitives[2].length, 2.0);

    AudioSourceTarget audio("speaker");
    set(audio, audio, selection("speaker"), "spatial.reference-distance", 2.0);
    set(audio, audio, selection("speaker"), "spatial.maximum-distance", 12.0);
    auto audioGizmo = builder.audioSource(audio);
    CHECK_EQ(static_cast<int>(audioGizmo.status), static_cast<int>(EditorStatus::Applied));
    CHECK_EQ(audioGizmo.primitives.size(), 3U);
    CHECK_EQ(audioGizmo.primitives[1].radius, 12.0);

    Light3DDocumentTarget light("sun");
    set(light, light, selection("sun"), "light.type", "dir");
    auto lightGizmo = builder.light(light);
    CHECK_EQ(static_cast<int>(lightGizmo.status), static_cast<int>(EditorStatus::Applied));
    REQUIRE_EQ(lightGizmo.primitives.size(), 1U);
    CHECK_EQ(lightGizmo.primitives[0].kind, "arrow");
}

// editor_import_coordinator.cpp
using namespace eve::editor;

namespace {
ImportProduct product(const ImportTicket& ticket) {
    ImportProduct result;
    result.record.guid            = ticket.asset;
    result.record.logicalUri      = "asset://animations/walk";
    result.record.typeId          = "animation-clip";
    result.record.sourceUri       = "source://walk.fbx";
    result.record.sourceHash      = ticket.sourceHash;
    result.record.importerId      = ticket.importerId;
    result.record.importerVersion = ticket.importerVersion;
    result.record.artifacts       = {"cache://walk.eva"};
    return result;
}
}  // namespace

TEST_CASE("editor.import.generation_tickets_reject_stale_or_incomplete_worker_products") {
    MemoryAssetDatabase database;
    ImportCoordinator   coordinator(&database);
    auto                first  = coordinator.begin(AssetGuid("walk"), "hash-a", "fbx-animation", 2);
    auto                latest = coordinator.begin(AssetGuid("walk"), "hash-b", "fbx-animation", 2);
    REQUIRE(first.ok());
    REQUIRE(latest.ok());
    CHECK_EQ(static_cast<int>(coordinator.publish(first.value(), product(first.value())).code()),
             static_cast<int>(EditorStatus::Conflict));
    ImportProduct incomplete = product(latest.value());
    incomplete.record.artifacts.clear();
    CHECK_EQ(static_cast<int>(coordinator.publish(latest.value(), std::move(incomplete)).code()),
             static_cast<int>(EditorStatus::Rejected));
    auto published = coordinator.publish(latest.value(), product(latest.value()));
    REQUIRE(published.ok());
    CHECK_EQ(published.value().sourceHash, "hash-b");
    REQUIRE(database.find(AssetGuid("walk")).ok());
}

// editor_input_map_target.cpp
using namespace eve::input_editing;
namespace {
void apply(InputMapTarget& t, Result<DomainOperation> op) {
    REQUIRE(op.ok());
    REQUIRE(t.applyDomainOperation(op.value()).ok());
}
}  // namespace
TEST_CASE("editor.input_map.evaluates_composite_axis_deadzone_and_buttons") {
    InputMapTarget target("game.input");
    apply(target, target.makeCreateAction({ObjectId("move"), "move-x", "axis1d"}));
    apply(target, target.makeCreateAction({ObjectId("jump"), "jump", "button"}));
    apply(target, target.makeCreateBinding({ObjectId("left"), ObjectId("move"), "keyboard", "A", -1, 0, false}));
    apply(target, target.makeCreateBinding({ObjectId("right"), ObjectId("move"), "keyboard", "D", 1, 0, false}));
    apply(target, target.makeCreateBinding({ObjectId("stick"), ObjectId("move"), "gamepad", "leftx", 1, .2, false}));
    apply(target, target.makeCreateBinding({ObjectId("space"), ObjectId("jump"), "keyboard", "Space", 1, 0, false}));
    InputMapEvaluator evaluator;
    auto              result =
        evaluator.evaluate(target, {{"keyboard", "D", 1}, {"gamepad", "leftx", .6}, {"keyboard", "Space", 1}});
    REQUIRE(result.ok());
    CHECK_EQ(result.value().at("move-x"), 1.0);
    CHECK_EQ(result.value().at("jump"), 1.0);
}
TEST_CASE("editor.input_map.rejects_dangling_delete_and_captures_intentional_input") {
    InputMapTarget target("game.input");
    apply(target, target.makeCreateAction({ObjectId("jump"), "jump", "button"}));
    apply(target, target.makeCreateBinding({ObjectId("space"), ObjectId("jump"), "keyboard", "Space", 1, 0, false}));
    CHECK_EQ(static_cast<int>(target.makeDeleteAction(ObjectId("jump")).code()),
             static_cast<int>(EditorStatus::Rejected));
    InputBindingCapture capture;
    capture.begin("gamepad");
    CHECK(!capture.feed({"keyboard", "A", 1}));
    CHECK(!capture.feed({"gamepad", "leftx", .2}));
    auto captured = capture.feed({"gamepad", "leftx", .8});
    REQUIRE(captured);
    CHECK_EQ(captured->control, std::string("leftx"));
    CHECK(!capture.active());
}
TEST_CASE("editor.input_map.snapshot_load_is_atomic") {
    InputMapTarget target("game.input");
    apply(target, target.makeCreateAction({ObjectId("jump"), "jump", "button"}));
    const auto  before  = target.snapshotValue();
    EditorValue invalid = before;
    auto*       root    = invalid.getIf<EditorValue::Object>();
    auto*       content = (*root)["content"].getIf<EditorValue::Object>();
    auto*       actions = (*content)["actions"].getIf<EditorValue::Array>();
    auto*       first   = (*actions)[0].getIf<EditorValue::Object>();
    (*first)["kind"]    = "vector3";
    CHECK_EQ(static_cast<int>(target.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.snapshotValue(), before);
}

// editor_material_studio.cpp
using namespace eve::editor;

namespace {

class RecordingMaterialSink final : public IMaterialRuntimeSink {
public:
    Result<void> publish(const MaterialDocumentTarget& candidate) override {
        if (reject)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("test.material.reload-rejected"),
                                              "Runtime rejected reloaded material");
        ++publications;
        last = candidate.snapshotValue();
        return eve::editing::applied<void>();
    }

    int         publications = 0;
    bool        reject       = false;
    EditorValue last;
};

class RecordingStudioRenderer final : public IMaterialPreviewRenderer {
public:
    MaterialPreviewRenderResult render(const MaterialPreviewRenderRequest& request) override {
        ++renders;
        revisions.push_back(request.documentRevision);
        snapshots.push_back(request.material);
        return {EditorStatus::Applied, "artifact://material-studio/" + std::to_string(renders), {}};
    }

    int                      renders = 0;
    std::vector<Revision>    revisions;
    std::vector<EditorValue> snapshots;
};

SelectionSnapshot materialSelection(const MaterialDocumentTarget& material) {
    SelectionSnapshot selection;
    selection.channel = "asset";
    SelectionItem item{SelectionDomain::Asset, TargetId(material.targetId()), StableId(material.targetId().value()),
                       "graphics.material"};
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

}  // namespace

TEST_CASE("editor.material.studio_coalesces_live_gesture_into_one_transaction") {
    RecordingMaterialSink    sink;
    MaterialPublishingTarget target("studio-metal", &sink);
    LocalWorldAuthority      authority(&target);
    LocalTransactionBackend  transactions(&authority);
    MaterialPreviewService   previews;
    RecordingStudioRenderer  renderer;
    MaterialStudioController studio(DocumentId("material.studio-metal"), target, transactions, previews, renderer);

    REQUIRE(studio.beginInteraction(PropertyPath("shading.roughness")).ok());
    REQUIRE(studio.updateInteraction(0.2).ok());
    REQUIRE(studio.tick(100).ok());
    REQUIRE(studio.updateInteraction(0.35).ok());
    REQUIRE(studio.tick(133).ok());
    REQUIRE(studio.updateInteraction(0.7).ok());
    REQUIRE(studio.tick(166).ok());

    CHECK_EQ(renderer.renders, 3);
    CHECK_EQ(sink.publications, 0);
    CHECK(studio.state().interactionActive);
    const Revision before    = target.revision();
    auto           committed = studio.commitInteraction();
    REQUIRE(committed.ok());
    CHECK_EQ(target.revision(), before + 1);
    CHECK_EQ(sink.publications, 1);
    CHECK(!studio.state().interactionActive);
    CHECK(transactions.canUndo());
    CHECK(target.authoringTarget()
              .read(materialSelection(target.authoringTarget()), PropertyPath("shading.roughness"))
              .value == EditorValue(0.7));

    REQUIRE(transactions.undo().ok());
    CHECK(target.authoringTarget()
              .read(materialSelection(target.authoringTarget()), PropertyPath("shading.roughness"))
              .value == EditorValue(0.45));
}

TEST_CASE("editor.material.studio_cancel_preserves_live_material_and_refreshes_preview") {
    RecordingMaterialSink    sink;
    MaterialPublishingTarget target("studio-cancel", &sink);
    LocalWorldAuthority      authority(&target);
    LocalTransactionBackend  transactions(&authority);
    MaterialPreviewService   previews;
    RecordingStudioRenderer  renderer;
    MaterialStudioController studio(DocumentId("material.studio-cancel"), target, transactions, previews, renderer);

    const Revision original = target.revision();
    REQUIRE(studio.beginInteraction(PropertyPath("shading.metallic")).ok());
    REQUIRE(studio.updateInteraction(0.9).ok());
    REQUIRE(studio.refreshPreview().ok());
    REQUIRE(studio.cancelInteraction().ok());
    REQUIRE(studio.refreshPreview().ok());
    CHECK_EQ(target.revision(), original);
    CHECK_EQ(sink.publications, 0);
    CHECK_EQ(renderer.renders, 2);
    CHECK_EQ(studio.state().previewRevision, original);
}

TEST_CASE("editor.material.hot_reload_publishes_candidate_atomically") {
    RecordingMaterialSink    sink;
    MaterialPublishingTarget target("hot-leaf", &sink);
    MaterialDocumentTarget   source("hot-leaf");
    const SelectionSnapshot  selection = materialSelection(source);
    auto                     operation =
        source.makeSet(selection, PropertyPath("vegetation.alpha.glancing"), 0.65, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    REQUIRE(source.applyDomainOperation(operation.value()).ok());

    const Revision before = target.revision();
    sink.reject           = true;
    REQUIRE(!target.reloadSnapshot(source.snapshotValue()).ok());
    CHECK_EQ(target.revision(), before);
    CHECK(target.authoringTarget()
              .read(materialSelection(target.authoringTarget()), PropertyPath("vegetation.alpha.glancing"))
              .value == EditorValue(0.0));

    sink.reject = false;
    REQUIRE(target.reloadSnapshot(source.snapshotValue()).ok());
    CHECK_EQ(target.revision(), before + 1);
    CHECK_EQ(sink.publications, 1);
    CHECK(target.authoringTarget()
              .read(materialSelection(target.authoringTarget()), PropertyPath("vegetation.alpha.glancing"))
              .value == EditorValue(0.65));
}

TEST_CASE("editor.material.studio_rate_limits_with_injected_monotonic_time") {
    RecordingMaterialSink    sink;
    MaterialPublishingTarget target("studio-rate", &sink);
    LocalWorldAuthority      authority(&target);
    LocalTransactionBackend  transactions(&authority);
    MaterialPreviewService   previews;
    RecordingStudioRenderer  renderer;
    MaterialStudioController studio(DocumentId("material.studio-rate"), target, transactions, previews, renderer);

    REQUIRE(studio.setPreviewRate(20.0).ok());
    REQUIRE(studio.beginInteraction(PropertyPath("shading.roughness")).ok());
    REQUIRE(studio.updateInteraction(0.6).ok());
    REQUIRE(studio.tick(1000).ok());
    REQUIRE(studio.updateInteraction(0.61).ok());
    CHECK_EQ(static_cast<int>(studio.tick(1020).code()), static_cast<int>(EditorStatus::NoOp));
    REQUIRE(studio.tick(1050).ok());
    CHECK_EQ(renderer.renders, 2);
    REQUIRE(studio.updateInteraction(0.62).ok());
    CHECK_EQ(static_cast<int>(studio.tick(1049).code()), static_cast<int>(EditorStatus::Rejected));
}
