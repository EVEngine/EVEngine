#include "zeroerr/assert.h"

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
namespace test_graphics_vegetation_extras {
using namespace eve::graphics;


TEST_CASE("graphics.vegetation.vertex_occlusion_rgb_matches_tve_remap") {
    auto *gfx = Graphics::create();
    gfx->initHeadless(64, 32);

    const float    positions[] = {-.95f, -.9f, .5f, .95f, -.9f, .5f, .95f, .9f, .5f, -.95f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto          *canvas      = gfx->newCanvas(64, 32);
    REQUIRE(mesh != nullptr);
    REQUIRE(canvas != nullptr);
    std::vector<float> factors{.5f, 0, 0, 0, 0, .5f, 1, 1, 0, 0, .5f, 1, 1, 0, 0, .5f, 0, 0, 0, 0};
    REQUIRE(mesh->adoptVegetationFactors(std::move(factors)).ok());

    PbrSurface surface;
    surface.unlit                                  = true;
    surface.vegetationColor.vertexOcclusionColor   = {.2f, .4f, .6f};
    surface.vegetationColor.vertexOcclusionMinimum = .2f;
    surface.vegetationColor.vertexOcclusionMaximum = .8f;
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->begin3DFrameToCanvas(canvas);
    gfx->setMesh3DMaterial(0, 1);
    gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
    gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(1, 1, 1, 1));
    REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
    gfx->end3DFrameToCanvas();

    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image != nullptr);
    const auto left  = image->getPixel(8, 16);
    const auto right = image->getPixel(56, 16);
    CHECK(std::abs(left.r - .2f) < .02f);
    CHECK(std::abs(left.g - .4f) < .02f);
    CHECK(std::abs(left.b - .6f) < .02f);
    CHECK(right.r > .98f);
    CHECK(right.g > .98f);
    CHECK(right.b > .98f);
}

TEST_CASE("graphics.vegetation.backface_normal_modes_match_tve_facing_branch") {
    auto *gfx = Graphics::create();
    gfx->initHeadless(32, 32);

    const float    positions[] = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 1, 2, 0, 2, 3};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto          *canvas      = gfx->newCanvas(32, 32);
    REQUIRE(mesh != nullptr);
    REQUIRE(canvas != nullptr);
    Lighting3DPack lighting{};
    lighting.count               = 1;
    lighting.lights[0].posRadius = {0, 0, 1, 0};
    lighting.lights[0].color     = {1, 1, 1, 0};
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->setMesh3DCameraPos({0, 0, 2});

    auto render = [&](PbrVegetationBackfaceNormalMode mode) {
        PbrSurface surface;
        surface.specularFactor                     = 0;
        surface.vegetationColor.backfaceNormalMode = mode;
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DLighting(lighting);
        gfx->setMesh3DMaterial(0, 1);
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
        REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(1, 1, 1, 1));
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
        REQUIRE(image != nullptr);
        return image->getPixel(16, 16);
    };
    const auto flipped  = render(PbrVegetationBackfaceNormalMode::Flip);
    const auto mirrored = render(PbrVegetationBackfaceNormalMode::Mirror);
    const auto same     = render(PbrVegetationBackfaceNormalMode::Same);
    CHECK(same.r > flipped.r + .15f);
    CHECK(std::abs(mirrored.r - same.r) < .02f);
}

TEST_CASE("graphics.vegetation.pbr_front_face_culling_matches_tve_render_cull") {
    auto *gfx = Graphics::create();
    gfx->initHeadless(32, 32);
    const float    positions[] = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto          *canvas      = gfx->newCanvas(32, 32);
    REQUIRE(mesh != nullptr);
    REQUIRE(canvas != nullptr);
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    auto render = [&](PbrCullMode cull) {
        PbrSurface surface;
        surface.unlit    = true;
        surface.cullMode = cull;
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DMaterial(0, 1);
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
        REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(1, 1, 1, 1));
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
        REQUIRE(image != nullptr);
        return image->getPixel(16, 16).r;
    };
    const auto none  = render(PbrCullMode::None);
    const auto back  = render(PbrCullMode::Back);
    const auto front = render(PbrCullMode::Front);
    CHECK(none > .98f);
    CHECK(back > .98f);
    CHECK(front < .02f);
}

TEST_CASE("graphics.vegetation.extras_array_samples_world_position_per_fragment") {
    auto *gfx = Graphics::create();
    gfx->initHeadless(64, 32);

    VegetationField   field;
    VegetationGlobals globals;
    globals.extras = {1, 0, 0, 1};
    VegetationElement right;
    right.channel  = VegetationChannel::Extras;
    right.shape    = VegetationShape::Box;
    right.center   = {.5f, 0, 0};
    right.extents  = {.5f, 1, 1};
    right.edgeFade = 0;
    right.value    = {1, 0, 1, 0};
    REQUIRE(field.replace(globals, std::span(&right, 1)).ok());
    auto atlas = uploadVegetationExtrasAtlas(*gfx, field, {0, 0, 0}, {1, 1, 1}, 2, 1, false);
    REQUIRE(atlas.ok());

    const float    positions[] = {-.95f, -.9f, .5f, .95f, -.9f, .5f, .95f, .9f, .5f, -.95f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto          *canvas      = gfx->newCanvas(64, 32);
    REQUIRE(mesh != nullptr);
    REQUIRE(canvas != nullptr);

    PbrSurface surface;
    surface.unlit                             = true;
    surface.vegetationColor.overlayColor      = {1, 0, 0};
    surface.vegetationColor.overlay           = 1;
    surface.vegetationColor.overlayProjection = 0;
    surface.vegetationExtras.texture          = atlas.value().texture;
    surface.vegetationExtras.usage[0]         = 1;
    surface.vegetationExtras.coords           = {.5f, .5f, .5f, .5f};
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->begin3DFrameToCanvas(canvas);
    gfx->setMesh3DMaterial(0, 1);
    gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
    gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(0, 1, 0, 1));
    REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
    gfx->end3DFrameToCanvas();

    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image != nullptr);
    const auto leftPixel  = image->getPixel(12, 16);
    const auto rightPixel = image->getPixel(52, 16);
    CHECK(leftPixel.g > .95f);
    CHECK(leftPixel.r < .05f);
    CHECK(rightPixel.r > .95f);
    CHECK(rightPixel.g < .05f);

    surface.vegetationAlpha.enabled   = true;
    surface.vegetationAlpha.global    = 1;
    surface.vegetationAlpha.variation = 0;
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    gfx->begin3DFrameToCanvas(canvas);
    gfx->setMesh3DMaterial(0, 1);
    gfx->setMesh3DSurface(SurfaceMode::Masked, BlendMode::Opaque, true, true, .5f, "cutoff");
    gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(0, 1, 0, 1));
    REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
    gfx->end3DFrameToCanvas();
    image.reset(canvas->newImageData());
    REQUIRE(image != nullptr);
    CHECK(image->getPixel(12, 16).g > .95f);
    CHECK(std::abs(image->getPixel(52, 16).r - image->getPixel(0, 0).r) < .005f);
    REQUIRE(gfx->releaseTexture(atlas.value().texture));
}

TEST_CASE("graphics.vegetation.alpha_fade_samples_volume_noise_after_local_controls") {
    auto *gfx = Graphics::create();
    gfx->initHeadless(64, 64);

    VegetationField   field;
    VegetationGlobals globals;
    globals.extras = {1, 0, 0, 1};
    REQUIRE(field.replace(globals, {}).ok());
    auto atlas = uploadVegetationExtrasAtlas(*gfx, field, {0, 0, 0}, {1, 1, 1}, 1, 1, false);
    REQUIRE(atlas.ok());
    const std::array<uint8_t, 4> halfNoise{128, 0, 0, 255};
    auto                         noise = gfx->newTexture3DRgba8(1, 1, 1, halfNoise);
    REQUIRE(noise.ok());

    const float    positions[] = {-.8f, -.8f, .5f, .8f, -.8f, .5f, .8f, .8f, .5f, -.8f, .8f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto          *visible     = gfx->newCanvas(64, 64);
    auto          *clipped     = gfx->newCanvas(64, 64);
    REQUIRE(mesh != nullptr);
    REQUIRE(visible != nullptr);
    REQUIRE(clipped != nullptr);

    PbrSurface surface;
    surface.unlit                     = true;
    surface.vegetationExtras.texture  = atlas.value().texture;
    surface.vegetationExtras.usage[0] = 1;
    surface.vegetationExtras.coords   = {.5f, .5f, .5f, .5f};
    surface.vegetationAlpha.noise     = noise.value();
    surface.vegetationAlpha.global    = 1;
    surface.vegetationAlpha.camera    = 0;
    surface.vegetationAlpha.glancing  = 0;
    surface.vegetationAlpha.enabled   = true;

    auto draw = [&](Canvas *canvas, float constantFade) {
        surface.vegetationAlpha.constant = constantFade;
        REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
        gfx->setMesh3DViewProj(glm::mat4(1));
        gfx->setMesh3DView(glm::mat4(1));
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DMaterial(0, 1);
        gfx->setMesh3DSurface(SurfaceMode::Masked, BlendMode::Opaque, true, true, .1f, "cutoff");
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(0, 1, 0, 1));
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
    };
    draw(visible, .25f);
    draw(clipped, .75f);

    std::unique_ptr<eve::image::ImageData> visibleImage(visible->newImageData());
    std::unique_ptr<eve::image::ImageData> clippedImage(clipped->newImageData());
    REQUIRE(visibleImage != nullptr);
    REQUIRE(clippedImage != nullptr);
    CHECK(visibleImage->getPixel(32, 32).g > .95f);
    CHECK(std::abs(clippedImage->getPixel(32, 32).g - clippedImage->getPixel(0, 0).g) < .005f);
    REQUIRE(gfx->releaseTexture(noise.value()));
    REQUIRE(gfx->releaseTexture(atlas.value().texture));
}

TEST_CASE("graphics.vegetation.emission_matches_tve_texture_remap_phase_and_global_field") {
    auto *gfx = Graphics::create();
    gfx->initHeadless(64, 32);

    VegetationField   field;
    VegetationGlobals globals;
    globals.extras = {.5f, 0, 0, 1};
    REQUIRE(field.replace(globals, {}).ok());
    auto atlas = uploadVegetationExtrasAtlas(*gfx, field, {0, 0, 0}, {1, 1, 1}, 1, 1, false);
    REQUIRE(atlas.ok());

    const std::array<uint8_t, 8> sourcePixels{64, 64, 64, 255, 192, 192, 192, 255};
    const float rightMask   = std::clamp((192.f / 255.f - .2f) / (.8f - .2f + .0001f) + .5f * .75f - 1.f, 0.f, 1.f);
    const auto  transformed = uint8_t(std::lround(rightMask * 255));
    const std::array<uint8_t, 8> referencePixels{0, 0, 0, 255, transformed, transformed, transformed, 255};
    auto                        *sourceTexture    = gfx->newTexture(2, 1, sourcePixels.data());
    auto                        *referenceTexture = gfx->newTexture(2, 1, referencePixels.data());
    REQUIRE(sourceTexture != nullptr);
    REQUIRE(referenceTexture != nullptr);

    const float    positions[] = {-.95f, -.9f, .5f, .95f, -.9f, .5f, .95f, .9f, .5f, -.95f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto          *actual      = gfx->newCanvas(64, 32);
    auto          *reference   = gfx->newCanvas(64, 32);
    REQUIRE(mesh != nullptr);
    REQUIRE(actual != nullptr);
    REQUIRE(reference != nullptr);

    auto draw = [&](Canvas *canvas, PbrSurface &surface) {
        auto &binding      = surface.textures[size_t(PbrTextureSlot::Emissive)];
        binding.srgbDecode = false;
        binding.minFilter  = 9728;
        binding.magFilter  = 9728;
        REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
        gfx->setMesh3DViewProj(glm::mat4(1));
        gfx->setMesh3DView(glm::mat4(1));
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DMaterial(0, 1);
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(0, 0, 0, 1));
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
    };

    PbrSurface surface;
    surface.emissive                                           = {.25f, .5f, 1.f};
    surface.emissiveStrength                                   = .8f;
    surface.textures[size_t(PbrTextureSlot::Emissive)].texture = sourceTexture;
    surface.vegetationExtras.texture                           = atlas.value().texture;
    surface.vegetationExtras.usage[0]                          = 1;
    surface.vegetationExtras.coords                            = {.5f, .5f, .5f, .5f};
    surface.vegetationEmission                                 = {.2f, .8f, .75f, 1.f, true};
    draw(actual, surface);

    PbrSurface referenceSurface;
    referenceSurface.emissive                                           = surface.emissive;
    referenceSurface.emissiveStrength                                   = surface.emissiveStrength;
    referenceSurface.textures[size_t(PbrTextureSlot::Emissive)].texture = referenceTexture;
    draw(reference, referenceSurface);

    std::unique_ptr<eve::image::ImageData> actualImage(actual->newImageData());
    std::unique_ptr<eve::image::ImageData> referenceImage(reference->newImageData());
    REQUIRE(actualImage != nullptr);
    REQUIRE(referenceImage != nullptr);
    for (const int x : {12, 52}) {
        const auto actualPixel    = actualImage->getPixel(x, 16);
        const auto referencePixel = referenceImage->getPixel(x, 16);
        CHECK(std::abs(actualPixel.r - referencePixel.r) < .006f);
        CHECK(std::abs(actualPixel.g - referencePixel.g) < .006f);
        CHECK(std::abs(actualPixel.b - referencePixel.b) < .006f);
    }
    CHECK(actualImage->getPixel(12, 16).r < .02f);
    CHECK(actualImage->getPixel(52, 16).b > .2f);
    REQUIRE(gfx->releaseTexture(sourceTexture));
    REQUIRE(gfx->releaseTexture(referenceTexture));
    REQUIRE(gfx->releaseTexture(atlas.value().texture));
}

