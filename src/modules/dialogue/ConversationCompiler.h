#pragma once

#include "dialogue/Conversation.h"

#include <string>
#include <vector>

namespace eve::dialogue {

struct ConversationDiagnostic {
    enum class Severity { Warning, Error };
    Severity severity = Severity::Error;
    std::string path;
    int line = 0;
    std::string message;
};

/** @brief Compile parameterized conversation blocks embedded in .dnut text. */
bool compileDnutConversations(const std::string& source, const std::string& path,
                              std::vector<ConversationAsset>& assets,
                              std::vector<ConversationDiagnostic>& diagnostics);

/** @brief Validate references and report unreachable nodes. */
bool lintConversations(const std::vector<ConversationAsset>& assets, const std::string& path,
                       std::vector<ConversationDiagnostic>& diagnostics);

/** @brief Export stable line IDs and localization keys as RFC4180 CSV. */
std::string exportConversationLocalizationCsv(const std::vector<ConversationAsset>& assets);

}  // namespace eve::dialogue
