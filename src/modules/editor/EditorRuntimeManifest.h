#pragma once

#include "editor/EditorAssetDatabase.h"
#include "editor/EditorExtension.h"

namespace eve::editor {

/** @brief Explicit publish allow-list for a player-facing editor build. */
struct RuntimeEditorManifest {
    HostFeature            features = HostFeature::RuntimeWorld | HostFeature::UserCreations;
    std::vector<CommandId> commands;
    std::vector<ToolId>    tools;
    std::vector<StableId>  palettes;
    std::vector<RuleId>    rules;
    std::vector<AssetGuid> rootAssets;
    std::size_t            maxPayloadBytes = 64 * 1024;
};

/** @brief Resolved editor package embedded into a game build. */
struct RuntimeEditorPackage {
    HostProfile                             profile;
    std::vector<CommandDescriptor>          commands;
    std::vector<ExtensionToolDescriptor>    tools;
    std::vector<ExtensionPaletteDescriptor> palettes;
    std::vector<ExtensionRuleDescriptor>    rules;
    std::vector<AssetGuid>                  assetClosure;
};

/** @brief Validates an explicit manifest and resolves its runtime dependency closure. */
class RuntimeEditorPublisher {
public:
    /**
     * @brief Build a deny-by-default runtime package.
     * @param manifest Explicit components and root assets selected by the game.
     * @param commands Command registry used to validate command identities.
     * @param extensions Extension registry used to resolve presentation metadata.
     * @param assets Asset index used to close hard/build/soft runtime dependencies.
     * @return A package or a diagnostic when any selected identity is unavailable.
     */
    EditorResult<RuntimeEditorPackage> publish(const RuntimeEditorManifest&   manifest,
                                               const EditorCommandService&    commands,
                                               const EditorExtensionRegistry& extensions,
                                               const MemoryAssetDatabase&     assets) const;
};

}  // namespace eve::editor
