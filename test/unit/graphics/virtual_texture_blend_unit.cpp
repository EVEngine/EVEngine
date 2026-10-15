#include "graphics/VirtualTextureBlend.h"

#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <memory>

using eve::graphics::VirtualTextureBlend;
using eve::graphics::VirtualTextureBlendConfig;
using eve::graphics::VirtualTextureBlendLayer;
using eve::graphics::VirtualTexturePageId;
using eve::image::ImageData;

namespace {

std::shared_ptr<ImageData> solid(float r, float g, float b, float a = 1.f) {
    auto image = std::make_shared<ImageData>(4, 4, "RGBA8");
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) image->setPixel(x, y, {r, g, b, a});
    return image;
}

}  // namespace

TEST_CASE("graphics.virtual_texture.validates_contract") {
    VirtualTextureBlendConfig invalid{};
    invalid.tileSize = 2;
    auto rejected    = VirtualTextureBlend::create(invalid);
    CHECK(!rejected.ok());

    VirtualTextureBlendConfig valid{};
    valid.width      = 8;
    valid.height     = 8;
    valid.tileSize   = 4;
    valid.borderSize = 1;
    auto created     = VirtualTextureBlend::create(valid);
    REQUIRE(created.ok());
    auto noLayers = created.value()->requestAlbedoPage(0, 0, 0);
    CHECK(!noLayers.ok());
}

TEST_CASE("graphics.virtual_texture.height_blends_and_normalizes_normals") {
    VirtualTextureBlendConfig config{8, 8, 4, 1, 8};
    auto                      created = VirtualTextureBlend::create(config);
    REQUIRE(created.ok());
    auto blend = std::move(created).value();

    VirtualTextureBlendLayer red{};
    red.albedo         = solid(1.f, 0.f, 0.f, 0.2f);
    red.normal         = solid(1.f, 0.5f, 0.5f);
    red.constantWeight = 1.f;
    red.heightContrast = 1.f;
    REQUIRE(blend->addLayer(red).ok());

    VirtualTextureBlendLayer blue{};
    blue.albedo         = solid(0.f, 0.f, 1.f, 0.8f);
    blue.normal         = solid(0.5f, 1.f, 0.5f);
    blue.constantWeight = 1.f;
    blue.heightContrast = 1.f;
    REQUIRE(blend->addLayer(blue).ok());

    auto albedo = blend->requestAlbedoPage(0, 0, 0);
    REQUIRE(albedo.ok());
    const auto color = albedo.value()->getPixel(2, 2);
    CHECK(color.b > 0.95f);
    CHECK(color.r < 0.05f);

    auto normal = blend->requestNormalPage(0, 0, 0);
    REQUIRE(normal.ok());
    const auto  encoded = normal.value()->getPixel(2, 2);
    const float nx      = encoded.r * 2.f - 1.f;
    const float ny      = encoded.g * 2.f - 1.f;
    const float nz      = encoded.b * 2.f - 1.f;
    CHECK(std::abs(std::sqrt(nx * nx + ny * ny + nz * nz) - 1.f) < 0.03f);
}

TEST_CASE("graphics.virtual_texture_pages_have_borders_and_lru_receipts_survive") {
    VirtualTextureBlendConfig config{12, 4, 4, 1, 1};
    auto                      created = VirtualTextureBlend::create(config);
    REQUIRE(created.ok());
    auto                     blend = std::move(created).value();
    VirtualTextureBlendLayer layer{};
    layer.albedo = solid(0.25f, 0.5f, 0.75f);
    REQUIRE(blend->addLayer(layer).ok());

    auto first = blend->requestAlbedoPage(0, 0, 0);
    REQUIRE(first.ok());
    CHECK_EQ(first.value()->getWidth(), 6);
    REQUIRE(blend->requestAlbedoPage(1, 0, 0).ok());
    CHECK_EQ(blend->stats().pageEvictions, 1u);
    CHECK(first.value()->getPixel(0, 0).g > 0.48f);

    auto baked = blend->bakeAlbedo();
    REQUIRE(baked.ok());
    CHECK_EQ(baked.value()->getWidth(), 12);
    CHECK_EQ(baked.value()->getHeight(), 4);
}

TEST_CASE("graphics.virtual_texture_builds_atomic_resident_set_with_fallback") {
    VirtualTextureBlendConfig config{8, 8, 4, 1, 2};
    auto                      created = VirtualTextureBlend::create(config);
    REQUIRE(created.ok());
    auto                     blend = std::move(created).value();
    VirtualTextureBlendLayer layer{};
    layer.albedo = solid(0.2f, 0.4f, 0.8f);
    REQUIRE(blend->addLayer(layer).ok());

    auto duplicate = blend->buildResidentSet({{0, 0}, {0, 0}});
    CHECK(!duplicate.ok());
    auto tooMany = blend->buildResidentSet({{0, 0}, {1, 0}, {0, 1}});
    CHECK(!tooMany.ok());

    auto built = blend->buildResidentSet({VirtualTexturePageId{1, 0}});
    REQUIRE(built.ok());
    const auto& set = built.value();
    CHECK_EQ(set.pageExtent, 6);
    CHECK_EQ(set.residentPages, 1u);
    CHECK_EQ(set.pageTable->getWidth(), 2);
    CHECK_EQ(set.pageTable->getHeight(), 2);
    CHECK(set.pageTable->getPixel(0, 0).b < 0.05f);
    CHECK(set.pageTable->getPixel(1, 0).b > 0.95f);
    CHECK(set.albedoAtlas->getPixel(0, 0).b > 0.75f);
    CHECK(set.normalAtlas->getPixel(0, 0).b > 0.95f);
}
