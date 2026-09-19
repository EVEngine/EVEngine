#pragma once
#include "common/Export.h"


#include "dialogue/ConversationCompiler.h"

namespace eve::dialogue {

/** @brief Import Yarn Spinner source into a validated conversation asset. */
EVENGINE_API_ORCHESTRATION bool importYarnConversation(const std::string& source, const std::string& path, std::vector<ConversationAsset>& assets,
                            std::vector<ConversationDiagnostic>& diagnostics);

/** @brief Import Twee 3 passage source into a validated conversation asset. */
EVENGINE_API_ORCHESTRATION bool importTweeConversation(const std::string& source, const std::string& path, std::vector<ConversationAsset>& assets,
                            std::vector<ConversationDiagnostic>& diagnostics);

}  // namespace eve::dialogue
