#include "asset/Evpack.h"
#include "asset/CanonicalImageCook.h"
#include "asset/RuntimeDefinition.h"
#include "asset/graphics/EvpackVegetationScene.h"
#include "asset/graphics/VegetationSceneLiveElements.h"
#include "asset/graphics/VegetationSceneInstance.h"
#include "filesystem/FileData.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::asset;
using namespace eve::asset_graphics;

namespace {
AssetRef sceneRef() {
    auto result = AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440000");
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

AssetRef sceneTemplateRef() {
    auto result = AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440002");
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

AssetRef duplicateSceneRef() {
    auto result = AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440003");
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

std::shared_ptr<const Evpack> scenePack(std::string_view definition, bool duplicateManager = false) {
    auto parsed = Value::fromJson(definition);
    REQUIRE(parsed.ok());
    auto encoded = encodeRuntimeDefinition(parsed.value());
    REQUIRE(encoded.ok());
    EvpackBuild build;
    build.packageId = *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31040");
    build.buildId = *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    build.variants = {{"windows", "x86_64", "vulkan", {}, "spirv-1.6", "high", {}}};
    auto imageRef = AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440001");
    REQUIRE(imageRef.ok());
    build.chunks.push_back({sceneRef().id(), "eve.vegetation-scene", SchemaVersion(1), 0,
                            EvpackChunkKind::Definition, 0, EvpackCodec::None, 8, {},
                            std::move(encoded).takeValue()});
    if (duplicateManager) {
        auto duplicateValue = Value::fromJson(definition);
        REQUIRE(duplicateValue.ok());
        auto duplicateDefinition = encodeRuntimeDefinition(duplicateValue.value());
        REQUIRE(duplicateDefinition.ok());
        build.chunks.push_back({duplicateSceneRef().id(), "eve.vegetation-scene", SchemaVersion(1), 0,
                                EvpackChunkKind::Definition, 0, EvpackCodec::None, 8, {},
                                std::move(duplicateDefinition).takeValue()});
    }
    const std::string templateJson =
        R"({"schema":"eve.scene-template","schemaVersion":3,"sourceGuid":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","coordinateSystem":"right-handed-x-right-y-up-minus-z-forward","nodes":[{"objectId":"018f6f22-2490-7ad2-bf58-4f1dbca31042","sourceFileId":1,"parentSourceFileId":0,"name":"Vegetation Scene","visible":true,"position":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}],"renderers":[]})";
    auto templateValue = Value::fromJson(templateJson);
    REQUIRE(templateValue.ok());
    auto templateDefinition = encodeRuntimeDefinition(templateValue.value());
    REQUIRE(templateDefinition.ok());
    build.chunks.push_back({sceneTemplateRef().id(), "eve.scene-template", SchemaVersion(3), 0,
                            EvpackChunkKind::Definition, 0, EvpackCodec::None, 8, {sceneRef().id()},
                            std::move(templateDefinition).takeValue()});
    const std::string imageJson =
        R"({"schema":"eve.image","schemaVersion":3,"width":1,"height":1,"encoding":"rgba8-mips","color":{"transfer":"linear"},"mipCount":1})";
    const std::array<std::uint8_t, 4> pixel{64, 128, 192, 255};
    auto cookedImage = cookCanonicalImageRgba8(
        {reinterpret_cast<const std::uint8_t*>(imageJson.data()), imageJson.size()}, pixel, 1024);
    REQUIRE(cookedImage.ok());
    auto imageDefinition = Value::fromJson(std::string_view(
        reinterpret_cast<const char*>(cookedImage.value().definition.data()), cookedImage.value().definition.size()));
    REQUIRE(imageDefinition.ok());
    auto encodedImage = encodeRuntimeDefinition(imageDefinition.value());
    REQUIRE(encodedImage.ok());
    build.chunks.push_back({imageRef.value().id(), "eve.image", SchemaVersion(3), 0, EvpackChunkKind::Definition, 0,
                            EvpackCodec::None, 8, {}, std::move(encodedImage).takeValue()});
    build.chunks.push_back({imageRef.value().id(), "eve.image", SchemaVersion(3), 0, EvpackChunkKind::Bulk, 1,
                            EvpackCodec::None, 8, {}, std::move(cookedImage).takeValue().bulk});
    auto bytes = buildEvpack(std::move(build));
    REQUIRE(bytes.ok());
    auto result = parseEvpack(bytes.value());
    REQUIRE(result.ok());
    return std::make_shared<const Evpack>(std::move(result).takeValue());
}

constexpr std::string_view validScene = R"json({
  "schema":"eve.vegetation-scene","schemaVersion":1,"sourceGuid":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "control":{"values":[3.25,0.75,0.5,0.4,0.8,0.7,0.6,0.55,0.45,0.35,2,0.25,0.2,3,4,5,6],
    "globalColor":[0.2,0.3,0.4,0.5],"overlayColor":[0.6,0.7,0.8,1],
    "overlayAlbedoGuid":"11111111111111111111111111111111","overlayNormalGuid":"","noiseTextureGuid":""},
  "details":{"layers":[2,3,4],"global":[0.6,0.7,0.8,0.9],"colorMask":[0.1,0.2],
    "overlayMask":[0.3,0.4],"alphaThreshold":0.75,"perspective":[1,2,3],
    "motionHighlight":[2,3,4],"bending":[1.2,11,5],"flutter":[0.3,17,8],"interactionAmplitude":1.5},
  "motion":{"values":[0.8,2,1.1,1.2,1.3,2,1,75],"direction":[0.6,0.8],"noiseTextureGuid":"22222222222222222222222222222222"},
  "volume":{"values":[1.5,20,10,0.25],"colors":[10,1024,512],"extras":[-1,256,256],
    "motion":[20,2048,1024],"vertex":[10,512,512]},
  "elements":[{"sourceFileId":99,"shaderGuid":"8aca4d3e46e8b7b458ec65a9bb11f144","kind":"motion-interaction","channel":2,
    "properties":[{"name":"_MotionPower","type":2,"textureGuid":"","textureAsset":"","vector":[0,0,0,0],"value":0.25}],"enabled":true,"visibility":-1,
    "position":[1,2,3],"rotation":[0,0,0,1],"scale":[4,4,4],"layers":1,"intensity":0.8,
    "value":[1,1,0,1],"seasonal":false,"seasons":[[1,1,1,1],[1,1,1,1],[1,1,1,1],[1,1,1,1]],
    "textureGuid":"33333333333333333333333333333333","textureAsset":"asset://550e8400-e29b-41d4-a716-446655440001","remap":[0,1,0,1,0,0],"blendRgb":2,"blendAlpha":0,
    "directionMode":20,"invertDirection":false,"volumeFade":true,"motionMode":15,"motionPower":0.25}]
})json";
}  // namespace

