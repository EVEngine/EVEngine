#pragma once

#include "dialogue/ConversationCompiler.h"

namespace eve::dialogue {

/** @brief Import Yarn Spinner source through the canonical validated conversation schema. */
[[nodiscard]] eve::Result<std::vector<ConversationAsset>> importYarnConversation(
    const std::string& source, const std::string& path, std::vector<ConversationDiagnostic>& diagnostics);

/** @brief Import Twee 3 passage source through the canonical validated conversation schema. */
[[nodiscard]] eve::Result<std::vector<ConversationAsset>> importTweeConversation(
    const std::string& source, const std::string& path, std::vector<ConversationDiagnostic>& diagnostics);

}  // namespace eve::dialogue
