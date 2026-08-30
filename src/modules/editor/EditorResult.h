#pragma once

#include "editing/EditingResult.h"
#include "editor/EditorIds.h"

namespace eve::editor {

using EditorStatus       = eve::editing::Status;
using DiagnosticSeverity = eve::editing::DiagnosticSeverity;
using EditorDiagnostic   = eve::editing::Diagnostic;

template <class T>
using EditorResult = eve::editing::Result<T>;

}  // namespace eve::editor
