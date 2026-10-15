#include <zeroerr/unittest.h>

#include "filesystem/FileData.h"
#include "image/ImageData.h"
#include "map/DualGrid.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>

using namespace eve::map;

namespace {

[[maybe_unused]] void writePreviewBmp(const DualGridRgbaImage& image, const char* path) {
    std::ofstream out(path, std::ios::binary);
    REQUIRE(out.good());
    const uint32_t pixelBytes = uint32_t(image.width * image.height * 4);
    const uint32_t fileBytes  = 54u + pixelBytes;
    const uint8_t  header[54] = {'B',
                                 'M',
                                 uint8_t(fileBytes),
                                 uint8_t(fileBytes >> 8),
                                 uint8_t(fileBytes >> 16),
                                 uint8_t(fileBytes >> 24),
                                 0,
                                 0,
                                 0,
                                 0,
                                 54,
                                 0,
                                 0,
                                 0,
                                 40,
                                 0,
                                 0,
                                 0,
                                 uint8_t(image.width),
                                 uint8_t(image.width >> 8),
                                 uint8_t(image.width >> 16),
                                 uint8_t(image.width >> 24),
                                 uint8_t(image.height),
                                 uint8_t(image.height >> 8),
                                 uint8_t(image.height >> 16),
                                 uint8_t(image.height >> 24),
                                 1,
                                 0,
                                 32,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 uint8_t(pixelBytes),
                                 uint8_t(pixelBytes >> 8),
                                 uint8_t(pixelBytes >> 16),
                                 uint8_t(pixelBytes >> 24),
                                 0x13,
                                 0x0b,
                                 0,
                                 0,
                                 0x13,
                                 0x0b,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0};
    out.write(reinterpret_cast<const char*>(header), sizeof(header));
    for (int y = image.height - 1; y >= 0; --y) {
        for (int x = 0; x < image.width; ++x) {
            const size_t  p       = size_t((y * image.width + x) * 4);
            const uint8_t bgra[4] = {image.pixels[p + 2], image.pixels[p + 1], image.pixels[p], image.pixels[p + 3]};
            out.write(reinterpret_cast<const char*>(bgra), 4);
        }
    }
    REQUIRE(out.good());
}

[[maybe_unused]] DualGridRgbaImage loadAtlasTile(const char* path, int tileX, int tileY) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    std::vector<uint8_t>      bytes{std::istreambuf_iterator<char>(input), {}};
    eve::filesystem::FileData encoded(path, bytes.size());
    std::memcpy(encoded.getData(), bytes.data(), bytes.size());
    eve::image::ImageData atlas(&encoded);
    REQUIRE_EQ(atlas.getFormat(), std::string("RGBA8"));
    REQUIRE(atlas.getWidth() >= (tileX + 1) * 256);
    REQUIRE(atlas.getHeight() >= (tileY + 1) * 128);

    DualGridRgbaImage tile{256, 128, std::vector<uint8_t>(256u * 128u * 4u)};
    const auto*       source = static_cast<const uint8_t*>(atlas.getData());
    for (int y = 0; y < tile.height; ++y)
        for (int x = 0; x < tile.width; ++x) {
            const size_t src = size_t(((tileY * 128 + y) * atlas.getWidth() + tileX * 256 + x) * 4);
            const size_t dst = size_t((y * tile.width + x) * 4);
            std::memcpy(tile.pixels.data() + dst, source + src, 4);
            const float diamond = std::abs((float(x) + 0.5f) / 128.f - 1.f) + std::abs((float(y) + 0.5f) / 64.f - 1.f);
            if (diamond > 1.f) {
                tile.pixels[dst] = tile.pixels[dst + 1] = tile.pixels[dst + 2] = 0;
                tile.pixels[dst + 3]                                           = 0;
            }
        }
    return tile;
}

[[maybe_unused]] void compositeTile(DualGridRgbaImage& canvas, const DualGridRgbaImage& atlas, int frame, int dstX, int dstY,
                   int scaleDivisor = 1) {
    constexpr int tileWidth    = 256;
    constexpr int tileHeight   = 128;
    const int     sourceX      = (frame % 4) * tileWidth;
    const int     sourceY      = (frame / 4) * tileHeight;
    const int     outputWidth  = tileWidth / scaleDivisor;
    const int     outputHeight = tileHeight / scaleDivisor;
    for (int y = 0; y < outputHeight; ++y)
        for (int x = 0; x < outputWidth; ++x) {
            const int px = dstX + x;
            const int py = dstY + y;
            if (px < 0 || py < 0 || px >= canvas.width || py >= canvas.height) continue;
            const size_t src   = size_t(((sourceY + y * scaleDivisor) * atlas.width + sourceX + x * scaleDivisor) * 4);
            const size_t dst   = size_t((py * canvas.width + px) * 4);
            const int    alpha = atlas.pixels[src + 3];
            for (int channel = 0; channel < 3; ++channel) {
                canvas.pixels[dst + channel] = uint8_t(
                    (int(atlas.pixels[src + channel]) * alpha + int(canvas.pixels[dst + channel]) * (255 - alpha)) /
                    255);
            }
            canvas.pixels[dst + 3] = 255;
        }
}

