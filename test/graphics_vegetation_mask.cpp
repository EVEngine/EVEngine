#include <cmath>
#include <limits>
#include "graphics/VegetationField.h"
#include "graphics/VegetationMaskPacking.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::graphics;
TEST_CASE("graphics.vegetation.mask_orm_uses_green_ao_alpha_smoothness_and_retains_blue") {
    VegetationMask source{2, 1, {glm::vec4(.9f, .2f, .4f, .7f), glm::vec4(.1f, .8f, .6f, .3f)}};
    auto           encoded = packVegetationOrm(source, .5f, .8f);
    REQUIRE(encoded.ok());
    REQUIRE_EQ(encoded.value().width, 2u);
    const auto a = encoded.value().pixels[0];
    REQUIRE(std::abs(a.r - .6f) < 1e-6f);
    REQUIRE(std::abs(a.g - .44f) < 1e-6f);
    REQUIRE_EQ(a.b, 0.f);
    REQUIRE_EQ(a.a, .4f);
    REQUIRE(std::abs(encoded.value().pixels[1].r - .9f) < 1e-6f);
    REQUIRE(std::abs(encoded.value().pixels[1].g - .76f) < 1e-6f);
    auto disabled = packVegetationOrm(source, 0, 0);
    REQUIRE(disabled.ok());
    REQUIRE_EQ(disabled.value().pixels[0], glm::vec4(1, 1, 0, .4f));
    source.pixels[0] = glm::vec4(0);
    REQUIRE_EQ(encoded.value().pixels[0], a);
}
TEST_CASE("graphics.vegetation.mask_orm_rejects_malformed_linear_inputs") {
    VegetationMask source{1, 1, {glm::vec4(1)}};
    REQUIRE(!packVegetationOrm(source, -.1f, 1).ok());
    REQUIRE(!packVegetationOrm(source, 1, 1.1f).ok());
    source.width = 2;
    REQUIRE(!packVegetationOrm(source, 1, 1).ok());
    source.width       = 1;
    source.pixels[0].g = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!packVegetationOrm(source, 1, 1).ok());
    source.width  = UINT32_MAX;
    source.height = UINT32_MAX;
    REQUIRE(!packVegetationOrm(source, 1, 1).ok());
}
