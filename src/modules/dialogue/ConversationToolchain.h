#pragma once
#include "common/Export.h"

#include "dialogue/ConversationCompiler.h"

namespace eve::dialogue {

/** @brief Lint a complete multi-file registry, including cross-asset call targets. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION eve::Result<void> lintConversationWorkspace(
    const std::vector<ConversationAsset>& assets, const std::string& label,
    std::vector<ConversationDiagnostic>& diagnostics);

/** @brief Rename an asset and rewrite every cross-asset call target. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION eve::Result<void> renameConversationAsset(
    std::vector<ConversationAsset>& assets, const std::string& oldId, const std::string& newId);

/** @brief Rename one stable node and rewrite all references inside its asset. */
[[nodiscard]] EVENGINE_API_ORCHESTRATION eve::Result<void> renameConversationNode(
    std::vector<ConversationAsset>& assets, const std::string& assetId, const std::string& oldId,
    const std::string& newId);

}  // namespace eve::dialogue
