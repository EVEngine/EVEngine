#pragma once

#include "dialogue/DnutDocument.h"

#include <string>

namespace eve::dialogue {

/** @brief Parse pools and conversations from one versioned dnut token stream. */
bool parseDnutDocument(const std::string& source, const std::string& path, DnutDocument& out,
                       std::vector<ConversationDiagnostic>& diagnostics);

}  // namespace eve::dialogue
