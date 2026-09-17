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

void writePreviewBmp(const DualGridRgbaImage& image, const char* path) {
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

DualGridRgbaImage loadAtlasTile(const char* path, int tileX, int tileY) {
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

void compositeTile(DualGridRgbaImage& canvas, const DualGridRgbaImage& atlas, int frame, int dstX, int dstY,
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

DualGridRgbaImage blendMaterialTile(const std::array<DualGridRgbaImage, 3>& materials, const DualGridMaskAtlas& masks,
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

void compositeImage(DualGridRgbaImage& canvas, const DualGridRgbaImage& tile, int dstX, int dstY, int scaleDivisor) {
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

float smoothStep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float mirrorRepeat(float value) {
    float repeated = std::fmod(value, 2.f);
    if (repeated < 0.f) repeated += 2.f;
    return repeated <= 1.f ? repeated : 2.f - repeated;
}

std::array<float, 4> sampleProjectedMaterial(const DualGridRgbaImage& tile, float worldX, float worldY) {
    const float  u = 0.04f + mirrorRepeat(worldX) * 0.92f;
    const float  v = 0.04f + mirrorRepeat(worldY) * 0.92f;
    const int    x = std::clamp(int(128.f + (u - v) * 127.f), 0, tile.width - 1);
    const int    y = std::clamp(int((u + v) * 63.f), 0, tile.height - 1);
    const size_t p = size_t((y * tile.width + x) * 4);
    return {float(tile.pixels[p]), float(tile.pixels[p + 1]), float(tile.pixels[p + 2]), float(tile.pixels[p + 3])};
}

float terrainNoise(float x, float y) {
    return 0.55f * std::sin(x * 0.83f + y * 0.37f) + 0.28f * std::sin(x * 2.17f - y * 1.31f + 1.7f) +
           0.17f * std::sin(x * 4.73f + y * 3.19f + 0.4f);
}

std::array<float, 4> blendSamples(const std::array<float, 4>& a, const std::array<float, 4>& b, float weight) {
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

TEST_CASE("map.dualGridMask.packagedSbsFixturesDecode") {
    const auto grass = loadAtlasTile("test/assets/map/sbs_isometric_overworld_flat/terrain-1.png", 1, 0);
    const auto sand  = loadAtlasTile("test/assets/map/sbs_isometric_overworld_flat/terrain-1.png", 0, 5);
    const auto stone = loadAtlasTile("test/assets/map/sbs_isometric_overworld_flat/terrain-2.png", 0, 0);
    const auto water = loadAtlasTile("test/assets/map/sbs_isometric_overworld_flat/water.png", 0, 0);
    CHECK_EQ(grass.width, 256);
    CHECK_EQ(sand.height, 128);
    CHECK_EQ(stone.pixels.size(), size_t(256 * 128 * 4));
    CHECK_EQ(water.pixels.size(), size_t(256 * 128 * 4));
}

TEST_CASE("map.dualGridMask.preview") {
    const char* output = std::getenv("EVENGINE_DUAL_GRID_MASK_PREVIEW");
    if (!output || !*output) return;

    DualGridMaskConfig config;
    config.width         = 128;
    config.height        = 128;
    config.edgeWidth     = 0.035f;
    config.noiseScale    = 7.f;
    config.noiseStrength = 0.055f;
    config.seed          = 20260917u;
    DualGridRgbaImage grass{128, 128, std::vector<uint8_t>(128u * 128u * 4u)};
    DualGridRgbaImage water = grass;
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
            const size_t p          = size_t((y * 128 + x) * 4);
            const int    grassGrain = ((x * 17 + y * 31) & 15) - 8;
            grass.pixels[p]         = uint8_t(64 + grassGrain);
            grass.pixels[p + 1]     = uint8_t(126 + grassGrain);
            grass.pixels[p + 2]     = uint8_t(62 + grassGrain / 2);
            grass.pixels[p + 3]     = 255;
            const int ripple        = int(10.f * std::sin(float(x + y * 2) * 0.18f));
            water.pixels[p]         = uint8_t(25 + ripple / 3);
            water.pixels[p + 1]     = uint8_t(102 + ripple);
            water.pixels[p + 2]     = uint8_t(170 + ripple);
            water.pixels[p + 3]     = 255;
        }
    auto baked = bakeDualGridTransitionAtlas(grass, water, config);
    REQUIRE(baked.ok());
    writePreviewBmp(baked.value(), output);
}

TEST_CASE("map.dualGridMask.realAssetPreview") {
    const char* output    = std::getenv("EVENGINE_DUAL_GRID_MASK_REAL_PREVIEW");
    const char* grassPath = std::getenv("EVENGINE_DUAL_GRID_MASK_GRASS");
    const char* waterPath = std::getenv("EVENGINE_DUAL_GRID_MASK_WATER");
    if (!output || !*output || !grassPath || !*grassPath || !waterPath || !*waterPath) return;

    DualGridMaskConfig config;
    config.width                  = 256;
    config.height                 = 128;
    config.edgeWidth              = 0.018f;
    config.noiseScale             = 9.f;
    config.noiseStrength          = 0.035f;
    config.seed                   = 20260917u;
    const DualGridRgbaImage grass = loadAtlasTile(grassPath, 1, 0);
    const DualGridRgbaImage water = loadAtlasTile(waterPath, 0, 0);
    auto                    baked = bakeDualGridTransitionAtlas(grass, water, config);
    REQUIRE(baked.ok());
    writePreviewBmp(baked.value(), output);
}

TEST_CASE("map.dualGridMask.realAssetMap16x16Preview") {
    const char* output    = std::getenv("EVENGINE_DUAL_GRID_MASK_MAP_PREVIEW");
    const char* grassPath = std::getenv("EVENGINE_DUAL_GRID_MASK_GRASS");
    const char* waterPath = std::getenv("EVENGINE_DUAL_GRID_MASK_WATER");
    if (!output || !*output || !grassPath || !*grassPath || !waterPath || !*waterPath) return;

    DualGridMaskConfig config;
    config.width         = 256;
    config.height        = 128;
    config.edgeWidth     = 0.018f;
    config.noiseScale    = 9.f;
    config.noiseStrength = 0.035f;
    config.seed          = 20260917u;
    auto baked = bakeDualGridTransitionAtlas(loadAtlasTile(grassPath, 1, 0), loadAtlasTile(waterPath, 0, 0), config);
    REQUIRE(baked.ok());

    bool water[17][17]{};
    for (int y = 0; y <= 16; ++y)
        for (int x = 0; x <= 16; ++x) {
            const float lakeX      = (float(x) - 8.2f) / 5.2f;
            const float lakeY      = (float(y) - 8.0f) / 3.5f;
            const float pondX      = (float(x) - 3.0f) / 2.0f;
            const float pondY      = (float(y) - 12.4f) / 1.7f;
            const float shoreNoise = 0.12f * std::sin(float(x * 13 + y * 7));
            water[y][x] = lakeX * lakeX + lakeY * lakeY < 1.f + shoreNoise || pondX * pondX + pondY * pondY < 1.f;
        }

    constexpr int     mapSize    = 16;
    constexpr int     halfWidth  = 32;
    constexpr int     halfHeight = 16;
    DualGridRgbaImage canvas{1152, 640, std::vector<uint8_t>(1152u * 640u * 4u, 255u)};
    for (size_t p = 0; p < canvas.pixels.size(); p += 4u) {
        canvas.pixels[p]     = 20;
        canvas.pixels[p + 1] = 27;
        canvas.pixels[p + 2] = 24;
    }
    const int originX = 544;
    const int originY = 48;
    for (int diagonal = 0; diagonal <= (mapSize - 1) * 2; ++diagonal) {
        for (int y = 0; y < mapSize; ++y) {
            const int x = diagonal - y;
            if (x < 0 || x >= mapSize) continue;
            const int mask =
                dualGridMaskFromCorners(water[y][x], water[y][x + 1], water[y + 1][x], water[y + 1][x + 1]);
            compositeTile(canvas, baked.value(), mask, originX + (x - y) * halfWidth - halfWidth,
                          originY + (x + y) * halfHeight, 4);
        }
    }
    writePreviewBmp(canvas, output);
}

TEST_CASE("map.dualGridMask.threeMaterialMap16x16Preview") {
    const char* output     = std::getenv("EVENGINE_DUAL_GRID_MASK_MULTI_MAP_PREVIEW");
    const char* grassPath  = std::getenv("EVENGINE_DUAL_GRID_MASK_GRASS");
    const char* waterPath  = std::getenv("EVENGINE_DUAL_GRID_MASK_WATER");
    const char* forestPath = std::getenv("EVENGINE_DUAL_GRID_MASK_FOREST");
    if (!output || !*output || !grassPath || !waterPath || !forestPath) return;

    DualGridMaskConfig config;
    config.width         = 256;
    config.height        = 128;
    config.edgeWidth     = 0.018f;
    config.noiseScale    = 9.f;
    config.noiseStrength = 0.035f;
    config.seed          = 20260917u;
    auto generated       = generateDualGridMaskAtlas(config);
    REQUIRE(generated.ok());
    const std::array<DualGridRgbaImage, 3> materials{loadAtlasTile(grassPath, 1, 0), loadAtlasTile(waterPath, 0, 0),
                                                     loadAtlasTile(forestPath, 0, 0)};

    bool waterField[17][17]{};
    bool forestCandidate[17][17]{};
    for (int y = 0; y <= 16; ++y)
        for (int x = 0; x <= 16; ++x) {
            const float lakeX     = (float(x) - 9.f) / 4.6f;
            const float lakeY     = (float(y) - 8.2f) / 3.4f;
            forestCandidate[y][x] = x + y < 13 || (x < 6 && y < 10);
            waterField[y][x]      = lakeX * lakeX + lakeY * lakeY < 1.f + 0.1f * std::sin(float(x * 11 + y * 5));
        }
    int terrain[17][17]{};
    for (int y = 0; y <= 16; ++y)
        for (int x = 0; x <= 16; ++x) {
            if (waterField[y][x]) {
                terrain[y][x] = 1;
                continue;
            }
            bool nearWater = false;
            for (int oy = -2; oy <= 2 && !nearWater; ++oy)
                for (int ox = -2; ox <= 2; ++ox) {
                    const int nx = x + ox;
                    const int ny = y + oy;
                    if (nx >= 0 && ny >= 0 && nx <= 16 && ny <= 16 && waterField[ny][nx]) {
                        nearWater = true;
                        break;
                    }
                }
            if (forestCandidate[y][x] && !nearWater) terrain[y][x] = 2;
        }
    for (int y = 0; y <= 16; ++y)
        for (int x = 0; x <= 16; ++x) {
            if (terrain[y][x] != 2) continue;
            for (int oy = -1; oy <= 1; ++oy)
                for (int ox = -1; ox <= 1; ++ox) {
                    const int nx = x + ox;
                    const int ny = y + oy;
                    if (nx >= 0 && ny >= 0 && nx <= 16 && ny <= 16) CHECK(terrain[ny][nx] != 1);
                }
        }

    DualGridRgbaImage canvas{1152, 640, std::vector<uint8_t>(1152u * 640u * 4u, 255u)};
    for (size_t p = 0; p < canvas.pixels.size(); p += 4u) {
        canvas.pixels[p]     = 20;
        canvas.pixels[p + 1] = 27;
        canvas.pixels[p + 2] = 24;
    }
    for (int diagonal = 0; diagonal <= 30; ++diagonal)
        for (int y = 0; y < 16; ++y) {
            const int x = diagonal - y;
            if (x < 0 || x >= 16) continue;
            const std::array<int, 4> corners{terrain[y][x], terrain[y][x + 1], terrain[y + 1][x],
                                             terrain[y + 1][x + 1]};
            const auto               tile = blendMaterialTile(materials, generated.value(), corners);
            compositeImage(canvas, tile, 544 + (x - y) * 32 - 32, 48 + (x + y) * 16, 4);
        }
    writePreviewBmp(canvas, output);
}

TEST_CASE("map.dualGridMask.worldSpaceSplatPreview") {
    const char* output     = std::getenv("EVENGINE_DUAL_GRID_WORLD_SPLAT_PREVIEW");
    const char* grassPath  = std::getenv("EVENGINE_DUAL_GRID_MASK_GRASS");
    const char* waterPath  = std::getenv("EVENGINE_DUAL_GRID_MASK_WATER");
    const char* waterFrame = std::getenv("EVENGINE_DUAL_GRID_WATER_FRAME");
    const char* waterTime  = std::getenv("EVENGINE_DUAL_GRID_WATER_TIME");
    if (!output || !*output || !grassPath || !waterPath) return;

    const auto        grassA = loadAtlasTile(grassPath, 1, 0);
    const auto        grassB = loadAtlasTile(grassPath, 2, 0);
    const auto        dirtA  = loadAtlasTile(grassPath, 1, 1);
    const auto        dirtB  = loadAtlasTile(grassPath, 0, 5);
    const auto        waterA = loadAtlasTile(waterPath, 0, 0);
    const auto        waterB = loadAtlasTile(waterPath, 0, 1);
    DualGridRgbaImage canvas{1152, 640, std::vector<uint8_t>(1152u * 640u * 4u, 255u)};
    for (size_t p = 0; p < canvas.pixels.size(); p += 4u) {
        canvas.pixels[p]     = 17;
        canvas.pixels[p + 1] = 23;
        canvas.pixels[p + 2] = 21;
    }

    constexpr float originX = 544.f;
    constexpr float originY = 48.f;
    for (int py = 0; py < canvas.height; ++py)
        for (int px = 0; px < canvas.width; ++px) {
            const float isoX   = (float(px) - originX) / 32.f;
            const float isoY   = (float(py) - originY) / 16.f;
            const float worldX = (isoX + isoY) * 0.5f;
            const float worldY = (isoY - isoX) * 0.5f;
            if (worldX < 0.f || worldY < 0.f || worldX >= 16.f || worldY >= 16.f) continue;

            const float warp        = terrainNoise(worldX * 0.72f, worldY * 0.72f);
            const float dx          = (worldX - 8.8f + warp * 0.42f) / 4.9f;
            const float dy          = (worldY - 8.0f - warp * 0.31f) / 3.25f;
            const float distance    = std::sqrt(dx * dx + dy * dy) - 1.f;
            const float waterWeight = 1.f - smoothStep(-0.08f, 0.10f, distance);
            const float shoreWeight = (1.f - waterWeight) * (1.f - smoothStep(0.08f, 0.38f, distance));
            const float grassWeight = std::max(0.f, 1.f - waterWeight - shoreWeight);

            const float textureX = worldX * 0.68f + terrainNoise(worldX * 0.19f, worldY * 0.23f) * 0.12f;
            const float textureY = worldY * 0.68f + terrainNoise(worldY * 0.17f, worldX * 0.21f) * 0.12f;
            const float variant  = smoothStep(-0.45f, 0.45f, terrainNoise(worldX * 0.31f + 8.f, worldY * 0.31f - 3.f));
            const auto  grass =
                blendSamples(sampleProjectedMaterial(grassA, textureX, textureY),
                             sampleProjectedMaterial(grassB, textureX + 0.41f, textureY - 0.27f), variant);
            const auto dirt = blendSamples(
                sampleProjectedMaterial(dirtA, textureX * 0.91f, textureY * 0.91f),
                sampleProjectedMaterial(dirtB, textureX * 0.83f + 0.2f, textureY * 0.83f + 0.3f), variant * 0.55f);
            const float time           = waterTime ? std::strtof(waterTime, nullptr) : 0.f;
            const float edgeMotionFade = smoothStep(0.22f, 0.72f, waterWeight);
            const float distortion     = edgeMotionFade * 0.07f * std::sin(worldX * 1.7f + worldY * 1.1f + time * 2.4f);
            const auto  waterDetail =
                sampleProjectedMaterial(waterA, textureX * 0.82f - time * 0.22f * edgeMotionFade + distortion,
                                        textureY * 0.82f + time * 0.09f * edgeMotionFade - distortion * 0.6f);
            const auto waterBase =
                sampleProjectedMaterial(waterB, textureX * 0.61f + time * 0.10f * edgeMotionFade - distortion * 0.4f,
                                        textureY * 0.61f + time * 0.045f * edgeMotionFade + distortion);
            const float animatedBlend =
                waterTime ? 0.42f + 0.10f * std::sin(worldX * 0.8f - worldY * 0.55f + time * 1.8f)
                          : (waterFrame ? float(std::clamp(std::atoi(waterFrame), 0, 1)) : variant * 0.42f);
            const auto   water     = blendSamples(waterDetail, waterBase, animatedBlend);
            const float  foamBand  = 1.f - smoothStep(0.014f, 0.052f, std::abs(distance + 0.022f));
            const float  foamPulse = 0.62f + 0.18f * std::sin(worldX * 2.4f + worldY * 1.6f - time * 3.1f);
            const float  foam      = waterTime ? foamBand * foamPulse * smoothStep(0.02f, 0.22f, waterWeight) : 0.f;
            const size_t p         = size_t((py * canvas.width + px) * 4);
            for (int channel = 0; channel < 3; ++channel) {
                float value = grass[channel] * grassWeight + dirt[channel] * shoreWeight + water[channel] * waterWeight;
                if (foam > 0.f) {
                    const float foamColor[3] = {190.f, 224.f, 231.f};
                    value                    = value * (1.f - foam * 0.48f) + foamColor[channel] * foam * 0.48f;
                }
                canvas.pixels[p + channel] = uint8_t(std::clamp(value, 0.f, 255.f));
            }
        }
    writePreviewBmp(canvas, output);
}

TEST_CASE("map.dualGridMask.worldSpaceSandStonePreview") {
    const char* output       = std::getenv("EVENGINE_DUAL_GRID_LAND_SPLAT_PREVIEW");
    const char* terrain1Path = std::getenv("EVENGINE_DUAL_GRID_MASK_GRASS");
    const char* terrain2Path = std::getenv("EVENGINE_DUAL_GRID_TERRAIN2");
    if (!output || !*output || !terrain1Path || !terrain2Path) return;

    const auto        grassA = loadAtlasTile(terrain1Path, 1, 0);
    const auto        grassB = loadAtlasTile(terrain1Path, 2, 0);
    const auto        sandA  = loadAtlasTile(terrain1Path, 0, 5);
    const auto        sandB  = loadAtlasTile(terrain2Path, 2, 5);
    const auto        stoneA = loadAtlasTile(terrain1Path, 2, 3);
    const auto        stoneB = loadAtlasTile(terrain2Path, 0, 0);
    DualGridRgbaImage canvas{1152, 640, std::vector<uint8_t>(1152u * 640u * 4u, 255u)};
    for (size_t p = 0; p < canvas.pixels.size(); p += 4u) {
        canvas.pixels[p]     = 17;
        canvas.pixels[p + 1] = 23;
        canvas.pixels[p + 2] = 21;
    }

    constexpr float originX = 544.f;
    constexpr float originY = 48.f;
    for (int py = 0; py < canvas.height; ++py)
        for (int px = 0; px < canvas.width; ++px) {
            const float isoX   = (float(px) - originX) / 32.f;
            const float isoY   = (float(py) - originY) / 16.f;
            const float worldX = (isoX + isoY) * 0.5f;
            const float worldY = (isoY - isoX) * 0.5f;
            if (worldX < 0.f || worldY < 0.f || worldX >= 16.f || worldY >= 16.f) continue;

            const float warp     = terrainNoise(worldX * 0.61f + 2.f, worldY * 0.61f - 4.f);
            const float sandDx   = (worldX - 5.0f + warp * 0.52f) / 4.1f;
            const float sandDy   = (worldY - 9.6f - warp * 0.28f) / 3.5f;
            const float stoneDx  = (worldX - 11.2f - warp * 0.37f) / 3.8f;
            const float stoneDy  = (worldY - 6.1f + warp * 0.34f) / 3.2f;
            const float sandMask = 1.f - smoothStep(-0.10f, 0.16f, std::sqrt(sandDx * sandDx + sandDy * sandDy) - 1.f);
            const float stoneMask =
                1.f - smoothStep(-0.10f, 0.16f, std::sqrt(stoneDx * stoneDx + stoneDy * stoneDy) - 1.f);
            float weights[3] = {(1.f - sandMask) * (1.f - stoneMask), sandMask * (1.f - stoneMask * 0.32f), stoneMask};
            const float weightSum = weights[0] + weights[1] + weights[2];
            for (float& weight : weights) weight /= weightSum;

            const float textureX     = worldX * 0.70f + warp * 0.10f;
            const float textureY     = worldY * 0.70f - warp * 0.08f;
            const float variantNoise = 0.72f * terrainNoise(worldX * 1.15f, worldY * 1.15f) +
                                       0.28f * terrainNoise(worldX * 3.35f + 7.f, worldY * 2.85f - 5.f);
            const float variant      = smoothStep(-0.28f, 0.28f, variantNoise);
            const auto  grass =
                blendSamples(sampleProjectedMaterial(grassA, textureX, textureY),
                             sampleProjectedMaterial(grassB, textureX + 0.37f, textureY - 0.21f), variant * 0.52f);
            const auto sand = blendSamples(
                sampleProjectedMaterial(sandA, textureX * 0.81f, textureY * 0.81f),
                sampleProjectedMaterial(sandB, textureX * 0.67f + 0.18f, textureY * 0.67f + 0.27f), variant * 0.34f);
            const auto stone = blendSamples(
                sampleProjectedMaterial(stoneA, textureX * 0.88f, textureY * 0.88f),
                sampleProjectedMaterial(stoneB, textureX * 0.73f - 0.24f, textureY * 0.73f + 0.16f), variant * 0.38f);
            const size_t p = size_t((py * canvas.width + px) * 4);
            for (int channel = 0; channel < 3; ++channel) {
                const float value =
                    grass[channel] * weights[0] + sand[channel] * weights[1] + stone[channel] * weights[2];
                canvas.pixels[p + channel] = uint8_t(std::clamp(value, 0.f, 255.f));
            }
        }
    writePreviewBmp(canvas, output);
}

TEST_CASE("map.dualGridMask.worldSpaceFourMaterialPreview") {
    const char* output       = std::getenv("EVENGINE_DUAL_GRID_FOUR_MATERIAL_PREVIEW");
    const char* terrain1Path = std::getenv("EVENGINE_DUAL_GRID_MASK_GRASS");
    const char* terrain2Path = std::getenv("EVENGINE_DUAL_GRID_TERRAIN2");
    const char* waterPath    = std::getenv("EVENGINE_DUAL_GRID_MASK_WATER");
    if (!output || !*output) return;
    if (!terrain1Path || !*terrain1Path) terrain1Path = "test/assets/map/sbs_isometric_overworld_flat/terrain-1.png";
    if (!terrain2Path || !*terrain2Path) terrain2Path = "test/assets/map/sbs_isometric_overworld_flat/terrain-2.png";
    if (!waterPath || !*waterPath) waterPath = "test/assets/map/sbs_isometric_overworld_flat/water.png";

    const auto        grassA     = loadAtlasTile(terrain1Path, 1, 0);
    const auto        grassB     = loadAtlasTile(terrain1Path, 2, 0);
    const auto        sandA      = loadAtlasTile(terrain1Path, 0, 5);
    const auto        sandB      = loadAtlasTile(terrain2Path, 2, 5);
    const auto        stoneA     = loadAtlasTile(terrain1Path, 2, 3);
    const auto        stoneB     = loadAtlasTile(terrain2Path, 0, 0);
    const auto        waterSharp = loadAtlasTile(waterPath, 0, 0);
    const auto        waterSoft  = loadAtlasTile(waterPath, 0, 1);
    DualGridRgbaImage canvas{1152, 640, std::vector<uint8_t>(1152u * 640u * 4u, 255u)};
    for (size_t p = 0; p < canvas.pixels.size(); p += 4u) {
        canvas.pixels[p]     = 17;
        canvas.pixels[p + 1] = 23;
        canvas.pixels[p + 2] = 21;
    }

    constexpr float originX = 544.f;
    constexpr float originY = 48.f;
    for (int py = 0; py < canvas.height; ++py)
        for (int px = 0; px < canvas.width; ++px) {
            const float isoX   = (float(px) - originX) / 32.f;
            const float isoY   = (float(py) - originY) / 16.f;
            const float worldX = (isoX + isoY) * 0.5f;
            const float worldY = (isoY - isoX) * 0.5f;
            if (worldX < 0.f || worldY < 0.f || worldX >= 16.f || worldY >= 16.f) continue;

            const float warp         = terrainNoise(worldX * 0.67f + 1.3f, worldY * 0.59f - 3.7f);
            const float lakeDx       = (worldX - 5.1f + warp * 0.40f) / 3.65f;
            const float lakeDy       = (worldY - 8.5f - warp * 0.27f) / 3.15f;
            const float lakeDistance = std::sqrt(lakeDx * lakeDx + lakeDy * lakeDy) - 1.f;
            const float waterMask    = 1.f - smoothStep(-0.08f, 0.09f, lakeDistance);
            const float shoreOuter   = 1.f - smoothStep(0.02f, 0.34f, lakeDistance);
            const float sandMask     = std::max(0.f, shoreOuter - waterMask * 0.93f);

            const float stoneDx = (worldX - 11.4f - warp * 0.31f) / 3.55f;
            const float stoneDy = (worldY - 6.2f + warp * 0.36f) / 2.95f;
            const float stoneMask =
                1.f - smoothStep(-0.10f, 0.17f, std::sqrt(stoneDx * stoneDx + stoneDy * stoneDy) - 1.f);

            // Resolve every material in one normalized field. Water suppresses stone,
            // and the shoreline ring ensures the lake transitions through sand first.
            float weights[4] = {
                (1.f - waterMask) * (1.f - sandMask) * (1.f - stoneMask),
                sandMask * (1.f - stoneMask * 0.35f),
                stoneMask * (1.f - waterMask) * (1.f - sandMask * 0.72f),
                waterMask,
            };
            const float weightSum = weights[0] + weights[1] + weights[2] + weights[3];
            for (float& weight : weights) weight /= weightSum;

            const float textureX     = worldX * 0.70f + warp * 0.10f;
            const float textureY     = worldY * 0.70f - warp * 0.08f;
            const float variantNoise = 0.72f * terrainNoise(worldX * 1.15f, worldY * 1.15f) +
                                       0.28f * terrainNoise(worldX * 3.35f + 7.f, worldY * 2.85f - 5.f);
            const float variant      = smoothStep(-0.28f, 0.28f, variantNoise);
            const auto  grass =
                blendSamples(sampleProjectedMaterial(grassA, textureX, textureY),
                             sampleProjectedMaterial(grassB, textureX + 0.37f, textureY - 0.21f), variant * 0.52f);
            const auto sand = blendSamples(
                sampleProjectedMaterial(sandA, textureX * 0.81f, textureY * 0.81f),
                sampleProjectedMaterial(sandB, textureX * 0.67f + 0.18f, textureY * 0.67f + 0.27f), variant * 0.34f);
            const auto stone = blendSamples(
                sampleProjectedMaterial(stoneA, textureX * 0.88f, textureY * 0.88f),
                sampleProjectedMaterial(stoneB, textureX * 0.73f - 0.24f, textureY * 0.73f + 0.16f), variant * 0.38f);
            const auto water =
                blendSamples(sampleProjectedMaterial(waterSoft, textureX * 0.58f, textureY * 0.58f),
                             sampleProjectedMaterial(waterSharp, textureX * 0.76f + 0.31f, textureY * 0.76f - 0.19f),
                             0.36f + variant * 0.18f);
            const size_t p = size_t((py * canvas.width + px) * 4);
            for (int channel = 0; channel < 3; ++channel) {
                const float value          = grass[channel] * weights[0] + sand[channel] * weights[1] +
                                             stone[channel] * weights[2] + water[channel] * weights[3];
                canvas.pixels[p + channel] = uint8_t(std::clamp(value, 0.f, 255.f));
            }
        }
    writePreviewBmp(canvas, output);
}
