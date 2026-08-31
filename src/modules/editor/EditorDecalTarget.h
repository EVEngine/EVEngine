#pragma once
#include "editor/EditorProperty.h"
#include "editor/EditorProtocol.h"
#include "decal_editing/DecalTarget.h"
namespace eve::editor {
using DecalDocumentTarget=decal_editing::DecalDocumentTarget;
using IDecalRuntimeAssetResolver=decal_editing::IDecalRuntimeAssetResolver;
using IDecalRuntimeSink=decal_editing::IDecalRuntimeSink;
using DecalRuntimeBinding=decal_editing::DecalRuntimeBinding;
using DecalPublishingTarget=decal_editing::DecalPublishingTarget;
using DecalGizmoPreviewService=decal_editing::DecalGizmoPreviewService;
}  // namespace eve::editor
