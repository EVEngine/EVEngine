#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_map_dual_grid_masks)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include "filesystem/FileData.h"
#include "image/ImageData.h"
#include "map/DualGrid.h"
// map_dual_grid_masks.cpp
namespace test_map_dual_grid_masks_unit {
using namespace eve::map;

namespace {

[[maybe_unused]] void writePreviewBmp(const DualGridRgbaImage &image, const char *path) {
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
    out.write(reinterpret_cast<const char *>(header), sizeof(header));
    for (int y = image.height - 1; y >= 0; --y) {
        for (int x = 0; x < image.width; ++x) {
            const size_t  p       = size_t((y * image.width + x) * 4);
            const uint8_t bgra[4] = {image.pixels[p + 2], image.pixels[p + 1], image.pixels[p], image.pixels[p + 3]};
            out.write(reinterpret_cast<const char *>(bgra), 4);
        }
    }
    REQUIRE(out.good());
}

[[maybe_unused]] DualGridRgbaImage loadAtlasTile(const char *path, int tileX, int tileY) {
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
    const auto       *source = static_cast<const uint8_t *>(atlas.getData());
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

[[maybe_unused]] void compositeTile(DualGridRgbaImage &canvas, const DualGridRgbaImage &atlas, int frame, int dstX,
                                    int dstY, int scaleDivisor = 1) {
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

[[maybe_unused]] DualGridRgbaImage blendMaterialTile(const std::array<DualGridRgbaImage, 3> &materials,
                                                     const DualGridMaskAtlas                &masks,
                                                     const std::array<int, 4>               &corners) {
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

[[maybe_unused]] void compositeImage(DualGridRgbaImage &canvas, const DualGridRgbaImage &tile, int dstX, int dstY,
                                     int scaleDivisor) {
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

[[maybe_unused]] std::array<float, 4> sampleProjectedMaterial(const DualGridRgbaImage &tile, float worldX,
                                                              float worldY) {
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

[[maybe_unused]] std::array<float, 4> blendSamples(const std::array<float, 4> &a, const std::array<float, 4> &b,
                                                   float weight) {
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
    const auto &atlas = generated.value();
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
}  // namespace test_map_dual_grid_masks_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_map_fov)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "map/FlowField.h"
#include "map/Fov.h"
#include "map/Map.h"
#include "map/Path.h"
#include "map/Pathfinder.h"
#include "map/TileLayer.h"
#include "map/TileOrientation.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// map_fov.cpp
namespace test_map_fov_unit {
// map_fov.cpp
using namespace eve::map;

namespace {

[[maybe_unused]] int countVisible(Fov *fov) {
    int n = 0;
    for (int y = 0; y < fov->getHeight(); ++y)
        for (int x = 0; x < fov->getWidth(); ++x)
            if (fov->isVisible(x, y)) ++n;
    return n;
}

}  // namespace

TEST_CASE("map.fov.algorithm.defaults") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(4, 4);
    REQUIRE(fov != nullptr);
    CHECK_EQ(fov->getWidth(), 4);
    CHECK_EQ(fov->getHeight(), 4);
    CHECK_EQ(fov->getAlgorithm(), std::string("shadowcast"));
    CHECK_EQ(fov->getRadiusMetric(), std::string("euclidean"));
    fov->setRadiusMetric("chebyshev");
    CHECK_EQ(fov->getRadiusMetric(), std::string("chebyshev"));
    fov->setRadiusMetric("nope");
    CHECK_EQ(fov->getRadiusMetric(), std::string("chebyshev"));
    fov->setAlgorithm("raycast");
    CHECK_EQ(fov->getAlgorithm(), std::string("raycast"));
    fov->setAlgorithm("permissive");
    CHECK_EQ(fov->getAlgorithm(), std::string("permissive"));
    fov->setAlgorithm("rectangle");
    CHECK_EQ(fov->getAlgorithm(), std::string("rectangle"));
    fov->setAlgorithm("nope");
    CHECK_EQ(fov->getAlgorithm(), std::string("rectangle"));
    CHECK_EQ(fov->getMode(), std::string("grid2d"));
    CHECK_EQ(fov->getTopology(), std::string("ortho"));
    delete fov;
}

TEST_CASE("map.fov.openRadius.chebyshev") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(11, 11);
    REQUIRE(fov != nullptr);
    fov->setRadiusMetric("chebyshev");
    fov->setBlockEmpty(false);
    const int id = fov->addRevealer(5, 5, 2);
    CHECK(id >= 1);
    fov->compute();
    CHECK(fov->isVisible(5, 5));
    CHECK(fov->isVisible(5 + 2, 5));
    CHECK(fov->isVisible(5, 5 + 2));
    CHECK(fov->isVisible(5 + 2, 5 + 2));
    CHECK(!fov->isVisible(5 + 3, 5));
    // Chebyshev radius 2 => 5x5 = 25 cells
    CHECK_EQ(countVisible(fov), 25);
    delete fov;
}

TEST_CASE("map.fov.wallCastsShadow") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(9, 5);
    REQUIRE(fov != nullptr);
    fov->setRadiusMetric("chebyshev");
    fov->setBlockEmpty(false);
    // Vertical wall at x=4, gap none — full column opaque
    for (int y = 0; y < 5; ++y) fov->setOpaque(4, y, true);
    fov->addRevealer(1, 2, 6);
    fov->compute();
    CHECK(fov->isVisible(1, 2));
    CHECK(fov->isVisible(4, 2));   // blocking cell itself is visible
    CHECK(!fov->isVisible(6, 2));  // behind wall
    CHECK(!fov->isVisible(7, 2));
    delete fov;
}

TEST_CASE("map.fov.corridorAndRoom") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(7, 5);
    fov->setBlockEmpty(false);
    fov->setRadiusMetric("chebyshev");
    // Room walls with a corridor opening east from (1,2)
    for (int x = 0; x < 7; ++x) {
        fov->setOpaque(x, 0, true);
        fov->setOpaque(x, 4, true);
    }
    for (int y = 0; y < 5; ++y) {
        fov->setOpaque(0, y, true);
        fov->setOpaque(6, y, true);
    }
    // Interior divider with door at (3,2)
    fov->setOpaque(3, 1, true);
    fov->setOpaque(3, 3, true);
    fov->addRevealer(1, 2, 8);
    fov->compute();
    CHECK(fov->isVisible(1, 2));
    CHECK(fov->isVisible(2, 2));
    CHECK(fov->isVisible(3, 2));  // open gap between walls
    CHECK(fov->isVisible(4, 2));  // room beyond door
    CHECK(fov->isVisible(3, 1));  // opaque wall cell is lit when hit
    delete fov;
}

TEST_CASE("map.fov.coneFacing") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(11, 11);
    fov->setBlockEmpty(false);
    fov->setRadiusMetric("chebyshev");
    const int id = fov->addRevealer(5, 5, 4);
    // Face +X (east), half-angle 30°
    fov->setRevealerFacing(id, 0.f, 30.f);
    fov->compute();
    CHECK(fov->isVisible(5, 5));
    CHECK(fov->isVisible(8, 5));   // east
    CHECK(!fov->isVisible(2, 5));  // west outside cone
    CHECK(!fov->isVisible(5, 8));  // south outside cone
    fov->clearRevealerFacing(id);
    fov->compute();
    CHECK(fov->isVisible(2, 5));
    delete fov;
}

TEST_CASE("map.fov.multiRevealerOr") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(15, 5);
    fov->setBlockEmpty(false);
    fov->setRadiusMetric("chebyshev");
    fov->addRevealer(1, 2, 1);
    fov->addRevealer(13, 2, 1);
    fov->compute();
    CHECK(fov->isVisible(1, 2));
    CHECK(fov->isVisible(13, 2));
    CHECK(!fov->isVisible(7, 2));
    CHECK_EQ(fov->getRevealerCount(), 2);
    delete fov;
}

TEST_CASE("map.fov.exploredMemory") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(9, 9);
    fov->setBlockEmpty(false);
    fov->setRadiusMetric("chebyshev");
    const int id = fov->addRevealer(4, 4, 2);
    fov->compute();
    CHECK(fov->isVisible(4, 4));
    CHECK(fov->isExplored(4, 4));
    CHECK_EQ(fov->getState(4, 4), std::string("visible"));

    fov->setRevealerPosition(id, 0, 0);
    fov->setRevealerRadius(id, 1);
    fov->compute();
    CHECK(!fov->isVisible(4, 4));
    CHECK(fov->isExplored(4, 4));
    CHECK_EQ(fov->getState(4, 4), std::string("explored"));
    CHECK(fov->isVisible(0, 0));

    fov->clearMemory();
    CHECK(!fov->isExplored(4, 4));
    CHECK_EQ(fov->getState(4, 4), std::string("unknown"));
    // clearMemory does not recompute; current visibility cleared too
    CHECK(!fov->isVisible(0, 0));
    delete fov;
}

TEST_CASE("map.fov.snapshotRestoresExactVisibilityMemory") {
    Fov fov(7, 7);
    fov.setBlockEmpty(false);
    fov.setRadiusMetric("chebyshev");
    const int revealer = fov.addRevealer(2, 2, 1);
    fov.compute();
    fov.setRevealerPosition(revealer, 5, 5);
    fov.compute();

    const auto saved = fov.snapshot();
    CHECK(fov.isExplored(2, 2));
    CHECK(!fov.isVisible(2, 2));
    CHECK(fov.isVisible(5, 5));
    CHECK(!fov.isExplored(0, 6));

    fov.clearMemory();
    CHECK(!fov.isExplored(2, 2));
    REQUIRE(fov.restore(saved).ok());
    CHECK(fov.isExplored(2, 2));
    CHECK(!fov.isVisible(2, 2));
    CHECK(fov.isVisible(5, 5));
    CHECK(!fov.isExplored(0, 6));

    auto invalid = saved;
    invalid.width += 1;
    CHECK(!fov.restore(invalid).ok());
}

TEST_CASE("map.fov.layerOpaqueGid") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(7, 3, 8.f, 8.f);
    layer->fill(2);
    for (int y = 0; y < 3; ++y) layer->setTile(3, y, 1);

    Fov *fov = mod->newFov(layer);
    REQUIRE(fov != nullptr);
    fov->setBlockEmpty(false);
    fov->blockOpaqueGid(1);
    fov->setRadiusMetric("chebyshev");
    fov->addRevealer(0, 1, 6);
    fov->compute();
    CHECK(fov->isOpaque(3, 1));
    CHECK(fov->isVisible(3, 1));
    CHECK(!fov->isVisible(5, 1));

    layer->setTile(3, 1, 2);
    fov->syncFromLayer();
    fov->compute();
    CHECK(!fov->isOpaque(3, 1));
    CHECK(fov->isVisible(5, 1));
    delete fov;
}

TEST_CASE("map.fov.boundLayerAutoSyncsRevision") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(7, 1, 8.f, 8.f);
    layer->fill(2);
    Fov *fov = mod->newFov(layer);
    fov->setBlockEmpty(false);
    fov->blockOpaqueGid(1);
    fov->addRevealer(0, 0, 7);
    fov->compute();
    CHECK(fov->isVisible(6, 0));
    layer->setTile(3, 0, 1);
    fov->compute();
    CHECK(!fov->isVisible(6, 0));
    delete fov;
}

TEST_CASE("map.fov.removeAndDisable") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(5, 5);
    fov->setBlockEmpty(false);
    const int a = fov->addRevealer(2, 2, 1);
    const int b = fov->addRevealer(0, 0, 1);
    fov->setRevealerEnabled(b, false);
    fov->compute();
    CHECK(fov->isVisible(2, 2));
    CHECK(!fov->isVisible(0, 0));
    fov->removeRevealer(a);
    fov->setRevealerEnabled(b, true);
    fov->compute();
    CHECK(fov->isVisible(0, 0));
    CHECK(!fov->isVisible(2, 2));
    fov->clearRevealers();
    CHECK_EQ(fov->getRevealerCount(), 0);
    delete fov;
}

