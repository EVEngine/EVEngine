#include "editor/EditorProjectSettings.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

namespace {
SelectionSnapshot selection(const ProjectSettingsTarget& target) {
    SelectionSnapshot result;
    SelectionItem item; item.target = TargetId(target.targetId()); item.item = StableId(target.targetId().value());
    result.items.push_back(item); result.primary = item; return result;
}
}

TEST_CASE("editor.settings.secret_references_are_redacted_and_raw_values_rejected") {
    ProjectSettingsTarget settings("project", defaultProjectSettingsSchema());
    const auto selected = selection(settings);
    auto raw = settings.makeSet(selected, PropertyPath("network.auth-token"), "plain-token",
                                PropertySetMode::Absolute);
    CHECK_EQ(static_cast<int>(raw.code()), static_cast<int>(EditorStatus::Rejected));
    auto secret = settings.makeSet(selected, PropertyPath("network.auth-token"),
                                   "secret://project/network-token", PropertySetMode::Absolute);
    REQUIRE(secret.ok());
    CHECK(settings.applyDomainOperation(secret.value()).ok());
    auto read = settings.read(selected, PropertyPath("network.auth-token"));
    CHECK(read.value == EditorValue("secret://redacted"));
    const EditorValue snapshot = settings.snapshotValue();
    const auto* root = snapshot.getIf<EditorValue::Object>(); REQUIRE(root);
    const auto* values = root->at("values").getIf<EditorValue::Object>(); REQUIRE(values);
    CHECK(values->at("network.auth-token") == EditorValue("secret://project/network-token"));
}

TEST_CASE("editor.settings_changes_are_reversible_restart_aware_and_atomic") {
    ProjectSettingsTarget settings("project", defaultProjectSettingsSchema());
    const auto selected = selection(settings);
    auto change = settings.makeSet(selected, PropertyPath("content.asset-root"), "content",
                                   PropertySetMode::Absolute);
    REQUIRE(change.ok()); CHECK(settings.applyDomainOperation(change.value()).ok());
    CHECK_EQ(settings.pendingRestart().size(), 1U);
    DomainOperation undo = change.value(); undo.type = change.value().inverseType;
    undo.payload = change.value().inverse;
    CHECK(settings.applyDomainOperation(undo).ok());
    CHECK(settings.read(selected, PropertyPath("content.asset-root")).value == EditorValue("assets"));

    const auto validSnapshot = settings.snapshotValue();
    EditorValue invalid = validSnapshot;
    auto* root = invalid.getIf<EditorValue::Object>(); REQUIRE(root);
    auto* values = root->at("values").getIf<EditorValue::Object>(); REQUIRE(values);
    (*values)["unknown.setting"] = true;
    CHECK_EQ(static_cast<int>(settings.loadSnapshot(invalid).code()), static_cast<int>(EditorStatus::Rejected));
    CHECK(settings.read(selected, PropertyPath("content.asset-root")).value == EditorValue("assets"));
}
