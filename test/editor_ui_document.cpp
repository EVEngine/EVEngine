#include "editor/EditorAuthority.h"
#include "editor/EditorTransactionService.h"
#include "ui_editing/UiDocument.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::ui_editing;
using namespace eve::editing;
using eve::editor::LocalTransactionBackend;

TEST_CASE("editor.ui.style_inspector_preview_pick_and_anchor_gizmo_are_revision_safe") {
    UiDocumentTarget document("hud");
    UiLayoutValue rootLayout; rootLayout.width = 800.0; rootLayout.height = 600.0;
    auto root = document.makeCreate({ObjectId("root"), ObjectId(), "window", "Root", rootLayout});
    REQUIRE(root.value); CHECK(document.applyDomainOperation(*root.value).isAccepted());
    UiLayoutValue childLayout; childLayout.x = 20.0; childLayout.y = 30.0;
    childLayout.width = 100.0; childLayout.height = 40.0;
    auto child = document.makeCreate({ObjectId("play"), ObjectId("root"), "button", "Play", childLayout});
    REQUIRE(child.value); CHECK(document.applyDomainOperation(*child.value).isAccepted());
    UiStyleValue style; style.paddingLeft = 10.0; style.paddingTop = 5.0;
    style.tintR = 0.25; style.cornerRadius = 6.0; style.direction = "column";
    auto styled = document.makeSetStyle(ObjectId("play"), style);
    REQUIRE(styled.value); CHECK(document.applyDomainOperation(*styled.value).isAccepted());

    UiDocumentPreviewService previewService;
    const auto preview = previewService.build(document, 800.0, 600.0);
    CHECK_EQ(static_cast<int>(preview.status), static_cast<int>(EditorStatus::Applied));
    auto picked = previewService.pick(preview, 25.0, 35.0);
    REQUIRE(picked.value); CHECK_EQ(picked.value->value(), "play");
    auto gizmo = previewService.anchorGizmo(document, preview, ObjectId("play"));
    CHECK(gizmo.isAccepted());
    auto rename = document.makeRename(ObjectId("play"), "Play now"); REQUIRE(rename.value);
    CHECK(document.applyDomainOperation(*rename.value).isAccepted());
    CHECK_EQ(static_cast<int>(previewService.anchorGizmo(document, preview, ObjectId("play")).status),
             static_cast<int>(EditorStatus::Conflict));
}

namespace {

EditorResult<TransactionReceipt> commitUi(UiDocumentTarget& target,
                                          LocalTransactionBackend& transactions,
                                          const DomainOperation& operation,
                                          const std::string& id) {
    TransactionSpec specification;
    specification.id = TransactionId(id);
    specification.label = "Edit UI";
    specification.target = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun = transactions.begin(std::move(specification));
    if (!begun.isAccepted())
        return EditorResult<TransactionReceipt>::error(begun.status, RuleId("test.ui.begin"),
                                                       "Could not begin UI transaction");
    auto appended = transactions.append(operation);
    if (!appended.isAccepted()) {
        [[maybe_unused]] const auto rolledBack = transactions.rollback();
        return EditorResult<TransactionReceipt>::error(appended.status, RuleId("test.ui.append"),
                                                       "Could not append UI operation");
    }
    return transactions.commit();
}

SelectionSnapshot uiSelection(const UiDocumentTarget& target,
                              std::initializer_list<const char*> ids) {
    SelectionSnapshot selection;
    selection.channel = "ui";
    for (const char* id : ids) {
        SelectionItem item;
        item.domain = SelectionDomain::UI;
        item.target = TargetId(target.targetId());
        item.item = StableId(id);
        item.type = "ui.widget";
        selection.items.push_back(item);
    }
    if (!selection.items.empty()) selection.primary = selection.items.front();
    return selection;
}

}  // namespace

