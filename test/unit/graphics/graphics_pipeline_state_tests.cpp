#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_BloomSettings)
#include "PrivateTestScope.h"
#include <limits>
#include "graphics/Bloom.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// BloomSettings.cpp
namespace test_BloomSettings {
TEST_CASE("graphics.bloom rejects invalid reconstruction settings") {
    using namespace eve::graphics;
    BloomFilterSettings settings;
    REQUIRE(validateBloomFilterSettings(settings).ok());
    settings.filter = BloomFilter::GaussianScatter;
    REQUIRE(validateBloomFilterSettings(settings).ok());
    settings.filter = BloomFilter::GaussianPyramid;
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
}  // namespace test_BloomSettings
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ClusteredLighting)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include "Fixtures.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/ClusteredLight.h"
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
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ClusteredLighting.cpp
namespace test_ClusteredLighting_unit {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

[[maybe_unused]] static float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }


[[maybe_unused]] static void resetScene3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr]   = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data]  = *it;
            data->active = false;
        }
    }
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d]   = *it;
            d->enabled = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp]   = *it;
            sp->visible = false;
        }
    }
}

[[maybe_unused]] static Texture *makeSolidGray(Graphics *gfx, uint8_t g) {
    uint8_t px[4] = {g, g, g, 255};
    return gfx->newTexture(1, 1, px);
}

TEST_CASE("ClusteredLighting.buildProducesNonEmptyTables") {
    std::vector<ClusteredLightGpu> points;
    for (int i = 0; i < 32; ++i) {
        ClusteredLightGpu L{};
        const float       a = float(i) * 0.2f;
        L.posRadius         = glm::vec4(std::cos(a) * 1.5f, 0.f, std::sin(a) * 1.5f, 2.f);
        L.color             = glm::vec4(1.f, 1.f, 1.f, 1.f);
        points.push_back(L);
    }
    std::vector<ClusteredLightGpu> dirs;
    ClusteredLightGpu              d{};
    d.posRadius = glm::vec4(0.f, 1.f, 0.f, 0.f);
    d.color     = glm::vec4(0.2f, 0.2f, 0.2f, 1.f);
    dirs.push_back(d);

    glm::mat4 view = glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto      upload =
        buildClusteredLighting(points, dirs, view, 0.1f, 100.f, 320, 240, 1.0f, glm::vec4(0.1f, 0.1f, 0.1f, 0.f));
    REQUIRE(upload.active);
    CHECK_EQ(upload.lights.size(), 32u);
    CHECK_EQ(upload.clusterTable.size(), size_t(ClusteredLightConfig::kClusterCount));
    CHECK(!upload.lightIndices.empty());
    uint32_t total = 0;
    for (const auto &e : upload.clusterTable) total += e.count;
    CHECK_GT(total, 0u);
}

TEST_CASE("ClusteredLighting.reportsFrameAndClusterBudgetOverflow") {
    std::vector<ClusteredLightGpu> points;
    points.reserve(300);
    for (int i = 0; i < 300; ++i) {
        ClusteredLightGpu light{};
        // Deliberately overlap every candidate so at least one cluster exceeds 32 lights.
        light.posRadius = glm::vec4(0.f, 0.f, 0.f, 4.f);
        light.color     = glm::vec4(1.f + float(i) * 0.001f, 1.f, 1.f, 1.f);
        points.push_back(light);
    }
    const glm::mat4 view   = glm::lookAtRH(glm::vec3(0, 0, 3), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const auto      upload = buildClusteredLighting(points, {}, view, 0.1f, 100.f, 1920, 1080, 1.f, glm::vec4(0.f));
    REQUIRE(upload.active);
    CHECK_EQ(upload.lights.size(), size_t(ClusteredLightConfig::kMaxLights));
    CHECK_EQ(upload.truncatedLightCount, 44u);
    CHECK_GT(upload.overflowClusterCount, 0u);
    CHECK_GT(upload.droppedLightReferenceCount, 0u);
    for (const auto &cluster : upload.clusterTable)
        CHECK_LE(cluster.count, uint32_t(ClusteredLightConfig::kMaxLightsPerCluster));
}

TEST_CASE("ClusteredLighting.acceptsDistributed256LightBudget") {
    std::vector<ClusteredLightGpu> points;
    points.reserve(ClusteredLightConfig::kMaxLights);
    for (int i = 0; i < ClusteredLightConfig::kMaxLights; ++i) {
        ClusteredLightGpu light{};
        const int         column = i % 16;
        const int         row    = (i / 16) % 4;
        const int         depth  = i / 64;
        light.posRadius =
            glm::vec4((float(column) - 7.5f) * 1.5f, (float(row) - 1.5f) * 1.5f, -4.f - float(depth) * 12.f, 0.45f);
        light.color = glm::vec4(1.f);
        points.push_back(light);
    }
    const auto upload =
        buildClusteredLighting(points, {}, glm::mat4(1.f), 0.1f, 100.f, 1920, 1080, 1.f, glm::vec4(0.f));
    REQUIRE(upload.active);
    CHECK_EQ(upload.lights.size(), size_t(ClusteredLightConfig::kMaxLights));
    CHECK_EQ(upload.truncatedLightCount, 0u);
    CHECK_EQ(upload.overflowClusterCount, 0u);
    CHECK_EQ(upload.droppedLightReferenceCount, 0u);
}
}  // namespace test_ClusteredLighting_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_CubemapPrefilter)
#include "PrivateTestScope.h"
#include <cstddef>
#include <cstdint>
#include <vector>
#include "graphics/CubemapPrefilter.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// CubemapPrefilter.cpp
namespace test_CubemapPrefilter_unit {
using eve::graphics::buildGgxCubemapMipChain;

TEST_CASE("graphics.cubemapPrefilter.rejectsInvalidInput") {
    CHECK(buildGgxCubemapMipChain(nullptr, 4u, 3u).empty());
    const uint8_t pixel[4] = {0u, 0u, 0u, 255u};
    CHECK(buildGgxCubemapMipChain(pixel, 0u, 1u).empty());
    CHECK(buildGgxCubemapMipChain(pixel, 1u, 0u).empty());
}

TEST_CASE("graphics.cubemapPrefilter.uniformRadianceAndLayout") {
    constexpr uint32_t   size    = 4;
    constexpr uint32_t   levels  = 3;
    constexpr uint8_t    rgba[4] = {37, 91, 173, 229};
    std::vector<uint8_t> faces(size_t(size) * size * 4u * 6u);
    for (size_t i = 0; i < faces.size(); i += 4u) {
        faces[i]      = rgba[0];
        faces[i + 1u] = rgba[1];
        faces[i + 2u] = rgba[2];
        faces[i + 3u] = rgba[3];
    }

    const std::vector<uint8_t> chain         = buildGgxCubemapMipChain(faces.data(), size, levels, 16u);
    const size_t               expectedBytes = 6u * 4u * (size_t(4u * 4u) + size_t(2u * 2u) + 1u);
    REQUIRE(chain.size() == expectedBytes);
    REQUIRE(chain.size() >= faces.size());
    for (size_t i = 0; i < faces.size(); ++i) CHECK(chain[i] == faces[i]);
    for (size_t i = faces.size(); i < chain.size(); i += 4u) {
        CHECK(chain[i] == rgba[0]);
        CHECK(chain[i + 1u] == rgba[1]);
        CHECK(chain[i + 2u] == rgba[2]);
        CHECK(chain[i + 3u] == rgba[3]);
    }
}

TEST_CASE("graphics.cubemapPrefilter.roughMipsCrossFaceSample") {
    constexpr uint32_t   size = 4;
    std::vector<uint8_t> faces(size_t(size) * size * 4u * 6u, 255u);
    for (uint32_t face = 0; face < 6u; ++face) {
        const uint8_t value = uint8_t(face * 40u);
        const size_t  begin = size_t(face) * size * size * 4u;
        for (size_t i = begin; i < begin + size_t(size) * size * 4u; i += 4u) {
            faces[i]      = value;
            faces[i + 1u] = value;
            faces[i + 2u] = value;
        }
    }

    const std::vector<uint8_t> chain   = buildGgxCubemapMipChain(faces.data(), size, 3u, 32u);
    const size_t               lastMip = size_t(size) * size * 4u * 6u + size_t(2u) * 2u * 4u * 6u;
    bool                       mixed   = false;
    for (uint32_t face = 0; face < 6u; ++face) {
        const uint8_t original = uint8_t(face * 40u);
        if (chain[lastMip + size_t(face) * 4u] != original) mixed = true;
    }
    CHECK(mixed);
}

TEST_CASE("graphics.cubemapPrefilter.diffuseIrradianceKeepsDirection") {
    constexpr uint32_t   size = 4;
    std::vector<uint8_t> faces(size_t(size) * size * 4u * 6u, 255u);
    for (size_t i = 0; i < faces.size(); i += 4u) {
        faces[i]      = 0u;
        faces[i + 1u] = 0u;
        faces[i + 2u] = 0u;
    }
    const size_t positiveY = size_t(2u) * size * size * 4u;
    for (size_t i = positiveY; i < positiveY + size_t(size) * size * 4u; i += 4u) {
        faces[i]      = 255u;
        faces[i + 1u] = 255u;
        faces[i + 2u] = 255u;
    }

    const std::vector<uint8_t> chain         = buildGgxCubemapMipChain(faces.data(), size, 3u, 32u);
    const size_t               lastMip       = size_t(size) * size * 4u * 6u + size_t(2u) * 2u * 4u * 6u;
    const uint8_t              towardLight   = chain[lastMip + size_t(2u) * 4u];
    const uint8_t              awayFromLight = chain[lastMip + size_t(3u) * 4u];
    CHECK(towardLight > awayFromLight);
}

TEST_CASE("graphics.cubemapPrefilter.bilinearTapCrossesFaceEdge") {
    constexpr uint32_t   size = 4;
    std::vector<uint8_t> faces(size_t(size) * size * 4u * 6u, 255u);
    for (size_t i = 0; i < faces.size(); i += 4u) {
        faces[i]      = 0u;
        faces[i + 1u] = 0u;
        faces[i + 2u] = 0u;
    }
    const size_t positiveZ = size_t(4u) * size * size * 4u;
    for (size_t i = positiveZ; i < positiveZ + size_t(size) * size * 4u; i += 4u) faces[i] = 200u;

    const glm::vec4 edge = eve::graphics::cubemap_prefilter_detail::sampleBase(
        faces.data(), size, glm::normalize(glm::vec3(1.f, 0.f, 1.f)));
    CHECK(edge.r > 0.f);
    CHECK(edge.r < 200.f);
}
}  // namespace test_CubemapPrefilter_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_Lighting2D)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <cstring>
#include <initializer_list>
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
#include "graphics/RenderSystem.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "image/ImageData.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Lighting2D.cpp
namespace test_Lighting2D_unit {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

[[maybe_unused]] static float luma(const Color &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

[[maybe_unused]] static Texture *makeSolidTexture(Graphics *gfx, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4, 255);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = r;
        px[i + 1] = g;
        px[i + 2] = b;
    }
    eve::image::ImageData imageData(w, h, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    return gfx->newTexture(&imageData);
}

/**
 * Normal map biased toward +X on the right half and -X on the left half so a light
 * from the right brightens the right side more.
 */
[[maybe_unused]] static Texture *makeBiasedNormal(Graphics *gfx, int w, int h) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4;
            // Encode [-1,1] → [0,255]; left leans -X, right leans +X.
            float nx  = (x < w / 2) ? -0.7f : 0.7f;
            float ny  = 0.f;
            float nz  = 0.7f;
            px[i + 0] = uint8_t((nx * 0.5f + 0.5f) * 255.f);
            px[i + 1] = uint8_t((ny * 0.5f + 0.5f) * 255.f);
            px[i + 2] = uint8_t((nz * 0.5f + 0.5f) * 255.f);
            px[i + 3] = 255;
        }
    }
    eve::image::ImageData imageData(w, h, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    return gfx->newTexture(&imageData);
}

/** Live-render the current lit scene to the window for ~1s.
 *  Retargets cam / sprites / lights from the offscreen canvas onto the swapchain
 *  so we don't depend on sampling a stale canvas texture after getPixel/present. */
[[maybe_unused]] static void previewOnWindow(Graphics *gfx, Camera2D *cam,
                                             std::initializer_list<Renderable2D *> sprites,
                                             std::initializer_list<Light2D *> lights, int ms = 1000) {
    cam->data()->canvas = nullptr;
    // 128×64 content → roughly fill a 320×240 window.
    cam->data()->zoom = 2.5f;
    for (auto *sp : sprites) sp->sprite()->canvas = nullptr;
    for (auto *L : lights) L->setCanvas(nullptr);

    gfx->setBackgroundColorRGBA(0.06f, 0.06f, 0.08f, 1.f);
    const int frames = (ms >= 16) ? (ms / 16) : 1;
    for (int i = 0; i < frames; ++i) {
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return;
        }
        SDL_Delay(16);
    }
}


TEST_CASE("Lighting2D.createLightTypesAndAmbient") {
    auto *pt = Light2D::createLight("point");
    CHECK_EQ(pt->getType(), std::string("point"));
    auto *dir = Light2D::createLight("dir");
    CHECK_EQ(dir->getType(), std::string("dir"));
    dir->setDirection(1.f, 0.f);
    CHECK(std::abs(dir->getDirX() - 1.f) < 1e-5f);
    auto *spot = Light2D::createLight("spot");
    CHECK_EQ(spot->getType(), std::string("spot"));
    spot->setSpotAngle(25.f);
    spot->setSpotSoftness(0.5f);
    CHECK(std::abs(spot->getSpotAngle() - 25.f) < 1e-5f);
    CHECK(std::abs(spot->getSpotSoftness() - 0.5f) < 1e-5f);
    spot->setSpotAngle(120.f);  // clamp to 89
    CHECK(spot->getSpotAngle() <= 89.f + 1e-5f);

    auto *cam = Camera2D::createCamera();
    cam->setAmbient(0.2f, 0.3f, 0.4f);
    CHECK(std::abs(cam->data()->ambientR - 0.2f) < 1e-5f);
    CHECK(std::abs(cam->data()->ambientG - 0.3f) < 1e-5f);
    CHECK(std::abs(cam->data()->ambientB - 0.4f) < 1e-5f);

    pt->setEnabled(false);
    dir->setEnabled(false);
    spot->setEnabled(false);
    cam->data()->active = false;
}


/**
 * Pack a biased normal into a corner of a large atlas so UV derivatives are
 * tiny. The lit2d det singularity must be scale-aware or atlas tiles fall back
 * to the flat-Z path and lose the left/right bias.
 */
[[maybe_unused]] static Texture *makeAtlasBiasedNormal(Graphics *gfx, int atlasW, int atlasH, int tileX, int tileY,
                                                       int tileW, int tileH) {
    std::vector<uint8_t> px(size_t(atlasW) * size_t(atlasH) * 4);
    for (int y = 0; y < atlasH; ++y) {
        for (int x = 0; x < atlasW; ++x) {
            size_t i  = (size_t(y) * size_t(atlasW) + size_t(x)) * 4;
            px[i + 0] = 128;
            px[i + 1] = 128;
            px[i + 2] = 255;
            px[i + 3] = 255;
        }
    }
    for (int y = 0; y < tileH; ++y) {
        for (int x = 0; x < tileW; ++x) {
            size_t i  = (size_t(tileY + y) * size_t(atlasW) + size_t(tileX + x)) * 4;
            float  nx = (x < tileW / 2) ? -0.7f : 0.7f;
            float  ny = 0.f;
            float  nz = 0.7f;
            px[i + 0] = uint8_t((nx * 0.5f + 0.5f) * 255.f);
            px[i + 1] = uint8_t((ny * 0.5f + 0.5f) * 255.f);
            px[i + 2] = uint8_t((nz * 0.5f + 0.5f) * 255.f);
            px[i + 3] = 255;
        }
    }
    eve::image::ImageData imageData(atlasW, atlasH, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    return gfx->newTexture(&imageData);
}

[[maybe_unused]] static Texture *makeAtlasSolid(Graphics *gfx, int atlasW, int atlasH, int tileX, int tileY, int tileW,
                                                int tileH, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(size_t(atlasW) * size_t(atlasH) * 4, 0);
    for (int y = 0; y < tileH; ++y) {
        for (int x = 0; x < tileW; ++x) {
            size_t i  = (size_t(tileY + y) * size_t(atlasW) + size_t(tileX + x)) * 4;
            px[i + 0] = r;
            px[i + 1] = g;
            px[i + 2] = b;
            px[i + 3] = 255;
        }
    }
    eve::image::ImageData imageData(atlasW, atlasH, "RGBA8");
    std::memcpy(imageData.getData(), px.data(), px.size());
    return gfx->newTexture(&imageData);
}
}  // namespace test_Lighting2D_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_MeshInstanceRange)
#include "PrivateTestScope.h"
#include <limits>
#include "graphics/MeshInstanceRange.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// MeshInstanceRange.cpp
namespace test_MeshInstanceRange {
TEST_CASE("graphics.instance range culling respects Vulkan near plane distance and transformed bounds") {
    using namespace eve::graphics;
    MeshInstanceRange range{0, 3, {-.2f, -.2f, .2f}, {.2f, .2f, .8f}, 0};
    REQUIRE(validateMeshInstanceRange(range).ok());
    const glm::mat4 identity(1);
    CHECK(meshInstanceRangeVisible(range, identity, identity, glm::vec3(0)));
    auto moved  = identity;
    moved[3][0] = 1.1f;
    CHECK(meshInstanceRangeVisible(range, moved, identity, glm::vec3(0)));
    moved[3][0] = 1.3f;
    CHECK(!meshInstanceRangeVisible(range, moved, identity, glm::vec3(0)));
    moved       = identity;
    moved[3][2] = -1;
    CHECK(!meshInstanceRangeVisible(range, moved, identity, glm::vec3(0)));
    range.maximumHorizontalDistance = 1;
    CHECK(!meshInstanceRangeVisible(range, identity, identity, glm::vec3(10, 0, 0)));
    CHECK(meshInstanceRangeVisible(range, identity, identity, glm::vec3(0, 100, 0)));
    range.count = 0;
    CHECK(!meshInstanceRangeVisible(range, identity, identity, glm::vec3(0)));
}
TEST_CASE("graphics.instance range rejects overflow inverted bounds and invalid distance") {
    using namespace eve::graphics;
    MeshInstanceRange range;
    range.first = UINT32_MAX;
    range.count = 1;
    CHECK(!validateMeshInstanceRange(range).ok());
    range.first      = 0;
    range.minimum[0] = 1;
    CHECK(!validateMeshInstanceRange(range).ok());
    range.minimum[0]                = 0;
    range.maximumHorizontalDistance = -1;
    CHECK(!validateMeshInstanceRange(range).ok());
    range.maximumHorizontalDistance = 0;
    range.maximum[1]                = std::numeric_limits<float>::infinity();
    CHECK(!validateMeshInstanceRange(range).ok());
}
}  // namespace test_MeshInstanceRange
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ParallaxMap)
#include "PrivateTestScope.h"
#include <cmath>
#include "graphics/ParallaxMap.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ParallaxMap.cpp
namespace test_ParallaxMap_unit {
using eve::graphics::clampParallaxParams;
using eve::graphics::parallaxOffsetUV;
using eve::graphics::ParallaxParams;
using eve::graphics::silPomCoverage;
using eve::graphics::silPomCoverageSoft;
using eve::graphics::silPomHorizonTrim;
using eve::graphics::ssdmCoverage;
using eve::graphics::ssdmScreenOffset;

TEST_CASE("ParallaxMap.scaleZeroIsIdentity") {
    float ou = -1.f, ov = -1.f;
    parallaxOffsetUV(0.4f, 0.6f, 0.2f, 0.5f, -0.3f, 0.8f, 0.f, ou, ov);
    CHECK(std::fabs(ou - 0.4f) < 1e-6f);
    CHECK(std::fabs(ov - 0.6f) < 1e-6f);
}

TEST_CASE("ParallaxMap.heightDisplacesTowardViewXY") {
    // Low height → deep → larger displacement along -viewXY.
    float ouLo = 0.f, ovLo = 0.f;
    float ouHi = 0.f, ovHi = 0.f;
    parallaxOffsetUV(0.5f, 0.5f, 0.0f, 0.4f, 0.0f, 0.9f, 0.1f, ouLo, ovLo);
    parallaxOffsetUV(0.5f, 0.5f, 1.0f, 0.4f, 0.0f, 0.9f, 0.1f, ouHi, ovHi);
    CHECK(ouLo < ouHi);  // deeper surface shifts UV more opposite +viewX
    CHECK(std::fabs(ovLo - 0.5f) < 1e-5f);
    CHECK(std::fabs(ovHi - 0.5f) < 1e-5f);
}

TEST_CASE("ParallaxMap.clampParams") {
    ParallaxParams p;
    p.scale     = 1.f;
    p.minLayers = 0.f;
    p.maxLayers = 100.f;
    clampParallaxParams(p);
    CHECK(p.scale == 0.25f);
    CHECK(p.minLayers == 1.f);
    CHECK(p.maxLayers == 64.f);

    p.scale     = -1.f;
    p.minLayers = 16.f;
    p.maxLayers = 8.f;
    clampParallaxParams(p);
    CHECK(p.scale == 0.f);
    CHECK(p.maxLayers == 16.f);
}

TEST_CASE("ParallaxMap.silPomCoverageClipsOutsideChart") {
    CHECK(silPomCoverage(0.5f, 0.5f, 0.f) == 1.f);
    CHECK(silPomCoverage(0.0f, 0.0f, 0.f) == 1.f);
    CHECK(silPomCoverage(1.0f, 1.0f, 0.f) == 1.f);
    CHECK(silPomCoverage(-0.01f, 0.5f, 0.f) == 0.f);
    CHECK(silPomCoverage(0.5f, 1.01f, 0.f) == 0.f);
    // Padding expands the keep region.
    CHECK(silPomCoverage(-0.01f, 0.5f, 0.02f) == 1.f);
}

TEST_CASE("ParallaxMap.silPomSoftCoverageAndHorizon") {
    CHECK(silPomCoverageSoft(0.5f, 0.5f, 0.05f) == 1.f);
    CHECK(silPomCoverageSoft(-0.01f, 0.5f, 0.05f) == 0.f);
    const float edge = silPomCoverageSoft(0.02f, 0.5f, 0.05f);
    CHECK(edge > 0.f);
    CHECK(edge < 1.f);
    // Face-on: keep low height. Grazing: clip low height, keep high height.
    CHECK(silPomHorizonTrim(0.1f, 1.0f, 0.45f) == 1.f);
    CHECK(silPomHorizonTrim(0.1f, 0.05f, 0.45f) == 0.f);
    CHECK(silPomHorizonTrim(0.95f, 0.05f, 0.45f) == 1.f);
}

TEST_CASE("ParallaxMap.ssdmOffsetScalesWithHeight") {
    float ox0 = 0.f, oy0 = 0.f;
    float ox1 = 0.f, oy1 = 0.f;
    ssdmScreenOffset(0.0f, 1.0f, 0.0f, 0.05f, ox0, oy0);
    ssdmScreenOffset(1.0f, 1.0f, 0.0f, 0.05f, ox1, oy1);
    CHECK(std::fabs(ox0) < 1e-6f);
    CHECK(std::fabs(oy0) < 1e-6f);
    CHECK(std::fabs(ox1 - 0.05f) < 1e-5f);
    CHECK(std::fabs(oy1) < 1e-6f);
    CHECK(ssdmCoverage(0.9f, 0.5f, 0.15f, 0.f, 0.f) == 0.f);
    CHECK(ssdmCoverage(0.9f, 0.5f, 0.05f, 0.f, 0.f) == 1.f);
}
}  // namespace test_ParallaxMap_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_Quad)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <cstring>
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
#include "graphics/RenderSystem.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "image/ImageData.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Quad.cpp
namespace test_Quad_unit {
using namespace eve::graphics;

TEST_CASE("Quad.getUVMapsPixelRect") {
    Quad  q(16, 8, 32, 16);
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    q.getUV(64, 64, u0, v0, u1, v1);
    CHECK(std::abs(u0 - 0.25f) < 1e-5f);
    CHECK(std::abs(v0 - 0.125f) < 1e-5f);
    CHECK(std::abs(u1 - 0.75f) < 1e-5f);
    CHECK(std::abs(v1 - 0.375f) < 1e-5f);

    q.setViewport(0, 0, 64, 64);
    q.getUV(64, 64, u0, v0, u1, v1);
    CHECK(std::abs(u0) < 1e-5f);
    CHECK(std::abs(v0) < 1e-5f);
    CHECK(std::abs(u1 - 1.f) < 1e-5f);
    CHECK(std::abs(v1 - 1.f) < 1e-5f);
}
}  // namespace test_Quad_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_RenderSceneEffects)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <assimp/matrix4x4.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>
#include "Fixtures.h"
#include "SceneColor.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
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
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Shadow.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "medialoader/model/ModelLoader.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// RenderSceneEffects.cpp
namespace test_RenderSceneEffects_unit {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {

[[maybe_unused]] float luma(const Color &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }


[[maybe_unused]] void resetScene3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr]   = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data]        = *it;
            data->active       = false;
            data->envMap       = nullptr;
            data->envIntensity = 0.f;
        }
    }
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d]   = *it;
            d->enabled = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp]   = *it;
            sp->visible = false;
        }
    }
}

[[maybe_unused]] Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

[[maybe_unused]] void addHud() {
    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;
}

