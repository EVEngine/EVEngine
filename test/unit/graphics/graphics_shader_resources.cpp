#include <array>
#include <cstring>
#include <limits>
#include "graphics/ShaderResources.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// ShaderInstances.cpp
namespace {
namespace ShaderInstances {
TEST_CASE("graphics.shader instance matrices reject malformed nonfinite and singular records") {
    std::array<float, 16> matrix{1, 0, 0, 0, 0, 2, 0, 0, 0, 0, -3, 0, 100, 50, -20, 1};
    auto                  bytes = std::as_bytes(std::span(matrix));
    auto                  valid = eve::graphics::shaderInstanceMatrixCount(bytes);
    REQUIRE(valid.ok());
    CHECK_EQ(valid.value(), 1u);
    REQUIRE(eve::graphics::shaderInstanceMatrixCount({}).ok());
    CHECK(!eve::graphics::shaderInstanceMatrixCount(bytes.first(63)).ok());
    matrix[0] = 0;
    CHECK(!eve::graphics::shaderInstanceMatrixCount(bytes).ok());
    matrix[0] = 1;
    matrix[3] = 1;
    CHECK(!eve::graphics::shaderInstanceMatrixCount(bytes).ok());
    matrix[3]  = 0;
    matrix[12] = std::numeric_limits<float>::infinity();
    CHECK(!eve::graphics::shaderInstanceMatrixCount(bytes).ok());
    matrix[12] = 100;
    std::array<std::byte, 65> unaligned;
    std::memcpy(unaligned.data() + 1, matrix.data(), 64);
    REQUIRE(eve::graphics::shaderInstanceMatrixCount(std::span(unaligned).subspan(1)).ok());
}
}  // namespace ShaderInstances
}  // namespace

// ShaderResources.cpp
namespace {
namespace ShaderResources {
TEST_CASE("graphics.shader resources preserve BC3 tail mips and layer boundaries") {
    std::array<std::byte, 160>      bytes{};
    eve::graphics::ShaderImageInput image;
    image.width     = 8;
    image.height    = 4;
    image.layers    = 2;
    image.mipLevels = 4;
    image.dimension = eve::graphics::ShaderImageDimension::Array2D;
    image.format    = eve::graphics::ShaderImageFormat::BC3;
    image.bytes     = bytes;
    auto result     = eve::graphics::shaderImageRegions(image);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 8);
    REQUIRE(result.value()[0].size == 32);
    REQUIRE(result.value()[3].size == 16);
    REQUIRE(result.value()[4].offset == 80);
    REQUIRE(result.value()[7].offset + result.value()[7].size == bytes.size());
    image.format = eve::graphics::ShaderImageFormat::BC7Srgb;
    auto bc7     = eve::graphics::shaderImageRegions(image);
    REQUIRE(bc7.ok());
    REQUIRE(bc7.value()[4].offset == 80);
    image.bytes = std::span(bytes).first(159);
    REQUIRE(!eve::graphics::shaderImageRegions(image).ok());
}

TEST_CASE("graphics.shader resources reject invalid dimensions and preserve R16 byte width") {
    std::array<std::byte, 4>        bytes{};
    eve::graphics::ShaderImageInput image;
    image.width  = 2;
    image.height = 1;
    image.format = eve::graphics::ShaderImageFormat::R16;
    image.bytes  = bytes;
    auto result  = eve::graphics::shaderImageRegions(image);
    REQUIRE(result.ok());
    REQUIRE(result.value()[0].size == 4);
    image.mipLevels = 3;
    REQUIRE(!eve::graphics::shaderImageRegions(image).ok());
    image.mipLevels = 1;
    image.dimension = eve::graphics::ShaderImageDimension::Cube;
    REQUIRE(!eve::graphics::shaderImageRegions(image).ok());
    image.dimension = eve::graphics::ShaderImageDimension::Image2D;
    image.width     = 0;
    REQUIRE(!eve::graphics::shaderImageRegions(image).ok());
}
}  // namespace ShaderResources
}  // namespace
