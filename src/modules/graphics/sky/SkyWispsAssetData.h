#pragma once
#include "graphics/ShaderResources.h"
#include "graphics/sky/SkyWispsAsset.h"
#include "graphics/sky/SkyWispsLayer.h"
namespace eve::graphics {
struct SkyWispsAsset::Impl {
    /** @brief Internal owned triangle data.
     * @cost Storage scales with corner count; allocate only during asset preparation. */
    std::vector<float> corners;
    /** @brief Internal owned density pixels.
     * @cost Storage scales with texture area; allocate only during asset preparation. */
    std::vector<std::byte>                pixels;
    ShaderImageInput                      texture;
    std::array<std::vector<std::byte>, 4> celestialPixels;
    std::array<ShaderImageInput, 4>       celestialImages;
    SkyWispsLayer                         layer;
};
}  // namespace eve::graphics
