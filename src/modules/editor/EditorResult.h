#pragma once

#include "editing/EditingResult.h"
#include "editor/EditorIds.h"

namespace eve::editor {

using EditorStatus       = eve::StatusCode;
using DiagnosticSeverity = eve::Severity;
using EditorDiagnostic   = eve::Diagnostic;

template <class T>
using EditorResult = eve::Result<T>;

}  // namespace eve::editor
