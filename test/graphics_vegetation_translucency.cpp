#include <cmath>
#include <limits>
#include <memory>
#include "asset/AssetCooker.h"
#include "asset/graphics/CookedMaterial.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "asset/import/UnityImporter.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/PbrSurface.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::graphics;

TEST_CASE("graphics.vegetation.translucency_validates_factors") {
    PbrSurface surface;
    REQUIRE(validatePbrSurface(surface).ok());
    surface.motionHighlightColor = {3, 2, 0};
    REQUIRE(validatePbrSurface(surface).ok());
    surface.motionHighlightColor[1] = -1;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.motionHighlightColor             = {0, 0, 0};
    surface.vegetationColor.overlayVariation = -0.1f;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.vegetationColor.overlayVariation  = .5f;
    surface.vegetationColor.overlayProjection = 1.1f;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.vegetationColor.overlayProjection    = .5f;
    surface.vegetationColor.vertexOcclusionAlpha = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.vegetationColor.vertexOcclusionAlpha = .5f;
    surface.vegetationColor.overlaySubsurface    = 1.1f;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.vegetationColor.overlaySubsurface = .5f;
    surface.translucency.intensity            = .5f;
    surface.translucency.scattering           = 0;
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.translucency.scattering = 2;
    surface.translucency.color[1]   = std::numeric_limits<float>::infinity();
    REQUIRE(!validatePbrSurface(surface).ok());
    surface.translucency.color[1]   = 1;
    surface.translucency.maskAmount = 1;
    REQUIRE(!validatePbrSurface(surface).ok());
}

