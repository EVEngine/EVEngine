#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>
#include "image/ImageData.h"
#include "procgen/texture/EverydayMaterials.h"
#include "procgen/texture/EverydayPatterns.h"
#include "zeroerr/unittest.h"
using namespace eve::procgen;

TEST_CASE("procgen.everyday.familiesDeterminismAndPhysicalChannels") {
    PbrRecipeRegistry::instance().registerPbrBuiltins();
    for (int i = 0; i < 13; ++i) {
        Params p;
        p.setSize(37, 29);
        p.setSeed(42);
        auto a = EverydayMaterials::generate(static_cast<EverydayMaterial>(i), p);
        auto b = EverydayMaterials::generate(static_cast<EverydayMaterial>(i), p);
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        auto                   first   = std::move(a).takeValue();
        auto                   second  = std::move(b).takeValue();
        eve::image::ImageData *maps[]  = {first->albedo,   first->normal, first->roughness,
                                          first->metallic, first->height, first->ao};
        eve::image::ImageData *other[] = {second->albedo,   second->normal, second->roughness,
                                          second->metallic, second->height, second->ao};
        const bool             metal   = i == 1 || i == 5 || i == 8 || i == 9;
        for (int m = 0; m < 6; ++m) {
            REQUIRE(maps[m]);
            CHECK(maps[m]->getWidth() == 37);
            CHECK(maps[m]->getHeight() == 29);
            CHECK(std::memcmp(maps[m]->getData(), other[m]->getData(), 37u * 29u * 4u) == 0);
        }
        const auto *metallic = static_cast<const unsigned char *>(first->metallic->getData());
        for (size_t j = 0; j < 37u * 29u; ++j) CHECK(metallic[j * 4] == (metal ? 255 : 0));
        CHECK(first->transmission == (i == 6 ? .96f : 0.f));
    }
}

TEST_CASE("procgen.everyday.boundedParametersAndRegistryParity") {
    Params p;
    p.setSize(17, 11);
    p.setFloat("scale", std::numeric_limits<float>::infinity());
    p.setFloat("detail", -1e30f);
    p.setFloat("finish", 1e30f);
    p.setFloat("variation", std::numeric_limits<float>::quiet_NaN());
    auto a = EverydayMaterials::wood(p);
    REQUIRE(a.ok());
    auto  direct   = std::move(a).takeValue();
    auto &registry = PbrRecipeRegistry::instance();
    registry.registerPbrBuiltins();
    std::string error;
    auto        viaRegistry = registry.generate("pbr.everyday.wood", p, error);
    REQUIRE(viaRegistry);
    CHECK(error.empty());
    CHECK(std::memcmp(direct->albedo->getData(), viaRegistry->albedo->getData(), 17u * 11u * 4u) == 0);
    p.setSize(2049, 1);
    auto tooBig = EverydayMaterials::glass(p);
    CHECK(!tooBig.ok());
    p.setSize(1, 1);
    auto tiny = EverydayMaterials::paper(p);
    CHECK(tiny.ok());
    auto invalid = EverydayMaterials::generate(static_cast<EverydayMaterial>(999), p);
    CHECK(!invalid.ok());
}

TEST_CASE("procgen.everyday.pigmentDoesNotEmbossMarble") {
    Params p;
    p.setSize(64, 64);
    auto result = EverydayMaterials::marble(p);
    REQUIRE(result.ok());
    auto        maps   = std::move(result).takeValue();
    const auto *height = static_cast<const unsigned char *>(maps->height->getData());
    int         lo = 255, hi = 0;
    for (size_t i = 0; i < 4096; ++i) {
        lo = std::min(lo, int(height[i * 4]));
        hi = std::max(hi, int(height[i * 4]));
    }
    CHECK(hi - lo < 5);
}

TEST_CASE("procgen.everyday.seedAndTintHaveObservableEffects") {
    static_assert(!std::is_copy_constructible_v<PbrTextureSet>);
    for (int family = 0; family < 13; ++family) {
        Params p;
        p.setSize(43, 35);
        p.setSeed(1);
        auto a = EverydayMaterials::generate(static_cast<EverydayMaterial>(family), p);
        REQUIRE(a.ok());
        auto original = std::move(a).takeValue();
        p.setSeed(2);
        auto b = EverydayMaterials::generate(static_cast<EverydayMaterial>(family), p);
        REQUIRE(b.ok());
        auto changed = std::move(b).takeValue();
        CHECK(std::memcmp(original->albedo->getData(), changed->albedo->getData(), 43u * 35u * 4u) != 0);
        p.setFloat("tintR", 0.f);
        auto c = EverydayMaterials::generate(static_cast<EverydayMaterial>(family), p);
        REQUIRE(c.ok());
        auto tinted = std::move(c).takeValue();
        CHECK(std::memcmp(tinted->albedo->getData(), changed->albedo->getData(), 43u * 35u * 4u) != 0);
        CHECK(std::memcmp(tinted->normal->getData(), changed->normal->getData(), 43u * 35u * 4u) == 0);
    }
}

