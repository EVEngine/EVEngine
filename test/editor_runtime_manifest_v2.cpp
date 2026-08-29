#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorRuntimeManifest.h"

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
        CHECK(registry.registerCommand(
            std::move(command),
            [](const CommandContext&, const EditorValue&) {
                return EditorResult<EditorValue>::applied(EditorValue("tree"));
            },
            ExtensionAudience::Player)
                  .isAccepted());
        CHECK(registry.registerTool({ToolId("park.tree-tool"), {}, "Tree", "Build", {}, ExtensionAudience::Player})
                  .isAccepted());
        CHECK(registry.registerRule({RuleId("park.path-rule"), {}, ExtensionAudience::Player}).isAccepted());
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
    REQUIRE(extensions.load(park).isAccepted());

    MemoryAssetDatabase assets;
    REQUIRE(assets
                .publish(runtimeAsset("park", "content://Park"),
                         {{AssetGuid("park"), AssetGuid("tree"), DependencyKind::Hard, {}},
                          {AssetGuid("park"), AssetGuid("source"), DependencyKind::EditorOnly, {}}})
                .isAccepted());
    REQUIRE(assets.publish(runtimeAsset("tree", "content://Tree")).isAccepted());
    REQUIRE(assets.publish(runtimeAsset("source", "content://TreeSource")).isAccepted());

    RuntimeEditorManifest manifest;
    manifest.commands   = {CommandId("park.place-tree")};
    manifest.tools      = {ToolId("park.tree-tool")};
    manifest.rules      = {RuleId("park.path-rule")};
    manifest.rootAssets = {AssetGuid("park")};
    auto package        = RuntimeEditorPublisher().publish(manifest, commands, extensions, assets);
    REQUIRE(package.isAccepted());
    CHECK(package.value->profile.allowsCommand(CommandId("park.place-tree")));
    CHECK(!package.value->profile.allowsCommand(CommandId("developer.delete-source")));
    CHECK_EQ(package.value->tools.size(), static_cast<std::size_t>(1));
    CHECK_EQ(package.value->rules.size(), static_cast<std::size_t>(1));
    CHECK_EQ(package.value->assetClosure.size(), static_cast<std::size_t>(2));
}

TEST_CASE("editor.v2.runtime_manifest_rejects_developer_features") {
    EditorCommandService    commands;
    EditorExtensionRegistry extensions(&commands);
    MemoryAssetDatabase     assets;
    RuntimeEditorManifest   manifest;
    manifest.features = HostFeature::RuntimeWorld | HostFeature::ArbitraryScript;
    auto result       = RuntimeEditorPublisher().publish(manifest, commands, extensions, assets);
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Rejected));
}
