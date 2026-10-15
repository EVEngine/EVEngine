#include <array>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>
#include "asset/CanonicalImageCook.h"
#include "asset/RuntimeDefinition.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/PbrSurface.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.texture.explicit_mips_preserve_every_uploaded_level") {
    using namespace eve;
    using namespace eve::graphics;
    auto* gfx = Graphics::create();
    gfx->initHeadless(32, 32);
    std::cout << "Explicit mip contract backend: " << gfx->getBackendName() << '\n';
    auto* canvas = gfx->newCanvas(32, 32);
    REQUIRE(canvas != nullptr);
    asset_graphics::GraphicsImageFactoryAdapter images(*gfx);
    // Rectangular, non-power-of-two input also exercises the one-texel tail.
    const std::array<std::array<uint8_t, 4>, 3> colors{{{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}}};
    std::vector<uint8_t>                        bytes;
    unsigned                                    w = 7, h = 3;
    for (const auto& color : colors) {
        for (unsigned i = 0; i < w * h; ++i) bytes.insert(bytes.end(), color.begin(), color.end());
        w = std::max(w / 2, 1u);
        h = std::max(h / 2, 1u);
    }
    REQUIRE(!images.uploadRgba8MipChain(7, 3, 2, bytes).ok());
    REQUIRE(!images.uploadRgba8MipChain(7, 3, 3, std::span<const uint8_t>(bytes).first(bytes.size() - 1)).ok());
    REQUIRE(!images.uploadRgba8MipChain(0, 3, 3, bytes).ok());
    REQUIRE(!images.uploadRgba8MipChain(std::numeric_limits<uint32_t>::max(), 3, 3, bytes).ok());
    const std::string definition =
        R"({"schema":"eve.image","schemaVersion":3,"width":7,"height":3,"mipCount":3,"encoding":"rgba8-mips","color":{"transfer":"linear"}})";
    auto cooked = asset::cookCanonicalImageRgba8(
        {reinterpret_cast<const uint8_t*>(definition.data()), definition.size()}, bytes, 4096);
    REQUIRE(cooked.ok());
    auto metadata = Value::fromJson(std::string(cooked.value().definition.begin(), cooked.value().definition.end()));
    REQUIRE(metadata.ok());
    auto encoded = asset::encodeRuntimeDefinition(metadata.value());
    REQUIRE(encoded.ok());
    auto reference = AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(reference.ok());
    asset::EvpackBuild build;
    build.packageId                = reference.value().id().child("mip-probe-package");
    build.buildId                  = reference.value().id().child("mip-probe-build");
    const std::string backend      = gfx->getBackendName();
    const std::string shaderFormat = backend == "vulkan" ? "spirv-1.6" : "wgsl";
    build.variants                 = {{"windows", "x86_64", backend, {"rgba8"}, shaderFormat, "high", {}}};
    build.chunks                   = {{reference.value().id(),
                                       "eve.image",
                                       SchemaVersion(3),
                                       0,
                                       asset::EvpackChunkKind::Definition,
                                       0,
                                       asset::EvpackCodec::None,
                                       8,
                                       {},
                                       std::move(encoded).takeValue()},
                                      {reference.value().id(),
                                       "eve.image",
                                       SchemaVersion(3),
                                       0,
                                       asset::EvpackChunkKind::Bulk,
                                       1,
                                       asset::EvpackCodec::None,
                                       8,
                                       {},
                                       std::move(cooked).takeValue().bulk}};
    auto packed                    = asset::buildEvpack(std::move(build));
    REQUIRE(packed.ok());
    auto parsed = asset::parseEvpack(packed.value());
    REQUIRE(parsed.ok());
    asset::EvpackResourceReader       reader(std::make_shared<const asset::Evpack>(std::move(parsed).takeValue()));
    asset_graphics::EvpackImageLoader loader(reader, images);
    auto                              uploaded =
        loader.load(reference.value(), {"windows", "x86_64", backend, {"rgba8"}, {shaderFormat}, {"high"}, {}});
    REQUIRE(uploaded.ok());
    auto* texture = uploaded.value().texture;
    REQUIRE(texture != nullptr);
    CHECK(texture->mipmapCount == 3);
    for (unsigned level = 0; level < colors.size(); ++level) {
        auto sampler   = TextureSampler::linearMipmap();
        sampler.minLod = sampler.maxLod = float(level);
        gfx->setTextureSampler(texture, sampler);
        gfx->setCanvas(canvas);
        gfx->clear(Color(0, 0, 0, 1), std::nullopt, std::nullopt);
        gfx->drawTexturedRectUV(texture, 0, 0, 32, 32, 0, 0, 1, 1, Color(1, 1, 1, 1));
        gfx->setCanvas();
        std::unique_ptr<image::ImageData> pixels(canvas->newImageData());
        REQUIRE(pixels != nullptr);
        const auto pixel = pixels->getPixel(16, 16);
        CHECK(std::abs(pixel.r - colors[level][0] / 255.f) < .01f);
        CHECK(std::abs(pixel.g - colors[level][1] / 255.f) < .01f);
        CHECK(std::abs(pixel.b - colors[level][2] / 255.f) < .01f);
    }
    REQUIRE(images.releaseImage(texture).ok());
}

