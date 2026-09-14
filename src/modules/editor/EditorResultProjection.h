#pragma once

#include "editing/EditingResult.h"
#include "editor/EditorResult.h"

#include <utility>
#include <vector>

namespace eve::editor {

/** @brief Return the editing RuleId projected through a common diagnostic. */
[[nodiscard]] inline RuleId editorRuleFrom(const eve::Diagnostic& diagnostic) {
    return eve::editing::diagnosticRule(diagnostic);
}

/** @brief Preserve a common Result at the editor compatibility boundary. */
template <class Output>
[[nodiscard]] inline EditorResult<Output> projectCommonResult(eve::Result<Output>&& result) {
    return std::move(result);
}

/** @brief Preserve a common void Result at the editor compatibility boundary. */
[[nodiscard]] inline EditorResult<void> projectCommonResult(eve::Result<void>&& result) {
    return std::move(result);
}

/** @brief Project a failed common status into an editor result without inventing a second envelope. */
template <class Output>
[[nodiscard]] inline EditorResult<Output> projectCommonFailure(const eve::Status& status) {
    return EditorResult<Output>::failure(status);
}

/** @brief Append diagnostics while assembling a new immutable Result status. */
inline void appendProjectedDiagnostics(std::vector<eve::Diagnostic>& destination, const eve::Status& status) {
    destination.insert(destination.end(), status.diagnostics().begin(), status.diagnostics().end());
}

}  // namespace eve::editor
