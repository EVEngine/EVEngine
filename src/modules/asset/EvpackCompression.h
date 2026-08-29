#pragma once

/**
 * @file EvpackCompression.h
 * @brief Bounded deterministic compression used by the `.evpack` wire format.
 */

#include "asset/Evpack.h"

namespace eve::asset {

/** @brief Compress decoded chunk bytes using the selected stable wire codec. */
[[nodiscard]] Result<std::vector<std::uint8_t>> compressEvpackChunk(
    EvpackCodec codec, std::span<const std::uint8_t> decoded);

/**
 * @brief Decode one chunk into its authenticated canonical bytes.
 * @param codec Physical codec recorded in the TOC.
 * @param stored Untrusted stored bytes.
 * @param decodedSize Exact output size admitted from the TOC.
 * @param maximumDecodedBytes Caller budget checked before allocation.
 */
[[nodiscard]] Result<std::vector<std::uint8_t>> decompressEvpackChunk(
    EvpackCodec codec, std::span<const std::uint8_t> stored, std::uint64_t decodedSize,
    std::uint64_t maximumDecodedBytes);

/** @brief Stable codec implementation identity included in deterministic Cook keys. */
[[nodiscard]] std::string evpackCodecBuildIdentity(EvpackCodec codec);

}  // namespace eve::asset
