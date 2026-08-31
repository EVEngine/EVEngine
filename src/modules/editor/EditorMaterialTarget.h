#pragma once

#include "editor/EditorProperty.h"
#include "editor/EditorProtocol.h"
#include "material_editing/MaterialTarget.h"

namespace eve::editor {

using MaterialDocumentTarget = material_editing::MaterialDocumentTarget;
using IMaterialRuntimeSink = material_editing::IMaterialRuntimeSink;
using MaterialPublishingTarget = material_editing::MaterialPublishingTarget;
using IMaterialRuntimeAssetResolver = material_editing::IMaterialRuntimeAssetResolver;
using Renderable3DMaterialRuntimeSink = material_editing::Renderable3DMaterialRuntimeSink;

}  // namespace eve::editor