TEST_CASE("map.fov.raycast.wallShadow") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(9, 5);
    fov->setAlgorithm("raycast");
    fov->setRadiusMetric("chebyshev");
    fov->setBlockEmpty(false);
    for (int y = 0; y < 5; ++y) fov->setOpaque(4, y, true);
    fov->addRevealer(1, 2, 6);
    fov->compute();
    CHECK(fov->isVisible(1, 2));
    CHECK(fov->isVisible(4, 2));
    CHECK(!fov->isVisible(6, 2));
    delete fov;
}

TEST_CASE("map.fov.permissive.openRadius") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(7, 7);
    fov->setAlgorithm("permissive");
    fov->setRadiusMetric("chebyshev");
    fov->setBlockEmpty(false);
    fov->addRevealer(3, 3, 2);
    fov->compute();
    CHECK(fov->isVisible(3, 3));
    CHECK(fov->isVisible(5, 5));
    CHECK(!fov->isVisible(6, 3));
    delete fov;
}

TEST_CASE("map.fov.heightmap.cliffBlocks") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(9, 3);
    fov->setMode("heightmap");
    fov->setRadiusMetric("chebyshev");
    fov->setBlockEmpty(false);
    fov->setCliffBlock(1.f);
    fov->setEyeOffset(0.f);
    // Flat ground elev 0; cliff column at x=4 with elev 2
    for (int y = 0; y < 3; ++y) fov->setElevation(4, y, 2.f);
    fov->addRevealer(1, 1, 6);
    fov->compute();
    CHECK(fov->isVisible(1, 1));
    CHECK(!fov->isVisible(6, 1));  // behind high cliff
    // Lower cliff should not block with cliffBlock=1 if elev=0.5
    fov->setElevation(4, 1, 0.5f);
    fov->compute();
    CHECK(fov->isVisible(6, 1));
    delete fov;
}

TEST_CASE("map.fov.volume.bridgeLayers") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovVolume(7, 3, 4);
    REQUIRE(fov != nullptr);
    CHECK_EQ(fov->getDepth(), 4);
    CHECK_EQ(fov->getMode(), std::string("volume"));
    fov->setRadiusMetric("chebyshev");
    fov->setVerticalRange(3);
    // Solid slab at z=1 separates upper (z=2) and lower (z=0) spaces.
    for (int x = 0; x < 7; ++x) fov->setOpaque3(x, 1, 1, true);

    fov->addRevealer3(1, 1, 2, 5);  // above slab
    fov->compute();
    CHECK(fov->isVisible3(1, 1, 2));
    CHECK(fov->isVisible3(3, 1, 2));
    CHECK(fov->isVisible3(3, 1, 1));   // slab surface lit
    CHECK(!fov->isVisible3(3, 1, 0));  // below slab blocked

    fov->clearRevealers();
    fov->addRevealer3(1, 1, 0, 5);  // below slab
    fov->compute();
    CHECK(fov->isVisible3(1, 1, 0));
    CHECK(fov->isVisible3(3, 1, 1));
    CHECK(!fov->isVisible3(3, 1, 2));
    delete fov;
}

TEST_CASE("map.fov.volume.verticalExtend") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovVolume(5, 5, 5);
    fov->setRadiusMetric("chebyshev");
    fov->setVerticalRange(2);
    fov->addRevealer3(2, 2, 2, 1);
    fov->compute();
    CHECK(fov->isVisible3(2, 2, 2));
    CHECK(fov->isVisible3(2, 2, 3));
    CHECK(fov->isVisible3(2, 2, 4));
    CHECK(fov->isVisible3(2, 2, 1));
    CHECK(fov->isVisible3(2, 2, 0));
    // Block upward
    fov->setOpaque3(2, 2, 3, true);
    fov->compute();
    CHECK(fov->isVisible3(2, 2, 3));  // opaque cell lit
    CHECK(!fov->isVisible3(2, 2, 4));
    delete fov;
}

TEST_CASE("map.fov.dirty.skipsRecompute") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(5, 5);
    fov->setBlockEmpty(false);
    fov->setRadiusMetric("chebyshev");
    fov->addRevealer(2, 2, 1);
    CHECK(fov->isDirty());
    fov->compute();
    CHECK(!fov->isDirty());
    CHECK(fov->isVisible(2, 2));
    // Second compute without changes is a no-op (still visible).
    fov->compute();
    CHECK(fov->isVisible(2, 2));
    CHECK(!fov->isDirty());
    fov->setOpaque(1, 2, true);
    CHECK(fov->isDirty());
    fov->markDirty();
    CHECK(fov->isDirty());
    delete fov;
}

TEST_CASE("map.fov.maskExport") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(5, 5);
    fov->setBlockEmpty(false);
    fov->setRadiusMetric("chebyshev");
    const int id = fov->addRevealer(2, 2, 1);
    fov->compute();
    CHECK_EQ(fov->getMaskByte(2, 2), 255);
    CHECK(fov->getMaskValue(2, 2) > 0.9f);

    fov->setRevealerPosition(id, 0, 0);
    fov->setRevealerRadius(id, 0);
    fov->compute();
    CHECK_EQ(fov->getMaskByte(2, 2), 89);  // explored ≈ 0.35*255
    CHECK(!fov->isVisible(2, 2));
    CHECK(fov->isExplored(2, 2));

    std::vector<uint8_t> mask;
    CHECK(fov->fillMaskR8(mask));
    CHECK_EQ(int(mask.size()), 25);
    CHECK_EQ(int(mask[size_t(2 + 2 * 5)]), 89);
    delete fov;
}

TEST_CASE("map.fov.hex.topologyRadius") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(11, 11);
    fov->setTopology("hex");
    fov->setBlockEmpty(false);
    CHECK_EQ(fov->getTopology(), std::string("hex"));
    fov->addRevealer(5, 5, 2);
    fov->compute();
    CHECK(fov->isVisible(5, 5));
    // Hex distance 2 neighbors should be visible; far chebyshev corner may not.
    CHECK(fov->isVisible(6, 5));
    const bool nearRing = fov->isVisible(5, 7) || fov->isVisible(6, 6);
    CHECK(nearRing);
    CHECK(!fov->isVisible(0, 0));
    delete fov;
}

TEST_CASE("map.fov.hex.wallBlocksCubeLine") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(9, 5);
    fov->setTopology("hex");
    fov->setAlgorithm("shadowcast");
    fov->setBlockEmpty(false);
    for (int y = 0; y < 5; ++y) fov->setOpaque(4, y, true);
    fov->addRevealer(1, 2, 5);
    fov->compute();
    CHECK(fov->isVisible(1, 2));
    CHECK(fov->isVisible(4, 2));
    CHECK(!fov->isVisible(7, 2));
    delete fov;
}

TEST_CASE("map.fov.rectangle.wallShadow") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(11, 5);
    fov->setAlgorithm("rectangle");
    fov->setRadiusMetric("chebyshev");
    fov->setBlockEmpty(false);
    for (int y = 0; y < 5; ++y) {
        fov->setOpaque(5, y, true);
        fov->setOpaque(6, y, true);  // thick wall merges to a rect
    }
    fov->addRevealer(1, 2, 8);
    fov->compute();
    CHECK(fov->isVisible(1, 2));
    CHECK(fov->isVisible(5, 2));
    CHECK(!fov->isVisible(9, 2));
    delete fov;
}

TEST_CASE("map.fov.perception.canDetect") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(7, 7);
    fov->setBlockEmpty(false);
    fov->setRadiusMetric("chebyshev");
    fov->setPerceptionRadiusScale(1.f);
    const int id = fov->addRevealer(3, 3, 1);
    fov->setRevealerPerception(id, 2.f);  // effective radius 3
    CHECK_EQ(fov->getEffectiveRadius(id), 3);
    fov->compute();
    CHECK(fov->isVisible(3, 6));
    fov->setDetectionMargin(0.f);
    CHECK(fov->canDetect(id, 3, 3, 1.f));   // perception 2 >= stealth 1
    CHECK(!fov->canDetect(id, 3, 3, 3.f));  // stealth too high
    CHECK(!fov->canDetect(id, 0, 0, 0.f));  // not visible
    delete fov;
}


TEST_CASE("map.fov.accessorRoundTrips") {
    auto *mod = Map::create();
    Fov  *fov = mod->newFovSize(4, 4);
    REQUIRE(fov != nullptr);

    fov->setCornerPeek(true);
    CHECK(fov->getCornerPeek());
    fov->setCornerPeek(false);
    CHECK(!fov->getCornerPeek());

    fov->setEyeOffset(1.f);
    CHECK_EQ(fov->getEyeOffset(), 1.f);

    fov->setCliffBlock(0.5f);
    CHECK_EQ(fov->getCliffBlock(), 0.5f);

    fov->setVerticalRange(3);
    CHECK_EQ(fov->getVerticalRange(), 3);

    fov->setPerceptionRadiusScale(1.5f);
    CHECK_EQ(fov->getPerceptionRadiusScale(), 1.5f);

    const int id = fov->addRevealer(1, 1, 2);
    REQUIRE(id >= 0);
    fov->setRevealerPosition3(id, 2, 3, 1);
    fov->setRevealerRadius(id, 4);
    fov->setRevealerPerception(id, 2.f);
    CHECK_EQ(fov->getRevealerPerception(id), 2.f);
    CHECK_EQ(fov->getRevealerCount(), 1);
    fov->setRevealerEnabled(id, false);
    CHECK_EQ(fov->getRevealerCount(), 1);            // disabled revealers stay registered
    CHECK_EQ(fov->getRevealerPerception(999), 0.f);  // unknown id

    delete fov;
}

// map_path.cpp
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::map;
using namespace eve::graphics;

namespace {

[[maybe_unused]] void fillOpen(TileLayer *layer, int floorGid = 2) { layer->fill(floorGid); }

[[maybe_unused]] void wallRect(TileLayer *layer, int x0, int y0, int x1, int y1, int wallGid = 1) {
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) layer->setTile(x, y, wallGid);
}

[[maybe_unused]] bool pathEndsAt(Path *p, int x, int y) {
    if (!p || p->getLength() <= 0) return false;
    return p->getX(p->getLength() - 1) == x && p->getY(p->getLength() - 1) == y;
}

[[maybe_unused]] bool pathStartsAt(Path *p, int x, int y) {
    if (!p || p->getLength() <= 0) return false;
    return p->getX(0) == x && p->getY(0) == y;
}

[[maybe_unused]] bool pathWalkable(Path *p, Pathfinder *pf) {
    if (!p) return false;
    for (int i = 0; i < p->getLength(); ++i) {
        if (!pf->isWalkable(p->getX(i), p->getY(i))) return false;
    }
    return true;
}

}  // namespace

TEST_CASE("map.path.topology.parse") {
    auto       *mod = Map::create();
    Pathfinder *pf  = mod->newPathfinderSize(2, 2);
    pf->setTopology("ortho4");
    CHECK_EQ(pf->getTopology(), std::string("ortho4"));
    pf->setTopology("ortho8");
    CHECK_EQ(pf->getTopology(), std::string("ortho8"));
    pf->setTopology("hex");
    CHECK_EQ(pf->getTopology(), std::string("hex"));
    pf->setTopology("nope");  // fallback keeps last valid (hex)
    CHECK_EQ(pf->getTopology(), std::string("hex"));
    delete pf;
}

TEST_CASE("map.path.astar.orthoBypassWall") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(8, 5, 16.f, 16.f);
    fillOpen(layer);
    for (int y = 0; y < 5; ++y) layer->setTile(3, y, 1);
    layer->setTile(3, 2, 2);

    Pathfinder *pf = mod->newPathfinder(layer);
    REQUIRE(pf != nullptr);
    pf->blockGid(1);
    pf->setTopology("ortho4");

    Path *path = pf->findPath(0, 2, 7, 2);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() > 0);
    CHECK(pathStartsAt(path, 0, 2));
    CHECK(pathEndsAt(path, 7, 2));
    CHECK(pathWalkable(path, pf));
    bool viaGap = false;
    for (int i = 0; i < path->getLength(); ++i) {
        if (path->getX(i) == 3 && path->getY(i) == 2) viaGap = true;
    }
    CHECK(viaGap);
    delete path;
    delete pf;
}