TEST_CASE("graphics.vegetation.gradient_uses_mesh_height_and_blended_blue_mask") {
    auto *gfx = Graphics::create();
    gfx->initHeadless(64, 32);

    const float    positions[] = {-.95f, -.9f, .5f, .95f, -.9f, .5f, .95f, .9f, .5f, -.95f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto          *canvas      = gfx->newCanvas(64, 32);
    REQUIRE(mesh != nullptr);
    REQUIRE(canvas != nullptr);
    std::vector<float> factors{.5f, 1, 0, 0, 0, .5f, 1, 1, 0, 0, .5f, 1, 1, 0, 0, .5f, 1, 0, 0, 0};
    REQUIRE(mesh->adoptVegetationFactors(std::move(factors)).ok());

    PbrSurface surface;
    surface.unlit                       = true;
    surface.vegetationGradient.enabled  = true;
    surface.vegetationGradient.colorOne = {1, 0, 0};
    surface.vegetationGradient.colorTwo = {0, 0, 1};
    surface.vegetationGradient.minimum  = .2f;
    surface.vegetationGradient.maximum  = .8f;
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->begin3DFrameToCanvas(canvas);
    gfx->setMesh3DMaterial(0, 1);
    gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
    gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(1, 1, 1, 1));
    REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
    gfx->end3DFrameToCanvas();

    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image != nullptr);
    const auto left  = image->getPixel(8, 16);
    const auto right = image->getPixel(56, 16);
    CHECK(left.b > .9f);
    CHECK(left.r < .1f);
    CHECK(right.r > .9f);
    CHECK(right.b < .1f);
}

TEST_CASE("graphics.vegetation.colors_array_samples_world_position_per_fragment") {
    auto *gfx = Graphics::create();
    gfx->initHeadless(64, 32);

    VegetationField   field;
    VegetationGlobals globals;
    globals.color = {.2f, 0, 0, 1};
    VegetationElement right;
    right.channel  = VegetationChannel::Color;
    right.shape    = VegetationShape::Box;
    right.center   = {.5f, 0, 0};
    right.extents  = {.5f, 1, 1};
    right.edgeFade = 0;
    right.value    = {0, 0, .2f, 1};
    REQUIRE(field.replace(globals, std::span(&right, 1)).ok());
    auto atlas = uploadVegetationColorsAtlas(*gfx, field, {0, 0, 0}, {1, 1, 1}, 2, 1, false);
    REQUIRE(atlas.ok());

    const float    positions[] = {-.95f, -.9f, .5f, .95f, -.9f, .5f, .95f, .9f, .5f, -.95f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto          *canvas      = gfx->newCanvas(64, 32);
    REQUIRE(mesh != nullptr);
    REQUIRE(canvas != nullptr);

    PbrSurface surface;
    surface.unlit                                = true;
    surface.vegetationColor.colorsMask           = 0;
    surface.vegetationColor.colorsVariation      = 0;
    surface.vegetationColor.vertexOcclusionAlpha = 1;
    surface.vegetationColors.texture             = atlas.value().texture;
    surface.vegetationColors.usage[0]            = 1;
    surface.vegetationColors.coords              = {.5f, .5f, .5f, .5f};
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->begin3DFrameToCanvas(canvas);
    gfx->setMesh3DMaterial(0, 1);
    gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
    gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(.5f, .5f, .5f, 1));
    REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image != nullptr);
    const auto leftPixel  = image->getPixel(12, 16);
    const auto rightPixel = image->getPixel(52, 16);
    CHECK(std::abs(leftPixel.r - .4594794f) < .02f);
    CHECK(leftPixel.b < .02f);
    CHECK(std::abs(rightPixel.b - .4594794f) < .02f);
    CHECK(rightPixel.r < .02f);
    REQUIRE(gfx->releaseTexture(atlas.value().texture));
}

TEST_CASE("graphics.vegetation.vertex_array_scales_each_object_from_its_world_pivot") {
    auto *gfx = Graphics::create();
    gfx->initHeadless(96, 64);

    VegetationField   field;
    VegetationGlobals globals;
    globals.vertex = {0, 0, 0, .5f};
    VegetationElement right;
    right.channel  = VegetationChannel::Vertex;
    right.shape    = VegetationShape::Box;
    right.center   = {.5f, 0, 0};
    right.extents  = {.5f, 1, 1};
    right.edgeFade = 0;
    right.value    = {0, 0, 0, 1};
    REQUIRE(field.replace(globals, std::span(&right, 1)).ok());
    auto atlas = uploadVegetationVertexAtlas(*gfx, field, {0, 0, 0}, {1, 1, 1}, 2, 1, false);
    REQUIRE(atlas.ok());

    const float    positions[] = {-.15f, -.3f, .5f, .15f, -.3f, .5f, .15f, .9f, .5f, -.15f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto          *canvas      = gfx->newCanvas(96, 64);
    REQUIRE(mesh != nullptr);
    REQUIRE(canvas != nullptr);
    std::vector<float> deformation;
    for (int i = 0; i < 4; ++i) deformation.insert(deformation.end(), {0, .3f, 0, 0, 0, 0, .5f, 1, 1});
    REQUIRE(mesh->adoptVegetationDeformationFactors(std::move(deformation)).ok());

    PbrSurface surface;
    surface.unlit                        = true;
    surface.vegetationVertex.texture     = atlas.value().texture;
    surface.vegetationVertex.usage[0]    = 1;
    surface.vegetationVertex.coords      = {.5f, .5f, .5f, .5f};
    surface.vegetationVertex.source      = PbrVegetationDeformationSource::GpuFields;
    surface.vegetationVertex.sizeFadeEnd = 100;
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->begin3DFrameToCanvas(canvas);
    gfx->setMesh3DMaterial(0, 1);
    gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
    gfx->drawMesh(mesh, glm::translate(glm::mat4(1), glm::vec3(-.5f, 0, 0)), nullptr, Color(0, 1, 0, 1));
    gfx->drawMesh(mesh, glm::translate(glm::mat4(1), glm::vec3(.5f, 0, 0)), nullptr, Color(0, 1, 0, 1));
    REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
    gfx->end3DFrameToCanvas();

    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image != nullptr);
    std::array<int, 2> count{}, ySum{};
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 96; ++x)
            if (const auto pixel = image->getPixel(x, y); std::max({pixel.r, pixel.g, pixel.b}) > .2f) {
                const auto half = x < 48 ? 0 : 1;
                ++count[half];
                ySum[half] += y;
            }
#if !defined(__APPLE__)
    REQUIRE(count[1] >= 20);
    REQUIRE(count[0] >= 20);
    REQUIRE(count[1] > count[0] * 3);
    CHECK(std::abs(float(ySum[0]) / count[0] - float(ySum[1]) / count[1]) < 2.f);
#endif
    REQUIRE(gfx->releaseTexture(atlas.value().texture));
}

