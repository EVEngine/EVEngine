#pragma once

#include "editing/EditingGraph.h"

namespace eve::dialogue_editing {
using editing::DiagnosticSeverity;
using editing::GraphConnectionDecision;
using editing::GraphDocumentData;
using editing::GraphEdgeRecord;
using editing::GraphNodeId;
using editing::GraphNodeRecord;
using editing::GraphPinDirection;
using editing::GraphPinId;
using editing::GraphPinRecord;
using editing::IGraphDomainProvider;
using editing::Revision;
using editing::RuleId;
using editing::Status;
using editing::Value;
template <class T> using EditorResult = editing::Result<T>;
using EditorStatus = editing::Status;
using EditorValue = editing::Value;
using EditorDiagnostic = editing::Diagnostic;
}  // namespace eve::dialogue_editing
