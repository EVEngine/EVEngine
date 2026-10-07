#pragma once

#include "editing/EditingResult.h"
#include "editor/EditorIds.h"

namespace eve::editor {

using EditorStatus       = eve::StatusCode;
using DiagnosticSeverity = eve::Severity;
using EditorDiagnostic   = eve::Diagnostic;

/** @brief Editor-facing Result name; same type as editing::Result / eve::Result. */
using editing::EditorResult;

}  // namespace eve::editor