TEST_CASE("graphics.vegetation.motion_array_deforms_rest_stream_with_explicit_noise_and_time") {
    auto *gfx = Graphics::create();
    gfx->initHeadless(64, 64);

    VegetationField   field;
    VegetationGlobals globals;
    globals.motion = {1, 0, 1, 0};
    REQUIRE(field.replace(globals, {}).ok());
    auto atlas = uploadVegetationMotionAtlas(*gfx, field, {0, 0, 0}, {1, 1, 1}, 1, 1, false);
    REQUIRE(atlas.ok());
    const uint8_t noisePixel[]{128, 128, 128, 255};
    auto         *noise = gfx->newTexture(1, 1, noisePixel, true, true);
    REQUIRE(noise != nullptr);

    const float    positions[] = {-.08f, 0, .5f, .08f, 0, .5f, .08f, .8f, .5f, -.08f, .8f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    std::vector<float> deformation;
    for (int i = 0; i < 4; ++i) deformation.insert(deformation.end(), {0, 0, 0, 1, 0, 0, .5f, 1, 1});
    REQUIRE(mesh->adoptVegetationDeformationFactors(std::move(deformation)).ok());
    auto *restCanvas   = gfx->newCanvas(64, 64);
    auto *motionCanvas = gfx->newCanvas(64, 64);
    REQUIRE(restCanvas != nullptr);
    REQUIRE(motionCanvas != nullptr);

    auto draw = [&](Canvas *canvas, const PbrSurface &surface) {
        REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
        gfx->setMesh3DViewProj(glm::mat4(1));
        gfx->setMesh3DView(glm::mat4(1));
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DMaterial(0, 1);
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(0, 1, 0, 1));
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
    };
    PbrSurface rest;
    rest.unlit = true;
    draw(restCanvas, rest);
    auto moving                              = rest;
    moving.vegetationVertex.source           = PbrVegetationDeformationSource::GpuFields;
    moving.vegetationMotion.mode             = PbrVegetationMotionMode::Object;
    moving.vegetationMotion.texture          = atlas.value().texture;
    moving.vegetationMotion.noise            = noise;
    moving.vegetationMotion.usage[0]         = 1;
    moving.vegetationMotion.globalDirection  = {1, 0};
    moving.vegetationMotion.bending          = .5f;
    moving.vegetationMotion.branch           = 0;
    moving.vegetationMotion.rolling          = 0;
    moving.vegetationMotion.flutter          = 0;
    moving.vegetationMotion.perspectivePush  = 0;
    moving.vegetationMotion.perspectiveNoise = 0;
    draw(motionCanvas, moving);

    std::unique_ptr<eve::image::ImageData> restImage(restCanvas->newImageData());
    std::unique_ptr<eve::image::ImageData> motionImage(motionCanvas->newImageData());
    REQUIRE(restImage != nullptr);
    REQUIRE(motionImage != nullptr);
    auto centroid = [](const eve::image::ImageData &image) {
        int count = 0, xSum = 0;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                if (image.getPixel(x, y).g > .95f) {
                    ++count;
                    xSum += x;
                }
        return std::pair(count, count ? float(xSum) / count : 0.f);
    };
    const auto [restCount, restX]     = centroid(*restImage);
    const auto [motionCount, motionX] = centroid(*motionImage);
    REQUIRE(restCount > 100);
    REQUIRE(motionCount >= 100);
    CHECK(motionX > restX + 4.f);
    REQUIRE(gfx->releaseTexture(noise));
    REQUIRE(gfx->releaseTexture(atlas.value().texture));
}
}  // namespace test_graphics_vegetation_extras
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
namespace test_graphics_webgpu_pbr_variant {
#ifdef EVENGINE_WEBGPU


using namespace eve::graphics;
using namespace eve::graphics::webgpu;

namespace {
[[maybe_unused]] Texture *marker(std::uintptr_t value) { return reinterpret_cast<Texture *>(value); }

}  // namespace


TEST_CASE("graphics.webgpu.pbrVariantSpecializationCoalescesEquivalentSamplers") {
    PbrSurface surface;
    surface.textures[std::size_t(PbrTextureSlot::BaseColor)].texture = marker(1);
    surface.textures[std::size_t(PbrTextureSlot::Normal)].texture    = marker(2);
    surface.vegetationDetail.value                                   = 1.f;
    surface.vegetationDetail.textures[0].texture                     = marker(3);
    surface.vegetationDetail.textures[1].texture                     = marker(4);
    auto plan                                                        = planPbrVariant(surface, false, false);
    REQUIRE(plan.ok());
    CHECK_EQ(plan.value().fragmentTextures, 4u);
    CHECK_EQ(plan.value().fragmentSamplers, 1u);
    auto specialized = specializePbrVariantSources(plan.value());
    CHECK(specialized.fragment.find("var map0_sampler") != std::string::npos);
    CHECK(specialized.fragment.find("var map2_sampler") == std::string::npos);
    CHECK(specialized.fragment.find("var detailMap0_sampler") == std::string::npos);
    CHECK(specialized.fragment.find("var detailMap1_sampler") == std::string::npos);

    using eve::graphics::parity_test::headlessGraphics;
    auto *base = headlessGraphics();
    auto *gfx  = dynamic_cast<eve::graphics::webgpu::Graphics *>(base);
    REQUIRE(gfx != nullptr);
    PbrVariantSources views{specialized.vertex, specialized.fragment};
    const auto        compiled = gfx->debugValidatePbrVariantSources(views);
    if (!compiled.ok()) std::cerr << compiled.error()->message() << '\n';
    REQUIRE(compiled.ok());
    const auto linked = gfx->debugValidatePbrVariantPipeline(views);
    if (!linked.ok()) std::cerr << linked.error()->message() << '\n';
    REQUIRE(linked.ok());
}

TEST_CASE("graphics.webgpu.pbrVariantBaseSourcesCompileOnDawn") {
    using eve::graphics::parity_test::headlessGraphics;
    auto *base = headlessGraphics();
    auto *gfx  = dynamic_cast<eve::graphics::webgpu::Graphics *>(base);
    REQUIRE(gfx != nullptr);
    const auto compiled = gfx->debugValidatePbrVariantBaseSources();
    REQUIRE(compiled.ok());
}

TEST_CASE("graphics.webgpu.pbrVariantSpecializationRemovesUnreachableBindings") {
    PbrSurface surface;
    surface.textures[std::size_t(PbrTextureSlot::BaseColor)].texture = marker(1);
    auto plan                                                        = planPbrVariant(surface, false, false);
    REQUIRE(plan.ok());
    auto specialized = specializePbrVariantSources(plan.value());
    CHECK(specialized.fragment.find("var map0_image") != std::string::npos);
    CHECK(specialized.fragment.find("var map1_image") == std::string::npos);
    CHECK(specialized.fragment.find("map1_image") == std::string::npos);
    CHECK(specialized.fragment.find("environmentMap_image") == std::string::npos);
    CHECK(specialized.fragment.find("shadowMap_image") == std::string::npos);
    CHECK(specialized.vertex.find("vertexField_image") == std::string::npos);

    using eve::graphics::parity_test::headlessGraphics;
    auto *base = headlessGraphics();
    auto *gfx  = dynamic_cast<eve::graphics::webgpu::Graphics *>(base);
    REQUIRE(gfx != nullptr);
    PbrVariantSources views{specialized.vertex, specialized.fragment};
    const auto        compiled = gfx->debugValidatePbrVariantSources(views);
    if (!compiled.ok()) std::cerr << compiled.error()->message() << '\n';
    REQUIRE(compiled.ok());
}

#endif
}  // namespace test_graphics_webgpu_pbr_variant
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
namespace test_grass {
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


TEST_CASE("graphics.Grass.photoModeAuthorityOwnsFourRuntimeMultipliers") {
    auto *gfx   = Graphics::create();
    auto *field = gfx->newGrassField();
    field->setPhotoModeAuthority(true);
    REQUIRE(field->applyPhotoModeField({"m_globalGrassDensity", eve::PhotoModeDomain::Grass, 2.f}).ok());
    REQUIRE(field->applyPhotoModeField({"m_globalGrassDistance", eve::PhotoModeDomain::Grass, 3.f}).ok());
    REQUIRE(field->applyPhotoModeField({"m_cameraCellDistance", eve::PhotoModeDomain::Grass, 1.5f}).ok());
    REQUIRE(field->applyPhotoModeField({"m_cameraCellSubdivision", eve::PhotoModeDomain::Grass, int64_t{2}}).ok());
    CHECK_EQ(field->getPhotoModeDensity(), 2.f);
    CHECK_EQ(field->getPhotoModeDistance(), 3.f);
    CHECK_EQ(field->getPhotoModeCellDistance(), 1.5f);
    CHECK_EQ(field->getPhotoModeCellSubdivision(), 2);
    CHECK_EQ(field->getTerrainDetailHardDistance(), 472.5f);
    CHECK_EQ(field->getTerrainDetailDensity(), 1.f);
    CHECK(!field->applyPhotoModeField({"m_cameraCellSubdivision", eve::PhotoModeDomain::Grass, int64_t{9}}).ok());
    CHECK_EQ(field->getPhotoModeCellSubdivision(), 2);
    delete field;
}


TEST_CASE("graphics.Grass.fileSwayAtlasPacksFourGrassAndTwoLeaf") {
    const std::vector<std::string> grass = {
        pathBesideThisSource("Texture/grass_sway_0.png"),
        pathBesideThisSource("Texture/grass_sway_1.png"),
        pathBesideThisSource("Texture/grass_sway_2.png"),
        pathBesideThisSource("Texture/grass_sway_3.png"),
    };
    const std::vector<std::string> leaf = {
        pathBesideThisSource("Texture/leaf_sway_0.png"),
        pathBesideThisSource("Texture/leaf_sway_1.png"),
    };
    for (const auto &p : grass) REQUIRE(std::filesystem::exists(p));
    for (const auto &p : leaf) REQUIRE(std::filesystem::exists(p));

    std::vector<uint8_t> rgba;
    PackedAtlasInfo      packed;
    eve::graphics::grass::packSwayAtlasRGBA(grass, leaf, rgba, packed);
    CHECK_EQ(packed.grassVariants, 4);
    CHECK_EQ(packed.leafVariants, 2);
    CHECK_EQ(packed.atlasCols, 8);
    CHECK_EQ(packed.atlasRows, 4);
    CHECK_EQ(packed.leafRowOffset, 2);
    CHECK_EQ(packed.frames, 4);
    CHECK(packed.width >= 512);
    CHECK(packed.height >= 512);
    CHECK_EQ(int(rgba.size()), packed.width * packed.height * 4);

    auto opaqueInCell = [&](int col, int row) {
        const int cellW = packed.width / packed.atlasCols;
        const int cellH = packed.height / packed.atlasRows;
        int       n     = 0;
        for (int y = row * cellH; y < (row + 1) * cellH; ++y) {
            for (int x = col * cellW; x < (col + 1) * cellW; ++x) {
                if (rgba[(size_t(y) * size_t(packed.width) + size_t(x)) * 4u + 3u] > 16) ++n;
            }
        }
        return n;
    };
    // Each grass variant is a 2x2 block on the top two frame-rows.
    for (int v = 0; v < 4; ++v) {
        CHECK(opaqueInCell(v * 2, 0) + opaqueInCell(v * 2 + 1, 0) + opaqueInCell(v * 2, 1) +
                  opaqueInCell(v * 2 + 1, 1) >
              80);
    }
    // Leaf variants occupy the bottom two frame-rows.
    CHECK(opaqueInCell(0, 2) + opaqueInCell(1, 2) + opaqueInCell(0, 3) + opaqueInCell(1, 3) > 80);
    CHECK(opaqueInCell(2, 2) + opaqueInCell(3, 2) + opaqueInCell(2, 3) + opaqueInCell(3, 3) > 80);
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

TEST_CASE("graphics.Grass.gpuRenderScreenshot") {
    auto *win = eve::window::Window::create();
    auto *gfx = Graphics::create();
    REQUIRE(win != nullptr);
    REQUIRE(gfx != nullptr);
    gfx->setMsaaSamples(0);
    eve::window::WindowSettings s;
    s.width    = 960;
    s.height   = 540;
    s.centered = true;
    REQUIRE(win->setWindowSettings(s));

    resetGrassScene();

    auto *rc = gfx->getRenderControl();
    REQUIRE(rc != nullptr);
    rc->disable("aa");
    rc->disable("msaa");
    rc->disable("ao");
    rc->disable("gi");
    rc->enable("shadow");
    rc->enable("forward");
    rc->compile();

    constexpr float kField = 10.f;
    auto           *ground = Renderable3D::create();
    ground->setMesh(makeGroundMesh(gfx, kField, kField));
    ground->setTexture(makeSolidTex(gfx, 68, 54, 30));
    ground->setPosition(0.f, 0.f, 0.f);
    ground->setMetallic(0.f);
    ground->setRoughness(0.92f);
    ground->setCastShadow(false);
    ground->setReceiveShadow(true);

    GrassField::BakeParams bake;
    bake.denseRadius     = 0.10f;
    bake.sparseRadius    = 0.46f;
    bake.maxDense        = 7000;
    bake.maxSparse       = 220;
    bake.width           = 0.58f;
    bake.height          = 0.98f;
    bake.seed            = 11;
    bake.minSlopeDot     = 0.2f;
    bake.grassAtlasFiles = {
        pathBesideThisSource("Texture/grass_sway_0.png"),
        pathBesideThisSource("Texture/grass_sway_1.png"),
        pathBesideThisSource("Texture/grass_sway_2.png"),
        pathBesideThisSource("Texture/grass_sway_3.png"),
    };
    bake.leafAtlasFiles = {
        pathBesideThisSource("Texture/leaf_sway_0.png"),
        pathBesideThisSource("Texture/leaf_sway_1.png"),
    };

    GrassField *field = gfx->newGrassField();
    REQUIRE(field != nullptr);
    field->bakePlane(kField, kField, 16, 16, bake);
    field->setTime(0.22f);
    std::printf("\ngrass dense=%d sparse=%d\n", field->getDenseCount(), field->getSparseCount());
    REQUIRE(field->getDenseCount() >= 800);
    REQUIRE(field->getDenseMesh() != nullptr);
    REQUIRE(field->getShader() != nullptr);
    REQUIRE(field->getAtlas() != nullptr);

    auto *dense = Renderable3D::create();
    dense->setMesh(field->getDenseMesh());
    dense->setTexture(field->getAtlas());
    dense->setShader(field->getShader());
    dense->setCastShadow(false);
    dense->setReceiveShadow(true);

    if (field->getSparseMesh()) {
        auto *sparse = Renderable3D::create();
        sparse->setMesh(field->getSparseMesh());
        sparse->setTexture(field->getAtlas());
        sparse->setShader(field->getShader());
        sparse->setCastShadow(false);
        sparse->setReceiveShadow(true);
    }

    auto *post = Renderable3D::create();
    post->setMesh(gfx->newMeshCylinder(18, 1, true));
    post->setTexture(makeSolidTex(gfx, 168, 148, 122));
    post->setPosition(1.35f, 0.95f, -0.4f);
    post->setScale(0.28f, 0.95f, 0.28f);
    post->setMetallic(0.f);
    post->setRoughness(0.7f);
    post->setCastShadow(true);
    post->setReceiveShadow(false);

    auto *cam = Camera3D::createCamera();
    cam->setEye(4.8f, 2.9f, 5.9f);
    cam->setTarget(0.f, 0.45f, 0.f);
    cam->setAmbient(0.18f, 0.20f, 0.16f);
    cam->data()->nearZ = 0.1f;
    cam->data()->farZ  = 80.f;

    auto *sun = Light3D::createLight("dir");
    sun->setDirection(0.62f, 1.05f, 0.28f);
    sun->setColor(1.f, 0.97f, 0.88f, 2.4f);
    sun->setCastShadow(true);
    sun->setShadowStrength(1.f);
    sun->setShadowBias(0.003f);

    auto *hud              = Renderable2D::create();
    hud->transform()->x    = 0;
    hud->transform()->y    = 0;
    hud->sprite()->width   = 2;
    hud->sprite()->height  = 2;
    hud->sprite()->r       = 0.f;
    hud->sprite()->g       = 0.f;
    hud->sprite()->b       = 0.f;
    hud->sprite()->a       = 0.f;
    hud->sprite()->visible = true;

    gfx->setScreenReadbackEnabled(true);
    gfx->setBackgroundColor(eve::graphics::Color(0.52f, 0.72f, 0.92f, 1.f));

    for (int i = 0; i < 4; ++i) {
        RenderSystem3D::render(*gfx);
        RenderSystem::render(*gfx);
    }

    [[maybe_unused]] auto *const imageModule = eve::image::Image::create();
    eve::image::ImageData       *frame       = gfx->newImageData();
    REQUIRE(frame != nullptr);
    std::unique_ptr<eve::image::ImageData> ownedFrame(frame);

    int       greenish  = 0;
    int       darkGreen = 0;
    int       litGreen  = 0;
    const int w         = gfx->getWidth();
    const int h         = gfx->getHeight();
    for (int y = 0; y < h; y += 2) {
        for (int x = 0; x < w; x += 2) {
            const auto c = frame->getPixel(x, y);
            if (c.g > c.r + 0.04f && c.g > c.b + 0.02f) {
                ++greenish;
                const float luma = (c.r + c.g + c.b) / 3.f;
                if (luma < 0.28f) ++darkGreen;
                if (luma > 0.38f) ++litGreen;
            }
        }
    }
    // This fixed-size scene contains a broad field spanning multiple cascades.
    // A WebGPU regression that left empty cascade layers uncleared turned the
    // whole field dark while still satisfying the old handful-of-pixels check.
    CHECK(greenish > 50000);
    CHECK(darkGreen > 100);
    CHECK(litGreen > 50000);

    const std::string outDir = std::string(EVENGINE_TEST_BINARY_DIR) + "/out";
    savePng(frame, outDir + "/grass_field.png");
    savePng(frame, "/opt/cursor/artifacts/grass_field.png");

    std::vector<uint8_t> atlasRgba;
    PackedAtlasInfo      atlasInfo;
    eve::graphics::grass::packSwayAtlasRGBA(bake.grassAtlasFiles, bake.leafAtlasFiles, atlasRgba, atlasInfo);
    auto *atlasImg = new eve::image::ImageData(atlasInfo.width, atlasInfo.height, "RGBA8", atlasRgba.data(), false);
    savePng(atlasImg, outDir + "/grass_atlas.png");
    savePng(atlasImg, "/opt/cursor/artifacts/grass_atlas.png");
    delete atlasImg;

    win->close();
}
}  // namespace test_grass
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_model3d_gltf_uv)
#include "PrivateTestScope.h"
#include <assimp/material.h>
#include <assimp/scene.h>
#include <cmath>
#include <filesystem>
#include "PathBesideSource.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/RenderSystem3D.h"
#include "medialoader/model/ModelLoader.h"
#include "model3d/ModelData.h"
#include "model3d/ModelRenderer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// model3d_gltf_uv.cpp
namespace test_model3d_gltf_uv {
TEST_CASE("model3d.gltfUvOriginAndTransformSurviveLoader") {
    auto *gfx = eve::graphics::Graphics::create();
    gfx->initHeadless(128, 128);
    const auto source = std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path() /
                        "fixtures/model3d/uv-origin.gltf";
    medialoader::ModelLoader loader;
    medialoader::LoadOptions options;
    options.joinIdenticalVertices = false;
    options.improveCacheLocality  = false;
    for (bool flip : {false, true}) {
        options.flipUVs               = flip;
        auto                    scene = loader.loadFromPath(source.string().c_str(), options);
        eve::model3d::ModelData model(std::move(scene), "", flip);
        auto                   *entity = eve::model3d::buildRenderable(*gfx, &model, 0);
        REQUIRE(entity != nullptr);
        const auto uv = entity->getMesh()->texcoordSet(0);
        REQUIRE_EQ(uv.size(), size_t(6));
        REQUIRE(std::abs(uv[1] - .2f) < .00001f);
        const auto binding = entity->getMaterial()->pbrSurface().textures[0];
        REQUIRE(std::abs(binding.rotation - .6f) < .00001f);
        REQUIRE(std::abs(binding.offset[0] - .13f) < .00001f);
        REQUIRE(std::abs(binding.offset[1] - .27f) < .00001f);
    }
}

TEST_CASE("model3d.missingPreviewTextureRetainsMaterialFactors") {
    auto *gfx = eve::graphics::Graphics::create();
    gfx->initHeadless(128, 128);
    const auto source = std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path() /
                        "fixtures/model3d/uv-origin.gltf";
    medialoader::ModelLoader loader;
    auto                     scene = loader.loadFromPath(source.string().c_str(), {});
    eve::model3d::ModelData  model(std::move(scene));
    auto                    *material = model.getScene()->mMaterials[0];
    aiString                 missing("missing-preview-texture.png");
    material->AddProperty(&missing, AI_MATKEY_TEXTURE(aiTextureType_BASE_COLOR, 0));
    material->AddProperty(&missing, AI_MATKEY_TEXTURE(aiTextureType_DIFFUSE, 0));
    auto *entity = eve::model3d::buildRenderable(*gfx, &model, 0);
    REQUIRE(entity != nullptr);
    REQUIRE(entity->getMaterial()->pbrSurface().textures[0].texture == nullptr);
}
}  // namespace test_model3d_gltf_uv
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_mesh_vertex_axis_gizmo)
#include "PrivateTestScope.h"
#include <cmath>
#include <cstdint>
#include <memory>
#include "GraphicsParitySupport.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/editing/PrimitiveGizmoRenderer.h"
#include "image/ImageData.h"
#include "procgen/editing/MeshVertexAxisGizmo.h"
#include "zeroerr/unittest.h"
// procgen_mesh_vertex_axis_gizmo.cpp
namespace test_procgen_mesh_vertex_axis_gizmo {
using namespace eve::editing;
using namespace eve::graphics;
using namespace eve::graphics::parity_test;
using namespace eve::procgen;
using namespace eve::procgen_editing;

TEST_CASE("procgen.meshVertexAxisGizmo.picksMovesAndRendersThroughPrimitiveBackend") {
    MeshBuild mesh;
    mesh.addVertex(-0.2f, -0.2f, -2.f, 0.f, 0.f, 1.f, 0.f, 0.f);
    mesh.addVertex(0.2f, -0.2f, -2.f, 0.f, 0.f, 1.f, 1.f, 0.f);
    mesh.addVertex(0.f, 0.2f, -2.f, 0.f, 0.f, 1.f, 0.5f, 1.f);
    mesh.addTriangle(0, 1, 2);
    MeshDeformationSession session;
    REQUIRE(session.initializeResult(mesh).ok());
    REQUIRE(session.selectVerticesSphereResult(0.f, 0.f, -2.f, 1.f).ok());

    MeshVertexAxisGizmoBuilder builder;
    const auto                 snapshot = builder.build(session, 0.8);
    REQUIRE(snapshot.status == eve::editing::Status::Applied);
    CHECK_EQ(snapshot.primitives.size(), std::size_t(3));
    auto picked = builder.pickAxisResult(snapshot, 0.5, 0.0, 0.0, 0.0, 0.0, -1.0);
    REQUIRE(picked.ok());
    CHECK_EQ(picked.value(), "x");
    REQUIRE(session.moveSelectedVerticesResult(0.25f, 0.f, 0.f).ok());
    auto movedCenter = session.selectedVertexCenterResult();
    REQUIRE(movedCenter.ok());
    CHECK(std::abs(movedCenter.value().x - 0.25f) < 1e-6f);

    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    Canvas *target = gfx->newCanvas(128, 128);
    REQUIRE(target != nullptr);
    SceneDrawContext context;
    context.viewportSize = {128, 128};
    context.nearPlane    = 0.1f;
    context.farPlane     = 10.f;
    context.projection   = perspectiveVulkanRH_ZO(glm::radians(60.f), 1.f, context.nearPlane, context.farPlane);
    gfx->begin3DFrameToCanvas(target);
    auto rendered = eve::graphics_editing::PrimitiveGizmoRenderer().render(snapshot, context, *gfx);
    REQUIRE(rendered.ok());
    CHECK(rendered.value().segmentCount > 6u);
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> image(target->newImageData());
    REQUIRE(image != nullptr);
    const auto *pixels        = static_cast<const std::uint8_t *>(image->getData());
    std::size_t visiblePixels = 0;
    for (std::size_t offset = 0; offset < image->getSize(); offset += 4u)
        if (pixels[offset] > 12u || pixels[offset + 1u] > 12u || pixels[offset + 2u] > 12u) ++visiblePixels;
    CHECK(visiblePixels > 40u);
}
}  // namespace test_procgen_mesh_vertex_axis_gizmo
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_procgen_spline_gizmo_render)
#include "PrivateTestScope.h"
#include <cstdint>
#include <memory>
#include "GraphicsParitySupport.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/editing/PrimitiveGizmoRenderer.h"
#include "image/ImageData.h"
#include "procgen/editing/SplinePathDocument.h"
#include "procgen/editing/SplinePathGizmo.h"
#include "zeroerr/unittest.h"
// procgen_spline_gizmo_render.cpp
namespace test_procgen_spline_gizmo_render {
using namespace eve::editing;
using namespace eve::graphics;
using namespace eve::graphics::parity_test;
using namespace eve::procgen_editing;

namespace {

SplinePathControlPoint gizmoPoint(const char *id, std::int64_t order, double x, double y) {
    SplinePathControlPoint point;
    point.id    = StableId(id);
    point.order = order;
    point.x     = x;
    point.y     = y;
    point.z     = -2.0;
    return point;
}

}  // namespace

TEST_CASE("procgen.splineGizmo.rendersThroughPrimitiveBackend") {
    SplinePathDocument document("gpu-spline-gizmo");
    REQUIRE(document.applyDomainOperation(document.makeSetSettings({"bezier", false}).value()).ok());
    auto left  = gizmoPoint("left", 0, -0.7, -0.35);
    left.outX  = 0.45;
    left.outY  = 0.8;
    auto right = gizmoPoint("right", 1, 0.7, 0.35);
    right.inX  = -0.45;
    right.inY  = -0.8;
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(left).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(right).value()).ok());

    SplineGizmoStyle style;
    style.showPointLabels = false;  // Text labels belong to the viewport's 2D overlay pass.
    const auto snapshot   = SplinePathGizmoBuilder().build(document, 24, 4096, style);
    REQUIRE(snapshot.status == EditorStatus::Applied);
    REQUIRE(snapshot.primitives.size() > 20u);

    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    Canvas *target = gfx->newCanvas(128, 128);
    REQUIRE(target != nullptr);
    SceneDrawContext context;
    context.viewportSize = {128, 128};
    context.nearPlane    = 0.1f;
    context.farPlane     = 10.f;
    context.projection   = perspectiveVulkanRH_ZO(glm::radians(60.f), 1.f, context.nearPlane, context.farPlane);

    gfx->begin3DFrameToCanvas(target);
    auto rendered = eve::graphics_editing::PrimitiveGizmoRenderer().render(snapshot, context, *gfx);
    REQUIRE(rendered.ok());
    CHECK(rendered.value().segmentCount > 10u);
    gfx->end3DFrameToCanvas();

    std::unique_ptr<eve::image::ImageData> image(target->newImageData());
    REQUIRE(image != nullptr);
    const auto *pixels        = static_cast<const std::uint8_t *>(image->getData());
    std::size_t visiblePixels = 0;
    for (std::size_t offset = 0; offset < image->getSize(); offset += 4u)
        if (pixels[offset] > 12u || pixels[offset + 1u] > 12u || pixels[offset + 2u] > 12u) ++visiblePixels;
    CHECK(visiblePixels > 80u);
}
}  // namespace test_procgen_spline_gizmo_render
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_stylize_mesh_vertex)
#include "PrivateTestScope.h"
#include <array>
#include <cmath>
#include <memory>
#include <vector>
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "image/ImageData.h"
#include "stylize/StyleShaders.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// stylize_mesh_vertex.cpp
namespace test_stylize_mesh_vertex {
namespace {

constexpr int kStylizeTestSize = 128;

/** @brief Draw one stylize frame into an offscreen canvas and read the pixels back. */
std::vector<unsigned char> drawStylizeFrame(eve::graphics::Graphics *gfx, eve::graphics::Canvas *canvas,
                                            eve::graphics::Mesh *mesh, eve::graphics::Shader *shader,
                                            const glm::vec4 &tint) {
    gfx->begin3DFrameToCanvas(canvas);
    gfx->drawMeshShader(mesh, glm::mat4(1.f), nullptr, tint, shader);
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
    if (!pixels) return {};
    const auto  *data = static_cast<const unsigned char *>(pixels->getData());
    const size_t size = size_t(kStylizeTestSize) * size_t(kStylizeTestSize) * 4u;
    return std::vector<unsigned char>(data, data + size);
}

size_t countDifferentPixels(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b) {
    size_t changed = 0;
    for (size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4)
        if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) ++changed;
    return changed;
}

long totalLuma(const std::vector<unsigned char> &pixels) {
    long sum = 0;
    for (size_t i = 0; i + 3 < pixels.size(); i += 4)
        sum += long(pixels[i]) + long(pixels[i + 1]) + long(pixels[i + 2]);
    return sum;
}

}  // namespace