TEST_CASE("map.path.astar.unreachable") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(5, 3, 8.f, 8.f);
    fillOpen(layer);
    for (int y = 0; y < 3; ++y) layer->setTile(2, y, 1);
    Pathfinder *pf = mod->newPathfinder(layer);
    pf->blockGid(1);
    Path *path = pf->findPath(0, 1, 4, 1);
    REQUIRE(path != nullptr);
    CHECK_EQ(path->getLength(), 0);
    delete path;
    delete pf;
}

TEST_CASE("map.path.layerMetadataIsNavigationAuthority") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(3, 2, 8.f, 8.f);
    layer->fill(2);
    layer->setTile(1, 0, 9);
    layer->setTileMetadata(9, 1, 1, false, 7.f);

    Pathfinder *pf = mod->newPathfinder(layer);
    REQUIRE(pf != nullptr);
    CHECK(!pf->isWalkable(1, 0));

    Path *detour = pf->findPath(0, 0, 2, 0);
    REQUIRE(detour != nullptr);
    CHECK(pathEndsAt(detour, 2, 0));
    CHECK(detour->getLength() == 5);
    delete detour;

    // Metadata mutation publishes a layer revision, so an already-bound
    // pathfinder observes the new canonical navigation state automatically.
    layer->setTileMetadata(9, 1, 1, true, 7.f);
    CHECK(pf->isWalkable(1, 0));
    CHECK_EQ(pf->getCellCost(1, 0), 7.f);
    delete pf;
}

TEST_CASE("map.path.directionalNavigationUsesTileProfiles") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(3, 1, 8.f, 8.f);
    REQUIRE(layer->applyConfig(R"({
      "width":3,"height":1,"data":[2,5,2],
      "tileset":{"firstgid":1,"columns":5,"tilewidth":8,"tileheight":8,
        "tiles":[{"id":4,"properties":[
          {"name":"exitMask","type":"int","value":2}
        ]}]}
    })"));

    Pathfinder pathfinder(layer);
    Path      *east = pathfinder.findPath(0, 0, 2, 0);
    REQUIRE(east != nullptr);
    CHECK(pathEndsAt(east, 2, 0));
    delete east;

    Path *west = pathfinder.findPath(2, 0, 0, 0);
    REQUIRE(west != nullptr);
    CHECK_EQ(west->getLength(), 0);
    delete west;
}

TEST_CASE("map.path.astar.ortho8NoCornerCut") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(3, 3, 8.f, 8.f);
    fillOpen(layer);
    layer->setTile(1, 1, 1);
    Pathfinder *pf = mod->newPathfinder(layer);
    pf->blockGid(1);
    pf->setTopology("ortho8");
    Path *path = pf->findPath(0, 1, 2, 1);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() > 0);
    CHECK(pathEndsAt(path, 2, 1));
    CHECK(pathWalkable(path, pf));
    CHECK(path->getLength() >= 5);
    delete path;
    delete pf;
}

TEST_CASE("map.path.customGrid") {
    auto       *mod = Map::create();
    Pathfinder *pf  = mod->newPathfinderSize(4, 4);
    REQUIRE(pf != nullptr);
    pf->setTopology("ortho4");
    pf->setBlocked(1, 0, true);
    pf->setBlocked(1, 1, true);
    pf->setBlocked(1, 2, true);
    Path *blocked = pf->findPath(0, 1, 3, 1);
    CHECK_EQ(blocked->getLength(), 0);
    delete blocked;

    pf->setBlocked(1, 1, false);
    Path *path = pf->findPath(0, 1, 3, 1);
    CHECK(pathStartsAt(path, 0, 1));
    CHECK(pathEndsAt(path, 3, 1));
    delete path;
    delete pf;
}

TEST_CASE("map.path.flowField.groupSameGoal") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(10, 8, 16.f, 16.f);
    fillOpen(layer);
    wallRect(layer, 4, 0, 4, 5);
    layer->setTile(4, 6, 2);

    Pathfinder *pf = mod->newPathfinder(layer);
    pf->blockGid(1);
    pf->setTopology("ortho4");

    const int  gx = 9, gy = 1;
    FlowField *field = pf->buildFlowField(gx, gy);
    REQUIRE(field != nullptr);
    CHECK_EQ(field->getGoalX(), gx);
    CHECK_EQ(field->getGoalY(), gy);
    CHECK(field->isReachable(0, 0));
    CHECK(field->isReachable(1, 7));

    int starts[][2] = {{0, 0}, {0, 7}, {2, 3}};
    for (auto &s : starts) {
        Path *p = pf->followFlow(field, s[0], s[1]);
        CHECK(pathStartsAt(p, s[0], s[1]));
        CHECK(pathEndsAt(p, gx, gy));
        CHECK(pathWalkable(p, pf));
        delete p;
    }

    Path *g0 = pf->findGroupPath(0, 0, gx, gy);
    Path *g1 = pf->findGroupPath(2, 3, gx, gy);
    CHECK(pathEndsAt(g0, gx, gy));
    CHECK(pathEndsAt(g1, gx, gy));
    delete g0;
    delete g1;
    delete field;
    delete pf;
}

TEST_CASE("map.path.flowField.matchesAstarReachability") {
    auto       *mod = Map::create();
    Pathfinder *pf  = mod->newPathfinderSize(6, 6);
    pf->setTopology("ortho4");
    for (int y = 0; y < 5; ++y) pf->setBlocked(3, y, true);

    Path *a = pf->findPath(0, 0, 5, 0);
    Path *b = pf->findGroupPath(0, 0, 5, 0);
    CHECK_EQ(a->getLength(), 0);
    CHECK_EQ(b->getLength(), 0);
    delete a;
    delete b;

    pf->setBlocked(3, 5, false);
    Path *a2 = pf->findPath(0, 0, 5, 0);
    Path *b2 = pf->findGroupPath(0, 0, 5, 0);
    CHECK(a2->getLength() > 0);
    CHECK(b2->getLength() > 0);
    CHECK(pathEndsAt(a2, 5, 0));
    CHECK(pathEndsAt(b2, 5, 0));
    delete a2;
    delete b2;
    delete pf;
}

TEST_CASE("map.path.hex.staggeredNeighborsReach") {
    auto      *mod                = Map::create();
    TileLayer *layer              = mod->newLayer(5, 5, 64.f, 32.f);
    layer->config()->orientation  = MapOrientation::Staggered;
    layer->config()->staggerAxis  = StaggerAxis::Y;
    layer->config()->staggerIndex = StaggerIndex::Odd;
    fillOpen(layer);

    Pathfinder *pf = mod->newPathfinder(layer);
    pf->setTopology("auto");
    CHECK_EQ(pf->getTopology(), std::string("hex"));

    Path *path = pf->findPath(0, 0, 4, 4);
    REQUIRE(path != nullptr);
    CHECK(path->getLength() > 0);
    CHECK(pathEndsAt(path, 4, 4));
    // Consecutive steps must be adjacent (chebyshev distance 1 in offset space is not
    // enough for hex; verify each step changes coords and stays walkable).
    for (int i = 1; i < path->getLength(); ++i) {
        const int x0 = path->getX(i - 1), y0 = path->getY(i - 1);
        const int x1 = path->getX(i), y1 = path->getY(i);
        const int adx = x1 > x0 ? x1 - x0 : x0 - x1;
        const int ady = y1 > y0 ? y1 - y0 : y0 - y1;
        CHECK(adx + ady > 0);
        CHECK(adx <= 1);
        CHECK(ady <= 1);
        CHECK(pf->isWalkable(x1, y1));
    }
    delete path;
    delete pf;
}

TEST_CASE("map.path.isometric.autoUsesOrtho") {
    auto      *mod               = Map::create();
    TileLayer *layer             = mod->newLayer(4, 4, 64.f, 32.f);
    layer->config()->orientation = MapOrientation::Isometric;
    fillOpen(layer);
    Pathfinder *pf = mod->newPathfinder(layer);
    pf->setTopology("auto");
    CHECK_EQ(pf->getTopology(), std::string("ortho4"));
    Path *path = pf->findPath(0, 0, 3, 3);
    CHECK(pathEndsAt(path, 3, 3));
    delete path;

    pf->setDiagonal(true);
    pf->setTopology("auto");
    CHECK_EQ(pf->getTopology(), std::string("ortho8"));
    delete pf;
}

TEST_CASE("map.path.blockGid.syncFromLayer") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(3, 1, 8.f, 8.f);
    layer->setTile(0, 0, 2);
    layer->setTile(1, 0, 2);
    layer->setTile(2, 0, 2);
    Pathfinder *pf = mod->newPathfinder(layer);
    pf->blockGid(1);
    Path *ok = pf->findPath(0, 0, 2, 0);
    CHECK(ok->getLength() > 0);
    delete ok;

    layer->setTile(1, 0, 1);
    pf->syncFromLayer();
    Path *blocked = pf->findPath(0, 0, 2, 0);
    CHECK_EQ(blocked->getLength(), 0);
    delete blocked;
    delete pf;
}

TEST_CASE("map.path.cellCost.prefersCheaper") {
    auto       *mod = Map::create();
    Pathfinder *pf  = mod->newPathfinderSize(3, 3);
    pf->setTopology("ortho4");
    pf->setCellCost(1, 1, 10.f);
    Path *path = pf->findPath(0, 1, 2, 1);
    REQUIRE(path->getLength() > 0);
    bool throughCenter = false;
    for (int i = 0; i < path->getLength(); ++i)
        if (path->getX(i) == 1 && path->getY(i) == 1) throughCenter = true;
    CHECK(!throughCenter);
    delete path;
    delete pf;
}

TEST_CASE("map.path.nullLayerReturnsNull") {
    auto *mod = Map::create();
    CHECK(mod->newPathfinder(nullptr) == nullptr);
    CHECK(mod->newPathfinderSize(0, 5) == nullptr);
}


TEST_CASE("map.path.accessorRoundTrips") {
    auto       *mod = Map::create();
    Pathfinder *pf  = mod->newPathfinderSize(4, 4);
    REQUIRE(pf != nullptr);

    pf->setDiagonal(true);
    CHECK(pf->getDiagonal());
    pf->setDiagonal(false);
    CHECK(!pf->getDiagonal());

    pf->setBlockEmpty(true);
    CHECK(pf->getBlockEmpty());
    pf->setBlockEmpty(false);
    CHECK(!pf->getBlockEmpty());

    pf->setCellCost(1, 1, 2.5f);
    CHECK_EQ(pf->getCellCost(1, 1), 2.5f);
    CHECK_EQ(pf->getCellCost(9, 9), 0.f);  // out of bounds

    pf->blockGid(7);
    pf->setBlocked(0, 0, true);
    pf->unblockGid(7);
    pf->clearBlockedGids();
    pf->invalidateCache();
    // setBlocked cells stay blocked until explicitly cleared; gid blocking is gone.
    CHECK(!pf->isWalkable(0, 0));
    pf->setBlocked(0, 0, false);
    CHECK(pf->isWalkable(0, 0));

    delete pf;
}

TEST_CASE("map.path.boundLayerAutoSyncsRevision") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(5, 1, 8.f, 8.f);
    layer->fill(2);
    Pathfinder *pf = mod->newPathfinder(layer);
    pf->blockGid(1);
    Path *initial = pf->findPath(0, 0, 4, 0);
    CHECK_GT(initial->getLength(), 0);
    delete initial;
    layer->setTile(2, 0, 1);
    Path *blocked = pf->findPath(0, 0, 4, 0);
    CHECK_EQ(blocked->getLength(), 0);
    delete blocked;
    layer->setTile(2, 0, 2);
    Path *open = pf->findPath(0, 0, 4, 0);
    CHECK_GT(open->getLength(), 0);
    delete open;
    delete pf;
}