[[maybe_unused]] void warmPresent(Graphics *gfx, int frames = 2) {
    for (int i = 0; i < frames; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Delay(8);
    }
}

[[maybe_unused]] float meanLumaRegion(Graphics *gfx, int x0, int y0, int x1, int y1, int step = 4,
                                      bool sceneLinear = false) {
    double sum = 0.0;
    int    n   = 0;
    for (int y = y0; y < y1; y += step) {
        for (int x = x0; x < x1; x += step) {
            const auto pixel = gfx->getPixel(x, y);
            sum += luma(sceneLinear ? testSceneLinearColor(pixel) : pixel);
            ++n;
        }
    }
    return n ? float(sum / n) : 0.f;
}

[[maybe_unused]] float meanGreenRegion(Graphics *gfx, int x0, int y0, int x1, int y1, int step = 4) {
    double sum = 0.0;
    int    n   = 0;
    for (int y = y0; y < y1; y += step) {
        for (int x = x0; x < x1; x += step) {
            sum += gfx->getPixel(x, y).g;
            ++n;
        }
    }
    return n ? float(sum / n) : 0.f;
}

[[maybe_unused]] float meanRedRegion(Graphics *gfx, int x0, int y0, int x1, int y1, int step = 4) {
    double sum = 0.0;
    int    n   = 0;
    for (int y = y0; y < y1; y += step) {
        for (int x = x0; x < x1; x += step) {
            sum += gfx->getPixel(x, y).r;
            ++n;
        }
    }
    return n ? float(sum / n) : 0.f;
}

/** Minimal unit cube OBJ (outward CCW for RH Y-up). */
static const char kCubeObj[] =
    "v -0.5 -0.5  0.5\nv  0.5 -0.5  0.5\nv  0.5  0.5  0.5\nv -0.5  0.5  0.5\n"
    "vn 0 0 1\nf 1//1 2//1 3//1\nf 1//1 3//1 4//1\n"
    "v  0.5 -0.5 -0.5\nv -0.5 -0.5 -0.5\nv -0.5  0.5 -0.5\nv  0.5  0.5 -0.5\n"
    "vn 0 0 -1\nf 5//2 6//2 7//2\nf 5//2 7//2 8//2\n"
    "v  0.5 -0.5  0.5\nv  0.5 -0.5 -0.5\nv  0.5  0.5 -0.5\nv  0.5  0.5  0.5\n"
    "vn 1 0 0\nf 9//3 10//3 11//3\nf 9//3 11//3 12//3\n"
    "v -0.5 -0.5 -0.5\nv -0.5 -0.5  0.5\nv -0.5  0.5  0.5\nv -0.5  0.5 -0.5\n"
    "vn -1 0 0\nf 13//4 14//4 15//4\nf 13//4 15//4 16//4\n"
    "v -0.5  0.5  0.5\nv  0.5  0.5  0.5\nv  0.5  0.5 -0.5\nv -0.5  0.5 -0.5\n"
    "vn 0 1 0\nf 17//5 18//5 19//5\nf 17//5 19//5 20//5\n"
    "v -0.5 -0.5 -0.5\nv  0.5 -0.5 -0.5\nv  0.5 -0.5  0.5\nv -0.5 -0.5  0.5\n"
    "vn 0 -1 0\nf 21//6 22//6 23//6\nf 21//6 23//6 24//6\n";

[[maybe_unused]] Mesh *loadCube(Graphics *gfx) {
    medialoader::ModelLoader loader;
    auto                     scene = loader.loadFromMemory(kCubeObj, sizeof(kCubeObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    REQUIRE(scene->mNumMeshes >= 1);
    Mesh *m = gfx->newMeshFromAssimp(*scene->mMeshes[0]);
    REQUIRE(m != nullptr);
    return m;
}

}  // namespace


TEST_CASE("RenderScenes.clipSpace.perspectiveVulkanFlipsY") {
    const glm::mat4 gl = glm::perspectiveRH_ZO(1.0f, 1.0f, 0.1f, 100.f);
    const glm::mat4 vk = perspectiveVulkanRH_ZO(1.0f, 1.0f, 0.1f, 100.f);
    CHECK(std::fabs(gl[1][1] + vk[1][1]) < 1e-5f);
    CHECK(vk[1][1] < 0.f);
    CHECK(gl[1][1] > 0.f);
}
}  // namespace test_RenderSceneEffects_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_RenderSystem3D)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include "Fixtures.h"
#include "filesystem/FileData.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
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
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "medialoader/image/FormatHandler.h"
#include "medialoader/model/ModelLoader.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// RenderSystem3D.cpp
namespace test_RenderSystem3D_unit {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

// Cube with per-face UVs (0..1). Front face (+Z) is first so Assimp preserves it.
static const char kCubeObj[] =
    // +Z front
    "v -0.5 -0.5  0.5\n"
    "v  0.5 -0.5  0.5\n"
    "v  0.5  0.5  0.5\n"
    "v -0.5  0.5  0.5\n"
    "vt 0 0\n"
    "vt 1 0\n"
    "vt 1 1\n"
    "vt 0 1\n"
    "vn 0 0 1\n"
    "f 1/1/1 2/2/1 3/3/1\n"
    "f 1/1/1 3/3/1 4/4/1\n"
    // -Z back
    "v  0.5 -0.5 -0.5\n"
    "v -0.5 -0.5 -0.5\n"
    "v -0.5  0.5 -0.5\n"
    "v  0.5  0.5 -0.5\n"
    "vn 0 0 -1\n"
    "f 5/1/2 6/2/2 7/3/2\n"
    "f 5/1/2 7/3/2 8/4/2\n"
    // +X right
    "v  0.5 -0.5  0.5\n"
    "v  0.5 -0.5 -0.5\n"
    "v  0.5  0.5 -0.5\n"
    "v  0.5  0.5  0.5\n"
    "vn 1 0 0\n"
    "f 9/1/3 10/2/3 11/3/3\n"
    "f 9/1/3 11/3/3 12/4/3\n"
    // -X left
    "v -0.5 -0.5 -0.5\n"
    "v -0.5 -0.5  0.5\n"
    "v -0.5  0.5  0.5\n"
    "v -0.5  0.5 -0.5\n"
    "vn -1 0 0\n"
    "f 13/1/4 14/2/4 15/3/4\n"
    "f 13/1/4 15/3/4 16/4/4\n"
    // +Y top
    "v -0.5  0.5  0.5\n"
    "v  0.5  0.5  0.5\n"
    "v  0.5  0.5 -0.5\n"
    "v -0.5  0.5 -0.5\n"
    "vn 0 1 0\n"
    "f 17/1/5 18/2/5 19/3/5\n"
    "f 17/1/5 19/3/5 20/4/5\n"
    // -Y bottom
    "v -0.5 -0.5 -0.5\n"
    "v  0.5 -0.5 -0.5\n"
    "v  0.5 -0.5  0.5\n"
    "v -0.5 -0.5  0.5\n"
    "vn 0 -1 0\n"
    "f 21/1/6 22/2/6 23/3/6\n"
    "f 21/1/6 23/3/6 24/4/6\n";

[[maybe_unused]] static float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }


[[maybe_unused]] static Mesh *loadUvCube(Graphics *gfx) {
    medialoader::ModelLoader loader;
    auto                     scene = loader.loadFromMemory(kCubeObj, sizeof(kCubeObj) - 1, ".obj");
    REQUIRE(!scene.empty());
    REQUIRE(scene->mNumMeshes >= 1);
    REQUIRE(scene->mMeshes[0]->HasTextureCoords(0));
    Mesh *m = gfx->newMeshFromAssimp(*scene->mMeshes[0]);
    REQUIRE(m != nullptr);
    REQUIRE_GT(m->indexCount, 0);
    return m;
}

[[maybe_unused]] static Texture *makeChecker(Graphics *gfx, int size, int cell) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint8_t c = ((x / cell) ^ (y / cell)) & 1 ? 255 : 0;
            size_t  i = size_t(y * size + x) * 4;
            px[i]     = c;
            px[i + 1] = c;
            px[i + 2] = c;
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data(), TextureCreateInfo::withMipmaps(true));
}

[[maybe_unused]] static Texture *makeSolidGray(Graphics *gfx, uint8_t g) {
    uint8_t px[4] = {g, g, g, 255};
    return gfx->newTexture(1, 1, px);
}

/** Non-periodic motif so UV remapping (not just phase shift) is measurable. */
[[maybe_unused]] static Texture *makeMotif(Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    const float          cx = float(size) * 0.35f;
    const float          cy = float(size) * 0.4f;
    const float          r  = float(size) * 0.22f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float   dx = float(x) - cx;
            float   dy = float(y) - cy;
            float   d  = std::sqrt(dx * dx + dy * dy);
            uint8_t v  = d < r ? 255 : uint8_t((x * 13 + y * 7) % 180);
            if (x > size * 3 / 4 && y < size / 4) v = 40;
            size_t i  = size_t(y * size + x) * 4;
            px[i]     = v;
            px[i + 1] = uint8_t((v * 3) / 4);
            px[i + 2] = uint8_t(255 - v / 2);
            px[i + 3] = 255;
        }
    }
    TextureCreateInfo info = TextureCreateInfo::withMipmaps(true);
    info.sampler.repeatU   = true;
    info.sampler.repeatV   = true;
    return gfx->newTexture(size, size, px.data(), info);
}

// Graphics/ECS are process-wide singletons — hide leftovers from earlier cases.
[[maybe_unused]] static void resetScene3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr]   = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data]  = *it;
            data->active = false;
        }
    }
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d]   = *it;
            d->enabled = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp]   = *it;
            sp->visible = false;
        }
    }
}

// NOTE: Graphics is a process-wide singleton — one window lifetime per process.


/** Left-half high / right-half low height so glancing POM shifts albedo UVs. */
[[maybe_unused]] static Texture *makeSplitHeight(Graphics *gfx, int size) {
    std::vector<uint8_t> px(size_t(size) * size_t(size) * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint8_t h = (x < size / 2) ? 255 : 0;
            size_t  i = size_t(y * size + x) * 4;
            px[i]     = h;
            px[i + 1] = h;
            px[i + 2] = h;
            px[i + 3] = 255;
        }
    }
    return gfx->newTexture(size, size, px.data(), true, true);
}


TEST_CASE("Camera3D.screenToRayPick") {
    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 5.f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setUp(0.f, 1.f, 0.f);
    cam->setFov(60.f);
    cam->screenToRay(160.f, 120.f, 320.f, 240.f);  // center pixel

    CHECK(std::fabs(cam->getScreenRayOriginX()) < 1e-4f);
    CHECK(std::fabs(cam->getScreenRayOriginY()) < 1e-4f);
    CHECK(std::fabs(cam->getScreenRayOriginZ() - 5.f) < 1e-4f);

    // Looking toward -Z; center ray should be mostly -Z.
    CHECK(std::fabs(cam->getScreenRayDirX()) < 0.05f);
    CHECK(std::fabs(cam->getScreenRayDirY()) < 0.05f);
    CHECK(cam->getScreenRayDirZ() < -0.9f);

    float len = std::sqrt(cam->getScreenRayDirX() * cam->getScreenRayDirX() +
                          cam->getScreenRayDirY() * cam->getScreenRayDirY() +
                          cam->getScreenRayDirZ() * cam->getScreenRayDirZ());
    CHECK(std::fabs(len - 1.f) < 1e-4f);
}

TEST_CASE("Camera3D.orthographicScreenRaysAreParallel") {
    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 0.f, 10.f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setOrthographic(8.f);

    cam->screenToRay(20.f, 60.f, 160.f, 120.f);
    const float leftOrigin = cam->getScreenRayOriginX();
    const float leftDirX   = cam->getScreenRayDirX();
    const float leftDirZ   = cam->getScreenRayDirZ();
    cam->screenToRay(140.f, 60.f, 160.f, 120.f);

    CHECK(cam->getScreenRayOriginX() > leftOrigin + 1.f);
    CHECK(std::fabs(cam->getScreenRayDirX() - leftDirX) < 1e-4f);
    CHECK(std::fabs(cam->getScreenRayDirZ() - leftDirZ) < 1e-4f);
    CHECK(cam->getScreenRayDirZ() < -0.99f);
}

// ---------------------------------------------------------------------------
// GPU 验证：逐像素实体 ID mask（renderEntityIdMask）与通用纹理读回
// （readTextureToImageData）。这两条路径支撑 capture_render_frame 的
// "id" / "depth" / "normal" 缓冲区，必须在真实 Vulkan 设备上跑通。
// ---------------------------------------------------------------------------


TEST_CASE("RenderSystem3D.shadowExtraDrawerOwnerTokens") {
    using eve::graphics::RenderSystem3D;
    CHECK(RenderSystem3D::addShadowExtraDrawer({}) == 0);
    const auto first  = RenderSystem3D::addShadowExtraDrawer([](auto &, const auto &, const auto &) {});
    const auto second = RenderSystem3D::addShadowExtraDrawer([](auto &, const auto &, const auto &) {});
    REQUIRE(first != 0);
    REQUIRE(second != 0);
    CHECK(first != second);
    RenderSystem3D::removeShadowExtraDrawer(first);
    RenderSystem3D::removeShadowExtraDrawer(first);
    RenderSystem3D::removeShadowExtraDrawer(second);
    RenderSystem3D::removeShadowExtraDrawer(0);
}
}  // namespace test_RenderSystem3D_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_Shadow3D)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "Fixtures.h"
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
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Shadow.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Shadow3D.cpp
namespace test_Shadow3D_unit {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {


[[maybe_unused]] void resetScene3D() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr]   = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data]  = *it;
            data->active = false;
        }
    }
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d]   = *it;
            d->enabled = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp]   = *it;
            sp->visible = false;
        }
    }
}

[[maybe_unused]] Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

[[maybe_unused]] Mesh *makeGroundPlane(Graphics *gfx) { return gfx->newMeshSphere(16, 8); }

[[maybe_unused]] void addHud() {
    auto *hud             = Renderable2D::create();
    hud->transform()->x   = 0;
    hud->transform()->y   = 0;
    hud->sprite()->width  = 2;
    hud->sprite()->height = 2;
}

[[maybe_unused]] float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

[[maybe_unused]] void warmPresent(Graphics *gfx) {
    for (int i = 0; i < 3; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }
}

/** Live-render the shadow scene to the window for ~1s. */
[[maybe_unused]] void previewScene(Graphics *gfx, int ms = 1000) {
    const int frames = (ms >= 16) ? (ms / 16) : 1;
    for (int i = 0; i < frames; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return;
        }
        SDL_Delay(16);
    }
}

/** Max (a - b) over a coarse pixel grid. Used because Vulkan FB Y / RH_ZO place
 *  this scene's umbra off the naive "center + below" sample point. */
[[maybe_unused]] float maxLumaDelta(Graphics *gfx, const std::vector<float> &a, const std::vector<float> &b,
                                    int step = 4) {
    float     best = -1.f;
    const int w    = gfx->getWidth();
    const int h    = gfx->getHeight();
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            const size_t i = size_t(y * w + x);
            best           = std::max(best, a[i] - b[i]);
        }
    }
    return best;
}

[[maybe_unused]] std::vector<float> captureLumaGrid(Graphics *gfx, int step = 4) {
    const int          w = gfx->getWidth();
    const int          h = gfx->getHeight();
    std::vector<float> out(size_t(w * h), 0.f);
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            out[size_t(y * w + x)] = luma(gfx->getPixel(x, y));
        }
    }
    return out;
}

[[maybe_unused]] void setupShadowScene(Graphics *gfx, Renderable3D *&ground, Light3D *&sun, bool groundReceive,
                                       Renderable3D **caster = nullptr) {
    auto *cam = Camera3D::createCamera();
    cam->setEye(0.f, 3.5f, 5.5f);
    cam->setTarget(0.f, 0.f, 0.f);
    cam->setAmbient(0.05f, 0.05f, 0.05f);

    ground = Renderable3D::create();
    ground->setMesh(makeGroundPlane(gfx));
    ground->setTexture(makeSolid(gfx, 220, 220, 220));
    ground->setPosition(0.f, -1.2f, 0.f);
    ground->setScale(4.f, 0.15f, 4.f);
    ground->setMetallic(0.f);
    ground->setRoughness(0.85f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(groundReceive);

    auto *box = Renderable3D::create();
    box->setMesh(gfx->newMeshSphere(20, 12));
    box->setTexture(makeSolid(gfx, 200, 200, 200));
    box->setPosition(0.f, 0.35f, 0.f);
    box->setScale(0.55f, 0.55f, 0.55f);
    box->setMetallic(0.f);
    box->setRoughness(0.6f);
    box->setCastShadow(true);
    box->setReceiveShadow(false);
    if (caster) *caster = box;

    sun = Light3D::createLight("dir");
    sun->setDirection(0.55f, 1.f, 0.35f);
    sun->setColor(1.f, 1.f, 1.f, 2.5f);
    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    sun->setShadowBias(0.003f);

    addHud();
}

}  // namespace


TEST_CASE("Shadow.directionalCSM.clampsFarAndSplitsAreViewZ") {
    const glm::vec3 dir(0.3f, 1.f, 0.2f);
    const glm::vec3 eye(0.f, 2.f, 8.f);
    const glm::vec3 target(0.f, 1.f, 0.f);
    const glm::vec3 up(0.f, 1.f, 0.f);
    ShadowUpload    u = buildDirectionalCSM(dir, eye, target, up, 1.047f, 16.f / 9.f, 0.1f, 500.f, 0.002f, 1.f);
    REQUIRE(u.active);
    // Far used for splits must be the practical shadow distance, not camera far=500.
    CHECK(u.ubo.splits.x > 0.1f);
    CHECK(u.ubo.splits.x < u.ubo.splits.y);
    CHECK(u.ubo.splits.y <= u.ubo.splits.z + 1e-4f);
    CHECK(u.ubo.splits.z <= ShadowConfig::kMaxDistance + 1e-3f);
    CHECK(u.ubo.splits.z < 100.f);
    CHECK(u.ubo.bias.x >= 0.002f);
}
}  // namespace test_Shadow3D_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ShadowScheme)
#include "PrivateTestScope.h"
#include <string>
#include <vector>
#include "graphics/ShadowScheme.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// ShadowScheme.cpp
namespace test_ShadowScheme {
using namespace eve::graphics;

namespace {

ShadowPagingView makeViewAtOrigin() {
    ShadowPagingView view{};
    view.eye      = glm::vec3(0.f, 2.f, 8.f);
    view.viewProj = glm::mat4(1.f);  // identity: world ≈ clip for coarse tests
    view.valid    = true;
    return view;
}

Light3D *makeSpot(float x, float y, float z, float intensity) {
    Light3D *spot = Light3D::createLight("spot");
    spot->setPosition(x, y, z);
    spot->setDirection(0.f, -1.f, 0.f);
    spot->setColor(1.f, 1.f, 1.f, intensity);
    spot->setRadius(10.f);
    spot->setSpotAngle(35.f);
    spot->setCastShadow(true);
    spot->setShadowMethod("perspective");
    return spot;
}

}  // namespace

TEST_CASE("graphics.shadow_scheme.parse_and_resolve_methods") {
    CHECK(parseShadowMethod("auto").ok());
    CHECK_EQ(static_cast<int>(parseShadowMethod("csm").value()), static_cast<int>(ShadowMethod::CascadedDirectional));
    CHECK_EQ(static_cast<int>(parseShadowMethod("perspective").value()),
             static_cast<int>(ShadowMethod::PerspectiveSpot));
    CHECK_EQ(static_cast<int>(parseShadowMethod("cube").value()), static_cast<int>(ShadowMethod::CubePoint));
    CHECK(!parseShadowMethod("nope").ok());

    CHECK_EQ(static_cast<int>(resolveShadowMethod("dir", ShadowMethod::Auto).value()),
             static_cast<int>(ShadowMethod::CascadedDirectional));
    CHECK_EQ(static_cast<int>(resolveShadowMethod("spot", ShadowMethod::Auto).value()),
             static_cast<int>(ShadowMethod::PerspectiveSpot));
    CHECK_EQ(static_cast<int>(resolveShadowMethod("point", ShadowMethod::Auto).value()),
             static_cast<int>(ShadowMethod::CubePoint));
    CHECK(!resolveShadowMethod("dir", ShadowMethod::PerspectiveSpot).ok());
    CHECK(!resolveShadowMethod("spot", ShadowMethod::CascadedDirectional).ok());
    CHECK_EQ(std::string(shadowMethodName(ShadowMethod::PerspectiveSpot)), std::string("perspective"));
}

TEST_CASE("graphics.shadow_scheme.selects_spot_slots_and_builds_vp") {
    resetShadowLocalPageCache();
    Light3D *sun = Light3D::createLight("dir");
    sun->setDirection(-0.4f, 0.9f, 0.2f);
    sun->setColor(1.f, 1.f, 1.f, 2.f);
    sun->setCastShadow(true);
    sun->setShadowMethod("csm");

    Light3D *spot = makeSpot(0.f, 4.f, 0.f, 3.f);

    Light3D *point = Light3D::createLight("point");
    point->setPosition(2.f, 2.f, 2.f);
    point->setColor(1.f, 1.f, 1.f, 1.f);
    point->setRadius(6.f);
    point->setCastShadow(true);
    point->setShadowMethod("cube");

    Light3D::Data               *sunData   = &*sun->data();
    Light3D::Data               *spotData  = &*spot->data();
    Light3D::Data               *pointData = &*point->data();
    std::vector<Light3D::Data *> lights{sunData, spotData, pointData};
    std::vector<bool>            isPoint{false, true, true};
    ShadowSchemeSettings         settings = ShadowSchemeSettings::current();
    settings.enableDirectionalCsm         = true;
    settings.enableSpotPerspective        = true;
    settings.enablePointCube              = false;
    settings.maxSpotShadowCasters         = 2;
    settings.enableLocalPaging            = false;

    Light3D::Data               *directional = nullptr;
    std::vector<LocalShadowSlot> locals;
    selectShadowCasters(lights, isPoint, settings, ShadowPagingView{}, directional, locals);
    CHECK(directional == sunData);
    REQUIRE_EQ(locals.size(), size_t{1});
    CHECK(locals[0].light == spotData);
    CHECK_EQ(locals[0].layer, ShadowConfig::kCascades);
    CHECK(locals[0].needsUpdate);
    CHECK_EQ(spotData->shadowLocalSlot, 0);
    CHECK_EQ(pointData->shadowLocalSlot, -1);

    const glm::mat4 vp     = buildSpotShadowVP(glm::vec3(0.f, 4.f, 0.f), glm::vec3(0.f, -1.f, 0.f), 12.f, 35.f);
    const glm::vec4 origin = vp * glm::vec4(0.f, 0.f, 0.f, 1.f);
    CHECK(origin.w > 0.f);

    auto face = buildPointShadowFaceVP(glm::vec3(0.f), 0, 8.f);
    CHECK(face.ok());
    CHECK(!buildPointShadowFaceVP(glm::vec3(0.f), 6, 8.f).ok());
}

TEST_CASE("graphics.shadow_scheme.pages_more_spots_than_atlas_slots") {
    resetShadowLocalPageCache();
    ShadowSchemeSettings settings    = ShadowSchemeSettings::current();
    settings.enableSpotPerspective   = true;
    settings.enableDirectionalCsm    = false;
    settings.maxSpotShadowCasters    = 2;
    settings.enableLocalPaging       = true;
    settings.hysteresisBonus         = 0.f;
    settings.maxLocalUpdatesPerFrame = 4;

    // Five spots near the camera; only two atlas slots.
    Light3D                     *a = makeSpot(0.f, 2.f, 0.f, 10.f);
    Light3D                     *b = makeSpot(1.f, 2.f, 0.f, 8.f);
    Light3D                     *c = makeSpot(2.f, 2.f, 0.f, 6.f);
    Light3D                     *d = makeSpot(3.f, 2.f, 0.f, 4.f);
    Light3D                     *e = makeSpot(4.f, 2.f, 0.f, 2.f);
    std::vector<Light3D::Data *> lights{&*a->data(), &*b->data(), &*c->data(), &*d->data(), &*e->data()};
    std::vector<bool>            isPoint(5, true);

    Light3D::Data               *directional = nullptr;
    std::vector<LocalShadowSlot> locals;
    selectShadowCasters(lights, isPoint, settings, makeViewAtOrigin(), directional, locals);
    REQUIRE_EQ(locals.size(), size_t{2});
    CHECK(locals[0].light == &*a->data());
    CHECK(locals[1].light == &*b->data());
    CHECK_EQ(a->data()->shadowLocalSlot, 0);
    CHECK_EQ(b->data()->shadowLocalSlot, 1);
    CHECK_EQ(c->data()->shadowLocalSlot, -1);
}

TEST_CASE("graphics.shadow_scheme.hysteresis_keeps_sticky_slot") {
    resetShadowLocalPageCache();
    ShadowSchemeSettings settings    = ShadowSchemeSettings::current();
    settings.enableSpotPerspective   = true;
    settings.maxSpotShadowCasters    = 1;
    settings.enableLocalPaging       = false;  // intensity-only scores for a clear sticky check
    settings.hysteresisBonus         = 0.5f;   // 5 * 1.5 = 7.5 > 6
    settings.maxLocalUpdatesPerFrame = 4;

    Light3D                     *resident = makeSpot(0.f, 2.f, 0.f, 5.f);
    std::vector<Light3D::Data *> lights{&*resident->data()};
    std::vector<bool>            isPoint{true};
    const ShadowPagingView       view{};

    Light3D::Data               *directional = nullptr;
    std::vector<LocalShadowSlot> locals;
    selectShadowCasters(lights, isPoint, settings, view, directional, locals);
    REQUIRE_EQ(locals.size(), size_t{1});
    CHECK(locals[0].light == &*resident->data());

    // Challenger is brighter raw (6 > 5), but hadSlot × hysteresis keeps the resident (7.5).
    Light3D *challenger = makeSpot(1.f, 2.f, 0.f, 6.f);
    lights              = {&*resident->data(), &*challenger->data()};
    isPoint             = {true, true};
    locals.clear();
    selectShadowCasters(lights, isPoint, settings, view, directional, locals);
    REQUIRE_EQ(locals.size(), size_t{1});
    CHECK(locals[0].light == &*resident->data());
}

TEST_CASE("graphics.shadow_scheme.update_budget_skips_stable_slots") {
    resetShadowLocalPageCache();
    ShadowSchemeSettings settings    = ShadowSchemeSettings::current();
    settings.enableSpotPerspective   = true;
    settings.maxSpotShadowCasters    = 2;
    settings.enableLocalPaging       = true;
    settings.maxLocalUpdatesPerFrame = 1;

    Light3D                     *a = makeSpot(0.f, 2.f, 0.f, 10.f);
    Light3D                     *b = makeSpot(1.f, 2.f, 0.f, 9.f);
    std::vector<Light3D::Data *> lights{&*a->data(), &*b->data()};
    std::vector<bool>            isPoint{true, true};
    const ShadowPagingView       view = makeViewAtOrigin();

    Light3D::Data               *directional = nullptr;
    std::vector<LocalShadowSlot> locals;
    selectShadowCasters(lights, isPoint, settings, view, directional, locals);
    REQUIRE_EQ(locals.size(), size_t{2});
    // First frame: both new, but budget=1 → only one needsUpdate.
    int updating = 0;
    for (const auto &slot : locals)
        if (slot.needsUpdate) ++updating;
    CHECK_EQ(updating, 1);

    // Second frame, unchanged lights: no dirty updates.
    locals.clear();
    selectShadowCasters(lights, isPoint, settings, view, directional, locals);
    REQUIRE_EQ(locals.size(), size_t{2});
    updating = 0;
    for (const auto &slot : locals)
        if (slot.needsUpdate) ++updating;
    CHECK_EQ(updating, 0);

    // Move one light → dirty; budget still 1.
    a->setPosition(0.2f, 2.f, 0.f);
    locals.clear();
    selectShadowCasters(lights, isPoint, settings, view, directional, locals);
    updating = 0;
    for (const auto &slot : locals)
        if (slot.needsUpdate) ++updating;
    CHECK_EQ(updating, 1);
}

TEST_CASE("graphics.light3d_spot_and_shadow_method_api") {
    Light3D *spot = Light3D::createLight("spot");
    CHECK_EQ(spot->getType(), "spot");
    spot->setSpotAngle(40.f);
    spot->setSpotSoftness(0.5f);
    CHECK_EQ(spot->getSpotAngle(), 40.f);
    CHECK_EQ(spot->getSpotSoftness(), 0.5f);
    spot->setShadowMethod("perspective");
    CHECK_EQ(spot->getShadowMethod(), "perspective");
    spot->setShadowMethod("auto");
    CHECK_EQ(spot->getShadowMethod(), "auto");
    spot->setShadowMethod("not-a-method");
    CHECK_EQ(spot->getShadowMethod(), "auto");
}
}  // namespace test_ShadowScheme
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_SkyAtmosphereLuts)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "graphics/sky/SkyAtmosphereLuts.h"
#include "graphics/sky/SkyAtmospherePass.h"
#include "graphics/sky/SkyOpticalCycle.h"
#include "zeroerr/unittest.h"
// SkyAtmosphereLuts.cpp
namespace test_SkyAtmosphereLuts {
TEST_CASE("graphics.sky atmosphere vacuum preserves transmission without creating scattered energy") {
    eve::graphics::SkyAtmosphereParameters parameters;
    parameters.rayleigh = parameters.mieScattering = parameters.mieAbsorption = parameters.ozoneAbsorption =
        parameters.groundAlbedo                                               = {0, 0, 0};
    auto baked = eve::graphics::detail::bakeSkyAtmosphereLuts(parameters);
    REQUIRE(baked.ok());
    const auto &tables = baked.value();
    REQUIRE(tables.transmittance.size() == 256 * 64 * 4);
    REQUIRE(tables.multiScattering.size() == 32 * 32 * 4);
    for (size_t i = 0; i < tables.transmittance.size(); ++i) REQUIRE(tables.transmittance[i] == 1);
    for (size_t i = 0; i < tables.multiScattering.size(); ++i)
        REQUIRE(tables.multiScattering[i] == (i % 4 == 3 ? 1.f : 0.f));
    auto ambient = eve::graphics::detail::bakeDistantSkyAmbient(parameters, tables, {0, 1, 0});
    REQUIRE(ambient.ok());
    for (float channel : ambient.value()) REQUIRE(channel == 0);
}

TEST_CASE("graphics.sky multiple scattering adds finite energy independently of transmission") {
    eve::graphics::SkyAtmosphereParameters parameters;
    auto                                   enabled = eve::graphics::detail::bakeSkyAtmosphereLuts(parameters);
    REQUIRE(enabled.ok());
    parameters.multiScatteringFactor = 0;
    auto disabled                    = eve::graphics::detail::bakeSkyAtmosphereLuts(parameters);
    REQUIRE(disabled.ok());
    REQUIRE(enabled.value().transmittance == disabled.value().transmittance);
    double total = 0;
    for (size_t i = 0; i < enabled.value().multiScattering.size(); ++i) {
        if (i % 4 == 3) continue;
        const auto value = enabled.value().multiScattering[i];
        REQUIRE(std::isfinite(value));
        REQUIRE(value >= 0);
        REQUIRE(disabled.value().multiScattering[i] == 0);
        total += value;
    }
    REQUIRE(total > 0);
    for (auto transmission : enabled.value().transmittance) {
        REQUIRE(std::isfinite(transmission));
        REQUIRE(transmission >= 0);
        REQUIRE(transmission <= 1);
    }
    parameters.groundAlbedo[0] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!eve::graphics::detail::bakeSkyAtmosphereLuts(parameters).ok());
}

