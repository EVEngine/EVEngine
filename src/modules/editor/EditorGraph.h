#pragma once

#include "editor/EditorProtocol.h"
#include "material_editing/MaterialGraph.h"

namespace eve::editor {
using GraphPinDirection       = editing::GraphPinDirection;
using GraphNodeId             = editing::GraphNodeId;
using GraphPinId              = editing::GraphPinId;
using GraphPinRecord          = editing::GraphPinRecord;
using GraphNodeRecord         = editing::GraphNodeRecord;
using GraphEdgeRecord         = editing::GraphEdgeRecord;
using GraphDocumentData       = editing::GraphDocumentData;
using GraphConnectionDecision = editing::GraphConnectionDecision;
using GraphDocument           = editing::GraphDocument;
using IGraphDomainProvider    = editing::IGraphDomainProvider;
using MaterialCompileResult   = material_editing::MaterialCompileResult;
using MaterialGraphDomain     = material_editing::MaterialGraphDomain;
using MaterialEditorService   = material_editing::MaterialEditorService;
}  // namespace eve::editor
