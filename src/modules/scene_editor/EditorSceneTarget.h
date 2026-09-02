#pragma once

/** @file @brief Compatibility include for canonical scene editing targets. */
#include "editor/EditorAuthority.h"
#include "editor/EditorProperty.h"
#include "scene_editing/SceneTarget.h"

namespace eve::editor {
using SceneTransformValue       = scene_editing::SceneTransformValue;
using ITransformEditTarget      = scene_editing::ITransformEditTarget;
using SceneObjectSnapshot       = scene_editing::SceneObjectSnapshot;
using CreateSceneObjectRequest  = scene_editing::CreateSceneObjectRequest;
using ISceneHierarchyEditTarget = scene_editing::ISceneHierarchyEditTarget;
using SceneComponentLinkSnapshot = scene_editing::SceneComponentLinkSnapshot;
using ISceneComponentInspector   = scene_editing::ISceneComponentInspector;
using SceneTargetBase            = scene_editing::SceneTargetBase;
using SceneDocumentTarget        = scene_editing::SceneDocumentTarget;
using RuntimeWorldTarget         = scene_editing::RuntimeWorldTarget;
using SceneHostEditorTarget      = scene_editing::SceneHostEditorTarget;
using ScenePlacementToolLogic    = scene_editing::ScenePlacementToolLogic;
using SceneHierarchyToolLogic    = scene_editing::SceneHierarchyToolLogic;
using SceneTransformToolLogic    = scene_editing::SceneTransformToolLogic;
using ScenePropertyProvider      = scene_editing::ScenePropertyProvider;
}  // namespace eve::editor
