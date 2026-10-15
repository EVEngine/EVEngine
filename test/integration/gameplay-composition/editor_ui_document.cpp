#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "common/Module.h"
#include "editor/EditorAuthority.h"
#include "editor/EditorIds.h"
#include "editor/EditorTransactionService.h"
#include "ui/Theme.h"
#include "ui/editing/UiDocument.h"
#include "ui/editing/UiTheme.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// editor_ui_document.cpp
using namespace eve::ui_editing;
using namespace eve::editing;
using eve::editor::LocalTransactionBackend;

TEST_CASE("editor.ui.style_inspector_preview_pick_and_anchor_gizmo_are_revision_safe") {
    UiDocumentTarget document("hud");
    UiLayoutValue    rootLayout;
    rootLayout.width  = 800.0;
    rootLayout.height = 600.0;
    auto root         = document.makeCreate({ObjectId("root"), ObjectId(), "window", "Root", rootLayout});
    REQUIRE(root.ok());
    CHECK(document.applyDomainOperation(root.value()).ok());
    UiLayoutValue childLayout;
    childLayout.x      = 20.0;
    childLayout.y      = 30.0;
    childLayout.width  = 100.0;
    childLayout.height = 40.0;
    auto child         = document.makeCreate({ObjectId("play"), ObjectId("root"), "button", "Play", childLayout});
    REQUIRE(child.ok());
    CHECK(document.applyDomainOperation(child.value()).ok());
    UiStyleValue style;
    style.paddingLeft  = 10.0;
    style.paddingTop   = 5.0;
    style.tintR        = 0.25;
    style.cornerRadius = 6.0;
    style.direction    = "column";
    auto styled        = document.makeSetStyle(ObjectId("play"), style);
    REQUIRE(styled.ok());
    CHECK(document.applyDomainOperation(styled.value()).ok());

    UiDocumentPreviewService previewService;
    const auto               preview = previewService.build(document, 800.0, 600.0);
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    auto picked = previewService.pick(preview, 25.0, 35.0);
    REQUIRE(picked.ok());
    CHECK_EQ(picked.value().value(), "play");
    auto gizmo = previewService.anchorGizmo(document, preview, ObjectId("play"));
    CHECK(gizmo.ok());
    auto rename = document.makeRename(ObjectId("play"), "Play now");
    REQUIRE(rename.ok());
    CHECK(document.applyDomainOperation(rename.value()).ok());
    CHECK_EQ(static_cast<int>(previewService.anchorGizmo(document, preview, ObjectId("play")).code()),
             static_cast<int>(EditorStatus::Conflict));
}

namespace {

Result<TransactionReceipt> commitUi(UiDocumentTarget& target, LocalTransactionBackend& transactions,
                                    const DomainOperation& operation, const std::string& id) {
    TransactionSpec specification;
    specification.id           = TransactionId(id);
    specification.label        = "Edit UI";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<TransactionReceipt>(begun.code(), RuleId("test.ui.begin"),
                                                        "Could not begin UI transaction");
    auto appended = transactions.append(operation);
    if (!appended.ok()) {
        [[maybe_unused]] const auto rolledBack = transactions.rollback();
        return eve::editing::failed<TransactionReceipt>(appended.code(), RuleId("test.ui.append"),
                                                        "Could not append UI operation");
    }
    return transactions.commit();
}

SelectionSnapshot uiSelection(const UiDocumentTarget& target, std::initializer_list<const char*> ids) {
    SelectionSnapshot selection;
    selection.channel = "ui";
    for (const char* id : ids) {
        SelectionItem item;
        item.domain = SelectionDomain::UI;
        item.target = TargetId(target.targetId());
        item.item   = StableId(id);
        item.type   = "ui.widget";
        selection.items.push_back(item);
    }
    if (!selection.items.empty()) selection.primary = selection.items.front();
    return selection;
}

}  // namespace

