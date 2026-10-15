#pragma once

#include "editing/EditingResult.h"
#include "editor/EditorResult.h"

#include <vector>

namespace eve::editor {

/** @brief Return the editing RuleId projected through a common diagnostic. */
[[nodiscard]] inline RuleId editorRuleFrom(const eve::Diagnostic& diagnostic) {
    return eve::editing::diagnosticRule(diagnostic);
}

/** @brief Append diagnostics while assembling a new immutable Result status. */
inline void appendProjectedDiagnostics(std::vector<eve::Diagnostic>& destination, const eve::Status& status) {
    destination.insert(destination.end(), status.diagnostics().begin(), status.diagnostics().end());
}

}  // namespace eve::editor
