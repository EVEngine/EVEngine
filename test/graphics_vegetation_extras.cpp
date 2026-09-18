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

TEST_CASE("graphics.vegetation.vertex_occlusion_rgb_matches_tve_remap") {
    auto* gfx = Graphics::create();
    gfx->initHeadless(64, 32);

    const float    positions[] = {-.95f, -.9f, .5f, .95f, -.9f, .5f, .95f, .9f, .5f, -.95f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto*          mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto*          canvas      = gfx->newCanvas(64, 32);
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
    auto* gfx = Graphics::create();
    gfx->initHeadless(32, 32);

    const float    positions[] = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 1, 2, 0, 2, 3};
    auto*          mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto*          canvas      = gfx->newCanvas(32, 32);
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
    auto* gfx = Graphics::create();
    gfx->initHeadless(32, 32);
    const float    positions[] = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto*          mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto*          canvas      = gfx->newCanvas(32, 32);
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
    auto* gfx = Graphics::create();
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
    auto*          mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto*          canvas      = gfx->newCanvas(64, 32);
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
    auto* gfx = Graphics::create();
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
    auto*          mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto*          visible     = gfx->newCanvas(64, 64);
    auto*          clipped     = gfx->newCanvas(64, 64);
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

    auto draw = [&](Canvas* canvas, float constantFade) {
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
    auto* gfx = Graphics::create();
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
    auto*                        sourceTexture    = gfx->newTexture(2, 1, sourcePixels.data());
    auto*                        referenceTexture = gfx->newTexture(2, 1, referencePixels.data());
    REQUIRE(sourceTexture != nullptr);
    REQUIRE(referenceTexture != nullptr);

    const float    positions[] = {-.95f, -.9f, .5f, .95f, -.9f, .5f, .95f, .9f, .5f, -.95f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto*          mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto*          actual      = gfx->newCanvas(64, 32);
    auto*          reference   = gfx->newCanvas(64, 32);
    REQUIRE(mesh != nullptr);
    REQUIRE(actual != nullptr);
    REQUIRE(reference != nullptr);

    auto draw = [&](Canvas* canvas, PbrSurface& surface) {
        auto& binding      = surface.textures[size_t(PbrTextureSlot::Emissive)];
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
    auto* gfx = Graphics::create();
    gfx->initHeadless(64, 32);

    const float    positions[] = {-.95f, -.9f, .5f, .95f, -.9f, .5f, .95f, .9f, .5f, -.95f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto*          mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto*          canvas      = gfx->newCanvas(64, 32);
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
    auto* gfx = Graphics::create();
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
    auto*          mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto*          canvas      = gfx->newCanvas(64, 32);
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
    auto* gfx = Graphics::create();
    gfx->initHeadless(96, 64);

    VegetationField   field;
    VegetationGlobals globals;
    globals.vertex = {0, 0, 0, .25f};
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
    auto*          mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    auto*          canvas      = gfx->newCanvas(96, 64);
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
            if (const auto pixel = image->getPixel(x, y);
                std::max({pixel.r, pixel.g, pixel.b}) > .2f) {
                const auto half = x < 48 ? 0 : 1;
                ++count[half];
                ySum[half] += y;
            }
    REQUIRE(count[0] >= 20);
    REQUIRE(count[1] > count[0] * 3);
    CHECK(std::abs(float(ySum[0]) / count[0] - float(ySum[1]) / count[1]) < 2.f);
    REQUIRE(gfx->releaseTexture(atlas.value().texture));
}

TEST_CASE("graphics.vegetation.motion_array_deforms_rest_stream_with_explicit_noise_and_time") {
    auto* gfx = Graphics::create();
    gfx->initHeadless(64, 64);

    VegetationField   field;
    VegetationGlobals globals;
    globals.motion = {1, 0, 1, 0};
    REQUIRE(field.replace(globals, {}).ok());
    auto atlas = uploadVegetationMotionAtlas(*gfx, field, {0, 0, 0}, {1, 1, 1}, 1, 1, false);
    REQUIRE(atlas.ok());
    const uint8_t noisePixel[]{128, 128, 128, 255};
    auto*         noise = gfx->newTexture(1, 1, noisePixel, true, true);
    REQUIRE(noise != nullptr);

    const float    positions[] = {-.08f, 0, .5f, .08f, 0, .5f, .08f, .8f, .5f, -.08f, .8f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uvs[]       = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto*          mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    std::vector<float> deformation;
    for (int i = 0; i < 4; ++i) deformation.insert(deformation.end(), {0, 0, 0, 1, 0, 0, .5f, 1, 1});
    REQUIRE(mesh->adoptVegetationDeformationFactors(std::move(deformation)).ok());
    auto* restCanvas   = gfx->newCanvas(64, 64);
    auto* motionCanvas = gfx->newCanvas(64, 64);
    REQUIRE(restCanvas != nullptr);
    REQUIRE(motionCanvas != nullptr);

    auto draw = [&](Canvas* canvas, const PbrSurface& surface) {
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
    auto centroid = [](const eve::image::ImageData& image) {
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
