#include <cmath>
#include <memory>
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/PbrSurface.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::graphics;
TEST_CASE("graphics.vegetation.color_mask_requires_linear_binding_and_atomic_validity") {
    Material   material;
    PbrSurface surface;
    REQUIRE(material.setPbrSurface(surface).ok());
    surface.colorMaskEnabled = true;
    REQUIRE(!material.setPbrSurface(surface).ok());
    REQUIRE(!material.pbrSurface().colorMaskEnabled);
    surface.colorMaskEnabled = false;
    surface.colorMaskMin     = -.1f;
    surface.colorMaskMax     = .2f;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.colorMaskMin          = 0;
    surface.colorMaskMax          = 1;
    surface.albedoTextureStrength = 2;
    REQUIRE(!validatePbrSurface(surface).ok());
}
TEST_CASE("graphics.vegetation.color_mask_remaps_after_gpu_filtering") {
    auto* gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    auto* canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    const float    p[]       = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    n[]       = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]      = {.4f, .5f, .4f, .5f, .4f, .5f, .4f, .5f};
    const uint32_t indices[] = {0, 2, 1, 0, 3, 2};
    auto*          mesh      = gfx->newMeshFromArrays(p, n, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t pixels[] = {255, 255, 0, 0, 255, 255, 0, 255};
    auto*         mask     = gfx->newTexture(2, 1, pixels);
    REQUIRE(mask != nullptr);
    PbrSurface surface;
    surface.unlit              = true;
    surface.colorMaskEnabled   = true;
    surface.colorMaskSecondary = {0, 0, 1};
    surface.colorMaskMin       = .25f;
    surface.colorMaskMax       = .75f;
    auto& binding              = surface.textures[size_t(PbrTextureSlot::MetallicRoughness)];
    binding.texture            = mask;
    binding.srgbDecode         = false;
    binding.minFilter          = 9729;
    binding.magFilter          = 9729;
    REQUIRE(!gfx->Graphics::setMesh3DPbrSurface(&surface).ok());
    auto render = [&](const PbrSurface& current) {
        gfx->setMesh3DViewProj(glm::mat4(1));
        gfx->setMesh3DView(glm::mat4(1));
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DMaterial(0, 1);
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
        REQUIRE(gfx->setMesh3DPbrSurface(&current).ok());
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(1, 0, 0, 1));
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
        REQUIRE(image != nullptr);
        return image->getPixel(32, 32);
    };
    const auto mixed = render(surface);
    // UV .4 filters the two alpha endpoints to .3; remapping afterwards yields .09998.
    CHECK(std::abs(mixed.r - .1f) < .015f);
    CHECK(std::abs(mixed.b - .9f) < .015f);
    surface.colorMaskMin = .75f;
    surface.colorMaskMax = .25f;
    const auto inverted  = render(surface);
    CHECK(std::abs(inverted.r - .9f) < .015f);
    CHECK(std::abs(inverted.b - .1f) < .015f);
    surface.colorMaskMin     = .25f;
    surface.colorMaskMax     = .75f;
    surface.colorMaskEnabled = false;
    const auto primary       = render(surface);
    CHECK(primary.r > .98f);
    CHECK(primary.b < .02f);
    const uint8_t albedoPixel[] = {64, 255, 255, 255};
    auto*         albedo        = gfx->newTexture(1, 1, albedoPixel);
    REQUIRE(albedo != nullptr);
    surface.textures[0].texture    = albedo;
    surface.textures[0].srgbDecode = false;
    surface.albedoTextureStrength  = .5f;
    const auto halfway             = render(surface);
    CHECK(std::abs(halfway.r - .62549f) < .01f);
    surface.albedoTextureStrength = 0;
    CHECK(render(surface).r > .98f);
    surface.albedoTextureStrength = 1;
    CHECK(std::abs(render(surface).r - 64.f / 255.f) < .01f);
    surface.textures[0].texture = nullptr;
    surface.colorMaskEnabled    = true;
    const auto again            = render(surface);
    CHECK(std::abs(again.r - mixed.r) < .001f);
    CHECK(std::abs(again.b - mixed.b) < .001f);
}
TEST_CASE("graphics.vegetation.global_colors_matches_tve_linear_fragment_formula") {
    auto* gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    auto* canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    const float    p[]       = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    n[]       = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]      = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[] = {0, 2, 1, 0, 3, 2};
    auto*          mesh      = gfx->newMeshFromArrays(p, n, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    PbrSurface surface;
    surface.unlit                                = true;
    surface.vegetationColor.fieldColor           = {.1f, .2f, .3f, .5f};
    surface.vegetationColor.colorsMask           = 0;
    surface.vegetationColor.colorsVariation      = 0;
    surface.vegetationColor.vertexOcclusionAlpha = 1;
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->begin3DFrameToCanvas(canvas);
    gfx->setMesh3DMaterial(0, 1);
    gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
    gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(.4f, .4f, .4f, 1));
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image != nullptr);
    const auto pixel = image->getPixel(32, 32);
    CHECK(std::abs(pixel.r - .4f * .1f * 4.594794f) < .015f);
    CHECK(std::abs(pixel.g - .4f * .2f * 4.594794f) < .015f);
    CHECK(std::abs(pixel.b - .4f * .3f * 4.594794f) < .015f);

    surface.vegetationColor.globalColorMaskMinimum = .6f;
    surface.vegetationColor.globalColorMaskMaximum = .8f;
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    gfx->begin3DFrameToCanvas(canvas);
    gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(.4f, .4f, .4f, 1));
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> remappedImage(canvas->newImageData());
    REQUIRE(remappedImage != nullptr);
    const auto remapped = remappedImage->getPixel(32, 32);
    CHECK(std::abs(remapped.r - .4f) < .015f);
    CHECK(std::abs(remapped.g - .4f) < .015f);
    CHECK(std::abs(remapped.b - .4f) < .015f);
}
TEST_CASE("graphics.vegetation.detail_layer_matches_tve_replace_formula") {
    auto* gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    auto* canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    const float    p[]       = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    n[]       = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]      = {.5f, .5f, .5f, .5f, .5f, .5f, .5f, .5f};
    const uint32_t indices[] = {0, 2, 1, 0, 3, 2};
    auto*          mesh      = gfx->newMeshFromArrays(p, n, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t albedoPixel[] = {128, 64, 32, 255};
    const uint8_t normalPixel[] = {128, 128, 255, 255};
    auto*         albedo        = gfx->newTexture(1, 1, albedoPixel);
    auto*         normal        = gfx->newTexture(1, 1, normalPixel);
    REQUIRE(albedo != nullptr);
    REQUIRE(normal != nullptr);
    PbrSurface surface;
    surface.unlit              = true;
    auto& detail               = surface.vegetationDetail;
    detail.value               = 1;
    detail.blendMode           = 1;
    detail.blendMinimum        = 0;
    detail.blendMaximum        = 0;
    detail.textures[0].texture = albedo;
    detail.textures[1].texture = normal;
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->begin3DFrameToCanvas(canvas);
    gfx->setMesh3DMaterial(0, 1);
    gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
    gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(.2f, .3f, .4f, 1));
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image != nullptr);
    const auto pixel = image->getPixel(32, 32);
    auto       srgb  = [](float c) { return c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f); };
    CHECK(std::abs(pixel.r - srgb(128.f / 255.f)) < .015f);
    CHECK(std::abs(pixel.g - srgb(64.f / 255.f)) < .015f);
    CHECK(std::abs(pixel.b - srgb(32.f / 255.f)) < .015f);
}