TEST_CASE("map.path.dynamicEntryPenaltyDetoursWithoutMutatingGrid") {
    Pathfinder            pathfinder(5, 3);
    std::unique_ptr<Path> direct(pathfinder.findPath(0, 1, 4, 1));
    REQUIRE(direct.get() != nullptr);
    CHECK_EQ(direct->getLength(), 5);

    std::unique_ptr<Path> detour(
        pathfinder.findPath(0, 1, 4, 1, [](int x, int y) { return x == 2 && y == 1 ? 20.0f : 0.0f; }));
    REQUIRE(detour.get() != nullptr);
    CHECK_GT(detour->getLength(), direct->getLength());
    for (int index = 0; index < detour->getLength(); ++index)
        CHECK(!(detour->getX(index) == 2 && detour->getY(index) == 1));
    CHECK_EQ(pathfinder.getCellCost(2, 1), 1.0f);
}
}  // namespace test_map_fov_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_map_object_contract) || \
    defined(EVE_TEST_ORIGIN_resource_format_map)
#include "PrivateTestScope.h"
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include "map/Map.h"
#include "map/MapObjectContract.h"
#include "map/TileLayer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: map_object_formats
namespace test_map_object_formats {
// map_object_contract.cpp
namespace {
namespace map_object_contract {
TEST_CASE("map.objectContract.validatesRequiredKindsBoundsAndEnums") {
    const std::string   contract = R"({
        "schema":"eve.map.object-contract",
        "version":1,
        "types":[{"type":"portal","properties":[
            {"name":"target","kind":"string","required":true,"enum":["forest"]},
            {"name":"x","kind":"int","required":true,"min":0,"max":10},
            {"name":"enabled","kind":"bool","required":true}
        ]}]
    })";
    eve::map::MapObject object;
    object.name       = "gate";
    object.type       = "portal";
    object.properties = {{"target", "forest"}, {"x", "4"}, {"enabled", "true"}};
    std::vector objects{object};
    auto        valid = eve::map::validateMapObjects(objects, contract);
    CHECK(valid.ok());

    objects.front().properties["x"] = "4.5";
    auto wrongKind                  = eve::map::validateMapObjects(objects, contract);
    CHECK(!wrongKind.ok());
    objects.front().properties["x"] = "11";
    auto outsideBounds              = eve::map::validateMapObjects(objects, contract);
    CHECK(!outsideBounds.ok());
    objects.front().properties["x"]      = "4";
    objects.front().properties["target"] = "village";
    auto outsideEnum                     = eve::map::validateMapObjects(objects, contract);
    CHECK(!outsideEnum.ok());
}

TEST_CASE("map.objectContract.rejectsDuplicateNamesAndInvalidGeometry") {
    const std::string contract =
        R"({"schema":"eve.map.object-contract","version":1,"types":[{"type":"spawn","properties":[]}]})";
    eve::map::MapObject first;
    first.name = "spawn";
    first.type = "spawn";
    std::vector objects{first, first};
    auto        duplicate = eve::map::validateMapObjects(objects, contract);
    CHECK(!duplicate.ok());

    objects.resize(1);
    objects.front().width = -1.f;
    auto negativeGeometry = eve::map::validateMapObjects(objects, contract);
    CHECK(!negativeGeometry.ok());
    objects.front().width  = 0.f;
    objects.front().x      = std::numeric_limits<float>::infinity();
    auto nonFiniteGeometry = eve::map::validateMapObjects(objects, contract);
    CHECK(!nonFiniteGeometry.ok());
}

TEST_CASE("map.objectContract.rejectsMalformedContractDefinitions") {
    std::vector<eve::map::MapObject> objects;
    auto                             badVersion =
        eve::map::validateMapObjects(objects, R"({"schema":"eve.map.object-contract","version":2,"types":[]})");
    CHECK(!badVersion.ok());
    auto duplicateTypes = eve::map::validateMapObjects(
        objects, R"({"schema":"eve.map.object-contract","version":1,"types":[{"type":"spawn"},{"type":"spawn"}]})");
    CHECK(!duplicateTypes.ok());
}
}  // namespace map_object_contract
}  // namespace

// resource_format_map.cpp
namespace {
namespace resource_format_map {
TEST_CASE("resourceFormats.map.tiledFlipBitsSurviveAllWritePaths") {
    auto          *layer   = eve::map::Map::create()->newLayer(2, 2, 16.f, 16.f);
    const uint32_t flipped = 0x80000001u;
    layer->setTile(0, 0, static_cast<int>(flipped));
    REQUIRE_EQ(static_cast<uint32_t>(layer->getTile(0, 0)), flipped);
    REQUIRE_EQ(layer->getNonEmptyChunkCount(), 1);
    layer->clear();
    layer->fillRect(0, 0, 2, 1, static_cast<int>(flipped));
    REQUIRE_EQ(static_cast<uint32_t>(layer->getTile(1, 0)), flipped);
    REQUIRE_EQ(layer->getTile(1, 1), 0);
    layer->fill(static_cast<int>(flipped));
    REQUIRE_EQ(static_cast<uint32_t>(layer->getTile(1, 1)), flipped);
    REQUIRE_EQ(layer->getNonEmptyChunkCount(), 1);
    layer->fill(static_cast<int>(0x80000000u));
    REQUIRE_EQ(layer->getNonEmptyChunkCount(), 0);
    layer->clear();
    layer->setVisible(false);
}
}  // namespace resource_format_map
}  // namespace
}  // namespace test_map_object_formats
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_map)
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "data/DataModule.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "map/DualGrid.h"
#include "map/Fov.h"
#include "map/Map.h"
#include "map/MapObject.h"
#include "map/Pathfinder.h"
#include "map/TileCollision.h"
#include "map/TileConfig.h"
#include "map/TileLayer.h"
#include "map/TileProjection.h"
#include "map/TileSystem.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// map.cpp
namespace test_map_unit {
using namespace eve::map;

namespace {
class CollisionSinkMock final : public ITileCollisionSink {
public:
    void replaceTileCollision(TileLayerRef layer, const TileCollisionRect *rects, size_t count) override {
        source = layer;
        values.assign(rects, rects + count);
    }
    TileLayerRef                   source;
    std::vector<TileCollisionRect> values;
};
}  // namespace

namespace {

[[maybe_unused]] void hideAllTileLayers() {
    if (ecs::current()->getManager<TileLayer>() == nullptr) return;
    auto view = ecs::View<TileLayer, TileLayer::Draw>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [draw]   = *it;
        draw->visible = false;
    }
}

}  // namespace

TEST_CASE("map.newLayer.ecsView") {
    auto      *mod    = Map::create();
    int        before = mod->getLayerCount();
    TileLayer *layer  = mod->newLayer(4, 3, 16.f, 16.f);
    REQUIRE(layer != nullptr);
    CHECK_EQ(layer->getMapWidth(), 4);
    CHECK_EQ(layer->getMapHeight(), 3);
    CHECK_EQ(layer->getTileWidth(), 16.f);
    CHECK_EQ(layer->getTileHeight(), 16.f);
    CHECK_EQ(mod->getLayerCount(), before + 1);
    CHECK(layer->config()->entity == layer);
}

TEST_CASE("map.navigationProfile.drivesPathCollisionAndFov") {
    auto *mod   = Map::create();
    auto *layer = mod->newLayer(3, 1, 16.f, 16.f);
    layer->setTileNavigationProfile(7, false, 1.f, 0xff, 0xff, true, 0x20);
    layer->setTile(1, 0, 7);

    Pathfinder pathfinder(layer);
    Path      *path = pathfinder.findPath(0, 0, 2, 0);
    REQUIRE(path != nullptr);
    CHECK_EQ(path->getLength(), 0);
    delete path;

    CHECK_EQ(mod->publishCollision(layer), 1);
    Fov fov(layer);
    fov.syncFromLayer();
    CHECK(fov.isOpaque(1, 0));
}

TEST_CASE("map.layer.setGetTile") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(3, 2, 32.f, 32.f);
    CHECK_EQ(layer->getTile(0, 0), 0);
    layer->setTile(1, 1, 5);
    CHECK_EQ(layer->getTile(1, 1), 5);
    layer->setTile(-1, 0, 9);  // out of range no-op
    CHECK_EQ(layer->getTile(0, 0), 0);
    layer->fill(7);
    CHECK_EQ(layer->getTile(0, 0), 7);
    CHECK_EQ(layer->getTile(2, 1), 7);
    layer->clear();
    CHECK_EQ(layer->getTile(1, 0), 0);
}

TEST_CASE("map.layer.resizeClears") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(2, 2, 8.f, 8.f);
    layer->setTile(0, 0, 3);
    layer->resize(4, 1);
    CHECK_EQ(layer->getMapWidth(), 4);
    CHECK_EQ(layer->getMapHeight(), 1);
    CHECK_EQ(layer->getTile(0, 0), 0);
}

TEST_CASE("map.layer.chunkIndexAndRevision") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(96, 65, 8.f, 8.f);
    CHECK_EQ(layer->getChunkSize(), 32);
    CHECK_EQ(layer->getChunkCount(), 9);
    CHECK_EQ(layer->getNonEmptyChunkCount(), 0);
    const int revision = layer->getRevision();
    layer->setTile(0, 0, 1);
    layer->setTile(64, 64, 2);
    CHECK_EQ(layer->getNonEmptyChunkCount(), 2);
    CHECK_GT(layer->getRevision(), revision);
    const int unchanged = layer->getRevision();
    layer->setTile(64, 64, 2);
    CHECK_EQ(layer->getRevision(), unchanged);
    layer->setTile(0, 0, 0);
    CHECK_EQ(layer->getNonEmptyChunkCount(), 1);
}

TEST_CASE("map.layer.fillRectClipsAndPublishesOnce") {
    auto      *mod    = Map::create();
    TileLayer *layer  = mod->newLayer(64, 64, 8.f, 8.f);
    const int  before = layer->getRevision();
    layer->fillRect(-2, -2, 5, 5, 7);
    CHECK_EQ(layer->getTile(0, 0), 7);
    CHECK_EQ(layer->getTile(2, 2), 7);
    CHECK_EQ(layer->getTile(3, 3), 0);
    CHECK_EQ(layer->getNonEmptyChunkCount(), 1);
    CHECK_EQ(layer->getRevision(), before + 1);
    layer->fillRect(31, 31, 2, 2, 8);
    CHECK_EQ(layer->getNonEmptyChunkCount(), 4);
}

TEST_CASE("map.render.sparseChunksBoundCollectionWork") {
    hideAllTileLayers();
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(1024, 1024, 8.f, 8.f);
    layer->setTile(1, 1, 1);
    layer->setTile(900, 900, 2);
    std::vector<eve::graphics::DrawItem2D> items;
    TileRenderSystem::collect(items);
    CHECK_EQ(int(items.size()), 2);
    CHECK_EQ(TileRenderSystem::lastVisitedChunkCount(), 2);
    CHECK_EQ(TileRenderSystem::lastVisitedCellCount(), 2 * 32 * 32);
    layer->setVisible(false);
}

TEST_CASE("map.tileLayer.receiveLightDefaultsOffAndPropagates") {
    hideAllTileLayers();
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(2, 2, 16.f, 16.f);
    CHECK(!layer->getReceiveLight());
    layer->setTile(0, 0, 1);
    layer->setTile(1, 0, 2);
    std::vector<eve::graphics::DrawItem2D> items;
    TileRenderSystem::collect(items);
    REQUIRE(items.size() >= 2);
    CHECK(!items[0].receiveLight);
    CHECK(!items[0].litPath);

    layer->setReceiveLight(true);
    CHECK(layer->getReceiveLight());
    items.clear();
    TileRenderSystem::collect(items);
    REQUIRE(items.size() >= 2);
    // No tileset texture → solid color path; still marks receiveLight for modulateUnlit.
    CHECK(items[0].receiveLight);
    CHECK(!items[0].litPath);
    layer->setVisible(false);
}