TEST_CASE("graphics.sky distant ambient responds to solar elevation and rejects invalid preparation") {
    eve::graphics::SkyAtmosphereParameters parameters;
    auto                                   tables = eve::graphics::detail::bakeSkyAtmosphereLuts(parameters);
    REQUIRE(tables.ok());
    auto day    = eve::graphics::detail::bakeDistantSkyAmbient(parameters, tables.value(), {0, 1, 0});
    auto night  = eve::graphics::detail::bakeDistantSkyAmbient(parameters, tables.value(), {0, -1, 0});
    auto scaled = eve::graphics::detail::bakeDistantSkyAmbient(parameters, tables.value(), {0, 2, 0});
    REQUIRE(day.ok());
    REQUIRE(night.ok());
    REQUIRE(scaled.ok());
    for (size_t axis = 0; axis < 3; ++axis) {
        REQUIRE(std::isfinite(day.value()[axis]));
        REQUIRE(day.value()[axis] > night.value()[axis]);
        REQUIRE(night.value()[axis] >= 0);
        REQUIRE(day.value()[axis] == scaled.value()[axis]);
    }
    REQUIRE(!eve::graphics::detail::bakeDistantSkyAmbient(parameters, tables.value(), {0, 0, 0}).ok());
    auto corrupted = tables.value();
    corrupted.multiScattering.pop_back();
    REQUIRE(!eve::graphics::detail::bakeDistantSkyAmbient(parameters, corrupted, {0, 1, 0}).ok());
    corrupted                  = tables.value();
    corrupted.transmittance[0] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!eve::graphics::detail::bakeDistantSkyAmbient(parameters, corrupted, {0, 1, 0}).ok());
    parameters.atmosphereHeightKm = 5;
    REQUIRE(!eve::graphics::detail::bakeDistantSkyAmbient(parameters, tables.value(), {0, 1, 0}).ok());
}

TEST_CASE("graphics.sky optical cycle matches source component values and bounded LUT interpolation") {
    using namespace eve::graphics;
    SkyDaylightLayer        d;
    SkyAtmosphereParameters p;
    auto                    noon = detail::opticalSample(p, d, 1), midnight = detail::opticalSample(p, d, -1),
         dawn = detail::opticalSample(p, d, 0);
    REQUIRE(std::abs(noon.rayleigh[0] - .00674508f) < 1e-7f);
    REQUIRE(std::abs(midnight.rayleigh[2] - .024375f) < 1e-7f);
    REQUIRE(std::abs(dawn.rayleigh[2] - .03366377f) < 1e-7f);
    REQUIRE(std::abs(noon.ozoneAbsorption[0] - .001794476f) < 1e-8f);
    REQUIRE(std::abs(midnight.ozoneAbsorption[1] - .003396696f) < 1e-8f);
    REQUIRE(std::abs(dawn.ozoneAbsorption[1] - .002927907f) < 1e-8f);
    for (float y : {-.0625f, -.0275f, .0025f, .0575f}) {
        auto a     = detail::bakeSkyAtmosphereLuts(detail::opticalSample(p, d, y - .0025f));
        auto b     = detail::bakeSkyAtmosphereLuts(detail::opticalSample(p, d, y + .0025f));
        auto exact = detail::bakeSkyAtmosphereLuts(detail::opticalSample(p, d, y));
        REQUIRE(a.ok());
        REQUIRE(b.ok());
        REQUIRE(exact.ok());
        // The original .002 bound covers optical interpolation. Each packed
        // endpoint and the independently baked center add half a storage ULP;
        // budget these separately rather than hiding them in a global epsilon.
        const auto checkPackedInterpolation = [&](const auto &low, const auto &high, const auto &center) {
            for (size_t i = 0; i < center.size(); ++i) {
                if (i % 4 == 3) continue;
                const int  mantissaBits = i % 4 == 2 ? 5 : 6;
                const auto halfUlp      = [mantissaBits](float value) {
                    int         exponent = 0;
                    const float fraction = std::frexp(value, &exponent);
                    if (fraction == 0) exponent = -13;
                    return std::ldexp(1.f, std::max(-14, exponent - 1) - mantissaBits - 1);
                };
                const float storageError = .5f * (halfUlp(low[i]) + halfUlp(high[i])) + halfUlp(center[i]);
                REQUIRE(std::abs((low[i] + high[i]) * .5f - center[i]) < .002f + storageError);
            }
        };
        checkPackedInterpolation(a.value().transmittance, b.value().transmittance, exact.value().transmittance);
        checkPackedInterpolation(a.value().multiScattering, b.value().multiScattering, exact.value().multiScattering);
    }
}
}  // namespace test_SkyAtmosphereLuts
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_Volumetric)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <string>
#include <vector>
#include "Fixtures.h"
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
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Volumetric.cpp
namespace test_Volumetric_unit {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {


[[maybe_unused]] Texture *makeSolid(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    const uint8_t px[4] = {r, g, b, a};
    return gfx->newTexture(1, 1, px);
}

[[maybe_unused]] float luma(const Color &c) { return (c.r + c.g + c.b) / 3.f; }

/** Hold a textured canvas / texture on the swapchain for ~1s. */
[[maybe_unused]] void previewTexture(Graphics *gfx, Texture *tex, int ms = 1000) {
    gfx->setBackgroundColorRGBA(0.05f, 0.05f, 0.08f, 1.f);
    const int frames = (ms >= 16) ? (ms / 16) : 1;
    for (int i = 0; i < frames; ++i) {
        gfx->clearScreen();
        gfx->drawTexturedRectRGBA(tex, 0, 0, float(gfx->getWidth()), float(gfx->getHeight()), 1, 1, 1, 1);
        gfx->present();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return;
        }
        SDL_Delay(16);
    }
}

}  // namespace


TEST_CASE("volumetric.drawableOcclusionFlags") {
    Texture tex;
    CHECK(tex.getCastOcclusion() == true);
    tex.setCastOcclusion(false);
    CHECK(tex.getCastOcclusion() == false);

    Mesh mesh;
    CHECK(mesh.getCastOcclusion() == true);
    mesh.setCastOcclusion(false);
    CHECK(mesh.getCastOcclusion() == false);

    auto *r = Renderable2D::create();
    CHECK(r->getCastOcclusion() == true);
    r->setCastOcclusion(false);
    CHECK(r->getCastOcclusion() == false);

    auto *r3 = Renderable3D::create();
    CHECK(r3->getCastOcclusion() == true);
    r3->setCastOcclusion(false);
    CHECK(r3->getCastOcclusion() == false);
}


TEST_CASE("volumetric.lightFlags") {
    auto *l2 = Light2D::createLight("point");
    CHECK(l2->getVolumetric() == false);
    l2->setVolumetric(true);
    l2->setVolumetricIntensity(0.75f);
    CHECK(l2->getVolumetric() == true);
    CHECK(std::fabs(l2->getVolumetricIntensity() - 0.75f) < 1e-5f);
    CHECK(l2->getVolumetricOnly() == false);
    l2->setVolumetricOnly(true);
    CHECK(l2->getVolumetricOnly() == true);

    auto *l3 = Light3D::createLight("dir");
    CHECK(l3->getVolumetric() == false);
    l3->setVolumetric(true);
    CHECK(l3->getVolumetric() == true);
    CHECK(l3->getVolumetricOnly() == false);
}

TEST_CASE("volumetric.emissiveProxySkipsSurfaceLighting") {
    auto *glow2 = Light2D::createEmissiveProxy(40.f, 30.f, 1.f, 0.4f, 0.1f, 2.f, 80.f);
    CHECK(glow2->getVolumetric() == true);
    CHECK(glow2->getVolumetricOnly() == true);
    CHECK(std::fabs(glow2->getX() - 40.f) < 1e-5f);

    auto *glow3 = Light3D::createEmissiveProxy(1.f, 2.f, 3.f, 0.2f, 0.8f, 1.f, 4.f, 6.f);
    CHECK(glow3->getVolumetric() == true);
    CHECK(glow3->getVolumetricOnly() == true);
    CHECK(glow3->getCastShadow() == false);
    CHECK(std::fabs(glow3->getRadius() - 6.f) < 1e-5f);
}


