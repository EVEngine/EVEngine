#include "graphics/sky/SkyWispsAsset.h"
#include <algorithm>
#include <fstream>
#include <optional>
#include "Fixtures.h"
#include "common/Value.h"
#include "filesystem/Filesystem.h"
#include "graphics/ShaderResources.h"
#include "graphics/sky/SkyWispsLayer.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.sky generated assets own data and use deterministic independent seeds") {
    using namespace eve::graphics;
    auto first = SkyWispsAsset::generate(2026), again = SkyWispsAsset::generate(2026),
         other = SkyWispsAsset::generate(17);
    REQUIRE(first.ok());
    REQUIRE(again.ok());
    REQUIRE(other.ok());
    const auto& a = first.value().layer();
    const auto& b = again.value().layer();
    REQUIRE(a.daylight.validate().ok());
    REQUIRE(a.daylightEnabled);
    REQUIRE(a.opticalCycleEnabled);
    REQUIRE(a.daylight.moonDiskColor[3] == 1);
    REQUIRE(std::equal(a.corners.begin(), a.corners.end(), b.corners.begin(), b.corners.end()));
    const auto equal = [](const auto* x, const auto* y) {
        return std::equal(x->bytes.begin(), x->bytes.end(), y->bytes.begin(), y->bytes.end());
    };
    REQUIRE(equal(a.densityTexture, b.densityTexture));
    REQUIRE(equal(a.starsTexture, b.starsTexture));
    REQUIRE(equal(a.moonColorTexture, b.moonColorTexture));
    REQUIRE(!equal(a.densityTexture, other.value().layer().densityTexture));
    REQUIRE(!equal(a.starsTexture, other.value().layer().starsTexture));
    REQUIRE(!equal(a.moonColorTexture, other.value().layer().moonColorTexture));
    REQUIRE(a.starsTexture->mipLevels == 11);
    REQUIRE(a.moonColorTexture->mipLevels == 9);
    REQUIRE(!a.moonColorTexture->sampler.repeatU);
    auto bad             = a.daylight;
    bad.moonDiskShape[0] = 0;
    REQUIRE(!bad.validate().ok());
    bad                     = a.daylight;
    bad.moonDiskLighting[2] = 30;
    REQUIRE(!bad.validate().ok());
    bad                 = a.daylight;
    bad.moonDiskGlow[1] = 0;
    REQUIRE(!bad.validate().ok());
}
