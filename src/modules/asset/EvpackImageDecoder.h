#pragma once
#include "common/Export.h"

/** @file EvpackImageDecoder.h @brief Backend-neutral canonical EVIMG decoding. */

#include "asset/EvpackResourceReader.h"

namespace eve::asset {

/** @brief Bounds applied before allocating decoded image bytes. */
struct EvpackImageDecodeLimits {
    std::uint32_t maximumDimension    = 32768;
    std::uint64_t maximumPixels       = 268'435'456;
    std::uint64_t maximumDecodedBytes = 1024ull * 1024ull * 1024ull;
};

/** @brief Owning validated RGBA8 mip chain independent of a graphics backend. */
struct DecodedEvpackImage {
    AssetRef                  asset;
    std::uint32_t             width = 0, height = 0, levels = 0;
    bool                      srgb = false;
    std::vector<std::uint8_t> pixels;
    EvpackVariantSelection    variant;
};

/**
 * @brief Decode and validate canonical eve.image/3 data with eve.image/2 compatibility.
 * @param reader Borrowed immutable package reader valid for the call.
 * @param image Stable image identity.
 * @param capabilities Runtime capability selection.
 * @param limits Allocation limits checked before copying pixels.
 * @return Detached owning RGBA8 mip data; failure publishes no partial bytes.
 * @thread Worker-safe when reader is read concurrently; performs no GPU calls or callbacks.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<DecodedEvpackImage> decodeEvpackImage(
    const EvpackResourceReader& reader, const AssetRef& image, const EvpackCapabilities& capabilities,
    const EvpackImageDecodeLimits& limits = {});

}  // namespace eve::asset