namespace {

struct BoxDepth {
    int   w = 0, h = 0;
    float boxU0 = 0.35f, boxU1 = 0.65f, boxV0 = 0.35f, boxV1 = 0.65f;
    float boxDepth   = 0.35f;
    float floorDepth = 0.85f;
};

[[maybe_unused]] float boxDepth01(int x, int y, void *userdata) {
    auto       *b = static_cast<BoxDepth *>(userdata);
    const float u = (float(x) + 0.5f) / float(b->w);
    const float v = (float(y) + 0.5f) / float(b->h);
    if (u >= b->boxU0 && u <= b->boxU1 && v >= b->boxV0 && v <= b->boxV1) return b->boxDepth;
    return b->floorDepth;
}

}  // namespace
}  // namespace test_Volumetric_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_atmosphere_volume)
#include "PrivateTestScope.h"
#include <cmath>
#include "graphics/AtmosphereClipmap.h"
#include "graphics/AtmosphereVolume.h"
#include "graphics/FogVolume.h"
#include "graphics/SparseVolumeTexture.h"
#include "graphics/VolumeDensityGraph.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// atmosphere_volume.cpp
namespace test_atmosphere_volume_unit {
using eve::graphics::AtmosphereClipmap;
using eve::graphics::AtmosphereVolume;
using eve::graphics::FogVolume;
using eve::graphics::SparseVolumeTexture;
using eve::graphics::VolumeDensityGraph;
using eve::graphics::VolumetricLight;

TEST_CASE("atmosphereVolume.logarithmicSlicesRoundTrip") {
    AtmosphereVolume volume;
    volume.resize(4, 3, 32);
    volume.setDepthRange(0.1f, 1000.f);
    for (int z = 0; z < volume.getDepth(); ++z) CHECK(volume.sliceForDistance(volume.sliceDistance(z)) == z);
}

TEST_CASE("atmosphereVolume.uniformBeerLambert") {
    AtmosphereVolume volume;
    volume.resize(1, 1, 64);
    volume.setDepthRange(1.f, 101.f);
    for (int z = 0; z < volume.getDepth(); ++z) {
        volume.at(0, 0, z).extinction = 0.02f;
        volume.at(0, 0, z).scattering = glm::vec3(0.02f);
    }
    volume.integrate(glm::vec3(1.f));
    const float actual   = volume.integratedAt(0, 0, 63).a;
    const float expected = std::exp(-0.02f * (volume.sliceDistance(63) - 1.f));
    CHECK(std::fabs(actual - expected) < 1e-5f);
}

TEST_CASE("atmosphereVolume.heightFogFallsOffUpward") {
    AtmosphereVolume volume;
    volume.resize(2, 8, 4);
    volume.injectHeightFog(0.2f, glm::vec3(0.8f), 0.f, 0.5f, 0.f, 8.f);
    CHECK(volume.at(0, 0, 0).extinction > volume.at(0, 7, 0).extinction);
}

TEST_CASE("atmosphereVolume.frustumHeightFogUsesReconstructedWorldHeight") {
    AtmosphereVolume volume;
    volume.resize(1, 2, 4);
    volume.setDepthRange(0.1f, 1.f);
    glm::mat4 invViewProj(1.f);
    invViewProj[1] = glm::vec4(0.f, 0.f, 1.f, 0.f);
    invViewProj[2] = glm::vec4(0.f, 1.f, 0.f, 0.f);
    volume.injectHeightFogFrustum(0.2f, glm::vec3(0.8f), 0.f, 2.f, -2.f, 2.f, invViewProj);
    CHECK(std::fabs(volume.at(0, 0, 0).extinction - volume.at(0, 1, 0).extinction) < 1e-6f);
    CHECK(volume.at(0, 0, 0).extinction > volume.at(0, 0, 3).extinction);
}

TEST_CASE("atmosphereVolume.directionalShadowReducesScattering") {
    AtmosphereVolume lit;
    lit.resize(1, 1, 8);
    lit.setDepthRange(1.f, 20.f);
    for (int z = 0; z < 8; ++z) {
        lit.at(0, 0, z).extinction = 0.1f;
        lit.at(0, 0, z).scattering = glm::vec3(0.08f);
    }
    AtmosphereVolume shadowed = lit;
    for (int z = 0; z < 8; ++z) shadowed.setLightVisibility(0, 0, z, 0.f);
    lit.integrate(glm::vec3(1.f));
    shadowed.integrate(glm::vec3(1.f));
    CHECK(lit.integratedAt(0, 0, 7).r > shadowed.integratedAt(0, 0, 7).r);
}

TEST_CASE("atmosphereVolume.temporalHistoryRejectsLightingDiscontinuity") {
    AtmosphereVolume current;
    current.resize(2, 1, 2);
    current.setDepthRange(1.f, 4.f);
    for (int z = 0; z < 2; ++z) {
        current.at(0, 0, z).emissive = glm::vec3(2.f);
        current.at(1, 0, z).emissive = glm::vec3(0.2f);
    }
    current.integrate(glm::vec3(0.f));
    AtmosphereVolume history = current;
    history.clear();
    history.integrate(glm::vec3(0.f));
    CHECK(current.blendHistory(history, 0.9f, 0.2f) == 4);
    CHECK(current.integratedAt(0, 0, 1).r > 0.f);
}

TEST_CASE("atmosphereVolume.localVolumesAddAndCarveMedia") {
    AtmosphereVolume volume;
    volume.resize(8, 8, 8);
    volume.injectHeightFog(0.1f, glm::vec3(1.f), 0.f, 0.f, -4.f, 4.f);
    FogVolume add;
    add.setShape("sphere");
    add.setSize(4.f, 4.f, 4.f);
    add.setExtinction(0.2f);
    add.setEdgeFalloff(0.f);
    volume.injectLocalVolume(add, glm::vec3(-4.f), glm::vec3(4.f));
    CHECK(volume.at(4, 4, 4).extinction > volume.at(0, 0, 0).extinction);

    FogVolume carve = add;
    carve.setExtinction(-1.f);
    volume.injectLocalVolume(carve, glm::vec3(-4.f), glm::vec3(4.f));
    CHECK(volume.at(4, 4, 4).extinction == 0.f);
    CHECK(volume.at(0, 0, 0).extinction > 0.f);
}

TEST_CASE("fogVolume.noiseIsDeterministicAndSpatiallyVaried") {
    FogVolume volume;
    volume.setShape("box");
    volume.setSize(20.f, 20.f, 20.f);
    volume.setExtinction(1.f);
    volume.setEdgeFalloff(0.f);
    volume.setNoise(1.f, 0.7f, 42);
    const float first = volume.sampleExtinction(glm::vec3(0.25f, 0.5f, 0.75f));
    CHECK(first == volume.sampleExtinction(glm::vec3(0.25f, 0.5f, 0.75f)));
    CHECK(std::fabs(first - volume.sampleExtinction(glm::vec3(1.75f, 0.5f, 0.75f))) > 1e-4f);
    volume.setNoise(0.f, 0.7f, 42);
    CHECK(std::fabs(volume.sampleExtinction(glm::vec3(0.25f, 0.5f, 0.75f)) - 1.f) < 1e-6f);
}

TEST_CASE("atmosphereVolume.localLightAndTransparentDepthQuery") {
    AtmosphereVolume volume;
    volume.resize(4, 4, 16);
    volume.setDepthRange(0.5f, 30.f);
    volume.injectHeightFog(0.04f, glm::vec3(0.8f), 0.f, 0.f, -4.f, 4.f);
    VolumetricLight light;
    light.position = glm::vec3(0.f);
    light.radius   = 6.f;
    light.color    = glm::vec3(1.f, 0.2f, 0.1f);
    volume.integrateLocalLights({light}, glm::vec3(-8.f), glm::vec3(8.f), glm::vec3(0.02f));
    const glm::vec4 nearFog = volume.sampleIntegrated(0.5f, 0.5f, 1.f);
    const glm::vec4 farFog  = volume.sampleIntegrated(0.5f, 0.5f, 20.f);
    CHECK(farFog.a < nearFog.a);
    CHECK(farFog.r > farFog.b);
}

TEST_CASE("atmosphereClipmap.snapsCentersAndChoosesFinestLevel") {
    AtmosphereClipmap clipmap;
    clipmap.configure(3, 8, 8.f);
    clipmap.updateCenter(glm::vec3(1.1f, 0.f, 0.f));
    const glm::vec3 firstCenter = clipmap.getLevel(0).center;
    clipmap.updateCenter(glm::vec3(1.9f, 0.f, 0.f));
    CHECK(clipmap.getLevel(0).center == firstCenter);
    CHECK(clipmap.levelForPosition(glm::vec3(4.f, 0.f, 0.f)) == 0);
    CHECK(clipmap.levelForPosition(glm::vec3(12.f, 0.f, 0.f)) == 1);
    CHECK(clipmap.levelForPosition(glm::vec3(28.f, 0.f, 0.f)) == 2);
    CHECK(clipmap.levelForPosition(glm::vec3(80.f, 0.f, 0.f)) == -1);
}

TEST_CASE("volumeDensityGraph.composesShapeNoiseAndHeight") {
    VolumeDensityGraph graph;
    const int          sphere = graph.addNode(VolumeDensityGraph::Op::sphere, -1, -1, glm::vec4(0.f, 0.f, 0.f, 4.f));
    const int          noise  = graph.addNode(VolumeDensityGraph::Op::noise, -1, -1, glm::vec4(2.f, 0.2f, 0.f, 7.f));
    const int          shaped = graph.addNode(VolumeDensityGraph::Op::multiply, sphere, noise);
    graph.setOutput(shaped);
    CHECK(graph.evaluate(glm::vec3(0.f), 1.f) > 0.f);
    CHECK(graph.evaluate(glm::vec3(10.f), 1.f) == 0.f);

    AtmosphereVolume volume;
    volume.resize(8, 8, 8);
    volume.injectDensityGraph(graph, glm::vec3(-4.f), glm::vec3(4.f), 0.2f, glm::vec3(0.8f), 1.f);
    CHECK(volume.at(4, 4, 4).extinction > 0.f);
}

TEST_CASE("sparseVolumeTexture.allocatesOnlyOccupiedBricksAndInjects") {
    VolumeDensityGraph graph;
    graph.addNode(VolumeDensityGraph::Op::sphere, -1, -1, glm::vec4(0.f, 0.f, 0.f, 2.f));
    SparseVolumeTexture sparse;
    sparse.resize(32, 32, 32, 8);
    sparse.bake(graph, glm::vec3(-16.f), glm::vec3(16.f), 0.2f, glm::vec3(0.7f));
    CHECK(sparse.getBrickCount() > 0);
    CHECK(sparse.getBrickCount() < 64);
    CHECK(sparse.sample(0.f, 0.f, 0.f).extinction == 0.f);
    CHECK(sparse.sample(0.5f, 0.5f, 0.5f).extinction > 0.f);

    AtmosphereVolume volume;
    volume.resize(16, 16, 16);
    volume.injectSparseVolume(sparse);
    CHECK(volume.at(8, 8, 8).extinction > 0.f);
    CHECK(volume.at(0, 0, 0).extinction == 0.f);
}
}  // namespace test_atmosphere_volume_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_depth_of_field_focus)
#include "PrivateTestScope.h"
#include <limits>
#include "graphics/DepthOfFieldFocus.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// depth_of_field_focus.cpp
namespace test_depth_of_field_focus {
using namespace eve::graphics;
TEST_CASE("graphics.depthOfFieldFocus.matchesPcgTrackingAndSmoothing") {
    DepthOfFieldFocusState    state;
    DepthOfFieldFocusOutput   out;
    DepthOfFieldFocusSettings settings;
    DepthOfFieldFocusInput    input;
    input.hasRayHit      = true;
    input.rayHitDistance = 20.f;
    input.deltaSeconds   = 0.1f;
    REQUIRE(evaluateDepthOfFieldFocus(&state, &out, &settings, &input).ok());
    CHECK_EQ(out.focusDistance, 72.f);
    CHECK_EQ(out.aperture, 7.5f);
    CHECK_EQ(out.focalLength, 30.f);
    input.hitPlayer = true;
    REQUIRE(evaluateDepthOfFieldFocus(&state, &out, &settings, &input).ok());
    CHECK_EQ(out.focusDistance, 81.8f);
    settings.tracking    = int(DepthOfFieldTracking::FixedOffset);
    settings.focusOffset = 12.f;
    REQUIRE(evaluateDepthOfFieldFocus(&state, &out, &settings, &input).ok());
    CHECK_EQ(out.focusDistance, 12.f);
    auto before              = state;
    settings.maximumDistance = std::numeric_limits<float>::quiet_NaN();
    CHECK(!evaluateDepthOfFieldFocus(&state, &out, &settings, &input).ok());
    CHECK_EQ(state.focusDistance, before.focusDistance);
}
TEST_CASE("graphics.depthOfFieldFocus.followTargetUsesObservedDistance") {
    DepthOfFieldFocusState    state;
    DepthOfFieldFocusOutput   out;
    DepthOfFieldFocusSettings settings;
    DepthOfFieldFocusInput    input;
    settings.tracking    = int(DepthOfFieldTracking::FollowTarget);
    settings.focusOffset = 2.f;
    settings.response    = 10.f;
    input.hasTarget      = true;
    input.targetDistance = 8.f;
    input.deltaSeconds   = 1.f;
    REQUIRE(evaluateDepthOfFieldFocus(&state, &out, &settings, &input).ok());
    CHECK_EQ(out.focusDistance, 10.f);
}
}  // namespace test_depth_of_field_focus
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_diffuse_light_probe)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include "common/ECS.h"
#include "common/ProcgenProbeSink.h"
#include "graphics/Canvas.h"
#include "graphics/DiffuseLightProbeRegistry.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "image/ImageData.h"
// diffuse_light_probe.cpp
namespace test_diffuse_light_probe_unit {
using namespace eve;
using namespace eve::graphics;

TEST_CASE("graphics.lightProbe.generatedBatchBlendsAndRemovesAtomically") {
    ProcgenProbeDesc red;
    red.sourcePointId = 1;
    red.type          = 1;
    red.x             = -1.f;
    red.extentX = red.extentY = red.extentZ = 4.f;
    red.irradianceR                         = 1.f;
    red.irradianceG = red.irradianceB = 0.f;
    ProcgenProbeDesc blue             = red;
    blue.sourcePointId                = 2;
    blue.x                            = 1.f;
    blue.irradianceR                  = 0.f;
    blue.irradianceB                  = 1.f;

    auto &registry = DiffuseLightProbeRegistry::instance();
    registry.replaceBatch("light-probe-test", {red, blue});
    auto sample = registry.sample(glm::vec3(0.f), 4);
    REQUIRE(sample.ok());
    const glm::vec3 irradiance = glm::vec3(sample.value().coefficients[0]) * 0.2820947918f;
    CHECK(irradiance.r > 0.49f);
    CHECK(irradiance.r < 0.51f);
    CHECK(irradiance.b > 0.49f);
    CHECK(irradiance.b < 0.51f);
    CHECK(!registry.sample(glm::vec3(20.f), 4).ok());
    registry.removeBatch("light-probe-test");
    CHECK(!registry.sample(glm::vec3(0.f), 4).ok());
}

TEST_CASE("graphics.lightProbe.proxyVolumeSelectsBracketingGridCell") {
    std::vector<ProcgenProbeDesc> probes;
    std::uint64_t                 id = 100;
    for (float z : {-1.f, 1.f})
        for (float y : {-1.f, 1.f})
            for (float x : {-2.f, 0.f, 2.f}) {
                ProcgenProbeDesc probe;
                probe.sourcePointId = id++;
                probe.type          = 1;
                probe.x             = x;
                probe.y             = y;
                probe.z             = z;
                probe.extentX = probe.extentY = probe.extentZ = 2.f;
                probe.irradianceR                             = (x + 2.f) * 0.25f;
                probes.push_back(probe);
            }
    auto &registry = DiffuseLightProbeRegistry::instance();
    registry.replaceBatch("regular-grid-cell-test", probes);
    auto selected = registry.selectVolume(glm::vec3(0.5f, 0.25f, -0.25f));
    REQUIRE(selected.ok());
    CHECK(selected.value().trilinearCell);
    CHECK_EQ(selected.value().count, 8);
    for (int index = 0; index < selected.value().count; ++index) {
        const glm::vec3 p(selected.value().positions[static_cast<size_t>(index)]);
        CHECK((p.x == 0.f || p.x == 2.f));
        CHECK((p.y == -1.f || p.y == 1.f));
        CHECK((p.z == -1.f || p.z == 1.f));
    }
    registry.removeBatch("regular-grid-cell-test");
}

TEST_CASE("graphics.lightProbe.incompleteProxyGridUsesSparseFallback") {
    std::vector<ProcgenProbeDesc> probes;
    std::uint64_t                 id = 200;
    for (const glm::vec3 position :
         {glm::vec3(-1.f, -1.f, 0.f), glm::vec3(1.f, -1.f, 0.f), glm::vec3(-1.f, 1.f, 0.f)}) {
        ProcgenProbeDesc probe;
        probe.sourcePointId = id++;
        probe.type          = 1;
        probe.x             = position.x;
        probe.y             = position.y;
        probe.z             = position.z;
        probe.extentX = probe.extentY = probe.extentZ = 2.f;
        probes.push_back(probe);
    }
    auto &registry = DiffuseLightProbeRegistry::instance();
    registry.replaceBatch("incomplete-grid-test", probes);
    auto selected = registry.selectVolume(glm::vec3(0.f));
    REQUIRE(selected.ok());
    CHECK(!selected.value().trilinearCell);
    CHECK_EQ(selected.value().count, 3);
    registry.removeBatch("incomplete-grid-test");
}

TEST_CASE("graphics.lightProbe.customProvidedRejectsInvalidAndOwnsValue") {
    auto *renderable = Renderable3D::create();
    REQUIRE(renderable);
    CHECK(!renderable->setCustomLightProbe(-1.f, 0.f, 0.f).ok());
    CHECK(!renderable->setCustomLightProbe(std::numeric_limits<float>::infinity(), 0.f, 0.f).ok());
    REQUIRE(renderable->setCustomLightProbe(0.25f, 0.5f, 0.75f).ok());
    CHECK(renderable->meshRenderer()->customLightProbe);
    CHECK(std::abs(renderable->meshRenderer()->customLightProbeSh[0].x * 0.2820947918f - 0.25f) < 1e-5f);
    REQUIRE(renderable->setCustomLightProbeCoefficient(3, 0.5f, -0.25f, 0.1f).ok());
    CHECK(glm::vec3(renderable->meshRenderer()->customLightProbeSh[3]) == glm::vec3(0.5f, -0.25f, 0.1f));
    CHECK(!renderable->setCustomLightProbeCoefficient(9, 0.f, 0.f, 0.f).ok());
    renderable->clearCustomLightProbe();
    CHECK(!renderable->meshRenderer()->customLightProbe);
    ecs::DestroyEntity(renderable);
}
}  // namespace test_diffuse_light_probe_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_camera_factory)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
// graphics_camera_factory.cpp
namespace test_graphics_camera_factory_unit {
namespace {
template <class T>
std::size_t cameraCount(ecs::Table &table) {
    std::size_t count = 0;
    for (auto &&entity : ecs::View<T, typename T::Data>(table)) {
        (void)entity;
        ++count;
    }
    return count;
}
}  // namespace

TEST_CASE("graphics.cameraFactoryUsesCallerTable") {
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    auto            *two   = eve::graphics::Camera2D::createCamera();
    auto            *three = eve::graphics::Camera3D::createCamera();
    CHECK(two != nullptr);
    CHECK(three != nullptr);
    CHECK(two->data()->entity == two);
    CHECK(three->data()->entity == three);
    CHECK(two->data()->active);
    CHECK(three->data()->active);
    CHECK(cameraCount<eve::graphics::Camera2D>(table) == 1);
    CHECK(cameraCount<eve::graphics::Camera3D>(table) == 1);
    ecs::DestroyEntity(two);
    ecs::DestroyEntity(three);
    CHECK(cameraCount<eve::graphics::Camera2D>(table) == 0);
    CHECK(cameraCount<eve::graphics::Camera3D>(table) == 0);
}
}  // namespace test_graphics_camera_factory_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_display_output)
#include "PrivateTestScope.h"
#include <cmath>
#include "graphics/DisplayOutputEncoding.h"
#include "graphics/Graphics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_display_output.cpp
namespace test_graphics_display_output_unit {
using eve::graphics::Color;
using eve::graphics::Graphics;
using eve::graphics::display::acesToDisplayLinear;
using eve::graphics::display::ActiveColorSpace;
using eve::graphics::display::encodeHdr10;
using eve::graphics::display::linearToPq;
using eve::graphics::display::packNits;
using eve::graphics::display::sceneResolveTint;
using eve::graphics::display::uiResolveTint;
using eve::graphics::display::unpackNits;

TEST_CASE("graphics.displayOutput.packNitsRoundTrip") {
    float paper = 0.f, peak = 0.f;
    unpackNits(packNits(200.f, 1000.f), paper, peak);
    CHECK(std::abs(paper - 200.f) < 0.15f);
    CHECK(std::abs(peak - 1000.f) < 1.f);

    unpackNits(packNits(80.f, 200.f), paper, peak);
    CHECK(std::abs(paper - 80.f) < 0.15f);
    CHECK(peak >= paper);
}

TEST_CASE("graphics.displayOutput.encodeHdr10PaperWhiteNearMidCode") {
    float r = 1.f, g = 1.f, b = 1.f;
    encodeHdr10(r, g, b, 200.f, 1000.f);
    // Paper white at 200 nits is a mid PQ code, not near 0 or 1.
    CHECK(r > 0.4f);
    CHECK(r < 0.7f);
    CHECK(std::abs(r - g) < 1e-4f);
    CHECK(std::abs(g - b) < 1e-4f);
    CHECK(linearToPq(0.f) == 0.f);
    CHECK(linearToPq(1.f) > 0.9f);
}

TEST_CASE("graphics.displayOutput.acesPreservesHdrHeadroom") {
    float r = 8.f, g = 8.f, b = 8.f;
    acesToDisplayLinear(r, g, b, 200.f, 1000.f);
    CHECK(r > 1.f);
    CHECK(r <= 1000.f / 200.f + 1e-3f);
}

TEST_CASE("graphics.displayOutput.resolveTintModes") {
    const Color sdr = sceneResolveTint(true, ActiveColorSpace::Sdr, false, 200.f, 1000.f);
    CHECK(sdr.r >= 0.5f);
    CHECK(std::abs(sdr.g - 0.f) < 1e-5f);
    CHECK(sdr.a >= 65536.f);

    const Color hdr10 = sceneResolveTint(true, ActiveColorSpace::Hdr10, false, 200.f, 1000.f);
    CHECK(std::abs(hdr10.g - 2.f) < 1e-5f);
    CHECK(hdr10.a < 1.f);

    const Color ui = uiResolveTint(ActiveColorSpace::ScRgb, 200.f, 1000.f);
    CHECK(ui.r < 0.f);
    CHECK(std::abs(ui.g - 1.f) < 1e-5f);
}

TEST_CASE("graphics.displayOutput.encodeScRgbScalesByPaperWhite") {
    using eve::graphics::display::encodeScRgb;
    float r = 1.f, g = 1.f, b = 1.f;
    encodeScRgb(r, g, b, 200.f, 1000.f);
    // Paper white must map to 200/80 = 2.5 in scRGB (1.0 = 80 nits).
    CHECK(std::abs(r - 2.5f) < 1e-4f);
    CHECK(std::abs(g - 2.5f) < 1e-4f);
    CHECK(std::abs(b - 2.5f) < 1e-4f);

    r = g = b = 5.f;  // peakRatio = 1000/200 = 5
    encodeScRgb(r, g, b, 200.f, 1000.f);
    CHECK(std::abs(r - 12.5f) < 1e-3f);
}

TEST_CASE("graphics.displayOutput.composeTintUsesMode3") {
    using eve::graphics::display::isComposeLinearMode;
    using eve::graphics::display::sceneComposeTint;
    const Color compose = sceneComposeTint(true, 200.f, 1000.f);
    CHECK(compose.r >= 0.5f);
    CHECK(isComposeLinearMode(compose.g));
    CHECK(compose.a < 1.f);
}
}  // namespace test_graphics_display_output_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_pbr_surface)
#include "PrivateTestScope.h"
#include <array>
#include <limits>
#include <memory>
#include <vector>
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/PbrSurface.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_pbr_surface.cpp
namespace test_graphics_pbr_surface_unit {
using namespace eve::graphics;


TEST_CASE("graphics.shaderCustomMeshTexturesValidateSlotsAndPreserveBorrows") {
    Shader   shader;
    Texture *marker = reinterpret_cast<Texture *>(uintptr_t(16));
    REQUIRE(shader.setMeshTexture(3, marker).ok());
    REQUIRE_EQ(shader.meshTexture(3), marker);
    REQUIRE(!shader.setMeshTexture(4, nullptr).ok());
    REQUIRE_EQ(shader.meshTexture(3), marker);
    REQUIRE_EQ(shader.meshTexture(4), nullptr);
}
}  // namespace test_graphics_pbr_surface_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_primitives)
#include "PrivateTestScope.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "graphics/Batcher.h"
#include "graphics/PrimitiveDrawList.h"
#include "graphics/PrimitivePath.h"
#include "graphics/PrimitiveScene.h"
#include "graphics/PrimitiveTessellator.h"
#include "graphics/PrimitiveTypes.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_primitives.cpp
namespace test_graphics_primitives {
using namespace eve::graphics;

TEST_CASE("GraphicsPrimitives.dashPatternOwnsAndValidatesIntervals") {
    DashPattern dash{{4.f, 2.f, 1.f, 3.f}, 0.5f, DashSpace::WorldUnits};
    CHECK(std::fabs(dash.period() - 10.f) < 1e-5f);

    dash.intervals = {1.f, 2.f, 3.f};
    CHECK_THROWS((dash.validate(), false));
    dash.intervals = {1.f, 0.f};
    CHECK_THROWS((dash.validate(), false));
}

TEST_CASE("GraphicsPrimitives.canvas2DRecordsContinuousPolylineDistance") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    paint;
    paint.mode        = PaintMode::Stroke;
    paint.stroke.dash = DashPattern{{3.f, 2.f}, 0.f, DashSpace::ScreenPixels};
    std::array points{glm::vec2{0.f, 0.f}, glm::vec2{3.f, 4.f}, glm::vec2{6.f, 4.f}};

    canvas.drawPolyline(points, false, paint);

    REQUIRE_EQ(canvas.commands().size(), 1u);
    const auto &command = canvas.commands().front();
    REQUIRE_EQ(command.cumulativeLengths.size(), 3u);
    CHECK(std::fabs(command.cumulativeLengths[1] - 5.f) < 1e-5f);
    CHECK(std::fabs(command.cumulativeLengths[2] - 8.f) < 1e-5f);
    CHECK_EQ(canvas.statistics().segmentCount, 2u);
}

TEST_CASE("GraphicsPrimitives.closed3DPolylineIncludesClosingDistance") {
    SceneDrawContext context;
    context.viewportSize = {1280, 720};
    PrimitiveSceneCanvas3D canvas(context);
    ScenePrimitivePaint    paint;
    paint.mode = PaintMode::Stroke;
    std::array points{glm::vec3{0.f, 0.f, 0.f}, glm::vec3{3.f, 0.f, 0.f}, glm::vec3{3.f, 4.f, 0.f}};

    canvas.drawPolyline(points, true, paint);

    const auto &command = canvas.commands().front();
    REQUIRE_EQ(command.cumulativeWorldLengths.size(), 4u);
    CHECK(std::fabs(command.cumulativeWorldLengths.back() - 12.f) < 1e-5f);
    CHECK_EQ(canvas.statistics().segmentCount, 3u);
}

TEST_CASE("GraphicsPrimitives.canvasStateSnapshotsTransforms") {
    SceneDrawContext context;
    context.viewportSize = {640, 480};
    PrimitiveSceneCanvas3D canvas(context);
    ScenePrimitivePaint    paint;

    canvas.save();
    canvas.concat(glm::translate(glm::mat4(1.f), glm::vec3(2.f, 3.f, 4.f)));
    canvas.drawLine({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, paint);
    canvas.restore();
    canvas.drawLine({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, paint);

    REQUIRE_EQ(canvas.commands().size(), 2u);
    CHECK(std::fabs(canvas.commands()[0].transform[3].x - 2.f) < 1e-5f);
    CHECK(std::fabs(canvas.commands()[1].transform[3].x) < 1e-5f);
}

TEST_CASE("GraphicsPrimitives.rejectsInvalidContextAndCapacity") {
    SceneDrawContext context;
    context.viewportSize = {0, 720};
    CHECK_THROWS((PrimitiveSceneCanvas3D(context), false));

    PrimitiveCanvas2D canvas(1);
    PrimitivePaint    paint;
    canvas.drawLine({0.f, 0.f}, {1.f, 1.f}, paint);
    CHECK_THROWS((canvas.drawLine({1.f, 1.f}, {2.f, 2.f}, paint), false));
}

TEST_CASE("GraphicsPrimitives.explicitRecordingReportsBudgetWithoutPartialMutation") {
    PrimitivePaint paint;
    paint.mode = PaintMode::Stroke;
    PrimitiveCanvas2D canvas(1);
    const std::array  first{glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}};
    auto              recorded = canvas.tryDrawPolyline(first, false, paint);
    REQUIRE(recorded.ok());
    const std::array second{glm::vec2{0.f, 1.f}, glm::vec2{1.f, 1.f}};
    auto             rejected = canvas.tryDrawPolyline(second, false, paint);
    CHECK(!rejected.ok());
    REQUIRE(rejected.error() != nullptr);
    CHECK_EQ(static_cast<int>(rejected.error()->code()), static_cast<int>(eve::DiagnosticCode::Failed));
    CHECK_EQ(canvas.commands().size(), 1u);
    CHECK_EQ(canvas.statistics().droppedCommands, 1u);

    SceneDrawContext context;
    context.viewportSize = {640, 480};
    PrimitiveSceneCanvas3D sceneCanvas(context, 1);
    ScenePrimitivePaint    scenePaint;
    scenePaint.mode = PaintMode::Stroke;
    const std::array line{glm::vec3{0.f, 0.f, -2.f}, glm::vec3{1.f, 0.f, -2.f}};
    auto             sceneRecorded = sceneCanvas.tryDrawPolyline(line, false, scenePaint);
    REQUIRE(sceneRecorded.ok());
    auto sceneRejected = sceneCanvas.tryDrawPolyline(line, false, scenePaint);
    CHECK(!sceneRejected.ok());
    CHECK_EQ(sceneCanvas.commands().size(), 1u);
    CHECK_EQ(sceneCanvas.statistics().droppedCommands, 1u);
}

TEST_CASE("GraphicsPrimitives.pathFlattensBezierAndPreservesClosedContour") {
    Path2D path;
    path.moveTo({0.f, 0.f}).quadTo({5.f, 10.f}, {10.f, 0.f}).cubicTo({12.f, -4.f}, {18.f, -4.f}, {20.f, 0.f}).close();

    const auto coarse = path.flatten(2.f);
    const auto fine   = path.flatten(0.05f);
    REQUIRE_EQ(coarse.size(), 1u);
    CHECK(coarse.front().closed);
    CHECK(fine.front().points.size() > coarse.front().points.size());
    CHECK(std::fabs(fine.front().points.front().x) < 1e-5f);
    CHECK(std::fabs(fine.front().points.back().x - 20.f) < 1e-5f);
}

TEST_CASE("GraphicsPrimitives.canvasPathUsesSharedContinuousDistanceModel") {
    Path2D path;
    path.moveTo({0.f, 0.f}).lineTo({4.f, 0.f}).lineTo({4.f, 3.f}).close();
    PrimitiveCanvas2D canvas;
    PrimitivePaint    paint;
    paint.mode = PaintMode::Stroke;
    canvas.drawPath(path, paint);

    REQUIRE_EQ(canvas.commands().size(), 1u);
    CHECK(canvas.commands().front().closed);
    CHECK(std::fabs(canvas.commands().front().cumulativeLengths.back() - 12.f) < 1e-5f);
}

TEST_CASE("GraphicsPrimitives.concaveClosedPathProducesEarClippedFill") {
    Path2D path;
    path.moveTo({0.f, 0.f}).lineTo({8.f, 0.f}).lineTo({8.f, 8.f}).lineTo({4.f, 3.f}).lineTo({0.f, 8.f}).close();
    PrimitivePaint paint;
    paint.mode      = PaintMode::FillAndStroke;
    paint.antialias = false;
    PrimitiveCanvas2D canvas;
    canvas.drawPath(path, paint);
    CHECK_EQ(canvas.triangles().size(), 3u);
    REQUIRE_EQ(canvas.commands().size(), 1u);
    CHECK(canvas.commands().front().closed);
}

TEST_CASE("GraphicsPrimitives.pathFillRulesDistinguishNestedContourWinding") {
    const auto appendSquare = [](Path2D &path, float minimum, float maximum, bool clockwise) {
        if (clockwise)
            path.moveTo({minimum, minimum})
                .lineTo({minimum, maximum})
                .lineTo({maximum, maximum})
                .lineTo({maximum, minimum})
                .close();
        else
            path.moveTo({minimum, minimum})
                .lineTo({maximum, minimum})
                .lineTo({maximum, maximum})
                .lineTo({minimum, maximum})
                .close();
    };
    const auto triangleArea = [](const PrimitiveCanvas2D &canvas) {
        float area = 0.f;
        for (const TriangleCommand2D &triangle : canvas.triangles()) {
            const glm::vec2 ab = triangle.points[1] - triangle.points[0];
            const glm::vec2 ac = triangle.points[2] - triangle.points[0];
            area += std::fabs(ab.x * ac.y - ab.y * ac.x) * 0.5f;
        }
        return area;
    };
    PrimitivePaint fill;
    fill.mode      = PaintMode::Fill;
    fill.antialias = false;

    Path2D evenOdd;
    appendSquare(evenOdd, 0.f, 10.f, false);
    appendSquare(evenOdd, 3.f, 7.f, false);
    evenOdd.setFillRule(PathFillRule::EvenOdd);
    PrimitiveCanvas2D evenOddCanvas;
    evenOddCanvas.drawPath(evenOdd, fill);
    CHECK(std::fabs(triangleArea(evenOddCanvas) - 84.f) < 1e-4f);

    Path2D nonZeroSolid;
    appendSquare(nonZeroSolid, 0.f, 10.f, false);
    appendSquare(nonZeroSolid, 3.f, 7.f, false);
    nonZeroSolid.setFillRule(PathFillRule::NonZero);
    PrimitiveCanvas2D solidCanvas;
    solidCanvas.drawPath(nonZeroSolid, fill);
    CHECK(std::fabs(triangleArea(solidCanvas) - 100.f) < 1e-4f);

    Path2D nonZeroHole;
    appendSquare(nonZeroHole, 0.f, 10.f, false);
    appendSquare(nonZeroHole, 3.f, 7.f, true);
    nonZeroHole.setFillRule(PathFillRule::NonZero);
    PrimitiveCanvas2D holeCanvas;
    holeCanvas.drawPath(nonZeroHole, fill);
    CHECK(std::fabs(triangleArea(holeCanvas) - 84.f) < 1e-4f);
}

TEST_CASE("GraphicsPrimitives.pathRejectsVerbWithoutContour") {
    Path2D path;
    CHECK_THROWS((path.lineTo({1.f, 2.f}), false));
    CHECK_THROWS((path.close(), false));
}

TEST_CASE("GraphicsPrimitives.spatialWireShapesSharePolylineCommands") {
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    PrimitiveSceneCanvas3D canvas(context);
    ScenePrimitivePaint    paint;
    paint.mode        = PaintMode::Stroke;
    paint.stroke.dash = DashPattern{{0.5f, 0.25f}, 0.f, DashSpace::WorldUnits};

    canvas.drawAabb({-1.f, -2.f, -3.f}, {1.f, 2.f, 3.f}, paint);
    CHECK_EQ(canvas.commands().size(), 12u);
    canvas.drawSphere({0.f, 0.f, 0.f}, 2.f, paint, 8);
    CHECK_EQ(canvas.commands().size(), 15u);
    CHECK_EQ(canvas.statistics().segmentCount, 36u);

    for (const auto &command : canvas.commands()) {
        CHECK(command.paint.stroke.dash.has_value());
    }
}

TEST_CASE("GraphicsPrimitives.gridCylinderConeAndArrowProduceFiniteCommands") {
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    PrimitiveSceneCanvas3D canvas(context);
    ScenePrimitivePaint    paint;
    paint.mode = PaintMode::Stroke;

    canvas.drawGrid({0.f, 0.f, 0.f}, {4.f, 0.f, 0.f}, {0.f, 0.f, 3.f}, 4, 3, paint);
    canvas.drawCylinder({0.f, 0.f, 0.f}, {0.f, 2.f, 0.f}, 0.5f, paint, 8);
    canvas.drawCone({0.f, 2.f, 0.f}, {0.f, -1.f, 0.f}, 1.f, 0.5f, paint, 8);
    canvas.drawArrow({0.f, 0.f, 0.f}, {2.f, 0.f, 0.f}, 0.5f, 0.2f, paint);

    CHECK_EQ(canvas.statistics().droppedCommands, 0u);
    CHECK(canvas.statistics().commandCount > 0u);
    for (const auto &command : canvas.commands()) {
        CHECK(std::isfinite(command.cumulativeWorldLengths.back()));
    }
}

TEST_CASE("GraphicsPrimitives.resolve2DStrokeProducesPixelWidthQuad") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    paint;
    paint.mode         = PaintMode::Stroke;
    paint.stroke.width = 4.f;
    paint.antialias    = false;
    canvas.drawLine({10.f, 20.f}, {30.f, 20.f}, paint);

    const auto triangles = resolvePrimitiveStrokes2D(canvas, {100, 100});
    REQUIRE_EQ(triangles.vertices.size(), 6u);
    CHECK_EQ(triangles.statistics.triangleCount, 2u);
    const float firstY    = triangles.vertices[0].clipPosition.y;
    const float oppositeY = triangles.vertices[2].clipPosition.y;
    CHECK(std::fabs(std::fabs(oppositeY - firstY) - 0.08f) < 1e-5f);
    CHECK_EQ(triangles.statistics.uploadBytes, triangles.vertices.size() * sizeof(PrimitiveTriangleVertex));
}

