#include <array>
#include <cstring>
#include <limits>
#include "graphics/ShaderResources.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