TEST_CASE("editor.ui.document_hierarchy_operations_are_reversible_and_cycle_safe") {
    UiDocumentTarget target("hud");
    LocalWorldAuthority authority(&target);
    LocalTransactionBackend transactions(&authority);
    for (const CreateUiWidgetRequest& request : {
             CreateUiWidgetRequest{ObjectId("root"), {}, "column", "Root", {0, 0, 1280, 720}},
             CreateUiWidgetRequest{ObjectId("panel"), ObjectId("root"), "panel", "Panel", {10, 20, 300, 200}},
             CreateUiWidgetRequest{ObjectId("label"), ObjectId("panel"), "text", "Label", {4, 4, 100, 24}}}) {
        auto operation = target.makeCreate(request);
        REQUIRE(operation.isAccepted());
        REQUIRE(commitUi(target, transactions, *operation.value, "ui.create." + request.id.value()).isAccepted());
    }
    CHECK_EQ(target.children(ObjectId("panel")).size(), static_cast<std::size_t>(1));
    CHECK_EQ(static_cast<int>(target.makeDelete(ObjectId("panel")).status),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(static_cast<int>(target.makeReparent(ObjectId("root"), ObjectId("label")).status),
             static_cast<int>(EditorStatus::Rejected));

    auto rename = target.makeRename(ObjectId("label"), "Title");
    REQUIRE(rename.isAccepted());
    REQUIRE(commitUi(target, transactions, *rename.value, "ui.rename").isAccepted());
    CHECK_EQ(target.widget(ObjectId("label")).value->name, std::string("Title"));
    REQUIRE(transactions.undo().isAccepted());
    CHECK_EQ(target.widget(ObjectId("label")).value->name, std::string("Label"));
    REQUIRE(transactions.redo().isAccepted());

    auto reparent = target.makeReparent(ObjectId("label"), ObjectId("root"));
    REQUIRE(reparent.isAccepted());
    REQUIRE(commitUi(target, transactions, *reparent.value, "ui.reparent").isAccepted());
    CHECK(target.widget(ObjectId("label")).value->parent == ObjectId("root"));
    auto remove = target.makeDelete(ObjectId("label"));
    REQUIRE(remove.isAccepted());
    REQUIRE(commitUi(target, transactions, *remove.value, "ui.delete").isAccepted());
    CHECK_EQ(static_cast<int>(target.widget(ObjectId("label")).status),
             static_cast<int>(EditorStatus::NotFound));
    REQUIRE(transactions.undo().isAccepted());
    CHECK_EQ(target.widget(ObjectId("label")).value->name, std::string("Title"));
}

TEST_CASE("editor.ui.multi_selection_layout_inspector_is_atomic_and_reports_mixed_values") {
    UiDocumentTarget target("hud");
    LocalWorldAuthority authority(&target);
    LocalTransactionBackend transactions(&authority);
    for (const char* id : {"left", "right"}) {
        CreateUiWidgetRequest request{ObjectId(id), {}, "button", id, {}};
        auto operation = target.makeCreate(request);
        REQUIRE(operation.isAccepted());
        REQUIRE(commitUi(target, transactions, *operation.value, std::string("ui.create.") + id).isAccepted());
    }
    const SelectionSnapshot selection = uiSelection(target, {"left", "right"});
    CHECK_EQ(target.schema(selection).properties.size(), static_cast<std::size_t>(25));
    auto size = target.makeSet(selection, PropertyPath("layout.size"),
                               EditorValue::Array{180.0, 42.0}, PropertySetMode::Absolute);
    REQUIRE(size.isAccepted());
    REQUIRE(commitUi(target, transactions, *size.value, "ui.multi-size").isAccepted());
    CHECK_EQ(target.widget(ObjectId("left")).value->layout.width, 180.0);
    CHECK_EQ(target.widget(ObjectId("right")).value->layout.height, 42.0);
    REQUIRE(transactions.undo().isAccepted());
    CHECK_EQ(target.widget(ObjectId("left")).value->layout.width, 0.0);

    UiLayoutValue moved;
    moved.x = 15.0;
    auto move = target.makeSetLayout(ObjectId("left"), moved);
    REQUIRE(move.isAccepted());
    REQUIRE(commitUi(target, transactions, *move.value, "ui.move-one").isAccepted());
    CHECK_EQ(static_cast<int>(target.read(selection, PropertyPath("layout.position")).state),
             static_cast<int>(PropertyReadState::Mixed));
    CHECK_EQ(static_cast<int>(target.makeSet(selection, PropertyPath("layout.anchor"),
                                             EditorValue::Array{1.5, 0.0},
                                             PropertySetMode::Absolute).status),
             static_cast<int>(EditorStatus::Rejected));
}

TEST_CASE("editor.ui.snapshot_load_is_atomic_and_rejects_invalid_hierarchies") {
    UiDocumentTarget source("source");
    auto root = source.makeCreate({ObjectId("root"), {}, "column", "Root", {0, 0, 640, 480}});
    REQUIRE(root.value);
    REQUIRE(source.applyDomainOperation(*root.value).isAccepted());
    auto child = source.makeCreate({ObjectId("child"), ObjectId("root"), "text", "Child", {8, 8, 100, 20}});
    REQUIRE(child.value);
    REQUIRE(source.applyDomainOperation(*child.value).isAccepted());

    UiDocumentTarget restored("restored");
    REQUIRE(restored.loadSnapshot(source.snapshotValue()).isAccepted());
    CHECK_EQ(restored.children(ObjectId("root")).size(), static_cast<std::size_t>(1));

    EditorValue::Object invalidWidget;
    invalidWidget["id"] = "orphan";
    invalidWidget["parent"] = "missing";
    invalidWidget["type"] = "text";
    invalidWidget["name"] = "Orphan";
    invalidWidget["text"] = "";
    invalidWidget["visible"] = true;
    invalidWidget["enabled"] = true;
    invalidWidget["layout"] = EditorValue::Object{{"x", 0.0}, {"y", 0.0}, {"width", 10.0},
                                                    {"height", 10.0}, {"anchorX", 0.0},
                                                    {"anchorY", 0.0}, {"pivotX", 0.0},
                                                    {"pivotY", 0.0}};
    EditorValue::Object invalidRoot;
    invalidRoot["schemaVersion"] = 1;
    invalidRoot["widgets"] = EditorValue::Array{EditorValue(std::move(invalidWidget))};
    CHECK_EQ(static_cast<int>(restored.loadSnapshot(EditorValue(std::move(invalidRoot))).status),
             static_cast<int>(EditorStatus::Rejected));
    CHECK(restored.widget(ObjectId("child")).isAccepted());
}