TEST_CASE("GraphicsPrimitives.coverageAaEmitsTransparentOnePixelFringe") {
    PrimitivePaint paint;
    paint.mode         = PaintMode::Stroke;
    paint.stroke.width = 4.f;
    paint.antialias    = true;
    PrimitiveCanvas2D canvas;
    canvas.drawLine({10.f, 20.f}, {30.f, 20.f}, paint);
    const auto resolved = resolvePrimitiveStrokes2D(canvas, {100, 100});
    CHECK_EQ(resolved.vertices.size(), 18u);
    CHECK(std::any_of(resolved.vertices.begin(), resolved.vertices.end(),
                      [](const PrimitiveTriangleVertex &vertex) { return vertex.color.a == 0.f; }));
    CHECK(std::any_of(resolved.vertices.begin(), resolved.vertices.end(),
                      [](const PrimitiveTriangleVertex &vertex) { return vertex.color.a == 1.f; }));

    Batcher batch;
    batch.addTriangle({0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}, Color(1.f), Color(1.f, 1.f, 1.f, 0.5f),
                      Color(1.f, 1.f, 1.f, 0.f));
    REQUIRE_EQ(batch.vertices().size(), 3u);
    CHECK_EQ(batch.vertices()[1].color.a, 0.5f);
    CHECK_EQ(batch.vertices()[2].color.a, 0.f);
}

TEST_CASE("GraphicsPrimitives.filledContourCoverageAaUsesVertexAlphaFringe") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    fill;
    fill.mode      = PaintMode::Fill;
    fill.antialias = true;
    canvas.drawRect({10.f, 10.f}, {30.f, 30.f}, fill);
    CHECK_EQ(canvas.triangles().size(), 10u);
    const auto resolved = resolvePrimitiveStrokes2D(canvas, {100, 100});
    CHECK(std::any_of(resolved.vertices.begin(), resolved.vertices.end(),
                      [](const PrimitiveTriangleVertex &vertex) { return vertex.color.a == 0.f; }));
    CHECK(std::any_of(resolved.vertices.begin(), resolved.vertices.end(),
                      [](const PrimitiveTriangleVertex &vertex) { return vertex.color.a == 1.f; }));
}

TEST_CASE("GraphicsPrimitives.resolve2DPreservesBlendAndSubmissionOrder") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    opaque;
    opaque.mode      = PaintMode::Stroke;
    opaque.blend     = BlendMode::Opaque;
    opaque.antialias = false;
    canvas.drawLine({0.f, 0.f}, {10.f, 0.f}, opaque);
    PrimitivePaint additive = opaque;
    additive.blend          = BlendMode::Additive;
    canvas.drawLine({0.f, 2.f}, {10.f, 2.f}, additive);
    const auto resolved = resolvePrimitiveStrokes2D(canvas, {100, 100});
    REQUIRE_EQ(resolved.batches2D.size(), 2u);
    CHECK_EQ(static_cast<int>(resolved.batches2D[0].blend), static_cast<int>(BlendMode::Opaque));
    CHECK_EQ(static_cast<int>(resolved.batches2D[1].blend), static_cast<int>(BlendMode::Additive));
    CHECK_EQ(resolved.batches2D[0].sequence, 0u);
    CHECK_EQ(resolved.batches2D[1].sequence, 1u);
}

TEST_CASE("GraphicsPrimitives.resolve2DDashContinuesAcrossPolylineSegments") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    paint;
    paint.mode         = PaintMode::Stroke;
    paint.stroke.width = 2.f;
    paint.antialias    = false;
    paint.stroke.dash  = DashPattern{{6.f, 4.f}, 0.f, DashSpace::ScreenPixels};
    const std::array points{glm::vec2{0.f, 10.f}, glm::vec2{5.f, 10.f}, glm::vec2{10.f, 10.f}};
    canvas.drawPolyline(points, false, paint);

    const auto triangles = resolvePrimitiveStrokes2D(canvas, {100, 100});
    REQUIRE_EQ(triangles.vertices.size(), 12u);
    float maximumDistance = 0.f;
    for (const auto &vertex : triangles.vertices) {
        maximumDistance = std::max(maximumDistance, vertex.pathDistance);
    }
    CHECK(std::fabs(maximumDistance - 6.f) < 1e-5f);
}

TEST_CASE("GraphicsPrimitives.resolve2DDashPhaseSelectsGapAtStart") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    paint;
    paint.mode        = PaintMode::Stroke;
    paint.stroke.dash = DashPattern{{4.f, 4.f}, 4.f, DashSpace::ScreenPixels};
    paint.antialias   = false;
    canvas.drawLine({0.f, 0.f}, {8.f, 0.f}, paint);

    const auto triangles = resolvePrimitiveStrokes2D(canvas, {100, 100});
    REQUIRE_EQ(triangles.vertices.size(), 6u);
    CHECK(std::fabs(triangles.vertices.front().pathDistance - 4.f) < 1e-5f);
    CHECK(std::fabs(triangles.vertices[1].pathDistance - 8.f) < 1e-5f);
}

TEST_CASE("GraphicsPrimitives.rectangleHonorsFillStrokeAndCombinedModes") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    paint;
    paint.mode      = PaintMode::Fill;
    paint.antialias = false;
    canvas.drawRect({10.f, 20.f}, {30.f, 40.f}, paint);
    CHECK_EQ(canvas.triangles().size(), 2u);
    CHECK(canvas.commands().empty());

    canvas.reset();
    paint.mode = PaintMode::Stroke;
    canvas.drawRect({10.f, 20.f}, {30.f, 40.f}, paint);
    CHECK(canvas.triangles().empty());
    REQUIRE_EQ(canvas.commands().size(), 1u);
    CHECK(canvas.commands().front().closed);

    canvas.reset();
    paint.mode = PaintMode::FillAndStroke;
    canvas.drawRect({10.f, 20.f}, {30.f, 40.f}, paint);
    CHECK_EQ(canvas.triangles().size(), 2u);
    CHECK_EQ(canvas.commands().size(), 1u);
}

TEST_CASE("GraphicsPrimitives.circleEllipseAndArcGenerateRealFillTriangles") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    fill;
    fill.mode      = PaintMode::Fill;
    fill.antialias = false;
    canvas.drawCircle({50.f, 50.f}, 20.f, fill, 8);
    CHECK_EQ(canvas.triangles().size(), 8u);
    CHECK(canvas.commands().empty());

    PrimitivePaint combined;
    combined.mode      = PaintMode::FillAndStroke;
    combined.antialias = false;
    canvas.drawEllipse({20.f, 20.f}, {10.f, 5.f}, combined, 8);
    CHECK_EQ(canvas.triangles().size(), 16u);
    CHECK_EQ(canvas.commands().size(), 1u);

    canvas.drawArc({20.f, 20.f}, {8.f, 8.f}, 0.f, glm::pi<float>(), combined, 4);
    CHECK_EQ(canvas.triangles().size(), 20u);
    CHECK_EQ(canvas.commands().size(), 2u);
}

TEST_CASE("GraphicsPrimitives.unitCircleGeometryCacheReportsReuse") {
    PrimitivePaint fill;
    fill.mode = PaintMode::Fill;
    PrimitiveCanvas2D canvas;
    canvas.drawCircle({0.f, 0.f}, 1.f, fill, 37);
    const std::size_t firstHits = canvas.statistics().cacheHits;
    canvas.drawEllipse({2.f, 0.f}, {2.f, 1.f}, fill, 37);
    CHECK_EQ(canvas.statistics().cacheHits, firstHits + 1u);

    SceneDrawContext context;
    context.viewportSize = {640, 480};
    PrimitiveSceneCanvas3D sceneCanvas(context);
    ScenePrimitivePaint    stroke;
    stroke.mode = PaintMode::Stroke;
    sceneCanvas.drawSphere({0.f, 0.f, -4.f}, 1.f, stroke, 41);
    CHECK(sceneCanvas.statistics().cacheHits >= 2u);
}

TEST_CASE("GraphicsPrimitives.radialQualityAndAdaptiveLodResolveDeterministically") {
    CHECK_EQ(resolveRadialSegments({PrimitiveQuality::Low, 0.75f, 0}, 100.f), 16u);
    CHECK_EQ(resolveRadialSegments({PrimitiveQuality::Medium, 0.75f, 0}, 100.f), 32u);
    CHECK_EQ(resolveRadialSegments({PrimitiveQuality::High, 0.75f, 0}, 100.f), 64u);
    CHECK_EQ(resolveRadialSegments({PrimitiveQuality::Adaptive, 0.75f, 23}, 100.f), 23u);

    SceneDrawContext context;
    context.viewportSize = {800, 600};
    context.projection   = glm::perspective(glm::radians(60.f), 4.f / 3.f, 0.1f, 100.f);
    ScenePrimitivePaint stroke;
    stroke.mode = PaintMode::Stroke;
    const RadialTessellation adaptive{PrimitiveQuality::Adaptive, 0.5f, 0};
    PrimitiveSceneCanvas3D   nearCanvas(context);
    nearCanvas.drawSphere({0.f, 0.f, -2.f}, 1.f, stroke, adaptive);
    PrimitiveSceneCanvas3D farCanvas(context);
    farCanvas.drawSphere({0.f, 0.f, -20.f}, 1.f, stroke, adaptive);
    REQUIRE(!nearCanvas.commands().empty());
    REQUIRE(!farCanvas.commands().empty());
    CHECK(nearCanvas.commands().front().points.size() > farCanvas.commands().front().points.size());
}

TEST_CASE("GraphicsPrimitives.resolve2DCombinesFillAndStrokeTriangles") {
    PrimitiveCanvas2D canvas;
    PrimitivePaint    paint;
    paint.mode         = PaintMode::FillAndStroke;
    paint.stroke.width = 2.f;
    paint.antialias    = false;
    canvas.drawRect({10.f, 10.f}, {30.f, 30.f}, paint);

    const auto resolved = resolvePrimitiveStrokes2D(canvas, {100, 100});
    CHECK_EQ(resolved.statistics.triangleCount, 14u);
    CHECK_EQ(resolved.vertices.size(), 42u);
}

TEST_CASE("GraphicsPrimitives.strokeCapsProduceDistinctEndpointGeometry") {
    PrimitivePaint paint;
    paint.mode         = PaintMode::Stroke;
    paint.stroke.width = 4.f;
    paint.antialias    = false;

    PrimitiveCanvas2D butt;
    paint.stroke.cap = LineCap::Butt;
    butt.drawLine({10.f, 20.f}, {30.f, 20.f}, paint);
    CHECK_EQ(resolvePrimitiveStrokes2D(butt, {100, 100}).vertices.size(), 6u);

    PrimitiveCanvas2D square;
    paint.stroke.cap = LineCap::Square;
    square.drawLine({10.f, 20.f}, {30.f, 20.f}, paint);
    CHECK_EQ(resolvePrimitiveStrokes2D(square, {100, 100}).vertices.size(), 18u);

    PrimitiveCanvas2D round;
    paint.stroke.cap = LineCap::Round;
    round.drawLine({10.f, 20.f}, {30.f, 20.f}, paint);
    CHECK_EQ(resolvePrimitiveStrokes2D(round, {100, 100}).vertices.size(), 54u);
}

TEST_CASE("GraphicsPrimitives.strokeJoinsFillPolylineOuterCorner") {
    const std::array points{glm::vec2{10.f, 10.f}, glm::vec2{30.f, 10.f}, glm::vec2{30.f, 30.f}};
    PrimitivePaint   paint;
    paint.mode         = PaintMode::Stroke;
    paint.stroke.width = 6.f;
    paint.antialias    = false;
    paint.stroke.join  = LineJoin::Bevel;
    PrimitiveCanvas2D bevel;
    bevel.drawPolyline(points, false, paint);
    CHECK_EQ(resolvePrimitiveStrokes2D(bevel, {100, 100}).vertices.size(), 15u);

    paint.stroke.join = LineJoin::Round;
    PrimitiveCanvas2D round;
    round.drawPolyline(points, false, paint);
    CHECK(resolvePrimitiveStrokes2D(round, {100, 100}).vertices.size() > 15u);
}

TEST_CASE("GraphicsPrimitives.resolve3DScreenStrokeClipsNearPlane") {
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    context.nearPlane    = 0.1f;
    context.farPlane     = 100.f;
    context.projection   = glm::perspective(glm::radians(60.f), 4.f / 3.f, 0.1f, 100.f);
    PrimitiveSceneCanvas3D canvas(context);
    ScenePrimitivePaint    paint;
    paint.mode              = PaintMode::Stroke;
    paint.stroke.width      = 3.f;
    paint.antialias         = false;
    paint.stroke.widthSpace = WidthSpace::ScreenPixels;
    canvas.drawLine({-1.f, 0.f, -1.f}, {1.f, 0.f, 0.f}, paint);
    canvas.drawLine({-1.f, 1.f, 1.f}, {1.f, 1.f, 2.f}, paint);

    const auto triangles = resolvePrimitiveStrokes3D(canvas);
    REQUIRE_EQ(triangles.vertices.size(), 6u);
    for (const auto &vertex : triangles.vertices) {
        CHECK(std::isfinite(vertex.clipPosition.x));
        CHECK(std::isfinite(vertex.clipPosition.w));
        CHECK(vertex.clipPosition.w > 0.f);
    }
}

TEST_CASE("GraphicsPrimitives.resolve3DWorldStrokeExpandsInViewSpace") {
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    context.projection   = glm::perspective(glm::radians(60.f), 4.f / 3.f, 0.1f, 100.f);
    PrimitiveSceneCanvas3D canvas(context);
    ScenePrimitivePaint    paint;
    paint.mode              = PaintMode::Stroke;
    paint.stroke.width      = 0.2f;
    paint.antialias         = false;
    paint.stroke.widthSpace = WidthSpace::WorldUnits;
    canvas.drawLine({-1.f, 0.f, -4.f}, {1.f, 0.f, -4.f}, paint);

    const auto triangles = resolvePrimitiveStrokes3D(canvas);
    REQUIRE_EQ(triangles.vertices.size(), 6u);
    CHECK(std::fabs(triangles.vertices[0].clipPosition.y - triangles.vertices[2].clipPosition.y) > 1e-5f);
}

TEST_CASE("GraphicsPrimitives.resolve3DStrokeHonorsCapsAndJoins") {
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    context.projection   = glm::perspective(glm::radians(60.f), 4.f / 3.f, 0.1f, 100.f);
    ScenePrimitivePaint paint;
    paint.mode              = PaintMode::Stroke;
    paint.stroke.width      = 4.f;
    paint.stroke.widthSpace = WidthSpace::ScreenPixels;
    paint.antialias         = false;

    PrimitiveSceneCanvas3D square(context);
    paint.stroke.cap = LineCap::Square;
    square.drawLine({-1.f, 0.f, -4.f}, {1.f, 0.f, -4.f}, paint);
    CHECK_EQ(resolvePrimitiveStrokes3D(square).vertices.size(), 18u);

    PrimitiveSceneCanvas3D round(context);
    paint.stroke.cap = LineCap::Round;
    round.drawLine({-1.f, 0.f, -4.f}, {1.f, 0.f, -4.f}, paint);
    CHECK_EQ(resolvePrimitiveStrokes3D(round).vertices.size(), 54u);

    PrimitiveSceneCanvas3D bevel(context);
    paint.stroke.cap  = LineCap::Butt;
    paint.stroke.join = LineJoin::Bevel;
    const std::array points{glm::vec3{-1.f, 0.f, -4.f}, glm::vec3{0.f, 0.f, -4.f}, glm::vec3{0.f, 1.f, -4.f}};
    bevel.drawPolyline(points, false, paint);
    CHECK_EQ(resolvePrimitiveStrokes3D(bevel).vertices.size(), 15u);
}

TEST_CASE("GraphicsPrimitives.resolve3DScreenDashUsesProjectedPixelDistance") {
    SceneDrawContext context;
    context.viewportSize = {800, 600};
    context.projection   = glm::perspective(glm::radians(60.f), 4.f / 3.f, 0.1f, 100.f);
    PrimitiveSceneCanvas3D canvas(context);
    ScenePrimitivePaint    paint;
    paint.mode         = PaintMode::Stroke;
    paint.stroke.width = 2.f;
    paint.stroke.dash  = DashPattern{{10.f, 10.f}, 0.f, DashSpace::ScreenPixels};
    canvas.drawLine({-1.f, 0.f, -4.f}, {1.f, 0.f, -4.f}, paint);

    const auto triangles = resolvePrimitiveStrokes3D(canvas);
    CHECK(triangles.vertices.size() > 6u);
    CHECK_EQ(triangles.vertices.size() % 6u, 0u);
}

TEST_CASE("GraphicsPrimitives.batcherAcceptsResolvedArbitraryTriangles") {
    Batcher batch;
    batch.addTriangle({1.f, 2.f}, {7.f, 3.f}, {2.f, 9.f}, Color(1.f, 0.f, 0.f, 1.f));
    REQUIRE_EQ(batch.vertices().size(), 3u);
    CHECK(std::fabs(batch.vertices()[1].pos.x - 7.f) < 1e-5f);
    batch.toNDC(10, 10);
    CHECK(std::fabs(batch.vertices()[0].pos.x - (-0.8f)) < 1e-5f);
}

TEST_CASE("GraphicsPrimitives.persistentSceneDetectsStaleAndReusedSlots") {
    PrimitiveScene scene;
    CHECK(scene.isStale({}));
    PrimitiveDescriptor3D descriptor;
    descriptor.geometry = PrimitiveSphere3D{{0.f, 0.f, 0.f}, 1.f, 8};
    auto added          = scene.add(descriptor);
    REQUIRE(added.ok());
    const PrimitiveHandle first = added.value();
    CHECK(scene.tryGet(first) != nullptr);

    auto removed = scene.remove(first);
    REQUIRE(removed.ok());
    CHECK(scene.isStale(first));
    auto addedAgain = scene.add(descriptor);
    REQUIRE(addedAgain.ok());
    const PrimitiveHandle second = addedAgain.value();
    CHECK_EQ(second.index(), first.index());
    CHECK(second.generation() != first.generation());

    auto staleUpdate = scene.update(first, descriptor);
    CHECK(!staleUpdate.ok());
    CHECK_EQ(static_cast<int>(staleUpdate.error()->code()), static_cast<int>(eve::DiagnosticCode::StaleHandle));
}

TEST_CASE("GraphicsPrimitives.persistentSceneRejectsInvalidSpatialDescriptorsBeforeRender") {
    PrimitiveScene                           scene;
    const std::array<PrimitiveGeometry3D, 8> invalidGeometries{{
        PrimitiveAabb3D{{1.f, 0.f, 0.f}, {0.f, 1.f, 1.f}},
        PrimitiveObb3D{{0.f, 0.f, 0.f}, {{{1.f, 0.f, 0.f}, {2.f, 0.f, 0.f}, {0.f, 0.f, 1.f}}}},
        PrimitiveDisk3D{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, 1.f, 8},
        PrimitiveArc3D{{0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 2.f, 0.f}, 1.f, 0.f, 1.f, 8},
        PrimitiveCylinder3D{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, 1.f, 8},
        PrimitiveCone3D{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, 1.f, 1.f, 8},
        PrimitiveGrid3D{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {2.f, 0.f, 0.f}, 2, 2},
        PrimitiveArrow3D{{0.f, 0.f, 0.f}, {0.f, 0.f, 0.f}, 0.2f, 0.1f},
    }};

    for (const PrimitiveGeometry3D &geometry : invalidGeometries) {
        PrimitiveDescriptor3D descriptor;
        descriptor.geometry = geometry;
        auto added          = scene.add(std::move(descriptor));
        CHECK(!added.ok());
    }
    CHECK_EQ(scene.size(), 0u);
}

TEST_CASE("GraphicsPrimitives.sceneRejectsInvalidPaintWithoutMutation") {
    PrimitiveScene        scene;
    PrimitiveDescriptor3D descriptor;
    descriptor.geometry = PrimitiveSphere3D{};
    auto added          = scene.add(descriptor);
    REQUIRE(added.ok());
    const auto handle             = added.value();
    descriptor.paint.stroke.width = -1.f;
    auto badWidth                 = scene.update(handle, descriptor);
    CHECK(!badWidth.ok());
    REQUIRE(scene.tryGet(handle) != nullptr);
    CHECK_EQ(scene.tryGet(handle)->paint.stroke.width, 1.f);
    descriptor.paint.stroke.width = 1.f;
    descriptor.paint.stroke.dash  = DashPattern{{1.f, 0.f}, 0.f, DashSpace::WorldUnits};
    auto badDash                  = scene.update(handle, descriptor);
    CHECK(!badDash.ok());
    CHECK(!scene.tryGet(handle)->paint.stroke.dash.has_value());
}

TEST_CASE("GraphicsPrimitives.persistentSceneProjectsVisibleDescriptorsInSlotOrder") {
    PrimitiveScene        scene;
    PrimitiveDescriptor3D hidden;
    hidden.geometry   = PrimitiveAabb3D{glm::vec3(-1.f), glm::vec3(1.f)};
    hidden.visible    = false;
    auto hiddenHandle = scene.add(hidden);
    REQUIRE(hiddenHandle.ok());

    PrimitiveDescriptor3D arrow;
    arrow.geometry   = PrimitiveArrow3D{{0.f, 0.f, 0.f}, {0.f, 2.f, 0.f}, 0.4f, 0.2f};
    arrow.transform  = glm::translate(glm::mat4(1.f), {4.f, 0.f, 0.f});
    auto arrowHandle = scene.add(arrow);
    REQUIRE(arrowHandle.ok());

    SceneDrawContext context;
    context.viewportSize = {800, 600};
    PrimitiveSceneCanvas3D canvas(context);
    scene.render(canvas);
    CHECK(!canvas.commands().empty());
    for (const auto &command : canvas.commands()) {
        CHECK(std::fabs(command.transform[3].x - 4.f) < 1e-5f);
    }

    scene.clear();
    CHECK_EQ(scene.size(), 0u);
    CHECK(scene.isStale(arrowHandle.value()));
}

TEST_CASE("GraphicsPrimitives.persistentHandlesAreQualifiedByOwningScene") {
    PrimitiveScene        firstScene;
    PrimitiveScene        secondScene;
    PrimitiveDescriptor3D descriptor;
    descriptor.geometry = PrimitiveSphere3D{};
    auto first          = firstScene.add(descriptor);
    auto second         = secondScene.add(descriptor);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(first.value().owner() != second.value().owner());
    CHECK(secondScene.tryGet(first.value()) == nullptr);
    CHECK(!secondScene.update(first.value(), descriptor).ok());
}

TEST_CASE("GraphicsPrimitives.sceneBatchUpdateIsAtomicOnStaleHandle") {
    PrimitiveScene        scene;
    PrimitiveDescriptor3D firstDescriptor;
    firstDescriptor.geometry = PrimitiveSphere3D{{1.f, 0.f, 0.f}, 1.f, 8};
    PrimitiveDescriptor3D secondDescriptor;
    secondDescriptor.geometry = PrimitiveSphere3D{{2.f, 0.f, 0.f}, 1.f, 8};
    auto first                = scene.add(firstDescriptor);
    auto second               = scene.add(secondDescriptor);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    REQUIRE(scene.remove(second.value()).ok());

    PrimitiveDescriptor3D replacement = firstDescriptor;
    replacement.visible               = false;
    const std::array updates{PrimitiveBatchUpdate{first.value(), replacement},
                             PrimitiveBatchUpdate{second.value(), replacement}};
    auto             result = scene.updateMany(updates);
    CHECK(!result.ok());
    REQUIRE(scene.tryGet(first.value()) != nullptr);
    CHECK(scene.tryGet(first.value())->visible);
}

