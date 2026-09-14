#pragma once

#include "procgen/heightmap/Heightmap.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace eve::procgen {

/**
 * @brief Persisted terrain encodings the runtime can decode.
 *
 * `Evtr` is the chunked, PackBits-compressed `EVTR` archive produced by
 * `TerrainAsset::bake` (UNORM16 heights plus hydrology and climate layers).
 * `Evtrn` is the raw float32 `heightfield.bin` payload written next to an
 * `eve.terrain/1` asset definition. They are unrelated blobs, so the format is
 * normally detected from the magic rather than named by the caller.
 */
enum class TerrainFileFormat : std::uint8_t {
    Auto,  ///< Detect from the leading magic bytes.
    Evtr,  ///< Chunked EVTR archive.
    Evtrn  ///< Raw EVTRN heightfield blob.
};

/**
 * @brief Height field decoded from a persisted terrain file.
 *
 * `spacingX`/`spacingZ` are only meaningful when `hasSpacing` is true: the EVTRN
 * blob stores metres-per-cell, while an EVTR archive stores no spacing at all and
 * leaves it to the level that references the archive.
 */
struct DecodedTerrainFile {
    Heightmap   heightmap;
    float       spacingX  = 1.f;
    float       spacingZ  = 1.f;
    float       minHeight = 0.f;
    float       maxHeight = 0.f;
    bool        hasSpacing = false;
    std::string format;  ///< "evtr" or "evtrn".
};

/**
 * @brief Parse a format selector from a script-facing string.
 *
 * @param name "auto", "evtr" or "evtrn" (case-insensitive); anything else maps to
 *             `TerrainFileFormat::Auto`.
 * @return The parsed selector.
 */
[[nodiscard]] TerrainFileFormat parseTerrainFileFormat(std::string_view name) noexcept;

/**
 * @brief Decode a terrain height field from in-memory file bytes.
 *
 * @param bytes  Whole file contents.
 * @param format Encoding selector, or `Auto` to detect from the magic.
 * @return The decoded height field, or a failure diagnostic naming the reason.
 */
[[nodiscard]] eve::Result<DecodedTerrainFile> decodeTerrainFile(std::span<const std::uint8_t> bytes,
                                                                TerrainFileFormat format);

/**
 * @brief Read and decode a terrain height field through the filesystem capability.
 *
 * @param path   Path resolved by the active filesystem provider.
 * @param format Encoding selector, or `Auto` to detect from the magic.
 * @return The decoded height field, or a failure diagnostic.
 */
[[nodiscard]] eve::Result<DecodedTerrainFile> loadTerrainFile(const std::string &path,
                                                              TerrainFileFormat format);

}  // namespace eve::procgen
