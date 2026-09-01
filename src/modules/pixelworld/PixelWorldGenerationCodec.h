#pragma once

#include "common/Result.h"
#include "pixelworld/PixelWorldGeneration.h"

#include <string>
#include <string_view>

namespace eve::pixelworld {

/** @brief Encode schema `eve.pixelworld.generation-request` version 1 canonically. */
[[nodiscard]] eve::Result<std::string> encodePixelWorldGenerationRequestJson(
    const PixelWorldGenerationRequest& request);

/**
 * @brief Decode schema `eve.pixelworld.generation-request` version 1.
 * @remarks Unknown fields and versions are rejected. The returned request owns every
 * stamp cell, and decoding cannot mutate a live PixelWorld.
 */
[[nodiscard]] eve::Result<PixelWorldGenerationRequest>
decodePixelWorldGenerationRequestJson(std::string_view json);

}  // namespace eve::pixelworld
