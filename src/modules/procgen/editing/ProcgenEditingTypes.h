#pragma once
#include "editing/EditingGraph.h"
namespace eve::procgen_editing {
using editing::DiagnosticSeverity;
using editing::GraphConnectionDecision; using editing::GraphDocumentData; using editing::GraphEdgeRecord;
using editing::GraphNodeId; using editing::GraphNodeRecord; using editing::GraphPinDirection;
using editing::GraphPinId; using editing::GraphPinRecord; using editing::IGraphDomainProvider;
using editing::Revision; using editing::RuleId; using editing::StableId; using editing::Status; using editing::Value;
using editing::Result;
using EditorStatus = editing::Status; using EditorValue = editing::Value; using EditorDiagnostic = editing::Diagnostic;
/** @brief Procgen authoring result alias backed by the canonical editing result contract. */
template <class T>
using EditorResult = editing::Result<T>;
}  // namespace eve::procgen_editing
