#pragma once

/** @file TerrainDocumentCodec.h @brief Versioned editor persistence for editable heightfields. */

#include "editor/EditorResult.h"
#include "editor/EditorValue.h"
#include "procgen/heightmap/Heightmap.h"

#include <cstdint>

namespace eve::heightmap_target {

/** @brief Allocation limits checked before an editor terrain document is materialized. */
struct TerrainDocumentLimits {
    std::uint32_t maximumDimension = 8192;
    std::uint64_t maximumSamples   = 67'108'864;
};

/** @brief Owning editable terrain state restored from one document snapshot. */
struct TerrainDocumentData {
    procgen::Heightmap heightmap;
    float              spacingX = 1.0F;
    float              spacingZ = 1.0F;
};

/**
 * @brief Encode the current authoritative heightmap as `eve.terrain-editor-document/1`.
 * @return An owning deterministic value snapshot suitable for DocumentService.
 * @thread Owner/editor thread; does not retain references or invoke callbacks.
 */
[[nodiscard]] editor::EditorResult<editor::EditorValue> encodeTerrainDocument(const procgen::Heightmap& heightmap,
                                                                              float spacingX, float spacingZ,
                                                                              const TerrainDocumentLimits& limits = {});

/**
 * @brief Validate and atomically decode one terrain editor snapshot.
 * @return Owning data; unknown fields, versions, non-finite values and oversized grids are rejected.
 * @thread Worker-safe for an immutable input value; does not retain input references.
 */
[[nodiscard]] editor::EditorResult<TerrainDocumentData> decodeTerrainDocument(const editor::EditorValue&   value,
                                                                              const TerrainDocumentLimits& limits = {});

}  // namespace eve::heightmap_target
