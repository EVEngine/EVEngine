#include "editor/EditorPluginPermissions.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editor;

TEST_CASE("editor.plugins.permissions_are_narrow_reversible_and_default_to_ask") {
    PluginPermissionTarget permissions("project-plugins");
    CHECK_EQ(static_cast<int>(permissions.makeSet({StableId("broad"), "importer", "filesystem.write", "/", "allow"}).status),
             static_cast<int>(EditorStatus::Rejected));
    PluginPermissionGrant grant{StableId("assets"), "model-importer", "filesystem.read", "assets/models", "allow"};
    auto set = permissions.makeSet(grant); REQUIRE(set.value);
    CHECK(permissions.applyDomainOperation(*set.value).accepted());
    CHECK_EQ(permissions.decision("model-importer", "filesystem.read", "assets/models"), std::string("allow"));
    CHECK_EQ(permissions.decision("model-importer", "network.connect", "example.com"), std::string("ask"));
    DomainOperation undo = *set.value; undo.type = set.value->inverseType; undo.payload = set.value->inverse;
    CHECK(permissions.applyDomainOperation(undo).accepted());
    CHECK(permissions.grants().empty());
}

TEST_CASE("editor.plugins.permission_snapshot_is_atomic_and_rejects_duplicate_semantics") {
    PluginPermissionTarget permissions("project-plugins");
    auto set = permissions.makeSet({StableId("one"), "sync", "network.connect", "api.example.com:443", "ask"});
    REQUIRE(set.value); CHECK(permissions.applyDomainOperation(*set.value).accepted());
    EditorValue invalid = permissions.snapshotValue();
    auto* root = invalid.getIf<EditorValue::Object>(); REQUIRE(root);
    auto* grants = root->at("grants").getIf<EditorValue::Array>(); REQUIRE(grants);
    grants->push_back(EditorValue::Object{{"id","two"},{"plugin","sync"},{"capability","network.connect"},
                                          {"scope","api.example.com:443"},{"decision","allow"}});
    CHECK_EQ(static_cast<int>(permissions.loadSnapshot(invalid).status), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(permissions.grants().size(), 1U);
}
