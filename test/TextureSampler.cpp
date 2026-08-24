#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "Fixtures.h"
#include "common/Exception.h"
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
#include "graphics/RenderSystem3D.h"
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/TextureSampler.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "image/ImageData.h"
#include "window/Window.h"

#include <SDL2/SDL.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {

std::vector<uint8_t> makeSolid(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    return px;
}

}  // namespace

TEST_CASE("TextureSampler.mipmapCountForSize") {
    CHECK_EQ(mipmapCountForSize(1, 1), 1);
    CHECK_EQ(mipmapCountForSize(2, 2), 2);
    CHECK_EQ(mipmapCountForSize(256, 128), 9);
    CHECK_EQ(mipmapCountForSize(64, 64), 7);
}

TEST_CASE("TextureSampler.parseFilterAndMipmap") {
    CHECK_EQ(static_cast<int>(TextureSampler::parseFilter("nearest")),
             static_cast<int>(FilterMode::Nearest));
    CHECK_EQ(static_cast<int>(TextureSampler::parseFilter("linear")),
             static_cast<int>(FilterMode::Linear));
    CHECK_EQ(static_cast<int>(TextureSampler::parseMipmap("none")),
             static_cast<int>(MipmapMode::Disabled));
    CHECK_EQ(static_cast<int>(TextureSampler::parseMipmap("linear")),
             static_cast<int>(MipmapMode::Linear));
    CHECK_EQ(static_cast<int>(TextureSampler::parseMipmap("nearest")),
             static_cast<int>(MipmapMode::Nearest));
}

TEST_CASE("GraphicsSmoke.textureMipmapsAndAnisotropy") {
    GfxFixture fx(320, 240, /*useHeadless=*/true);
    auto px = makeSolid(64, 64, 200, 40, 40);

    TextureCreateInfo info = TextureCreateInfo::withMipmaps(true, 8.f);
    Texture *tex = fx.gfx->newTexture(64, 64, px.data(), info);
    REQUIRE(tex != nullptr);
    CHECK_EQ(tex->getMipmapCount(), mipmapCountForSize(64, 64));
    CHECK_EQ(static_cast<int>(tex->getSampler().mipmap), static_cast<int>(MipmapMode::Linear));
    CHECK(tex->getSampler().maxAnisotropy >= 8.f - 1e-3f);

    const float deviceMax = fx.gfx->getMaxAnisotropy();
    CHECK(deviceMax >= 1.f);

    // Nearest pixel-art style sampler without rebuilding image.
    fx.gfx->setTextureSampler(tex, TextureSampler::nearest());
    CHECK_EQ(static_cast<int>(tex->getSampler().min), static_cast<int>(FilterMode::Nearest));
    CHECK_EQ(static_cast<int>(tex->getSampler().mag), static_cast<int>(FilterMode::Nearest));

    // Draw once to exercise descriptor rewrite path.
    fx.gfx->setBackgroundColor(Color(0.05f, 0.05f, 0.08f, 1.f));
    fx.gfx->clear(std::nullopt, std::nullopt, std::nullopt);
    fx.gfx->drawTexturedRect(tex, 10.f, 10.f, 64.f, 64.f, Color(1, 1, 1, 1));
    fx.gfx->present();
}

TEST_CASE("GraphicsSmoke.setTextureSamplerValidatesStrings") {
    GfxFixture fx(320, 240, /*useHeadless=*/true);
    auto px = makeSolid(16, 16, 120, 120, 120);
    Texture *tex = fx.gfx->newTexture(16, 16, px.data());
    REQUIRE(tex != nullptr);

    // Valid string update through the script-facing name.
    fx.gfx->setTextureSampler(tex, "nearest", "none", 1.f, 0.f);
    CHECK_EQ(static_cast<int>(tex->getSampler().min), static_cast<int>(FilterMode::Nearest));

    fx.gfx->setTextureSampler(tex, "linear", "linear", 8.f, 0.f);
    CHECK_EQ(static_cast<int>(tex->getSampler().mipmap), static_cast<int>(MipmapMode::Linear));

    // Typos fail fast instead of silently falling back to linear.
    bool threw = false;
    try {
        fx.gfx->setTextureSampler(tex, "linar", "none", 1.f, 0.f);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        fx.gfx->setTextureSampler(tex, "linear", "mip", 1.f, 0.f);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("GraphicsSmoke.cubemapGeneratesMipChain") {
    GfxFixture fx(320, 240, /*useHeadless=*/true);
    const int face = 16;
    std::vector<uint8_t> faces(size_t(face) * face * 4 * 6, 128);
    Texture *cube = fx.gfx->newCubemap(face, faces.data());
    REQUIRE(cube != nullptr);
    CHECK_EQ(cube->getMipmapCount(), mipmapCountForSize(face, face));
    CHECK_EQ(static_cast<int>(cube->getSampler().mipmap), static_cast<int>(MipmapMode::Linear));
}

TEST_CASE("Renderable3D.meshLodSelectsByDistance") {
    auto *ent = Renderable3D::create();
    REQUIRE(ent != nullptr);

    // Use distinct non-null-ish pointers via cast of integers — only identity matters.
    Mesh *hi = reinterpret_cast<Mesh *>(uintptr_t(0x1000));
    Mesh *mid = reinterpret_cast<Mesh *>(uintptr_t(0x2000));
    Mesh *lo = reinterpret_cast<Mesh *>(uintptr_t(0x3000));

    ent->setMeshLod(0, hi);
    ent->setMeshLod(1, mid, 20.f);
    ent->setMeshLod(2, lo, 50.f);
    CHECK_EQ(ent->getMeshLodCount(), 3);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(0.f), 0);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(19.9f), 0);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(20.f), 1);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(49.9f), 1);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(50.f), 2);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(999.f), 2);

    auto mr = ent->meshRenderer();
    CHECK(mr->meshForDistance(5.f) == hi);
    CHECK(mr->meshForDistance(25.f) == mid);
    CHECK(mr->meshForDistance(80.f) == lo);

    ent->clearMeshLod();
    CHECK_EQ(ent->getMeshLodCount(), 0);
    CHECK(mr->meshForDistance(80.f) == hi);  // falls back to primary mesh
}