TEST_CASE("GraphicsPrimitives.persistentSceneRejectsInvalidGeometryAtomically") {
    PrimitiveScene        scene;
    PrimitiveDescriptor3D valid;
    valid.geometry = PrimitiveSphere3D{{0.f, 0.f, 0.f}, 1.f, 8};
    auto handle    = scene.add(valid);
    REQUIRE(handle.ok());

    PrimitiveDescriptor3D invalid = valid;
    invalid.geometry              = PrimitiveSphere3D{{0.f, 0.f, 0.f}, -1.f, 8};
    auto update                   = scene.update(handle.value(), invalid);
    CHECK(!update.ok());
    REQUIRE(scene.tryGet(handle.value()) != nullptr);
    const auto *sphere = std::get_if<PrimitiveSphere3D>(&scene.tryGet(handle.value())->geometry);
    REQUIRE(sphere != nullptr);
    CHECK_EQ(sphere->radius, 1.f);

    auto add = scene.add(invalid);
    CHECK(!add.ok());
    CHECK_EQ(scene.size(), 1u);
}
}  // namespace test_graphics_primitives
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_skin_palette)
#include "PrivateTestScope.h"
#include <limits>
#include <vector>
#include "graphics/Mesh.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_skin_palette.cpp
namespace test_graphics_skin_palette {
TEST_CASE("graphics.skinPalette.dynamicOwnershipAndRollback") {
    eve::graphics::Mesh mesh;
    for (int count : {129, 204, 1024, 4096, 17}) {
        std::vector<float> matrices(size_t(count) * 16, 0.f);
        for (int i = 0; i < count; ++i) {
            matrices[size_t(i) * 16 + 15] = 1.f;
            matrices[size_t(i) * 16 + 12] = float(i);
        }
        REQUIRE(mesh.setSkinPalette(matrices.data(), count).ok());
        matrices.back() = 5.f;
        REQUIRE_EQ(mesh.getSkinPaletteCount(), count);
        REQUIRE_EQ(mesh.skinPalette().back(), 1.f);
    }
    const auto before = mesh.skinPalette();
    REQUIRE(!mesh.setSkinPalette(nullptr, 20).ok());
    REQUIRE(!mesh.setSkinPalette(before.data(), -1).ok());
    REQUIRE_EQ(mesh.skinPalette(), before);
    auto invalid = before;
    invalid[0]   = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!mesh.setSkinPalette(invalid.data(), 17).ok());
    REQUIRE_EQ(mesh.skinPalette(), before);
}
}  // namespace test_graphics_skin_palette
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_vegetation_extras)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/PbrSurface.h"
#include "graphics/VegetationField.h"
#include "graphics/VegetationFieldGpu.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_vegetation_extras.cpp
namespace test_graphics_vegetation_extras_unit {
using namespace eve::graphics;

TEST_CASE("graphics.vegetation.gradient_validation_rejects_invalid_surface") {
    PbrSurface surface;
    surface.vegetationGradient.enabled = true;
    surface.vegetationGradient.minimum = -.01f;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.vegetationGradient.minimum = 0;
    surface.vegetationGradient.maximum = -.0001f;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.vegetationGradient.maximum  = 1;
    surface.vegetationGradient.colorOne = {-1, 1, 1};
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.vegetationGradient.colorOne = {1, 1, 1};
    surface.vegetationGradient.colorTwo = {1, INFINITY, 1};
    REQUIRE(!validatePbrSurface(surface).ok());
}
}  // namespace test_graphics_vegetation_extras_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_webgpu_pbr_variant)
#include "PrivateTestScope.h"
#include "common/config.h"
#ifdef EVENGINE_WEBGPU
#include <cstdint>
#endif
#ifdef EVENGINE_WEBGPU
#include <iostream>
#endif
#ifdef EVENGINE_WEBGPU
#include "GraphicsParitySupport.h"
#endif
#ifdef EVENGINE_WEBGPU
#include "graphics/PbrSurface.h"
#endif
#ifdef EVENGINE_WEBGPU
#include "graphics/webgpu/Graphics.h"
#endif
#ifdef EVENGINE_WEBGPU
#include "graphics/webgpu/PbrVariantPlan.h"
#endif
#ifdef EVENGINE_WEBGPU
#include "graphics/webgpu/PbrVariantSource.h"
#endif
#ifdef EVENGINE_WEBGPU
#include "zeroerr/assert.h"
#endif
#ifdef EVENGINE_WEBGPU
#include "zeroerr/unittest.h"
#endif
// graphics_webgpu_pbr_variant.cpp
namespace test_graphics_webgpu_pbr_variant_unit {
#ifdef EVENGINE_WEBGPU


using namespace eve::graphics;
using namespace eve::graphics::webgpu;

namespace {
[[maybe_unused]] Texture *marker(std::uintptr_t value) { return reinterpret_cast<Texture *>(value); }

}  // namespace

TEST_CASE("graphics.webgpu.pbrVariantPlansOnlyReachableResources") {
    PbrSurface surface;
    auto       empty = planPbrVariant(surface, false, false);
    REQUIRE(empty.ok());
    CHECK_EQ(empty.value().fragmentTextures, 0u);
    CHECK_EQ(empty.value().vertexTextures, 0u);

    surface.textures[std::size_t(PbrTextureSlot::BaseColor)].texture = marker(1);
    surface.textures[std::size_t(PbrTextureSlot::Normal)].texture    = marker(2);
    surface.vegetationDetail.value                                   = 1.f;
    surface.vegetationDetail.textures[0].texture                     = marker(3);
    surface.vegetationDetail.textures[1].texture                     = marker(4);
    auto detail                                                      = planPbrVariant(surface, true, true);
    REQUIRE(detail.ok());
    CHECK_EQ(detail.value().canonicalTextureMask, 0b101u);
    CHECK_EQ(detail.value().detailTextureMask, 0b011u);
    CHECK_EQ(detail.value().fragmentTextures, 6u);
    CHECK_EQ(detail.value().fragmentSamplers, 3u);
    CHECK_EQ(detail.value().canonicalSamplerRepresentatives[0], 0u);
    CHECK_EQ(detail.value().canonicalSamplerRepresentatives[2], 0u);
    CHECK_EQ(detail.value().detailSamplerRepresentatives[0], 0u);
    CHECK_EQ(detail.value().detailSamplerRepresentatives[1], 0u);

    surface.vegetationDetail.maskMode            = 1;
    surface.vegetationDetail.textures[2].texture = marker(5);
    auto masked                                  = planPbrVariant(surface, true, true);
    REQUIRE(masked.ok());
    CHECK_EQ(masked.value().detailTextureMask, 0b111u);
    CHECK_EQ(masked.value().fragmentTextures, 7u);
    CHECK_EQ(masked.value().fragmentSamplers, 3u);
}

TEST_CASE("graphics.webgpu.pbrVariantSeparatesVertexBudgetAndRejectsOverflow") {
    PbrSurface surface;
    surface.vegetationVertex.source  = PbrVegetationDeformationSource::GpuFields;
    surface.vegetationVertex.texture = marker(1);
    surface.vegetationMotion.mode    = PbrVegetationMotionMode::Object;
    surface.vegetationMotion.texture = marker(2);
    surface.vegetationMotion.noise   = marker(3);
    auto vertex                      = planPbrVariant(surface, false, false, {3, 3});
    REQUIRE(vertex.ok());
    CHECK_EQ(vertex.value().vertexTextures, 3u);
    CHECK_EQ(vertex.value().fragmentTextures, 0u);
    REQUIRE(!planPbrVariant(surface, false, false, {2, 3}).ok());

    for (std::size_t slot = 0; slot < std::size_t(PbrTextureSlot::Count); ++slot)
        surface.textures[slot].texture = marker(slot + 10);
    surface.vegetationExtras.texture = marker(30);
    surface.vegetationColors.texture = marker(31);
    REQUIRE(planPbrVariant(surface, true, true, {17, 16}).ok());
    surface.vegetationAlpha.enabled = true;
    surface.vegetationAlpha.noise   = marker(32);
    REQUIRE(planPbrVariant(surface, true, true, {17, 16}).ok());
    surface.vegetationDetail.value               = 1.f;
    surface.vegetationDetail.textures[0].texture = marker(33);
    surface.vegetationDetail.textures[1].texture = marker(34);
    auto overflow                                = planPbrVariant(surface, true, true, {17, 16});
    REQUIRE(!overflow.ok());
    CHECK_EQ(overflow.error()->code(), eve::DiagnosticCode::Unsupported);
}

TEST_CASE("graphics.webgpu.pbrVariantBaseSourcesCarryBothCanonicalStages") {
    const auto sources = pbrVariantBaseSources();
    CHECK(sources.vertex.starts_with("diagnostic(off, derivative_uniformity);"));
    CHECK(sources.vertex.find("struct Params") != std::string_view::npos);
    CHECK(sources.vertex.find("@vertex") != std::string_view::npos);
    CHECK(sources.vertex.find("vertexField_image") != std::string_view::npos);
    CHECK(sources.vertex.find("@binding(50u) var vertexField_image") != std::string_view::npos);
    CHECK(sources.fragment.starts_with("diagnostic(off, derivative_uniformity);"));
    CHECK(sources.fragment.find("struct Shadows") != std::string_view::npos);
    CHECK(sources.fragment.find("@fragment") != std::string_view::npos);
    CHECK(sources.fragment.find("detailMap2_image") != std::string_view::npos);
    CHECK(sources.fragment.find("vegetationFadeNoise_image") != std::string_view::npos);
}


#endif
}  // namespace test_graphics_webgpu_pbr_variant_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_grass_billboard)
#include "PrivateTestScope.h"
#include <limits>
#include "graphics/Grass.h"
#include "graphics/VegetationWind.h"
#include "zeroerr/unittest.h"
// grass_billboard.cpp
namespace test_grass_billboard {
TEST_CASE("graphics.VegetationWind.packetControlsAndExplicitTime") {
    eve::graphics::VegetationWindState   state{{1, -0.5F, 0.2F}, 0.8F, 0.17F};
    eve::graphics::VegetationWindProfile profile;
    auto                                 disabled = eve::graphics::packVegetationWind(state, profile, 2.0);
    REQUIRE(disabled.ok());
    CHECK(disabled.value()[5] == 0);
    profile.enabled   = true;
    profile.billboard = true;
    profile.alphaTest = false;
    auto packet       = eve::graphics::packVegetationWind(state, profile, 2.0);
    REQUIRE(packet.ok());
    const auto &data = packet.value();
    CHECK(data[0] == state.direction.x);
    CHECK(data[1] == state.direction.y);
    CHECK(data[2] == state.direction.z);
    CHECK(data[3] == state.strength);
    CHECK(data[4] == state.phase);
    CHECK(data[5] == -100);
    CHECK(data[6] == profile.flex.x);
    CHECK(data[7] == profile.flex.y);
    CHECK(data[8] == 0);
    CHECK(data[9] == profile.frequency.x);
    CHECK(data[10] == profile.frequency.y);
    CHECK(data[11] == profile.frequency.z);
    CHECK(std::abs(data[12] - 0.479425539F) < 0.000001F);
    CHECK(std::abs(data[13] - 0.909297427F) < 0.000001F);
    profile.billboard = false;
    profile.alphaTest = true;
    auto complete     = eve::graphics::packVegetationWind(state, profile, 2.0);
    REQUIRE(complete.ok());
    CHECK(complete.value()[5] == 100);
    CHECK(complete.value()[8] == profile.flex.z);
}
TEST_CASE("graphics.VegetationWind.packetRejectsInvalidDisabledInputs") {
    eve::graphics::VegetationWindState   state;
    eve::graphics::VegetationWindProfile profile;
    CHECK(!eve::graphics::packVegetationWind(state, profile, std::numeric_limits<double>::infinity()).ok());
    profile.maximumDistance = 0;
    CHECK(!eve::graphics::packVegetationWind(state, profile, 0).ok());
    profile.maximumDistance = 100;
    profile.frequency.x     = std::numeric_limits<float>::quiet_NaN();
    CHECK(!eve::graphics::packVegetationWind(state, profile, 0).ok());
    profile.frequency.x = 0.25F;
    state.strength      = 2;
    CHECK(!eve::graphics::packVegetationWind(state, profile, 0).ok());
    CHECK(!profile.enabled);
    CHECK(state.strength == 2);
}
TEST_CASE("graphics.Grass.independentBillboardDimensions") {
    std::vector<eve::graphics::grass::Point> points(1);
    points[0].position   = {3, 4, 5};
    points[0].id         = 11;
    points[0].scale      = 2;
    points[0].widthScale = 0.25F;
    const auto dense     = eve::graphics::grass::buildBillboards(points, 1, 1, false);
    const auto dark      = eve::graphics::grass::buildBillboards(points, 1, 1, true);
    for (int i = 0; i < 4; ++i) {
        CHECK(dense.posXYZ[size_t(i) * 3] == 3);
        CHECK(dense.posXYZ[size_t(i) * 3 + 1] == 4);
        CHECK(dense.nrmXYZ[size_t(i) * 3] == 11);
        CHECK(dense.nrmXYZ[size_t(i) * 3 + 1] == 2);
        CHECK(dense.nrmXYZ[size_t(i) * 3 + 2] == -0.25F);
        CHECK(dark.nrmXYZ[size_t(i) * 3 + 2] == 1.25F);
    }
    points[0].widthScale  = 0;
    const auto legacy     = eve::graphics::grass::buildBillboards(points, 1, 1, false);
    const auto legacyDark = eve::graphics::grass::buildBillboards(points, 1, 1, true);
    CHECK(legacy.nrmXYZ[2] == 0);
    CHECK(legacyDark.nrmXYZ[2] == 1);
    CHECK(legacy.uvST == dense.uvST);
    CHECK(legacy.indices == dense.indices);
    points[0].tint           = {0.2F, 0.4F, 0.8F};
    const auto     tinted    = eve::graphics::grass::buildBillboards(points, 1, 1, false);
    const uint32_t packedRgb = (51u << 16) | (102u << 8) | 204u;
    for (int i = 0; i < 4; ++i) CHECK(tinted.nrmXYZ[size_t(i) * 3] == -float(packedRgb + 1u));
}

TEST_CASE("graphics.VegetationWind.phaseAndSmoothing") {
    eve::graphics::VegetationWindState state;
    REQUIRE(eve::graphics::advanceVegetationWind(state, {1, 0, 0}, 1, 4).ok());
    CHECK(state.direction.x == 1);
    CHECK(state.direction.y == -0.5F);
    CHECK(state.strength == 1);
    CHECK(std::abs(state.phase - 0.4F) < 0.000001F);
    state.phase       = 99.9F;
    state.updatePhase = 99.9F;
    REQUIRE(eve::graphics::advanceVegetationWind(state, {1, 0, 0}, 1, 4).ok());
    CHECK(std::abs(state.phase - 0.3F) < 0.00001F);
    const auto saved = state;
    CHECK(!eve::graphics::advanceVegetationWind(state, {1, 0, 0}, 1, -1).ok());
    CHECK(state.phase == saved.phase);
}
TEST_CASE("graphics.VegetationWind.rootDistanceAndBillboardGolden") {
    eve::graphics::VegetationWindInput input;
    eve::graphics::VegetationWindState wind;
    wind.direction = {1, 0, 0};
    wind.strength  = 1;
    auto root      = eve::graphics::evaluateVegetationWind(input, wind);
    REQUIRE(root.ok());
    CHECK(root.value() == glm::vec3(0));
    input.position  = {0, 1, 0};
    input.billboard = true;
    input.flex      = {1, 0, 0};
    // phase=0, sine time=0: gust=(0.5+1)*0.7=1.05; source volume normalization is 1/(1+1.05^2).
    auto bent = eve::graphics::evaluateVegetationWind(input, wind);
    REQUIRE(bent.ok());
    CHECK(std::abs(bent.value().x - float(1.05 / 2.1025)) < 0.000001F);
    CHECK(std::abs(bent.value().y - float(1 / 2.1025)) < 0.000001F);
    input.cameraPosition = {100, 0, 0};
    auto far             = eve::graphics::evaluateVegetationWind(input, wind);
    REQUIRE(far.ok());
    CHECK(far.value() == input.position);
    input.objectToWorld = glm::mat3(0);
    CHECK(!eve::graphics::evaluateVegetationWind(input, wind).ok());
}

TEST_CASE("graphics.VegetationWind.branchLeafAndScaledTransformGolden") {
    eve::graphics::VegetationWindInput input;
    input.position            = {0.2F, 0.8F, 0.1F};
    input.worldOffset         = {3, 4, 5};
    input.cameraPosition      = input.worldOffset;
    input.objectToWorld[0][0] = 2;
    input.objectToWorld[1][1] = 3;
    input.objectToWorld[2][2] = 0.5F;
    input.sinTimeQuarter      = 0.3F;
    input.sinTimeFull         = -0.2F;
    eve::graphics::VegetationWindState state;
    state.direction            = {1, -0.5F, 0.2F};
    state.strength             = 0.8F;
    state.phase                = 0.17F;
    const glm::vec3 expected[] = {{0.535718825F, 0.604993208F, 0.383200569F},
                                  {0.664822256F, 0.513757707F, 0.489274523F},
                                  {0.664999698F, 0.513719712F, 0.491201362F}};
    for (int i = 0; i < 3; ++i) {
        input.billboard = i == 0;
        input.alphaTest = i != 1;
        auto result     = eve::graphics::evaluateVegetationWind(input, state);
        REQUIRE(result.ok());
        for (int axis = 0; axis < 3; ++axis) CHECK(std::abs(result.value()[axis] - expected[i][axis]) < 0.000002F);
    }
}
}  // namespace test_grass_billboard
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_grass)
#include "PrivateTestScope.h"
#include <SDL2/SDL.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "PathBesideSource.h"
#include "filesystem/FileData.h"
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
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// grass.cpp
namespace test_grass_unit {
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;


#include "graphics/shaders/mesh3d_grass_frag_spv.inc"
#include "graphics/shaders/mesh3d_grass_vert_spv.inc"


using eve::graphics::Camera3D;
using eve::graphics::Graphics;
using eve::graphics::GrassField;
using eve::graphics::Light3D;
using eve::graphics::Mesh;
using eve::graphics::Renderable2D;
using eve::graphics::Renderable3D;
using eve::graphics::RenderSystem;
using eve::graphics::RenderSystem3D;
using eve::graphics::Shader;
using eve::graphics::Texture;
using eve::graphics::grass::BillboardMesh;
using eve::graphics::grass::PackedAtlasInfo;
using eve::graphics::grass::Point;
using eve::graphics::grass::SampleParams;

namespace {


EVE_DEFINE_PATH_BESIDE_SOURCE()

[[maybe_unused]] void makeTestPlane(std::vector<float> &pos, std::vector<float> &nrm, std::vector<uint32_t> &idx) {
    eve::graphics::grass::makePlane(4.f, 4.f, 4, 4, pos, nrm, idx);
}

[[maybe_unused]] float minPairDist(const std::vector<Point> &pts) {
    float best = 1e9f;
    for (size_t i = 0; i < pts.size(); ++i) {
        for (size_t j = i + 1; j < pts.size(); ++j) {
            const glm::vec3 d = pts[i].position - pts[j].position;
            best              = std::min(best, glm::length(d));
        }
    }
    return best;
}

}  // namespace

TEST_CASE("graphics.Grass.bindDefaults") {
    Shader sh;
    sh.setKind(Shader::Kind::eMesh3D);
    eve::graphics::grass::bindDefaults(&sh);
    CHECK(sh.hasUniform("time"));
    CHECK(sh.hasUniform("frameDuration"));
    CHECK(sh.hasUniform("lightGreen"));
    CHECK(sh.hasUniform("darkGreen"));
    CHECK(sh.hasUniform("alwaysDark"));
    CHECK_EQ(sh.getUniformIndex("time"), 0);
    CHECK_EQ(sh.getUniformIndex("alwaysDark"), 5);
    CHECK_EQ(sh.getUniformIndex("lightGreen"), 6);
    CHECK_EQ(sh.getUniformIndex("darkGreen"), 9);
    CHECK_EQ(sh.getUniformIndex("frameCount"), 12);
    CHECK_EQ(sh.getUniformIndex("atlasCols"), 13);
    CHECK_EQ(sh.getUniformIndex("atlasRows"), 14);
    CHECK_EQ(sh.getUniformIndex("grassVariantCount"), 15);
    CHECK_EQ(sh.getUniformIndex("leafVariantCount"), 16);
    CHECK_EQ(sh.getUniformIndex("leafRowOffset"), 17);
    CHECK_EQ(sh.getUniformIndex("windGlobals"), 18);
    CHECK_EQ(sh.getUniformIndex("windPhaseDistance"), 22);
    CHECK_EQ(sh.getUniformIndex("windFlex"), 24);
    CHECK_EQ(sh.getUniformIndex("windFrequency"), 27);
    CHECK_EQ(sh.getUniformIndex("windSineTime"), 30);
    CHECK_EQ(sh.pushConstantData()[23], 0.f);
    CHECK_EQ(sh.usedFloats(), 32);
}

TEST_CASE("graphics.Grass.paramNames") {
    CHECK_EQ(eve::graphics::grass::paramCount(), 32);
    CHECK_EQ(eve::graphics::grass::paramName(0), std::string("time"));
    CHECK_EQ(eve::graphics::grass::paramName(5), std::string("alwaysDark"));
    CHECK_EQ(eve::graphics::grass::paramName(12), std::string("frameCount"));
    CHECK_EQ(eve::graphics::grass::paramName(13), std::string("atlasCols"));
    CHECK_EQ(eve::graphics::grass::paramName(17), std::string("leafRowOffset"));
}

TEST_CASE("graphics.Grass.foliageProfileReusesStablePushLayout") {
    Shader sh;
    sh.setKind(Shader::Kind::eMesh3D);
    eve::graphics::grass::GrassFoliageSettings settings;
    settings.baseR             = 0.2f;
    settings.baseG             = 0.4f;
    settings.baseB             = 0.6f;
    settings.normalStrength    = 1.7f;
    settings.renderDistance    = 91.f;
    settings.fadeRange         = 13.f;
    settings.snowMinimumHeight = 8.f;
    settings.snowFadeDistance  = 17.f;
    settings.snowProgress      = 0.75f;
    eve::graphics::grass::bindFoliage(&sh, settings);
    CHECK_EQ(sh.usedFloats(), 32);
    CHECK_EQ(sh.pushConstantData()[0], 8.f);
    CHECK_EQ(sh.pushConstantData()[1], 1.7f);
    CHECK_EQ(sh.pushConstantData()[6], 0.2f);
    CHECK_EQ(sh.pushConstantData()[12], -1.f);
    CHECK_EQ(sh.pushConstantData()[13], 91.f);
    CHECK_EQ(sh.pushConstantData()[14], 13.f);
    CHECK_EQ(sh.pushConstantData()[15], 17.f);
    CHECK_EQ(sh.pushConstantData()[16], 0.75f);
    CHECK_EQ(sh.pushConstantData()[17], 105.5f);
    CHECK_EQ(sh.getUniformIndex("windGlobals"), 18);
}

TEST_CASE("graphics.Grass.terrainDetailOverwriteKeepsThreeDistanceSemantics") {
    eve::graphics::grass::GrassFoliageSettings           foliage;
    eve::graphics::grass::TerrainDetailOverwriteSettings settings;
    settings.pcgDetailDistance        = -4;
    settings.pcgFadeoutDistance       = 12;
    settings.unityDetailDistance      = 140;
    settings.unityDetailDensity       = 0.35F;
    settings.detailResolutionPerPatch = 4;
    auto applied                      = eve::graphics::grass::applyTerrainDetailOverwrite(foliage, settings);
    REQUIRE(applied.ok());
    CHECK(applied.value() == static_cast<int>(eve::graphics::grass::TerrainDetailQuality::VeryHigh4));
    CHECK(foliage.renderDistance == 0);
    CHECK(foliage.fadeRange == 12);
    CHECK(foliage.hardRenderDistance == 140);
    CHECK(foliage.density == 0.35F);
    auto before                 = foliage;
    settings.unityDetailDensity = 2;
    CHECK(!eve::graphics::grass::applyTerrainDetailOverwrite(foliage, settings).ok());
    CHECK(foliage.renderDistance == before.renderDistance);
    CHECK(eve::graphics::grass::terrainDetailQuality(7) == eve::graphics::grass::TerrainDetailQuality::VeryLow64);
}


TEST_CASE("graphics.Grass.spvMagic") {
    CHECK(mesh3d_grass_vert_spv_count > 0);
    CHECK(mesh3d_grass_frag_spv_count > 0);
    CHECK_EQ(mesh3d_grass_vert_spv[0], 0x07230203u);
    CHECK_EQ(mesh3d_grass_frag_spv[0], 0x07230203u);
}

TEST_CASE("graphics.Grass.poissonMinDistance") {
    std::vector<float>    pos, nrm;
    std::vector<uint32_t> idx;
    makeTestPlane(pos, nrm, idx);

    SampleParams p;
    p.radius       = 0.45f;
    p.maxPoints    = 200;
    p.seed         = 7;
    p.minSlopeDot  = 0.1f;
    const auto pts = eve::graphics::grass::samplePoisson(pos.data(), nrm.data(), int(pos.size() / 3), idx.data(),
                                                         int(idx.size()), p);
    CHECK(pts.size() >= 8);
    CHECK(int(pts.size()) <= p.maxPoints);
    const float minD = minPairDist(pts);
    CHECK(minD + 1e-4f >= p.radius);

    for (const auto &pt : pts) {
        CHECK(std::abs(pt.position.y) < 1e-4f);
        CHECK(pt.position.x >= -2.01f);
        CHECK(pt.position.x <= 2.01f);
        CHECK(pt.position.z >= -2.01f);
        CHECK(pt.position.z <= 2.01f);
    }
}

TEST_CASE("graphics.Grass.haltonCountAndCoverage") {
    std::vector<float>    pos, nrm;
    std::vector<uint32_t> idx;
    makeTestPlane(pos, nrm, idx);

    const auto pts = eve::graphics::grass::sampleHalton(pos.data(), nrm.data(), int(pos.size() / 3), idx.data(),
                                                        int(idx.size()), 64, 3, 0.1f);
    CHECK_EQ(int(pts.size()), 64);
    CHECK_EQ(pts.front().id, 0u);
    CHECK_EQ(pts.back().id, 63u);

    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const auto &pt : pts) {
        minX = std::min(minX, pt.position.x);
        maxX = std::max(maxX, pt.position.x);
        minZ = std::min(minZ, pt.position.z);
        maxZ = std::max(maxZ, pt.position.z);
    }
    CHECK(maxX - minX > 2.f);
    CHECK(maxZ - minZ > 2.f);
}

