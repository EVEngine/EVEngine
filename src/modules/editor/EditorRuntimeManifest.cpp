#include "editor/EditorRuntimeManifest.h"

#include <algorithm>
#include <deque>
#include <unordered_set>

namespace eve::editor {
namespace {

template <class Id>
bool selected(const std::vector<Id>& values, const Id& id) {
    return std::find(values.begin(), values.end(), id) != values.end();
}

EditorResult<RuntimeEditorPackage> manifestError(const char* rule, std::string message) {
    return EditorResult<RuntimeEditorPackage>::error(EditorStatus::Rejected, RuleId(rule), std::move(message));
}

}  // namespace

EditorResult<RuntimeEditorPackage> RuntimeEditorPublisher::publish(const RuntimeEditorManifest&   manifest,
                                                                   const EditorCommandService&    commands,
                                                                   const EditorExtensionRegistry& extensions,
                                                                   const MemoryAssetDatabase&     assets) const {
    constexpr HostFeature forbidden = HostFeature::SourceAssets | HostFeature::ArbitraryScript |
                                      HostFeature::BuildCook | HostFeature::SourceControl | HostFeature::DebugOverride;
    if ((manifest.features & forbidden) != HostFeature::None)
        return manifestError("editor.runtime.forbidden-feature",
                             "Runtime editor manifest requests a developer-only feature");

    RuntimeEditorPackage package;
    package.profile = HostProfile::runtimeBuilder();
    package.profile.setFeatures(manifest.features);
    package.profile.setMaxPayloadBytes(manifest.maxPayloadBytes);
    for (const CommandId& id : manifest.commands) {
        const CommandDescriptor* descriptor = commands.find(id);
        if (!descriptor)
            return manifestError("editor.runtime.command-not-found",
                                 "Manifest command is not registered: " + id.value());
        if (!descriptor->automationAllowed)
            return manifestError("editor.runtime.command-not-publishable",
                                 "Manifest command is not publishable: " + id.value());
        if (!package.profile.hasFeatures(descriptor->requiredFeatures))
            return manifestError("editor.runtime.command-feature-missing",
                                 "Manifest omits a feature required by command: " + id.value());
        package.profile.allowCommand(id);
        package.commands.push_back(*descriptor);
    }

    HostProfile audienceProfile = HostProfile::runtimeBuilder();
    audienceProfile.setAllowAllCommands(true);
    for (const auto& descriptor : extensions.tools(audienceProfile))
        if (selected(manifest.tools, descriptor.id)) package.tools.push_back(descriptor);
    for (const ToolId& id : manifest.tools)
        if (std::none_of(package.tools.begin(), package.tools.end(), [&](const auto& value) { return value.id == id; }))
            return manifestError("editor.runtime.tool-not-found", "Manifest tool is unavailable: " + id.value());
    for (const auto& descriptor : extensions.palettes(audienceProfile))
        if (selected(manifest.palettes, descriptor.id)) package.palettes.push_back(descriptor);
    for (const StableId& id : manifest.palettes)
        if (std::none_of(package.palettes.begin(), package.palettes.end(),
                         [&](const auto& value) { return value.id == id; }))
            return manifestError("editor.runtime.palette-not-found", "Manifest palette is unavailable: " + id.value());
    for (const auto& descriptor : extensions.rules(audienceProfile))
        if (selected(manifest.rules, descriptor.id)) package.rules.push_back(descriptor);
    for (const RuleId& id : manifest.rules)
        if (std::none_of(package.rules.begin(), package.rules.end(), [&](const auto& value) { return value.id == id; }))
            return manifestError("editor.runtime.rule-not-found", "Manifest rule is unavailable: " + id.value());

    std::deque<AssetGuid> pending(manifest.rootAssets.begin(), manifest.rootAssets.end());
    std::unordered_set<AssetGuid, StrongEditorIdHash<AssetGuid>> visited;
    while (!pending.empty()) {
        AssetGuid current = pending.front();
        pending.pop_front();
        if (!visited.emplace(current).second) continue;
        if (!assets.find(current).isAccepted())
            return manifestError("editor.runtime.asset-not-found", "Manifest asset is unavailable: " + current.value());
        package.assetClosure.push_back(current);
        for (const AssetDependency& dependency : assets.dependencies(current))
            if (dependency.kind != DependencyKind::EditorOnly && dependency.kind != DependencyKind::Source)
                pending.push_back(dependency.to);
    }
    std::sort(package.assetClosure.begin(), package.assetClosure.end());
    return EditorResult<RuntimeEditorPackage>::applied(std::move(package));
}

}  // namespace eve::editor
