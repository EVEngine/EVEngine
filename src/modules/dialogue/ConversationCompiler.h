#pragma once
#include "common/Export.h"

#include "dialogue/DnutDocument.h"

#include <string>
#include <vector>

namespace eve::dialogue {

/** @brief Compile and lint the complete pools-plus-conversations dnut document. */
[[nodiscard]] eve::Result<DnutDocument> compileDnutDocument(
    const std::string& source, const std::string& path, std::vector<ConversationDiagnostic>& diagnostics);

/** @brief Validate references and report unreachable nodes. */
[[nodiscard]] eve::Result<void> lintConversations(const std::vector<eve::dnut::SequenceAsset>& assets,
                                                  const std::string& path,
                                                  std::vector<ConversationDiagnostic>& diagnostics);

/** @brief Export stable line IDs and localization keys as RFC4180 CSV. */
EVENGINE_API_ORCHESTRATION std::string exportConversationLocalizationCsv(
    const std::vector<eve::dnut::SequenceAsset>& assets);

}  // namespace eve::dialogue