TEST_CASE("graphics.Grass.sparseIsSparser") {
    std::vector<float>    pos, nrm;
    std::vector<uint32_t> idx;
    makeTestPlane(pos, nrm, idx);

    SampleParams dense;
    dense.radius        = 0.3f;
    dense.maxPoints     = 400;
    dense.seed          = 1;
    SampleParams sparse = dense;
    sparse.radius       = 1.2f;
    sparse.maxPoints    = 80;
    sparse.seed         = 99;

    const auto d = eve::graphics::grass::samplePoisson(pos.data(), nrm.data(), int(pos.size() / 3), idx.data(),
                                                       int(idx.size()), dense);
    const auto s = eve::graphics::grass::samplePoisson(pos.data(), nrm.data(), int(pos.size() / 3), idx.data(),
                                                       int(idx.size()), sparse);
    CHECK(d.size() > s.size());
    CHECK(s.size() >= 1);
}

TEST_CASE("graphics.Grass.billboardRootPivot") {
    std::vector<Point> pts(1);
    pts[0].position          = glm::vec3(3.f, 1.5f, -2.f);
    pts[0].id                = 11;
    pts[0].scale             = 1.25f;
    const BillboardMesh mesh = eve::graphics::grass::buildBillboards(pts, 0.4f, 0.8f);
    CHECK_EQ(int(mesh.posXYZ.size()), 12);
    CHECK_EQ(int(mesh.uvST.size()), 8);
    CHECK_EQ(int(mesh.indices.size()), 6);
    for (int i = 0; i < 4; ++i) {
        CHECK(std::abs(mesh.posXYZ[size_t(i * 3 + 0)] - 3.f) < 1e-5f);
        CHECK(std::abs(mesh.posXYZ[size_t(i * 3 + 1)] - 1.5f) < 1e-5f);
        CHECK(std::abs(mesh.posXYZ[size_t(i * 3 + 2)] + 2.f) < 1e-5f);
        CHECK(std::abs(mesh.nrmXYZ[size_t(i * 3 + 0)] - 11.f) < 1e-5f);
        CHECK(std::abs(mesh.nrmXYZ[size_t(i * 3 + 1)] - 1.25f) < 1e-5f);
        CHECK(std::abs(mesh.nrmXYZ[size_t(i * 3 + 2)]) < 1e-5f);
    }
    // Bottom edge UVs sit at v=0; their midpoint is the root (u=0.5).
    CHECK(std::abs(mesh.uvST[1]) < 1e-5f);
    CHECK(std::abs(mesh.uvST[3]) < 1e-5f);
    CHECK(std::abs(0.5f * (mesh.uvST[0] + mesh.uvST[2]) - 0.5f) < 1e-5f);
}

TEST_CASE("graphics.Grass.swayDesyncByInstanceId") {
    const int a = eve::graphics::grass::swayFrame(0.f, 0.12f, 0, 4);
    const int b = eve::graphics::grass::swayFrame(0.f, 0.12f, 1, 4);
    const int c = eve::graphics::grass::swayFrame(0.f, 0.12f, 2, 4);
    CHECK(a >= 0);
    CHECK(a < 4);
    CHECK(b >= 0);
    CHECK(b < 4);
    CHECK(c >= 0);
    CHECK(c < 4);
    CHECK((a != b || b != c));

    const int later = eve::graphics::grass::swayFrame(0.36f, 0.12f, 0, 4);
    CHECK(later != a);
}

TEST_CASE("graphics.Grass.swayAtlasFourFrames") {
    std::vector<uint8_t> rgba;
    eve::graphics::grass::makeSwayAtlasRGBA(32, 32, 4, rgba);
    CHECK_EQ(eve::graphics::grass::swayAtlasWidth(32, 4), 128);
    CHECK_EQ(eve::graphics::grass::swayAtlasHeight(32), 32);
    CHECK_EQ(int(rgba.size()), 128 * 32 * 4);

    int opaque      = 0;
    int perFrame[4] = {};
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 128; ++x) {
            const uint8_t a = rgba[(size_t(y) * 128u + size_t(x)) * 4u + 3u];
            if (a > 16) {
                ++opaque;
                perFrame[x / 32]++;
            }
        }
    }
    CHECK(opaque > 80);
    for (int f = 0; f < 4; ++f) CHECK(perFrame[f] > 10);

    // Filled tuft: the root band is denser than the tips (not five 1px sticks).
    int baseOpaque = 0, tipOpaque = 0;
    for (int x = 0; x < 32; ++x) {
        for (int y = 24; y < 32; ++y) {
            if (rgba[(size_t(y) * 128u + size_t(x)) * 4u + 3u] > 16) ++baseOpaque;
        }
        for (int y = 0; y < 8; ++y) {
            if (rgba[(size_t(y) * 128u + size_t(x)) * 4u + 3u] > 16) ++tipOpaque;
        }
    }
    CHECK(baseOpaque > tipOpaque);
    CHECK(baseOpaque > 40);

    // Frames actually differ (wind lean).
    int diffs = 0;
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            const uint8_t a0 = rgba[(size_t(y) * 128u + size_t(x)) * 4u + 3u];
            const uint8_t a3 = rgba[(size_t(y) * 128u + size_t(x + 96)) * 4u + 3u];
            if (a0 != a3) ++diffs;
        }
    }
    CHECK(diffs > 8);
}


TEST_CASE("graphics.Grass.layerFlag") {
    Shader sh;
    sh.setKind(Shader::Kind::eMesh3D);
    eve::graphics::grass::bindDefaults(&sh);
    eve::graphics::grass::bindLayer(&sh, true);
    float dark = 0.f;
    REQUIRE_EQ(sh.getFromVar("alwaysDark", &dark, sizeof(dark)), int(sizeof(dark)));
    CHECK(dark > 0.5f);
    eve::graphics::grass::bindLayer(&sh, false);
    REQUIRE_EQ(sh.getFromVar("alwaysDark", &dark, sizeof(dark)), int(sizeof(dark)));
    CHECK(dark < 0.5f);
}

TEST_CASE("graphics.Grass.billboardAlwaysDarkFlag") {
    std::vector<Point> pts(1);
    pts[0].position          = glm::vec3(0.f, 0.f, 0.f);
    pts[0].id                = 3;
    pts[0].scale             = 1.f;
    const BillboardMesh dark = eve::graphics::grass::buildBillboards(pts, 0.4f, 0.8f, true);
    CHECK_EQ(int(dark.nrmXYZ.size()), 12);
    for (int i = 0; i < 4; ++i) CHECK(dark.nrmXYZ[size_t(i * 3 + 2)] > 0.5f);
}

namespace {

[[maybe_unused]] void resetGrassScene() {
    if (ecs::current()->getManager<Renderable3D>() != nullptr) {
        auto view = ecs::View<Renderable3D, Renderable3D::MeshRenderer>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [mr]   = *it;
            mr->visible = false;
        }
    }
    if (ecs::current()->getManager<Camera3D>() != nullptr) {
        auto camView = ecs::View<Camera3D, Camera3D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data]  = *it;
            data->active = false;
        }
    }
    if (ecs::current()->getManager<Light3D>() != nullptr) {
        auto lightView = ecs::View<Light3D, Light3D::Data>();
        for (auto it = lightView.begin(); it != lightView.end(); ++it) {
            auto [d]   = *it;
            d->enabled = false;
        }
    }
    if (ecs::current()->getManager<Renderable2D>() != nullptr) {
        auto view = ecs::View<Renderable2D, Renderable2D::Sprite>();
        for (auto it = view.begin(); it != view.end(); ++it) {
            auto [sp]   = *it;
            sp->visible = false;
        }
    }
}

[[maybe_unused]] Texture *makeSolidTex(Graphics *gfx, uint8_t r, uint8_t g, uint8_t b) {
    const uint8_t px[4] = {r, g, b, 255};
    return gfx->newTexture(1, 1, px);
}

[[maybe_unused]] Mesh *makeGroundMesh(Graphics *gfx, float sizeX, float sizeZ) {
    std::vector<float>    pos, nrm;
    std::vector<uint32_t> idx;
    eve::graphics::grass::makePlane(sizeX, sizeZ, 12, 12, pos, nrm, idx);
    std::vector<float> uv(size_t(pos.size() / 3) * 2u, 0.f);
    for (size_t i = 0; i < pos.size() / 3; ++i) {
        uv[i * 2]     = pos[i * 3] / sizeX + 0.5f;
        uv[i * 2 + 1] = pos[i * 3 + 2] / sizeZ + 0.5f;
    }
    return gfx->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), int(pos.size() / 3), idx.data(), int(idx.size()));
}