[[maybe_unused]] DualGridRgbaImage blendMaterialTile(const std::array<DualGridRgbaImage, 3>& materials, const DualGridMaskAtlas& masks,
                                    const std::array<int, 4>& corners) {
    DualGridRgbaImage tile{masks.width, masks.height, std::vector<uint8_t>(size_t(masks.width * masks.height * 4))};
    int               materialMasks[3]{};
    for (int material = 0; material < 3; ++material) {
        materialMasks[material] = dualGridMaskFromCorners(corners[0] == material, corners[1] == material,
                                                          corners[2] == material, corners[3] == material);
    }
    for (int y = 0; y < masks.height; ++y)
        for (int x = 0; x < masks.width; ++x) {
            float weights[3]{};
            float sum = 0.f;
            for (int material = 0; material < 3; ++material) {
                weights[material] = masks.coverageAt(materialMasks[material], x, y);
                sum += weights[material];
            }
            if (sum <= 0.0001f) {
                const float u                = (float(x) + 0.5f) / float(masks.width);
                const float v                = (float(y) + 0.5f) / float(masks.height);
                const float cornerWeights[4] = {(1.f - u) * (1.f - v), u * (1.f - v), (1.f - u) * v, u * v};
                for (int corner = 0; corner < 4; ++corner) weights[corners[corner]] += cornerWeights[corner];
                sum = 1.f;
            }
            const size_t p = size_t((y * masks.width + x) * 4);
            for (int channel = 0; channel < 4; ++channel) {
                float value = 0.f;
                for (int material = 0; material < 3; ++material)
                    value += float(materials[material].pixels[p + channel]) * weights[material] / sum;
                tile.pixels[p + channel] = uint8_t(std::clamp(value, 0.f, 255.f));
            }
        }
    return tile;
}

[[maybe_unused]] void compositeImage(DualGridRgbaImage& canvas, const DualGridRgbaImage& tile, int dstX, int dstY, int scaleDivisor) {
    for (int y = 0; y < tile.height / scaleDivisor; ++y)
        for (int x = 0; x < tile.width / scaleDivisor; ++x) {
            const int px = dstX + x;
            const int py = dstY + y;
            if (px < 0 || py < 0 || px >= canvas.width || py >= canvas.height) continue;
            const size_t src   = size_t(((y * scaleDivisor) * tile.width + x * scaleDivisor) * 4);
            const size_t dst   = size_t((py * canvas.width + px) * 4);
            const int    alpha = tile.pixels[src + 3];
            for (int channel = 0; channel < 3; ++channel)
                canvas.pixels[dst + channel] = uint8_t(
                    (int(tile.pixels[src + channel]) * alpha + int(canvas.pixels[dst + channel]) * (255 - alpha)) /
                    255);
            canvas.pixels[dst + 3] = 255;
        }
}

[[maybe_unused]] float smoothStep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

[[maybe_unused]] float mirrorRepeat(float value) {
    float repeated = std::fmod(value, 2.f);
    if (repeated < 0.f) repeated += 2.f;
    return repeated <= 1.f ? repeated : 2.f - repeated;
}

[[maybe_unused]] std::array<float, 4> sampleProjectedMaterial(const DualGridRgbaImage& tile, float worldX, float worldY) {
    const float  u = 0.04f + mirrorRepeat(worldX) * 0.92f;
    const float  v = 0.04f + mirrorRepeat(worldY) * 0.92f;
    const int    x = std::clamp(int(128.f + (u - v) * 127.f), 0, tile.width - 1);
    const int    y = std::clamp(int((u + v) * 63.f), 0, tile.height - 1);
    const size_t p = size_t((y * tile.width + x) * 4);
    return {float(tile.pixels[p]), float(tile.pixels[p + 1]), float(tile.pixels[p + 2]), float(tile.pixels[p + 3])};
}