TEST_CASE("asset.graphics.vegetationSceneLoadsAndProjectsAtomically") {
    auto pack = scenePack(validScene);
    EvpackResourceReader reader(pack);
    EvpackVegetationSceneLoader loader(reader);
    EvpackCapabilities capabilities{"windows", "x86_64", "vulkan", {}, {"spirv-1.6"}, {"high"}, {}};
    auto loaded = loader.load(sceneRef(), capabilities);
    REQUIRE(loaded.ok());
    CHECK_EQ(loaded.value().sourceGuid, std::string("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    auto associated = VegetationSceneAssociationLoader(reader).load(sceneTemplateRef(), capabilities);
    REQUIRE(associated.ok());
    CHECK_EQ(associated.value().scene.sourceGuid, loaded.value().sourceGuid);
    CHECK_EQ(associated.value().manager.asset, sceneRef());
    CHECK_EQ(associated.value().masks.size(), std::size_t(1));
    CHECK_EQ(loaded.value().control.season, 3.25f);
    CHECK_EQ(loaded.value().details.motionLayer, std::uint8_t(4));
    CHECK_EQ(loaded.value().motion.windPower, .8f);
    CHECK_EQ(loaded.value().motion.direction[1], .8f);
    CHECK_EQ(loaded.value().volume.visibility, std::uint32_t(20));
    CHECK_EQ(loaded.value().volume.channels[2].width, std::uint32_t(2048));
    REQUIRE_EQ(loaded.value().elements.size(), std::size_t(1));
    CHECK_EQ(loaded.value().elements.front().kind, std::string("motion-interaction"));
    CHECK_EQ(loaded.value().elements.front().motionPower, .25f);
    CHECK_EQ(loaded.value().elements.front().blendRgb, 2);
    CHECK_EQ(loaded.value().elements.front().blendAlpha, 0);
    REQUIRE_EQ(loaded.value().elements.front().properties.size(), std::size_t(1));
    CHECK_EQ(loaded.value().elements.front().properties.front().name, std::string("_MotionPower"));
    auto masks = loadVegetationSceneElementMasks(reader, loaded.value(), capabilities);
    REQUIRE(masks.ok());
    REQUIRE_EQ(masks.value().size(), std::size_t(1));
    const auto& mask = masks.value().at("33333333333333333333333333333333");
    CHECK_EQ(mask.width, std::uint32_t(1));
    CHECK(std::abs(mask.pixels.front().g - 128.f / 255.f) < 1e-6f);

    VegetationSceneElementPixelInput pixelInput;
    pixelInput.mainSample = {.75f, .25f, .5f, 1.f};
    pixelInput.volumeFade = .5f;
    auto interaction = evaluateVegetationSceneElementPixel(loaded.value().elements.front(), pixelInput);
    REQUIRE(interaction.ok());
    CHECK(std::abs(interaction.value().value.r - .75f) < 1e-5f);
    CHECK(std::abs(interaction.value().value.g - .25f) < 1e-5f);
    CHECK_EQ(interaction.value().value.b, .25f);
    CHECK(std::abs(interaction.value().value.a - .4f) < 2e-4f);
    CHECK_EQ(interaction.value().colorMask, std::uint8_t(15));

    auto element = loaded.value().elements.front();
    element.volumeFade = false;
    element.seasonal = false;
    element.value = {.5f, .25f, .75f, .8f};
    element.kind = "color-tint";
    auto tint = evaluateVegetationSceneElementPixel(element, pixelInput);
    REQUIRE(tint.ok());
    CHECK(std::abs(tint.value().value.r - .375f) < 1e-5f);
    CHECK(std::abs(tint.value().value.g - .0625f) < 1e-5f);
    CHECK(std::abs(tint.value().value.b - .375f) < 1e-5f);
    CHECK(std::abs(tint.value().value.a - .64f) < 2e-4f);

    element.kind = "extras-overlay";
    auto overlay = evaluateVegetationSceneElementPixel(element, pixelInput);
    REQUIRE(overlay.ok());
    CHECK(std::abs(overlay.value().value.b - .375f) < 1e-5f);
    const auto composedOverlay = composeVegetationSceneElementPixel({.1f, .2f, .3f, .4f}, overlay.value());
    CHECK_EQ(composedOverlay.r, .1f);
    CHECK_EQ(composedOverlay.g, .2f);
    CHECK(std::abs(composedOverlay.b - .36f) < 2e-4f);
    CHECK_EQ(composedOverlay.a, .4f);
    element.kind = "motion-wind-power";
    auto wind = evaluateVegetationSceneElementPixel(element, pixelInput);
    REQUIRE(wind.ok());
    CHECK(std::abs(wind.value().value.b - .375f) < 1e-5f);
    element.kind = "vertex-size";
    element.blendAlpha = 0;
    auto size = evaluateVegetationSceneElementPixel(element, pixelInput);
    REQUIRE(size.ok());
    CHECK(std::abs(size.value().value.r - .375f) < 1e-5f);
    CHECK(std::abs(size.value().value.a - .5f) < 2e-4f);
    const auto composedSize = composeVegetationSceneElementPixel({.1f, .2f, .3f, .8f}, size.value());
    CHECK(std::abs(composedSize.a - .4f) < 2e-4f);

    const std::array<std::string_view, 19> executableKinds{
        "color-effect", "color-map", "color-noise", "color-tint", "extras-alpha", "extras-emissive",
        "extras-overlay", "extras-wetness", "motion-advanced", "motion-interaction", "motion-wind-power",
        "vertex-conform-model", "vertex-conform-simple", "vertex-conform-terrain", "vertex-height-offset",
        "vertex-height", "vertex-orientation-model", "vertex-orientation-terrain", "vertex-size"};
    for (const auto kind : executableKinds) {
        element.kind = kind;
        auto evaluated = evaluateVegetationSceneElementPixel(element, pixelInput);
        REQUIRE(evaluated.ok());
    }

    graphics::VegetationAtlas baseAtlas;
    baseAtlas.width = baseAtlas.height = 1;
    baseAtlas.center = {1.f, 2.f, 3.f};
    baseAtlas.extent = {1.f, 1.f, 1.f};
    for (auto& channelPixels : baseAtlas.channels) channelPixels.assign(1, glm::vec4(0.f));
    auto bakedElements = bakeVegetationSceneElements(loaded.value(), masks.value(), baseAtlas);
    REQUIRE(bakedElements.ok());
    CHECK(bakedElements.value().channels[2][0].r > .19f);
    CHECK(bakedElements.value().channels[2][0].r < .21f);
    CHECK(bakedElements.value().channels[2][0].a > .79f);
    CHECK_EQ(bakedElements.value().channels[0][0].r, 0.f);

    auto nativeAtlas = convertVegetationSceneAtlasToNative(bakedElements.value());
    REQUIRE(nativeAtlas.ok());
    CHECK(std::abs(nativeAtlas.value().channels[2][0].x -
                   (bakedElements.value().channels[2][0].x * 2.f - 1.f)) < 1e-6f);
    CHECK(std::abs(nativeAtlas.value().channels[2][0].y -
                   (bakedElements.value().channels[2][0].y * 2.f - 1.f)) < 1e-6f);
    for (const auto channel : {0u, 1u, 3u})
        for (glm::length_t component = 0; component < 4; ++component)
            CHECK(nativeAtlas.value().channels[channel][0][component] ==
                  bakedElements.value().channels[channel][0][component]);

    graphics::PbrSurface surface;
    surface.vegetationEmission.enabled = true;
    surface.vegetationEmission.global = .5f;
    graphics::VegetationMotion motion;
    motion.time = 12;
    auto projected = projectVegetationScene(surface, motion, loaded.value());
    REQUIRE(projected.ok());
    CHECK_EQ(projected.value().season, 3.25f);
    CHECK_EQ(projected.value().surface.vegetationColor.fieldColor[2], .4f);
    CHECK_EQ(projected.value().surface.vegetationAlpha.global, .7f * .75f);
    CHECK_EQ(projected.value().surface.vegetationEmission.global, .4f);
    CHECK_EQ(projected.value().surface.vegetationMotion.fallback[2], .8f);
    CHECK_EQ(projected.value().surface.vegetationMotion.globalBranch, 1.2f);
    CHECK_EQ(projected.value().surface.vegetationMotion.globalDirection[0], .6f);
    CHECK_EQ(projected.value().motion.time, 12.0);
    CHECK_EQ(projected.value().motion.timeScale, 2.0);
    CHECK_EQ(projected.value().motion.fadeDistance, 75.f);

    auto* gfx = graphics::Graphics::create();
    gfx->initHeadless(16, 16);
    const std::array<std::uint8_t, 4> noisePixel{128, 128, 128, 255};
    auto* noise = gfx->newTexture(1, 1, noisePixel.data(), true, true);
    REQUIRE(noise != nullptr);
    VegetationSceneGpuBuild gpuBuild;
    gpuBuild.sourceRevision = 91;
    gpuBuild.runtime.motionNoise = noise;
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 4> dimensions{{{1, 1}, {2, 1}, {1, 2}, {2, 2}}};
    const std::array<glm::vec4, 4> defaults{{{1.f, 1.f, 1.f, 0.f}, {1.f, 0.f, 0.f, 1.f},
                                             {.5f, .5f, .8f, 0.f}, {0.f, 0.f, 0.f, 1.f}}};
    for (std::size_t channel = 0; channel < gpuBuild.channels.size(); ++channel) {
        for (auto& layer : gpuBuild.channels[channel].baseLayers) {
            layer.width = dimensions[channel].first;
            layer.height = dimensions[channel].second;
            layer.center = baseAtlas.center;
            layer.extent = baseAtlas.extent;
            layer.pixels.assign(std::size_t(layer.width) * layer.height, defaults[channel]);
        }
    }
    graphics::PbrSurface runtimeSurface;
    runtimeSurface.unlit = true;
    auto gpuRuntime = VegetationSceneGpuRuntime::create(*gfx, runtimeSurface, motion, loaded.value(), masks.value(),
                                                        gpuBuild);
    if (!gpuRuntime) std::fprintf(stderr, "vegetation scene GPU runtime: %s\n", gpuRuntime.error()->message().c_str());
    REQUIRE(gpuRuntime.ok());
    CHECK_EQ(gpuRuntime.value()->sourceRevision(), std::uint64_t(91));
    CHECK(gpuRuntime.value()->surface().vegetationMotion.texture != nullptr);
    CHECK_EQ(gpuRuntime.value()->surface().vegetationMotion.time, 12.0);
    CHECK_EQ(gpuRuntime.value()->surface().vegetationMotion.globalDirection[0], .6f);

    const float positions[]{-.08f, 0, .5f, .08f, 0, .5f, .08f, .8f, .5f, -.08f, .8f, .5f};
    const float normals[]{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float uvs[]{0, 0, 1, 0, 1, 1, 0, 1};
    const std::uint32_t indices[]{0, 2, 1, 0, 3, 2};
    auto* mesh = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    std::vector<float> deformation;
    for (int vertex = 0; vertex < 4; ++vertex)
        deformation.insert(deformation.end(), {0, 0, 0, 1, 0, 0, .5f, 1, 1});
    REQUIRE(mesh->adoptVegetationDeformationFactors(std::move(deformation)).ok());
    auto* restCanvas = gfx->newCanvas(64, 64);
    auto* movedCanvas = gfx->newCanvas(64, 64);
    REQUIRE(restCanvas != nullptr);
    REQUIRE(movedCanvas != nullptr);
    auto draw = [&](graphics::Canvas* canvas, const graphics::PbrSurface& drawSurface) {
        REQUIRE(gfx->setMesh3DPbrSurface(&drawSurface).ok());
        gfx->setMesh3DViewProj(glm::mat4(1));
        gfx->setMesh3DView(glm::mat4(1));
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DMaterial(0, 1);
        gfx->setMesh3DSurface(graphics::SurfaceMode::Opaque, graphics::BlendMode::Opaque, true, true, .5f, "cutoff");
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(0, 1, 0, 1));
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
    };
    draw(restCanvas, runtimeSurface);
    draw(movedCanvas, gpuRuntime.value()->surface());
    std::unique_ptr<eve::image::ImageData> restImage(restCanvas->newImageData());
    std::unique_ptr<eve::image::ImageData> movedImage(movedCanvas->newImageData());
    REQUIRE(restImage != nullptr);
    REQUIRE(movedImage != nullptr);
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
    const auto [restCount, restX] = centroid(*restImage);
    const auto [movedCount, movedX] = centroid(*movedImage);
    REQUIRE(restCount > 100);
    REQUIRE(movedCount >= 20);
    CHECK(std::abs(movedX - restX) > 2.f);
    const auto framePath = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) / "out/tve-scene-runtime.png";
    std::filesystem::create_directories(framePath.parent_path());
    std::unique_ptr<eve::filesystem::FileData> framePng(
        movedImage->encode(medialoader::FormatHandler::ENCODED_PNG, "tve-scene-runtime.png", false));
    REQUIRE(framePng != nullptr);
    std::ofstream frameOutput(framePath, std::ios::binary);
    frameOutput.write(static_cast<const char*>(framePng->getData()), std::streamsize(framePng->getSize()));
    REQUIRE(frameOutput.good());

    auto* originalMotionTexture = gpuRuntime.value()->surface().vegetationMotion.texture;
    auto staleBuild = gpuBuild;
    staleBuild.sourceRevision = 92;
    auto staleReplacement = gpuRuntime.value()->replace(90, runtimeSurface, motion, loaded.value(), masks.value(),
                                                        staleBuild);
    REQUIRE(!staleReplacement.ok());
    CHECK_EQ(gpuRuntime.value()->sourceRevision(), std::uint64_t(91));
    CHECK_EQ(gpuRuntime.value()->surface().vegetationMotion.texture, originalMotionTexture);

    auto invalidBuild = staleBuild;
    invalidBuild.channels[0].baseLayers[0].width = 0;
    auto invalidReplacement = gpuRuntime.value()->replace(91, runtimeSurface, motion, loaded.value(), masks.value(),
                                                          invalidBuild);
    REQUIRE(!invalidReplacement.ok());
    CHECK_EQ(gpuRuntime.value()->sourceRevision(), std::uint64_t(91));
    CHECK_EQ(gpuRuntime.value()->surface().vegetationMotion.texture, originalMotionTexture);

    auto replacement = gpuRuntime.value()->replace(91, runtimeSurface, motion, loaded.value(), masks.value(),
                                                   staleBuild);
    REQUIRE(replacement.ok());
    CHECK_EQ(replacement.value().revision, std::uint64_t(92));
    CHECK_EQ(replacement.value().deferredReleaseCount, std::size_t(0));
    CHECK_EQ(gpuRuntime.value()->sourceRevision(), std::uint64_t(92));
    CHECK(gpuRuntime.value()->surface().vegetationMotion.texture != originalMotionTexture);

    auto liveElements = VegetationSceneLiveElements::create(loaded.value(), 7001);
    REQUIRE(liveElements.ok());
    auto dynamicElement = loaded.value().elements.front();
    dynamicElement.sourceFileId = 9001;
    auto registrationBuild = gpuBuild;
    registrationBuild.sourceRevision = 93;
    auto staleRegistration = liveElements.value()->registerElement(
        0, 92, dynamicElement, *gpuRuntime.value(), runtimeSurface, motion, masks.value(), registrationBuild);
    REQUIRE(!staleRegistration.ok());
    CHECK_EQ(liveElements.value()->revision(), std::uint64_t(1));
    CHECK_EQ(gpuRuntime.value()->sourceRevision(), std::uint64_t(92));
    auto registration = liveElements.value()->registerElement(
        1, 92, dynamicElement, *gpuRuntime.value(), runtimeSurface, motion, masks.value(), registrationBuild);
    REQUIRE(registration.ok());
    CHECK(registration.value().handle.isValid());
    CHECK_EQ(registration.value().registryRevision, std::uint64_t(2));
    CHECK_EQ(registration.value().gpu.revision, std::uint64_t(93));
    auto registeredScene = liveElements.value()->snapshot();
    REQUIRE(registeredScene.ok());
    CHECK_EQ(registeredScene.value().elements.size(), loaded.value().elements.size() + 1);

    auto withdrawalBuild = gpuBuild;
    withdrawalBuild.sourceRevision = 94;
    const VegetationSceneElementHandle foreignHandle(
        7002, registration.value().handle.index(), registration.value().handle.generation());
    auto foreignWithdrawal = liveElements.value()->withdrawElement(
        2, 93, foreignHandle, *gpuRuntime.value(), runtimeSurface, motion, masks.value(), withdrawalBuild);
    REQUIRE(!foreignWithdrawal.ok());
    CHECK_EQ(gpuRuntime.value()->sourceRevision(), std::uint64_t(93));
    auto withdrawal = liveElements.value()->withdrawElement(
        2, 93, registration.value().handle, *gpuRuntime.value(), runtimeSurface, motion, masks.value(), withdrawalBuild);
    REQUIRE(withdrawal.ok());
    CHECK_EQ(withdrawal.value().registryRevision, std::uint64_t(3));
    CHECK_EQ(withdrawal.value().gpu.revision, std::uint64_t(94));
    auto withdrawnScene = liveElements.value()->snapshot();
    REQUIRE(withdrawnScene.ok());
    CHECK_EQ(withdrawnScene.value().elements.size(), loaded.value().elements.size());
    auto repeatedWithdrawal = liveElements.value()->withdrawElement(
        3, 94, registration.value().handle, *gpuRuntime.value(), runtimeSurface, motion, masks.value(), withdrawalBuild);
    REQUIRE(!repeatedWithdrawal.ok());
    auto reuseBuild = gpuBuild;
    reuseBuild.sourceRevision = 95;
    auto reusedRegistration = liveElements.value()->registerElement(
        3, 94, dynamicElement, *gpuRuntime.value(), runtimeSurface, motion, masks.value(), reuseBuild);
    REQUIRE(reusedRegistration.ok());
    CHECK_EQ(reusedRegistration.value().handle.index(), registration.value().handle.index());
    CHECK(reusedRegistration.value().handle.generation() != registration.value().handle.generation());
    auto finalWithdrawalBuild = gpuBuild;
    finalWithdrawalBuild.sourceRevision = 96;
    auto staleGenerationWithdrawal = liveElements.value()->withdrawElement(
        4, 95, registration.value().handle, *gpuRuntime.value(), runtimeSurface, motion, masks.value(),
        finalWithdrawalBuild);
    REQUIRE(!staleGenerationWithdrawal.ok());
    auto finalWithdrawal = liveElements.value()->withdrawElement(
        4, 95, reusedRegistration.value().handle, *gpuRuntime.value(), runtimeSurface, motion, masks.value(),
        finalWithdrawalBuild);
    REQUIRE(finalWithdrawal.ok());
    CHECK_EQ(gpuRuntime.value()->sourceRevision(), std::uint64_t(96));
    REQUIRE(gpuRuntime.value()->release().ok());
    CHECK(gpuRuntime.value()->surface().vegetationMotion.texture == nullptr);

    auto instanceBuild = gpuBuild;
    instanceBuild.sourceRevision = 101;
    auto instance = VegetationSceneInstance::create(reader, *gfx, sceneTemplateRef(), capabilities, runtimeSurface,
                                                     motion, instanceBuild, 8001);
    REQUIRE(instance.ok());
    CHECK_EQ(instance.value()->scene().sourceGuid, loaded.value().sourceGuid);
    CHECK(instance.value()->surface().vegetationMotion.texture != nullptr);
    auto instanceRegisterBuild = gpuBuild;
    instanceRegisterBuild.sourceRevision = 102;
    auto instanceRegistration = instance.value()->registerElement(1, dynamicElement, instanceRegisterBuild);
    REQUIRE(instanceRegistration.ok());
    auto instanceWithdrawBuild = gpuBuild;
    instanceWithdrawBuild.sourceRevision = 103;
    auto instanceWithdrawal = instance.value()->withdrawElement(
        2, instanceRegistration.value().handle, instanceWithdrawBuild);
    REQUIRE(instanceWithdrawal.ok());
    REQUIRE(instance.value()->release().ok());
    REQUIRE(gfx->releaseTexture(noise));

    auto invalid = Value::fromJson(validScene);
    REQUIRE(invalid.ok());
    (*invalid.value().getIf<Value::Object>())["future"] = Value(true);
    auto text = invalid.value().toJson();
    REQUIRE(text.ok());
    auto invalidPack = scenePack(text.value());
    EvpackResourceReader invalidReader(invalidPack);
    auto rejected = EvpackVegetationSceneLoader(invalidReader).load(sceneRef(), capabilities);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::ParseError);

    auto fractional = Value::fromJson(validScene);
    REQUIRE(fractional.ok());
    auto& fractionalVolume = *fractional.value().getIf<Value::Object>()->at("volume").getIf<Value::Object>();
    fractionalVolume["colors"].getIf<Value::Array>()->at(1) = Value(1024.5);
    auto fractionalText = fractional.value().toJson();
    REQUIRE(fractionalText.ok());
    auto fractionalPack = scenePack(fractionalText.value());
    EvpackResourceReader fractionalReader(fractionalPack);
    auto fractionalRejected = EvpackVegetationSceneLoader(fractionalReader).load(sceneRef(), capabilities);
    REQUIRE(!fractionalRejected.ok());
    CHECK_EQ(fractionalRejected.error()->code(), DiagnosticCode::InvalidArgument);

    EvpackResourceReader duplicateReader(scenePack(validScene, true));
    auto duplicateAssociation = VegetationSceneAssociationLoader(duplicateReader).load(sceneTemplateRef(), capabilities);
    REQUIRE(!duplicateAssociation.ok());
    CHECK_EQ(duplicateAssociation.error()->code(), DiagnosticCode::Conflict);
}
