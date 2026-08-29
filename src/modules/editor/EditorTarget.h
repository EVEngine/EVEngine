#pragma once
#include "editing/EditableTarget.h"
#include "editor/EditorIds.h"
namespace eve::editor {
using EditRegion         = eve::editing::EditRegion;
using IEditableTarget    = eve::editing::IEditableTarget;
using TargetDescriptor   = eve::editing::TargetDescriptor;
using FieldWriteStatus   = eve::editing::FieldWriteStatus;
using IGridTarget        = eve::editing::IGridTarget;
using IIntFieldTarget    = eve::editing::IIntFieldTarget;
using IScalarFieldTarget = eve::editing::IScalarFieldTarget;
}  // namespace eve::editor