[[maybe_unused]] float terrainNoise(float x, float y) {
    return 0.55f * std::sin(x * 0.83f + y * 0.37f) + 0.28f * std::sin(x * 2.17f - y * 1.31f + 1.7f) +
           0.17f * std::sin(x * 4.73f + y * 3.19f + 0.4f);
}

[[maybe_unused]] std::array<float, 4> blendSamples(const std::array<float, 4>& a, const std::array<float, 4>& b, float weight) {
    std::array<float, 4> result{};
    for (int channel = 0; channel < 4; ++channel) result[channel] = a[channel] * (1.f - weight) + b[channel] * weight;
    return result;
}

}  // namespace

TEST_CASE("map.dualGridMask.generatesAllStatesAndSdfBands") {
    DualGridMaskConfig config;
    config.width         = 64;
    config.height        = 32;
    config.edgeWidth     = 0.06f;
    config.noiseScale    = 4.f;
    config.noiseStrength = 0.04f;
    config.seed          = 20260917u;
    auto generated       = generateDualGridMaskAtlas(config);
    REQUIRE(generated.ok());
    const auto& atlas = generated.value();
    CHECK_EQ(atlas.width, 64);
    CHECK_EQ(atlas.height, 32);
    CHECK_EQ(atlas.coverage.size(), size_t(16 * 64 * 32));
    CHECK_EQ(atlas.coverageAt(0, 32, 16), 0.f);
    CHECK_EQ(atlas.coverageAt(15, 32, 16), 1.f);

    int softPixels = 0;
    int bandPixels = 0;
    for (int y = 0; y < atlas.height; ++y)
        for (int x = 0; x < atlas.width; ++x) {
            const float a = atlas.coverageAt(3, x, y);
            if (a > 0.f && a < 1.f) ++softPixels;
            if (atlas.bandAt(3, x, y, 0.f, 0.03f, 0.02f) > 0.5f) ++bandPixels;
        }
    CHECK(softPixels > 0);
    CHECK(bandPixels > 0);
}

TEST_CASE("map.dualGridMask.isDeterministicAndComplementary") {
    DualGridMaskConfig config;
    config.width         = 48;
    config.height        = 24;
    config.noiseStrength = 0.f;
    auto first           = generateDualGridMaskAtlas(config);
    auto second          = generateDualGridMaskAtlas(config);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    for (int mask = 0; mask < 16; ++mask)
        for (int y = 0; y < config.height; ++y)
            for (int x = 0; x < config.width; ++x) {
                CHECK_EQ(first.value().coverageAt(mask, x, y), second.value().coverageAt(mask, x, y));
                CHECK(std::abs(first.value().coverageAt(mask, x, y) + first.value().coverageAt(15 - mask, x, y) - 1.f) <
                      0.0001f);
            }
}

TEST_CASE("map.dualGridMask.rejectsInvalidConfigAtomically") {
    DualGridMaskConfig config;
    config.width   = 0;
    auto generated = generateDualGridMaskAtlas(config);
    CHECK(!generated.ok());
}

TEST_CASE("map.dualGridMask.bakesOrdinaryTilesIntoMaskIndexedAtlas") {
    DualGridMaskConfig config;
    config.width         = 8;
    config.height        = 4;
    config.noiseStrength = 0.f;
    DualGridRgbaImage grass{8, 4, std::vector<uint8_t>(8u * 4u * 4u, 0u)};
    DualGridRgbaImage water = grass;
    for (size_t i = 0; i < grass.pixels.size(); i += 4u) {
        grass.pixels[i + 1] = 200u;
        grass.pixels[i + 3] = 255u;
        water.pixels[i + 2] = 240u;
        water.pixels[i + 3] = 255u;
    }
    auto baked = bakeDualGridTransitionAtlas(grass, water, config);
    REQUIRE(baked.ok());
    CHECK_EQ(baked.value().width, 32);
    CHECK_EQ(baked.value().height, 16);
    const auto pixel = [&](int frame, int x, int y, int channel) {
        const int px = (frame % 4) * config.width + x;
        const int py = (frame / 4) * config.height + y;
        return baked.value().pixels[size_t((py * baked.value().width + px) * 4 + channel)];
    };
    CHECK_EQ(pixel(0, 4, 2, 1), 200u);
    CHECK_EQ(pixel(0, 4, 2, 2), 0u);
    CHECK_EQ(pixel(15, 4, 2, 1), 0u);
    CHECK_EQ(pixel(15, 4, 2, 2), 240u);
}
