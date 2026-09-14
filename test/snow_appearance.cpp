#include "snow/SnowField.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("SnowField.compressedSnowRemainsSnow") {
    eve::snow::SnowField field(8, 8);
    field.fill(0.4f);
    const auto compressed = field.toAlbedoRGBA();
    REQUIRE(compressed[0] > 180);
    REQUIRE(compressed[2] > compressed[0]);
    field.fill(0.f);
    const auto exposed = field.toAlbedoRGBA();
    REQUIRE(exposed[0] < 110);
    REQUIRE(exposed[2] < exposed[0]);
    field.fill(0.85f);
    const auto fresh = field.toAlbedoRGBA();
    REQUIRE(fresh[0] > compressed[0]);
}