// The Vulkan stylize paths must run the same vertex contract as the WebGPU path
// (which uses the backend's standard mesh vertex shader): GPU skinning via the
// skin palette at set 0 binding 21, and the tint * vertexColor multiply.
TEST_CASE("stylize.anime.skinnedMeshFollowsPalette") {
    using namespace eve::graphics;
    auto *gfx = Graphics::create();
    REQUIRE(gfx != nullptr);
    if (!gfx->isHeadless()) gfx->initHeadless(kStylizeTestSize, kStylizeTestSize);
    Canvas *canvas = gfx->newCanvas(kStylizeTestSize, kStylizeTestSize);
    REQUIRE(canvas != nullptr);
    auto *shader = eve::stylize::createMeshShader(gfx, "anime");
    REQUIRE(shader != nullptr);

    const float    positions[] = {-0.3f, -0.3f, 0.5f, 0.3f, -0.3f, 0.5f, 0.f, 0.3f, 0.5f};
    const float    normals[]   = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float    uvs[]       = {0.f, 0.f, 1.f, 0.f, 0.5f, 1.f};
    const uint32_t indices[]   = {0, 1, 2};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 3, indices, 3);
    REQUIRE(mesh != nullptr);
    const uint16_t joints[]  = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const float    weights[] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f};
    REQUIRE(gfx->setMeshSkinningData(mesh, joints, weights, 3));

    std::array<float, 16> palette{};
    for (int diagonal : {0, 5, 10, 15}) palette[size_t(diagonal)] = 1.f;
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    Lighting3DPack lighting{};
    lighting.ambient             = glm::vec4(0.5f, 0.5f, 0.5f, 0.f);
    lighting.count               = 1;
    lighting.lights[0].posRadius = glm::vec4(0.f, 0.f, 1.f, 0.f);
    lighting.lights[0].color     = glm::vec4(1.f);
    gfx->setMesh3DLighting(lighting);

    std::vector<unsigned char> first;
    for (float translation : {-0.5f, 0.5f}) {
        palette[12] = translation;
        REQUIRE(mesh->setSkinPalette(palette.data(), 1).ok());
        std::vector<unsigned char> frame = drawStylizeFrame(gfx, canvas, mesh, shader, glm::vec4(1.f));
        REQUIRE(frame.size() == size_t(kStylizeTestSize) * size_t(kStylizeTestSize) * 4u);
        if (translation < 0.f)
            first = std::move(frame);
        else
            REQUIRE(countDifferentPixels(first, frame) > 200);
    }
}

// Vertex color must reach the Vulkan material color: the WebGPU stylize path runs
// the standard mesh vertex shader, which forwards ubo.tint * inColor.
TEST_CASE("stylize.anime.vertexColorFeedsMaterialColor") {
    using namespace eve::graphics;
    auto *gfx = Graphics::create();
    REQUIRE(gfx != nullptr);
    if (!gfx->isHeadless()) gfx->initHeadless(kStylizeTestSize, kStylizeTestSize);
    Canvas *canvas = gfx->newCanvas(kStylizeTestSize, kStylizeTestSize);
    REQUIRE(canvas != nullptr);
    auto *shader = eve::stylize::createMeshShader(gfx, "anime");
    REQUIRE(shader != nullptr);

    const float    positions[] = {-0.4f, -0.4f, 0.5f, 0.4f, -0.4f, 0.5f, 0.f, 0.4f, 0.5f};
    const float    normals[]   = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float    uvs[]       = {0.f, 0.f, 1.f, 0.f, 0.5f, 1.f};
    const uint32_t indices[]   = {0, 1, 2};
    const float    white[]     = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    const float    grey[]      = {0.2f, 0.2f, 0.2f, 1.f, 0.2f, 0.2f, 0.2f, 1.f, 0.2f, 0.2f, 0.2f, 1.f};
    auto          *whiteMesh   = gfx->newMeshFromArraysColored(positions, normals, uvs, white, 3, indices, 3);
    auto          *greyMesh    = gfx->newMeshFromArraysColored(positions, normals, uvs, grey, 3, indices, 3);
    REQUIRE(whiteMesh != nullptr);
    REQUIRE(greyMesh != nullptr);

    gfx->setMesh3DViewProj(glm::mat4(1.f));
    Lighting3DPack lighting{};
    lighting.ambient             = glm::vec4(0.5f, 0.5f, 0.5f, 0.f);
    lighting.count               = 1;
    lighting.lights[0].posRadius = glm::vec4(0.f, 0.f, 1.f, 0.f);
    lighting.lights[0].color     = glm::vec4(1.f);
    gfx->setMesh3DLighting(lighting);

    const std::vector<unsigned char> whiteFrame = drawStylizeFrame(gfx, canvas, whiteMesh, shader, glm::vec4(1.f));
    const std::vector<unsigned char> greyFrame  = drawStylizeFrame(gfx, canvas, greyMesh, shader, glm::vec4(1.f));
    REQUIRE(whiteFrame.size() == size_t(kStylizeTestSize) * size_t(kStylizeTestSize) * 4u);
    REQUIRE(greyFrame.size() == whiteFrame.size());
    REQUIRE(countDifferentPixels(whiteFrame, greyFrame) > 200);
    REQUIRE(totalLuma(whiteFrame) > totalLuma(greyFrame));
}
// The Vulkan stylize paths must run the same vertex contract as the WebGPU path
// (which uses the backend's standard mesh vertex shader): GPU skinning via the
// skin palette at set 0 binding 21, and the tint * vertexColor multiply.
TEST_CASE("stylize.cartoon.skinnedMeshFollowsPalette") {
    using namespace eve::graphics;
    auto *gfx = Graphics::create();
    REQUIRE(gfx != nullptr);
    if (!gfx->isHeadless()) gfx->initHeadless(kStylizeTestSize, kStylizeTestSize);
    Canvas *canvas = gfx->newCanvas(kStylizeTestSize, kStylizeTestSize);
    REQUIRE(canvas != nullptr);
    auto *shader = eve::stylize::createMeshShader(gfx, "cartoon");
    REQUIRE(shader != nullptr);

    const float    positions[] = {-0.3f, -0.3f, 0.5f, 0.3f, -0.3f, 0.5f, 0.f, 0.3f, 0.5f};
    const float    normals[]   = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float    uvs[]       = {0.f, 0.f, 1.f, 0.f, 0.5f, 1.f};
    const uint32_t indices[]   = {0, 1, 2};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 3, indices, 3);
    REQUIRE(mesh != nullptr);
    const uint16_t joints[]  = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    const float    weights[] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f};
    REQUIRE(gfx->setMeshSkinningData(mesh, joints, weights, 3));

    std::array<float, 16> palette{};
    for (int diagonal : {0, 5, 10, 15}) palette[size_t(diagonal)] = 1.f;
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    Lighting3DPack lighting{};
    lighting.ambient             = glm::vec4(0.5f, 0.5f, 0.5f, 0.f);
    lighting.count               = 1;
    lighting.lights[0].posRadius = glm::vec4(0.f, 0.f, 1.f, 0.f);
    lighting.lights[0].color     = glm::vec4(1.f);
    gfx->setMesh3DLighting(lighting);

    std::vector<unsigned char> first;
    for (float translation : {-0.5f, 0.5f}) {
        palette[12] = translation;
        REQUIRE(mesh->setSkinPalette(palette.data(), 1).ok());
        std::vector<unsigned char> frame = drawStylizeFrame(gfx, canvas, mesh, shader, glm::vec4(1.f));
        REQUIRE(frame.size() == size_t(kStylizeTestSize) * size_t(kStylizeTestSize) * 4u);
        if (translation < 0.f)
            first = std::move(frame);
        else
            REQUIRE(countDifferentPixels(first, frame) > 200);
    }
}

