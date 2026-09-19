#include <algorithm>
#include <string>
#include <vector>
#include "asset/CanonicalImageCook.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("asset.image.explicit_mip_cook_preserves_levels_and_rejects_malformed_chains") {
    const std::string prefix =
        R"({"schema":"eve.image","schemaVersion":3,"width":7,"height":3,"encoding":"rgba8-mips","color":{"transfer":"linear"},"mipCount":)";
    std::vector<uint8_t> pixels((7 * 3 + 3 + 1) * 4);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = uint8_t(i);
    auto cook = [&](const std::string& definition, std::span<const uint8_t> bytes, uint64_t budget) {
        return eve::asset::cookCanonicalImageRgba8(
            {reinterpret_cast<const uint8_t*>(definition.data()), definition.size()}, bytes, budget);
    };
    auto result = cook(prefix + "3}", pixels, pixels.size() + 28);
    REQUIRE(result.ok());
    const auto& bulk = result.value().bulk;
    REQUIRE(bulk.size() == pixels.size() + 28);
    CHECK(bulk[6] == 2);
    CHECK(bulk[8] == 7);
    CHECK(bulk[12] == 3);
    CHECK(bulk[20] == 3);
    CHECK(std::all_of(bulk.begin() + 24, bulk.begin() + 28, [](uint8_t v) { return v == 0; }));
    CHECK(std::equal(pixels.begin(), pixels.end(), bulk.begin() + 28));
    REQUIRE(!cook(prefix + "2}", pixels, 4096).ok());
    REQUIRE(!cook(prefix + "0}", pixels, 4096).ok());
    REQUIRE(!cook(prefix + "3}", std::span<const uint8_t>(pixels).first(pixels.size() - 1), 4096).ok());
    REQUIRE(!cook(prefix + "3}", pixels, pixels.size() + 27).ok());
    auto legacy = prefix;
    legacy.replace(legacy.find("\"schemaVersion\":3"), 17, "\"schemaVersion\":2");
    REQUIRE(!cook(legacy + "3}", pixels, 4096).ok());
    auto srgb = prefix;
    srgb.replace(srgb.find("linear"), 6, "srgb");
    auto srgbResult = cook(srgb + "3}", pixels, 4096);
    REQUIRE(srgbResult.ok());
    CHECK(srgbResult.value().bulk[28 + 1] != pixels[1]);
    CHECK_EQ(srgbResult.value().bulk[28 + 3], pixels[3]);
}
