#pragma once

#include "dialogue/Conversation.h"
#include "dialogue/Dialogue.h"

#include <string>
#include <vector>

namespace eve::dialogue {

/** @brief Exact source location in a dnut document. */
struct DnutSourceSpan {
    std::string sourceId;
    int line = 1;
    int column = 1;
};

/** @brief Stable diagnostic emitted by dnut compilation, lint, or migration. */
struct ConversationDiagnostic {
    enum class Severity { Warning, Error };
    Severity severity = Severity::Error;
    std::string path;
    int line = 0;
    std::string message;
    std::string code;
    int column = 0;
    std::string assetPath;
};

/** @brief UI-neutral syntax tree produced by the single dnut parser. */
struct DnutDocument {
    static constexpr int CurrentVersion = 1;
    std::string schema;
    int version = 0;
    DataValue poolRoot = DataValue::object({});
    std::vector<ConversationAsset> conversations;
};

}  // namespace eve::dialogue