// Vertex color must reach the Vulkan material color: the WebGPU stylize path runs
// the standard mesh vertex shader, which forwards ubo.tint * inColor.
TEST_CASE("stylize.cartoon.vertexColorFeedsMaterialColor") {
    using namespace eve::graphics;
    auto *gfx = Graphics::create();
    REQUIRE(gfx != nullptr);
    if (!gfx->isHeadless()) gfx->initHeadless(kStylizeTestSize, kStylizeTestSize);
    Canvas *canvas = gfx->newCanvas(kStylizeTestSize, kStylizeTestSize);
    REQUIRE(canvas != nullptr);
    auto *shader = eve::stylize::createMeshShader(gfx, "cartoon");
    REQUIRE(shader != nullptr);

    const float    positions[] = {-0.4f, -0.4f, 0.5f, 0.4f, -0.4f, 0.5f, 0.f, 0.4f, 0.5f};
    const float    normals[]   = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float    uvs[]       = {0.f, 0.f, 1.f, 0.f, 0.5f, 1.f};
    const uint32_t indices[]   = {0, 1, 2};
    const float    white[]     = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
    const float    grey[]      = {0.2f, 0.2f, 0.2f, 1.f, 0.2f, 0.2f, 0.2f, 1.f, 0.2f, 0.2f, 0.2f, 1.f};
    auto          *whiteMesh   = gfx->newMeshFromArraysColored(positions, normals, uvs, white, 3, indices, 3);
    auto          *greyMesh    = gfx->newMeshFromArraysColored(positions, normals, uvs, grey, 3, indices, 3);
    REQUIRE(whiteMesh != nullptr);
    REQUIRE(greyMesh != nullptr);

    gfx->setMesh3DViewProj(glm::mat4(1.f));
    Lighting3DPack lighting{};
    lighting.ambient             = glm::vec4(0.5f, 0.5f, 0.5f, 0.f);
    lighting.count               = 1;
    lighting.lights[0].posRadius = glm::vec4(0.f, 0.f, 1.f, 0.f);
    lighting.lights[0].color     = glm::vec4(1.f);
    gfx->setMesh3DLighting(lighting);

    const std::vector<unsigned char> whiteFrame = drawStylizeFrame(gfx, canvas, whiteMesh, shader, glm::vec4(1.f));
    const std::vector<unsigned char> greyFrame  = drawStylizeFrame(gfx, canvas, greyMesh, shader, glm::vec4(1.f));
    REQUIRE(whiteFrame.size() == size_t(kStylizeTestSize) * size_t(kStylizeTestSize) * 4u);
    REQUIRE(greyFrame.size() == whiteFrame.size());
    REQUIRE(countDifferentPixels(whiteFrame, greyFrame) > 200);
    REQUIRE(totalLuma(whiteFrame) > totalLuma(greyFrame));
}
}  // namespace test_stylize_mesh_vertex
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tensor)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
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
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "tensor/Graph.h"
#include "tensor/KernelGen.h"
#include "tensor/Optimizer.h"
#include "tensor/TF.h"
#include "tensor/Tensor.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tensor.cpp
namespace test_tensor {
using namespace eve::tensor;

namespace {

/** GPU tensor tests need a live Vulkan device; headless init is enough. */
[[maybe_unused]] bool tryInitHeadlessGfx() {
    auto *gfx = eve::graphics::Graphics::create();
    if (!gfx) return false;
    gfx->initHeadless(320, 240);
    return true;
}

}  // namespace


// --- GPU (compute shader) path ---------------------------------------------
// These skip (return) when no Vulkan device / glslc is available (e.g. some CI
// images), matching test/gpgpu.cpp's tolerant pattern. When the environment
// does support it, CompiledFunction::getDevice() should report "gpu".

TEST_CASE("tensor.gpu.compiledFunctionElementwiseAndMatmul") {
    if (!tryInitHeadlessGfx()) return;

    auto *tf = TF::create();

    std::unique_ptr<Tensor> w(tf->zeros2(3, 2));
    w->set2(0, 0, 1.f);
    w->set2(1, 0, 0.f);
    w->set2(2, 0, 1.f);
    w->set2(0, 1, 0.f);
    w->set2(1, 1, 1.f);
    w->set2(2, 1, 0.f);

    // relu(x * 2 + 1) matmul w  — exercises binary(add), unary(mul/relu chain as
    // separate nodes) and matmul kernels together.
    std::unique_ptr<Func> fn(tf->func());
    Tensor               *x = fn->input2(2, 3);
    Tensor               *y = tf->mulScalar(x, 2.f);
    y                       = tf->addScalar(y, 1.f);
    y                       = tf->relu(y);
    y                       = tf->matmul(y, w.get());
    fn->setOutput(y);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());

    std::unique_ptr<Tensor> xd(tf->zeros2(2, 3));
    xd->set2(0, 0, -1.f);
    xd->set2(0, 1, 2.f);
    xd->set2(0, 2, -3.f);
    xd->set2(1, 0, 4.f);
    xd->set2(1, 1, -5.f);
    xd->set2(1, 2, 6.f);

    std::unique_ptr<Tensor> out(compiled->run1(xd.get()));

    // CPU reference via eager ops.
    std::unique_ptr<Tensor> ref1(xd->mulScalar(2.f));
    std::unique_ptr<Tensor> ref2(ref1->addScalar(1.f));
    std::unique_ptr<Tensor> ref3(ref2->relu());
    std::unique_ptr<Tensor> ref(ref3->matmul(w.get()));

    REQUIRE_EQ(out->getDim0(), ref->getDim0());
    REQUIRE_EQ(out->getDim1(), ref->getDim1());
    for (int i = 0; i < out->getSize(); ++i) CHECK(std::fabs(out->get(i) - ref->get(i)) < 1e-3f);

    // Confirms the compute-shader path actually ran (not a silent CPU fallback).
    // Only meaningful when glslc + a Vulkan device are both available.
    REQUIRE_EQ(compiled->getDevice(), std::string("gpu"));
}

TEST_CASE("tensor.gpu.compiledFunctionWhereTranspose") {
    if (!tryInitHeadlessGfx()) return;

    auto *tf = TF::create();

    std::unique_ptr<Func> fn(tf->func());
    Tensor               *cond = fn->input1(4);
    Tensor               *a    = fn->input1(4);
    Tensor               *b    = fn->input1(4);
    Tensor               *sel  = tf->where(cond, a, b);
    Tensor               *sel2 = tf->reshape2(sel, 2, 2);
    Tensor               *out  = tf->transpose(sel2);
    fn->setOutput(out);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());

    std::unique_ptr<Tensor> c(tf->zeros1(4));
    c->set1(0, 1.f);
    c->set1(1, 0.f);
    c->set1(2, 1.f);
    c->set1(3, 0.f);
    std::unique_ptr<Tensor> av(tf->fill1(4, 10.f));
    std::unique_ptr<Tensor> bv(tf->fill1(4, 20.f));

    // select -> [10,20,10,20]; reshape(2,2) -> [[10,20],[10,20]]; transpose -> [[10,10],[20,20]]
    std::unique_ptr<Tensor> result(compiled->run3(c.get(), av.get(), bv.get()));
    REQUIRE_EQ(result->getDim0(), 2);
    REQUIRE_EQ(result->getDim1(), 2);
    CHECK(std::fabs(result->get2(0, 0) - 10.f) < 1e-4f);
    CHECK(std::fabs(result->get2(0, 1) - 10.f) < 1e-4f);
    CHECK(std::fabs(result->get2(1, 0) - 20.f) < 1e-4f);
    CHECK(std::fabs(result->get2(1, 1) - 20.f) < 1e-4f);
}

TEST_CASE("tensor.gpu.reduceLargeTensor") {
    if (!tryInitHeadlessGfx()) return;

    auto                   *tf = TF::create();
    const int               n  = 20000;  // above the GPU-reduce size threshold
    std::unique_ptr<Tensor> t(tf->zeros1(n));
    for (int i = 0; i < n; ++i) t->set1(i, float(i % 17) - 5.f);

    float gpuSum  = tf->reduceSum(t.get());
    float gpuMin  = tf->reduceMin(t.get());
    float gpuMax  = tf->reduceMax(t.get());
    float gpuMean = tf->reduceMean(t.get());

    double cpuSum = 0.0;
    float  cpuMin = t->get(0);
    float  cpuMax = t->get(0);
    for (int i = 0; i < n; ++i) {
        float v = t->get(i);
        cpuSum += v;
        cpuMin = std::min(cpuMin, v);
        cpuMax = std::max(cpuMax, v);
    }
    CHECK(std::fabs(gpuSum - float(cpuSum)) < std::max(1.f, std::fabs(float(cpuSum))) * 1e-3f);
    CHECK(std::fabs(gpuMin - cpuMin) < 1e-4f);
    CHECK(std::fabs(gpuMax - cpuMax) < 1e-4f);
    CHECK(std::fabs(gpuMean - float(cpuSum / n)) < 1.f);
}

// --- AITemplate-style refactor: new op coverage ---------------------------


namespace {

[[maybe_unused]] float conv2dRef(const float *x, int N, int C, int H, int W, const float *w, int F, int KH, int KW,
                                 int stride, int pad, int oh, int ow) {
    float acc = 0.f;
    for (int c = 0; c < C; ++c)
        for (int kh = 0; kh < KH; ++kh)
            for (int kw = 0; kw < KW; ++kw) {
                const int ih = oh * stride + kh - pad;
                const int iw = ow * stride + kw - pad;
                if (ih < 0 || ih >= H || iw < 0 || iw >= W) continue;
                acc += x[((0 * C + c) * H + ih) * W + iw] * w[((0 * F + c) * KH + kh) * KW + kw];
            }
    return acc;
}

}  // namespace