TEST_CASE("map.layer.applyConfigFlat") {
    auto       *mod   = Map::create();
    TileLayer  *layer = mod->newLayer(2, 2, 16.f, 16.f);
    const char *json  = R"({
        "width": 2,
        "height": 2,
        "tilewidth": 16,
        "tileheight": 16,
        "data": [1, 2, 3, 4],
        "origin": [10, 20],
        "layer": 2,
        "visible": true,
        "tint": [1, 0.5, 0.25, 1]
    })";
    CHECK(layer->applyConfig(json));
    CHECK_EQ(layer->getTile(0, 0), 1);
    CHECK_EQ(layer->getTile(1, 0), 2);
    CHECK_EQ(layer->getTile(0, 1), 3);
    CHECK_EQ(layer->getTile(1, 1), 4);
    CHECK_EQ(layer->getX(), 10.f);
    CHECK_EQ(layer->getY(), 20.f);
    CHECK_EQ(layer->getLayer(), 2);
    CHECK(layer->isVisible());
}

TEST_CASE("map.layer.applyConfigMultiLayerPicksFirst") {
    auto       *mod   = Map::create();
    TileLayer  *layer = mod->newLayer(2, 1, 8.f, 8.f);
    const char *json  = R"({
        "width": 2,
        "height": 1,
        "tilewidth": 8,
        "tileheight": 8,
        "layers": [
            { "type": "tilelayer", "width": 2, "height": 1, "data": [9, 8] },
            { "type": "objectgroup", "objects": [] },
            { "type": "tilelayer", "width": 2, "height": 1, "data": [1, 1] }
        ]
    })";
    CHECK(layer->applyConfig(json));
    CHECK_EQ(layer->getTile(0, 0), 9);
    CHECK_EQ(layer->getTile(1, 0), 8);
}

TEST_CASE("map.tileGid.masksFlipFlags") {
    CHECK_EQ(tileGid(0), 0u);
    CHECK_EQ(tileGid(5), 5u);
    CHECK_EQ(tileGid(0x80000005u), 5u);
}

TEST_CASE("map.tileset.irregularVisualMetadata") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(2, 1, 64.f, 32.f);
    layer->setTileVisual(7, 10, 20, 96, 128, 48.f, 104.f, 3.f);
    layer->setTileMetadata(7, 2, 1, false, 4.f);
    REQUIRE(layer->getTileVisualCount() == 1);
    const auto &visual = layer->tileset()->visuals[0];
    CHECK_EQ(visual.gid, 7);
    CHECK_EQ(visual.x, 10);
    CHECK_EQ(visual.y, 20);
    CHECK_EQ(visual.width, 96);
    CHECK_EQ(visual.height, 128);
    CHECK_EQ(visual.pivotX, 48.f);
    CHECK_EQ(visual.pivotY, 104.f);
    CHECK_EQ(visual.sortBias, 3.f);
    CHECK_EQ(visual.footprintW, 2);
    CHECK(!visual.walkable);
    CHECK_EQ(visual.cost, 4.f);
    layer->clearTileVisuals();
    CHECK_EQ(layer->getTileVisualCount(), 0);
    layer->setVisible(false);
}

TEST_CASE("map.tileset.manifestShapeInMapConfig") {
    auto       *mod   = Map::create();
    TileLayer  *layer = mod->newLayer(1, 1, 64.f, 32.f);
    const char *json  = R"({
      "width":1,"height":1,"orientation":"isometric","data":[7],
      "tileset":{
        "image":"missing-atlas.png","firstGid":1,
        "tiles":[{
          "gid":7,"region":[10,20,96,128],"pivot":[48,104],
          "footprint":[2,1],"walkable":false,"cost":4,"sortBias":3
        }]
      }
    })";
    CHECK(layer->applyConfig(json));
    REQUIRE(layer->getTileVisualCount() == 1);
    const auto &visual = layer->tileset()->visuals[0];
    CHECK_EQ(visual.gid, 7);
    CHECK_EQ(visual.width, 96);
    CHECK_EQ(visual.pivotY, 104.f);
    CHECK_EQ(visual.footprintW, 2);
    CHECK(!visual.walkable);
    layer->setVisible(false);
}

TEST_CASE("map.layer.applyTiledInfiniteChunks") {
    auto       *mod   = Map::create();
    TileLayer  *layer = mod->newLayer(1, 1, 16.f, 16.f);
    const char *json  = R"({
      "infinite":true,"width":0,"height":0,"tilewidth":16,"tileheight":16,
      "layers":[{"type":"tilelayer","chunks":[
        {"x":-2,"y":-1,"width":2,"height":2,"data":[1,2,3,4]},
        {"x":0,"y":-1,"width":2,"height":2,"data":[5,6,7,8]}
      ]}]
    })";
    CHECK(layer->applyConfig(json));
    CHECK_EQ(layer->getMapWidth(), 4);
    CHECK_EQ(layer->getMapHeight(), 2);
    CHECK_EQ(layer->getTile(0, 0), 1);
    CHECK_EQ(layer->getTile(3, 1), 8);
    CHECK_EQ(layer->getX(), -32.f);
    CHECK_EQ(layer->getY(), -16.f);
}

TEST_CASE("map.tileset.v2AnimationCustomDataAndTerrain") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(3, 3, 16.f, 16.f);
    layer->addTileAnimationFrame(10, 11, 100);
    layer->addTileAnimationFrame(10, 12, 150);
    CHECK_EQ(layer->getTileAnimationFrameCount(10), 2);
    layer->setTileDataString(10, "biome", "forest");
    layer->setTileDataNumber(10, "damage", 2.5f);
    layer->setTileDataBool(10, "wet", true);
    CHECK_EQ(layer->getTileDataType(10, "damage"), std::string("number"));
    CHECK_EQ(layer->getTileDataString(10, "biome"), std::string("forest"));
    CHECK_EQ(layer->getTileDataNumber(10, "damage"), 2.5f);
    CHECK(layer->getTileDataBool(10, "wet"));
    layer->setTerrainRule(20, 1, 0);
    layer->setTerrainRule(21, 1, 1 << 3);
    layer->paintTerrain(1, 1, 1);
    CHECK_EQ(layer->getTerrain(1, 1), 1);
    CHECK_EQ(layer->getTile(1, 1), 20);
    layer->paintTerrain(2, 1, 1);
    CHECK_EQ(layer->getTile(1, 1), 21);
    layer->clearTileAnimation(10);
    CHECK_EQ(layer->getTileAnimationFrameCount(10), 0);
}

TEST_CASE("map.collision.greedyMergeAndCapabilityPublish") {
    Map        map;
    TileLayer *layer = map.newLayer(5, 4, 16.f, 8.f);
    layer->setTileMetadata(9, 1, 1, false);
    layer->fillRect(1, 1, 3, 2, 9);
    CollisionSinkMock sink;
    eve::cap::addListener<ITileCollisionSink>(&sink);

    CHECK(map.publishCollision(layer) == 1);
    CHECK(map.getCollisionRectCount() == 1);
    CHECK(map.getCollisionRectX(0) == 16.f);
    CHECK(map.getCollisionRectY(0) == 8.f);
    CHECK(map.getCollisionRectWidth(0) == 48.f);
    CHECK(map.getCollisionRectHeight(0) == 16.f);
    CHECK_EQ(sink.source.slot, layer->layerRef().slot);
    CHECK_EQ(sink.source.generation, layer->layerRef().generation);
    REQUIRE(sink.values.size() == 1);
    CHECK(sink.values[0].width == 48.f);
    eve::cap::removeListener<ITileCollisionSink>(&sink);
}

TEST_CASE("map.tileset.v2ManifestReadsTiledFields") {
    auto       *mod   = Map::create();
    TileLayer  *layer = mod->newLayer(1, 1, 16.f, 16.f);
    const char *json  = R"({
      "width":1,"height":1,"data":[1],
      "tileset":{"firstGid":1,"columns":4,"tileWidth":16,"tileHeight":16,
        "tiles":[{"id":0,"animation":[{"tileid":1,"duration":80},{"tileid":2,"duration":120}],
          "terrain":2,"neighborMask":0,
          "properties":[{"name":"damage","type":"float","value":3.5}]}]}
    })";
    CHECK(layer->applyConfig(json));
    CHECK_EQ(layer->getTileAnimationFrameCount(1), 2);
    CHECK_EQ(layer->getTileDataType(1, "damage"), std::string("float"));
    CHECK_EQ(layer->getTileDataString(1, "damage"), std::string("3.5"));
    layer->paintTerrain(0, 0, 2);
    CHECK_EQ(layer->getTile(0, 0), 1);
}

TEST_CASE("map.autotile.wallAndWaterfallResolveExactDirtyRegion") {
    auto      *mod  = Map::create();
    TileLayer *wall = mod->newLayer(3, 5, 16.f, 16.f);
    wall->defineAutotileFamily(4, "wall", 17);
    wall->setAutotileRule(40, 4, 1 << 5, 1);               // top: connected south
    wall->setAutotileRule(41, 4, (1 << 1) | (1 << 5), 1);  // body
    wall->setAutotileRule(42, 4, 1 << 1, 1);               // foot: connected north
    const int before = wall->getRevision();
    wall->paintTerrainRect(1, 1, 1, 3, 4);
    CHECK_EQ(wall->getRevision(), before + 1);
    CHECK_EQ(wall->getTile(1, 1), 40);
    CHECK_EQ(wall->getTile(1, 2), 41);
    CHECK_EQ(wall->getTile(1, 3), 42);

    TileLayer *waterfall = mod->newLayer(1, 3, 16.f, 16.f);
    waterfall->defineAutotileFamily(8, "waterfall", 23);
    waterfall->setAutotileRule(80, 8, 1 << 5, 1);
    waterfall->setAutotileRule(81, 8, (1 << 1) | (1 << 5), 1);
    waterfall->setAutotileRule(82, 8, 1 << 1, 1);
    waterfall->paintTerrainRect(0, 0, 1, 3, 8);
    CHECK_EQ(waterfall->getTile(0, 0), 80);
    CHECK_EQ(waterfall->getTile(0, 1), 81);
    CHECK_EQ(waterfall->getTile(0, 2), 82);
    waterfall->addTileAnimationFrame(81, 81, 120);
    waterfall->addTileAnimationFrame(81, 83, 120);
    CHECK_EQ(waterfall->getTileAnimationFrameCount(81), 2);
}

TEST_CASE("map.rpgmaker.importsPlanesPassageAndSemanticsTransactionally") {
    const std::string dataDir  = std::string(EVENGINE_SOURCE_DIR) + "/test/data/rpgmaker/";
    auto              imported = importRpgMakerMap(dataDir + "Map001.json", dataDir + "Tilesets.json", "RPG Maker MZ");
    REQUIRE(imported.ok());
    auto receipt = std::move(imported).value();
    CHECK_EQ(receipt.sourceEngine, std::string("RPG Maker MZ"));
    CHECK_EQ(receipt.tilesetId, 1);
    REQUIRE(receipt.layers.size() == 4);
    TileLayer *ground = receipt.layers.front();
    CHECK_EQ(ground->getTile(0, 0), 1);
    CHECK_EQ(ground->getTile(1, 0), 2);
    CHECK_EQ(ground->resource()->sourceVersion, std::string("MV/MZ"));
    REQUIRE(receipt.navigationLayer != nullptr);
    Pathfinder pathfinder(receipt.navigationLayer);
    CHECK(!pathfinder.isWalkable(0, 0));
    CHECK(pathfinder.isWalkable(1, 0));
    REQUIRE(ground->tileset()->atlases.size() >= 6);

    auto failed = importRpgMakerMap(dataDir + "missing.json", dataDir + "Tilesets.json");
    CHECK(!failed.ok());
    REQUIRE(failed.error() != nullptr);
    CHECK_EQ(failed.error()->source(), std::string("map.rpgmaker"));
}

