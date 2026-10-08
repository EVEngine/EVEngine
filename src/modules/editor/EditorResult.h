#pragma once

#include "editing/EditingResult.h"
#include "editor/EditorIds.h"

namespace eve::editor {

using EditorStatus       = eve::StatusCode;
using DiagnosticSeverity = eve::Severity;
using EditorDiagnostic   = eve::Diagnostic;

/** @brief Editor-facing Result; same type as editing::Result / eve::Result. */
using editing::Result;

}  // namespace eve::editor