// --- new-op GPU paths (only run with a live Vulkan device; headless) -------
}  // namespace test_tensor
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tensor_gpu)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
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
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "tensor/Graph.h"
#include "tensor/KernelGen.h"
#include "tensor/Optimizer.h"
#include "tensor/TF.h"
#include "tensor/Tensor.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tensor_gpu.cpp
namespace test_tensor_gpu {
using namespace eve::tensor;

namespace {

/** GPU tensor tests need a live device; headless init is enough. */
void initHeadlessGfx(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT) {
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(gfx != nullptr);
    gfx->initHeadless(320, 240);
}

}  // namespace

TEST_CASE("tensor.gpu.fusedElementwiseChain") {
    initHeadlessGfx(_ZEROERR_TEST_CONTEXT);
    auto                 *tf = TF::create();
    std::unique_ptr<Func> fn(tf->func());
    Tensor               *x = fn->input2(4, 4);
    Tensor               *y = tf->mulScalar(x, 2.f);
    y                       = tf->addScalar(y, 1.f);
    y                       = tf->relu(y);
    y                       = tf->mulScalar(y, 0.5f);
    fn->setOutput(y);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    REQUIRE_EQ(compiled->getDevice(), std::string("gpu"));

    std::unique_ptr<Tensor> xd(tf->randomNormal2(4, 4));
    std::unique_ptr<Tensor> out(compiled->run1(xd.get()));
    std::unique_ptr<Tensor> ref(xd->mulScalar(2.f));
    ref.reset(ref->addScalar(1.f));
    ref.reset(ref->relu());
    ref.reset(ref->mulScalar(0.5f));
    for (int i = 0; i < out->getSize(); ++i) REQUIRE(std::fabs(out->get(i) - ref->get(i)) < 1e-4f);
}

TEST_CASE("tensor.gpu.softmaxLayernormConv") {
    initHeadlessGfx(_ZEROERR_TEST_CONTEXT);
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> w(tf->fill4(2, 1, 3, 3, 0.1f));

    std::unique_ptr<Func> fn(tf->func());
    Tensor               *in = fn->input4(1, 1, 6, 6);
    Tensor               *y  = tf->conv2d(in, w.get(), 1, 0);
    y                        = tf->relu(y);
    y                        = tf->flatten(y);
    y                        = tf->layernorm(y, 1e-5f);
    fn->setOutput(y);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    REQUIRE_EQ(compiled->getDevice(), std::string("gpu"));

    std::unique_ptr<Tensor> xd(tf->zeros4(1, 1, 6, 6));
    for (int i = 0; i < xd->getSize(); ++i) xd->set(i, float((i % 7) - 3));
    std::unique_ptr<Tensor> out(compiled->run1(xd.get()));

    std::unique_ptr<Tensor> ref(tf->conv2d(xd.get(), w.get(), 1, 0));
    ref.reset(ref->relu());
    ref.reset(ref->flatten());
    ref.reset(tf->layernorm(ref.get(), 1e-5f));
    REQUIRE_EQ(out->getSize(), ref->getSize());
    for (int i = 0; i < out->getSize(); ++i) REQUIRE(std::fabs(out->get(i) - ref->get(i)) < 1e-3f);
}

TEST_CASE("tensor.gpu.transformerBlockInference") {
    initHeadlessGfx(_ZEROERR_TEST_CONTEXT);
    auto       *tf = TF::create();
    const int   B = 1, T = 4, D = 8, C = 3;
    const float scale = 1.f / std::sqrt(float(D));
    const float eps   = 1e-5f;

    auto rand = [&](int d0, int d1, float s) {
        std::unique_ptr<Tensor> w(tf->randomNormal2(d0, d1));
        w.reset(w->mulScalar(s));
        return w;
    };
    std::unique_ptr<Tensor> Wq(rand(D, D, 0.15f));
    std::unique_ptr<Tensor> Wk(rand(D, D, 0.15f));
    std::unique_ptr<Tensor> Wv(rand(D, D, 0.15f));
    std::unique_ptr<Tensor> W1(rand(D, 2 * D, 0.15f));
    std::unique_ptr<Tensor> W2(rand(2 * D, D, 0.15f));
    std::unique_ptr<Tensor> Wc(rand(D, C, 0.15f));

    std::unique_ptr<Tensor> xEager(tf->randomNormal3(B, T, D));

    std::unique_ptr<Func> fn(tf->func());
    Tensor               *x   = fn->input3(B, T, D);
    Tensor               *l1  = tf->layernorm(x, eps);
    Tensor               *l1m = tf->reshape2(l1, B * T, D);
    Tensor               *qq  = tf->reshape4(tf->matmul(l1m, Wq.get()), 1, 1, T, D);
    Tensor               *kk  = tf->reshape4(tf->matmul(l1m, Wk.get()), 1, 1, T, D);
    Tensor               *vv  = tf->reshape4(tf->matmul(l1m, Wv.get()), 1, 1, T, D);
    Tensor               *a1  = tf->reshape3(tf->sdpa(qq, kk, vv, scale), B, T, D);
    Tensor               *x1s = tf->add(x, a1);
    Tensor               *l2  = tf->layernorm(x1s, eps);
    Tensor               *l2m = tf->reshape2(l2, B * T, D);
    Tensor               *hh  = tf->matmul(l2m, W1.get());
    hh                        = tf->gelu(hh);
    hh                        = tf->matmul(hh, W2.get());
    Tensor *x2s               = tf->add(x1s, tf->reshape3(hh, B, T, D));
    Tensor *lg                = tf->matmul(tf->reshape2(x2s, B * T, D), Wc.get());
    fn->setOutput(tf->softmax(lg, 1));
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    REQUIRE_EQ(compiled->getDevice(), std::string("gpu"));

    std::unique_ptr<Tensor> out(compiled->run1(xEager.get()));
    REQUIRE(std::fabs(out->reduceSum() - float(B * T)) < 1e-3f);
    std::unique_ptr<Tensor> action(tf->argmax(out.get(), 1, 0));
    REQUIRE_EQ(action->getSize(), B * T);

    // cross-check the generated GPU kernels against the eager CPU reference
    std::unique_ptr<Tensor> ln1(tf->layernorm(xEager.get(), eps));
    std::unique_ptr<Tensor> ln1m(tf->reshape2(ln1.get(), B * T, D));
    std::unique_ptr<Tensor> q(tf->reshape4(tf->matmul(ln1m.get(), Wq.get()), 1, 1, T, D));
    std::unique_ptr<Tensor> k(tf->reshape4(tf->matmul(ln1m.get(), Wk.get()), 1, 1, T, D));
    std::unique_ptr<Tensor> v(tf->reshape4(tf->matmul(ln1m.get(), Wv.get()), 1, 1, T, D));
    std::unique_ptr<Tensor> att(tf->reshape3(tf->sdpa(q.get(), k.get(), v.get(), scale), B, T, D));
    std::unique_ptr<Tensor> x1(tf->add(xEager.get(), att.get()));
    std::unique_ptr<Tensor> ln2(tf->layernorm(x1.get(), eps));
    std::unique_ptr<Tensor> ln2m(tf->reshape2(ln2.get(), B * T, D));
    std::unique_ptr<Tensor> h(tf->matmul(ln2m.get(), W1.get()));
    h.reset(tf->gelu(h.get()));
    h.reset(tf->matmul(h.get(), W2.get()));
    std::unique_ptr<Tensor> x2(tf->add(x1.get(), tf->reshape3(h.get(), B, T, D)));
    std::unique_ptr<Tensor> logits(tf->matmul(tf->reshape2(x2.get(), B * T, D), Wc.get()));
    std::unique_ptr<Tensor> ref(tf->softmax(logits.get(), 1));
    for (int i = 0; i < out->getSize(); ++i) REQUIRE(std::fabs(out->get(i) - ref->get(i)) < 1e-2f);
}

TEST_CASE("tensor.gpu.sdpaMatchesCpu") {
    initHeadlessGfx(_ZEROERR_TEST_CONTEXT);
    auto                   *tf = TF::create();
    const int               B = 1, H = 1, T = 4, S = 4, D = 8;
    std::unique_ptr<Tensor> q(tf->randomNormal4(B, H, T, D));
    std::unique_ptr<Tensor> k(tf->randomNormal4(B, H, S, D));
    std::unique_ptr<Tensor> v(tf->randomNormal4(B, H, S, D));
    std::unique_ptr<Tensor> ref(tf->sdpa(q.get(), k.get(), v.get(), 0.353553f));

    std::unique_ptr<Func> fn(tf->func());
    Tensor               *qq = fn->input4(B, H, T, D);
    Tensor               *kk = fn->input4(B, H, S, D);
    Tensor               *vv = fn->input4(B, H, S, D);
    fn->setOutput(tf->sdpa(qq, kk, vv, 0.353553f));
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    REQUIRE_EQ(compiled->getDevice(), std::string("gpu"));
    std::unique_ptr<Tensor> out(compiled->run3(q.get(), k.get(), v.get()));
    for (int i = 0; i < out->getSize(); ++i) REQUIRE(std::fabs(out->get(i) - ref->get(i)) < 1e-4f);
}

namespace {

void checkGpuGraphMatchesEager(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, TF *tf,
                               const std::function<Tensor *(Tensor *)> &build, Tensor *feed, const char *name) {
    std::unique_ptr<Func> fn(tf->func());
    Tensor               *in     = fn->input3(1, 4, 8);
    Tensor               *outSym = build(in);
    fn->setOutput(outSym);
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    REQUIRE_EQ(compiled->getDevice(), std::string("gpu"));
    std::unique_ptr<Tensor> out(compiled->run1(feed));
    std::unique_ptr<Tensor> ref(build(feed));
    REQUIRE_EQ(out->getRank(), ref->getRank());
    REQUIRE_EQ(out->getSize(), ref->getSize());
    float worst = 0.f;
    for (int i = 0; i < out->getSize(); ++i) {
        const float error = std::fabs(out->get(i) - ref->get(i));
        REQUIRE(std::isfinite(error));
        worst = std::max(worst, error);
    }
    if (worst >= 1e-3f) fprintf(stderr, "stage %s worst=%.6g\n", name, double(worst));
    REQUIRE_LT(worst, 1e-3f);
}

}  // namespace

TEST_CASE("tensor.gpu.stageBisect") {
    initHeadlessGfx(_ZEROERR_TEST_CONTEXT);
    auto                   *tf = TF::create();
    const int               T = 4, D = 8;
    std::unique_ptr<Tensor> x(tf->randomNormal3(1, T, D));
    std::unique_ptr<Tensor> W(tf->randomNormal2(D, D));

    auto stage1 = [&](Tensor *in) {
        Tensor *l = tf->layernorm(in, 1e-5f);
        Tensor *m = tf->reshape2(l, T, D);
        return tf->reshape4(tf->matmul(m, W.get()), 1, 1, T, D);
    };
    auto stage2 = [&](Tensor *in) {
        Tensor *q = stage1(in);
        return tf->sdpa(q, q, q, 0.353553f);
    };
    auto stage3 = [&](Tensor *in) {
        Tensor *a = stage2(in);
        return tf->add(in, tf->reshape3(a, 1, T, D));
    };
    auto stage4 = [&](Tensor *in) {
        Tensor *r = stage3(in);
        return tf->layernorm(r, 1e-5f);
    };

    checkGpuGraphMatchesEager(_ZEROERR_TEST_CONTEXT, tf, stage1, x.get(), "stage1");
    checkGpuGraphMatchesEager(_ZEROERR_TEST_CONTEXT, tf, stage2, x.get(), "stage2");
    checkGpuGraphMatchesEager(_ZEROERR_TEST_CONTEXT, tf, stage3, x.get(), "stage3");
    checkGpuGraphMatchesEager(_ZEROERR_TEST_CONTEXT, tf, stage4, x.get(), "stage4");
}
}  // namespace test_tensor_gpu
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tensor_gpu_contract)
#include "PrivateTestScope.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
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
#include "graphics/ScreenSpaceReflection.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/Volumetric.h"
#include "graphics/Water.h"
#include "graphics/Waterfall.h"
#include "tensor/GpuBackend.h"
#include "tensor/Graph.h"
#include "tensor/KernelGen.h"
#include "tensor/Optimizer.h"
#include "tensor/TF.h"
#include "tensor/Tensor.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// tensor_gpu_contract.cpp
namespace test_tensor_gpu_contract {
using namespace eve::tensor;

namespace {
using Operation = std::function<Tensor *(Tensor *)>;
void compareKernel(const char *name, Tensor *input, const Operation &operation,
                   zeroerr::TestContext *_ZEROERR_TEST_CONTEXT) {
    std::printf("GPU kernel contract: %s\n", name);
    auto                 *tf = TF::create();
    std::unique_ptr<Func> fn(tf->func());
    Tensor               *symbolic = nullptr;
    switch (input->getRank()) {
        case 1: symbolic = fn->input1(input->getDim0()); break;
        case 2: symbolic = fn->input2(input->getDim0(), input->getDim1()); break;
        case 3: symbolic = fn->input3(input->getDim0(), input->getDim1(), input->getDim2()); break;
        case 4: symbolic = fn->input4(input->getDim0(), input->getDim1(), input->getDim2(), input->getDim3()); break;
        default: REQUIRE(false); return;
    }
    std::unique_ptr<Tensor> traced(operation(symbolic));
    fn->setOutput(traced.get());
    std::unique_ptr<CompiledFunction> compiled(fn->compile());
    REQUIRE_EQ(compiled->getDevice(), std::string("gpu"));
    // Changing the feed detects stale uploads and arena contents on a reused program.
    for (int repeat = 0; repeat < 2; ++repeat) {
        std::unique_ptr<Tensor> feed(input->mulScalar(repeat == 0 ? 1.f : 0.5f));
        std::unique_ptr<Tensor> expected(operation(feed.get()));
        std::unique_ptr<Tensor> actual(compiled->run1(feed.get()));
        REQUIRE_EQ(actual->getSize(), expected->getSize());
        for (int i = 0; i < expected->getSize(); ++i) {
            REQUIRE(std::isfinite(actual->get(i)));
            REQUIRE(std::fabs(actual->get(i) - expected->get(i)) < 1e-3f * (1.f + std::fabs(expected->get(i))));
        }
    }
}
void fillPattern(Tensor *tensor) {
    for (int i = 0; i < tensor->getSize(); ++i) tensor->set(i, float((i * 7) % 19 - 9) / 10.f);
}
}  // namespace

TEST_CASE("tensor.gpu.kernelFamiliesMatchEager") {
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(gfx != nullptr);
    gfx->initHeadless(64, 64);
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> matrix(tf->zeros2(4, 3));
    fillPattern(matrix.get());
    const auto check = [&](const char *name, const Operation &op) {
        compareKernel(name, matrix.get(), op, _ZEROERR_TEST_CONTEXT);
    };
    check("sum axis 0", [&](Tensor *x) { return tf->sumAxis(x, 0, 0); });
    check("mean axis 1", [&](Tensor *x) { return tf->meanAxis(x, 1, 0); });
    check("min axis 0", [&](Tensor *x) { return tf->minAxis(x, 0, 0); });
    check("max axis 1", [&](Tensor *x) { return tf->maxAxis(x, 1, 0); });
    check("argmax", [&](Tensor *x) { return tf->argmax(x, 1, 0); });
    check("softmax noncontiguous axis", [&](Tensor *x) { return tf->softmax(x, 0); });
    check("log softmax", [&](Tensor *x) { return tf->logSoftmax(x, 1); });
    check("rms norm", [&](Tensor *x) { return tf->rmsnorm(x, 1e-5f); });
    check("permute", [&](Tensor *x) { return tf->permute2(x, 1, 0); });
    check("slice", [&](Tensor *x) { return tf->slice(x, 1, 1, 3); });
    std::unique_ptr<Tensor> scale(tf->fill1(3, 1.2f)), bias(tf->fill1(3, -0.2f));
    check("scaled rms norm", [&](Tensor *x) { return tf->rmsnormW(x, scale.get(), 1e-5f); });
    check("affine layer norm", [&](Tensor *x) { return tf->layernormWB(x, scale.get(), bias.get(), 1e-5f); });
    std::unique_ptr<Tensor> other(tf->fill2(1, 3, 0.4f));
    check("concat", [&](Tensor *x) { return tf->concat2(x, other.get(), 0); });
    check("broadcast addition", [&](Tensor *x) { return tf->add(x, scale.get()); });
    std::unique_ptr<Tensor> selected(tf->fill2(4, 3, 1.2f)), rejected(tf->fill2(4, 3, -0.2f));
    check("where", [&](Tensor *x) { return tf->where(x, selected.get(), rejected.get()); });
    std::unique_ptr<Tensor> indices(tf->zeros1(3));
    indices->set(0, -1.f);
    indices->set(1, 2.f);
    indices->set(2, 5.f);
    check("embedding clamps indices", [&](Tensor *x) { return tf->embedding(x, indices.get()); });
    std::unique_ptr<Tensor> projection(tf->zeros2(3, 5)), projectionBias(tf->fill1(5, 0.15f));
    fillPattern(projection.get());
    check("matrix bias activation with edge tiles", [&](Tensor *x) {
        std::unique_ptr<Tensor> product(tf->matmul(x, projection.get()));
        std::unique_ptr<Tensor> biased(tf->add(product.get(), projectionBias.get()));
        return tf->relu(biased.get());
    });
    std::unique_ptr<Tensor> attention(tf->zeros4(1, 1, 3, 5)), mask(tf->zeros4(1, 1, 3, 3));
    fillPattern(attention.get());
    mask->set(1, -1000.f);
    mask->set(2, -1000.f);
    mask->set(5, -1000.f);
    compareKernel(
        "masked attention with aliased inputs", attention.get(),
        [&](Tensor *x) { return tf->sdpaMasked(x, x, x, mask.get(), 0.4472136f); }, _ZEROERR_TEST_CONTEXT);


    std::unique_ptr<Tensor> image(tf->zeros4(1, 1, 5, 6));
    fillPattern(image.get());
    compareKernel(
        "max pool with padding", image.get(), [&](Tensor *x) { return tf->maxpool2d(x, 2, 1, 1); },
        _ZEROERR_TEST_CONTEXT);
    compareKernel(
        "average pool with padding", image.get(), [&](Tensor *x) { return tf->avgpool2d(x, 2, 1, 1); },
        _ZEROERR_TEST_CONTEXT);
    for (int mode : {0, 1})
        compareKernel(
            mode == 0 ? "nearest resize" : "bilinear resize", image.get(),
            [&](Tensor *x) { return tf->resize2d(x, 7, 3, mode); }, _ZEROERR_TEST_CONTEXT);
    std::unique_ptr<Tensor> signal(tf->zeros3(1, 1, 7)), weights(tf->fill3(2, 1, 3, 0.2f)),
        convBias(tf->fill1(2, -0.1f));
    convBias->set(1, 0.4f);
    fillPattern(signal.get());
    compareKernel(
        "conv1d bias and padding", signal.get(),
        [&](Tensor *x) { return tf->conv1dBias(x, weights.get(), convBias.get(), 2, 1); }, _ZEROERR_TEST_CONTEXT);
    std::unique_ptr<Tensor> spatialWeights(tf->fill4(2, 1, 3, 3, 0.2f)), columnBias(tf->fill1(6, 0.3f));
    fillPattern(columnBias.get());
    compareKernel(
        "conv2d channel bias and fused column bias", image.get(),
        [&](Tensor *x) {
            std::unique_ptr<Tensor> convolution(tf->conv2dBias(x, spatialWeights.get(), convBias.get(), 1, 1));
            std::unique_ptr<Tensor> biased(tf->add(convolution.get(), columnBias.get()));
            return tf->relu(biased.get());
        },
        _ZEROERR_TEST_CONTEXT);
    std::vector<float> values(513);
    float              sum = 0.f;
    for (size_t i = 0; i < values.size(); ++i) {
        values[i] = float(int(i % 7) - 3);
        sum += values[i];
    }
    for (int op : {0, 1, 2}) {
        float      actual   = 0.f;
        const bool executed = gpuReduce(values.data(), int(values.size()), op, actual);
        REQUIRE(executed);
        const float expected = op == 0 ? sum : (op == 1 ? -3.f : 3.f);
        REQUIRE(std::fabs(actual - expected) < 1e-4f);
    }
}

TEST_CASE("tensor.gpu.quantizedWeightsMatchEager") {
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(gfx != nullptr);
    gfx->initHeadless(64, 64);
    auto                   *tf = TF::create();
    std::unique_ptr<Tensor> input(tf->zeros2(3, 5)), weights(tf->zeros2(5, 7)), indices(tf->zeros1(3));
    fillPattern(input.get());
    fillPattern(weights.get());
    indices->set(0, 0.f);
    indices->set(1, 3.f);
    indices->set(2, 4.f);
    for (const char *dtype : {"fp16", "fp8", "fp4", "int8", "int4"}) {
        std::unique_ptr<Tensor> packed(tf->quantizeWeight(weights.get(), dtype, 8));
        compareKernel(
            dtype, input.get(), [&](Tensor *x) { return tf->matmul(x, packed.get()); }, _ZEROERR_TEST_CONTEXT);
        compareKernel(
            dtype, indices.get(), [&](Tensor *x) { return tf->embedding(packed.get(), x); }, _ZEROERR_TEST_CONTEXT);
    }
}
}  // namespace test_tensor_gpu_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_tensor_onnx_gpu)
#include "PrivateTestScope.h"
#include "common/config.h"
#if !defined(EVENGINE_WEBGPU)
#include <cstring>
#endif
#if !defined(EVENGINE_WEBGPU)
#include <memory>
#endif
#if !defined(EVENGINE_WEBGPU)
#include "gpgpu/ComputeProgram.h"
#endif
#if !defined(EVENGINE_WEBGPU)
#include "gpgpu/ComputeShader.h"
#endif
#if !defined(EVENGINE_WEBGPU)
#include "gpgpu/Gpgpu.h"
#endif
#if !defined(EVENGINE_WEBGPU)
#include "gpgpu/GpuBuffer.h"
#endif
#if !defined(EVENGINE_WEBGPU)
#include "gpgpu/Sequence.h"
#endif
#if !defined(EVENGINE_WEBGPU)
#include "graphics/Graphics.h"
#endif
#if !defined(EVENGINE_WEBGPU)
#include "graphics/vulkan/Graphics.h"
#endif
#if !defined(EVENGINE_WEBGPU)
#include "tensor/OnnxCompute.h"
#endif
#if !defined(EVENGINE_WEBGPU)
#include "tensor/OnnxModel.h"
#endif
#if !defined(EVENGINE_WEBGPU)
#include "zeroerr/unittest.h"
#endif
// tensor_onnx_gpu.cpp
namespace test_tensor_onnx_gpu {
#if !defined(EVENGINE_WEBGPU)


namespace {
void initialize() {
    static bool initialized = false;
    if (!initialized) {
        eve::graphics::Graphics::create()->initHeadless(64, 64);
        initialized = true;
    }
}
using Bytes = std::vector<uint8_t>;
void number(Bytes &b, uint64_t n) {
    while (n >= 128) {
        b.push_back(uint8_t(n) | 128);
        n >>= 7;
    }
    b.push_back(uint8_t(n));
}
void integer(Bytes &b, int tag, uint64_t n) {
    number(b, tag * 8);
    number(b, n);
}
void field(Bytes &b, int tag, const Bytes &value) {
    number(b, tag * 8 + 2);
    number(b, value.size());
    b.insert(b.end(), value.begin(), value.end());
}
void  text(Bytes &b, int tag, const std::string &s) { field(b, tag, Bytes(s.begin(), s.end())); }
Bytes chain(size_t count = 4096, float increment = 1, bool invalidReshape = false, bool mixed = false,
            int stages = 16) {
    Bytes graph, tensor, raw(count * sizeof(float));
    for (size_t i = 0; i < count; ++i) {
        float f = float(i);
        std::memcpy(raw.data() + i * 4, &f, 4);
    }
    integer(tensor, 1, count);
    integer(tensor, 2, 1);
    text(tensor, 8, "x");
    field(tensor, 9, raw);
    field(graph, 5, tensor);
    Bytes bias, one(4);
    float f = increment;
    std::memcpy(one.data(), &f, 4);
    integer(bias, 2, 1);
    text(bias, 8, "one");
    field(bias, 9, one);
    field(graph, 5, bias);
    std::string previous = "x";
    for (int i = 0; i < stages; ++i) {
        Bytes n;
        text(n, 1, previous);
        text(n, 1, "one");
        auto next = "v" + std::to_string(i);
        text(n, 2, next);
        text(n, 4, mixed ? std::vector<std::string>{"Add", "Mul", "Sub", "Div"}[i % 4] : "Add");
        field(graph, 1, n);
        previous = next;
        Bytes alias;
        text(alias, 1, previous);
        previous += "alias";
        text(alias, 2, previous);
        text(alias, 4, "Identity");
        field(graph, 1, alias);
    }
    if (invalidReshape) {
        Bytes   t, raw(8), node;
        int64_t dimension = int64_t(count) + 1;
        std::memcpy(raw.data(), &dimension, 8);
        integer(t, 1, 1);
        integer(t, 2, 7);
        text(t, 8, "badShape");
        field(t, 9, raw);
        field(graph, 5, t);
        text(node, 1, previous);
        text(node, 1, "badShape");
        text(node, 2, "invalid");
        text(node, 4, "Reshape");
        field(graph, 1, node);
        previous = "invalid";
    }
    Bytes out, opset, model;
    text(out, 1, previous);
    field(graph, 12, out);
    integer(opset, 2, 14);
    integer(model, 1, 7);
    field(model, 8, opset);
    field(model, 7, graph);
    return model;
}
}  // namespace
TEST_CASE("tensor.onnx.gpu.residentChain") {
    initialize();
    auto provider = eve::tensor::createOnnxGpuCompute();
    REQUIRE(provider.ok());
    auto model = eve::tensor::OnnxModel::load(chain());
    REQUIRE(model.ok());
    for (int repeat = 0; repeat < 2; ++repeat) {
        auto r = model.value()->runGpu({}, *provider.value());
        REQUIRE(r.ok());
        CHECK(r.value().dispatches == 16);
        CHECK(r.value().transfers.uploads == (repeat == 0 ? 2 : 0));
        CHECK(r.value().transfers.shaderCompilations == (repeat == 0 ? 1 : 0));
        CHECK(r.value().transfers.downloads == 1);
        CHECK(r.value().transfers.submissions == 1);
        const auto &raw = r.value().outputs[0].tensor.bytes;
        for (size_t i = 0; i < 4096; ++i) {
            float f;
            std::memcpy(&f, raw.data() + i * 4, 4);
            REQUIRE(f == float(i + 16));
        }
    }
}
TEST_CASE("gpgpu.sequence.stagingSizeReuse") {
    initialize();
    auto *gp = eve::gpgpu::Gpgpu::create();
    REQUIRE(gp->isAvailable());
    std::unique_ptr<eve::gpgpu::Sequence> seq(gp->newSequence());
    for (const auto &sizes :
         std::vector<std::vector<size_t>>{{4, 4096, 64}, {4096, 4, 64}, {8192, 8, 16384, 4}, {4, 16384, 8, 8192}}) {
        std::vector<std::unique_ptr<eve::gpgpu::GpuBuffer>> device, staging;
        std::vector<Bytes>                                  expected;
        seq->begin();
        for (size_t i = 0; i < sizes.size(); ++i) {
            Bytes src(sizes[i], uint8_t(i + 31));
            expected.push_back(src);
            device.emplace_back(gp->newBuffer(int(sizes[i])));
            staging.emplace_back(gp->newBuffer(int(sizes[i]), "staging"));
            seq->recordUpload(device.back().get(), src.data(), src.size());
            // recordUpload promises an immediate copy, including in a multi-upload batch.
            std::fill(src.begin(), src.end(), 0);
            seq->recordDownload(device.back().get(), staging.back().get(), sizes[i]);
        }
        REQUIRE(seq->submitAsync() == eve::gpgpu::SequenceStatus::Submitted);
        REQUIRE(seq->wait() == eve::gpgpu::SequenceStatus::Complete);
        for (size_t i = 0; i < sizes.size(); ++i) {
            Bytes actual(sizes[i]);
            staging[i]->downloadBytes(actual.data(), actual.size());
            REQUIRE(actual == expected[i]);
        }
    }
}

TEST_CASE("gpgpu.sequence.largeRebindingBatchesPreserveSnapshots") {
    initialize();
    auto *gp = eve::gpgpu::Gpgpu::create();
    REQUIRE(gp->isAvailable());
    std::unique_ptr<eve::gpgpu::GpuBuffer>     first(gp->newBuffer(4)), second(gp->newBuffer(4));
    std::unique_ptr<eve::gpgpu::ComputeShader> shader(
        gp->newShader("#version 450\nlayout(local_size_x=1)in;layout(std430,binding=0)buffer B{float x[];};"
                      "layout(push_constant)uniform P{float value[32];}pc;void main(){x[0]+=pc.value[0];}"));
    float zero = 0;
    first->uploadBytes(&zero, 4);
    second->uploadBytes(&zero, 4);
    auto record = [&](eve::gpgpu::Sequence &sequence, int count) {
        sequence.begin();
        for (int i = 0; i < count; ++i) {
            shader->bindBuffer(0, i % 2 ? second.get() : first.get());
            shader->setFloat(0, float(i + 1));
            sequence.recordDispatch(shader.get(), 1);
        }
    };
    // Abandon a recording that spans several descriptor pools. Its commands
    // must never execute, and its leases must not leak into later batches.
    {
        std::unique_ptr<eve::gpgpu::Sequence> canceled(gp->newSequence());
        record(*canceled, 192);
    }
    std::unique_ptr<eve::gpgpu::Sequence> sequence(gp->newSequence());
    float                                 expectedFirst = 0, expectedSecond = 0;
    for (int pass = 0; pass < 2; ++pass) {
        record(*sequence, 192);
        sequence->submit();
        for (int i = 0; i < 192; ++i) (i % 2 ? expectedSecond : expectedFirst) += float(i + 1);
        float actualFirst = 0, actualSecond = 0;
        first->downloadBytes(&actualFirst, 4);
        second->downloadBytes(&actualSecond, 4);
        REQUIRE(actualFirst == expectedFirst);
        REQUIRE(actualSecond == expectedSecond);
    }
    // Retiring one sequence must preserve snapshots held by another open
    // sequence, including snapshots allocated from earlier pools.
    std::unique_ptr<eve::gpgpu::Sequence> other(gp->newSequence());
    record(*sequence, 96);
    record(*other, 96);
    sequence->submit();
    other->submit();
    for (int i = 0; i < 96; ++i) (i % 2 ? expectedSecond : expectedFirst) += 2.f * float(i + 1);
    float actualFirst = 0, actualSecond = 0;
    first->downloadBytes(&actualFirst, 4);
    second->downloadBytes(&actualSecond, 4);
    REQUIRE(actualFirst == expectedFirst);
    REQUIRE(actualSecond == expectedSecond);
}

TEST_CASE("gpgpu.sequence.cancelRecordingRetiresDescriptors") {
    initialize();
    auto                                      *gp = eve::gpgpu::Gpgpu::create();
    std::unique_ptr<eve::gpgpu::GpuBuffer>     buffer(gp->newBuffer(4));
    std::unique_ptr<eve::gpgpu::ComputeShader> shader(
        gp->newShader("#version 450\nlayout(local_size_x=64)in;layout(std430,binding=0)buffer B{float x[];};void "
                      "main(){if(gl_GlobalInvocationID.x==0)x[0]*=2.0;}"));
    float value = 1;
    buffer->uploadBytes(&value, 4);
    for (int i = 0; i < 80; ++i) {
        std::unique_ptr<eve::gpgpu::Sequence> canceled(gp->newSequence());
        canceled->begin();
        shader->bindBuffer(0, buffer.get());
        canceled->recordDispatch(shader.get(), 1);
    }
    std::unique_ptr<eve::gpgpu::Sequence>  seq(gp->newSequence());
    std::unique_ptr<eve::gpgpu::GpuBuffer> staging(gp->newBuffer(4, "staging"));
    seq->begin();
    shader->bindBuffer(0, buffer.get());
    seq->recordDispatch(shader.get(), 1);
    seq->recordDownload(buffer.get(), staging.get(), 4);
    seq->submit();
    staging->downloadBytes(&value, 4);
    CHECK(value == 2);
}

TEST_CASE("tensor.onnx.gpu.sessionShapesAndLifetime") {
    auto *previous = eve::graphics::Graphics::create();
    auto  device   = std::make_unique<eve::graphics::vulkan::Graphics>();
    eve::ModuleManager::insert("Graphics", device.get());
    struct Restore {
        eve::graphics::Graphics *previous;
        ~Restore() { eve::ModuleManager::insert("Graphics", previous); }
    } restore{previous};
    device->initHeadless(64, 64);
    auto gpu = eve::tensor::createOnnxGpuCompute();
    REQUIRE(gpu.ok());
    auto original = eve::tensor::OnnxModel::load(chain());
    REQUIRE(original.ok());
    auto alternate = eve::tensor::OnnxModel::load(chain(2048, 2));
    REQUIRE(alternate.ok());
    for (int pass = 0; pass < 3; ++pass) {
        auto &model = pass == 1 ? alternate : original;
        auto  r     = model.value()->runGpu({}, *gpu.value());
        REQUIRE(r.ok());
        CHECK(r.value().transfers.shaderCompilations == (pass < 2 ? 1 : 0));
        const size_t count = pass == 1 ? 2048 : 4096;
        const float  added = pass == 1 ? 32.f : 16.f;
        REQUIRE(r.value().outputs[0].tensor.bytes.size() == count * 4);
        for (size_t i = 0; i < count; ++i) {
            float actual;
            std::memcpy(&actual, r.value().outputs[0].tensor.bytes.data() + i * 4, 4);
            REQUIRE(actual == float(i) + added);
        }
    }
    auto failing = eve::tensor::OnnxModel::load(chain(4096, 1, true));
    REQUIRE(failing.ok());
    auto failure = failing.value()->runGpu({}, *gpu.value());
    REQUIRE(!failure.ok());
    const std::vector<std::string> intermediate{"v15alias"};
    auto                           recovered = failing.value()->runGpu({}, *gpu.value(), intermediate);
    REQUIRE(recovered.ok());
    CHECK(recovered.value().transfers.shaderCompilations == 0);
    CHECK(recovered.value().transfers.uploads == 2);
    float recoveredValue = 0;
    std::memcpy(&recoveredValue, recovered.value().outputs[0].tensor.bytes.data(), 4);
    CHECK(recoveredValue == 16);
    // Session-first retirement cancels its observer and releases device resources.
    {
        auto second = eve::tensor::createOnnxGpuCompute();
        REQUIRE(second.ok());
        auto r = original.value()->runGpu({}, *second.value());
        REQUIRE(r.ok());
    }
    // Device-first retirement clears the surviving session before destroying Vulkan.
    device.reset();
    eve::ModuleManager::insert("Graphics", previous);
    auto stale = original.value()->runGpu({}, *gpu.value());
    REQUIRE(!stale.ok());
    CHECK(stale.error()->message().find("retired") != std::string::npos);
    gpu.value().reset();
}
TEST_CASE("tensor.onnx.gpu.parallelCompilation") {
    // Compilation has no dependency on a live Graphics device.
    auto bytecode = eve::gpgpu::compileComputeSpirv("#version 450\nlayout(local_size_x=64)in;void main(){}");
    REQUIRE(bytecode.ok());
    REQUIRE(bytecode.value().front() == 0x07230203);
    auto invalidHeader = eve::gpgpu::createComputeShader({});
    REQUIRE(!invalidHeader.ok());
    for (uint32_t invalid : {0u, 9u}) {
        auto r = eve::tensor::createOnnxGpuCompute(invalid);
        REQUIRE(!r.ok());
    }
    initialize();
    Bytes serial;
    for (uint32_t workers : {1u, 4u}) {
        auto provider = eve::tensor::createOnnxGpuCompute(workers);
        REQUIRE(provider.ok());
        auto model = eve::tensor::OnnxModel::load(chain(4096, 2, false, true, 80));
        REQUIRE(model.ok());
        for (int repeat = 0; repeat < 2; ++repeat) {
            auto r = model.value()->runGpu({}, *provider.value());
            REQUIRE(r.ok());
            CHECK(r.value().dispatches == 80);
            CHECK(r.value().transfers.shaderCompilations == (repeat ? 0 : 4));
            CHECK(r.value().transfers.compilerPeakWorkers <= workers);
            if (!repeat)
                CHECK(r.value().transfers.compilerPeakWorkers >= (workers == 1 ? 1 : 2));
            else
                CHECK(r.value().transfers.compilerPeakWorkers == 0);
            const auto &raw = r.value().outputs[0].tensor.bytes;
            for (size_t i = 0; i < 4096; ++i) {
                float actual;
                std::memcpy(&actual, raw.data() + i * 4, 4);
                REQUIRE(actual == float(i + 20));
            }
            if (serial.empty())
                serial = raw;
            else
                REQUIRE(raw == serial);
        }
        // A fresh, abandoned segment has active CPU compile jobs but no submitted
        // GPU work. Retry must rebuild those programs and re-upload its weights.
        auto canceled = eve::tensor::OnnxModel::load(chain(4093, 2, true, true));
        REQUIRE(canceled.ok());
        auto failed = canceled.value()->runGpu({}, *provider.value());
        REQUIRE(!failed.ok());
        const std::vector<std::string> requested{"v15alias"};
        auto                           retried = canceled.value()->runGpu({}, *provider.value(), requested);
        REQUIRE(retried.ok());
        CHECK(retried.value().transfers.shaderCompilations == 4);
        CHECK(retried.value().transfers.uploads == 2);
        // Compilation failure travels through the future and run cleanup;
        // subsequent valid work on the same session remains usable.
        eve::tensor::OnnxKernel bad{"#version 450\nvoid main(){missing_function();}", {}, 4, 1};
        auto                    rejected = provider.value()->dispatch(bad);
        REQUIRE(!rejected.ok());
        auto recovered = model.value()->runGpu({}, *provider.value());
        REQUIRE(recovered.ok());
        REQUIRE(recovered.value().outputs[0].tensor.bytes == serial);
    }
}
#endif
}  // namespace test_tensor_onnx_gpu
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_virtualgeometry_builder)
#include "PrivateTestScope.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "data/ByteData.h"
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
#include "virtualgeometry/Builder.h"
#include "virtualgeometry/LodSelection.h"
#include "virtualgeometry/VirtualGeometry.h"
#include "virtualgeometry/VirtualGeometryRenderer.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// virtualgeometry_builder.cpp
namespace test_virtualgeometry_builder {
// virtualgeometry_builder.cpp
using namespace eve::virtualgeometry;

namespace {

// Build a unit icosphere with `subdiv` levels (3 => 1280 tris, 4 => 5120).
struct IcoMesh {
    std::vector<float>         positions;
    std::vector<std::uint32_t> indices;