TEST_CASE("editor.ui.document_hierarchy_operations_are_reversible_and_cycle_safe") {
    UiDocumentTarget        target("hud");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    for (const CreateUiWidgetRequest& request :
         {CreateUiWidgetRequest{ObjectId("root"), {}, "column", "Root", {0, 0, 1280, 720}},
          CreateUiWidgetRequest{ObjectId("panel"), ObjectId("root"), "panel", "Panel", {10, 20, 300, 200}},
          CreateUiWidgetRequest{ObjectId("label"), ObjectId("panel"), "text", "Label", {4, 4, 100, 24}}}) {
        auto operation = target.makeCreate(request);
        REQUIRE(operation.ok());
        REQUIRE(commitUi(target, transactions, operation.value(), "ui.create." + request.id.value()).ok());
    }
    CHECK_EQ(target.children(ObjectId("panel")).size(), static_cast<std::size_t>(1));
    CHECK_EQ(static_cast<int>(target.makeDelete(ObjectId("panel")).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(target.makeReparent(ObjectId("root"), ObjectId("label")).code()),
             static_cast<int>(EditorStatus::Rejected));

    auto rename = target.makeRename(ObjectId("label"), "Title");
    REQUIRE(rename.ok());
    REQUIRE(commitUi(target, transactions, rename.value(), "ui.rename").ok());
    CHECK_EQ(target.widget(ObjectId("label")).value().name, std::string("Title"));
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(target.widget(ObjectId("label")).value().name, std::string("Label"));
    REQUIRE(transactions.redo().ok());

    auto reparent = target.makeReparent(ObjectId("label"), ObjectId("root"));
    REQUIRE(reparent.ok());
    REQUIRE(commitUi(target, transactions, reparent.value(), "ui.reparent").ok());
    CHECK(target.widget(ObjectId("label")).value().parent == ObjectId("root"));
    auto remove = target.makeDelete(ObjectId("label"));
    REQUIRE(remove.ok());
    REQUIRE(commitUi(target, transactions, remove.value(), "ui.delete").ok());
    CHECK_EQ(static_cast<int>(target.widget(ObjectId("label")).code()), static_cast<int>(EditorStatus::NotFound));
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(target.widget(ObjectId("label")).value().name, std::string("Title"));
}

TEST_CASE("editor.ui.multi_selection_layout_inspector_is_atomic_and_reports_mixed_values") {
    UiDocumentTarget        target("hud");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    for (const char* id : {"left", "right"}) {
        CreateUiWidgetRequest request{ObjectId(id), {}, "button", id, {}};
        auto                  operation = target.makeCreate(request);
        REQUIRE(operation.ok());
        REQUIRE(commitUi(target, transactions, operation.value(), std::string("ui.create.") + id).ok());
    }
    const SelectionSnapshot selection = uiSelection(target, {"left", "right"});
    CHECK_EQ(target.schema(selection).properties.size(), static_cast<std::size_t>(25));
    auto size = target.makeSet(selection, PropertyPath("layout.size"), EditorValue::Array{180.0, 42.0},
                               PropertySetMode::Absolute);
    REQUIRE(size.ok());
    REQUIRE(commitUi(target, transactions, size.value(), "ui.multi-size").ok());
    CHECK_EQ(target.widget(ObjectId("left")).value().layout.width, 180.0);
    CHECK_EQ(target.widget(ObjectId("right")).value().layout.height, 42.0);
    REQUIRE(transactions.undo().ok());
    CHECK_EQ(target.widget(ObjectId("left")).value().layout.width, 0.0);

    UiLayoutValue moved;
    moved.x   = 15.0;
    auto move = target.makeSetLayout(ObjectId("left"), moved);
    REQUIRE(move.ok());
    REQUIRE(commitUi(target, transactions, move.value(), "ui.move-one").ok());
    CHECK_EQ(static_cast<int>(target.read(selection, PropertyPath("layout.position")).state),
             static_cast<int>(PropertyReadState::Mixed));
    CHECK_EQ(static_cast<int>(target
                                  .makeSet(selection, PropertyPath("layout.anchor"), EditorValue::Array{1.5, 0.0},
                                           PropertySetMode::Absolute)
                                  .code()),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.ui.snapshot_load_is_atomic_and_rejects_invalid_hierarchies") {
    UiDocumentTarget source("source");
    auto             root = source.makeCreate({ObjectId("root"), {}, "column", "Root", {0, 0, 640, 480}});
    REQUIRE(root.ok());
    REQUIRE(source.applyDomainOperation(root.value()).ok());
    auto child = source.makeCreate({ObjectId("child"), ObjectId("root"), "text", "Child", {8, 8, 100, 20}});
    REQUIRE(child.ok());
    REQUIRE(source.applyDomainOperation(child.value()).ok());

    UiDocumentTarget restored("restored");
    REQUIRE(restored.loadSnapshot(source.snapshotValue()).ok());
    CHECK_EQ(restored.children(ObjectId("root")).size(), static_cast<std::size_t>(1));

    EditorValue::Object invalidWidget;
    invalidWidget["id"]      = "orphan";
    invalidWidget["parent"]  = "missing";
    invalidWidget["type"]    = "text";
    invalidWidget["name"]    = "Orphan";
    invalidWidget["text"]    = "";
    invalidWidget["visible"] = true;
    invalidWidget["enabled"] = true;
    invalidWidget["layout"] = EditorValue::Object{{"x", 0.0},       {"y", 0.0},       {"width", 10.0}, {"height", 10.0},
                                                  {"anchorX", 0.0}, {"anchorY", 0.0}, {"pivotX", 0.0}, {"pivotY", 0.0}};
    EditorValue::Object invalidRoot;
    invalidRoot["schemaVersion"] = 1;
    invalidRoot["widgets"]       = EditorValue::Array{EditorValue(std::move(invalidWidget))};
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(EditorValue(std::move(invalidRoot))).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK(restored.widget(ObjectId("child")).ok());
}

// editor_ui_theme.cpp
using namespace eve::ui_editing;
using namespace eve::editing;
using eve::editor::LocalTransactionBackend;
using eve::editor::LocalWorldAuthority;

namespace {

Result<eve::editor::TransactionReceipt> commitTheme(UiThemeCatalogTarget& target, LocalTransactionBackend& transactions,
                                                    const DomainOperation& operation, const std::string& id) {
    eve::editor::TransactionSpec specification;
    specification.id           = eve::editor::TransactionId(id);
    specification.label        = "Edit theme";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun                 = transactions.begin(std::move(specification));
    if (!begun.ok())
        return eve::editing::failed<eve::editor::TransactionReceipt>(begun.code(), RuleId("test.ui-theme.begin"),
                                                                     "Could not begin theme transaction");
    auto appended = transactions.append(operation);
    if (!appended.ok()) {
        auto discarded = transactions.discard();
        if (!discarded.ok()) discarded.ignore("theme test transaction already inactive");
        return eve::editing::failed<eve::editor::TransactionReceipt>(appended.code(), RuleId("test.ui-theme.append"),
                                                                     "Could not append theme operation");
    }
    return transactions.commit();
}

SelectionSnapshot themeSelection(const UiThemeCatalogTarget& target, const char* id) {
    SelectionSnapshot selection;
    selection.channel = "ui-theme";
    SelectionItem item;
    item.domain = SelectionDomain::Asset;
    item.target = TargetId(target.targetId());
    item.item   = StableId(id);
    item.type   = "ui.theme";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

}  // namespace

TEST_CASE("editor.ui.theme_catalog_duplicate_recolor_and_undo_are_revision_safe") {
    UiThemeCatalogTarget    target("styles");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    CHECK_EQ(target.themes().size(), static_cast<std::size_t>(2));

    auto duplicated = target.makeDuplicate(ObjectId("dark"), ObjectId("studio"), "Studio");
    REQUIRE(duplicated.ok());
    REQUIRE(commitTheme(target, transactions, duplicated.value(), "theme.duplicate").ok());
    CHECK_EQ(target.themes().size(), static_cast<std::size_t>(3));

    const SelectionSnapshot selection = themeSelection(target, "studio");
    auto recolor = target.makeSet(selection, PropertyPath("color.button"), EditorValue::Array{0.1, 0.2, 0.3, 1.0},
                                  PropertySetMode::Absolute);
    REQUIRE(recolor.ok());
    REQUIRE(commitTheme(target, transactions, recolor.value(), "theme.recolor").ok());
    auto studio = target.theme(ObjectId("studio"));
    REQUIRE(studio.ok());
    CHECK(std::abs(studio.value().tokens.button[0] - 0.1f) < 0.0001f);
    REQUIRE(transactions.undo().ok());
    studio = target.theme(ObjectId("studio"));
    REQUIRE(studio.ok());
    CHECK(studio.value().tokens.button[0] > 0.15f);
}

TEST_CASE("editor.ui.theme_catalog_rejects_deleting_the_last_asset_and_loads_snapshots_atomically") {
    UiThemeCatalogTarget    target("styles");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend transactions(&authority);
    auto                    removeLight = target.makeDelete(ObjectId("light"));
    REQUIRE(removeLight.ok());
    REQUIRE(commitTheme(target, transactions, removeLight.value(), "theme.delete-light").ok());
    CHECK_EQ(static_cast<int>(target.makeDelete(ObjectId("dark")).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.themes().size(), static_cast<std::size_t>(1));

    UiThemeCatalogTarget    source("source");
    LocalWorldAuthority     sourceAuthority(&source);
    LocalTransactionBackend sourceTx(&sourceAuthority);
    auto                    copied = source.makeDuplicate(ObjectId("dark"), ObjectId("copy"), "Copy");
    REQUIRE(copied.ok());
    REQUIRE(commitTheme(source, sourceTx, copied.value(), "theme.copy").ok());

    UiThemeCatalogTarget restored("restored");
    REQUIRE(restored.loadSnapshot(source.snapshotValue()).ok());
    CHECK_EQ(restored.themes().size(), source.themes().size());

    EditorValue::Object invalid;
    invalid["schemaVersion"] = int64_t{1};
    invalid["content"] = EditorValue::Object{{"activeId", std::string("missing")}, {"themes", EditorValue::Array{}}};
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(EditorValue(std::move(invalid))).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(restored.themes().size(), source.themes().size());
}

TEST_CASE("editor.ui.theme_preview_rejects_stale_revision_and_publish_updates_global_theme") {
    UiThemeCatalogTarget  target("styles");
    UiThemePreviewService previews;
    const auto            current = previews.build(target, ObjectId("dark"), target.revision());
    CHECK_EQ(static_cast<int>(current.status), static_cast<int>(EditorStatus::Applied));
    const auto stale = previews.build(target, ObjectId("dark"), target.revision() + 1);
    CHECK_EQ(static_cast<int>(stale.status), static_cast<int>(EditorStatus::Conflict));

    const eve::ui::Theme previous     = eve::ui::globalTheme();
    const std::string    previousName = eve::ui::globalThemeName();
    REQUIRE(target.applyDomainOperation(target.makeSetActive(ObjectId("light")).value()).ok());
    REQUIRE(UiThemeRuntimePublisher().publish(target).ok());
    CHECK_EQ(eve::ui::globalThemeName(), std::string("light"));
    eve::ui::setGlobalTheme(previous, previousName);
}

// editor_ui_theme_script.cpp
TEST_CASE("editor.ui.theme_editor_script_composes_workspace_and_transactions") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        workspace <- editor.newWorkspace("test.ui-theme", "Test Theme Editor");
        created <- eve.UiEditorModule().create("test.asset.themes");
        themeEditor <- created.value;
        configured <- themeEditor.configureWorkspace(workspace);
        workspacePanelCount <- workspace.getPanelCount();
        panel0 <- workspace.getPanelId(0);
        cap0 <- workspace.getPanelCapability(0);
        ctx1 <- workspace.getPanelContext(1);
        createdCustom <- themeEditor.createFromPreset("studio", "Studio", "dark");
        beforeRounding <- themeEditor.getFloat("geometry.frameRounding");
        roundingResult <- themeEditor.setFloat("geometry.frameRounding", 9.0);
        afterRounding <- themeEditor.getFloat("geometry.frameRounding");
        undoResult <- themeEditor.undo();
        restoredRounding <- themeEditor.getFloat("geometry.frameRounding");
        themeCount <- themeEditor.getThemeCount();
        selected <- themeEditor.getSelectedId();
        state <- themeEditor.getState();
        stateSelected <- state.value.selectedId;
        stateCount <- state.value.themeCount;
    )"));

    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("workspacePanelCount").toInt(), 3);
    CHECK_EQ(vm.find("panel0").toString(), std::string("ui.themes"));
    CHECK_EQ(vm.find("cap0").toString(), std::string("ui.theme"));
    CHECK_EQ(vm.find("ctx1").toString(), std::string("preview"));
    CHECK(vm.find("createdCustom").toTable().get<bool>("ok"));
    CHECK(vm.find("roundingResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("afterRounding").toFloat() - 9.0F), 0.0001F);
    CHECK(vm.find("undoResult").toTable().get<bool>("ok"));
    CHECK_LT(std::abs(vm.find("restoredRounding").toFloat() - vm.find("beforeRounding").toFloat()), 0.0001F);
    CHECK_EQ(vm.find("themeCount").toInt(), 3);
    CHECK_EQ(vm.find("selected").toString(), std::string("studio"));
    CHECK(vm.find("state").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("stateSelected").toString(), std::string("studio"));
    CHECK_EQ(vm.find("stateCount").toInt(), 3);
}