TEST_CASE("graphics.texture.rgba16f_array_upload_is_atomic_and_backend_explicit") {
    using namespace eve::graphics;
    auto* gfx = Graphics::create();
    gfx->initHeadless(16, 16);
    std::vector<uint16_t> pixels(2u * 3u * 4u * 4u, 0x3800u);
    REQUIRE(!gfx->newTextureArrayRgba16f(2, 3, 4, std::span<const uint16_t>(pixels).first(pixels.size() - 1)).ok());
    auto uploaded = gfx->newTextureArrayRgba16f(2, 3, 4, pixels);
    REQUIRE(uploaded.ok());
    CHECK(uploaded.value()->width == 2);
    CHECK(uploaded.value()->height == 3);
    CHECK(uploaded.value()->layers == 4);
    REQUIRE(gfx->releaseTexture(uploaded.value()));
}

TEST_CASE("graphics.texture.rgba8_volume_upload_is_atomic_and_backend_explicit") {
    using namespace eve::graphics;
    auto* gfx = Graphics::create();
    gfx->initHeadless(16, 16);
    std::vector<uint8_t> pixels(2u * 3u * 4u * 4u, 127u);
    REQUIRE(!gfx->newTexture3DRgba8(2, 3, 4, std::span<const uint8_t>(pixels).first(pixels.size() - 1)).ok());
    REQUIRE(!gfx->newTexture3DRgba8(0, 3, 4, pixels).ok());
    auto uploaded = gfx->newTexture3DRgba8(2, 3, 4, pixels);
    REQUIRE(uploaded.ok());
    CHECK(uploaded.value()->width == 2);
    CHECK(uploaded.value()->height == 3);
    CHECK(uploaded.value()->depth == 4);
    CHECK(uploaded.value()->sampler.repeatU);
    CHECK(uploaded.value()->sampler.repeatV);
    CHECK(uploaded.value()->sampler.repeatW);
    REQUIRE(gfx->releaseTexture(uploaded.value()));
}

TEST_CASE("graphics.pbrSurface.texture_dimensions_are_validated_before_publication") {
    using namespace eve::graphics;
    auto* gfx = Graphics::create();
    gfx->initHeadless(16, 16);
    const std::array<uint8_t, 4> rgba{255, 255, 255, 255};
    const std::array<uint16_t, 16> half{0x3c00u, 0x3c00u, 0x3c00u, 0x3c00u,
                                       0x3c00u, 0x3c00u, 0x3c00u, 0x3c00u,
                                       0x3c00u, 0x3c00u, 0x3c00u, 0x3c00u,
                                       0x3c00u, 0x3c00u, 0x3c00u, 0x3c00u};
    auto* texture2d = gfx->newTexture(1, 1, rgba.data());
    auto array = gfx->newTextureArrayRgba16f(1, 1, 4, half);
    auto volume = gfx->newTexture3DRgba8(1, 1, 1, rgba);
    REQUIRE(texture2d != nullptr);
    REQUIRE(array.ok());
    REQUIRE(volume.ok());

    PbrSurface surface;
    surface.textures[0].texture = array.value();
    REQUIRE(!gfx->setMesh3DPbrSurface(&surface).ok());
    surface.textures[0].texture = nullptr;
    surface.vegetationExtras.texture = texture2d;
    REQUIRE(!gfx->setMesh3DPbrSurface(&surface).ok());
    surface.vegetationExtras.texture = array.value();
    surface.vegetationExtras.layer = 3;
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    surface.vegetationExtras.layer = 4;
    REQUIRE(!gfx->setMesh3DPbrSurface(&surface).ok());
    surface.vegetationExtras = {};
    surface.vegetationAlpha.noise = texture2d;
    REQUIRE(!gfx->setMesh3DPbrSurface(&surface).ok());
    surface.vegetationAlpha.noise = volume.value();
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    surface.vegetationAlpha.noise = nullptr;
    surface.vegetationMotion.noise = volume.value();
    REQUIRE(!gfx->setMesh3DPbrSurface(&surface).ok());
    REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
    REQUIRE(gfx->releaseTexture(volume.value()));
    REQUIRE(gfx->releaseTexture(array.value()));
    REQUIRE(gfx->releaseTexture(texture2d));
}
