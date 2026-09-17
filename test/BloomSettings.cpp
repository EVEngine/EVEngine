#include <limits>
#include "graphics/Bloom.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.bloom rejects invalid reconstruction settings") {
    using namespace eve::graphics;
    BloomFilterSettings settings;
    REQUIRE(validateBloomFilterSettings(settings).ok());
    settings.filter = BloomFilter::GaussianScatter;
    REQUIRE(validateBloomFilterSettings(settings).ok());
    settings.scatter = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!validateBloomFilterSettings(settings).ok());
    settings.scatter = 1.01f;
    REQUIRE(!validateBloomFilterSettings(settings).ok());
    settings.scatter       = 0.f;
    settings.maxIterations = 0;
    REQUIRE(!validateBloomFilterSettings(settings).ok());
    settings.maxIterations = 17;
    REQUIRE(!validateBloomFilterSettings(settings).ok());
    settings.maxIterations = 1;
    settings.clamp         = std::numeric_limits<float>::infinity();
    REQUIRE(!validateBloomFilterSettings(settings).ok());
    settings.clamp = 0.f;
    REQUIRE(!validateBloomFilterSettings(settings).ok());
    settings.clamp = 65504.f;
    REQUIRE(validateBloomFilterSettings(settings).ok());
    settings.filter = static_cast<BloomFilter>(99);
    REQUIRE(!validateBloomFilterSettings(settings).ok());
}