TEST_CASE("map.rpgmaker.decodesA1A3A4QuarterTileTablesAndAnimation") {
    const auto water = decodeRpgMakerTileVisual(2048);
    REQUIRE(water.subtileFrames.size() == 4);
    CHECK_EQ(water.subtileFrames[0].parts.size(), size_t(4));
    CHECK_NE(water.subtileFrames[0].parts[0].x, water.subtileFrames[1].parts[0].x);

    const auto waterfall = decodeRpgMakerTileVisual(2048 + 5 * 48);
    REQUIRE(waterfall.subtileFrames.size() == 3);
    CHECK_NE(waterfall.subtileFrames[0].parts[0].y, waterfall.subtileFrames[1].parts[0].y);

    const auto roof = decodeRpgMakerTileVisual(4352);
    REQUIRE(roof.subtileFrames.size() == 1);
    CHECK_EQ(roof.subtileFrames[0].parts.size(), size_t(4));
    const auto wall = decodeRpgMakerTileVisual(5888 + 8 * 48);
    REQUIRE(wall.subtileFrames.size() == 1);
    CHECK_EQ(wall.subtileFrames[0].parts.size(), size_t(4));
}

TEST_CASE("map.tileset.importsTiledWangConnectivity") {
    auto       *mod   = Map::create();
    TileLayer  *layer = mod->newLayer(2, 1, 16.f, 16.f);
    const char *json  = R"({
      "width":2,"height":1,"data":[0,0],
      "tileset":{"firstgid":10,"columns":4,"tilewidth":16,"tileheight":16,
        "wangsets":[{"name":"shore","wangtiles":[
          {"tileid":2,"wangid":[0,0,1,0,0,0,0,0]},
          {"tileid":3,"wangid":[0,0,0,0,0,0,1,0]}
        ]}]}
    })";
    CHECK(layer->applyConfig(json));
    REQUIRE(layer->tileset()->terrainRules.size() == 2);
    CHECK_EQ(layer->tileset()->terrainRules[0].gid, 12);
    CHECK_EQ(layer->tileset()->terrainRules[0].terrain, 1);
    CHECK_EQ(layer->tileset()->terrainRules[0].neighborMask, 1 << 3);
    CHECK_EQ(layer->tileset()->terrainRules[1].gid, 13);
    CHECK_EQ(layer->tileset()->terrainRules[1].neighborMask, 1 << 7);
    layer->setVisible(false);
}


TEST_CASE("map.tiled.flattensNestedGroupsWithInheritedPresentation") {
    const std::string json = R"({
      "width":2,"height":1,"tilewidth":16,"tileheight":16,
      "layers":[{"type":"group","offsetx":7,"offsety":9,"opacity":0.5,
        "layers":[{"type":"tilelayer","width":2,"height":1,"data":[1,0]}]}]
    })";
    std::string       error;
    auto              layers = loadMapText(json, nullptr, &error);
    CHECK_EQ(error, std::string());
    REQUIRE(layers.size() == 1);
    CHECK_EQ(layers[0]->getX(), 7.f);
    CHECK_EQ(layers[0]->getY(), 9.f);
    CHECK_EQ(layers[0]->draw()->tint.a, 0.5f);
}

TEST_CASE("map.import.failureRollsBackObservableLayerState") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(2, 1, 16.f, 16.f);
    layer->setTile(0, 0, 7);
    const int   revision = layer->getRevision();
    std::string error;
    CHECK(!applyConfigText(layer, R"({"width":4,"height":4,"data":[1]})", &error));
    CHECK(!error.empty());
    CHECK_EQ(layer->getMapWidth(), 2);
    CHECK_EQ(layer->getMapHeight(), 1);
    CHECK_EQ(layer->getTile(0, 0), 7);
    CHECK_EQ(layer->getRevision(), revision);
}

TEST_CASE("map.tiled.preservesAndRendersUnsignedFlipFlags") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(2, 1, 16.f, 16.f);
    layer->setLayer(999);
    CHECK(layer->applyConfig(R"({"width":2,"height":1,"data":[2147483649,536870913]})"));
    CHECK_EQ(tileGid(uint32_t(layer->getTile(0, 0))), uint32_t(1));
    std::vector<eve::graphics::DrawItem2D> items;
    TileRenderSystem::collect(items);
    items.erase(std::remove_if(items.begin(), items.end(), [](const auto &item) { return item.layer != 999; }),
                items.end());
    REQUIRE(items.size() == 2);
    const auto &horizontal = items[0];
    const auto &diagonal   = items[1];
    CHECK(horizontal.flipX);
    CHECK_EQ(horizontal.rotation, 0.f);
    CHECK_EQ(diagonal.rotation, 270.f);
    CHECK(diagonal.flipX);
}

TEST_CASE("map.render.nullSafe") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(2, 2, 8.f, 8.f);
    layer->fill(1);
    TileRenderSystem::render(nullptr);
}

TEST_CASE("map.projection.orthogonal") {
    TileLayer::Config cfg;
    cfg.mapW        = 10;
    cfg.mapH        = 10;
    cfg.tileW       = 32.f;
    cfg.tileH       = 32.f;
    cfg.originX     = 10.f;
    cfg.originY     = 20.f;
    cfg.orientation = MapOrientation::Orthogonal;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 2, 3, wx, wy);
    CHECK_EQ(wx, 10.f + 2 * 32.f);
    CHECK_EQ(wy, 20.f + 3 * 32.f);
    CHECK_EQ(tileToDepthY(cfg, 2, 3), 20.f + 4 * 32.f);
    int tx = -1, ty = -1;
    worldToTile(cfg, wx + 1.f, wy + 1.f, tx, ty);
    CHECK_EQ(tx, 2);
    CHECK_EQ(ty, 3);
}

TEST_CASE("map.projection.isometric") {
    TileLayer::Config cfg;
    cfg.tileW       = 64.f;
    cfg.tileH       = 32.f;
    cfg.originX     = 0.f;
    cfg.originY     = 0.f;
    cfg.orientation = MapOrientation::Isometric;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 1, 0, wx, wy);
    CHECK_EQ(wx, 32.f);
    CHECK_EQ(wy, 16.f);
    tileToWorld(cfg, 0, 1, wx, wy);
    CHECK_EQ(wx, -32.f);
    CHECK_EQ(wy, 16.f);
    CHECK_EQ(tileToDepthY(cfg, 1, 0), 16.f + 32.f);
}

TEST_CASE("map.projection.isometric.renderSpacingRoundTrip") {
    TileLayer::Config cfg;
    cfg.mapW        = 8;
    cfg.mapH        = 8;
    cfg.tileW       = 64.f;
    cfg.tileH       = 32.f;
    cfg.cellGapX    = 16.f;
    cfg.cellGapY    = 8.f;
    cfg.originX     = 100.f;
    cfg.originY     = 20.f;
    cfg.orientation = MapOrientation::Isometric;
    float wx = 0.f, wy = 0.f;
    tileToWorld(cfg, 3, 2, wx, wy);
    CHECK_EQ(wx, 140.f);
    CHECK_EQ(wy, 120.f);
    int tx = -1, ty = -1;
    worldToTile(cfg, wx, wy, tx, ty);
    CHECK_EQ(tx, 3);
    CHECK_EQ(ty, 2);
}

TEST_CASE("map.layer.renderSpacingApi") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(2, 2, 80.f, 40.f);
    layer->setRenderSpacing(1.25f, 1.5f);
    CHECK_EQ(layer->getCellGapX(), 20.f);
    CHECK_EQ(layer->getCellGapY(), 20.f);
    CHECK_EQ(layer->getRenderSpacingX(), 1.25f);
    CHECK_EQ(layer->getRenderSpacingY(), 1.5f);
}

TEST_CASE("map.projection.staggeredYOdd") {
    TileLayer::Config cfg;
    cfg.tileW        = 64.f;
    cfg.tileH        = 32.f;
    cfg.originX      = 0.f;
    cfg.originY      = 0.f;
    cfg.orientation  = MapOrientation::Staggered;
    cfg.staggerAxis  = StaggerAxis::Y;
    cfg.staggerIndex = StaggerIndex::Odd;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 0, 0, wx, wy);
    CHECK_EQ(wx, 0.f);
    CHECK_EQ(wy, 0.f);
    tileToWorld(cfg, 0, 1, wx, wy);
    CHECK_EQ(wx, 32.f);
    CHECK_EQ(wy, 16.f);
}

TEST_CASE("map.config.orientationIsometric") {
    auto       *mod   = Map::create();
    TileLayer  *layer = mod->newLayer(2, 2, 64.f, 32.f);
    const char *json  = R"({
        "width": 2, "height": 2, "tilewidth": 64, "tileheight": 32,
        "orientation": "isometric",
        "data": [1, 0, 0, 1]
    })";
    CHECK(layer->applyConfig(json));
    CHECK_EQ(static_cast<int>(layer->config()->orientation), static_cast<int>(MapOrientation::Isometric));
}

TEST_CASE("map.config.unknownOrientationFails") {
    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(1, 1, 8.f, 8.f);
    CHECK(!layer->applyConfig(R"({"width":1,"height":1,"orientation":"banana","data":[1]})"));
}

TEST_CASE("map.decode.zstdFails") {
    auto       *mod   = Map::create();
    TileLayer  *layer = mod->newLayer(1, 1, 8.f, 8.f);
    const char *json =
        R"({"width":1,"height":1,"tilewidth":8,"tileheight":8,
        "encoding":"base64","compression":"zstd","data":"AAAA"})";
    CHECK(!layer->applyConfig(json));
}

TEST_CASE("map.objects.parseObjectGroup") {
    const char            *json = R"({
      "width": 2, "height": 1, "tilewidth": 16, "tileheight": 16,
      "layers": [
        { "type": "tilelayer", "width": 2, "height": 1, "data": [1, 0] },
        { "type": "objectgroup", "objects": [
            { "name": "spawn", "type": "player", "x": 8, "y": 24, "width": 16, "height": 16 }
        ]}
      ]
    })";
    std::vector<MapObject> objs;
    std::string            err;
    auto                   layers = loadMapText(json, &objs, &err);
    CHECK(err.empty());
    REQUIRE(layers.size() == 1);
    REQUIRE(objs.size() == 1);
    CHECK_EQ(objs[0].name, "spawn");
    CHECK_EQ(objs[0].type, "player");
    CHECK_EQ(objs[0].x, 8.f);
    CHECK_EQ(objs[0].y, 24.f);
}

TEST_CASE("map.render.tilesContributeDepthY") {
    hideAllTileLayers();
    auto      *mod               = Map::create();
    TileLayer *layer             = mod->newLayer(1, 2, 32.f, 32.f);
    layer->config()->orientation = MapOrientation::Orthogonal;
    layer->setOrigin(0, 0);
    layer->setTile(0, 0, 1);
    layer->setTile(0, 1, 1);
    std::vector<eve::graphics::DrawItem2D> items;
    TileRenderSystem::collect(items);
    REQUIRE(items.size() == 2);
    eve::graphics::sortDrawItems2D(items);
    CHECK(items[0].depthY < items[1].depthY);
    layer->clear();
    layer->setVisible(false);
}

TEST_CASE("map.drawItem.sortByDepthY") {
    using eve::graphics::DrawItem2D;
    std::vector<DrawItem2D> items(2);
    items[0].layer  = 0;
    items[0].depthY = 100.f;
    items[0].canvas = nullptr;
    items[1].layer  = 0;
    items[1].depthY = 50.f;
    items[1].canvas = nullptr;
    eve::graphics::sortDrawItems2D(items);
    CHECK_EQ(items[0].depthY, 50.f);
    CHECK_EQ(items[1].depthY, 100.f);
}

