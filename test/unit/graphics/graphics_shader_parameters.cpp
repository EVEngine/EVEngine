#include <array>
#include <limits>
#include "graphics/Shader.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.shader.floatBlockIsAtomic") {
    eve::graphics::Shader shader;
    shader.declareVec4("parameters");
    const std::array<float, 4> valid{1, 2, 3, 4};
    REQUIRE(shader.sendFloatBlock(valid).ok());
    auto invalid = valid;
    invalid[2]   = std::numeric_limits<float>::quiet_NaN();
    CHECK(!shader.sendFloatBlock(invalid).ok());
    CHECK(!shader.sendFloatBlock(std::span<const float>(valid.data(), 3)).ok());
    for (unsigned i = 0; i < 4; ++i) CHECK(shader.pushConstantData()[i] == valid[i]);
}
