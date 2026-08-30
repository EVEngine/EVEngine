#pragma once

/** @file @brief Compatibility include for scene component payload authoring. */
#include "editor/EditorAuthority.h"
#include "editor/EditorProperty.h"
#include "scene_editing/SceneComponentPayload.h"

namespace eve::editor {
using SceneComponentChange              = scene_editing::SceneComponentChange;
using SceneComponentPayloadRef          = scene_editing::SceneComponentPayloadRef;
using ISceneComponentPayloadProvider    = scene_editing::ISceneComponentPayloadProvider;
using SceneComponentPropertyBindings    = scene_editing::SceneComponentPropertyBindings;
using ISceneComponentPayloadTarget      = scene_editing::ISceneComponentPayloadTarget;
using SceneComponentPayloadRegistry     = scene_editing::SceneComponentPayloadRegistry;
using scene_editing::makeSceneComponentSelection;
}  // namespace eve::editor
