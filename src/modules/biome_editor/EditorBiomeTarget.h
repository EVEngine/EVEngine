#pragma once

// Compatibility facade. Canonical implementation is owned by biome_editing.
#include "biome_editing/BiomeTarget.h"
#include "editor/EditorProperty.h"
#include "editor/EditorProtocol.h"

namespace eve::editor {
using biome_editing::BiomeAssetValue;
using biome_editing::BiomeDocumentRuntime;
using biome_editing::BiomeDocumentTarget;
using biome_editing::BiomeLayerValue;
using biome_editing::IBiomeSpatialResolver;
} // namespace eve::editor
