#pragma once

#include "dialogue/ConversationCompiler.h"

namespace eve::dialogue {

/** @brief Lint a complete multi-file registry, including cross-asset call targets. */
bool lintConversationWorkspace(const std::vector<ConversationAsset>& assets, const std::string& label,
                               std::vector<ConversationDiagnostic>& diagnostics);

/** @brief Rename an asset and rewrite every cross-asset call target. */
bool renameConversationAsset(std::vector<ConversationAsset>& assets, const std::string& oldId, const std::string& newId,
                             std::string* error = nullptr);

/** @brief Rename one stable node and rewrite all references inside its asset. */
bool renameConversationNode(std::vector<ConversationAsset>& assets, const std::string& assetId,
                            const std::string& oldId, const std::string& newId, std::string* error = nullptr);

}  // namespace eve::dialogue
