#pragma once
#include "graphics/ShaderResources.h"
#include "graphics/sky/SkyWispsAsset.h"
#include "graphics/sky/SkyWispsLayer.h"
namespace eve::graphics {
struct SkyWispsAsset::Impl {
    std::vector<float>                    corners;
    std::vector<std::byte>                pixels;
    ShaderImageInput                      texture;
    std::array<std::vector<std::byte>, 4> celestialPixels;
    std::array<ShaderImageInput, 4>       celestialImages;
    SkyWispsLayer                         layer;
};
}  // namespace eve::graphics