TEST_CASE("graphics.vegetation.translucency_gpu_backlight_and_ambient") {
    auto* gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    auto* canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    const float    positions[]{-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    normals[]{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]{0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]{0, 2, 1, 0, 3, 2};
    auto*          mesh = gfx->newMeshFromArrays(positions, normals, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    Lighting3DPack lighting{};
    lighting.count               = 1;
    lighting.ambient             = {.2f, .2f, .2f, 0};
    lighting.lights[0].posRadius = {0, 0, -1, 0};
    lighting.lights[0].color     = {.5f, .6f, .7f, 0};
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->setMesh3DCameraPos({0, 0, 2});
    Color tint(.5f, .5f, .5f, 1);
    float materialRoughness = 1;
    auto  render            = [&](const PbrSurface& surface) {
        auto drawSurface = surface;
        drawSurface.vegetationColor.backfaceNormalMode = PbrVegetationBackfaceNormalMode::Same;
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DLighting(lighting);
        gfx->setMesh3DMaterial(0, materialRoughness);
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
        REQUIRE(gfx->setMesh3DPbrSurface(&drawSurface).ok());
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, tint);
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
        REQUIRE(image != nullptr);
        return image->getPixel(32, 32);
    };
    PbrSurface surface;
    const auto baseline                  = render(surface);
    surface.vegetationColor.overlayColor = {.2f, .4f, .8f};
    surface.vegetationColor.overlay      = .25f;
    surface.vegetationColor.wetness      = .5f;
    const auto           staged          = render(surface);
    const auto           originalTint    = tint;
    std::array<float, 3> stagedRgb{};
    for (size_t c = 0; c < 3; ++c) {
        const float overlay = std::lerp(.5f, surface.vegetationColor.overlayColor[size_t(c)], .25f);
        stagedRgb[c]        = std::lerp(overlay, overlay * overlay, .25f);
    }
    tint                       = Color(stagedRgb[0], stagedRgb[1], stagedRgb[2], 1);
    surface.vegetationColor    = {};
    const auto stagedReference = render(surface);
    CHECK(std::abs(staged.r - stagedReference.r) < .005f);
    CHECK(std::abs(staged.g - stagedReference.g) < .005f);
    CHECK(std::abs(staged.b - stagedReference.b) < .005f);
    tint                                         = originalTint;
    surface.vegetationColor.overlayColor         = {.2f, .4f, .8f};
    surface.vegetationColor.overlay              = .3f;
    surface.vegetationColor.wetness              = 0;
    surface.vegetationColor.overlayVariation     = .5f;
    surface.vegetationColor.overlayProjection    = 0;
    surface.vegetationColor.vertexOcclusionAlpha = .5f;
    std::vector<float> factors{.5f, 0, 1, 0, 0, .5f, 0, 1, 0, 0, .5f, 0, 1, 0, 0, .5f, 0, 1, 0, 0};
    REQUIRE(mesh->adoptVegetationFactors(std::move(factors)).ok());
    const auto  factored    = render(surface);
    const float overlayMask = std::clamp((.3f * .75f * .5f - .1f) / .1001f, 0.f, 1.f);
    tint =
        Color(std::lerp(.5f, .2f, overlayMask), std::lerp(.5f, .4f, overlayMask), std::lerp(.5f, .8f, overlayMask), 1);
    surface.vegetationColor      = {};
    const auto factoredReference = render(surface);
    CHECK(std::abs(factored.r - factoredReference.r) < .005f);
    CHECK(std::abs(factored.g - factoredReference.g) < .005f);
    CHECK(std::abs(factored.b - factoredReference.b) < .005f);
    tint                                         = originalTint;
    surface.vegetationColor.overlayColor         = {.2f, .4f, .8f};
    surface.vegetationColor.overlay              = 1;
    surface.vegetationColor.overlayVariation     = 0;
    surface.vegetationColor.overlayProjection    = 1;
    surface.vegetationColor.vertexOcclusionAlpha = 1;
    const auto sideProjected                     = render(surface);
    CHECK(std::abs(sideProjected.r - baseline.r) < .005f);
    CHECK(std::abs(sideProjected.g - baseline.g) < .005f);
    CHECK(std::abs(sideProjected.b - baseline.b) < .005f);
    std::vector<float> clearFactors;
    REQUIRE(mesh->adoptVegetationFactors(std::move(clearFactors)).ok());
    surface.vegetationColor                      = {};
    tint                                         = originalTint;
    surface.vegetationColor.overlayColor         = {.5f, .5f, .5f};
    surface.vegetationColor.overlay              = 1;
    surface.vegetationColor.overlayVariation     = 0;
    surface.vegetationColor.overlayProjection    = 0;
    surface.vegetationColor.vertexOcclusionAlpha = 1;
    surface.vegetationColor.overlaySmoothness    = .8f;
    const auto overlaySmooth                     = render(surface);
    surface.vegetationColor                      = {};
    materialRoughness                            = .2f;
    const auto overlaySmoothReference            = render(surface);
    CHECK(std::abs(overlaySmooth.r - overlaySmoothReference.r) < .005f);
    CHECK(std::abs(overlaySmooth.g - overlaySmoothReference.g) < .005f);
    CHECK(std::abs(overlaySmooth.b - overlaySmoothReference.b) < .005f);
    materialRoughness                       = 1;
    surface.vegetationColor.wetness         = .5f;
    surface.vegetationColor.wetnessContrast = 0;
    const auto wetSmooth                    = render(surface);
    surface.vegetationColor                 = {};
    materialRoughness                       = .25f;
    const auto wetSmoothReference           = render(surface);
    CHECK(std::abs(wetSmooth.r - wetSmoothReference.r) < .005f);
    CHECK(std::abs(wetSmooth.g - wetSmoothReference.g) < .005f);
    CHECK(std::abs(wetSmooth.b - wetSmoothReference.b) < .005f);
    materialRoughness = 1;
    const uint8_t tiltedNormal[]{204, 128, 255, 255};
    auto*         tiltedNormalTexture = gfx->newTexture(1, 1, tiltedNormal);
    REQUIRE(tiltedNormalTexture != nullptr);
    auto& normalBinding                          = surface.textures[size_t(PbrTextureSlot::Normal)];
    normalBinding.texture                        = tiltedNormalTexture;
    normalBinding.srgbDecode                     = false;
    surface.normalScale                          = 1;
    surface.vegetationColor.overlay              = 1;
    surface.vegetationColor.overlayVariation     = 0;
    surface.vegetationColor.overlayProjection    = 0;
    surface.vegetationColor.vertexOcclusionAlpha = 1;
    surface.vegetationColor.overlayColor         = {.5f, .5f, .5f};
    surface.vegetationColor.overlayNormalScale   = .25f;
    const auto overlayNormal                     = render(surface);
    surface.vegetationColor                      = {};
    surface.normalScale                          = .25f;
    const auto overlayNormalReference            = render(surface);
    CHECK(std::abs(overlayNormal.r - overlayNormalReference.r) < .005f);
    CHECK(std::abs(overlayNormal.g - overlayNormalReference.g) < .005f);
    CHECK(std::abs(overlayNormal.b - overlayNormalReference.b) < .005f);
    surface.textures[size_t(PbrTextureSlot::Normal)] = {};
    surface.normalScale                              = 1;
    tint                                             = originalTint;
    surface.translucency.intensity                   = .1f;
    surface.translucency.strength                    = 2;
    surface.translucency.direct                      = .8f;
    surface.translucency.ambient                     = .3f;
    const auto lit                                   = render(surface);
    // Center view lobe is approximately 0.5^2, not 1 from renormalizing
    // the distorted light. Two .5 albedo stages and source intensity*10 remain.
    CHECK(std::abs(lit.r - baseline.r - .065f) < .008f);
    CHECK(std::abs(lit.g - baseline.g - .078f) < .008f);
    CHECK(std::abs(lit.b - baseline.b - .091f) < .008f);
    surface.vegetationColor.overlay              = 1;
    surface.vegetationColor.overlayVariation     = 0;
    surface.vegetationColor.overlayProjection    = 0;
    surface.vegetationColor.vertexOcclusionAlpha = 1;
    surface.vegetationColor.overlayColor         = {.5f, .5f, .5f};
    surface.vegetationColor.overlaySmoothness    = 0;
    surface.vegetationColor.overlaySubsurface    = .25f;
    const auto overlaySubsurface                 = render(surface);
    surface.vegetationColor                      = {};
    surface.translucency.intensity               = .025f;
    const auto overlaySubsurfaceReference        = render(surface);
    CHECK(std::abs(overlaySubsurface.r - overlaySubsurfaceReference.r) < .005f);
    CHECK(std::abs(overlaySubsurface.g - overlaySubsurfaceReference.g) < .005f);
    CHECK(std::abs(overlaySubsurface.b - overlaySubsurfaceReference.b) < .005f);
    surface.translucency.intensity = .1f;
    std::vector<float> highlightValues(4, .5f);
    REQUIRE(mesh->adoptMotionHighlights(std::move(highlightValues)).ok());
    surface.motionHighlightColor   = {1, 1, 1};
    const auto highlighted         = render(surface);
    surface.translucency.intensity = 0;
    const auto highlightedBaseline = render(surface);
    // Final albedo receives highlight once; the earlier translucency albedo
    // remains unchanged. Applying it twice would give 2.25 instead of 1.5.
    CHECK(std::abs(highlighted.r - highlightedBaseline.r - (lit.r - baseline.r) * 1.5f) < .008f);
    CHECK(std::abs(highlighted.g - highlightedBaseline.g - (lit.g - baseline.g) * 1.5f) < .008f);
    CHECK(std::abs(highlighted.b - highlightedBaseline.b - (lit.b - baseline.b) * 1.5f) < .008f);
    surface.motionHighlightColor   = {0, 0, 0};
    surface.translucency.intensity = .1f;
    std::vector<float> clearHighlights;
    REQUIRE(mesh->adoptMotionHighlights(std::move(clearHighlights)).ok());
    surface.motionHighlightColor = {1, 1, 1};
    const auto absentHighlight   = render(surface);
    CHECK(std::abs(absentHighlight.r - lit.r) < .005f);
    // The left and right vertices differ. Center sampling must interpolate,
    // rather than use a flat value from one endpoint.
    std::vector<float> gradientHighlights{0, 1, 1, 0};
    REQUIRE(mesh->adoptMotionHighlights(std::move(gradientHighlights)).ok());
    const auto gradientLit         = render(surface);
    surface.translucency.intensity = 0;
    const auto gradientBaseline    = render(surface);
    CHECK(std::abs(gradientLit.r - gradientBaseline.r - (lit.r - baseline.r) * 1.5f) < .008f);
    surface.motionHighlightColor   = {0, 0, 0};
    surface.translucency.intensity = .1f;
    clearHighlights.clear();
    REQUIRE(mesh->adoptMotionHighlights(std::move(clearHighlights)).ok());
    // Translucency mask remains active without enabling the separate RGB tint mask.
    const uint8_t orm[]{255, 255, 0, 128};
    auto*         mask = gfx->newTexture(1, 1, orm);
    REQUIRE(mask != nullptr);
    auto& binding                    = surface.textures[size_t(PbrTextureSlot::MetallicRoughness)];
    binding.texture                  = mask;
    binding.srgbDecode               = false;
    surface.translucency.maskMinimum = 0;
    surface.translucency.maskMaximum = 1;
    surface.translucency.maskAmount  = 1;
    REQUIRE(!surface.colorMaskEnabled);
    const auto masked = render(surface);
    CHECK(std::abs(masked.r - baseline.r - .0326f) < .008f);
    CHECK(std::abs(masked.g - baseline.g - .0391f) < .008f);
    CHECK(std::abs(masked.b - baseline.b - .0457f) < .008f);
    surface.translucency.maskAmount = 0;
    surface.translucency.direct     = 0;
    const auto ambientOnly          = render(surface);
    CHECK(std::abs(ambientOnly.r - baseline.r - .015f) < .006f);
    surface.translucency.direct  = .8f;
    surface.translucency.ambient = 0;
    const auto directOnly        = render(surface);
    CHECK(std::abs(directOnly.r - baseline.r - .05f) < .006f);
    surface.translucency.globalIntensity = .5f;
    surface.translucency.overlay         = .5f;
    const auto globalReduced             = render(surface);
    CHECK(std::abs(globalReduced.r - baseline.r - .0125f) < .006f);
    lighting.lights[0].color       = {0, 0, 0, 0};
    const auto noLight             = render(surface);
    surface.translucency.intensity = 0;
    const auto noLightBaseline     = render(surface);
    CHECK(std::abs(noLight.r - noLightBaseline.r) < .005f);
    CHECK(std::abs(noLight.g - noLightBaseline.g) < .005f);
    CHECK(std::abs(noLight.b - noLightBaseline.b) < .005f);
    // Exercise the same lighting through Unity import, canonical cooking and
    // runtime image resolution, including the source HDR tint without conversion.
    eve::asset_import::UnityProjectImportRequest request;
    request.package = {
        *eve::PersistentId::parse("11111111-2222-4333-8444-555555555555"), "translucency.gpu", "1.0.0", {}};
    auto put = [&](const std::string& path, const std::string& text) {
        request.files[path] = {text.begin(), text.end()};
    };
    put("Assets/leaf.mat.meta", "guid: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");
    put("Assets/leaf.mat", R"(--- !u!21 &2100000
Material:
  m_Shader: {fileID: 4800000, guid: a933075b367f9b24981408633f72ff34, type: 3}
  m_SavedProperties:
    m_Floats:
    - _SubsurfaceValue: 0.1
    - _SubsurfaceScatteringValue: 2
    - _SubsurfaceAngleValue: 2
    - _SubsurfaceNormalValue: 0.5
    - _SubsurfaceDirectValue: 0.8
    - _SubsurfaceAmbientValue: 0.3
    - _SubsurfaceMaskValue: 0
    m_Colors:
    - _MainColor: {r: 0.5, g: 0.5, b: 0.5, a: 1}
    - _MotionHighlightColor: {r: 1, g: 1, b: 1, a: 1}
)");
    auto imported = eve::asset_import::prepareUnityProjectImport(request);
    REQUIRE(imported.ok());
    const auto ref = imported.value().manifest.entrypoints.at("Assets/leaf.mat");
    auto       eva = eve::asset::buildEvaArchive(imported.value().manifest, imported.value().entries);
    REQUIRE(eva.ok());
    auto archive = eve::asset::parseEvaArchive(eva.value());
    REQUIRE(archive.ok());
    auto profile = eve::asset::assetCookProfileForTarget("windows-x86_64-vulkan");
    REQUIRE(profile.ok());
    auto cooked = eve::asset::cookEvaToEvpack(archive.value(), profile.value());
    REQUIRE(cooked.ok());
    auto pack = eve::asset::parseEvpack(cooked.value().bytes);
    REQUIRE(pack.ok());
    eve::asset::EvpackResourceReader reader(std::make_shared<const eve::asset::Evpack>(std::move(pack).takeValue()));
    const eve::asset::EvpackCapabilities caps{"windows", "x86_64", "vulkan", {"rgba8"}, {"spirv-1.6"}, {"high"}, {}};
    auto                                 material = eve::asset_graphics::detail::readCookedMaterial(reader, ref, caps);
    REQUIRE(material.ok());
    eve::asset_graphics::GraphicsImageFactoryAdapter factory(*gfx);
    eve::asset_graphics::EvpackImageLoader           loader(reader, factory);
    std::vector<Texture*>                            images;
    for (size_t slot = 0; slot < material.value().images.size(); ++slot) {
        if (!material.value().images[slot]) continue;
        auto image = loader.load(*material.value().images[slot], caps);
        REQUIRE(image.ok());
        material.value().surface.textures[slot].texture = image.value().texture;
        images.push_back(image.value().texture);
    }
    tint                     = material.value().color;
    lighting.lights[0].color = {.5f, .6f, .7f, 0};
    const auto sourceLit     = render(material.value().surface);
    CHECK(std::abs(sourceLit.r - lit.r) < .005f);
    CHECK(std::abs(sourceLit.g - lit.g) < .005f);
    CHECK(std::abs(sourceLit.b - lit.b) < .005f);
    std::vector<float> importedHighlights(4, .5f);
    REQUIRE(mesh->adoptMotionHighlights(std::move(importedHighlights)).ok());
    const auto sourceHighlighted = render(material.value().surface);
    CHECK(std::abs(sourceHighlighted.r - highlighted.r) < .005f);
    CHECK(std::abs(sourceHighlighted.g - highlighted.g) < .005f);
    CHECK(std::abs(sourceHighlighted.b - highlighted.b) < .005f);
    for (auto* image : images) REQUIRE(factory.releaseImage(image).ok());
}