TEST_CASE("map.projection.isometric.worldToTile") {
    TileLayer::Config cfg;
    cfg.tileW       = 64.f;
    cfg.tileH       = 32.f;
    cfg.originX     = 0.f;
    cfg.originY     = 0.f;
    cfg.orientation = MapOrientation::Isometric;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 1, 0, wx, wy);
    int tx = -1, ty = -1;
    worldToTile(cfg, wx, wy, tx, ty);
    CHECK_EQ(tx, 1);
    CHECK_EQ(ty, 0);
    tileToWorld(cfg, 0, 1, wx, wy);
    worldToTile(cfg, wx, wy, tx, ty);
    CHECK_EQ(tx, 0);
    CHECK_EQ(ty, 1);
}

TEST_CASE("map.projection.staggeredYOdd.worldToTileAndDepth") {
    TileLayer::Config cfg;
    cfg.mapW         = 4;
    cfg.mapH         = 4;
    cfg.tileW        = 64.f;
    cfg.tileH        = 32.f;
    cfg.originX      = 0.f;
    cfg.originY      = 0.f;
    cfg.orientation  = MapOrientation::Staggered;
    cfg.staggerAxis  = StaggerAxis::Y;
    cfg.staggerIndex = StaggerIndex::Odd;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 0, 1, wx, wy);
    CHECK_EQ(wx, 32.f);
    CHECK_EQ(wy, 16.f);
    CHECK_EQ(tileToDepthY(cfg, 0, 1), 16.f + 32.f);
    // Nearest tile center
    int tx = -1, ty = -1;
    worldToTile(cfg, wx + 32.f, wy + 16.f, tx, ty);
    CHECK_EQ(tx, 0);
    CHECK_EQ(ty, 1);
}

TEST_CASE("map.projection.staggeredXEven") {
    TileLayer::Config cfg;
    cfg.mapW         = 3;
    cfg.mapH         = 3;
    cfg.tileW        = 64.f;
    cfg.tileH        = 32.f;
    cfg.orientation  = MapOrientation::Staggered;
    cfg.staggerAxis  = StaggerAxis::X;
    cfg.staggerIndex = StaggerIndex::Even;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 0, 0, wx, wy);
    // major=tx=0 even → staggerIndex Even → shift (since !odd)
    CHECK_EQ(wx, 0.f);
    CHECK_EQ(wy, 16.f);
    tileToWorld(cfg, 1, 0, wx, wy);
    CHECK_EQ(wx, 32.f);
    CHECK_EQ(wy, 0.f);
}

TEST_CASE("map.projection.hexagonalYOdd") {
    TileLayer::Config cfg;
    cfg.mapW          = 3;
    cfg.mapH          = 3;
    cfg.tileW         = 64.f;
    cfg.tileH         = 32.f;
    cfg.hexSideLength = 16.f;
    cfg.orientation   = MapOrientation::Hexagonal;
    cfg.staggerAxis   = StaggerAxis::Y;
    cfg.staggerIndex  = StaggerIndex::Odd;
    float wx = 0, wy = 0;
    tileToWorld(cfg, 0, 0, wx, wy);
    CHECK_EQ(wx, 0.f);
    CHECK_EQ(wy, 0.f);
    // pitchY = (32+16)/2 = 24; row 1 odd → +tw/2
    tileToWorld(cfg, 0, 1, wx, wy);
    CHECK_EQ(wx, 32.f);
    CHECK_EQ(wy, 24.f);
    CHECK_EQ(tileToDepthY(cfg, 0, 1), 24.f + 32.f);
    int tx = -1, ty = -1;
    worldToTile(cfg, wx + 32.f, wy + 16.f, tx, ty);
    CHECK_EQ(tx, 0);
    CHECK_EQ(ty, 1);
}

TEST_CASE("map.decode.base64ZlibRoundTrip") {
    // 2x2 little-endian GIDs: 1,2,3,4
    const unsigned char        raw[16] = {1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0};
    eve::data::CompressedData *cdata   = eve::data::compress("zlib", reinterpret_cast<const char *>(raw), 16, -1);
    REQUIRE(cdata != nullptr);
    const char  *compressed     = reinterpret_cast<const char *>(cdata->getData());
    const size_t compressedSize = cdata->getSize();
    size_t       b64len         = 0;
    char        *b64raw         = eve::data::encode("base64", compressed, compressedSize, b64len);
    REQUIRE(b64raw != nullptr);
    REQUIRE(b64len > 0);
    std::string b64(b64raw, b64len);
    delete[] b64raw;

    std::string json = std::string(R"({
      "width": 2, "height": 2, "tilewidth": 16, "tileheight": 16,
      "layers": [{
        "type": "tilelayer", "width": 2, "height": 2,
        "encoding": "base64", "compression": "zlib",
        "data": ")") + b64 +
                       R"("}]})";

    std::string err;
    auto        layers = loadMapText(json, nullptr, &err);
    CHECK(err.empty());
    REQUIRE(layers.size() == 1);
    CHECK_EQ(layers[0]->getTile(0, 0), 1);
    CHECK_EQ(layers[0]->getTile(1, 0), 2);
    CHECK_EQ(layers[0]->getTile(0, 1), 3);
    CHECK_EQ(layers[0]->getTile(1, 1), 4);
    delete cdata;
}

TEST_CASE("map.decode.base64Uncompressed") {
    const unsigned char raw[16] = {1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0};
    size_t              b64len  = 0;
    char               *b64raw  = eve::data::encode("base64", reinterpret_cast<const char *>(raw), 16, b64len);
    REQUIRE(b64raw != nullptr);
    std::string b64(b64raw, b64len);
    delete[] b64raw;

    std::string json = std::string(R"({
      "width": 2, "height": 2, "tilewidth": 16, "tileheight": 16,
      "encoding": "base64", "compression": "",
      "data": ")") + b64 +
                       R"("})";

    auto      *mod   = Map::create();
    TileLayer *layer = mod->newLayer(2, 2, 16.f, 16.f);
    CHECK(layer->applyConfig(json));
    CHECK_EQ(layer->getTile(0, 0), 1);
    CHECK_EQ(layer->getTile(1, 1), 4);
}

TEST_CASE("map.load.tiledLikeIsometricWithObjects") {
    const char            *json = R"({
      "width": 2, "height": 2, "tilewidth": 64, "tileheight": 32,
      "orientation": "isometric",
      "layers": [
        { "type": "tilelayer", "width": 2, "height": 2, "data": [1, 0, 0, 2] },
        { "type": "objectgroup", "objects": [
            { "name": "npc", "type": "talker", "x": 10, "y": 20, "width": 8, "height": 8, "gid": 3 }
        ]}
      ]
    })";
    std::vector<MapObject> objs;
    std::string            err;
    auto                   layers = loadMapText(json, &objs, &err);
    CHECK(err.empty());
    REQUIRE(layers.size() == 1);
    CHECK_EQ(static_cast<int>(layers[0]->config()->orientation), static_cast<int>(MapOrientation::Isometric));
    CHECK_EQ(layers[0]->getTile(0, 0), 1);
    CHECK_EQ(layers[0]->getTile(1, 1), 2);
    float wx = 0, wy = 0;
    layers[0]->tileToWorld(1, 0, wx, wy);
    CHECK_EQ(wx, 32.f);
    CHECK_EQ(wy, 16.f);
    REQUIRE(objs.size() == 1);
    CHECK_EQ(objs[0].name, "npc");
    CHECK_EQ(objs[0].type, "talker");
    CHECK_EQ(objs[0].gid, 3);
}

TEST_CASE("map.dualGrid.maskAndFrameTable") {
    CHECK_EQ(dualGridMaskFromCorners(false, false, false, false), 0);
    CHECK_EQ(dualGridMaskFromCorners(true, false, false, false), 1);
    CHECK_EQ(dualGridMaskFromCorners(false, true, false, false), 2);
    CHECK_EQ(dualGridMaskFromCorners(false, false, true, false), 4);
    CHECK_EQ(dualGridMaskFromCorners(false, false, false, true), 8);
    CHECK_EQ(dualGridMaskFromCorners(true, true, true, true), 15);
    // Diagonal TL+BR (classic dual-grid case that blob edge-masks miss).
    CHECK_EQ(dualGridMaskFromCorners(true, false, false, true), 9);
    CHECK_EQ(dualGridDefaultFrame(0), -1);
    CHECK_EQ(dualGridDefaultFrame(15), 6);
    CHECK_EQ(dualGridDefaultFrame(9), 4);
    CHECK_EQ(dualGridDefaultFrameTable()[1], 15);
}

TEST_CASE("map.dualGrid.resolveHalfOffsetAndSize") {
    auto      *mod     = Map::create();
    TileLayer *logic   = mod->newLayer(2, 2, 32.f, 32.f);
    TileLayer *display = mod->newLayer(1, 1, 8.f, 8.f);
    logic->setOrigin(10.f, 20.f);
    logic->setTile(0, 0, 1);
    logic->setTile(1, 1, 1);

    DualGridOptions opts;
    opts.useDefaultFrameTable = false;  // gid = firstGid + mask
    opts.firstDisplayGid      = 10;
    opts.hideLogic            = true;
    std::string err;
    CHECK(resolveDualGrid(logic, display, opts, &err));
    CHECK(err.empty());
    CHECK_EQ(display->getMapWidth(), 3);
    CHECK_EQ(display->getMapHeight(), 3);
    CHECK_EQ(display->getTileWidth(), 32.f);
    CHECK_EQ(display->getX(), 10.f - 16.f);
    CHECK_EQ(display->getY(), 20.f - 16.f);
    CHECK(!logic->isVisible());
    CHECK(display->isVisible());

    // Display (1,1) samples logic (0,0),(1,0),(0,1),(1,1) → TL+BR = mask 9
    CHECK_EQ(dualGridMaskAt(*logic, 1, 1, 0), 9);
    CHECK_EQ(display->getTile(1, 1), 10 + 9);
    // Display (0,0) samples only out-of-bounds + logic(0,0) as BR → mask 8
    CHECK_EQ(dualGridMaskAt(*logic, 0, 0, 0), 8);
    CHECK_EQ(display->getTile(0, 0), 10 + 8);
    // Empty neighborhood
    CHECK_EQ(display->getTile(2, 0), 0);

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.dualGrid.resolveRejectsSameLayer") {
    auto       *mod   = Map::create();
    TileLayer  *layer = mod->newLayer(2, 2, 16.f, 16.f);
    std::string err;
    CHECK(!resolveDualGrid(layer, layer, DualGridOptions{}, &err));
    CHECK(!err.empty());
    CHECK(!mod->resolveDualGrid(nullptr, layer));
    CHECK(!mod->lastDualGridError().empty());
    layer->clear();
    layer->setVisible(false);
}

