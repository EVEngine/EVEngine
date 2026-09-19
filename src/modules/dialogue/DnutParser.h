#pragma once

#include "dialogue/DnutDocument.h"

#include <string>

namespace eve::dialogue {

/** @brief Parse pools and conversations from one versioned dnut token stream. */
[[nodiscard]] eve::Result<DnutDocument> parseDnutDocument(
    const std::string& source, const std::string& path, std::vector<ConversationDiagnostic>& diagnostics);

}  // namespace eve::dialogue