    int addVtx(float x, float y, float z) {
        float l = std::sqrt(x * x + y * y + z * z);
        positions.push_back(x / l);
        positions.push_back(y / l);
        positions.push_back(z / l);
        return static_cast<int>(positions.size() / 3) - 1;
    }
    void tri(int a, int b, int c) {
        indices.push_back(static_cast<std::uint32_t>(a));
        indices.push_back(static_cast<std::uint32_t>(b));
        indices.push_back(static_cast<std::uint32_t>(c));
    }
    int mid(int i0, int i1) {
        float x = (positions[3 * i0] + positions[3 * i1]) * 0.5f;
        float y = (positions[3 * i0 + 1] + positions[3 * i1 + 1]) * 0.5f;
        float z = (positions[3 * i0 + 2] + positions[3 * i1 + 2]) * 0.5f;
        return addVtx(x, y, z);
    }
    void build(int subdiv) {
        const float                       t    = (1.f + std::sqrt(5.f)) * 0.5f;
        std::vector<std::array<float, 3>> base = {{{-1, t, 0},
                                                   {1, t, 0},
                                                   {-1, -t, 0},
                                                   {1, -t, 0},
                                                   {0, -1, t},
                                                   {0, 1, t},
                                                   {0, -1, -t},
                                                   {0, 1, -t},
                                                   {t, 0, -1},
                                                   {t, 0, 1},
                                                   {-t, 0, -1},
                                                   {-t, 0, 1}}};
        int                               idx[12];
        for (int i = 0; i < 12; ++i) idx[i] = addVtx(base[i][0], base[i][1], base[i][2]);
        int faces[20][3] = {{0, 11, 5},  {0, 5, 1},  {0, 1, 7},  {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                            {11, 10, 2}, {10, 7, 6}, {7, 1, 8},  {3, 9, 4},  {3, 4, 2},   {3, 2, 6}, {3, 6, 8},
                            {3, 8, 9},   {4, 9, 5},  {2, 4, 11}, {6, 2, 10}, {8, 6, 7},   {9, 8, 1}};
        struct FT {
            int a, b, c;
        };
        std::vector<FT> cur;
        for (auto &f : faces) cur.push_back({idx[f[0]], idx[f[1]], idx[f[2]]});
        for (int d = 0; d < subdiv; ++d) {
            std::vector<FT> nf;
            for (auto &x : cur) {
                int ab = mid(x.a, x.b), ac = mid(x.a, x.c), bc = mid(x.b, x.c);
                nf.push_back({x.a, ab, ac});
                nf.push_back({ab, x.b, bc});
                nf.push_back({ac, bc, x.c});
                nf.push_back({ab, bc, ac});
            }
            cur.swap(nf);
        }
        indices.clear();
        for (auto &x : cur) tri(x.a, x.b, x.c);
    }
};

[[maybe_unused]] VirtualGeometryAsset buildIco(int subdiv) {
    IcoMesh m;
    m.build(subdiv);
    VirtualGeometryBuilder            b;
    VirtualGeometryBuilder::MeshInput in;
    in.vertexCount = static_cast<int>(m.positions.size() / 3);
    in.positions   = m.positions.data();
    in.indices     = m.indices.data();
    in.indexCount  = static_cast<int>(m.indices.size());
    VirtualGeometryBuilder::Options opt;
    opt.minLodLevels           = 5;
    opt.mergeFactor            = 4;
    opt.maxTrianglesPerCluster = 124;
    opt.maxVerticesPerCluster  = 64;
    VirtualGeometryAsset a;
    REQUIRE(b.build(in, opt, a));
    return a;
}

}  // namespace


// ---------------------------------------------------------------------------
// LOD transition tests. Sweep camera distance and drive the CPU reference of
// the GPU cull accept-rule (LodSelection::selectClusters) to verify that the
// visible LOD changes continuously: fine LOD0 near, coarse LOD far, with a
// gradual mix in between and no gaps/overlaps (a full, hole-free cover).
// ---------------------------------------------------------------------------

namespace {

constexpr float kProjScale = 540.f;  // e.g. 1080px / (2*tan(30deg))
constexpr float kErrorPx   = 1.0f;

struct SelectionStats {
    int              selected = 0;
    std::vector<int> hist;  // per-LOD counts
};

[[maybe_unused]] SelectionStats selectAt(const VirtualGeometryAsset &a, float dist) {
    std::vector<std::uint32_t> sel;
    selectClusters(a, dist, kProjScale, kErrorPx, sel);
    SelectionStats s;
    s.selected = static_cast<int>(sel.size());
    lodHistogram(a, sel, s.hist);
    return s;
}

// Highest LOD level that has any selected cluster (0..maxLod); -1 if none.
[[maybe_unused]] int maxSelectedLod(const SelectionStats &s) {
    for (int l = static_cast<int>(s.hist.size()) - 1; l >= 0; --l)
        if (s.hist[l] > 0) return l;
    return -1;
}

}  // namespace


// virtualgeometry_gpu.cpp
using namespace eve::virtualgeometry;

namespace {

/** GPU cluster building only needs a live graphics device; headless init is enough. */
[[maybe_unused]] bool tryInitHeadlessGfx() {
    auto *gfx = eve::graphics::Graphics::create();
    if (!gfx) return false;
    gfx->initHeadless(320, 240);
    return true;
}

}  // namespace

TEST_CASE("virtualgeometry.module.create") {
    auto *mod = VirtualGeometry::create();
    REQUIRE(mod != nullptr);
    CHECK_EQ(mod->getName(), std::string("VirtualGeometry"));
}

TEST_CASE("virtualgeometry.gpu.buildIcosphere") {
    if (!tryInitHeadlessGfx()) return;

    auto *mod = VirtualGeometry::create();
    if (!mod->isAvailable()) return;
    auto *r = mod->newRenderer();
    REQUIRE(r != nullptr);
    REQUIRE(r->isReady());

    bool built = r->buildIcosphere(3);  // 1280-triangle unit sphere
    CHECK(built);
    CHECK(r->getClusterCount() > 0);
    CHECK(r->getMaxLodLevel() >= 2);
    CHECK(r->getTotalTriangleCount() > 0);
    CHECK(r->getLodLevel(0) >= 0);

    delete r;
}

TEST_CASE("virtualgeometry.gpu.cullRasterResolve") {
    if (!tryInitHeadlessGfx()) return;

    auto *mod = VirtualGeometry::create();
    if (!mod->isAvailable()) return;
    auto *r = mod->newRenderer();
    REQUIRE(r != nullptr);
    REQUIRE(r->isReady());
    bool built = r->buildIcosphere(3);
    REQUIRE(built);

    r->setViewport(128, 128, 60.f, 1.0f);
    r->setCameraSimple(0.f, 0.f, 3.2f);
    r->setModelYaw(0.3f);

    // GPU-driven culling + software rasterization into the visibility buffer.
    int visible = r->update();
    CHECK(visible > 0);  // some clusters are visible
    CHECK(visible <= r->getClusterCount());

    // Resolve the visibility buffer to RGBA.
    auto *rgba = r->resolveByteData();
    REQUIRE(rgba != nullptr);
    CHECK_EQ(r->getViewWidth(), 128);
    CHECK_EQ(r->getViewHeight(), 128);
    CHECK_EQ(rgba->getSize(), std::size_t(128 * 128 * 4));

    // Most of the projected sphere's pixels should be covered (non-empty color).
    const unsigned char *px      = static_cast<const unsigned char *>(rgba->getData());
    int                  covered = 0;
    for (std::size_t i = 0; i < rgba->getSize(); i += 4) {
        bool empty = px[i] == 4 && px[i + 1] == 6 && px[i + 2] == 12;
        if (!empty) ++covered;
    }
    CHECK(covered > 128 * 128 / 4);  // >25% coverage of the viewport

    delete rgba;
    delete r;
}

// Sweep the camera distance through the real GPU pipeline and verify LOD
// transitions are smooth: fine detail up close, coarse detail far away, with a
// continuous, hole-free cover the whole time.
TEST_CASE("virtualgeometry.gpu.lodTransitionSweep") {
    if (!tryInitHeadlessGfx()) return;

    auto *mod = VirtualGeometry::create();
    if (!mod->isAvailable()) return;
    auto *r = mod->newRenderer();
    REQUIRE(r != nullptr);
    REQUIRE(r->isReady());
    bool built = r->buildIcosphere(4);  // 5 LOD levels
    REQUIRE(built);
    r->setViewport(128, 128, 60.f, 1.0f);

    const float      distances[] = {1.6f, 2.2f, 3.2f, 5.0f, 9.0f};
    std::vector<int> visibleCounts;
    std::vector<int> coveredPixels;
    for (float d : distances) {
        r->setCameraSimple(0.f, 0.f, d);
        r->setModelYaw(0.2f);
        int visible = r->update();
        CHECK(visible > 0);
        visibleCounts.push_back(visible);

        auto *rgba = r->resolveByteData();
        REQUIRE(rgba != nullptr);
        const unsigned char *px      = static_cast<const unsigned char *>(rgba->getData());
        int                  covered = 0;
        for (std::size_t i = 0; i < rgba->getSize(); i += 4) {
            bool empty = px[i] == 4 && px[i + 1] == 6 && px[i + 2] == 12;
            if (!empty) ++covered;
        }
        coveredPixels.push_back(covered);
        delete rgba;
    }

    // Fine detail (many clusters) up close, coarse (few) far away.
    CHECK(visibleCounts[0] > visibleCounts.back());
    // Monotonic non-increase of visible clusters with distance.
    for (std::size_t i = 1; i < visibleCounts.size(); ++i) CHECK(visibleCounts[i] <= visibleCounts[i - 1]);
    // The sphere stays fully rendered at every distance (no holes).
    for (std::size_t i = 1; i < coveredPixels.size(); ++i) {
        CHECK(coveredPixels[i] > 0);                      // still visible far away
        CHECK(coveredPixels[i] <= coveredPixels[i - 1]);  // smaller when farther
    }

    delete r;
}
}  // namespace test_virtualgeometry_builder
#endif
