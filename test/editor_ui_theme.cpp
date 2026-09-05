#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorAuthority.h"
#include "editor/EditorIds.h"
#include "editor/EditorTransactionService.h"
#include "ui/Theme.h"
#include "ui_editing/UiTheme.h"

#include <cmath>

using namespace eve::ui_editing;
using namespace eve::editing;
using eve::editor::LocalTransactionBackend;
using eve::editor::LocalWorldAuthority;

namespace {

EditorResult<eve::editor::TransactionReceipt> commitTheme(UiThemeCatalogTarget& target,
                                                          LocalTransactionBackend& transactions,
                                                          const DomainOperation& operation,
                                                          const std::string& id) {
    eve::editor::TransactionSpec specification;
    specification.id = eve::editor::TransactionId(id);
    specification.label = "Edit theme";
    specification.target = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    auto begun = transactions.begin(std::move(specification));
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
    item.item = StableId(id);
    item.type = "ui.theme";
    selection.items.push_back(item);
    selection.primary = item;
    return selection;
}

}  // namespace

TEST_CASE("editor.ui.theme_catalog_duplicate_recolor_and_undo_are_revision_safe") {
    UiThemeCatalogTarget target("styles");
    LocalWorldAuthority authority(&target);
    LocalTransactionBackend transactions(&authority);
    CHECK_EQ(target.themes().size(), static_cast<std::size_t>(2));

    auto duplicated = target.makeDuplicate(ObjectId("dark"), ObjectId("studio"), "Studio");
    REQUIRE(duplicated.ok());
    REQUIRE(commitTheme(target, transactions, duplicated.value(), "theme.duplicate").ok());
    CHECK_EQ(target.themes().size(), static_cast<std::size_t>(3));

    const SelectionSnapshot selection = themeSelection(target, "studio");
    auto recolor = target.makeSet(selection, PropertyPath("color.button"),
                                  EditorValue::Array{0.1, 0.2, 0.3, 1.0}, PropertySetMode::Absolute);
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
    UiThemeCatalogTarget target("styles");
    LocalWorldAuthority authority(&target);
    LocalTransactionBackend transactions(&authority);
    auto removeLight = target.makeDelete(ObjectId("light"));
    REQUIRE(removeLight.ok());
    REQUIRE(commitTheme(target, transactions, removeLight.value(), "theme.delete-light").ok());
    CHECK_EQ(static_cast<int>(target.makeDelete(ObjectId("dark")).code()),
             static_cast<int>(EditorStatus::Rejected));
    CHECK_EQ(target.themes().size(), static_cast<std::size_t>(1));

    UiThemeCatalogTarget source("source");
    LocalWorldAuthority sourceAuthority(&source);
    LocalTransactionBackend sourceTx(&sourceAuthority);
    auto copied = source.makeDuplicate(ObjectId("dark"), ObjectId("copy"), "Copy");
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
    UiThemeCatalogTarget target("styles");
    UiThemePreviewService previews;
    const auto current = previews.build(target, ObjectId("dark"), target.revision());
    CHECK_EQ(static_cast<int>(current.status), static_cast<int>(EditorStatus::Applied));
    const auto stale = previews.build(target, ObjectId("dark"), target.revision() + 1);
    CHECK_EQ(static_cast<int>(stale.status), static_cast<int>(EditorStatus::Conflict));

    const eve::ui::Theme previous = eve::ui::globalTheme();
    const std::string previousName = eve::ui::globalThemeName();
    REQUIRE(target.applyDomainOperation(target.makeSetActive(ObjectId("light")).value()).ok());
    REQUIRE(UiThemeRuntimePublisher().publish(target).ok());
    CHECK_EQ(eve::ui::globalThemeName(), std::string("light"));
    eve::ui::setGlobalTheme(previous, previousName);
}