TEST_CASE("map.dualGrid.filledGidFilter") {
    auto      *mod     = Map::create();
    TileLayer *logic   = mod->newLayer(1, 1, 16.f, 16.f);
    TileLayer *display = mod->newLayer(1, 1, 16.f, 16.f);
    logic->setTile(0, 0, 5);
    DualGridOptions opts;
    opts.filledGid            = 9;  // 5 should not count
    opts.useDefaultFrameTable = false;
    opts.firstDisplayGid      = 1;
    opts.hideLogic            = false;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));
    // All display cells empty when filled filter mismatches
    CHECK_EQ(display->getTile(0, 0), 0);
    CHECK_EQ(display->getTile(1, 0), 0);
    CHECK_EQ(display->getTile(0, 1), 0);
    CHECK_EQ(display->getTile(1, 1), 0);
    CHECK(logic->isVisible());

    CHECK(mod->resolveDualGridFilled(logic, display, 5));
    // logic(0,0) is BR of display(0,0) and TL of display(1,1).
    CHECK_EQ(dualGridMaskAt(*logic, 0, 0, 5), 8);
    CHECK_EQ(dualGridMaskAt(*logic, 1, 1, 5), 1);
    CHECK_EQ(display->getTile(0, 0), 1 + dualGridDefaultFrame(8));
    CHECK_EQ(display->getTile(1, 1), 1 + dualGridDefaultFrame(1));

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.dualGrid.halfOffset.orthogonalIsoStaggerHex") {
    TileLayer::Config ortho;
    ortho.orientation = MapOrientation::Orthogonal;
    ortho.tileW       = 32.f;
    ortho.tileH       = 32.f;
    float ox = 0, oy = 0;
    dualGridHalfOffset(ortho, ox, oy);
    CHECK_EQ(ox, -16.f);
    CHECK_EQ(oy, -16.f);

    TileLayer::Config iso;
    iso.orientation = MapOrientation::Isometric;
    iso.tileW       = 64.f;
    iso.tileH       = 32.f;
    dualGridHalfOffset(iso, ox, oy);
    CHECK_EQ(ox, 0.f);
    CHECK_EQ(oy, -16.f);

    TileLayer::Config stagY;
    stagY.orientation = MapOrientation::Staggered;
    stagY.staggerAxis = StaggerAxis::Y;
    stagY.tileW       = 64.f;
    stagY.tileH       = 32.f;
    dualGridHalfOffset(stagY, ox, oy);
    CHECK_EQ(ox, -32.f);
    CHECK_EQ(oy, -8.f);  // pitchY = th/2 = 16 → half = 8

    TileLayer::Config stagX;
    stagX.orientation = MapOrientation::Staggered;
    stagX.staggerAxis = StaggerAxis::X;
    stagX.tileW       = 64.f;
    stagX.tileH       = 32.f;
    dualGridHalfOffset(stagX, ox, oy);
    CHECK_EQ(ox, -16.f);  // pitchX = tw/2 = 32 → half = 16
    CHECK_EQ(oy, -16.f);

    TileLayer::Config hexY;
    hexY.orientation   = MapOrientation::Hexagonal;
    hexY.staggerAxis   = StaggerAxis::Y;
    hexY.tileW         = 64.f;
    hexY.tileH         = 32.f;
    hexY.hexSideLength = 16.f;
    dualGridHalfOffset(hexY, ox, oy);
    // pitchY = (32+16)/2 = 24 → half = 12
    CHECK_EQ(ox, -32.f);
    CHECK_EQ(oy, -12.f);

    TileLayer::Config hexX;
    hexX.orientation   = MapOrientation::Hexagonal;
    hexX.staggerAxis   = StaggerAxis::X;
    hexX.tileW         = 64.f;
    hexX.tileH         = 32.f;
    hexX.hexSideLength = 16.f;
    dualGridHalfOffset(hexX, ox, oy);
    // pitchX = (64+16)/2 = 40 → half = 20
    CHECK_EQ(ox, -20.f);
    CHECK_EQ(oy, -16.f);
}

TEST_CASE("map.dualGrid.resolve.isometric") {
    auto      *mod               = Map::create();
    TileLayer *logic             = mod->newLayer(2, 2, 64.f, 32.f);
    TileLayer *display           = mod->newLayer(1, 1, 8.f, 8.f);
    logic->config()->orientation = MapOrientation::Isometric;
    logic->setOrigin(100.f, 200.f);
    logic->setTile(0, 0, 1);
    logic->setTile(1, 1, 1);

    DualGridOptions opts;
    opts.useDefaultFrameTable = false;
    opts.firstDisplayGid      = 1;
    opts.hideLogic            = true;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));
    CHECK_EQ(static_cast<int>(display->config()->orientation), static_cast<int>(MapOrientation::Isometric));
    CHECK_EQ(display->getMapWidth(), 3);
    CHECK_EQ(display->getMapHeight(), 3);
    CHECK_EQ(display->getX(), 100.f);         // iso: offX = 0
    CHECK_EQ(display->getY(), 200.f - 16.f);  // offY = -tileH/2
    CHECK_EQ(mod->dualGridOffsetX(logic), 0.f);
    CHECK_EQ(mod->dualGridOffsetY(logic), -16.f);

    // Index-space mask unchanged from orthogonal dual-grid.
    CHECK_EQ(dualGridMaskAt(*logic, 1, 1, 0), 9);
    CHECK_EQ(display->getTile(1, 1), 1 + 9);

    // display(1,0) == logic fractional (0.5, -0.5) under iso projection:
    // wx = ox + (1-0)*tw/2 = ox+32, wy = (oy-th/2) + (1+0)*th/2 = oy
    CHECK_EQ(display->tileToWorldX(1, 0), 100.f + 32.f);
    CHECK_EQ(display->tileToWorldY(1, 0), 200.f);

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.dualGrid.resolve.staggeredYOdd") {
    auto      *mod     = Map::create();
    TileLayer *logic   = mod->newLayer(3, 2, 64.f, 32.f);
    TileLayer *display = mod->newLayer(1, 1, 8.f, 8.f);
    {
        auto lc          = logic->config();
        lc->orientation  = MapOrientation::Staggered;
        lc->staggerAxis  = StaggerAxis::Y;
        lc->staggerIndex = StaggerIndex::Odd;
    }
    logic->setOrigin(0.f, 0.f);
    logic->fill(1);

    DualGridOptions opts;
    opts.useDefaultFrameTable = false;
    opts.firstDisplayGid      = 20;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));
    CHECK_EQ(static_cast<int>(display->config()->orientation), static_cast<int>(MapOrientation::Staggered));
    CHECK_EQ(static_cast<int>(display->config()->staggerAxis), static_cast<int>(StaggerAxis::Y));
    CHECK_EQ(static_cast<int>(display->config()->staggerIndex), static_cast<int>(StaggerIndex::Odd));
    CHECK_EQ(display->getMapWidth(), 4);
    CHECK_EQ(display->getMapHeight(), 3);
    CHECK_EQ(display->getX(), -32.f);
    CHECK_EQ(display->getY(), -8.f);

    // Interior display(1,1) sees four filled logic cells → mask 15
    CHECK_EQ(dualGridMaskAt(*logic, 1, 1, 0), 15);
    CHECK_EQ(display->getTile(1, 1), 20 + 15);
    // Corner display(0,0) only BR → mask 8
    CHECK_EQ(display->getTile(0, 0), 20 + 8);

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.dualGrid.resolve.staggeredXEven") {
    auto      *mod     = Map::create();
    TileLayer *logic   = mod->newLayer(2, 3, 64.f, 32.f);
    TileLayer *display = mod->newLayer(1, 1, 8.f, 8.f);
    {
        auto lc          = logic->config();
        lc->orientation  = MapOrientation::Staggered;
        lc->staggerAxis  = StaggerAxis::X;
        lc->staggerIndex = StaggerIndex::Even;
    }
    logic->setOrigin(10.f, 20.f);
    logic->setTile(0, 1, 1);
    logic->setTile(1, 1, 1);

    DualGridOptions opts;
    opts.useDefaultFrameTable = false;
    opts.firstDisplayGid      = 1;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));
    CHECK_EQ(static_cast<int>(display->config()->staggerAxis), static_cast<int>(StaggerAxis::X));
    CHECK_EQ(static_cast<int>(display->config()->staggerIndex), static_cast<int>(StaggerIndex::Even));
    CHECK_EQ(display->getX(), 10.f - 16.f);
    CHECK_EQ(display->getY(), 20.f - 16.f);
    // display(1,2) samples logic (0,1),(1,1),(0,2),(1,2) → TL+TR = mask 3
    CHECK_EQ(dualGridMaskAt(*logic, 1, 2, 0), 3);
    CHECK_EQ(display->getTile(1, 2), 1 + 3);

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.dualGrid.resolve.hexagonal") {
    auto      *mod     = Map::create();
    TileLayer *logic   = mod->newLayer(2, 2, 64.f, 32.f);
    TileLayer *display = mod->newLayer(1, 1, 8.f, 8.f);
    {
        auto lc           = logic->config();
        lc->orientation   = MapOrientation::Hexagonal;
        lc->staggerAxis   = StaggerAxis::Y;
        lc->staggerIndex  = StaggerIndex::Odd;
        lc->hexSideLength = 16.f;
    }
    logic->setOrigin(5.f, 7.f);
    logic->setTile(0, 0, 1);

    DualGridOptions opts;
    opts.useDefaultFrameTable = true;
    opts.firstDisplayGid      = 1;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));
    CHECK_EQ(static_cast<int>(display->config()->orientation), static_cast<int>(MapOrientation::Hexagonal));
    CHECK_EQ(display->config()->hexSideLength, 16.f);
    CHECK_EQ(display->getX(), 5.f - 32.f);
    CHECK_EQ(display->getY(), 7.f - 12.f);  // pitchY=24 → half=12
    CHECK_EQ(display->getTile(0, 0), 1 + dualGridDefaultFrame(8));
    CHECK_EQ(display->getTile(1, 1), 1 + dualGridDefaultFrame(1));

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.dualGrid.flipFlagsCountAsFilled") {
    auto      *mod     = Map::create();
    TileLayer *logic   = mod->newLayer(1, 1, 16.f, 16.f);
    TileLayer *display = mod->newLayer(1, 1, 16.f, 16.f);
    // Tiled horizontal flip flag on GID 1
    logic->setTile(0, 0, int(0x80000001u));
    DualGridOptions opts;
    opts.useDefaultFrameTable = false;
    opts.firstDisplayGid      = 1;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));
    CHECK_EQ(dualGridMaskAt(*logic, 0, 0, 0), 8);
    CHECK_EQ(display->getTile(0, 0), 1 + 8);

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.dualGrid.noHalfOffsetKeepsOrigin") {
    auto      *mod               = Map::create();
    TileLayer *logic             = mod->newLayer(2, 1, 32.f, 32.f);
    TileLayer *display           = mod->newLayer(1, 1, 8.f, 8.f);
    logic->config()->orientation = MapOrientation::Isometric;
    logic->setOrigin(40.f, 50.f);
    logic->setTile(0, 0, 1);
    DualGridOptions opts;
    opts.applyHalfOffset      = false;
    opts.useDefaultFrameTable = false;
    opts.firstDisplayGid      = 1;
    opts.hideLogic            = false;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));
    CHECK_EQ(display->getX(), 40.f);
    CHECK_EQ(display->getY(), 50.f);
    CHECK_EQ(static_cast<int>(display->config()->orientation), static_cast<int>(MapOrientation::Isometric));
    CHECK(logic->isVisible());

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}

TEST_CASE("map.dualGrid.render.isometricDisplayDepth") {
    hideAllTileLayers();
    auto      *mod               = Map::create();
    TileLayer *logic             = mod->newLayer(2, 2, 64.f, 32.f);
    TileLayer *display           = mod->newLayer(1, 1, 64.f, 32.f);
    logic->config()->orientation = MapOrientation::Isometric;
    logic->setOrigin(0.f, 0.f);
    logic->fill(1);
    DualGridOptions opts;
    opts.useDefaultFrameTable = false;
    opts.firstDisplayGid      = 1;
    CHECK(resolveDualGrid(logic, display, opts, nullptr));

    std::vector<eve::graphics::DrawItem2D> items;
    TileRenderSystem::collect(items);
    // Interior + border dual cells that are non-empty on a full 2x2 logic map:
    // display 3x3, only mask0 empty — all 9 have some contribution from filled
    // neighbors except none are fully empty when logic is full... actually edges
    // still have non-zero masks. All 9 display cells should be non-empty.
    CHECK_EQ(items.size(), 9u);
    eve::graphics::sortDrawItems2D(items);
    for (size_t i = 1; i < items.size(); ++i) {
        CHECK(items[i - 1].depthY <= items[i].depthY);
    }

    logic->clear();
    display->clear();
    logic->setVisible(false);
    display->setVisible(false);
}
}  // namespace test_map_unit
#endif