TEST_CASE("procgen.everyday.sharedCavityMatchesReference") {
    for (bool seamless : {false, true}) {
        for (int size : {1, 7, 129}) {
            Params p;
            p.setSize(size, std::max(1, size - 3));
            p.setBool("seamless", seamless);
            TextureRecipeDef def;
            def.height         = [](float u, float v, const NoiseField &n) { return n.valueNoise(u, v); };
            def.pbr.aoStrength = 3.f;
            std::string error;
            auto        maps = generatePbrSet(def, p, error);
            REQUIRE(maps);
            const auto         ctx = TextureGenContext::fromParams(p);
            std::vector<float> heights;
            fillHeightField(ctx, def.height, heights);
            const auto *pixels = static_cast<const unsigned char *>(maps->ao->getData());
            const int   radius = std::max(1, std::min(ctx.width, ctx.height) / 64);
            for (int y = 0; y < ctx.height; ++y) {
                for (int x = 0; x < ctx.width; ++x) {
                    double sum   = 0.0;
                    int    count = 0;
                    for (int dy = -radius; dy <= radius; ++dy) {
                        for (int dx = -radius; dx <= radius; ++dx) {
                            int xx = x + dx, yy = y + dy;
                            if (seamless) {
                                xx = (xx % ctx.width + ctx.width) % ctx.width;
                                yy = (yy % ctx.height + ctx.height) % ctx.height;
                            } else if (xx < 0 || yy < 0 || xx >= ctx.width || yy >= ctx.height)
                                continue;
                            sum += heights[size_t(yy * ctx.width + xx)];
                            ++count;
                        }
                    }
                    const size_t i  = size_t(y * ctx.width + x);
                    const float  ao = std::clamp(1.f - 3.f * std::max(0.f, float(sum / count) - heights[i]), 0.f, 1.f);
                    CHECK(std::abs(int(pixels[i * 4]) - int(std::lround(ao * 255.f))) <= 1);
                }
            }
        }
    }
}

TEST_CASE("procgen.everyday.microstructureIsPeriodicAtBothSeams") {
    for (int family = 0; family < 13; ++family)
        for (int bandwidth : {7, 21, 171, 341}) {
            for (int j = 0; j <= 32; ++j) {
                const float t  = float(j) / 32.f;
                const auto  f  = static_cast<EverydayMaterial>(family);
                const auto  x0 = detail::sampleEveryday(f, 0.f, t, 42, 4, bandwidth);
                const auto  x1 = detail::sampleEveryday(f, 1.f, t, 42, 4, bandwidth);
                const auto  y0 = detail::sampleEveryday(f, t, 0.f, 42, 4, bandwidth);
                const auto  y1 = detail::sampleEveryday(f, t, 1.f, 42, 4, bandwidth);
                CHECK(std::abs(x0.pigment - x1.pigment) < .001f);
                CHECK(std::abs(x0.relief - x1.relief) < .001f);
                CHECK(std::abs(x0.roughness - x1.roughness) < .001f);
                CHECK(std::abs(y0.pigment - y1.pigment) < .001f);
                CHECK(std::abs(y0.relief - y1.relief) < .001f);
                CHECK(std::abs(y0.roughness - y1.roughness) < .001f);
            }
        }
}

TEST_CASE("procgen.everyday.resolutionDoesNotRescaleFeatures") {
    for (auto family :
         {EverydayMaterial::Wood, EverydayMaterial::Metal, EverydayMaterial::Aluminum, EverydayMaterial::Marble}) {
        Params p;
        p.setSize(256, 256);
        p.setSeed(42);
        p.setFloat("scale", 1.f);
        auto a = EverydayMaterials::generate(family, p);
        REQUIRE(a.ok());
        auto small = std::move(a).takeValue();
        p.setSize(512, 512);
        auto b = EverydayMaterials::generate(family, p);
        REQUIRE(b.ok());
        auto                   large = std::move(b).takeValue();
        eve::image::ImageData *lo[]  = {small->albedo, small->height, small->roughness};
        eve::image::ImageData *hi[]  = {large->albedo, large->height, large->roughness};
        for (int m = 0; m < 3; ++m)
            for (int y = 0; y < 256; y += 7)
                for (int x = 0; x < 256; x += 7) {
                    const auto *first = static_cast<const unsigned char *>(lo[m]->getData()) + (y * 256 + x) * 4;
                    const auto *second =
                        static_cast<const unsigned char *>(hi[m]->getData()) + (y * 2 * 512 + x * 2) * 4;
                    CHECK(std::memcmp(first, second, 4) == 0);
                }
    }
}

TEST_CASE("procgen.everyday.requestedSurfacesHaveReliefAndRoughness") {
    for (auto family : {EverydayMaterial::Paint, EverydayMaterial::Cloth, EverydayMaterial::CastIron,
                        EverydayMaterial::Rubber, EverydayMaterial::Cement}) {
        Params p;
        p.setSize(512, 512);
        p.setSeed(42);
        auto result = EverydayMaterials::generate(family, p);
        REQUIRE(result.ok());
        auto        maps      = std::move(result).takeValue();
        const auto *normal    = static_cast<const unsigned char *>(maps->normal->getData());
        const auto *height    = static_cast<const unsigned char *>(maps->height->getData());
        const auto *roughness = static_cast<const unsigned char *>(maps->roughness->getData());
        double      slope = 0., rough = 0.;
        int         raised = 0, lowered = 0;
        for (int i = 0; i < 512 * 512; ++i) {
            const double x = normal[i * 4] / 127.5 - 1., y = normal[i * 4 + 1] / 127.5 - 1.,
                         z = normal[i * 4 + 2] / 127.5 - 1.;
            slope += std::atan2(std::sqrt(x * x + y * y), z) * 57.2957795;
            rough += roughness[i * 4];
            raised += height[i * 4] > 128;
            lowered += height[i * 4] < 127;
        }
        slope /= 512 * 512;
        rough /= 512 * 512;
        CHECK(slope > (family == EverydayMaterial::Paint ? 2. : 5.));
        if (family == EverydayMaterial::CastIron) CHECK(rough > 210.);
        if (family == EverydayMaterial::Rubber) CHECK(rough > 230.);
        if (family == EverydayMaterial::Cement) {
            CHECK(raised > 512 * 512 / 10);
            CHECK(lowered > 512 * 512 / 10);
        }
    }
}
