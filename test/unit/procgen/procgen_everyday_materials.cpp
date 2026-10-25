#include "image/ImageData.h"
#include "procgen/texture/EverydayMaterials.h"
#include "procgen/texture/EverydayPatterns.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

using namespace eve::procgen;

namespace {
constexpr int familyCount = 13;

Params parameters(int width = 37, int height = 29, std::uint32_t seed = 42) {
    Params value;
    value.setSize(width, height);
    value.setSeed(seed);
    return value;
}

bool samePixels(const eve::image::ImageData* first, const eve::image::ImageData* second) {
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE_EQ(first->getWidth(), second->getWidth());
    REQUIRE_EQ(first->getHeight(), second->getHeight());
    const auto count = static_cast<std::size_t>(first->getWidth() * first->getHeight() * 4);
    return std::memcmp(first->getData(), second->getData(), count) == 0;
}
}  // namespace

TEST_CASE("Everyday material generation determinism contract") {
    SUB_CASE("everyday material contract: every family returns six aligned owning maps") {
        static_assert(!std::is_copy_constructible_v<PbrTextureSet>);
        for (int index = 0; index < familyCount; ++index) {
            auto generated = EverydayMaterials::generate(static_cast<EverydayMaterial>(index), parameters());
            REQUIRE(generated.ok());
            auto                         maps       = std::move(generated).takeValue();
            const eve::image::ImageData* channels[] = {maps->albedo,   maps->normal, maps->roughness,
                                                       maps->metallic, maps->height, maps->ao};
            for (const auto* channel : channels) {
                REQUIRE(channel != nullptr);
                REQUIRE_EQ(channel->getWidth(), 37);
                REQUIRE_EQ(channel->getHeight(), 29);
            }
            const bool  expectedMetal = index == 1 || index == 5 || index == 8 || index == 9;
            const auto* metallic      = static_cast<const unsigned char*>(maps->metallic->getData());
            for (std::size_t pixel = 0; pixel < 37u * 29u; ++pixel)
                REQUIRE_EQ(metallic[pixel * 4], expectedMetal ? 255 : 0);
            REQUIRE_EQ(maps->transmission, index == 6 ? .96f : 0.f);
        }
    };

    SUB_CASE("everyday material contract: same seed and effective parameters reproduce every map byte") {
        for (int index = 0; index < familyCount; ++index) {
            auto a = EverydayMaterials::generate(static_cast<EverydayMaterial>(index), parameters());
            auto b = EverydayMaterials::generate(static_cast<EverydayMaterial>(index), parameters());
            REQUIRE(a.ok());
            REQUIRE(b.ok());
            auto first  = std::move(a).takeValue();
            auto second = std::move(b).takeValue();
            REQUIRE(samePixels(first->albedo, second->albedo));
            REQUIRE(samePixels(first->normal, second->normal));
            REQUIRE(samePixels(first->roughness, second->roughness));
            REQUIRE(samePixels(first->metallic, second->metallic));
            REQUIRE(samePixels(first->height, second->height));
            REQUIRE(samePixels(first->ao, second->ao));
        }
    };

    SUB_CASE("everyday material contract: seed changes pigment and tint changes only color channels") {
        for (int index = 0; index < familyCount; ++index) {
            auto original    = EverydayMaterials::generate(static_cast<EverydayMaterial>(index), parameters(43, 35, 1));
            auto changedSeed = EverydayMaterials::generate(static_cast<EverydayMaterial>(index), parameters(43, 35, 2));
            REQUIRE(original.ok());
            REQUIRE(changedSeed.ok());
            REQUIRE(!samePixels(original.value()->albedo, changedSeed.value()->albedo));
            auto tintedParams = parameters(43, 35, 2);
            tintedParams.setFloat("tintR", 0.f);
            auto tinted = EverydayMaterials::generate(static_cast<EverydayMaterial>(index), tintedParams);
            REQUIRE(tinted.ok());
            REQUIRE(!samePixels(tinted.value()->albedo, changedSeed.value()->albedo));
            REQUIRE(samePixels(tinted.value()->normal, changedSeed.value()->normal));
            REQUIRE(samePixels(tinted.value()->height, changedSeed.value()->height));
        }
    };
}

