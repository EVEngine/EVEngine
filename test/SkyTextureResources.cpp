#include <array>
#include "graphics/ShaderResources.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::graphics;

TEST_CASE("graphics.sky volume layout shrinks all three mip dimensions without losing HDR bytes") {
    std::array<std::byte, 344> bytes{};
    ShaderImageInput           image;
    image.dimension = ShaderImageDimension::Image3D;
    image.format    = ShaderImageFormat::RGBA16Float;
    image.width     = 2;
    image.height    = 4;
    image.depth     = 4;
    image.mipLevels = 3;
    // 32 + 4 + 1 RGBA16F texels.
    image.bytes  = std::span(bytes).first(296);
    auto regions = shaderImageRegions(image);
    REQUIRE(regions.ok());
    REQUIRE(regions.value().size() == 3);
    REQUIRE(regions.value()[0].size == 256);
    REQUIRE(regions.value()[1].offset == 256);
    REQUIRE(regions.value()[1].depth == 2);
    REQUIRE(regions.value()[1].size == 32);
    REQUIRE(regions.value()[2].offset == 288);
    REQUIRE(regions.value()[2].size == 8);
    REQUIRE(regions.value()[2].depth == 1);
    image.bytes = std::span(bytes).first(295);
    REQUIRE(!shaderImageRegions(image).ok());
}

TEST_CASE("graphics.sky volume depth determines legal mip count") {
    std::array<std::byte, 120> bytes{};
    ShaderImageInput           image;
    image.dimension = ShaderImageDimension::Image3D;
    image.format    = ShaderImageFormat::RGBA16Unorm;
    image.width = image.height = 1;
    image.depth                = 8;
    image.mipLevels            = 4;
    image.bytes                = bytes;
    auto regions               = shaderImageRegions(image);
    REQUIRE(regions.ok());
    REQUIRE(regions.value()[3].offset == 112);
    image.dimension = ShaderImageDimension::Image2D;
    REQUIRE(!shaderImageRegions(image).ok());
    image.dimension = ShaderImageDimension::Image3D;
    image.layers    = 2;
    REQUIRE(!shaderImageRegions(image).ok());
    image.layers = 1;
    image.format = ShaderImageFormat::BC3;
    REQUIRE(!shaderImageRegions(image).ok());
}

TEST_CASE("graphics.sky float32 textures preserve full channel storage and reject oversized volumes") {
    std::array<std::byte, 32> bytes{};
    ShaderImageInput          image;
    image.format = ShaderImageFormat::RGBA32Float;
    image.width  = 2;
    image.height = 1;
    image.bytes  = bytes;
    auto regions = shaderImageRegions(image);
    REQUIRE(regions.ok());
    REQUIRE(regions.value()[0].size == 32);
    image.dimension = ShaderImageDimension::Image3D;
    image.width = image.height = image.depth = 2048;
    REQUIRE(!shaderImageRegions(image).ok());
}