[[maybe_unused]] void savePng(eve::image::ImageData *frame, const std::string &path) {
    REQUIRE(frame != nullptr);
    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    eve::filesystem::FileData   *png = frame->encode(medialoader::FormatHandler::ENCODED_PNG, "grass.png", false);
    REQUIRE(png != nullptr);
    REQUIRE(png->getSize() > 0);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.good());
        out.write(static_cast<const char *>(png->getData()), static_cast<std::streamsize>(png->getSize()));
        REQUIRE(out.good());
    }
    delete png;
    std::printf("\ngrass render saved: %s\n", path.c_str());
}

}  // namespace
}  // namespace test_grass_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_grass_wind)
#include "PrivateTestScope.h"
#include <algorithm>
#include <array>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "graphics/Grass.h"
#include "graphics/GrassWind.h"
#include "graphics/VegetationWind.h"
#include "zeroerr/unittest.h"
// grass_wind.cpp
namespace test_grass_wind_unit {
namespace {
[[maybe_unused]] std::array<float, 32> snapshot(const eve::graphics::Shader &shader) {
    std::array<float, 32> result;
    std::copy_n(shader.pushConstantData(), 32, result.begin());
    return result;
}
}  // namespace
TEST_CASE("graphics.GrassWind.atomicSnapshotAndDisabledDefaults") {
    using namespace eve::graphics;
    Shader shader;
    grass::bindDefaults(&shader);
    REQUIRE(shader.usedFloats() == 32);
    CHECK(grass::paramCount() == 32);
    CHECK(grass::paramName(31) == "windSinFull");
    CHECK(shader.pushConstantData()[23] == 0);
    auto                  original = snapshot(shader);
    VegetationWindState   state{{1, -0.5F, 0.2F}, 0.8F, 0.17F};
    VegetationWindProfile profile;
    profile.enabled = true;
    auto packed     = packVegetationWind(state, profile, 2);
    REQUIRE(packed.ok());
    REQUIRE(applyGrassWind(shader, state, profile, 2).ok());
    for (int i = 0; i < 18; ++i) CHECK(shader.pushConstantData()[i] == original[i]);
    for (int i = 0; i < 14; ++i) CHECK(shader.pushConstantData()[i + 18] == packed.value()[i]);
    const auto before       = snapshot(shader);
    profile.maximumDistance = 0;
    CHECK(!applyGrassWind(shader, state, profile, 3).ok());
    CHECK(snapshot(shader) == before);
    profile.maximumDistance = 100;
    CHECK(!applyGrassWind(shader, state, profile, std::numeric_limits<double>::infinity()).ok());
    CHECK(snapshot(shader) == before);
    profile.enabled = false;
    REQUIRE(applyGrassWind(shader, state, profile, 3).ok());
    CHECK(shader.pushConstantData()[23] == 0);
}
TEST_CASE("graphics.VegetationWind.instantApplyIsDistinctAndAtomic") {
    eve::graphics::VegetationWindState state{{9, 8, 7}, 0.25F, 4};
    REQUIRE(eve::graphics::initializeVegetationWind(state, {0.6F, 0.8F, 0}, 2).ok());
    CHECK(state.direction == glm::vec3(0.6F, -0.8F, 0));
    CHECK(state.strength == 1.2F);
    CHECK(std::abs(state.phase - 0.1331F) < 0.000001F);
    CHECK(state.updatePhase == 0);
    REQUIRE(eve::graphics::advanceVegetationWind(state, {0.6F, 0.8F, 0}, 1.2F, 1).ok());
    CHECK(std::abs(state.phase - 0.1331F) < 0.000001F);
    CHECK(std::abs(state.updatePhase - 0.1331F) < 0.000001F);
    const auto before = state;
    CHECK(!eve::graphics::initializeVegetationWind(state, {std::numeric_limits<float>::infinity(), 0, 0}, 1).ok());
    CHECK(state.direction == before.direction);
    CHECK(state.strength == before.strength);
    CHECK(state.phase == before.phase);
    CHECK(state.updatePhase == before.updatePhase);
}
TEST_CASE("graphics.VegetationWind.audioRetainsSourceControllerSemantics") {
    using namespace eve::graphics;
    VegetationWindAudioState state;
    REQUIRE(advanceVegetationWindAudio(state, 1, 5, 1, true, true).ok());
    CHECK(state.volume == 0.2F);
    CHECK(state.anchorVolume == 0);
    CHECK(state.currentWindSpeed == 0);
    CHECK(state.processing);
    CHECK(state.playing);
    for (int i = 0; i < 20 && state.processing; ++i)
        REQUIRE(advanceVegetationWindAudio(state, 1, 5, 1, true, true).ok());
    CHECK(state.volume == 1);
    CHECK(!state.processing);
    REQUIRE(advanceVegetationWindAudio(state, 0, 5, 1, true, true).ok());
    CHECK(state.volume == 1);
    state.processing   = true;
    state.anchorVolume = state.volume;
    REQUIRE(advanceVegetationWindAudio(state, 0, 5, 1, true, true).ok());
    CHECK(state.volume == 0.8F);
    CHECK(state.processing);
    const auto before = state;
    CHECK(!advanceVegetationWindAudio(state, 1, 0, 1, true, true).ok());
    CHECK(state.volume == before.volume);
    CHECK(state.processing == before.processing);
}
TEST_CASE("graphics.GrassWind.rejectsLateSchemaMismatchBeforeAnyWrite") {
    using namespace eve::graphics;
    Shader shader;
    for (int i = 0; i < 18; ++i) shader.declareFloat("prefix" + std::to_string(i));
    shader.declareVec4("windGlobals");
    shader.declareVec2("windPhaseDistance");
    shader.declareVec3("windFlex");
    shader.declareVec3("windFrequency");
    shader.declareVec2("wrongLastUniform");
    shader.sendVec4("windGlobals", 9, 8, 7, 6);
    const auto            before = snapshot(shader);
    VegetationWindState   state{{1, 0, 0}, 1, 0};
    VegetationWindProfile profile;
    profile.enabled = true;
    CHECK(!applyGrassWind(shader, state, profile, 0).ok());
    CHECK(snapshot(shader) == before);
}
}  // namespace test_grass_wind_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_hair)
#include "PrivateTestScope.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include "Fixtures.h"
#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Font.h"
#include "graphics/GBuffer.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "graphics/HairCards.h"
#include "graphics/HairShader.h"
#include "graphics/Light.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/Outline.h"
#include "graphics/Quad.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "graphics/hair/Binding.h"
#include "graphics/hair/ClusterGrid.h"
#include "graphics/hair/GroomAsset.h"
#include "graphics/hair/GroomInstance.h"
#include "graphics/hair/Guides.h"
#include "graphics/hair/Procedural.h"
#include "graphics/hair/RibbonBuilder.h"
#include "graphics/hair/Simulation.h"
#include "graphics/hair/StrandsDatas.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// hair.cpp
namespace test_hair_unit {
#include "graphics/shaders/mesh3d_hair_frag_spv.inc"
#include "graphics/shaders/mesh3d_hair_vert_spv.inc"


// hair.cpp
using eve::graphics::Graphics;
using eve::graphics::Renderable3D;
using eve::graphics::Shader;

TEST_CASE("graphics.HairShader.bindDefaults") {
    Shader sh;
    sh.setKind(Shader::Kind::eMesh3D);
    eve::graphics::hair::bindDefaults(&sh);
    CHECK(sh.hasUniform("specExp"));
    CHECK(sh.hasUniform("strandDir"));
    CHECK(sh.hasUniform("marschnerR"));
    CHECK(sh.hasUniform("marschnerTT"));
    CHECK(sh.hasUniform("marschnerTRT"));
    CHECK(sh.hasUniform("selfShadowStrength"));
    CHECK(sh.hasUniform("selfShadowBias"));
    CHECK(sh.hasUniform("rootAoStrength"));
    CHECK_EQ(sh.getUniformIndex("specExp"), 0);
    CHECK_EQ(sh.getUniformIndex("strandDir"), 6);
    CHECK_EQ(sh.getUniformIndex("marschnerR"), 9);
    CHECK_EQ(sh.usedFloats(), 15);
}

TEST_CASE("graphics.HairShader.paramNames") {
    CHECK_EQ(eve::graphics::hair::paramCount(), 15);
    CHECK_EQ(eve::graphics::hair::paramName(0), std::string("specExp"));
    CHECK_EQ(eve::graphics::hair::paramName(8), std::string("strandDirZ"));
    CHECK_EQ(eve::graphics::hair::paramName(9), std::string("marschnerR"));
    CHECK_EQ(eve::graphics::hair::paramName(11), std::string("marschnerTRT"));
    CHECK_EQ(eve::graphics::hair::paramName(12), std::string("selfShadowStrength"));
    CHECK_EQ(eve::graphics::hair::paramName(14), std::string("rootAoStrength"));
}

TEST_CASE("graphics.HairShader.spvMagic") {
    CHECK(mesh3d_hair_vert_spv_count > 0);
    CHECK(mesh3d_hair_frag_spv_count > 0);
    CHECK_EQ(mesh3d_hair_vert_spv[0], 0x07230203u);
    CHECK_EQ(mesh3d_hair_frag_spv[0], 0x07230203u);
}


TEST_CASE("graphics.Renderable3D.hairFlag") {
    auto *r = Renderable3D::create();
    REQUIRE(r != nullptr);
    CHECK(!r->getHair());
    r->setHair(true);
    CHECK(r->getHair());
    r->setHair(false);
    CHECK(!r->getHair());
}

// hair_cards.cpp
using eve::graphics::Graphics;
using eve::graphics::Material;
using eve::graphics::Mesh;
using eve::graphics::Renderable3D;

TEST_CASE("graphics.HairCards.buildCardTopology") {
    auto data = eve::graphics::hair::buildCard(0.2f, 0.5f);
    CHECK_EQ(data.positions.size(), 12u);
    CHECK_EQ(data.normals.size(), 12u);
    CHECK_EQ(data.uvs.size(), 8u);
    CHECK_EQ(data.indices.size(), 6u);
}

TEST_CASE("graphics.HairCards.buildFromRootsAndPolyline") {
    eve::graphics::hair::CardRoot roots[2] = {};
    roots[0]                               = {0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.1f, 0.4f};
    roots[1]                               = {0.2f, 0.f, 0.f, 0.f, 1.f, 0.1f, 0.08f, 0.35f};
    auto fromRoots                         = eve::graphics::hair::buildCardsFromRoots(roots, 2);
    CHECK_EQ(fromRoots.indices.size(), 12u);

    const float poly[] = {0.f, 0.f, 0.f, 0.f, 0.2f, 0.f, 0.f, 0.45f, 0.05f};
    auto        along  = eve::graphics::hair::buildCardsAlongPolyline(poly, 3, 0.1f);
    CHECK_EQ(along.indices.size(), 12u);
}


// hair_groom.cpp
using eve::graphics::Graphics;
using eve::graphics::hair::appendRibbonMesh;
using eve::graphics::hair::BindingDeformMode;
using eve::graphics::hair::buildGuideWeights;
using eve::graphics::hair::buildRibbons;
using eve::graphics::hair::ClusterGrid;
using eve::graphics::hair::extractGuides;
using eve::graphics::hair::filterStrandsByCurves;
using eve::graphics::hair::generateOnPlane;
using eve::graphics::hair::GroomAsset;
using eve::graphics::hair::GroomBinding;
using eve::graphics::hair::GroomGroup;
using eve::graphics::hair::GroomInstance;
using eve::graphics::hair::GroomLod;
using eve::graphics::hair::GuideInfluence;
using eve::graphics::hair::GuideSimParams;
using eve::graphics::hair::GuideSimulator;
using eve::graphics::hair::interpolateStrands;
using eve::graphics::hair::InterpolationMode;
using eve::graphics::hair::ProceduralParams;
using eve::graphics::hair::Representation;
using eve::graphics::hair::RibbonMesh;
using eve::graphics::hair::RibbonParams;
using eve::graphics::hair::selectLodIndex;
using eve::graphics::hair::SkinTriMesh;
using eve::graphics::hair::StrandCurve;
using eve::graphics::hair::StrandGuideWeights;
using eve::graphics::hair::StrandPoint;
using eve::graphics::hair::StrandsDatas;

TEST_CASE("graphics.hair.strandsValidateRejectsEmpty") {
    StrandsDatas empty;
    auto         bad = empty.validate();
    CHECK(!bad.ok());
}

TEST_CASE("graphics.hair.strandsValidateAcceptsSimpleCurve") {
    StrandsDatas             data;
    std::vector<StrandPoint> points(3);
    points[0] = {glm::vec3(0.f, 0.f, 0.f), 0.002f, 0.f};
    points[1] = {glm::vec3(0.f, 0.05f, 0.f), 0.0015f, 0.5f};
    points[2] = {glm::vec3(0.f, 0.1f, 0.f), 0.001f, 1.f};
    StrandCurve curve;
    curve.pointOffset = 0;
    curve.pointCount  = 3;
    curve.length      = 0.1f;
    data.setPoints(std::move(points));
    data.setCurves({curve});
    auto ok = data.validate();
    CHECK(ok.ok());
    CHECK_EQ(data.curveCount(), 1u);
    CHECK_EQ(data.pointCount(), 3u);
    CHECK_EQ(data.curvePoints(0).size(), 3u);
}

TEST_CASE("graphics.hair.proceduralPlaneDeterministic") {
    ProceduralParams params;
    params.strandCount     = 64;
    params.pointsPerStrand = 6;
    params.seed            = 42;
    auto a                 = generateOnPlane(0.4f, 0.4f, params);
    auto b                 = generateOnPlane(0.4f, 0.4f, params);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    CHECK_EQ(a.value().curveCount(), 64u);
    CHECK_EQ(a.value().pointCount(), 64u * 6u);
    CHECK_EQ(a.value().curveCount(), b.value().curveCount());
    CHECK_EQ(a.value().points()[0].position.x, b.value().points()[0].position.x);
    CHECK_EQ(a.value().points()[0].position.y, b.value().points()[0].position.y);
}

TEST_CASE("graphics.hair.ribbonTopology") {
    ProceduralParams params;
    params.strandCount     = 8;
    params.pointsPerStrand = 4;
    params.seed            = 7;
    auto strands           = generateOnPlane(0.3f, 0.3f, params);
    REQUIRE(strands.ok());
    RibbonParams ribbon;
    ribbon.widthScale = 2.f;
    auto mesh         = buildRibbons(strands.value(), ribbon);
    REQUIRE(mesh.ok());
    CHECK(mesh.value().vertexCount() > 0);
    CHECK(mesh.value().indexCount() > 0);
    CHECK_EQ(mesh.value().indexCount() % 3, 0);
    CHECK_EQ(mesh.value().posXYZ.size(), mesh.value().nrmXYZ.size());
    CHECK_EQ(mesh.value().uvST.size(), size_t(mesh.value().vertexCount()) * 2u);
}

TEST_CASE("graphics.hair.groomAssetLodSelection") {
    GroomGroup group;
    group.name    = "scalp";
    group.groupId = 1;

    ProceduralParams params;
    params.strandCount     = 16;
    params.pointsPerStrand = 4;
    params.seed            = 3;
    auto strands           = generateOnPlane(0.2f, 0.2f, params);
    REQUIRE(strands.ok());
    group.strands = std::move(strands).value();

    GroomLod nearLod;
    nearLod.screenSize    = 1.f;
    nearLod.curveFraction = 1.f;
    GroomLod farLod;
    farLod.screenSize     = 0.3f;
    farLod.curveFraction  = 0.25f;
    farLod.thicknessScale = 2.f;
    group.lods            = {nearLod, farLod};

    GroomAsset asset;
    auto       add = asset.addGroup(std::move(group));
    REQUIRE(add.ok());
    auto valid = asset.validate();
    CHECK(valid.ok());
    CHECK_EQ(selectLodIndex(asset.groupAt(0)->lods, 1.f), 0u);
    CHECK_EQ(selectLodIndex(asset.groupAt(0)->lods, 0.2f), 1u);
}

TEST_CASE("graphics.hair.groomAssetRejectsDuplicateId") {
    ProceduralParams params;
    params.strandCount     = 4;
    params.pointsPerStrand = 3;
    auto strands           = generateOnPlane(0.1f, 0.1f, params);
    REQUIRE(strands.ok());

    GroomGroup a;
    a.name    = "a";
    a.groupId = 9;
    a.strands = strands.value();
    // Need a second independent copy — regenerate.
    auto strands2 = generateOnPlane(0.1f, 0.1f, params);
    REQUIRE(strands2.ok());
    GroomGroup b;
    b.name    = "b";
    b.groupId = 9;
    b.strands = std::move(strands2).value();

    GroomAsset asset;
    REQUIRE(asset.addGroup(std::move(a)).ok());
    auto dup = asset.addGroup(std::move(b));
    CHECK(!dup.ok());
}

TEST_CASE("graphics.hair.clusterGridAabbAndCull") {
    StrandsDatas             data;
    std::vector<StrandPoint> points;
    std::vector<StrandCurve> curves;

    auto addStrand = [&](glm::vec3 root) {
        StrandCurve c;
        c.pointOffset = uint32_t(points.size());
        c.pointCount  = 2;
        c.length      = 0.1f;
        points.push_back({root, 0.002f, 0.f});
        points.push_back({root + glm::vec3(0.f, 0.1f, 0.f), 0.001f, 1.f});
        curves.push_back(c);
    };
    // Two spatial clumps far apart so cellSize 0.5 yields ≥2 clusters.
    addStrand(glm::vec3(-2.f, 0.f, 0.f));
    addStrand(glm::vec3(-1.9f, 0.f, 0.05f));
    addStrand(glm::vec3(2.f, 0.f, 0.f));
    addStrand(glm::vec3(2.1f, 0.f, -0.05f));
    data.setPoints(std::move(points));
    data.setCurves(std::move(curves));
    REQUIRE(data.validate().ok());

    ClusterGrid grid;
    auto        built = grid.build(data, 0.5f);
    REQUIRE(built.ok());
    CHECK(grid.clusterCount() >= 2u);

    glm::vec3 bmin, bmax;
    grid.computeBounds(bmin, bmax);
    CHECK(bmin.x < -1.f);
    CHECK(bmax.x > 1.f);

    // Camera looks at the left clump; right clump should cull out.
    const glm::mat4 view =
        glm::lookAtRH(glm::vec3(-2.f, 0.4f, 1.5f), glm::vec3(-2.f, 0.05f, 0.f), glm::vec3(0.f, 1.f, 0.f));
    const glm::mat4 proj     = glm::perspectiveRH_ZO(glm::radians(35.f), 1.f, 0.1f, 4.f);
    const glm::mat4 viewProj = proj * view;
    auto            visible  = grid.cullClusters(reinterpret_cast<const float *>(&viewProj[0][0]));
    REQUIRE(visible.ok());
    CHECK(!visible.value().empty());
    CHECK(visible.value().size() < grid.clusterCount());

    auto curvesLeft = grid.collectCurveIndices(visible.value());
    REQUIRE(curvesLeft.ok());
    auto filtered = filterStrandsByCurves(data, curvesLeft.value());
    REQUIRE(filtered.ok());
    CHECK(filtered.value().curveCount() > 0u);
    CHECK(filtered.value().curveCount() < data.curveCount());
}

TEST_CASE("graphics.hair.groomAssetLodRepresentationNone") {
    GroomGroup group;
    group.name    = "scalp";
    group.groupId = 2;

    ProceduralParams params;
    params.strandCount     = 24;
    params.pointsPerStrand = 4;
    params.seed            = 5;
    auto strands           = generateOnPlane(0.2f, 0.2f, params);
    REQUIRE(strands.ok());
    group.strands = std::move(strands).value();

    GroomLod nearLod;
    nearLod.screenSize     = 1.f;
    nearLod.representation = Representation::Strands;
    GroomLod farLod;
    farLod.screenSize     = 0.2f;
    farLod.representation = Representation::None;
    group.lods            = {nearLod, farLod};

    GroomAsset asset;
    REQUIRE(asset.addGroup(std::move(group)).ok());
    REQUIRE(asset.validate().ok());
    CHECK_EQ(selectLodIndex(asset.groupAt(0)->lods, 0.9f), 0u);
    CHECK_EQ(selectLodIndex(asset.groupAt(0)->lods, 0.1f), 1u);
    CHECK(asset.groupAt(0)->lods[1].representation == Representation::None);
}

TEST_CASE("graphics.hair.appendRibbonMeshRebasesIndices") {
    ProceduralParams params;
    params.strandCount     = 4;
    params.pointsPerStrand = 3;
    params.seed            = 11;
    auto a                 = generateOnPlane(0.15f, 0.15f, params);
    params.seed            = 12;
    auto b                 = generateOnPlane(0.15f, 0.15f, params);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto meshA = buildRibbons(a.value());
    auto meshB = buildRibbons(b.value());
    REQUIRE(meshA.ok());
    REQUIRE(meshB.ok());

    RibbonMesh combined  = meshA.value();
    const int  baseVerts = combined.vertexCount();
    const int  baseIdx   = combined.indexCount();
    appendRibbonMesh(combined, meshB.value());
    CHECK_EQ(combined.vertexCount(), baseVerts + meshB.value().vertexCount());
    CHECK_EQ(combined.indexCount(), baseIdx + meshB.value().indexCount());
    CHECK_EQ(combined.indexCount() % 3, 0);
    for (size_t i = size_t(baseIdx); i < combined.indices.size(); ++i) {
        CHECK(combined.indices[i] >= uint32_t(baseVerts));
        CHECK(combined.indices[i] < uint32_t(combined.vertexCount()));
    }
}

TEST_CASE("graphics.hair.groomAssetMultiGroup") {
    ProceduralParams params;
    params.strandCount     = 8;
    params.pointsPerStrand = 4;
    params.seed            = 21;
    auto scalp             = generateOnPlane(0.2f, 0.2f, params);
    params.seed            = 22;
    auto fringe            = generateOnPlane(0.1f, 0.1f, params);
    REQUIRE(scalp.ok());
    REQUIRE(fringe.ok());

    GroomLod lod;
    lod.screenSize     = 1.f;
    lod.representation = Representation::Strands;

    GroomGroup g0;
    g0.name    = "scalp";
    g0.groupId = 1;
    g0.strands = std::move(scalp).value();
    g0.lods    = {lod};

    GroomGroup g1;
    g1.name    = "fringe";
    g1.groupId = 2;
    g1.strands = std::move(fringe).value();
    g1.lods    = {lod};

    GroomAsset asset;
    REQUIRE(asset.addGroup(std::move(g0)).ok());
    REQUIRE(asset.addGroup(std::move(g1)).ok());
    REQUIRE(asset.validate().ok());
    CHECK_EQ(asset.groupCount(), 2u);
    CHECK_EQ(asset.groupAt(0)->name, std::string("scalp"));
    CHECK_EQ(asset.groupAt(1)->name, std::string("fringe"));
}


TEST_CASE("graphics.hair.groomBindingProjectsAndDeforms") {
    // Two strands rooted above a flat quad (two triangles) on XZ.
    StrandsDatas             strands;
    std::vector<StrandPoint> points;
    std::vector<StrandCurve> curves;
    auto                     add = [&](glm::vec3 root) {
        StrandCurve c;
        c.pointOffset = uint32_t(points.size());
        c.pointCount  = 3;
        points.push_back({root, 0.002f, 0.f});
        points.push_back({root + glm::vec3(0.f, 0.05f, 0.f), 0.0015f, 0.5f});
        points.push_back({root + glm::vec3(0.f, 0.1f, 0.f), 0.001f, 1.f});
        c.length = 0.1f;
        curves.push_back(c);
    };
    add(glm::vec3(-0.2f, 0.f, 0.f));
    add(glm::vec3(0.2f, 0.f, 0.f));
    strands.setPoints(std::move(points));
    strands.setCurves(std::move(curves));
    REQUIRE(strands.validate().ok());

    float restPos[] = {
        -0.5f, 0.f, -0.5f, 0.5f, 0.f, -0.5f, 0.5f, 0.f, 0.5f, -0.5f, 0.f, 0.5f,
    };
    uint32_t    idx[] = {0, 1, 2, 0, 2, 3};
    SkinTriMesh rest{restPos, 4, idx, 6};

    GroomBinding binding;
    auto         built = binding.build(strands, rest);
    REQUIRE(built.ok());
    CHECK_EQ(binding.rootCount(), 2u);
    REQUIRE(binding.rootAt(0) != nullptr);
    CHECK(binding.rootAt(0)->triangleIndex < 2u);

    // Lift the scalp by +0.25 on Y; Rigid deform should lift strand roots.
    float defPos[12];
    for (int i = 0; i < 4; ++i) {
        defPos[i * 3 + 0] = restPos[i * 3 + 0];
        defPos[i * 3 + 1] = restPos[i * 3 + 1] + 0.25f;
        defPos[i * 3 + 2] = restPos[i * 3 + 2];
    }
    SkinTriMesh deformed{defPos, 4, idx, 6};
    auto        moved = binding.deform(strands, deformed, BindingDeformMode::Rigid);
    REQUIRE(moved.ok());
    CHECK_EQ(moved.value().curveCount(), 2u);
    const float y0 = moved.value().curvePoints(0)[0].position.y;
    CHECK(y0 > 0.2f);
    CHECK(y0 < 0.3f);
    // Tip moves by the same delta under Rigid.
    const float tipDelta = moved.value().curvePoints(0)[2].position.y - strands.curvePoints(0)[2].position.y;
    CHECK(tipDelta > 0.2f);

    auto offset = binding.deform(strands, deformed, BindingDeformMode::Offset);
    REQUIRE(offset.ok());
    CHECK_EQ(offset.value().curveCount(), 2u);
}

TEST_CASE("graphics.hair.guideWeightsAndInterpolate") {
    ProceduralParams params;
    params.strandCount     = 24;
    params.pointsPerStrand = 5;
    params.seed            = 9;
    auto strands           = generateOnPlane(0.3f, 0.3f, params);
    REQUIRE(strands.ok());

    auto guides = extractGuides(strands.value(), 0.25f);
    REQUIRE(guides.ok());
    CHECK(guides.value().curveCount() >= 1u);
    CHECK(guides.value().curveCount() < strands.value().curveCount());

    auto weights = buildGuideWeights(strands.value(), guides.value(), 3, InterpolationMode::Offset);
    REQUIRE(weights.ok());
    CHECK_EQ(weights.value().size(), strands.value().curveCount());
    float wSum = 0.f;
    for (int i = 0; i < weights.value()[0].count; ++i) wSum += weights.value()[0].influencers[i].weight;
    CHECK(wSum > 0.99f);
    CHECK(wSum < 1.01f);

    // Translate all guide points by +0.1 X and interpolate.
    StrandsDatas             guidesDef = guides.value();
    std::vector<StrandPoint> gp(guidesDef.points().begin(), guidesDef.points().end());
    for (auto &p : gp) p.position.x += 0.1f;
    guidesDef.setPoints(std::move(gp));

    auto rigid =
        interpolateStrands(strands.value(), guides.value(), guidesDef, weights.value(), InterpolationMode::Rigid);
    REQUIRE(rigid.ok());
    const float dx = rigid.value().curvePoints(0)[0].position.x - strands.value().curvePoints(0)[0].position.x;
    CHECK(dx > 0.05f);

    auto offset =
        interpolateStrands(strands.value(), guides.value(), guidesDef, weights.value(), InterpolationMode::Offset);
    REQUIRE(offset.ok());
    auto smooth =
        interpolateStrands(strands.value(), guides.value(), guidesDef, weights.value(), InterpolationMode::Smooth);
    REQUIRE(smooth.ok());
    CHECK_EQ(smooth.value().curveCount(), strands.value().curveCount());
}


TEST_CASE("graphics.hair.guideSimulatorPinsRootsAndFalls") {
    StrandsDatas             guides;
    std::vector<StrandPoint> points;
    std::vector<StrandCurve> curves;
    auto                     add = [&](glm::vec3 root) {
        StrandCurve c;
        c.pointOffset = uint32_t(points.size());
        c.pointCount  = 4;
        for (int i = 0; i < 4; ++i) {
            const float u = float(i) / 3.f;
            points.push_back({root + glm::vec3(0.f, 0.04f * float(i), 0.f), 0.001f, u});
        }
        c.length = 0.12f;
        curves.push_back(c);
    };
    add(glm::vec3(-0.1f, 0.5f, 0.f));
    add(glm::vec3(0.1f, 0.5f, 0.f));
    guides.setPoints(std::move(points));
    guides.setCurves(std::move(curves));
    REQUIRE(guides.validate().ok());

    GuideSimParams params;
    params.gravity    = glm::vec3(0.f, -20.f, 0.f);
    params.damping    = 0.98f;
    params.iterations = 6;
    params.compliance = 0.f;
    params.collisionY = 0.f;

    GuideSimulator sim;
    auto           reset = sim.reset(guides, params);
    REQUIRE(reset.ok());
    CHECK(sim.isReady());
    CHECK_EQ(sim.curveCount(), 2u);
    CHECK_EQ(sim.particleCount(), 8u);

    const float rootY0 = guides.curvePoints(0)[0].position.y;
    for (int i = 0; i < 30; ++i) {
        auto stepped = sim.step(1.f / 60.f);
        REQUIRE(stepped.ok());
    }
    auto snap = sim.snapshot();
    REQUIRE(snap.ok());
    CHECK_EQ(snap.value().curvePoints(0)[0].position.y, rootY0);
    CHECK_EQ(snap.value().curvePoints(1)[0].position.y, rootY0);
    const float tipY = snap.value().curvePoints(0)[3].position.y;
    CHECK(tipY < rootY0 - 0.02f);
    CHECK(tipY >= -0.001f);

    CHECK(!sim.step(0.f).ok());
    CHECK(!sim.step(-0.1f).ok());
}
}  // namespace test_hair_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_indirect_builder)
#include "PrivateTestScope.h"
#include "graphics/GpuDrivenTypes.h"
#include "graphics/IndirectBuilder.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// indirect_builder.cpp
namespace test_indirect_builder {
using namespace eve::graphics;

namespace {

GpuMeshRecord makeMeshRecord(uint32_t indexCount) {
    GpuMeshRecord rec{};
    rec.indexCount = indexCount;
    rec.firstIndex = 0;
    rec.vertexBase = 0;
    rec.indexType  = 1;
    return rec;
}

}  // namespace

TEST_CASE("IndirectBuilder.mergesBuckets") {
    std::vector<GpuMeshRecord> table;
    table.push_back(makeMeshRecord(36));  // mesh 0
    table.push_back(makeMeshRecord(72));  // mesh 1

    IndirectBuilder b;
    b.add(0, 0, 0, 0);  // seq, mesh, material, pipeline
    b.add(1, 0, 0, 0);
    b.add(2, 1, 0, 0);
    b.add(3, 0, 1, 0);
    REQUIRE(b.build(table) == 3);

    const auto &cmds = b.commands();
    REQUIRE(cmds.size() == 3);
    CHECK(cmds[0].instanceCount == 2);  // material 0 + mesh 0
    CHECK(cmds[0].firstInstance == 0);
    CHECK(cmds[0].indexCount == 36);
    CHECK(cmds[1].instanceCount == 1);  // material 0 + mesh 1
    CHECK(cmds[1].firstInstance == 2);
    CHECK(cmds[1].indexCount == 72);
    CHECK(cmds[2].instanceCount == 1);  // material 1 + mesh 0
    CHECK(cmds[2].firstInstance == 3);

    const auto &order = b.sortedInstanceOrder();
    REQUIRE(order.size() == 4);
    CHECK(order[0] == 0);
    CHECK(order[1] == 1);
    CHECK(order[2] == 2);
    CHECK(order[3] == 3);
}

TEST_CASE("IndirectBuilder.stableOrderWithinBucket") {
    std::vector<GpuMeshRecord> table;
    table.push_back(makeMeshRecord(12));

    IndirectBuilder b;
    b.add(7, 0, 5, 0);
    b.add(3, 0, 5, 0);
    b.add(9, 0, 5, 0);
    REQUIRE(b.build(table) == 1);
    CHECK(b.commands()[0].instanceCount == 3);

    // Stable: sequence numbers inside the bucket drive the tiebreak (7, 3, 9 → 3, 7, 9).
    const auto &order = b.sortedInstanceOrder();
    REQUIRE(order.size() == 3);
    CHECK(order[0] == 1);
    CHECK(order[1] == 0);
    CHECK(order[2] == 2);
}

TEST_CASE("IndirectBuilder.pipelineSeparatesBuckets") {
    std::vector<GpuMeshRecord> table;
    table.push_back(makeMeshRecord(12));

    IndirectBuilder b;
    b.add(0, 0, 0, 1);
    b.add(1, 0, 0, 2);
    REQUIRE(b.build(table) == 2);  // same mesh+material, different pipeline
    CHECK(b.commands()[0].instanceCount == 1);
    CHECK(b.commands()[1].instanceCount == 1);
}

TEST_CASE("IndirectBuilder.skipsUnknownMesh") {
    std::vector<GpuMeshRecord> table;
    table.push_back(makeMeshRecord(12));

    IndirectBuilder b;
    b.add(0, 0, 0, 0);
    b.add(1, 99, 0, 0);  // meshId outside the table
    REQUIRE(b.build(table) == 1);
    CHECK(b.commands()[0].instanceCount == 1);
    CHECK(b.commands()[0].firstInstance == 0);
}
}  // namespace test_indirect_builder
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_reflection_probe_culling)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <glm/glm.hpp>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include "common/ECS.h"
#include "graphics/ReflectionProbeCapture.h"
#include "graphics/ReflectionProbeRegistry.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/shaders/ReflectionProbeWgsl.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// reflection_probe_culling.cpp
namespace test_reflection_probe_culling_unit {
// reflection_probe_culling.cpp
using namespace eve::graphics;
TEST_CASE("graphics.reflectionProbe.pcgDistanceCulling") {
    ReflectionProbeRegistry registry;
    ReflectionProbeCapture  nearProbe(nullptr), farProbe(nullptr);
    nearProbe.configure(3.f, 0.f, 0.f);
    farProbe.configure(30.f, 0.f, 0.f);
    registry.add(&nearProbe);
    registry.add(&farProbe);
    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 0.f, 0.f);
    camera->setTarget(0.f, 0.f, -1.f);
    REQUIRE(registry.setMaxRenderDistance(10.f).ok());
    registry.setDistanceCullingEnabled(true);
    registry.updateCamera(camera);
    CHECK_EQ(registry.getLastDistanceCulledCount(), 1);
    camera->setEye(25.f, 0.f, 0.f);
    camera->setTarget(25.f, 0.f, -1.f);
    registry.updateCamera(camera);
    CHECK_EQ(registry.getLastDistanceCulledCount(), 1);
    CHECK(!registry.setMaxRenderDistance(0.f).ok());
    CHECK(!registry.setMaxRenderDistance(std::numeric_limits<float>::infinity()).ok());
    CHECK_EQ(registry.getMaxRenderDistance(), 10.f);
    registry.setDistanceCullingEnabled(false);
    registry.updateCamera(camera);
    CHECK_EQ(registry.getLastDistanceCulledCount(), 0);
    ecs::DestroyEntity(camera);
}

// reflection_probe_quality.cpp
namespace {

constexpr float kPi = 3.14159265358979323846f;

[[maybe_unused]] float radicalInverse(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

[[maybe_unused]] glm::vec3 faceDirection(int face, float u, float v) {
    switch (face) {
        case 0: return glm::normalize(glm::vec3(1.f, -v, -u));
        case 1: return glm::normalize(glm::vec3(-1.f, -v, u));
        case 2: return glm::normalize(glm::vec3(u, 1.f, v));
        case 3: return glm::normalize(glm::vec3(u, -1.f, -v));
        case 4: return glm::normalize(glm::vec3(u, -v, 1.f));
        default: return glm::normalize(glm::vec3(-u, -v, -1.f));
    }
}

[[maybe_unused]] std::array<glm::vec3, 24 * 33> allFaceEdges() {
    std::array<glm::vec3, 24 * 33> result{};
    size_t                         out = 0;
    for (int face = 0; face < 6; ++face) {
        for (int edge = 0; edge < 4; ++edge) {
            for (int sample = 0; sample < 33; ++sample) {
                const float t = float(sample) / 16.f - 1.f;
                const float u = edge < 2 ? (edge == 0 ? -1.f : 1.f) : t;
                const float v = edge >= 2 ? (edge == 2 ? -1.f : 1.f) : t;
                result[out++] = faceDirection(face, u, v);
            }
        }
    }
    return result;
}

}  // namespace

TEST_CASE("graphics.reflectionProbe.hammersleyDeterministic") {
    CHECK(std::abs(radicalInverse(0u) - 0.f) < 1e-7f);
    CHECK(std::abs(radicalInverse(1u) - 0.5f) < 1e-7f);
    CHECK(std::abs(radicalInverse(2u) - 0.25f) < 1e-7f);
    CHECK(std::abs(radicalInverse(3u) - 0.75f) < 1e-7f);
    for (uint32_t i = 0; i < 512; ++i) {
        CHECK(float(i) / 512.f >= 0.f);
        CHECK(float(i) / 512.f < 1.f);
        CHECK(radicalInverse(i) >= 0.f);
        CHECK(radicalInverse(i) < 1.f);
    }
}

TEST_CASE("graphics.reflectionProbe.cubemapEdgesAreContinuous") {
    const auto       edges          = allFaceEdges();
    constexpr size_t samplesPerFace = 4u * 33u;
    for (size_t face = 0; face < 6; ++face) {
        for (size_t local = 0; local < samplesPerFace; ++local) {
            const glm::vec3 direction       = edges[face * samplesPerFace + local];
            float           bestAdjacentDot = -1.f;
            for (size_t otherFace = 0; otherFace < 6; ++otherFace) {
                if (otherFace == face) continue;
                for (size_t other = 0; other < samplesPerFace; ++other)
                    bestAdjacentDot =
                        std::max(bestAdjacentDot, glm::dot(direction, edges[otherFace * samplesPerFace + other]));
            }
            CHECK(bestAdjacentDot > 0.999999f);
        }
    }
}
}  // namespace test_reflection_probe_culling_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tree_wind)
#include "PrivateTestScope.h"
#include <algorithm>
#include <array>
#include <limits>
#include "graphics/Shader.h"
#include "graphics/TreeWind.h"
#include "graphics/VegetationWind.h"
#include "zeroerr/unittest.h"
// tree_wind.cpp
namespace test_tree_wind {
namespace {
void declareTreeLayout(eve::graphics::Shader &shader) {
    shader.declareVec4("windGlobals");
    shader.declareVec2("windPhaseDistance");
    shader.declareVec3("windFlex");
    shader.declareVec3("windFrequency");
    shader.declareVec2("windSineTime");
    shader.declareVec2("treeDimensions");
}
std::array<float, 32> snapshot(const eve::graphics::Shader &shader) {
    std::array<float, 32> values{};
    std::copy_n(shader.pushConstantData(), 32, values.begin());
    return values;
}
}  // namespace

TEST_CASE("graphics.TreeWind.appliesPcgBendAndDimensions") {
    using namespace eve::graphics;
    Shader shader;
    declareTreeLayout(shader);
    VegetationWindState   state{{1, -0.5F, 0.25F}, 0.8F, 0.17F};
    VegetationWindProfile vegetation;
    vegetation.enabled = true;
    TreeWindProfile tree{{4, 9}, 0.35F};
    REQUIRE(applyTreeWind(shader, state, vegetation, tree, 2).ok());
    auto packed = packVegetationWind(state, vegetation, 2);
    REQUIRE(packed.ok());
    CHECK(shader.usedFloats() == 16);
    CHECK(shader.pushConstantData()[0] == packed.value()[0]);
    CHECK(shader.pushConstantData()[6] == packed.value()[6] * tree.bendFactor);
    CHECK(shader.pushConstantData()[7] == packed.value()[7] * tree.bendFactor);
    CHECK(shader.pushConstantData()[8] == packed.value()[8] * tree.bendFactor);
    CHECK(shader.pushConstantData()[14] == 4);
    CHECK(shader.pushConstantData()[15] == 9);
}

TEST_CASE("graphics.TreeWind.validationAndSchemaFailuresAreAtomic") {
    using namespace eve::graphics;
    Shader shader;
    declareTreeLayout(shader);
    VegetationWindState   state{{1, 0, 0}, 1, 0.2F};
    VegetationWindProfile vegetation;
    vegetation.enabled = true;
    TreeWindProfile tree{{2, 6}, 0.5F};
    REQUIRE(applyTreeWind(shader, state, vegetation, tree, 0).ok());
    const auto before  = snapshot(shader);
    tree.widthHeight.y = 0;
    CHECK(!applyTreeWind(shader, state, vegetation, tree, 0).ok());
    CHECK(snapshot(shader) == before);
    tree = {{2, 6}, std::numeric_limits<float>::infinity()};
    CHECK(!applyTreeWind(shader, state, vegetation, tree, 0).ok());
    CHECK(snapshot(shader) == before);

    Shader wrong;
    wrong.declareVec4("windGlobals");
    wrong.declareVec2("windPhaseDistance");
    wrong.declareVec3("windFlex");
    wrong.declareVec3("windFrequency");
    wrong.declareVec2("wrongSineTime");
    wrong.declareVec2("treeDimensions");
    const auto wrongBefore = snapshot(wrong);
    tree                   = {{2, 6}, 0.5F};
    CHECK(!applyTreeWind(wrong, state, vegetation, tree, 0).ok());
    CHECK(snapshot(wrong) == wrongBefore);
}
}  // namespace test_tree_wind
#endif