TEST_CASE("Everyday material bounds seams and scale contract") {
    SUB_CASE("everyday material contract: invalid family or oversized dimensions fail and finite controls clamp") {
        auto invalid = EverydayMaterials::generate(static_cast<EverydayMaterial>(999), parameters(1, 1));
        REQUIRE(!invalid.ok());
        REQUIRE(!EverydayMaterials::glass(parameters(2049, 1)).ok());
        REQUIRE(EverydayMaterials::paper(parameters(1, 1)).ok());

        auto controls = parameters(17, 11);
        controls.setFloat("scale", std::numeric_limits<float>::infinity());
        controls.setFloat("detail", -1e30f);
        controls.setFloat("finish", 1e30f);
        controls.setFloat("variation", std::numeric_limits<float>::quiet_NaN());
        auto direct  = EverydayMaterials::wood(controls);
        auto generic = EverydayMaterials::generate(EverydayMaterial::Wood, controls);
        REQUIRE(direct.ok());
        REQUIRE(generic.ok());
        REQUIRE(samePixels(direct.value()->albedo, generic.value()->albedo));
    };

    SUB_CASE("everyday material contract: direct family samples meet both periodic boundaries") {
        for (int family = 0; family < familyCount; ++family) {
            for (int bandwidth : {7, 21, 171, 341}) {
                for (int step = 0; step <= 32; ++step) {
                    const float t      = float(step) / 32.f;
                    const auto  type   = static_cast<EverydayMaterial>(family);
                    const auto  left   = detail::sampleEveryday(type, 0.f, t, 42, 4, bandwidth);
                    const auto  right  = detail::sampleEveryday(type, 1.f, t, 42, 4, bandwidth);
                    const auto  top    = detail::sampleEveryday(type, t, 0.f, 42, 4, bandwidth);
                    const auto  bottom = detail::sampleEveryday(type, t, 1.f, 42, 4, bandwidth);
                    REQUIRE(std::abs(left.pigment - right.pigment) < .001f);
                    REQUIRE(std::abs(left.relief - right.relief) < .001f);
                    REQUIRE(std::abs(left.roughness - right.roughness) < .001f);
                    REQUIRE(std::abs(top.pigment - bottom.pigment) < .001f);
                    REQUIRE(std::abs(top.relief - bottom.relief) < .001f);
                    REQUIRE(std::abs(top.roughness - bottom.roughness) < .001f);
                }
            }
        }
    };
}

TEST_CASE("Everyday material registry and physical surface contract") {
    SUB_CASE("everyday material contract: marble pigment does not create a large relief range") {
        auto generated = EverydayMaterials::marble(parameters(64, 64));
        REQUIRE(generated.ok());
        const auto* height  = static_cast<const unsigned char*>(generated.value()->height->getData());
        int         minimum = 255;
        int         maximum = 0;
        for (std::size_t pixel = 0; pixel < 64u * 64u; ++pixel) {
            minimum = std::min(minimum, int(height[pixel * 4]));
            maximum = std::max(maximum, int(height[pixel * 4]));
        }
        REQUIRE(maximum - minimum < 5);
    };

    SUB_CASE("everyday material contract: registry recipe and direct factory produce the same wood") {
        auto params = parameters(17, 11);
        auto direct = EverydayMaterials::wood(params);
        REQUIRE(direct.ok());
        auto& registry = PbrRecipeRegistry::instance();
        registry.registerPbrBuiltins();
        std::string error;
        auto        registered = registry.generate("pbr.everyday.wood", params, error);
        REQUIRE(registered != nullptr);
        REQUIRE(error.empty());
        REQUIRE(samePixels(direct.value()->albedo, registered->albedo));
        REQUIRE(samePixels(direct.value()->normal, registered->normal));
    };

    SUB_CASE("everyday material contract: physical surface classes have measurable roughness and relief") {
        for (auto family : {EverydayMaterial::Paint, EverydayMaterial::Cloth, EverydayMaterial::CastIron,
                            EverydayMaterial::Rubber, EverydayMaterial::Cement}) {
            auto generated = EverydayMaterials::generate(family, parameters(512, 512));
            REQUIRE(generated.ok());
            const auto* normal        = static_cast<const unsigned char*>(generated.value()->normal->getData());
            const auto* roughness     = static_cast<const unsigned char*>(generated.value()->roughness->getData());
            const auto* height        = static_cast<const unsigned char*>(generated.value()->height->getData());
            double      meanSlope     = 0.;
            double      meanRoughness = 0.;
            int         raised        = 0;
            int         lowered       = 0;
            for (int pixel = 0; pixel < 512 * 512; ++pixel) {
                const double nx = normal[pixel * 4] / 127.5 - 1.;
                const double ny = normal[pixel * 4 + 1] / 127.5 - 1.;
                const double nz = normal[pixel * 4 + 2] / 127.5 - 1.;
                meanSlope += std::atan2(std::sqrt(nx * nx + ny * ny), nz) * 57.2957795;
                meanRoughness += roughness[pixel * 4];
                raised += height[pixel * 4] > 128;
                lowered += height[pixel * 4] < 127;
            }
            meanSlope /= 512 * 512;
            meanRoughness /= 512 * 512;
            REQUIRE(meanSlope > (family == EverydayMaterial::Paint ? 2. : 5.));
            if (family == EverydayMaterial::CastIron) REQUIRE(meanRoughness > 210.);
            if (family == EverydayMaterial::Rubber) REQUIRE(meanRoughness > 230.);
            if (family == EverydayMaterial::Cement) {
                REQUIRE(raised > 512 * 512 / 10);
                REQUIRE(lowered > 512 * 512 / 10);
            }
        }
    };
}
