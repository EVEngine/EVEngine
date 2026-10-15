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
#include "procgen/GtsTerrainLod.h"
#include "procgen/GtsTerrainLodRuntime.h"
#include "procgen/PcgMeshLod.h"
#include "procgen/PcgMeshLodBackup.h"
#include "procgen/Procgen.h"
#include "window/Window.h"

#include <SDL2/SDL.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

using namespace eve::graphics;

namespace {

[[maybe_unused]] std::vector<uint8_t> makeSolid(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
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
    ent->setMeshLodCullDistance(100.f);CHECK_EQ(ent->getMeshLodCullDistance(),100.f);
    CHECK(mr->meshForDistance(99.9f)==lo);CHECK(mr->meshForDistance(100.f)==nullptr);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(100.f),-1);

    REQUIRE(ent->setMeshLodFadeWidth(0, .25f).ok());
    REQUIRE(ent->setMeshLodFadeWidth(2, .2f).ok());
    REQUIRE(ent->setMeshLodFadePolicy(2, false, .5f).ok());
    CHECK_EQ(ent->getMeshLodSecondaryLevelAtDistance(17.5f), 1);
    CHECK(std::abs(ent->getMeshLodSecondaryWeightAtDistance(17.5f) - .5f) < .0001f);
    CHECK_EQ(ent->getMeshLodSecondaryLevelAtDistance(95.f), -1);
    CHECK(std::abs(ent->getMeshLodSecondaryWeightAtDistance(95.f) - .5f) < .0001f);
    CHECK(!ent->setMeshLodFadeWidth(0, 1.1f).ok());
    CHECK(!ent->setMeshLodFadePolicy(0, true, .5f).ok());

    REQUIRE(ent->setMeshLodFadePolicy(2, true, 1.f).ok());
    REQUIRE(ent->advanceMeshLodTransition(5.f, 0.f).ok());
    REQUIRE(ent->advanceMeshLodTransition(25.f, 0.f).ok());
    CHECK_EQ(ent->getMeshLodSecondaryLevelAtDistance(25.f), 1);
    CHECK_EQ(ent->getMeshLodSecondaryWeightAtDistance(25.f), 0.f);
    REQUIRE(ent->advanceMeshLodTransition(25.f, .25f).ok());
    CHECK(std::abs(ent->getMeshLodSecondaryWeightAtDistance(25.f) - .25f) < .0001f);
    REQUIRE(ent->advanceMeshLodTransition(25.f, .75f).ok());
    CHECK_EQ(ent->getMeshLodSecondaryLevelAtDistance(25.f), -1);
    CHECK_EQ(ent->getMeshLodLevelAtDistance(25.f), 1);
    CHECK(!ent->advanceMeshLodTransition(-1.f, .1f).ok());

    ent->clearMeshLod();
    CHECK_EQ(ent->getMeshLodCount(), 0);
    CHECK(mr->meshForDistance(80.f) == hi);  // falls back to primary mesh
}
