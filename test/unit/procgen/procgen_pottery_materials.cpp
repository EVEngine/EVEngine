#include "image/ImageData.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/PotteryTextures.h"
#include "zeroerr/assert.h"

#include <zeroerr/unittest.h>
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace eve::procgen;
namespace {
bool samePixels(const eve::image::ImageData& a, const eve::image::ImageData& b) {
    return a.getSize() == b.getSize() && std::memcmp(a.getData(), b.getData(), a.getSize()) == 0;
}
}  // namespace

TEST_CASE("procgen.pottery.seededPbrMapsAreAlignedDetailedAndRepeatable") {
    Params params;
    params.setSize(128, 128);
    params.setInt("colors", 64);
    params.setSeed(41);
    for (const auto& def : potteryTextureDefinitions()) {
        std::string error;
        auto        a         = generatePbrSet(def, params, error);
        auto        b         = generatePbrSet(def, params, error);
        Params      different = params;
        different.setSeed(42);
        auto c = generatePbrSet(def, different, error);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        REQUIRE(c != nullptr);
        for (auto* image : {a->albedo, a->normal, a->roughness, a->metallic, a->height, a->ao}) {
            REQUIRE(image != nullptr);
            CHECK_EQ(image->getWidth(), 128);
            CHECK_EQ(image->getHeight(), 128);
        }
        CHECK(samePixels(*a->albedo, *b->albedo));
        CHECK(samePixels(*a->normal, *b->normal));
        CHECK(samePixels(*a->roughness, *b->roughness));
        CHECK(samePixels(*a->height, *b->height));
        CHECK(samePixels(*a->ao, *b->ao));
        CHECK(!samePixels(*a->albedo, *c->albedo));
        CHECK(!samePixels(*a->normal, *c->normal));
        CHECK(!samePixels(*a->height, *c->height));
        const auto* normal  = static_cast<const unsigned char*>(a->normal->getData());
        const auto* metal   = static_cast<const unsigned char*>(a->metallic->getData());
        int         minimum = 255, maximum = 0;
        for (int i = 0; i < 128 * 128; ++i) {
            const float x = normal[i * 4] / 127.5f - 1.f;
            const float y = normal[i * 4 + 1] / 127.5f - 1.f;
            const float z = normal[i * 4 + 2] / 127.5f - 1.f;
            CHECK(std::abs(x * x + y * y + z * z - 1.f) < 0.02f);
            CHECK(z > 0.f);
            CHECK_EQ(metal[i * 4], 0);
            minimum = std::min(minimum, int(normal[i * 4]));
            maximum = std::max(maximum, int(normal[i * 4]));
        }
        CHECK(maximum - minimum > 2);
    }
}

TEST_CASE("procgen.pottery.detailFieldsTileWithoutCircumferenceSeams") {
    NoiseField noise;
    noise.seed    = 113;
    noise.periodX = 4;
    noise.periodY = 4;
    for (const auto& def : potteryTextureDefinitions()) {
        float minimum = 1.f, maximum = 0.f;
        for (int i = 0; i < 100; ++i) {
            const float u = float(i) / 31.f, v = float(i) / 37.f;
            const float h = def.height(u, v, noise);
            CHECK((std::isfinite(h) && h >= 0.f && h <= 1.f));
            CHECK(std::abs(h - def.height(u + 4.f, v, noise)) < 0.0002f);
            CHECK(std::abs(h - def.height(u, v + 4.f, noise)) < 0.0002f);
            minimum = std::min(minimum, h);
            maximum = std::max(maximum, h);
        }
        REQUIRE(maximum - minimum > 0.01f);
    }
}
