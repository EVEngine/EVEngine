// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "render-offscreen",
  "cases": {
    "runtime-lifecycle": [
      "gpgpu.module.create",
      "gpgpu.procgen.transformChainRejectsInvalidLength"
    ],
    "script-render": [
      "gpgpu.ecsPack.roundTrip",
      "gpgpu.ecsPack.contiguousRange",
      "gpgpu.ecsPack.errors"
    ],
    "script-binding": [
      "gpgpu.buffer.readFloat32sReturnsAnArrayToScript",
      "gpgpu.ecsPack.registeredScriptEcsComponentsRoundTrip",
      "gpgpu.ecsPack.scriptEcsComponentsRoundTrip"
    ]
  },
  "module": "action",
  "modules": [
    "action",
    "asset_graphics",
    "fluids",
    "gpgpu",
    "graphics",
    "graphics_fog",
    "graphics_raytracing"
  ],
  "origins": [
    "action_prefab_runtime.cpp",
    "asset_graphics_vegetation_scene.cpp",
    "diffuse_light_probe.cpp",
    "fluids.cpp",
    "gpgpu.cpp",
    "graphics_backend_parity.cpp",
    "graphics_display_output.cpp",
    "graphics_fog.cpp",
    "graphics_gbuffer_depth.cpp",
    "graphics_headless.cpp",
    "graphics_map_fog.cpp",
    "graphics_pbr_surface.cpp",
    "graphics_raytracing_alignment.cpp",
    "graphics_skin_palette_render.cpp",
    "graphics_texture_mips.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_action_prefab_runtime)
#include "PrivateTestScope.h"
#include <filesystem>
#include <string_view>
#include "PathBesideSource.h"
#include "action/ActionBlockRuntime.h"
#include "action/ActionNotifyRegistry.h"
#include "action/ActionPrefabInstances.h"
#include "action/ActionPreview.h"
#include "common/Capability.h"
#include "filesystem/Filesystem.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem3D.h"
#include "model3d/Model3D.h"
#include "scene/loader/SceneLoader.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// action_prefab_runtime.cpp
namespace test_action_prefab_runtime {
namespace {

eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return *parsed;
}

std::size_t renderableCount(bool visibleOnly) {
    if (!ecs::current()->getManager<eve::graphics::Renderable3D>()) return 0;
    std::size_t count = 0;
    auto        view  = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::MeshRenderer>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [renderer] = *it;
        if (!visibleOnly || renderer->visible) ++count;
    }
    return count;
}

eve::action::ActionTimelineEvent boundary(eve::action::ActionTimelineEventKind kind, std::string_view item,
                                          eve::Value::Object payload) {
    return {kind,
            id("prefab-track:gameplay"),
            id(item),
            id("gameplay:prefab-spawn"),
            eve::Duration::zero(),
            std::move(payload)};
}

eve::action::ActionActiveBlock active(std::string_view item, eve::Value::Object payload) {
    return {id("prefab-track:gameplay"),
            id(item),
            id("gameplay:prefab-spawn"),
            eve::Duration::zero(),
            eve::Duration::fromNanoseconds(1000000000),
            std::move(payload)};
}

}  // namespace

TEST_CASE("actionPrefabRuntime.spawnsRealRenderablePoolsAndAppliesThreeLifecycles") {
    auto      *filesystem    = eve::filesystem::Filesystem::create();
    const auto testDirectory = std::filesystem::path(eve_test_path::sourceBesideTestDir(__FILE__)).parent_path();
    filesystem->allowMountingForPath(testDirectory.string());
    REQUIRE(filesystem->mount(testDirectory.string(), "", false));
    const std::string prefabUri = "fixtures/model3d/uv-origin.gltf";

    auto *graphics = eve::graphics::Graphics::create();
    graphics->initHeadless(64, 64);
    REQUIRE(eve::model3d::Model3D::create() != nullptr);
    REQUIRE(eve::sceneloader::SceneLoader::create() != nullptr);

    auto registry = eve::action::ActionNotifyRegistry::withBuiltins();
    REQUIRE(registry.ok());
    eve::action::ActionBlockRuntime runtime(registry.value());

    eve::Value::Object         recycledPayload{{"uri", prefabUri},
                                               {"lifecycle", "recycle_on_block_exit"},
                                               {"positionOffset", eve::Value::Array{2.0, 3.0, 4.0}}};
    eve::action::ActionAdvance enter;
    enter.id = eve::action::ActionExecutionId{101};
    enter.timelineEvents.push_back(
        boundary(eve::action::ActionTimelineEventKind::StateEnter, "prefab-item:recycled", recycledPayload));
    enter.activeBlocks.push_back(active("prefab-item:recycled", recycledPayload));
    eve::action::ActionNotifyContext context;
    context.executionId = enter.id;
    REQUIRE(runtime.apply(enter, context).ok());
    CHECK_EQ(renderableCount(true), 1u);
    auto transformed = ecs::View<eve::graphics::Renderable3D, eve::graphics::Renderable3D::Transform3D,
                                 eve::graphics::Renderable3D::MeshRenderer>();
    for (auto it = transformed.begin(); it != transformed.end(); ++it) {
        auto [transform, renderer] = *it;
        if (!renderer->visible) continue;
        CHECK_EQ(transform->x, 2.f);
        CHECK_EQ(transform->y, 3.f);
        CHECK_EQ(transform->z, 4.f);
    }
    const auto firstTotal = renderableCount(false);
    REQUIRE_EQ(firstTotal, 1u);

    eve::action::ActionAdvance leave;
    leave.id = enter.id;
    leave.timelineEvents.push_back(
        boundary(eve::action::ActionTimelineEventKind::StateExit, "prefab-item:recycled", recycledPayload));
    REQUIRE(runtime.apply(leave, context).ok());
    CHECK_EQ(renderableCount(true), 0u);

    enter.id            = eve::action::ActionExecutionId{102};
    context.executionId = enter.id;
    REQUIRE(runtime.apply(enter, context).ok());
    CHECK_EQ(renderableCount(true), 1u);
    CHECK_EQ(renderableCount(false), firstTotal);
    leave.id            = enter.id;
    context.executionId = leave.id;
    REQUIRE(runtime.apply(leave, context).ok());

    eve::Value::Object timedPayload{
        {"uri", prefabUri}, {"lifecycle", "custom_duration"}, {"customDurationSeconds", 0.1}};
    eve::action::ActionAdvance timedEnter;
    timedEnter.id = eve::action::ActionExecutionId{103};
    timedEnter.timelineEvents.push_back(
        boundary(eve::action::ActionTimelineEventKind::StateEnter, "prefab-item:timed", timedPayload));
    timedEnter.activeBlocks.push_back(active("prefab-item:timed", timedPayload));
    context.executionId = timedEnter.id;
    REQUIRE(runtime.apply(timedEnter, context).ok());
    CHECK_EQ(renderableCount(true), 1u);
    eve::action::ActionAdvance timedExit;
    timedExit.id           = timedEnter.id;
    timedExit.totalElapsed = eve::Duration::fromNanoseconds(50000000);
    timedExit.timelineEvents.push_back(
        boundary(eve::action::ActionTimelineEventKind::StateExit, "prefab-item:timed", timedPayload));
    REQUIRE(runtime.apply(timedExit, context).ok());
    CHECK_EQ(renderableCount(true), 1u);
    eve::action::ActionAdvance timedMaintenance;
    timedMaintenance.id           = timedEnter.id;
    timedMaintenance.totalElapsed = eve::Duration::fromNanoseconds(200000000);
    REQUIRE(runtime.apply(timedMaintenance, context).ok());
    CHECK_EQ(renderableCount(true), 0u);

    eve::Value::Object         independentPayload{{"uri", prefabUri}, {"lifecycle", "independent"}};
    eve::action::ActionAdvance independentEnter;
    independentEnter.id = eve::action::ActionExecutionId{104};
    independentEnter.timelineEvents.push_back(
        boundary(eve::action::ActionTimelineEventKind::StateEnter, "prefab-item:independent", independentPayload));
    independentEnter.activeBlocks.push_back(active("prefab-item:independent", independentPayload));
    context.executionId = independentEnter.id;
    REQUIRE(runtime.apply(independentEnter, context).ok());
    eve::action::ActionAdvance independentExit;
    independentExit.id = independentEnter.id;
    independentExit.timelineEvents.push_back(
        boundary(eve::action::ActionTimelineEventKind::StateExit, "prefab-item:independent", independentPayload));
    REQUIRE(runtime.apply(independentExit, context).ok());
    CHECK_EQ(renderableCount(true), 1u);
    auto *instances = eve::cap::query<eve::action::IActionPrefabInstances>();
    REQUIRE(instances != nullptr);
    const auto independent = instances->independentInstances();
    REQUIRE_EQ(independent.size(), 1u);
    CHECK_EQ(independent.front().uri, prefabUri);
    REQUIRE(instances->recycleIndependent(independent.front().handle).ok());
    CHECK_EQ(renderableCount(true), 0u);
    auto stale = instances->recycleIndependent(independent.front().handle);
    CHECK(!stale.ok());
    CHECK_EQ(stale.status().code(), eve::StatusCode::NotFound);

    REQUIRE_EQ(eve::cap::listenerCount<eve::action::IActionPreviewSinkProvider>(), 1u);
    auto *previewProvider = eve::cap::listenerAt<eve::action::IActionPreviewSinkProvider>(0);
    REQUIRE(previewProvider != nullptr);
    auto preview = previewProvider->createActionPreviewSink();
    REQUIRE(preview.ok());
    eve::action::ActionPreviewFrame frame;
    eve::Value::Object              previewPayload{{"uri", prefabUri},
                                                   {"lifecycle", "recycle_on_block_exit"},
                                                   {"positionOffset", eve::Value::Array{6.0, 7.0, 8.0}}};
    frame.activeBlocks.push_back(active("prefab-item:preview", previewPayload));
    REQUIRE(preview.value()->prepare(frame).ok());
    CHECK_EQ(renderableCount(true), 0u);
    preview.value()->present(frame);
    CHECK_EQ(renderableCount(true), 1u);
    for (auto it = transformed.begin(); it != transformed.end(); ++it) {
        auto [transform, renderer] = *it;
        if (!renderer->visible) continue;
        CHECK_EQ(transform->x, 6.f);
        CHECK_EQ(transform->y, 7.f);
        CHECK_EQ(transform->z, 8.f);
    }

    frame.activeBlocks.front().payload["positionOffset"] = eve::Value::Array{9.0, 10.0, 11.0};
    REQUIRE(preview.value()->prepare(frame).ok());
    CHECK_EQ(renderableCount(true), 1u);
    preview.value()->present(frame);
    CHECK_EQ(renderableCount(true), 1u);
    auto invalidFrame = frame;
    invalidFrame.activeBlocks.front().payload.erase("uri");
    CHECK(!preview.value()->prepare(invalidFrame).ok());
    CHECK_EQ(renderableCount(true), 1u);

    auto timedPreview                                                  = frame;
    timedPreview.activeBlocks.front().payload["lifecycle"]             = "custom_duration";
    timedPreview.activeBlocks.front().payload["customDurationSeconds"] = 0.1;
    timedPreview.activeBlocks.front().localTime                        = eve::Duration::fromNanoseconds(200000000);
    REQUIRE(preview.value()->prepare(timedPreview).ok());
    preview.value()->present(timedPreview);
    CHECK_EQ(renderableCount(true), 0u);
    eve::action::ActionPreviewFrame emptyFrame;
    REQUIRE(preview.value()->prepare(emptyFrame).ok());
    preview.value()->present(emptyFrame);
    CHECK_EQ(renderableCount(true), 0u);
}
}  // namespace test_action_prefab_runtime
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_asset_graphics_vegetation_scene)
#include "PrivateTestScope.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "asset/CanonicalImageCook.h"
#include "asset/Evpack.h"
#include "asset/RuntimeDefinition.h"
#include "asset/graphics/EvpackVegetationScene.h"
#include "asset/graphics/VegetationSceneInstance.h"
#include "asset/graphics/VegetationSceneLiveElements.h"
#include "filesystem/FileData.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// asset_graphics_vegetation_scene.cpp
namespace test_asset_graphics_vegetation_scene {
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
    build.buildId   = *PersistentId::parse("018f6f22-2490-7ad2-bf58-4f1dbca31041");
    build.variants  = {{"windows", "x86_64", "vulkan", {}, "spirv-1.6", "high", {}}};
    auto imageRef   = AssetRef::parse("asset://550e8400-e29b-41d4-a716-446655440001");
    REQUIRE(imageRef.ok());
    build.chunks.push_back({sceneRef().id(),
                            "eve.vegetation-scene",
                            SchemaVersion(1),
                            0,
                            EvpackChunkKind::Definition,
                            0,
                            EvpackCodec::None,
                            8,
                            {},
                            std::move(encoded).takeValue()});
    if (duplicateManager) {
        auto duplicateValue = Value::fromJson(definition);
        REQUIRE(duplicateValue.ok());
        auto duplicateDefinition = encodeRuntimeDefinition(duplicateValue.value());
        REQUIRE(duplicateDefinition.ok());
        build.chunks.push_back({duplicateSceneRef().id(),
                                "eve.vegetation-scene",
                                SchemaVersion(1),
                                0,
                                EvpackChunkKind::Definition,
                                0,
                                EvpackCodec::None,
                                8,
                                {},
                                std::move(duplicateDefinition).takeValue()});
    }
    const std::string templateJson =
        R"({"schema":"eve.scene-template","schemaVersion":3,"sourceGuid":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","coordinateSystem":"right-handed-x-right-y-up-minus-z-forward","nodes":[{"objectId":"018f6f22-2490-7ad2-bf58-4f1dbca31042","sourceFileId":1,"parentSourceFileId":0,"name":"Vegetation Scene","visible":true,"position":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}],"renderers":[]})";
    auto templateValue = Value::fromJson(templateJson);
    REQUIRE(templateValue.ok());
    auto templateDefinition = encodeRuntimeDefinition(templateValue.value());
    REQUIRE(templateDefinition.ok());
    build.chunks.push_back({sceneTemplateRef().id(),
                            "eve.scene-template",
                            SchemaVersion(3),
                            0,
                            EvpackChunkKind::Definition,
                            0,
                            EvpackCodec::None,
                            8,
                            {sceneRef().id()},
                            std::move(templateDefinition).takeValue()});
    const std::string imageJson =
        R"({"schema":"eve.image","schemaVersion":3,"width":1,"height":1,"encoding":"rgba8-mips","color":{"transfer":"linear"},"mipCount":1})";
    const std::array<std::uint8_t, 4> pixel{64, 128, 192, 255};
    auto                              cookedImage = cookCanonicalImageRgba8(
        {reinterpret_cast<const std::uint8_t *>(imageJson.data()), imageJson.size()}, pixel, 1024);
    REQUIRE(cookedImage.ok());
    auto imageDefinition = Value::fromJson(std::string_view(
        reinterpret_cast<const char *>(cookedImage.value().definition.data()), cookedImage.value().definition.size()));
    REQUIRE(imageDefinition.ok());
    auto encodedImage = encodeRuntimeDefinition(imageDefinition.value());
    REQUIRE(encodedImage.ok());
    build.chunks.push_back({imageRef.value().id(),
                            "eve.image",
                            SchemaVersion(3),
                            0,
                            EvpackChunkKind::Definition,
                            0,
                            EvpackCodec::None,
                            8,
                            {},
                            std::move(encodedImage).takeValue()});
    build.chunks.push_back({imageRef.value().id(),
                            "eve.image",
                            SchemaVersion(3),
                            0,
                            EvpackChunkKind::Bulk,
                            1,
                            EvpackCodec::None,
                            8,
                            {},
                            std::move(cookedImage).takeValue().bulk});
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
    auto                        pack = scenePack(validScene);
    EvpackResourceReader        reader(pack);
    EvpackVegetationSceneLoader loader(reader);
    EvpackCapabilities          capabilities{"windows", "x86_64", "vulkan", {}, {"spirv-1.6"}, {"high"}, {}};
    auto                        loaded = loader.load(sceneRef(), capabilities);
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
    const auto &mask = masks.value().at("33333333333333333333333333333333");
    CHECK_EQ(mask.width, std::uint32_t(1));
    CHECK(std::abs(mask.pixels.front().g - 128.f / 255.f) < 1e-6f);

    VegetationSceneElementPixelInput pixelInput;
    pixelInput.mainSample = {.75f, .25f, .5f, 1.f};
    pixelInput.volumeFade = .5f;
    auto interaction      = evaluateVegetationSceneElementPixel(loaded.value().elements.front(), pixelInput);
    REQUIRE(interaction.ok());
    CHECK(std::abs(interaction.value().value.r - .75f) < 1e-5f);
    CHECK(std::abs(interaction.value().value.g - .25f) < 1e-5f);
    CHECK_EQ(interaction.value().value.b, .25f);
    CHECK(std::abs(interaction.value().value.a - .4f) < 2e-4f);
    CHECK_EQ(interaction.value().colorMask, std::uint8_t(15));

    auto element       = loaded.value().elements.front();
    element.volumeFade = false;
    element.seasonal   = false;
    element.value      = {.5f, .25f, .75f, .8f};
    element.kind       = "color-tint";
    auto tint          = evaluateVegetationSceneElementPixel(element, pixelInput);
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
    auto wind    = evaluateVegetationSceneElementPixel(element, pixelInput);
    REQUIRE(wind.ok());
    CHECK(std::abs(wind.value().value.b - .375f) < 1e-5f);
    element.kind       = "vertex-size";
    element.blendAlpha = 0;
    auto size          = evaluateVegetationSceneElementPixel(element, pixelInput);
    REQUIRE(size.ok());
    CHECK(std::abs(size.value().value.r - .375f) < 1e-5f);
    CHECK(std::abs(size.value().value.a - .5f) < 2e-4f);
    const auto composedSize = composeVegetationSceneElementPixel({.1f, .2f, .3f, .8f}, size.value());
    CHECK(std::abs(composedSize.a - .4f) < 2e-4f);

    const std::array<std::string_view, 19> executableKinds{"color-effect",
                                                           "color-map",
                                                           "color-noise",
                                                           "color-tint",
                                                           "extras-alpha",
                                                           "extras-emissive",
                                                           "extras-overlay",
                                                           "extras-wetness",
                                                           "motion-advanced",
                                                           "motion-interaction",
                                                           "motion-wind-power",
                                                           "vertex-conform-model",
                                                           "vertex-conform-simple",
                                                           "vertex-conform-terrain",
                                                           "vertex-height-offset",
                                                           "vertex-height",
                                                           "vertex-orientation-model",
                                                           "vertex-orientation-terrain",
                                                           "vertex-size"};
    for (const auto kind : executableKinds) {
        element.kind   = kind;
        auto evaluated = evaluateVegetationSceneElementPixel(element, pixelInput);
        REQUIRE(evaluated.ok());
    }

    graphics::VegetationAtlas baseAtlas;
    baseAtlas.width = baseAtlas.height = 1;
    baseAtlas.center                   = {1.f, 2.f, 3.f};
    baseAtlas.extent                   = {1.f, 1.f, 1.f};
    for (auto &channelPixels : baseAtlas.channels) channelPixels.assign(1, glm::vec4(0.f));
    auto bakedElements = bakeVegetationSceneElements(loaded.value(), masks.value(), baseAtlas);
    REQUIRE(bakedElements.ok());
    CHECK(bakedElements.value().channels[2][0].r > .19f);
    CHECK(bakedElements.value().channels[2][0].r < .21f);
    CHECK(bakedElements.value().channels[2][0].a > .79f);
    CHECK_EQ(bakedElements.value().channels[0][0].r, 0.f);

    auto nativeAtlas = convertVegetationSceneAtlasToNative(bakedElements.value());
    REQUIRE(nativeAtlas.ok());
    CHECK(std::abs(nativeAtlas.value().channels[2][0].x - (bakedElements.value().channels[2][0].x * 2.f - 1.f)) <
          1e-6f);
    CHECK(std::abs(nativeAtlas.value().channels[2][0].y - (bakedElements.value().channels[2][0].y * 2.f - 1.f)) <
          1e-6f);
    for (const auto channel : {0u, 1u, 3u})
        for (glm::length_t component = 0; component < 4; ++component)
            CHECK(nativeAtlas.value().channels[channel][0][component] ==
                  bakedElements.value().channels[channel][0][component]);

    graphics::PbrSurface surface;
    surface.vegetationEmission.enabled = true;
    surface.vegetationEmission.global  = .5f;
    graphics::VegetationMotion motion;
    motion.time    = 12;
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

    auto *gfx = graphics::Graphics::create();
    gfx->initHeadless(16, 16);
    const std::array<std::uint8_t, 4> noisePixel{128, 128, 128, 255};
    auto                             *noise = gfx->newTexture(1, 1, noisePixel.data(), true, true);
    REQUIRE(noise != nullptr);
    VegetationSceneGpuBuild gpuBuild;
    gpuBuild.sourceRevision      = 91;
    gpuBuild.runtime.motionNoise = noise;
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 4> dimensions{{{1, 1}, {2, 1}, {1, 2}, {2, 2}}};
    const std::array<glm::vec4, 4>                               defaults{
                                      {{1.f, 1.f, 1.f, 0.f}, {1.f, 0.f, 0.f, 1.f}, {.5f, .5f, .8f, 0.f}, {0.f, 0.f, 0.f, 1.f}}};
    for (std::size_t channel = 0; channel < gpuBuild.channels.size(); ++channel) {
        for (auto &layer : gpuBuild.channels[channel].baseLayers) {
            layer.width  = dimensions[channel].first;
            layer.height = dimensions[channel].second;
            layer.center = baseAtlas.center;
            layer.extent = baseAtlas.extent;
            layer.pixels.assign(std::size_t(layer.width) * layer.height, defaults[channel]);
        }
    }
    graphics::PbrSurface runtimeSurface;
    runtimeSurface.unlit = true;
    auto gpuRuntime =
        VegetationSceneGpuRuntime::create(*gfx, runtimeSurface, motion, loaded.value(), masks.value(), gpuBuild);
    if (!gpuRuntime) std::fprintf(stderr, "vegetation scene GPU runtime: %s\n", gpuRuntime.error()->message().c_str());
    REQUIRE(gpuRuntime.ok());
    CHECK_EQ(gpuRuntime.value()->sourceRevision(), std::uint64_t(91));
    CHECK(gpuRuntime.value()->surface().vegetationMotion.texture != nullptr);
    CHECK_EQ(gpuRuntime.value()->surface().vegetationMotion.time, 12.0);
    CHECK_EQ(gpuRuntime.value()->surface().vegetationMotion.globalDirection[0], .6f);

    const float         positions[]{-.08f, 0, .5f, .08f, 0, .5f, .08f, .8f, .5f, -.08f, .8f, .5f};
    const float         normals[]{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float         uvs[]{0, 0, 1, 0, 1, 1, 0, 1};
    const std::uint32_t indices[]{0, 2, 1, 0, 3, 2};
    auto               *mesh = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    std::vector<float> deformation;
    for (int vertex = 0; vertex < 4; ++vertex) deformation.insert(deformation.end(), {0, 0, 0, 1, 0, 0, .5f, 1, 1});
    REQUIRE(mesh->adoptVegetationDeformationFactors(std::move(deformation)).ok());
    auto *restCanvas  = gfx->newCanvas(64, 64);
    auto *movedCanvas = gfx->newCanvas(64, 64);
    REQUIRE(restCanvas != nullptr);
    REQUIRE(movedCanvas != nullptr);
    auto draw = [&](graphics::Canvas *canvas, const graphics::PbrSurface &drawSurface) {
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
    const auto [restCount, restX]   = centroid(*restImage);
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
    frameOutput.write(static_cast<const char *>(framePng->getData()), std::streamsize(framePng->getSize()));
    REQUIRE(frameOutput.good());

    auto *originalMotionTexture = gpuRuntime.value()->surface().vegetationMotion.texture;
    auto  staleBuild            = gpuBuild;
    staleBuild.sourceRevision   = 92;
    auto staleReplacement =
        gpuRuntime.value()->replace(90, runtimeSurface, motion, loaded.value(), masks.value(), staleBuild);
    REQUIRE(!staleReplacement.ok());
    CHECK_EQ(gpuRuntime.value()->sourceRevision(), std::uint64_t(91));
    CHECK_EQ(gpuRuntime.value()->surface().vegetationMotion.texture, originalMotionTexture);

    auto invalidBuild                            = staleBuild;
    invalidBuild.channels[0].baseLayers[0].width = 0;
    auto invalidReplacement =
        gpuRuntime.value()->replace(91, runtimeSurface, motion, loaded.value(), masks.value(), invalidBuild);
    REQUIRE(!invalidReplacement.ok());
    CHECK_EQ(gpuRuntime.value()->sourceRevision(), std::uint64_t(91));
    CHECK_EQ(gpuRuntime.value()->surface().vegetationMotion.texture, originalMotionTexture);

    auto replacement =
        gpuRuntime.value()->replace(91, runtimeSurface, motion, loaded.value(), masks.value(), staleBuild);
    REQUIRE(replacement.ok());
    CHECK_EQ(replacement.value().revision, std::uint64_t(92));
    CHECK_EQ(replacement.value().deferredReleaseCount, std::size_t(0));
    CHECK_EQ(gpuRuntime.value()->sourceRevision(), std::uint64_t(92));
    CHECK(gpuRuntime.value()->surface().vegetationMotion.texture != originalMotionTexture);

    auto liveElements = VegetationSceneLiveElements::create(loaded.value(), 7001);
    REQUIRE(liveElements.ok());
    auto dynamicElement              = loaded.value().elements.front();
    dynamicElement.sourceFileId      = 9001;
    auto registrationBuild           = gpuBuild;
    registrationBuild.sourceRevision = 93;
    auto staleRegistration           = liveElements.value()->registerElement(
        0, 92, dynamicElement, *gpuRuntime.value(), runtimeSurface, motion, masks.value(), registrationBuild);
    REQUIRE(!staleRegistration.ok());
    CHECK_EQ(liveElements.value()->revision(), std::uint64_t(1));
    CHECK_EQ(gpuRuntime.value()->sourceRevision(), std::uint64_t(92));
    auto registration = liveElements.value()->registerElement(1, 92, dynamicElement, *gpuRuntime.value(),
                                                              runtimeSurface, motion, masks.value(), registrationBuild);
    REQUIRE(registration.ok());
    CHECK(registration.value().handle.isValid());
    CHECK_EQ(registration.value().registryRevision, std::uint64_t(2));
    CHECK_EQ(registration.value().gpu.revision, std::uint64_t(93));
    auto registeredScene = liveElements.value()->snapshot();
    REQUIRE(registeredScene.ok());
    CHECK_EQ(registeredScene.value().elements.size(), loaded.value().elements.size() + 1);

    auto withdrawalBuild           = gpuBuild;
    withdrawalBuild.sourceRevision = 94;
    const VegetationSceneElementHandle foreignHandle(7002, registration.value().handle.index(),
                                                     registration.value().handle.generation());
    auto                               foreignWithdrawal = liveElements.value()->withdrawElement(
        2, 93, foreignHandle, *gpuRuntime.value(), runtimeSurface, motion, masks.value(), withdrawalBuild);
    REQUIRE(!foreignWithdrawal.ok());
    CHECK_EQ(gpuRuntime.value()->sourceRevision(), std::uint64_t(93));
    auto withdrawal = liveElements.value()->withdrawElement(2, 93, registration.value().handle, *gpuRuntime.value(),
                                                            runtimeSurface, motion, masks.value(), withdrawalBuild);
    REQUIRE(withdrawal.ok());
    CHECK_EQ(withdrawal.value().registryRevision, std::uint64_t(3));
    CHECK_EQ(withdrawal.value().gpu.revision, std::uint64_t(94));
    auto withdrawnScene = liveElements.value()->snapshot();
    REQUIRE(withdrawnScene.ok());
    CHECK_EQ(withdrawnScene.value().elements.size(), loaded.value().elements.size());
    auto repeatedWithdrawal =
        liveElements.value()->withdrawElement(3, 94, registration.value().handle, *gpuRuntime.value(), runtimeSurface,
                                              motion, masks.value(), withdrawalBuild);
    REQUIRE(!repeatedWithdrawal.ok());
    auto reuseBuild           = gpuBuild;
    reuseBuild.sourceRevision = 95;
    auto reusedRegistration   = liveElements.value()->registerElement(3, 94, dynamicElement, *gpuRuntime.value(),
                                                                      runtimeSurface, motion, masks.value(), reuseBuild);
    REQUIRE(reusedRegistration.ok());
    CHECK_EQ(reusedRegistration.value().handle.index(), registration.value().handle.index());
    CHECK(reusedRegistration.value().handle.generation() != registration.value().handle.generation());
    auto finalWithdrawalBuild           = gpuBuild;
    finalWithdrawalBuild.sourceRevision = 96;
    auto staleGenerationWithdrawal =
        liveElements.value()->withdrawElement(4, 95, registration.value().handle, *gpuRuntime.value(), runtimeSurface,
                                              motion, masks.value(), finalWithdrawalBuild);
    REQUIRE(!staleGenerationWithdrawal.ok());
    auto finalWithdrawal =
        liveElements.value()->withdrawElement(4, 95, reusedRegistration.value().handle, *gpuRuntime.value(),
                                              runtimeSurface, motion, masks.value(), finalWithdrawalBuild);
    REQUIRE(finalWithdrawal.ok());
    CHECK_EQ(gpuRuntime.value()->sourceRevision(), std::uint64_t(96));
    REQUIRE(gpuRuntime.value()->release().ok());
    CHECK(gpuRuntime.value()->surface().vegetationMotion.texture == nullptr);

    auto instanceBuild           = gpuBuild;
    instanceBuild.sourceRevision = 101;
    auto instance = VegetationSceneInstance::create(reader, *gfx, sceneTemplateRef(), capabilities, runtimeSurface,
                                                    motion, instanceBuild, 8001);
    REQUIRE(instance.ok());
    CHECK_EQ(instance.value()->scene().sourceGuid, loaded.value().sourceGuid);
    CHECK(instance.value()->surface().vegetationMotion.texture != nullptr);
    auto instanceRegisterBuild           = gpuBuild;
    instanceRegisterBuild.sourceRevision = 102;
    auto instanceRegistration            = instance.value()->registerElement(1, dynamicElement, instanceRegisterBuild);
    REQUIRE(instanceRegistration.ok());
    auto instanceWithdrawBuild           = gpuBuild;
    instanceWithdrawBuild.sourceRevision = 103;
    auto instanceWithdrawal =
        instance.value()->withdrawElement(2, instanceRegistration.value().handle, instanceWithdrawBuild);
    REQUIRE(instanceWithdrawal.ok());
    REQUIRE(instance.value()->release().ok());
    REQUIRE(gfx->releaseTexture(noise));

    auto invalid = Value::fromJson(validScene);
    REQUIRE(invalid.ok());
    (*invalid.value().getIf<Value::Object>())["future"] = Value(true);
    auto text                                           = invalid.value().toJson();
    REQUIRE(text.ok());
    auto                 invalidPack = scenePack(text.value());
    EvpackResourceReader invalidReader(invalidPack);
    auto                 rejected = EvpackVegetationSceneLoader(invalidReader).load(sceneRef(), capabilities);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), DiagnosticCode::ParseError);

    auto fractional = Value::fromJson(validScene);
    REQUIRE(fractional.ok());
    auto &fractionalVolume = *fractional.value().getIf<Value::Object>()->at("volume").getIf<Value::Object>();
    fractionalVolume["colors"].getIf<Value::Array>()->at(1) = Value(1024.5);
    auto fractionalText                                     = fractional.value().toJson();
    REQUIRE(fractionalText.ok());
    auto                 fractionalPack = scenePack(fractionalText.value());
    EvpackResourceReader fractionalReader(fractionalPack);
    auto fractionalRejected = EvpackVegetationSceneLoader(fractionalReader).load(sceneRef(), capabilities);
    REQUIRE(!fractionalRejected.ok());
    CHECK_EQ(fractionalRejected.error()->code(), DiagnosticCode::InvalidArgument);

    EvpackResourceReader duplicateReader(scenePack(validScene, true));
    auto                 duplicateAssociation =
        VegetationSceneAssociationLoader(duplicateReader).load(sceneTemplateRef(), capabilities);
    REQUIRE(!duplicateAssociation.ok());
    CHECK_EQ(duplicateAssociation.error()->code(), DiagnosticCode::Conflict);
}
}  // namespace test_asset_graphics_vegetation_scene
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
namespace test_diffuse_light_probe {
using namespace eve;
using namespace eve::graphics;


TEST_CASE("graphics.lightProbe.customProvidedChangesRenderedPixels") {
    auto *gfx = Graphics::create();
    REQUIRE(gfx);
    gfx->initHeadless(96, 96);
    Canvas *canvas = gfx->newCanvas(96, 96);
    REQUIRE(canvas);
    const float    positions[] = {-0.8f, -0.7f, 0.f, 0.8f, -0.7f, 0.f, 0.f, 0.8f, 0.f};
    const float    normals[]   = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const uint32_t indices[]   = {0, 1, 2, 2, 1, 0};
    Mesh          *mesh        = gfx->newMeshFromArrays(positions, nullptr, normals, 3, indices, 6);
    REQUIRE(mesh);
    auto *camera = Camera3D::createCamera();
    camera->setEye(0.f, 0.f, 3.f);
    camera->setTarget(0.f, 0.f, 0.f);
    camera->setAmbient(0.f, 0.f, 0.f);
    auto *renderable = Renderable3D::create();
    renderable->setMeshLod(0, mesh);
    REQUIRE(renderable->setMeshLodRendererState(0, 0, 1, true, 1, true, 4, 0).ok());

    std::array<unsigned long long, 3> totals{};
    for (int pass = 0; pass < 2; ++pass) {
        REQUIRE(renderable->setCustomLightProbe(pass == 0 ? 1.f : 0.f, 0.f, pass == 0 ? 0.f : 1.f).ok());
        RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
        std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
        REQUIRE(pixels);
        const auto        *data = static_cast<const unsigned char *>(pixels->getData());
        unsigned long long red = 0, blue = 0;
        for (size_t i = 0; i < 96u * 96u * 4u; i += 4) {
            red += data[i];
            blue += data[i + 2];
        }
        if (pass == 0) {
            totals[0] = red;
            totals[1] = blue;
        } else {
            totals[2] = blue;
            CHECK(red < totals[0]);
        }
    }
    CHECK(totals[0] > totals[1] + 1000);
    CHECK(totals[2] > totals[1] + 1000);
    unsigned long long directionalRed[2]{};
    for (int pass = 0; pass < 2; ++pass) {
        REQUIRE(renderable->setCustomLightProbe(0.45f, 0.f, 0.f).ok());
        REQUIRE(renderable->setCustomLightProbeCoefficient(2, pass == 0 ? 0.35f : -0.35f, 0.f, 0.f).ok());
        RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
        std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
        REQUIRE(pixels);
        const auto *data = static_cast<const unsigned char *>(pixels->getData());
        for (size_t i = 0; i < 96u * 96u * 4u; i += 4) directionalRed[pass] += data[i];
    }
    CHECK(directionalRed[0] > directionalRed[1] + 1000);

    ProcgenProbeDesc leftProbe;
    leftProbe.sourcePointId = 10;
    leftProbe.type          = 1;
    leftProbe.x             = -0.55f;
    leftProbe.extentX = leftProbe.extentY = leftProbe.extentZ = 2.f;
    leftProbe.irradianceR                                     = 1.f;
    leftProbe.irradianceG = leftProbe.irradianceB = 0.f;
    ProcgenProbeDesc rightProbe                   = leftProbe;
    rightProbe.sourcePointId                      = 11;
    rightProbe.x                                  = 0.55f;
    rightProbe.irradianceR                        = 0.f;
    rightProbe.irradianceB                        = 1.f;
    DiffuseLightProbeRegistry::instance().replaceBatch("pixel-volume-test", {leftProbe, rightProbe});
    REQUIRE(renderable->setMeshLodRendererState(0, 0, 1, true, 1, true, 2, 0).ok());
    RenderSystem3D::renderToCanvas(*gfx, canvas, camera);
    std::unique_ptr<eve::image::ImageData> volumePixels(canvas->newImageData());
    REQUIRE(volumePixels);
    const auto *volumeData = static_cast<const unsigned char *>(volumePixels->getData());
    long long   halfDominance[2]{};
    for (int y = 0; y < 96; ++y)
        for (int x = 0; x < 96; ++x) {
            const size_t offset = static_cast<size_t>(y * 96 + x) * 4;
            halfDominance[x >= 48] += static_cast<long long>(volumeData[offset]) - volumeData[offset + 2];
        }
    CHECK(std::abs(halfDominance[0]) > 1000);
    CHECK(std::abs(halfDominance[1]) > 1000);
    CHECK(halfDominance[0] * halfDominance[1] < 0);
    DiffuseLightProbeRegistry::instance().removeBatch("pixel-volume-test");
    ecs::DestroyEntity(renderable);
    ecs::DestroyEntity(camera);
}
}  // namespace test_diffuse_light_probe
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_fluids)
#include "PrivateTestScope.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>
#include "fluids/FluidMath.h"
#include "fluids/FluidSdf.h"
#include "fluids/FluidSimulation.h"
#include "fluids/FluidSurfaceBinding.h"
#include "fluids/FluidSurfaceRenderer.h"
#include "fluids/Fluids.h"
#include "fluids/SurfaceDropletSimulation.h"
#include "fluids/SurfaceFluidRenderData.h"
#include "fluids/SurfaceFluidSceneRenderer.h"
#include "fluids/SurfaceWetnessField.h"
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidCodec.h"
#include "fluids/VolumeFluidDiffuse.h"
#include "graphics/Graphics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// fluids.cpp
namespace test_fluids {
// fluids.cpp
using namespace eve::fluids;

namespace {

[[maybe_unused]] bool tryInitHeadlessGfx() {
    auto *gfx = eve::graphics::Graphics::create();
    if (!gfx) return false;
    gfx->initHeadless(320, 240);
    return true;
}

/** @brief Subdivided icosahedron sphere mesh (positions + triangle indices). */
[[maybe_unused]] std::pair<std::vector<glm::vec3>, std::vector<uint32_t>> makeIcosphere(float radius,
                                                                                        int   subdivisions) {
    const float            t     = (1.f + std::sqrt(5.f)) / 2.f;
    std::vector<glm::vec3> verts = {
        glm::normalize(glm::vec3(-1.f, t, 0.f)),  glm::normalize(glm::vec3(1.f, t, 0.f)),
        glm::normalize(glm::vec3(-1.f, -t, 0.f)), glm::normalize(glm::vec3(1.f, -t, 0.f)),
        glm::normalize(glm::vec3(0.f, -1.f, t)),  glm::normalize(glm::vec3(0.f, 1.f, t)),
        glm::normalize(glm::vec3(0.f, -1.f, -t)), glm::normalize(glm::vec3(0.f, 1.f, -t)),
        glm::normalize(glm::vec3(t, 0.f, -1.f)),  glm::normalize(glm::vec3(t, 0.f, 1.f)),
        glm::normalize(glm::vec3(-t, 0.f, -1.f)), glm::normalize(glm::vec3(-t, 0.f, 1.f)),
    };
    std::vector<uint32_t> tris = {
        0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4,  11, 10, 2,  10, 7, 6, 7, 1, 8,
        3, 9,  4, 3, 4, 2, 3, 2, 6, 3, 6, 8,  3, 8,  9,  4, 9, 5, 2, 4,  11, 6,  2,  10, 8,  6, 7, 9, 8, 1,
    };
    for (int s = 0; s < subdivisions; ++s) {
        std::vector<uint32_t> nextTris;
        nextTris.reserve(tris.size() * 4);
        for (size_t i = 0; i < tris.size(); i += 3) {
            const uint32_t a = tris[i], b = tris[i + 1], c = tris[i + 2];
            const uint32_t ab = uint32_t(verts.size());
            const uint32_t bc = uint32_t(verts.size()) + 1;
            const uint32_t ca = uint32_t(verts.size()) + 2;
            verts.push_back(glm::normalize((verts[a] + verts[b]) * 0.5f));
            verts.push_back(glm::normalize((verts[b] + verts[c]) * 0.5f));
            verts.push_back(glm::normalize((verts[c] + verts[a]) * 0.5f));
            nextTris.insert(nextTris.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        tris.swap(nextTris);
    }
    for (glm::vec3 &v : verts) v *= radius;
    return {std::move(verts), std::move(tris)};
}

[[maybe_unused]] float distToCenter(const glm::vec3 &p) { return glm::length(p); }

}  // namespace


TEST_CASE("fluids.gpu.surfaceFlow") {
    if (!tryInitHeadlessGfx()) return;
    FluidParams params;
    params.gravity   = glm::vec3(0.f, -9.8f, 0.f);
    params.viscosity = 0.02f;
    FluidSimulator sim(2048, params, true);
    sim.setSdfSphere(0.f, 0.f, 0.f, 1.f, 32);
    sim.spawnDrop(glm::vec3(0.f, 1.5f, 0.f), 0.25f, 200);
    sim.step(1.f / 60.f);

    if (!sim.usingGpu()) return;  // no Vulkan device / no shader compiler — skip

    std::vector<float> dens;
    sim.readDensities(dens);
    REQUIRE(dens.size() == size_t(sim.getParticleCount()));
    for (float d : dens) CHECK(d > 0.f);

    float                  meanY0 = 0.f;
    std::vector<glm::vec3> pos;
    sim.readPositions(pos);
    for (const glm::vec3 &p : pos) meanY0 += p.y;
    meanY0 /= float(pos.size());

    for (int s = 0; s < 120; ++s) sim.step(1.f / 60.f);
    sim.readPositions(pos);

    float meanY1       = 0.f;
    bool  allOnSurface = true;
    float maxP         = 0.f;
    for (const glm::vec3 &p : pos) {
        meanY1 += p.y;
        maxP = std::max(maxP, distToCenter(p));
        if (distToCenter(p) < 1.f - 0.05f) allOnSurface = false;
    }
    meanY1 /= float(pos.size());
    CHECK(meanY1 < meanY0 - 0.05f);
    CHECK(allOnSurface);
    // Particles must stay glued to the SDF; a broken upload would let the
    // projection push them straight off the model (regression guard).
    CHECK(maxP < 3.f);
}

TEST_CASE("fluids.gpu.pbfCohesionClusterRemainsBounded") {
    if (!tryInitHeadlessGfx()) return;
    FluidParams params;
    params.gravity       = glm::vec3(0.f, -9.8f, 0.f);
    params.viscosity     = 0.05f;
    params.cohesion      = 0.5f;
    params.adhesion      = 0.5f;
    params.pbfIterations = 2;
    FluidSimulator sim(1024, params, true);
    sim.setSdfSphere(0.f, 0.f, 0.f, 1.f, 32);
    sim.spawnDrop(glm::vec3(0.f, 1.45f, 0.f), 0.22f, 128);
    sim.step(1.f / 60.f);
    if (!sim.usingGpu()) return;

    auto clusterRadius = [&]() {
        std::vector<glm::vec3> pos;
        sim.readPositions(pos);
        glm::vec3 c(0.f);
        for (const glm::vec3 &p : pos) c += p;
        c /= float(pos.size());
        double sum = 0.0;
        for (const glm::vec3 &p : pos) sum += glm::length(p - c);
        return float(sum / double(pos.size()));
    };

    const float before = clusterRadius();
    for (int s = 0; s < 60; ++s) sim.step(1.f / 60.f);
    const float after = clusterRadius();
    // Gravity, density relaxation and cohesion compete on the curved surface.
    // The GPU contract is bounded retention; CPU coverage above separately proves
    // that cohesion contracts a force-isolated cloud.
    CHECK(after < before * 1.25f);

    std::vector<glm::vec3> pos;
    sim.readPositions(pos);
    bool allOnSurface = true;
    for (const glm::vec3 &p : pos) {
        if (distToCenter(p) < 1.f - 0.06f) allOnSurface = false;
    }
    CHECK(allOnSurface);
}

TEST_CASE("fluids.ssf.gpu.pipeline") {
    if (!tryInitHeadlessGfx()) return;
    FluidSurfaceParams params;
    params.width            = 64;
    params.height           = 64;
    params.eye              = glm::vec3(0.f, 0.f, -3.f);
    params.particleRadius   = 0.08f;
    params.smoothIterations = 0;
    FluidSurfaceRenderer r(params, true);
    r.render(std::vector<glm::vec3>{glm::vec3(0.f)}, 0.08f);
    if (!r.usingGpu()) return;

    const int cx = 32;
    const int cy = 32;
    CHECK(std::fabs(r.depth()[size_t(cy * 64 + cx)] - 3.f) < 0.25f);
    CHECK(r.normals()[size_t(cy * 64 + cx)].z > 0.8f);
    CHECK(r.thickness()[size_t(cy * 64 + cx)] > 0.f);
    CHECK(r.color()[size_t(cy * 64 + cx) * 4u + 3] > 0u);

    r.writePpm("fluid_ssf_gpu.ppm");
    std::ifstream f("fluid_ssf_gpu.ppm", std::ios::binary | std::ios::ate);
    REQUIRE(f.good());
    const long size = long(f.tellg());
    f.close();
    CHECK(size > 100);
    std::remove("fluid_ssf_gpu.ppm");
}

// fluids_foam_surface.cpp
using namespace eve::fluids;


TEST_CASE("fluids.ssf.anisotropicProjectionIsOptionalAndGpuMatchesCpu") {
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    settings.spacing = .2f;
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position    = {0, 1, 0};
    particle.radii       = {.1f, .4f, .1f};
    particle.orientation = {0, 0, .70710677f, .70710677f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    FluidSurfaceParams params;
    params.width            = 97;
    params.height           = 97;
    params.eye              = {0, 1, 3};
    params.target           = {0, 1, 0};
    params.smoothIterations = 0;
    auto bounds             = [](const FluidSurfaceRenderer &renderer) {
        int x0 = 97, y0 = 97, x1 = -1, y1 = -1;
        for (int y = 0; y < 97; ++y)
            for (int x = 0; x < 97; ++x)
                if (renderer.color()[(size_t(y) * 97u + size_t(x)) * 4u + 3u]) {
                    x0 = std::min(x0, x);
                    x1 = std::max(x1, x);
                    y0 = std::min(y0, y);
                    y1 = std::max(y1, y);
                }
        return glm::ivec4(x0, y0, x1, y1);
    };
    FluidSurfaceRenderer spherical(params, false);
    spherical.renderVolume(*solver);
    const auto           sphericalBounds = bounds(spherical);
    FluidSurfaceRenderer cpu(params, false);
    REQUIRE(cpu.configureAnisotropy(true).ok());
    cpu.renderVolume(*solver);
    const auto cpuBounds = bounds(cpu);
    REQUIRE(cpuBounds.z - cpuBounds.x > sphericalBounds.z - sphericalBounds.x + 4);
    REQUIRE(cpuBounds.w - cpuBounds.y <= sphericalBounds.w - sphericalBounds.y - 4);

    FluidSurfaceRenderer gpu(params, true);
    REQUIRE(gpu.configureAnisotropy(true).ok());
    REQUIRE(gpu.prepare().ok());
    gpu.renderVolume(*solver);
    REQUIRE(gpu.usingGpu());
    const auto gpuBounds = bounds(gpu);
    REQUIRE(glm::all(glm::lessThanEqual(glm::abs(gpuBounds - cpuBounds), glm::ivec4(1))));
    const size_t center = 48u * 97u + 48u;
    REQUIRE(std::abs(gpu.depth()[center] - cpu.depth()[center]) < 1e-4f);
}


TEST_CASE("fluids.ssf.surfaceDownsampleRunsReducedCpuAndGpuTargets") {
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    FluidSurfaceParams params;
    params.width            = 96;
    params.height           = 96;
    params.eye              = {0, 0, 3};
    params.target           = {0, 0, 0};
    params.smoothIterations = 0;
    const std::vector<glm::vec3> particles{{0, 0, 0}};
    FluidSurfaceRenderer         cpu(params, false);
    REQUIRE(cpu.configureSurfaceDownsample(2).ok());
    REQUIRE(!cpu.configureSurfaceDownsample(5).ok());
    cpu.render(particles, .5f);
    REQUIRE(cpu.color().size() == 96u * 96u * 4u);
    REQUIRE(cpu.depth().size() == 96u * 96u);
    for (int y = 42; y < 54; y += 2)
        for (int x = 42; x < 54; x += 2) {
            const size_t a = size_t(y) * 96u + size_t(x);
            REQUIRE(cpu.depth()[a] == cpu.depth()[a + 1u]);
            REQUIRE(cpu.depth()[a] == cpu.depth()[a + 96u]);
            REQUIRE(std::memcmp(cpu.color().data() + a * 4u, cpu.color().data() + (a + 1u) * 4u, 4u) == 0);
        }
    FluidSurfaceRenderer gpu(params, true);
    REQUIRE(gpu.configureSurfaceDownsample(2).ok());
    gpu.render(particles, .5f);
    REQUIRE(gpu.usingGpu());
    const size_t center = 48u * 96u + 48u;
    REQUIRE(std::abs(gpu.depth()[center] - cpu.depth()[center]) < 2e-5f);
    REQUIRE(glm::length(gpu.normals()[center] - cpu.normals()[center]) < .002f);
}

TEST_CASE("fluids.ssf.thicknessDownsampleIsIndependentBeforeVolumeShading") {
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    settings.spacing = .4f;
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = {0, 1, 0};
    particle.color    = {.2f, .6f, .9f, 1.f};
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    FluidSurfaceParams params;
    params.width = params.height = 96;
    params.eye                   = {0, 1, 3};
    params.target                = {0, 1, 0};
    params.smoothIterations      = 0;
    FluidSurfaceRenderer cpu(params, false);
    REQUIRE(cpu.configureSurfaceDownsample(2).ok());
    REQUIRE(cpu.configureThicknessDownsample(4).ok());
    REQUIRE(!cpu.configureThicknessDownsample(0).ok());
    cpu.renderVolume(*solver);
    const size_t center = 48u * 96u + 48u;
    REQUIRE(cpu.color()[center * 4u + 3u] > 0u);
    for (int oy = 0; oy < 4; ++oy)
        for (int ox = 0; ox < 4; ++ox)
            REQUIRE(cpu.thickness()[center] == cpu.thickness()[size_t(48 + oy) * 96u + size_t(48 + ox)]);
    REQUIRE(cpu.depth()[center] == cpu.depth()[center + 1u]);

    FluidSurfaceRenderer gpu(params, true);
    REQUIRE(gpu.configureSurfaceDownsample(2).ok());
    REQUIRE(gpu.configureThicknessDownsample(4).ok());
    REQUIRE(gpu.prepare().ok());
    gpu.renderVolume(*solver);
    REQUIRE(gpu.usingGpu());
    REQUIRE(std::abs(gpu.depth()[center] - cpu.depth()[center]) < 2e-5f);
    REQUIRE(std::abs(gpu.thickness()[center] - cpu.thickness()[center]) < 1.f / 256.f + .000001f);
    REQUIRE(std::abs(int(gpu.color()[center * 4u + 3u]) - int(cpu.color()[center * 4u + 3u])) <= 1);
}

// fluids_ssf.cpp
using namespace eve::fluids;

TEST_CASE("fluids.ssf.uniformVolumeColorOnlyGpuPath") {
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = glm::vec3(0.f, 1.f, 0.f);
    particle.color    = glm::vec4(.8f, .1f, .05f, .5f);
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());
    FluidSurfaceParams params;
    params.width            = 64;
    params.height           = 64;
    params.eye              = glm::vec3(0.f, 1.f, 3.f);
    params.target           = glm::vec3(0.f, 1.f, 0.f);
    params.smoothIterations = 0;
    FluidSurfaceRenderer renderer(params, true);
    const auto           depthBefore = renderer.depth();
    renderer.renderVolumeColorOnly(*solver);
    REQUIRE(renderer.usingGpu());
    const size_t middle = (32u * 64u + 32u) * 4u;
    REQUIRE(renderer.color()[middle + 3u] > 0);
    REQUIRE(renderer.color()[middle] > renderer.color()[middle + 1u]);
    REQUIRE(renderer.depth() == depthBefore);
    const auto         colorBefore = renderer.color();
    std::vector<float> sceneDepth(64u * 64u, 1e30f);
    REQUIRE(!renderer.occludeWithSceneDepth(sceneDepth).ok());
    REQUIRE(renderer.color() == colorBefore);
}

TEST_CASE("fluids.ssf.customMaterialUniformColorOnlyRemainsGpuFastPath") {
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particle;
    particle.position = glm::vec3(0.f, 1.f, 0.f);
    particle.color    = glm::vec4(.65f, .2f, .08f, .75f);
    REQUIRE(solver->emit(std::span(&particle, 1)).ok());

    FluidSurfaceParams params;
    params.width = params.height = 64;
    params.eye                   = glm::vec3(0.f, 1.f, 3.f);
    params.target                = glm::vec3(0.f, 1.f, 0.f);
    params.smoothIterations      = 0;
    FluidSurfaceRenderer cpu(params, false), gpu(params, true);
    for (auto *renderer : {&cpu, &gpu}) {
        REQUIRE(renderer->configureSurface(1.f, .2f, .08f, 0).ok());
        REQUIRE(renderer->configureMaterial(true, .35f, .4f, 1.25f, .3f, .7f).ok());
        REQUIRE(renderer->configureColors(glm::vec3(.1f, .2f, .3f), glm::vec3(.2f, .7f, .9f)).ok());
    }

    cpu.renderVolume(*solver);
    const auto depthBefore = gpu.depth();
    gpu.renderVolumeColorOnly(*solver);
    REQUIRE(gpu.usingGpu());
    REQUIRE(gpu.depth() == depthBefore);
    const size_t middle = (32u * 64u + 32u) * 4u;
    for (size_t channel = 0; channel < 4; ++channel)
        CHECK(std::abs(int(cpu.color()[middle + channel]) - int(gpu.color()[middle + channel])) <= 3);
}

TEST_CASE("fluids.ssf.multicolorVolumeColorOnlyRemainsDeviceLocal") {
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    VolumeFluidSettings settings;
    settings.gravity = glm::vec3(0.f);
    auto created     = VolumeFluid::create(settings);
    REQUIRE(created.ok());
    auto                solver = std::move(created).takeValue();
    VolumeFluidParticle particles[2];
    particles[0].position    = glm::vec3(-.025f, 1.f, 0.f);
    particles[0].color       = glm::vec4(.9f, .1f, .05f, .8f);
    particles[0].radii       = glm::vec3(.1f, .25f, .1f);
    particles[0].orientation = glm::vec4(0.f, 0.f, .70710677f, .70710677f);
    particles[1].position    = glm::vec3(.025f, 1.f, 0.f);
    particles[1].color       = glm::vec4(.05f, .2f, .9f, .6f);
    particles[1].radii       = glm::vec3(.1f, .25f, .1f);
    particles[1].orientation = particles[0].orientation;
    REQUIRE(solver->emit(particles).ok());

    FluidSurfaceParams params;
    params.width = params.height = 64;
    params.eye                   = glm::vec3(0.f, 1.f, 3.f);
    params.target                = glm::vec3(0.f, 1.f, 0.f);
    params.smoothIterations      = 0;
    FluidSurfaceRenderer cpu(params, false), gpu(params, true);
    REQUIRE(cpu.configureAnisotropy(true).ok());
    REQUIRE(gpu.configureAnisotropy(true).ok());
    cpu.renderVolume(*solver);
    const auto depthBefore = gpu.depth();
    gpu.renderVolumeColorOnly(*solver);
    REQUIRE(gpu.usingGpu());
    REQUIRE(gpu.depth() == depthBefore);
    const size_t middle = (32u * 64u + 32u) * 4u;
    REQUIRE(gpu.color()[middle + 3u] > 0);
    for (size_t channel = 0; channel < 4; ++channel)
        CHECK(std::abs(int(cpu.color()[middle + channel]) - int(gpu.color()[middle + channel])) <= 4);
}


TEST_CASE("fluids.ssf.sphericalDepthAndNormalParity") {
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    for (int passes : {0, 1, 2}) {
        FluidSurfaceParams params;
        params.width            = 64;
        params.height           = 64;
        params.eye              = {0, 0, 3};
        params.target           = {0, 0, 0};
        params.smoothIterations = passes;
        FluidSurfaceRenderer         cpu(params, false), gpu(params, true);
        const std::vector<glm::vec3> particles{{0, 0, 0}};
        cpu.render(particles, .6f);
        gpu.render(particles, .6f);
        REQUIRE(gpu.usingGpu());
        const size_t middle = 32 * 64 + 32, left = 32 * 64 + 26, right = 32 * 64 + 38;
        const size_t top = 26 * 64 + 32, bottom = 38 * 64 + 32;
        REQUIRE(cpu.depth()[middle] < 2.42f);
        REQUIRE(cpu.depth()[right] > cpu.depth()[middle] + .06f);
        REQUIRE(gpu.normals()[left].x < -.2f);
        REQUIRE(gpu.normals()[right].x > .2f);
        REQUIRE(gpu.normals()[top].y > .2f);
        REQUIRE(gpu.normals()[bottom].y < -.2f);
        REQUIRE(gpu.normals()[middle].z > .95f);
        for (size_t i = 0; i < cpu.depth().size(); ++i) {
            if (cpu.depth()[i] >= 1e29f) {
                REQUIRE(gpu.depth()[i] >= 1e29f);
                continue;
            }
            REQUIRE(std::abs(cpu.depth()[i] - gpu.depth()[i]) < .00002f);
            REQUIRE(glm::length(cpu.normals()[i] - gpu.normals()[i]) < .002f);
            // GPU thickness uses 1/256-unit fixed-point accumulation.
            REQUIRE(std::abs(cpu.thickness()[i] - gpu.thickness()[i]) < 1.f / 256.f + .000001f);
        }
        gpu.render({}, .6f);
        REQUIRE(gpu.depth()[middle] >= 1e29f);
        REQUIRE(glm::length(gpu.normals()[middle]) == 0.f);
        REQUIRE(gpu.thickness()[middle] == 0.f);
        REQUIRE(gpu.color()[middle * 4 + 3] == 0);
    }
}

TEST_CASE("fluids.ssf.worldSpaceBlurRadiusUsesExistingBoundedPass") {
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    FluidSurfaceParams params;
    params.width = params.height = 64;
    params.eye                   = {0, 0, 3};
    params.target                = {0, 0, 0};
    params.smoothIterations      = 1;
    const std::vector<glm::vec3> particles{{-.18f, 0, 0}, {.18f, 0, -.12f}};
    FluidSurfaceRenderer         unblurred(params, false);
    REQUIRE(unblurred.configureSurfaceBlurRadius(0.f).ok());
    REQUIRE(!unblurred.configureSurfaceBlurRadius(.1001f).ok());
    unblurred.render(particles, .35f);
    auto noPassParams             = params;
    noPassParams.smoothIterations = 0;
    FluidSurfaceRenderer noPass(noPassParams, false);
    noPass.render(particles, .35f);
    REQUIRE(unblurred.depth() == noPass.depth());

    params.smoothIterations = 1;
    FluidSurfaceRenderer cpu(params, false), gpu(params, true);
    REQUIRE(cpu.configureSurfaceBlurRadius(.02f).ok());
    REQUIRE(gpu.configureSurfaceBlurRadius(.02f).ok());
    cpu.render(particles, .35f);
    gpu.render(particles, .35f);
    REQUIRE(gpu.usingGpu());
    for (size_t i = 0; i < cpu.depth().size(); ++i) {
        if (cpu.depth()[i] >= 1e29f || gpu.depth()[i] >= 1e29f) continue;
        REQUIRE(std::abs(cpu.depth()[i] - gpu.depth()[i]) < 1e-4f);
    }
}


TEST_CASE("fluids.ssf.orthographicDepthIndependentCoverageAndGpuParity") {
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    FluidSurfaceParams params;
    params.width            = 64;
    params.height           = 64;
    params.eye              = {0, 0, 3};
    params.target           = {0, 0, 0};
    params.smoothIterations = 0;
    params.orthographic     = true;
    params.orthographicSize = 1.f;
    FluidSurfaceRenderer nearCpu(params, false), farCpu(params, false), gpu(params, true);
    nearCpu.render(std::vector<glm::vec3>{{0, 0, 0}}, .25f);
    farCpu.render(std::vector<glm::vec3>{{0, 0, -1}}, .25f);
    gpu.render(std::vector<glm::vec3>{{0, 0, 0}}, .25f);
    REQUIRE(gpu.usingGpu());

    size_t nearCoverage = 0, farCoverage = 0;
    for (size_t i = 0; i < nearCpu.depth().size(); ++i) {
        nearCoverage += nearCpu.depth()[i] < 1e29f;
        farCoverage += farCpu.depth()[i] < 1e29f;
        if (nearCpu.depth()[i] >= 1e29f) {
            REQUIRE(gpu.depth()[i] >= 1e29f);
            continue;
        }
        REQUIRE(std::abs(nearCpu.depth()[i] - gpu.depth()[i]) < .00002f);
        REQUIRE(glm::length(nearCpu.normals()[i] - gpu.normals()[i]) < .002f);
    }
    CHECK(nearCoverage == farCoverage);
    CHECK(nearCoverage > 150);

    const auto previous = nearCpu.depth();
    REQUIRE(!nearCpu.configureProjection(true, 0.f).ok());
    nearCpu.render(std::vector<glm::vec3>{{0, 0, 0}}, .25f);
    CHECK(nearCpu.depth() == previous);
}


TEST_CASE("fluids.ssf.occlusionDoesNotTiltForegroundNormals") {
    auto *graphics = eve::graphics::Graphics::create();
    REQUIRE(graphics != nullptr);
    graphics->initHeadless(64, 64);
    FluidSurfaceParams params;
    params.width            = 64;
    params.height           = 64;
    params.eye              = {0, 0, 3};
    params.target           = {0, 0, 0};
    params.smoothIterations = 0;
    FluidSurfaceRenderer foreground(params, false), combined(params, true), reference(params, false);
    foreground.render(std::vector<glm::vec3>{{0, 0, 0}}, .6f);
    const std::vector<glm::vec3> particles{{0, 0, 0}, {.9f, 0, -2.f}};
    combined.render(particles, .6f);
    reference.render(particles, .6f);
    REQUIRE(combined.usingGpu());
    unsigned occludedEdges = 0;
    for (size_t i = 0; i < foreground.depth().size(); ++i) {
        if (foreground.depth()[i] >= 1e29f) continue;
        REQUIRE(std::abs(combined.depth()[i] - foreground.depth()[i]) < .00002f);
        REQUIRE(glm::length(combined.normals()[i] - reference.normals()[i]) < .002f);
        const size_t x = i % 64;
        if (x < 63 && foreground.depth()[i + 1] >= 1e29f && combined.depth()[i + 1] < 1e29f) {
            ++occludedEdges;
            REQUIRE(glm::length(combined.normals()[i] - foreground.normals()[i]) < .002f);
        }
    }
    REQUIRE(occludedEdges >= 8);
}

// fluids_surface.cpp
using namespace eve::fluids;
}  // namespace test_fluids
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_gpgpu)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#include "PrivateTestScope.h"
#include "ScriptTest.h"
#include "common/ECS.h"
#include "common/Exception.h"
#include "common/ScriptComponentStore.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/EcsGpu.h"
#include "gpgpu/EcsScriptPack.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"
#include "gpgpu/ShaderSystem.h"
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
#include "procgen/PointCompute.h"
#include "procgen/PointGraph.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// gpgpu.cpp
namespace test_gpgpu {
using namespace eve;


using namespace eve::gpgpu;

namespace {

/** Compute tests only need a live Vulkan device; headless init is enough. */
bool tryInitHeadlessGfx() {
    auto *gfx = eve::graphics::Graphics::create();
    if (!gfx) return false;
    gfx->initHeadless(320, 240);
    return true;
}

void setPointComputeFailure(const char *stage) {
#ifdef _WIN32
    _putenv_s("EVENGINE_POINT_COMPUTE_FAIL", stage ? stage : "");
#else
    if (stage)
        setenv("EVENGINE_POINT_COMPUTE_FAIL", stage, 1);
    else
        unsetenv("EVENGINE_POINT_COMPUTE_FAIL");
#endif
}

double medianMilliseconds(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    const size_t middle = samples.size() / 2;
    return samples.size() % 2 ? samples[middle] : (samples[middle - 1] + samples[middle]) * 0.5;
}

const char *kScaleKernel = R"(#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Data {
    float values[];
} data;
layout(push_constant) uniform PC {
    float data[32];
} pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    data.values[i] *= pc.data[0];
}
)";

const char *kScaleKernelWgsl = R"(
struct Data { values: array<f32> };
struct Push { data: array<vec4f, 8> };
@group(0) @binding(0) var<storage, read_write> values: Data;
@group(0) @binding(8) var<uniform> push: Push;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    values.values[gid.x] *= push.data[0].x;
}
)";

const char *kMoveKernel = R"(#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Pos {
    float data[];
} pos;
layout(set = 0, binding = 1) buffer Vel {
    float data[];
} vel;
layout(push_constant) uniform PC {
    float data[32];
} pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint n = uint(pc.data[1]);
    if (i >= n) return;
    float dt = pc.data[0];
    uint b = i * 2u;
    pos.data[b + 0u] += vel.data[b + 0u] * dt;
    pos.data[b + 1u] += vel.data[b + 1u] * dt;
}
)";

const char *kMoveKernelWgsl = R"(
struct Data { values: array<f32> };
struct Push { data: array<vec4f, 8> };
@group(0) @binding(0) var<storage, read_write> pos: Data;
@group(0) @binding(1) var<storage, read_write> vel: Data;
@group(0) @binding(8) var<uniform> push: Push;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= u32(push.data[0].y)) { return; }
    let base = gid.x * 2u;
    pos.values[base] += vel.values[base] * push.data[0].x;
    pos.values[base + 1u] += vel.values[base + 1u] * push.data[0].x;
}
)";

const char *kDoubleKernel = R"(#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer In {
    float a[];
} inBuf;
layout(set = 0, binding = 1) buffer Out {
    float b[];
} outBuf;
void main() {
    uint i = gl_GlobalInvocationID.x;
    outBuf.b[i] = inBuf.a[i] * 2.0;
}
)";

const char *kDoubleKernelWgsl = R"(
struct Data { values: array<f32> };
@group(0) @binding(0) var<storage, read_write> input: Data;
@group(0) @binding(1) var<storage, read_write> output: Data;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    output.values[gid.x] = input.values[gid.x] * 2.0;
}
)";

const char *backendKernel(const char *glsl, const char *wgsl) {
    auto *gfx = eve::graphics::Graphics::create();
    return gfx && gfx->getBackendName() == "webgpu" ? wgsl : glsl;
}

class GpuNode : public ecs::Entity {
public:
    ENTITY(GpuNode, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }

    struct Position {
        float x = 0;
        float y = 0;
    };
    COMPONENT(Position, position)

    struct Velocity {
        float dx = 0;
        float dy = 0;
    };
    COMPONENT(Velocity, velocity)
};

}  // namespace

TEST_CASE("gpgpu.module.create") {
    auto *mod = Gpgpu::create();
    REQUIRE(mod != nullptr);
    CHECK_EQ(mod->getName(), std::string("Gpgpu"));
}

TEST_CASE("gpgpu.graphics.backendName") {
    if (!tryInitHeadlessGfx()) return;
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE(gfx != nullptr);
    const std::string backend          = gfx->getBackendName();
    const bool        supportedBackend = backend == "vulkan" || backend == "webgpu";
    CHECK(supportedBackend);
}

TEST_CASE("gpgpu.newShaderFromSpvFile.delegatesWhenMissing") {
    if (!tryInitHeadlessGfx()) return;
    auto *mod = Gpgpu::create();
    if (!mod->isAvailable()) return;
    bool threw = false;
    try {
        mod->newShaderFromSpvFile("__eve_missing_compute.spv");
    } catch (...) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("gpgpu.dispatch.scaleFloats") {
    if (!tryInitHeadlessGfx()) return;
    auto *mod = Gpgpu::create();
    REQUIRE(mod->isAvailable());

    const int  count = 128;
    GpuBuffer *buf   = mod->newBuffer(count * int(sizeof(float)), "storage");
    REQUIRE(buf != nullptr);
    CHECK_EQ(buf->getSize(), count * int(sizeof(float)));

    for (int i = 0; i < count; ++i) buf->writeFloat32(i, float(i + 1));

    ComputeShader *shader = nullptr;
    try {
        shader = mod->newShader(backendKernel(kScaleKernel, kScaleKernelWgsl));
    } catch (...) {
        // glslc may be missing in some CI images — skip GPU path.
        return;
    }
    REQUIRE(shader != nullptr);
    shader->bindBuffer(0, buf);
    shader->setFloat(0, 2.f);

    const int groups = (count + 63) / 64;
    mod->dispatch(shader, groups, 1, 1);

    CHECK(std::fabs(buf->readFloat32(0) - 2.f) < 1e-4f);
    CHECK(std::fabs(buf->readFloat32(1) - 4.f) < 1e-4f);
    CHECK(std::fabs(buf->readFloat32(count - 1) - float(count) * 2.f) < 1e-3f);

    delete shader;
    delete buf;
}

TEST_CASE("gpgpu.meshDeformationProvider.runsRealCompute") {
    if (!tryInitHeadlessGfx()) return;
    auto *mod = Gpgpu::create();
    REQUIRE(mod->isAvailable());
    eve::MeshDeformationComputeRequest request;
    request.operation = eve::MeshDeformationComputeOperation::Inflate;
    request.positions = {0.f, 0.f, 0.f, 2.f, 0.f, 0.f};
    request.normals   = {0.f, 1.f, 0.f, 0.f, 1.f, 0.f};
    request.centerX   = 0.f;
    request.centerY   = 0.f;
    request.centerZ   = 0.f;
    request.radius    = 1.f;
    request.strength  = 0.5f;
    request.falloff   = 1.f;
    auto output       = mod->deform(std::move(request));
    REQUIRE(output.ok());
    REQUIRE_EQ(output.value().size(), std::size_t{6});
    CHECK(std::abs(output.value()[1] - 0.5f) < 1e-4f);
    CHECK(std::abs(output.value()[4]) < 1e-4f);
}

TEST_CASE("gpgpu.sequence.dispatchScale") {
    if (!tryInitHeadlessGfx()) return;
    auto *mod = Gpgpu::create();
    REQUIRE(mod->isAvailable());

    const int  count   = 128;
    GpuBuffer *in      = mod->newBuffer(count * int(sizeof(float)), "storage");
    GpuBuffer *out     = mod->newBuffer(count * int(sizeof(float)), "storage");
    GpuBuffer *staging = mod->newBuffer(count * int(sizeof(float)), "staging");
    REQUIRE(in != nullptr);
    REQUIRE(out != nullptr);
    REQUIRE(staging != nullptr);

    ComputeShader *shader = nullptr;
    try {
        shader = mod->newShader(backendKernel(kDoubleKernel, kDoubleKernelWgsl));
    } catch (...) {
        delete in;
        delete out;
        delete staging;
        return;  // no compiler available — skip GPU path
    }
    REQUIRE(shader != nullptr);

    std::vector<float> src;
    src.reserve(count);
    for (int i = 0; i < count; ++i) src.push_back(float(i + 1));

    Sequence *seq = mod->newSequence();
    REQUIRE(seq->isAvailable());
    shader->bindBuffer(0, in);
    shader->bindBuffer(1, out);

    const int groups = (count + 63) / 64;
    seq->begin();
    seq->recordUpload(in, src.data(), uint64_t(count) * sizeof(float));
    seq->recordDispatch(shader, groups, 1, 1);
    seq->recordDownload(out, staging, uint64_t(count) * sizeof(float));
    CHECK(int(seq->submitAsync()) == int(SequenceStatus::Submitted));
    CHECK_EQ(seq->getStatusName(), std::string("submitted"));
    bool rejectedPendingReuse = false;
    try {
        seq->begin();
    } catch (const eve::Exception &) {
        rejectedPendingReuse = true;
    }
    CHECK(rejectedPendingReuse);

    // The same shader may be rebound and submitted by another Sequence while
    // the first is pending; descriptor retirement waits for both submissions.
    Sequence *overlap = mod->newSequence();
    REQUIRE(overlap->isAvailable());
    shader->bindBuffer(0, in);
    shader->bindBuffer(1, in);
    overlap->begin();
    overlap->recordDispatch(shader, groups, 1, 1);
    CHECK(int(overlap->submitAsync()) == int(SequenceStatus::Submitted));
    const SequenceStatus observed = seq->poll();
    const bool pendingOrComplete  = observed == SequenceStatus::Submitted || observed == SequenceStatus::Complete;
    CHECK(pendingOrComplete);
    CHECK(int(seq->wait()) == int(SequenceStatus::Complete));
    CHECK(int(overlap->wait()) == int(SequenceStatus::Complete));
    CHECK_EQ(seq->getStatusName(), std::string("complete"));

    std::vector<float> dst(size_t(count), 0.f);
    staging->downloadBytes(dst.data(), uint64_t(count) * sizeof(float));
    CHECK(std::fabs(dst[0] - 2.f) < 1e-4f);
    CHECK(std::fabs(dst[1] - 4.f) < 1e-4f);
    CHECK(std::fabs(dst[count - 1] - float(count) * 2.f) < 1e-3f);

    delete seq;
    delete overlap;
    delete shader;
    delete in;
    delete out;
    delete staging;
}

TEST_CASE("gpgpu.procgen.transformParity") {
    if (!tryInitHeadlessGfx()) return;
    constexpr int          count = 2048;
    eve::procgen::PointSet input;
    for (int index = 0; index < count; ++index) {
        const int point = input.add(float(index % 64), float(index % 7), float(index / 64));
        input.setNormal(point, 0.25f, 1.f, -0.5f);
        input.setYaw(point, float(index % 360));
        input.setScale(point, 0.5f, 1.5f, 2.f);
        input.setDensity(point, float(index % 10) * 0.1f);
        input.setPointSeed(point, uint32_t(index + 17));
        input.setStringAttribute(point, "asset", "oak");
    }
    const eve::procgen::PointSet expected =
        eve::procgen::transformPointSet(input, 12.f, -3.f, 8.f, 31.f, 1.25f, 0.75f, 2.f);
    eve::procgen::PointSet     actual;
    eve::procgen::PointCompute compute;
    REQUIRE(compute.transform(input, actual, 12.f, -3.f, 8.f, 31.f, 1.25f, 0.75f, 2.f));
    REQUIRE(actual.getCount() == expected.getCount());
    for (int index = 0; index < count; ++index) {
        CHECK(std::fabs(actual.getX(index) - expected.getX(index)) < 0.0001f);
        CHECK(std::fabs(actual.getY(index) - expected.getY(index)) < 0.0001f);
        CHECK(std::fabs(actual.getZ(index) - expected.getZ(index)) < 0.0001f);
        CHECK(std::fabs(actual.getNormalX(index) - expected.getNormalX(index)) < 0.0001f);
        CHECK(std::fabs(actual.getNormalY(index) - expected.getNormalY(index)) < 0.0001f);
        CHECK(std::fabs(actual.getNormalZ(index) - expected.getNormalZ(index)) < 0.0001f);
        CHECK(actual.getPointSeed(index) == input.getPointSeed(index));
        CHECK(actual.getStringAttribute(index, "asset", "") == "oak");
    }

    const eve::procgen::PointSet reusedExpected =
        eve::procgen::transformPointSet(input, -4.f, 6.f, 2.f, -17.f, 0.5f, 2.f, 1.25f);
    eve::procgen::PointSet reusedActual;
    REQUIRE(compute.transform(input, reusedActual, -4.f, 6.f, 2.f, -17.f, 0.5f, 2.f, 1.25f));
    REQUIRE(reusedActual.getCount() == reusedExpected.getCount());
    CHECK(std::fabs(reusedActual.getX(0) - reusedExpected.getX(0)) < 0.0001f);
    CHECK(std::fabs(reusedActual.getZ(count - 1) - reusedExpected.getZ(count - 1)) < 0.0001f);

    const std::vector<eve::procgen::PointCompute::Transform> chain{
        {2.f, 3.f, -1.f, 15.f, 1.2f, 0.8f, 1.1f},
        {-7.f, 0.5f, 4.f, -33.f, 0.75f, 2.f, 0.6f},
        {1.f, -2.f, 8.f, 91.f, 1.5f, 1.25f, 0.9f},
        {0.f, 6.f, -3.f, 5.f, 0.5f, 0.5f, 2.f},
    };
    eve::procgen::PointSet fusedExpected = input;
    for (const auto &operation : chain)
        fusedExpected =
            eve::procgen::transformPointSet(fusedExpected, operation.x, operation.y, operation.z, operation.yaw,
                                            operation.scaleX, operation.scaleY, operation.scaleZ);
    eve::procgen::PointSet fusedActual;
    REQUIRE(compute.transformChain(input, fusedActual, chain));
    REQUIRE(fusedActual.getCount() == fusedExpected.getCount());
    CHECK_EQ(compute.getUploadCount(), uint64_t(3));
    CHECK_EQ(compute.getDispatchCount(), uint64_t(3));
    CHECK_EQ(compute.getReadbackCount(), uint64_t(3));
    CHECK_EQ(compute.getBufferReuseCount(), uint64_t(2));
    CHECK(compute.getPeakBufferBytes() >= uint64_t(count * 11 * int(sizeof(float))));
    CHECK_EQ(compute.getLastFusedTransformCount(), 4);
    for (int index : {0, count / 2, count - 1}) {
        CHECK(std::fabs(fusedActual.getX(index) - fusedExpected.getX(index)) < 0.0002f);
        CHECK(std::fabs(fusedActual.getY(index) - fusedExpected.getY(index)) < 0.0002f);
        CHECK(std::fabs(fusedActual.getZ(index) - fusedExpected.getZ(index)) < 0.0002f);
        CHECK(std::fabs(fusedActual.getNormalX(index) - fusedExpected.getNormalX(index)) < 0.0002f);
        CHECK(std::fabs(fusedActual.getNormalY(index) - fusedExpected.getNormalY(index)) < 0.0002f);
        CHECK(std::fabs(fusedActual.getNormalZ(index) - fusedExpected.getNormalZ(index)) < 0.0002f);
    }
}

TEST_CASE("gpgpu.procgen.transformChainRejectsInvalidLength") {
    eve::procgen::PointSet input;
    input.add(1.f, 2.f, 3.f);
    eve::procgen::PointSet                                   output;
    eve::procgen::PointCompute                               compute;
    const std::vector<eve::procgen::PointCompute::Transform> empty;
    CHECK(!compute.transformChain(input, output, empty));
    CHECK(output.empty());
    CHECK_EQ(compute.getDispatchCount(), uint64_t(0));

    const std::vector<eve::procgen::PointCompute::Transform> oversized(5);
    CHECK(!compute.transformChain(input, output, oversized));
    CHECK(output.empty());
    CHECK_EQ(compute.getDispatchCount(), uint64_t(0));
}

TEST_CASE("gpgpu.procgen.pointGraphCompilesLinearTransformSegment") {
    if (!tryInitHeadlessGfx()) return;
    constexpr int          count = 2048;
    eve::procgen::PointSet input;
    for (int index = 0; index < count; ++index) input.add(float(index), 2.f, -3.f);

    eve::procgen::PointGraph graph;
    REQUIRE(graph.addNode("source", "input"));
    REQUIRE(graph.setNodePoints("source", &input));
    std::string previous = "source";
    for (int index = 0; index < 4; ++index) {
        const std::string id = "move-" + std::to_string(index);
        REQUIRE(graph.addNode(id, "transform"));
        REQUIRE(graph.connect(previous, id));
        REQUIRE(graph.setNodeFloat(id, "x", float(index + 1)));
        previous = id;
    }
    REQUIRE(graph.setComputePolicy("gpu"));
    std::unique_ptr<eve::procgen::PointSet> output(graph.execute(previous));
    REQUIRE(bool(output));
    CHECK_EQ(graph.getComputeUploadCount(), uint64_t(1));
    CHECK_EQ(graph.getComputeDispatchCount(), uint64_t(1));
    CHECK_EQ(graph.getComputeReadbackCount(), uint64_t(1));
    CHECK_EQ(graph.getLastFusedTransformCount(), 4);
    CHECK(std::fabs(output->getX(0) - 10.f) < 0.0001f);

    std::unique_ptr<eve::procgen::PointSet> preview(graph.getNodeOutput("move-1"));
    REQUIRE(bool(preview));
    CHECK(std::fabs(preview->getX(0) - 3.f) < 0.0001f);
    CHECK_EQ(graph.getComputeReadbackCount(), uint64_t(1));

    std::unique_ptr<eve::procgen::PointSet> cached(graph.execute(previous));
    REQUIRE(bool(cached));
    CHECK_EQ(graph.getComputeDispatchCount(), uint64_t(1));
    CHECK(graph.getCacheHitCount() > 0);
}

TEST_CASE("gpgpu.procgen.pointGraphFallsBackAtomicallyAfterForcedGpuFailures") {
    if (!tryInitHeadlessGfx()) return;
    for (const char *stage : {"compile", "allocation", "submit", "readback"}) {
        eve::procgen::PointSet input;
        for (int index = 0; index < 2048; ++index) input.add(float(index), 0.f, 0.f);
        eve::procgen::PointGraph graph;
        REQUIRE(graph.addNode("source", "input"));
        REQUIRE(graph.addNode("first", "transform"));
        REQUIRE(graph.addNode("second", "transform"));
        REQUIRE(graph.connect("source", "first"));
        REQUIRE(graph.connect("first", "second"));
        REQUIRE(graph.setNodePoints("source", &input));
        REQUIRE(graph.setNodeFloat("first", "x", 2.f));
        REQUIRE(graph.setNodeFloat("second", "x", 3.f));
        REQUIRE(graph.setComputePolicy("gpu"));

        setPointComputeFailure(stage);
        std::unique_ptr<eve::procgen::PointSet> output(graph.execute("second"));
        setPointComputeFailure(nullptr);
        REQUIRE(bool(output));
        CHECK_EQ(output->getX(0), 5.f);
        CHECK(graph.getComputeFallbackReason().find("forced") != std::string::npos);
        CHECK_EQ(graph.getComputeUploadCount(), uint64_t(0));
        CHECK_EQ(graph.getComputeDispatchCount(), uint64_t(0));
        CHECK_EQ(graph.getComputeReadbackCount(), uint64_t(0));
        std::unique_ptr<eve::procgen::PointSet> intermediate(graph.getNodeOutput("first"));
        REQUIRE(bool(intermediate));
        CHECK_EQ(intermediate->getX(0), 2.f);
    }
}

TEST_CASE("gpgpu.procgen.millionPointAcceptanceBenchmark") {
    if (!std::getenv("EVENGINE_PCG_MILLION_BENCHMARK") || !tryInitHeadlessGfx()) return;
    constexpr int                                            count = 1000000;
    const std::vector<eve::procgen::PointCompute::Transform> chain{
        {2.f, 3.f, -1.f, 15.f, 1.2f, 0.8f, 1.1f},
        {-7.f, 0.5f, 4.f, -33.f, 0.75f, 2.f, 0.6f},
        {1.f, -2.f, 8.f, 91.f, 1.5f, 1.25f, 0.9f},
        {0.f, 6.f, -3.f, 5.f, 0.5f, 0.5f, 2.f},
    };
    eve::procgen::PointSet input;
    for (int index = 0; index < count; ++index) input.add(float(index % 1000), float(index % 31), float(index / 1000));

    const auto measure = [](auto &&operation) {
        const auto started = std::chrono::steady_clock::now();
        operation();
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    };
    auto cpuRun = [&] {
        eve::procgen::PointSet output = input;
        for (const auto &transform : chain)
            output = eve::procgen::transformPointSet(output, transform.x, transform.y, transform.z, transform.yaw,
                                                     transform.scaleX, transform.scaleY, transform.scaleZ);
        REQUIRE(output.getCount() == count);
    };
    eve::procgen::PointGraph fusedGraph;
    REQUIRE(fusedGraph.addNode("source", "input"));
    REQUIRE(fusedGraph.setNodePoints("source", &input));
    std::string previous = "source";
    for (int index = 0; index < 4; ++index) {
        const std::string id        = "move-" + std::to_string(index);
        const auto       &transform = chain[size_t(index)];
        REQUIRE(fusedGraph.addNode(id, "transform"));
        REQUIRE(fusedGraph.connect(previous, id));
        REQUIRE(fusedGraph.setNodeFloat(id, "x", transform.x));
        REQUIRE(fusedGraph.setNodeFloat(id, "y", transform.y));
        REQUIRE(fusedGraph.setNodeFloat(id, "z", transform.z));
        REQUIRE(fusedGraph.setNodeFloat(id, "yaw", transform.yaw));
        REQUIRE(fusedGraph.setNodeFloat(id, "scaleX", transform.scaleX));
        REQUIRE(fusedGraph.setNodeFloat(id, "scaleY", transform.scaleY));
        REQUIRE(fusedGraph.setNodeFloat(id, "scaleZ", transform.scaleZ));
        previous = id;
    }
    REQUIRE(fusedGraph.setComputePolicy("gpu"));
    auto fusedRun = [&] {
        fusedGraph.clearCache();
        std::unique_ptr<eve::procgen::PointSet> output(fusedGraph.execute(previous));
        REQUIRE(bool(output));
        REQUIRE(output->getCount() == count);
    };
    eve::procgen::PointCompute isolatedCompute;
    auto                       isolatedRun = [&] {
        eve::procgen::PointSet current = input;
        for (const auto &transform : chain) {
            eve::procgen::PointSet output;
            REQUIRE(isolatedCompute.transform(current, output, transform.x, transform.y, transform.z, transform.yaw,
                                                                    transform.scaleX, transform.scaleY, transform.scaleZ));
            current = std::move(output);
        }
    };

    for (int warmup = 0; warmup < 5; ++warmup) {
        fusedRun();
        isolatedRun();
    }
    cpuRun();
    std::vector<double> cpu, fused, isolated;
    for (int iteration = 0; iteration < 20; ++iteration) {
        cpu.push_back(measure(cpuRun));
        fused.push_back(measure(fusedRun));
        isolated.push_back(measure(isolatedRun));
    }
    const double cpuMedian      = medianMilliseconds(cpu);
    const double fusedMedian    = medianMilliseconds(fused);
    const double isolatedMedian = medianMilliseconds(isolated);
    CHECK(cpuMedian / fusedMedian >= 2.0);
    CHECK(isolatedMedian / fusedMedian >= 1.5);
    CHECK_EQ(fusedGraph.getComputeUploadCount(), uint64_t(25));
    CHECK_EQ(fusedGraph.getComputeDispatchCount(), uint64_t(25));
    CHECK_EQ(fusedGraph.getComputeReadbackCount(), uint64_t(25));
    CHECK_EQ(isolatedCompute.getUploadCount(), uint64_t(100));
    CHECK_EQ(isolatedCompute.getDispatchCount(), uint64_t(100));
    CHECK_EQ(isolatedCompute.getReadbackCount(), uint64_t(100));
    std::cout << "PCG_BENCHMARK_JSON={\"points\":" << count << ",\"operations\":4,\"samples\":20,"
              << "\"cpuMedianMs\":" << cpuMedian << ",\"fusedGpuMedianMs\":" << fusedMedian
              << ",\"isolatedGpuMedianMs\":" << isolatedMedian << ",\"cpuSpeedup\":" << cpuMedian / fusedMedian
              << ",\"roundTripSpeedup\":" << isolatedMedian / fusedMedian << "}\n";
    CHECK_EQ(fusedGraph.getLastFusedTransformCount(), 4);
}

TEST_CASE("gpgpu.sequence.singleSubmitChainedDispatches") {
    if (!tryInitHeadlessGfx()) return;
    auto *mod = Gpgpu::create();
    REQUIRE(mod->isAvailable());

    const int  count   = 256;
    GpuBuffer *a       = mod->newBuffer(count * int(sizeof(float)), "storage");
    GpuBuffer *b       = mod->newBuffer(count * int(sizeof(float)), "storage");
    GpuBuffer *c       = mod->newBuffer(count * int(sizeof(float)), "storage");
    GpuBuffer *staging = mod->newBuffer(count * int(sizeof(float)), "staging");

    ComputeShader *shader = nullptr;
    try {
        shader = mod->newShader(backendKernel(kDoubleKernel, kDoubleKernelWgsl));
    } catch (...) {
        delete a;
        delete b;
        delete c;
        delete staging;
        return;
    }

    std::vector<float> src(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) src[size_t(i)] = float(i % 7);

    Sequence *seq = mod->newSequence();
    REQUIRE(seq->isAvailable());
    const int groups = (count + 63) / 64;

    // a -> b -> c in one submission; c[i] must be src[i] * 4.
    seq->begin();
    seq->recordUpload(a, src.data(), uint64_t(count) * sizeof(float));
    shader->bindBuffer(0, a);
    shader->bindBuffer(1, b);
    seq->recordDispatch(shader, groups, 1, 1);
    shader->bindBuffer(0, b);
    shader->bindBuffer(1, c);
    seq->recordDispatch(shader, groups, 1, 1);
    seq->recordDownload(c, staging, uint64_t(count) * sizeof(float));
    seq->submit();

    std::vector<float> dst(size_t(count), 0.f);
    staging->downloadBytes(dst.data(), uint64_t(count) * sizeof(float));
    for (int i = 0; i < count; ++i) CHECK(std::fabs(dst[size_t(i)] - float(i % 7) * 4.f) < 1e-3f);

    // Reuse the same sequence for a second cycle.
    seq->begin();
    seq->recordUpload(a, src.data(), uint64_t(count) * sizeof(float));
    shader->bindBuffer(0, a);
    shader->bindBuffer(1, b);
    seq->recordDispatch(shader, groups, 1, 1);
    seq->recordDownload(b, staging, uint64_t(count) * sizeof(float));
    seq->submit();

    staging->downloadBytes(dst.data(), uint64_t(count) * sizeof(float));
    CHECK(std::fabs(dst[0] - 0.f) < 1e-4f);
    CHECK(std::fabs(dst[count - 1] - float((count - 1) % 7) * 2.f) < 1e-3f);

    delete seq;
    delete shader;
    delete a;
    delete b;
    delete c;
    delete staging;
}

TEST_CASE("gpgpu.shaderSystem.ecsMove") {
    if (!tryInitHeadlessGfx()) return;
    auto *mod = Gpgpu::create();
    REQUIRE(mod->isAvailable());

    ecs::Table       world;
    ecs::ScopedTable guard(world);

    auto *a           = GpuNode::create();
    a->position()->x  = 1.f;
    a->position()->y  = 2.f;
    a->velocity()->dx = 10.f;
    a->velocity()->dy = 20.f;

    auto *b           = GpuNode::create();
    b->position()->x  = 0.f;
    b->position()->y  = 0.f;
    b->velocity()->dx = -5.f;
    b->velocity()->dy = 1.f;

    ShaderSystem sys;
    sys.setGpgpu(mod);
    try {
        sys.setShaderSource(backendKernel(kMoveKernel, kMoveKernelWgsl));
    } catch (...) {
        return;
    }

    EcsGpuWorkspace<GpuNode::Position> posWorkspace;
    EcsGpuWorkspace<GpuNode::Velocity> velWorkspace;
    posWorkspace.reserveEntities(1024);
    const int nPos = packViewComponent<GpuNode, GpuNode::Position>(sys, 0, posWorkspace);
    const int nVel = packViewComponent<GpuNode, GpuNode::Velocity>(sys, 1, velWorkspace);
    REQUIRE_EQ(nPos, nVel);
    REQUIRE_GE(nPos, 2);

    sys.dispatch(nPos, 0.5f);

    unpackViewComponent<GpuNode, GpuNode::Position>(sys, 0, nPos, posWorkspace);
    CHECK_GE(posWorkspace.entityCapacity(), size_t(1024));

    CHECK_EQ(sys.getUploadCount(), uint64_t(2));
    CHECK_EQ(sys.getDispatchCount(), uint64_t(1));
    CHECK_EQ(sys.getDownloadCount(), uint64_t(1));

    CHECK(std::fabs(a->position()->x - 6.f) < 1e-3f);
    CHECK(std::fabs(a->position()->y - 12.f) < 1e-3f);
    CHECK(std::fabs(b->position()->x - (-2.5f)) < 1e-3f);
    CHECK(std::fabs(b->position()->y - 0.5f) < 1e-3f);

    // A downstream system can consume the resident buffers in the same GPU
    // command sequence without an intervening CPU download/upload.
    ShaderSystem consumer;
    consumer.setGpgpu(mod);
    consumer.setShaderSource(backendKernel(kMoveKernel, kMoveKernelWgsl));
    consumer.attachBuffer(0, sys.getBuffer(0));
    consumer.attachBuffer(1, sys.getBuffer(1));
    Sequence *sequence = mod->newSequence();
    REQUIRE(sequence->isAvailable());
    sequence->begin();
    consumer.recordDispatch(sequence, nPos, 0.25f);
    sequence->submit();
    const std::vector<float> resident = consumer.download(0, nPos * 2);
    CHECK(std::fabs(resident[0] - 8.5f) < 1e-3f);
    CHECK(std::fabs(resident[1] - 17.f) < 1e-3f);
    CHECK_EQ(consumer.getUploadCount(), uint64_t(0));
    CHECK_EQ(consumer.getDispatchCount(), uint64_t(1));
    CHECK_EQ(consumer.getDownloadCount(), uint64_t(1));
    delete sequence;

    // Patch only the first typed component record; the second resident record
    // remains untouched and the same staging allocation is reused.
    const int patchedCount = packViewComponentRange<GpuNode, GpuNode::Position>(sys, 0, 0, 1, posWorkspace);
    CHECK_EQ(patchedCount, 1);
    float patched[2] = {};
    sys.downloadRange(0, patched, 2, 0);
    CHECK(std::fabs(patched[0] - 6.f) < 1e-3f);
    CHECK(std::fabs(patched[1] - 12.f) < 1e-3f);

    sys.resetStatistics();
    CHECK_EQ(sys.getUploadCount(), uint64_t(0));
    CHECK_EQ(sys.getDispatchCount(), uint64_t(0));
    CHECK_EQ(sys.getDownloadCount(), uint64_t(0));
}

TEST_CASE("gpgpu.shaderSystem.millionResidentBenchmark") {
    if (!std::getenv("EVENGINE_ECS_MILLION_BENCHMARK") || !tryInitHeadlessGfx()) return;
    constexpr int count   = 1000000;
    constexpr int samples = 10;
    auto         *mod     = Gpgpu::create();
    REQUIRE(mod->isAvailable());

    ShaderSystem sys;
    sys.setGpgpu(mod);
    sys.setShaderSource(backendKernel(kMoveKernel, kMoveKernelWgsl));
    std::vector<float> positions(size_t(count) * 2, 0.f);
    std::vector<float> velocities(size_t(count) * 2, 1.f);
    sys.upload(0, positions);
    sys.upload(1, velocities);

    Sequence *sequence = mod->newSequence();
    REQUIRE(sequence->isAvailable());
    auto runResidentTick = [&] {
        sequence->begin();
        sys.recordDispatch(sequence, count, 0.016f);
        sequence->submit();
    };
    for (int warmup = 0; warmup < 3; ++warmup) runResidentTick();

    std::vector<double> timings;
    timings.reserve(samples);
    for (int sample = 0; sample < samples; ++sample) {
        const auto started = std::chrono::steady_clock::now();
        runResidentTick();
        timings.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    }
    std::sort(timings.begin(), timings.end());
    const double             p50    = timings[size_t(samples / 2)];
    const double             p95    = timings[size_t(samples - 1)];
    const std::vector<float> result = sys.download(0, 2);
    CHECK(std::fabs(result[0] - 13.f * 0.016f) < 1e-3f);
    CHECK_EQ(sys.getUploadCount(), uint64_t(2));
    CHECK_EQ(sys.getDownloadCount(), uint64_t(1));
    CHECK_EQ(sys.getDispatchCount(), uint64_t(13));
    std::cout << "ECS_GPU_BENCHMARK_JSON={\"entities\":" << count << ",\"residentTicks\":13,\"samples\":" << samples
              << ",\"p50Ms\":" << p50 << ",\"p95Ms\":" << p95
              << ",\"steadyUploadBytes\":0,\"steadyDownloadBytes\":0}\n";
    delete sequence;
}

TEST_CASE("gpgpu.buffer.bulkFloats") {
    if (!tryInitHeadlessGfx()) return;
    auto *mod = Gpgpu::create();
    REQUIRE(mod->isAvailable());

    std::vector<float> src = {1.f, 2.f, 3.f, 4.f};
    GpuBuffer         *buf = mod->newBuffer(int(src.size() * sizeof(float)), "storage");
    buf->writeFloat32s(src.data(), int(src.size()), 0);
    std::vector<float> dst(src.size(), 0.f);
    buf->readFloat32s(dst.data(), int(dst.size()), 0);
    CHECK_EQ(dst[0], 1.f);
    CHECK_EQ(dst[3], 4.f);
    delete buf;
}

namespace {

// CPU-only GpuBuffer so pack/unpackScriptEntityFloats can be exercised without
// a live Vulkan device.
class TestBuffer : public eve::gpgpu::GpuBuffer {
public:
    std::vector<float> floats;

    explicit TestBuffer(int bytes) : floats(size_t(bytes) / sizeof(float), 0.f) {}

    int                  getSize() const override { return int(floats.size()) * int(sizeof(float)); }
    std::string          getUsage() const override { return "test"; }
    void                 writeData(eve::data::ByteData *, int) override {}
    eve::data::ByteData *readData(int, int) override { return nullptr; }
    void                 writeFloat32(int i, float v) override { floats[size_t(i)] = v; }
    float                readFloat32(int i) override { return floats[size_t(i)]; }
    void                 fillFloat32(float v) override { std::fill(floats.begin(), floats.end(), v); }
    void                 writeFloat32s(const float *data, int count, int startIndex) override {
        for (int i = 0; i < count; ++i) floats[size_t(startIndex + i)] = data[i];
    }
    void readFloat32s(float *out, int count, int startIndex) const override {
        for (int i = 0; i < count; ++i) out[i] = floats[size_t(startIndex + i)];
    }
    void uploadBytes(const void *, uint64_t, uint64_t) override {}
    void downloadBytes(void *, uint64_t, uint64_t) const override {}
};

bool throwsException(const std::function<void()> &fn) {
    try {
        fn();
    } catch (const eve::Exception &) {
        return true;
    }
    return false;
}

float readArrayFloat(ssq::VM &vm, ssq::Object arr, int idx) {
    HSQUIRRELVM     h   = arr.getHandle();
    const SQInteger top = sq_gettop(h);
    sq_pushobject(h, arr.getRaw());
    sq_pushinteger(h, idx);
    SQFloat v = 0.f;
    if (SQ_SUCCEEDED(sq_get(h, -2))) sq_getfloat(h, -1, &v);
    sq_settop(h, top);
    return float(v);
}

}  // namespace

// ---------------------------------------------------------------------------
// 脚本面的批量回读（方案 §10.21 决定：给脚本面加"返回数组"的绑定）。
//
// 不用 GPU：把一个 CPU-only 的 `TestBuffer` 当 `GpuBuffer*` 交给脚本，走的就是绑定生成的
// 那条路（`readFloat32s(count, startIndex) -> array`），而裁剪/越界边界可以逐个钉死。
// ---------------------------------------------------------------------------



UnitSciptTest(GpuBufferBulkReadFixture, R"SQ(
// 模块绑定是**惰性**的：不碰 `eve.Gpgpu` 就不会注册 `GpuBuffer` 类，而原生指针要交给脚本
// 就必须先有这个类（否则 simplesquirrel 退化成裸 userpointer，脚本侧一调方法就报错）。
// 这一行就是"先让 exposer 跑一遍"，不需要真的设备。
gpgpuBindingPoked <- (eve.Gpgpu != null)

values <- null
count <- -1
function readRange(buf, count_, startIndex) {
    values = buf.readFloat32s(count_, startIndex)
    count = values.len()
    return true
}
)SQ");

TEST_CASE_FIXTURE(GpuBufferBulkReadFixture, "gpgpu.buffer.readFloat32sReturnsAnArrayToScript") {
    TestBuffer buffer(4 * int(sizeof(float)));
    for (int i = 0; i < 4; ++i) buffer.writeFloat32(i, float(i + 1));

    // 脚本拿到的是一个真数组：能 .len()，也能按下标取值。
    REQUIRE(vm.callFunc(vm.findFunc("readRange"), vm, static_cast<eve::gpgpu::GpuBuffer *>(&buffer), 4, 0).toBool());
    REQUIRE_EQ(vm.find("count").toInt(), 4);
    CHECK(std::fabs(readArrayFloat(vm, vm.find("values"), 0) - 1.f) < 1e-5f);
    CHECK(std::fabs(readArrayFloat(vm, vm.find("values"), 3) - 4.f) < 1e-5f);

    // 多要 -> 裁剪（脚本侧看得见拿到几个，而不是抛错）。
    REQUIRE(vm.callFunc(vm.findFunc("readRange"), vm, static_cast<eve::gpgpu::GpuBuffer *>(&buffer), 100, 2).toBool());
    REQUIRE_EQ(vm.find("count").toInt(), 2);
    CHECK(std::fabs(readArrayFloat(vm, vm.find("values"), 0) - 3.f) < 1e-5f);

    // 起点在容量之后 -> 空数组。
    REQUIRE(vm.callFunc(vm.findFunc("readRange"), vm, static_cast<eve::gpgpu::GpuBuffer *>(&buffer), 4, 9).toBool());
    REQUIRE_EQ(vm.find("count").toInt(), 0);
}

UnitSciptTest(EcsPackFixture, R"SQ(
class Vec { x = null; y = null }
class Entity { Vec = null }
function makeEntities() {
    local a = Entity(); a.Vec = Vec(); a.Vec.x = 1.0; a.Vec.y = 2.0
    local b = Entity(); b.Vec = Vec(); b.Vec.x = 3.0; b.Vec.y = 4.0
    return [a, b]
}
function getEntities() { return makeEntities() }
function getFields() { return ["x", "y"] }
function getNoComp() { return [Entity()] }
function getNotInstances() { return [1, 2] }
function getScalar() { return 42 }
function readBack(ents) { return [ents[0].Vec.x, ents[0].Vec.y, ents[1].Vec.x, ents[1].Vec.y] }
)SQ");

TEST_CASE_FIXTURE(EcsPackFixture, "gpgpu.ecsPack.roundTrip") {
    ssq::Object ents   = vm.callFunc(vm.findFunc("getEntities"), vm);
    ssq::Object fields = vm.callFunc(vm.findFunc("getFields"), vm);
    TestBuffer  buf(4 * int(sizeof(float)));

    CHECK_EQ(packScriptEntityFloats(ents, "Vec", fields, &buf), 2);
    CHECK(std::fabs(buf.floats[0] - 1.f) < 1e-5f);
    CHECK(std::fabs(buf.floats[1] - 2.f) < 1e-5f);
    CHECK(std::fabs(buf.floats[2] - 3.f) < 1e-5f);
    CHECK(std::fabs(buf.floats[3] - 4.f) < 1e-5f);

    // Overwrite the buffer and unpack back into the instances.
    buf.floats = {10.f, 20.f, 30.f, 40.f};
    CHECK_EQ(unpackScriptEntityFloats(ents, "Vec", fields, &buf, 2), 2);
    ssq::Object back = vm.callFunc(vm.findFunc("readBack"), vm, ents);
    CHECK(std::fabs(readArrayFloat(vm, back, 0) - 10.f) < 1e-5f);
    CHECK(std::fabs(readArrayFloat(vm, back, 1) - 20.f) < 1e-5f);
    CHECK(std::fabs(readArrayFloat(vm, back, 2) - 30.f) < 1e-5f);
    CHECK(std::fabs(readArrayFloat(vm, back, 3) - 40.f) < 1e-5f);

    // entityCount caps the unpack; non-array / empty inputs are no-ops.
    CHECK_EQ(unpackScriptEntityFloats(ents, "Vec", fields, &buf, 0), 0);
    CHECK_EQ(unpackScriptEntityFloats(ents, "Vec", fields, &buf, 99), 2);
}

TEST_CASE_FIXTURE(EcsPackFixture, "gpgpu.ecsPack.contiguousRange") {
    ssq::Object ents   = vm.callFunc(vm.findFunc("getEntities"), vm);
    ssq::Object fields = vm.callFunc(vm.findFunc("getFields"), vm);
    TestBuffer  buf(4 * int(sizeof(float)));
    buf.floats = {-1.f, -1.f, -1.f, -1.f};

    CHECK_EQ(packScriptEntityFloatsRange(ents, "Vec", fields, &buf, 1, 1), 1);
    CHECK_EQ(buf.floats[0], -1.f);
    CHECK_EQ(buf.floats[1], -1.f);
    CHECK_EQ(buf.floats[2], 3.f);
    CHECK_EQ(buf.floats[3], 4.f);

    buf.floats[2] = 30.f;
    buf.floats[3] = 40.f;
    CHECK_EQ(unpackScriptEntityFloatsRange(ents, "Vec", fields, &buf, 1, 1), 1);
    ssq::Object back = vm.callFunc(vm.findFunc("readBack"), vm, ents);
    CHECK_EQ(readArrayFloat(vm, back, 0), 1.f);
    CHECK_EQ(readArrayFloat(vm, back, 1), 2.f);
    CHECK_EQ(readArrayFloat(vm, back, 2), 30.f);
    CHECK_EQ(readArrayFloat(vm, back, 3), 40.f);

    CHECK_EQ(packScriptEntityFloatsRange(ents, "Vec", fields, &buf, 9, 2), 0);
    CHECK_EQ(unpackScriptEntityFloatsRange(ents, "Vec", fields, &buf, 9, 2), 0);
}

TEST_CASE_FIXTURE(EcsPackFixture, "gpgpu.ecsPack.errors") {
    ssq::Object ents   = vm.callFunc(vm.findFunc("getEntities"), vm);
    ssq::Object fields = vm.callFunc(vm.findFunc("getFields"), vm);
    TestBuffer  buf(4 * int(sizeof(float)));

    CHECK(throwsException([&] { packScriptEntityFloats(ents, "", fields, &buf); }));
    CHECK(throwsException([&] { packScriptEntityFloats(ents, "Vec", fields, nullptr); }));

    ssq::Object noComp = vm.callFunc(vm.findFunc("getNoComp"), vm);
    CHECK(throwsException([&] { packScriptEntityFloats(noComp, "Missing", fields, &buf); }));
    ssq::Object notInst = vm.callFunc(vm.findFunc("getNotInstances"), vm);
    CHECK(throwsException([&] { packScriptEntityFloats(notInst, "Vec", fields, &buf); }));

    TestBuffer small(2 * int(sizeof(float)));
    CHECK(throwsException([&] { packScriptEntityFloats(ents, "Vec", fields, &small); }));

    ssq::Object scalar = vm.callFunc(vm.findFunc("getScalar"), vm);
    CHECK_EQ(packScriptEntityFloats(scalar, "Vec", fields, &buf), 0);
}

// ---------------------------------------------------------------------------
// Script ECS components (eve.Entity / eve.Component), not plain Squirrel classes.
// ---------------------------------------------------------------------------
//
// `EcsPackFixture` above declares `class Vec` / `class Entity` by hand, so it never touched
// the script ECS the engine actually ships: the case below goes through
// `eve.Entity.create()`, whose component instance lives in `_comps[name]` *and* in the
// declared field slot (`e.pos`). That field slot is what `packScriptEntityFloats` reads, so
// this pins the third interop bridge from plan doc 2.1 -- script component numbers entering
// a GPU buffer -- for the real component objects.

UnitSciptTest(EcsPackScriptEcsFixture, R"SQ(
class Pos extends eve.Component { x = 1.0; y = 2.0 }
class Node extends eve.Entity { pos = Pos }
function makeNodes() {
    local a = Node.create(); a.pos.x = 1.0; a.pos.y = 2.0
    local b = Node.create(); b.pos.x = 3.0; b.pos.y = 4.0
    return [a, b]
}
function getNodes() { return makeNodes() }
function getView() { return eve.view(Node) }
function getFields() { return ["x", "y"] }
function readBack(ents) { return [ents[0].pos.x, ents[0].pos.y, ents[1].pos.x, ents[1].pos.y] }
)SQ");

TEST_CASE_FIXTURE(EcsPackScriptEcsFixture, "gpgpu.ecsPack.scriptEcsComponentsRoundTrip") {
    ssq::Object ents   = vm.callFunc(vm.findFunc("getNodes"), vm);
    ssq::Object view   = vm.callFunc(vm.findFunc("getView"), vm);
    ssq::Object fields = vm.callFunc(vm.findFunc("getFields"), vm);
    TestBuffer  buf(4 * int(sizeof(float)));

    REQUIRE_EQ(packScriptEntityFloats(ents, "pos", fields, &buf), 2);
    REQUIRE(std::fabs(buf.floats[0] - 1.f) < 1e-5f);
    REQUIRE(std::fabs(buf.floats[1] - 2.f) < 1e-5f);
    REQUIRE(std::fabs(buf.floats[2] - 3.f) < 1e-5f);
    REQUIRE(std::fabs(buf.floats[3] - 4.f) < 1e-5f);

    // The same components packed through eve.view() -- the array the GPU system actually
    // packs -- and in entity-id order, which is what makes the row indices deterministic.
    REQUIRE_EQ(packScriptEntityFloats(view, "pos", fields, &buf), 2);
    REQUIRE(std::fabs(buf.floats[0] - 1.f) < 1e-5f);
    REQUIRE(std::fabs(buf.floats[2] - 3.f) < 1e-5f);

    // Read back into the component instances through the entity field slot.
    buf.floats = {10.f, 20.f, 30.f, 40.f};
    REQUIRE_EQ(unpackScriptEntityFloats(ents, "pos", fields, &buf, 2), 2);
    ssq::Object back = vm.callFunc(vm.findFunc("readBack"), vm, ents);
    REQUIRE(std::fabs(readArrayFloat(vm, back, 0) - 10.f) < 1e-5f);
    REQUIRE(std::fabs(readArrayFloat(vm, back, 1) - 20.f) < 1e-5f);
    REQUIRE(std::fabs(readArrayFloat(vm, back, 2) - 30.f) < 1e-5f);
    REQUIRE(std::fabs(readArrayFloat(vm, back, 3) - 40.f) < 1e-5f);
}

// ---------------------------------------------------------------------------
// 登记过的脚本组件：字段槽位在登记时就被摘掉、数据搬进引擎侧存储（方案文档 §9.64），
// 所以打包这条路必须走 ECS 的归属查询，而不是自己去 `sq_get` 实例槽位——后者对这些组件
// 会一律失败（`sq_get` 用 DONT_FALL_BACK，连 `_get` 都不会兜底）。
// ---------------------------------------------------------------------------

UnitSciptTest(EcsPackRegisteredScriptEcsFixture, R"SQ(
class RPos extends eve.Component { x = 1.0; y = 2.0 }
class RNode extends eve.Entity { rpos = RPos }
function makeRegistered() {
    registeredOk <- eve.component("RPos", 1, RPos).ok
    local a = RNode.create(); a.rpos.x = 5.0; a.rpos.y = 6.0
    local b = RNode.create(); b.rpos.x = 7.0; b.rpos.y = 8.0
    idA <- a.getId()
    idB <- b.getId()
    return [a, b]
}
function getRegisteredFields() { return ["x", "y"] }
function readBackRegistered(ents) { return [ents[0].rpos.x, ents[0].rpos.y, ents[1].rpos.x, ents[1].rpos.y] }
)SQ");

TEST_CASE_FIXTURE(EcsPackRegisteredScriptEcsFixture, "gpgpu.ecsPack.registeredScriptEcsComponentsRoundTrip") {
    ssq::Object ents = vm.callFunc(vm.findFunc("makeRegistered"), vm);
    REQUIRE(vm.find("registeredOk").toBool());

    // 登记过的组件：脚本写 `a.rpos.x = 5.0` 落进的是**引擎侧存储**（不是实例槽位），
    // 打包读的也是同一份数据——这条路就是 `readScriptComponentField` 的意义所在。
    const auto                         idA = static_cast<std::uint64_t>(vm.find("idA").toInt());
    const eve::ScriptComponentStoreKey keyA{idA, "RPos", eve::SchemaVersion(1)};
    const eve::Value                  *stored = eve::findScriptComponentValues(keyA);
    REQUIRE(stored != nullptr);
    REQUIRE_EQ(stored->find("x")->asDouble(), 5.0);

    ssq::Object fields = vm.callFunc(vm.findFunc("getRegisteredFields"), vm);
    TestBuffer  buf(4 * int(sizeof(float)));

    REQUIRE_EQ(packScriptEntityFloats(ents, "rpos", fields, &buf), 2);
    REQUIRE(std::fabs(buf.floats[0] - 5.f) < 1e-5f);
    REQUIRE(std::fabs(buf.floats[1] - 6.f) < 1e-5f);
    REQUIRE(std::fabs(buf.floats[2] - 7.f) < 1e-5f);
    REQUIRE(std::fabs(buf.floats[3] - 8.f) < 1e-5f);

    // 解包写回引擎侧存储，脚本侧从 `_get` 读到的就是新值。
    buf.floats = {50.f, 60.f, 70.f, 80.f};
    REQUIRE_EQ(unpackScriptEntityFloats(ents, "rpos", fields, &buf, 2), 2);
    ssq::Object back = vm.callFunc(vm.findFunc("readBackRegistered"), vm, ents);
    REQUIRE(std::fabs(readArrayFloat(vm, back, 0) - 50.f) < 1e-5f);
    REQUIRE(std::fabs(readArrayFloat(vm, back, 1) - 60.f) < 1e-5f);
    REQUIRE(std::fabs(readArrayFloat(vm, back, 2) - 70.f) < 1e-5f);
    REQUIRE(std::fabs(readArrayFloat(vm, back, 3) - 80.f) < 1e-5f);

    // 解包的结果确实落在引擎侧存储里（原生侧直接读得到，不必问脚本）。
    const eve::Value *afterUnpack = eve::findScriptComponentValues(keyA);
    REQUIRE(afterUnpack != nullptr);
    REQUIRE_EQ(afterUnpack->find("x")->asDouble(), 50.0);
    REQUIRE_EQ(afterUnpack->find("y")->asDouble(), 60.0);
}
}  // namespace test_gpgpu
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_backend_parity)
#include "PrivateTestScope.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <glm/ext/matrix_clip_space.hpp>
#include <memory>
#include "GraphicsParitySupport.h"
#include "common/config.h"
#include "filesystem/FileData.h"
#include "graphics/AlphaMask.h"
#include "graphics/Canvas.h"
#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/PrimitiveDrawList.h"
#include "graphics/RenderControl.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/TextureSampler.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "pixelworld/PixelWorld.h"
#include "pixelworld/graphics/PixelWorldGraphics.h"
#include "zeroerr/unittest.h"
// graphics_backend_parity.cpp
namespace test_graphics_backend_parity {
using namespace eve::graphics;

using namespace eve::graphics::parity_test;


TEST_CASE("graphics.backendParity.textureUpdateAndAlphaBlend") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    const std::string backend = gfx->getBackendName();
    REQUIRE(backend == expectedBackendName());

    const uint8_t red[4]   = {255, 0, 0, 255};
    const uint8_t green[4] = {0, 255, 0, 255};
    Texture      *texture  = gfx->newTexture(1, 1, red);
    REQUIRE(texture != nullptr);
    Texture *stable = texture;
    REQUIRE(gfx->updateTexture(texture, 1, 1, green));
    REQUIRE(texture == stable);
    // Same-size streaming updates must retain the Vulkan image and descriptor.
    // The former replacement path exhausted the shared descriptor pool after
    // a few hundred frames in the volume-fluid presentation loop.
    for (int frame = 0; frame < 400; ++frame) REQUIRE(gfx->updateTexture(texture, 1, 1, (frame & 1) ? red : green));
    REQUIRE(!gfx->updateTexture(texture, 2, 1, green));
    REQUIRE(gfx->updateTextureRegion(texture, 0, 0, 1, 1, red).ok());
    CHECK(!gfx->updateTextureRegion(texture, 1, 0, 1, 1, red).ok());
    CHECK(!gfx->updateTextureRegion(texture, 0, 0, 1, 1, std::span<const uint8_t>(red, 3)).ok());
    const TextureRegionUpload rejectedBatch[] = {
        {0, 0, 1, 1, green, 0},
        {1, 0, 1, 1, green, 0},
    };
    CHECK(!gfx->updateTextureRegions(texture, rejectedBatch).ok());

    Canvas *canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    gfx->setCanvas(canvas);
    gfx->clear(Color(0.f, 0.f, 1.f, 1.f), std::nullopt, std::nullopt);
    gfx->drawTexturedRect(texture, 8.f, 8.f, 24.f, 24.f, Color(1.f));
    gfx->drawSolidRect(32.f, 8.f, 24.f, 24.f, Color(1.f, 0.f, 0.f, 0.5f), BlendMode::Alpha);
    gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image.get() != nullptr);
    const uint8_t *background = pixel(*image, 2, 2);
    REQUIRE(background[0] < 8);
    REQUIRE(background[1] < 8);
    REQUIRE(background[2] > 247);

    const uint8_t *updated = pixel(*image, 16, 16);
    REQUIRE(updated[0] > 247);
    REQUIRE(updated[1] < 8);
    REQUIRE(updated[2] < 8);

    const uint8_t *blended    = pixel(*image, 40, 16);
    const bool     redBlended = blended[0] >= 126 && blended[0] <= 129;
    REQUIRE(redBlended);
    REQUIRE(blended[1] < 8);
    const bool blueBlended = blended[2] >= 126 && blended[2] <= 129;
    REQUIRE(blueBlended);
    writeParityArtifact(*image, "texture_update_alpha_blend", backend);
}

TEST_CASE("graphics.backendParity.pixelWorldMillionPixelAtlas") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);

    eve::pixelworld::PixelWorld world(4242);
    // Radius 724 covers every cell in [0,1024)^2, creating a genuine one-million
    // pixel viewport while also proving off-viewport chunks are clipped by the adapter.
    REQUIRE(world.paintCircleChecked(512, 512, 724, "stone").ok());
    eve::pixelworld_graphics::PixelWorldAtlasRenderer renderer(0, 0, 1024, 1024);
    CHECK(renderer.sync(&world, gfx) == 256);
    REQUIRE(renderer.texture() != nullptr);
    CHECK(renderer.texture()->width == 1024);
    CHECK(renderer.texture()->height == 1024);
    CHECK(renderer.uploadCount() == 1);
    CHECK(renderer.totalUploadedChunks() == 256);
    CHECK(renderer.renderedRevision() == world.revision());

    world.setMaterial(1, 1, "sand");
    world.setMaterial(1022, 1022, "water");
    CHECK(renderer.sync(&world, gfx) == 2);
    CHECK(renderer.uploadCount() == 2);
    CHECK(renderer.totalUploadedChunks() == 258);
    CHECK(renderer.renderedRevision() == world.revision());
}

TEST_CASE("graphics.backendParity.draw2dUvRotationAndBlendModes") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    const std::string backend          = gfx->getBackendName();
    const bool        supportedBackend = backend == "vulkan" || backend == "webgpu";
    REQUIRE(supportedBackend);

    const uint8_t stripes[] = {
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
    };
    Texture *texture = gfx->newTexture(4, 1, stripes);
    REQUIRE(texture != nullptr);
    gfx->setTextureSampler(texture, TextureSampler::nearest());

    Canvas *canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    gfx->setCanvas(canvas);
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);

    // Crop the second texel. Nearest sampling makes the expected green region exact.
    gfx->drawTexturedRectShaderUV(texture, nullptr, 2.f, 2.f, 18.f, 12.f, 0.25f, 0.f, 0.5f, 1.f, Color(1.f), false,
                                  BlendMode::Opaque);

    // A 90-degree turn changes the wide rectangle into a narrow vertical footprint.
    gfx->drawSolidRectRotated(34.f, 10.f, 18.f, 6.f, 90.f, Color(1.f, 1.f, 0.f, 1.f), BlendMode::Opaque);
    gfx->drawTexturedRectShaderUVRotated(texture, nullptr, 50.f, 10.f, 18.f, 6.f, 90.f, 0.f, 0.f, 0.25f, 1.f,
                                         Color(1.f), false, BlendMode::Opaque);

    gfx->drawSolidRect(2.f, 28.f, 12.f, 12.f, Color(0.f, 0.f, 0.5f, 1.f), BlendMode::Opaque);
    gfx->drawSolidRect(2.f, 28.f, 12.f, 12.f, Color(0.5f, 0.f, 0.f, 1.f), BlendMode::Additive);

    gfx->drawSolidRect(18.f, 28.f, 12.f, 12.f, Color(0.f, 0.f, 1.f, 1.f), BlendMode::Opaque);
    gfx->drawSolidRect(18.f, 28.f, 12.f, 12.f, Color(0.25f, 0.f, 0.f, 0.25f), BlendMode::Premultiplied);

    gfx->drawSolidRect(34.f, 28.f, 12.f, 12.f, Color(0.5f, 1.f, 0.25f, 1.f), BlendMode::Opaque);
    gfx->drawSolidRect(34.f, 28.f, 12.f, 12.f, Color(0.5f, 0.25f, 1.f, 1.f), BlendMode::Multiply);
    gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image.get() != nullptr);

    const uint8_t *crop = pixel(*image, 10, 8);
    REQUIRE(crop[0] < 8);
    REQUIRE(crop[1] > 247);
    REQUIRE(crop[2] < 8);

    const uint8_t *rotatedSolid = pixel(*image, 34, 3);
    REQUIRE(rotatedSolid[0] > 247);
    REQUIRE(rotatedSolid[1] > 247);
    REQUIRE(rotatedSolid[2] < 8);
    const uint8_t *outsideSolid = pixel(*image, 27, 10);
    REQUIRE(outsideSolid[0] < 8);
    REQUIRE(outsideSolid[1] < 8);
    REQUIRE(outsideSolid[2] < 8);

    const uint8_t *rotatedTexture = pixel(*image, 50, 3);
    REQUIRE(rotatedTexture[0] > 247);
    REQUIRE(rotatedTexture[1] < 8);
    REQUIRE(rotatedTexture[2] < 8);

    const uint8_t *additive    = pixel(*image, 8, 34);
    const bool     additiveRed = additive[0] >= 126 && additive[0] <= 129;
    REQUIRE(additiveRed);
    REQUIRE(additive[1] < 8);
    const bool additiveBlue = additive[2] >= 126 && additive[2] <= 129;
    REQUIRE(additiveBlue);

    const uint8_t *premultiplied    = pixel(*image, 24, 34);
    const bool     premultipliedRed = premultiplied[0] >= 62 && premultiplied[0] <= 65;
    REQUIRE(premultipliedRed);
    REQUIRE(premultiplied[1] < 8);
    const bool premultipliedBlue = premultiplied[2] >= 190 && premultiplied[2] <= 193;
    REQUIRE(premultipliedBlue);

    const uint8_t *multiply      = pixel(*image, 40, 34);
    const bool     multiplyRed   = multiply[0] >= 62 && multiply[0] <= 65;
    const bool     multiplyGreen = multiply[1] >= 62 && multiply[1] <= 65;
    const bool     multiplyBlue  = multiply[2] >= 62 && multiply[2] <= 65;
    REQUIRE(multiplyRed);
    REQUIRE(multiplyGreen);
    REQUIRE(multiplyBlue);
    writeParityArtifact(*image, "draw2d_uv_rotation_blend_modes", backend);
}

TEST_CASE("graphics.backendParity.lighting2dNormalMapAndMultipleLights") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    const std::string backend          = gfx->getBackendName();
    const bool        supportedBackend = backend == "vulkan" || backend == "webgpu";
    REQUIRE(supportedBackend);

    const uint8_t white[]       = {255, 255, 255, 255};
    const uint8_t flatNormal[]  = {128, 128, 255, 255};
    const uint8_t rightNormal[] = {255, 128, 128, 255};
    Texture      *albedo        = gfx->newTexture(1, 1, white);
    Texture      *flat          = gfx->newTexture(1, 1, flatNormal);
    Texture      *right         = gfx->newTexture(1, 1, rightNormal);
    REQUIRE(albedo != nullptr);
    REQUIRE(flat != nullptr);
    REQUIRE(right != nullptr);

    Lighting2DUBO directional{};
    directional.ambient             = Color(0.05f, 0.05f, 0.05f, 0.f);
    directional.meta                = Color(1.f, 64.f, 64.f, 0.f);
    directional.lights[0].posRadius = Color(1.f, 0.f, 0.f, 0.f);
    directional.lights[0].color     = Color(0.5f, 0.5f, 0.5f, 0.f);

    Canvas *normalCanvas = gfx->newCanvas(64, 64);
    REQUIRE(normalCanvas != nullptr);
    gfx->setCanvas(normalCanvas);
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    gfx->setLighting2D(directional);
    gfx->drawTexturedRectLitUV(albedo, flat, 4.f, 8.f, 24.f, 24.f, 0.f, 0.f, 1.f, 1.f, Color(1.f));
    gfx->drawTexturedRectLitUV(albedo, right, 36.f, 8.f, 24.f, 24.f, 0.f, 0.f, 1.f, 1.f, Color(1.f));
    gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> normalImage(normalCanvas->newImageData());
    REQUIRE(normalImage.get() != nullptr);
    const uint8_t *flatLit  = pixel(*normalImage, 16, 20);
    const uint8_t *rightLit = pixel(*normalImage, 48, 20);
    REQUIRE(flatLit[0] > 32);
    REQUIRE(rightLit[0] > flatLit[0] + 50);
    REQUIRE(rightLit[1] > flatLit[1] + 50);
    REQUIRE(rightLit[2] > flatLit[2] + 50);
    writeParityArtifact(*normalImage, "lighting2d_normal_map", backend);

    Lighting2DUBO points{};
    points.ambient             = Color(0.02f, 0.02f, 0.02f, 0.f);
    points.meta                = Color(2.f, 64.f, 64.f, 0.f);
    points.lights[0].posRadius = Color(10.f, 48.f, 0.f, 28.f);
    points.lights[0].color     = Color(0.9f, 0.f, 0.f, 0.f);
    points.lights[1].posRadius = Color(54.f, 48.f, 0.f, 28.f);
    points.lights[1].color     = Color(0.f, 0.f, 0.9f, 0.f);

    Canvas *pointCanvas = gfx->newCanvas(64, 64);
    REQUIRE(pointCanvas != nullptr);
    gfx->setCanvas(pointCanvas);
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    gfx->setLighting2D(points);
    gfx->drawTexturedRectLitUV(albedo, flat, 2.f, 36.f, 60.f, 24.f, 0.f, 0.f, 1.f, 1.f, Color(1.f));
    gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> pointImage(pointCanvas->newImageData());
    REQUIRE(pointImage.get() != nullptr);
    const uint8_t *leftLight  = pixel(*pointImage, 10, 48);
    const uint8_t *rightLight = pixel(*pointImage, 54, 48);
    REQUIRE(leftLight[0] > leftLight[2] + 80);
    REQUIRE(rightLight[2] > rightLight[0] + 80);
    REQUIRE(leftLight[0] > 120);
    REQUIRE(rightLight[2] > 120);
    writeParityArtifact(*pointImage, "lighting2d_multiple_lights", backend);
}

TEST_CASE("graphics.backendParity.textureSamplingRepeatAndMipmaps") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    REQUIRE(gfx->getMaxAnisotropy() >= 1.f);
    const std::string backend          = gfx->getBackendName();
    const bool        supportedBackend = backend == "vulkan" || backend == "webgpu";
    REQUIRE(supportedBackend);

    const uint8_t redBlue[]      = {255, 0, 0, 255, 0, 0, 255, 255};
    Texture      *nearestTexture = gfx->newTexture(2, 1, redBlue);
    Texture      *repeatTexture  = gfx->newTexture(2, 1, redBlue);
    Texture      *linearTexture  = gfx->newTexture(2, 1, redBlue);
    REQUIRE(nearestTexture != nullptr);
    REQUIRE(repeatTexture != nullptr);
    REQUIRE(linearTexture != nullptr);

    uint8_t checker[4 * 4]{};
    for (int x = 0; x < 4; ++x) {
        uint8_t *p            = checker + x * 4;
        p[x % 2 == 0 ? 0 : 1] = 255;
        p[3]                  = 255;
    }
    TextureCreateInfo mipInfo;
    mipInfo.generateMipmaps = true;
    Texture *mipped         = gfx->newTexture(4, 1, checker, mipInfo);
    REQUIRE(mipped != nullptr);
    REQUIRE(mipped->getMipmapCount() == 3);
    const bool mipSamplingEnabled = mipped->getSampler().mipmap == MipmapMode::Linear;
    REQUIRE(mipSamplingEnabled);

    Canvas *canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    gfx->setCanvas(canvas);
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);

    gfx->setTextureSampler(nearestTexture, TextureSampler::nearest());
    gfx->drawTexturedRectShaderUV(nearestTexture, nullptr, 2.f, 2.f, 28.f, 12.f, 0.f, 0.f, 1.f, 1.f, Color(1.f), false,
                                  BlendMode::Opaque);

    TextureSampler repeat = TextureSampler::nearest();
    repeat.repeatU        = true;
    gfx->setTextureSampler(repeatTexture, repeat);
    gfx->drawTexturedRectShaderUV(repeatTexture, nullptr, 34.f, 2.f, 28.f, 12.f, 0.f, 0.f, 2.f, 1.f, Color(1.f), false,
                                  BlendMode::Opaque);

    gfx->setTextureSampler(linearTexture, TextureSampler::linear());
    gfx->drawTexturedRectShaderUV(linearTexture, nullptr, 2.f, 20.f, 28.f, 12.f, 0.f, 0.f, 1.f, 1.f, Color(1.f), false,
                                  BlendMode::Opaque);

    TextureSampler lastMip = TextureSampler::nearest();
    lastMip.mipmap         = MipmapMode::Nearest;
    lastMip.minLod         = 2.f;
    lastMip.maxLod         = 2.f;
    gfx->setTextureSampler(mipped, lastMip);
    gfx->drawTexturedRect(mipped, 34.f, 20.f, 28.f, 28.f, Color(1.f));
    gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image.get() != nullptr);
    const uint8_t *nearestLeft  = pixel(*image, 8, 8);
    const uint8_t *nearestRight = pixel(*image, 24, 8);
    REQUIRE(nearestLeft[0] > 247);
    REQUIRE(nearestLeft[2] < 8);
    REQUIRE(nearestRight[0] < 8);
    REQUIRE(nearestRight[2] > 247);

    const uint8_t *repeat0 = pixel(*image, 38, 8);
    const uint8_t *repeat1 = pixel(*image, 45, 8);
    const uint8_t *repeat2 = pixel(*image, 52, 8);
    const uint8_t *repeat3 = pixel(*image, 59, 8);
    REQUIRE(repeat0[0] > 247);
    REQUIRE(repeat1[2] > 247);
    REQUIRE(repeat2[0] > 247);
    REQUIRE(repeat3[2] > 247);

    const uint8_t *linearBoundary = pixel(*image, 16, 26);
    REQUIRE(linearBoundary[0] > 96);
    REQUIRE(linearBoundary[2] > 96);

    const uint8_t *mip      = pixel(*image, 48, 34);
    const bool     mipRed   = mip[0] >= 126 && mip[0] <= 129;
    const bool     mipGreen = mip[1] >= 126 && mip[1] <= 129;
    REQUIRE(mipRed);
    REQUIRE(mipGreen);
    REQUIRE(mip[2] < 8);
    writeParityArtifact(*image, "texture_sampling_repeat_mipmaps", backend);
}

TEST_CASE("graphics.backendParity.webgpuCustomShaderLifetime") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    REQUIRE(gfx->getBackendName() == expectedBackendName());
    if (gfx->getBackendName() != "webgpu") {
        return;
    }

    const char *frag   = R"wgsl(
struct FSIn {
    @location(0) color: vec4f,
    @location(1) uv: vec2f,
};
@group(0) @binding(0) var mainTex: texture_2d<f32>;
@group(0) @binding(2) var mainSamp: sampler;
@fragment
fn fs_main(in: FSIn) -> @location(0) vec4f {
    let sampled = textureSample(mainTex, mainSamp, in.uv);
    return vec4f(sampled.r * 0.25, sampled.g * 0.5, sampled.b, sampled.a) * in.color;
}
)wgsl";
    Shader     *shader = gfx->newShaderFromWgsl({}, frag);
    REQUIRE(shader != nullptr);
    REQUIRE(shader->gpuHandle != nullptr);

    const uint8_t white[4] = {255, 255, 255, 255};
    Texture      *texture  = gfx->newTexture(1, 1, white);
    Canvas       *canvas   = gfx->newCanvas(64, 64);
    REQUIRE(texture != nullptr);
    REQUIRE(canvas != nullptr);
    gfx->setCanvas(canvas);
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    gfx->drawTexturedRectShader(texture, shader, 8.f, 8.f, 48.f, 48.f, Color(1.f, 1.f, 1.f, 1.f));
    gfx->setCanvas(nullptr);

    const Color center = canvas->getPixel(32, 32);
    REQUIRE(center.r > 0.20f);
    REQUIRE(center.r < 0.30f);
    REQUIRE(center.g > 0.45f);
    REQUIRE(center.g < 0.55f);
    REQUIRE(center.b > 0.95f);
}

TEST_CASE("graphics.backendParity.dynamicMeshUpdate") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);

    const float positions[] = {
        -1.f, -1.f, 0.f, 1.f, -1.f, 0.f, 0.f, 1.f, 0.f,
    };
    const uint32_t indices[] = {0, 1, 2};
    Mesh          *mesh      = gfx->newMeshFromArrays(positions, nullptr, nullptr, 3, indices, 3);
    REQUIRE(mesh != nullptr);
    void *stableGpuHandle = mesh->gpuHandle;

    const float moved[] = {
        -2.f, -1.f, 0.f, 2.f, -1.f, 0.f, 0.f, 2.f, 0.f,
    };
    REQUIRE(gfx->updateMeshVertices(mesh, moved, nullptr, nullptr, 3, nullptr, 0));
    REQUIRE(mesh->gpuHandle == stableGpuHandle);
    REQUIRE(mesh->gpuVertexCount == 3);
    REQUIRE(mesh->indexCount == 3);

    const uint32_t invalid[] = {0, 1, 3};
    REQUIRE(!gfx->updateMeshVertices(mesh, moved, nullptr, nullptr, 3, invalid, 3));
}

TEST_CASE("graphics.backendParity.gbufferAlphaCutout") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);

    const float positions[] = {
        -1.f, -1.f, 0.5f, 1.f, -1.f, 0.5f, 1.f, 1.f, 0.5f, -1.f, 1.f, 0.5f,
    };
    const float normals[] = {
        0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
    };
    const float uvs[] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    // These 3D parity fixtures use identity clip transforms rather than
    // perspectiveVulkanRH_ZO, so reverse the quad to preserve the engine's
    // framebuffer-space CCW front-face convention.
    const uint32_t indices[] = {0, 2, 1, 2, 0, 3};
    Mesh          *mesh      = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);

    const uint8_t cutoutTexture[] = {255, 0, 0, 0, 255, 0, 0, 255};
    Texture      *albedo          = gfx->newTexture(2, 1, cutoutTexture);
    REQUIRE(albedo != nullptr);

    gfx->beginGBufferPass(64, 64);
    gfx->drawMeshGBufferAlpha(mesh, glm::mat4(1.f), glm::mat4(1.f), 0.1f, 100.f, albedo);
    gfx->endGBufferPass();

    std::unique_ptr<eve::image::ImageData> image(gfx->readGBufferToImageData("albedo"));
    REQUIRE(image.get() != nullptr);
    writeParityArtifact(*image, "gbuffer_alpha_cutout", gfx->getBackendName());
    const uint8_t *discarded = pixel(*image, 16, 16);
    REQUIRE(discarded[0] < 8);
    const uint8_t *kept = pixel(*image, 48, 16);
    REQUIRE(kept[0] > 247);
    REQUIRE(kept[1] < 8);
    REQUIRE(kept[2] < 8);

    std::unique_ptr<eve::image::ImageData> normal(gfx->readGBufferToImageData("normal"));
    REQUIRE(normal.get() != nullptr);
    const uint8_t *encodedNormal   = pixel(*normal, 48, 16);
    const bool     normalXCentered = encodedNormal[0] >= 127 && encodedNormal[0] <= 128;
    const bool     normalYCentered = encodedNormal[1] >= 127 && encodedNormal[1] <= 128;
    REQUIRE(normalXCentered);
    REQUIRE(normalYCentered);
    REQUIRE(encodedNormal[2] > 247);
    writeParityArtifact(*normal, "gbuffer_world_normal", gfx->getBackendName());

    std::unique_ptr<eve::image::ImageData> depth(gfx->readGBufferToImageData("depth"));
    REQUIRE(depth.get() != nullptr);
    const uint8_t *linearDepth         = pixel(*depth, 48, 16);
    const bool     expectedLinearDepth = linearDepth[0] <= 1;
    REQUIRE(expectedLinearDepth);
    REQUIRE(linearDepth[1] == linearDepth[0]);
    REQUIRE(linearDepth[2] == linearDepth[0]);
    writeParityArtifact(*depth, "gbuffer_linear_depth", gfx->getBackendName());
}

TEST_CASE("graphics.backendParity.decalLayerProjection") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    const float positions[] = {
        -1.f, -1.f, 0.5f, 1.f, -1.f, 0.5f, 1.f, 1.f, 0.5f, -1.f, 1.f, 0.5f,
    };
    const float normals[] = {
        0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
    };
    const float    uvs[]     = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const uint32_t indices[] = {0, 2, 1, 2, 0, 3};
    Mesh          *mesh      = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t white[] = {255, 255, 255, 255};
    const uint8_t red[]   = {200, 20, 20, 255};
    Texture      *base    = gfx->newTexture(1, 1, white);
    Texture      *decal   = gfx->newTexture(1, 1, red);
    REQUIRE(base != nullptr);
    REQUIRE(decal != nullptr);

    gfx->beginGBufferPass(64, 64);
    gfx->drawMeshGBuffer(mesh, glm::mat4(1.f), glm::mat4(1.f), 0.1f, 100.f, base);
    gfx->endGBufferPass();
    gfx->beginDecalPass(64, 64);
    gfx->setDecalCamera(glm::mat4(1.f), 0.1f, 100.f);
    glm::mat4 decalModel(1.f);
    decalModel[2][2] = 2.f;
    gfx->drawDecal(decalModel, decal, nullptr, nullptr, nullptr, 1.f, 0.f, 0.f, 0.f, 0.f);
    gfx->endDecalPass();

    std::unique_ptr<eve::image::ImageData> image(gfx->readDecalLayerToImageData("albedo"));
    REQUIRE(image.get() != nullptr);
    writeParityArtifact(*image, "decal_layer_projection", gfx->getBackendName());
    const uint8_t *center         = pixel(*image, 32, 32);
    const bool     decalCenterRed = center[0] > 150 && center[1] < 80;
    REQUIRE(decalCenterRed);
    const uint8_t *corner = pixel(*image, 2, 2);
    REQUIRE(corner[0] < 20);

    Lighting3DPack lighting{};
    lighting.ambient = glm::vec4(1.f, 1.f, 1.f, 0.f);
    gfx->setMesh3DLighting(lighting);
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    gfx->setMesh3DView(glm::mat4(1.f));
    gfx->setMesh3DCameraPos(glm::vec3(0.f, 0.f, 3.f));
    Canvas *forwardTarget = gfx->newCanvas(64, 64);
    REQUIRE(forwardTarget != nullptr);
    gfx->begin3DFrameToCanvas(forwardTarget);
    gfx->drawMesh(mesh, glm::mat4(1.f), base, Color(1.f));
    gfx->end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> composited(forwardTarget->newImageData());
    REQUIRE(composited.get() != nullptr);
    const uint8_t *compositedCenter  = pixel(*composited, 32, 32);
    const bool     centerHasRedDecal = compositedCenter[0] > compositedCenter[1] + 80;
    REQUIRE(centerHasRedDecal);
    const uint8_t *compositedCorner = pixel(*composited, 2, 2);
    const bool     cornerHasBase    = compositedCorner[0] > 180 && compositedCorner[1] > 180;
    REQUIRE(cornerHasBase);
    writeParityArtifact(*composited, "decal_forward_composite", gfx->getBackendName());
}

TEST_CASE("graphics.backendParity.pbrNormalParallaxAndCellBomb") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    const std::string backend          = gfx->getBackendName();
    const bool        supportedBackend = backend == "vulkan" || backend == "webgpu";
    REQUIRE(supportedBackend);

    const float positions[] = {
        -1.f, -1.f, 0.5f, 1.f, -1.f, 0.5f, 1.f, 1.f, 0.5f, -1.f, 1.f, 0.5f,
    };
    const float normals[] = {
        0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
    };
    const float    uvs[]     = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const uint32_t indices[] = {0, 2, 1, 2, 0, 3};
    Mesh          *mesh      = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);

    const uint8_t stripes[] = {
        255, 32, 16, 255, 255, 32, 16, 255, 16, 32, 255, 255, 16, 32, 255, 255,
        255, 32, 16, 255, 255, 32, 16, 255, 16, 32, 255, 255, 16, 32, 255, 255,
    };
    const uint8_t flatNormal[]   = {128, 128, 255, 255};
    const uint8_t tiltedNormal[] = {255, 128, 128, 255};
    const uint8_t raisedHeight[] = {64, 64, 64, 255};
    Texture      *albedo         = gfx->newTexture(8, 1, stripes);
    Texture      *flat           = gfx->newTexture(1, 1, flatNormal);
    Texture      *tilted         = gfx->newTexture(1, 1, tiltedNormal);
    Texture      *height         = gfx->newTexture(1, 1, raisedHeight);
    REQUIRE(albedo != nullptr);
    REQUIRE(flat != nullptr);
    REQUIRE(tilted != nullptr);
    REQUIRE(height != nullptr);
    gfx->setTextureSampler(albedo, TextureSampler::nearest());

    Lighting3DPack lighting{};
    lighting.ambient             = glm::vec4(0.02f, 0.02f, 0.02f, 0.f);
    lighting.count               = 1;
    lighting.lights[0].posRadius = glm::vec4(1.f, 0.f, 0.15f, 0.f);
    lighting.lights[0].color     = glm::vec4(0.85f, 0.85f, 0.85f, 1.f);
    gfx->setMesh3DLighting(lighting);
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    gfx->setMesh3DView(glm::mat4(1.f));
    gfx->setMesh3DCameraPos(glm::vec3(1.2f, 0.f, 3.f));
    gfx->setMesh3DMaterial(0.f, 0.8f);

    auto render = [&](Texture *normal, Texture *heightMap, float parallax, float bombStrength, const char *artifact) {
        gfx->setMesh3DNormalTexture(normal);
        gfx->setMesh3DHeightTexture(heightMap);
        gfx->setMesh3DParallax(parallax, 12.f, 12.f);
        gfx->setMesh3DTexCellBomb(4.f, bombStrength, 1.f);
        Canvas *target = gfx->newCanvas(64, 64);
        REQUIRE(target != nullptr);
        gfx->begin3DFrameToCanvas(target);
        gfx->drawMesh(mesh, glm::mat4(1.f), albedo, Color(1.f));
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(target->newImageData());
        REQUIRE(image.get() != nullptr);
        writeParityArtifact(*image, artifact, backend);
        return image;
    };

    auto baseline       = render(flat, height, 0.f, 0.f, "pbr_material_baseline");
    auto normalMapped   = render(tilted, height, 0.f, 0.f, "pbr_material_normal_map");
    auto parallaxMapped = render(flat, height, 0.3f, 0.f, "pbr_material_parallax");
    auto cellBombed     = render(flat, height, 0.f, 1.f, "pbr_material_cell_bomb");

    // The albedo texture requests nearest filtering. The center boundary must
    // stay a hard blue-to-red transition on both backends; using WebGPU's
    // backend-global linear sampler here blends the two texels.
    const uint8_t *nearestBlue = pixel(*baseline, 31, 32);
    const uint8_t *nearestRed  = pixel(*baseline, 32, 32);
    REQUIRE(nearestBlue[2] > nearestBlue[0] + 50);
    REQUIRE(nearestRed[0] > nearestRed[2] + 50);
    // A cached material binding must follow sampler edits between offscreen frames.
    gfx->setTextureSampler(albedo, TextureSampler::linear());
    auto linearSampled = render(flat, height, 0.f, 0.f, "pbr_material_linear_sampler");
    REQUIRE(imageRgbDifference(*baseline, *linearSampled) > 0u);
    gfx->setTextureSampler(albedo, TextureSampler::nearest());
    auto nearestAgain = render(flat, height, 0.f, 0.f, "pbr_material_nearest_sampler_restored");
    REQUIRE(imageRgbDifference(*baseline, *nearestAgain) == 0u);
    REQUIRE(imageRgbDifference(*baseline, *normalMapped) > 100000u);
    REQUIRE(imageRgbDifference(*baseline, *parallaxMapped) > 50000u);
    REQUIRE(imageRgbDifference(*baseline, *cellBombed) > 50000u);
}

TEST_CASE("graphics.backendParity.pbrEnvironmentAndCloudShadow") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    const std::string backend          = gfx->getBackendName();
    const bool        supportedBackend = backend == "vulkan" || backend == "webgpu";
    REQUIRE(supportedBackend);

    const float positions[] = {
        -1.f, -1.f, 0.5f, 1.f, -1.f, 0.5f, 1.f, 1.f, 0.5f, -1.f, 1.f, 0.5f,
    };
    const float normals[] = {
        0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
    };
    const float    uvs[]     = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const uint32_t indices[] = {0, 2, 1, 2, 0, 3};
    Mesh          *mesh      = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);

    const uint8_t white[]      = {255, 255, 255, 255};
    const uint8_t flatNormal[] = {128, 128, 255, 255};
    const uint8_t greenCube[]  = {
        16, 220, 32, 255, 16, 220, 32, 255, 16, 220, 32, 255, 16, 220, 32, 255, 16, 220, 32, 255, 16, 220, 32, 255,
    };
    Texture *albedo      = gfx->newTexture(1, 1, white);
    Texture *normal      = gfx->newTexture(1, 1, flatNormal);
    Texture *environment = gfx->newCubemap(1, greenCube);
    REQUIRE(albedo != nullptr);
    REQUIRE(normal != nullptr);
    REQUIRE(environment != nullptr);
    gfx->setMesh3DNormalTexture(normal);
    gfx->setMesh3DHeightTexture(nullptr);
    gfx->setMesh3DParallax(0.f);
    gfx->setMesh3DTexCellBomb(1.f, 0.f);
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    gfx->setMesh3DView(glm::mat4(1.f));
    gfx->setMesh3DCameraPos(glm::vec3(0.f, 0.f, 3.f));

    auto render = [&](const char *artifact) {
        Canvas *target = gfx->newCanvas(64, 64);
        REQUIRE(target != nullptr);
        gfx->begin3DFrameToCanvas(target);
        gfx->drawMesh(mesh, glm::mat4(1.f), albedo, Color(1.f));
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(target->newImageData());
        REQUIRE(image.get() != nullptr);
        writeParityArtifact(*image, artifact, backend);
        return image;
    };

    Lighting3DPack noLights{};
    noLights.ambient = glm::vec4(0.f);
    gfx->setMesh3DLighting(noLights);
    gfx->setMesh3DMaterial(1.f, 0.1f);
    gfx->setMesh3DEnv(nullptr, 0.f);
    auto environmentOff = render("pbr_environment_off");
    gfx->setMesh3DEnv(environment, 1.f);
    auto           environmentOn = render("pbr_environment_on");
    const uint8_t *reflected     = pixel(*environmentOn, 32, 32);
    REQUIRE(reflected[1] > reflected[0] + 20);
    REQUIRE(imageRgbDifference(*environmentOff, *environmentOn) > 20000u);

    Lighting3DPack sun{};
    sun.ambient             = glm::vec4(0.01f, 0.01f, 0.01f, 0.f);
    sun.count               = 1;
    sun.lights[0].posRadius = glm::vec4(0.f, 0.f, 1.f, 0.f);
    sun.lights[0].color     = glm::vec4(0.9f, 0.9f, 0.9f, 1.f);
    gfx->setMesh3DLighting(sun);
    gfx->setMesh3DMaterial(0.f, 0.8f);
    gfx->setMesh3DEnv(nullptr, 0.f);
    gfx->setCloudShadows(0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    auto cloudOff = render("pbr_cloud_shadow_off");
    gfx->setCloudShadows(1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    auto           cloudOn  = render("pbr_cloud_shadow_on");
    const uint8_t *lit      = pixel(*cloudOff, 32, 32);
    const uint8_t *shadowed = pixel(*cloudOn, 32, 32);
    REQUIRE(lit[0] > shadowed[0] + 80);
    REQUIRE(imageRgbDifference(*cloudOff, *cloudOn) > 100000u);
}

TEST_CASE("graphics.backendParity.maskedMaterialTechniques") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    const float positions[] = {
        -1.f, -1.f, 0.5f, 1.f, -1.f, 0.5f, 1.f, 1.f, 0.5f, -1.f, 1.f, 0.5f,
    };
    const float normals[] = {
        0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
    };
    const float    uvs[]       = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const uint32_t indices[]   = {0, 2, 1, 2, 0, 3};
    Mesh          *mesh        = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    const uint8_t  halfAlpha[] = {255, 255, 255, 128};
    Texture       *albedo      = gfx->newTexture(1, 1, halfAlpha);
    REQUIRE(mesh != nullptr);
    REQUIRE(albedo != nullptr);

    Lighting3DPack lighting{};
    lighting.ambient = glm::vec4(1.f, 1.f, 1.f, 0.f);
    gfx->setMesh3DLighting(lighting);
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    gfx->setMesh3DView(glm::mat4(1.f));
    gfx->setMesh3DCameraPos(glm::vec3(0.f, 0.f, 3.f));

    auto render = [&](float cutoff, const char *technique, const char *artifact) {
        gfx->setMesh3DSurface(SurfaceMode::Masked, BlendMode::Opaque, true, false, cutoff, technique);
        Canvas *target = gfx->newCanvas(64, 64);
        REQUIRE(target != nullptr);
        gfx->begin3DFrameToCanvas(target);
        gfx->drawMesh(mesh, glm::mat4(1.f), albedo, Color(1.f));
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(target->newImageData());
        REQUIRE(image.get() != nullptr);
        writeParityArtifact(*image, artifact, gfx->getBackendName());
        return image;
    };

    auto cutoffKept      = render(0.4f, "cutoff", "masked_cutoff_kept");
    auto cutoffDiscarded = render(0.6f, "cutoff", "masked_cutoff_discarded");
    REQUIRE(pixel(*cutoffKept, 32, 32)[0] > 180);
    REQUIRE(pixel(*cutoffDiscarded, 32, 32)[0] < 32);

    auto visiblePixels = [](const eve::image::ImageData &image) {
        int visible = 0;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                if (pixel(image, x, y)[0] > 180) ++visible;
        return visible;
    };
    const int fullyCovered = visiblePixels(*cutoffKept);
    REQUIRE(fullyCovered > 3500);

    for (const char *technique : {"dither", "coverage"}) {
        auto      dithered = render(0.5f, technique, technique);
        const int visible  = visiblePixels(*dithered);
        // Alpha hashing is backend-specific. Validate partial coverage relative to the
        // rendered quad instead of a Vulkan-tuned absolute pixel count. Near-full coverage
        // (visible ~= fullyCovered) means the hash path did not discard — historically seen
        // on Dawn Metal when the hash depended only on a degenerate fragment position.
        REQUIRE(visible * 5 > fullyCovered);
        REQUIRE(visible * 20 < fullyCovered * 17);
    }
}

TEST_CASE("graphics.backendParity.surfaceBlendDepthWriteAndCulling") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    const float positions[] = {
        -1.f, -1.f, 0.2f, 1.f, -1.f, 0.2f, 1.f, 1.f, 0.2f, -1.f, 1.f, 0.2f,
    };
    const float normals[] = {
        0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
    };
    const float    uvs[]          = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const uint32_t frontIndices[] = {0, 2, 1, 2, 0, 3};
    const uint32_t backIndices[]  = {0, 1, 2, 2, 3, 0};
    Mesh          *front          = gfx->newMeshFromArrays(positions, normals, uvs, 4, frontIndices, 6);
    Mesh          *back           = gfx->newMeshFromArrays(positions, normals, uvs, 4, backIndices, 6);
    const uint8_t  halfRed[]      = {255, 0, 0, 128};
    const uint8_t  green[]        = {0, 255, 0, 255};
    const uint8_t  blue[]         = {0, 0, 255, 255};
    const uint8_t  halfGray[]     = {128, 128, 128, 128};
    Texture       *redTexture     = gfx->newTexture(1, 1, halfRed);
    Texture       *greenTexture   = gfx->newTexture(1, 1, green);
    Texture       *blueTexture    = gfx->newTexture(1, 1, blue);
    Texture       *grayTexture    = gfx->newTexture(1, 1, halfGray);
    REQUIRE(front != nullptr);
    REQUIRE(back != nullptr);
    REQUIRE(redTexture != nullptr);
    REQUIRE(greenTexture != nullptr);
    REQUIRE(blueTexture != nullptr);
    REQUIRE(grayTexture != nullptr);

    Lighting3DPack lighting{};
    lighting.ambient = glm::vec4(1.f, 1.f, 1.f, 0.f);
    gfx->setMesh3DLighting(lighting);
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    gfx->setMesh3DView(glm::mat4(1.f));
    gfx->setMesh3DCameraPos(glm::vec3(0.f, 0.f, 3.f));

    auto renderCulling = [&](bool doubleSided, const char *artifact) {
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, doubleSided, 0.5f, "cutoff");
        Canvas *target = gfx->newCanvas(64, 64);
        gfx->begin3DFrameToCanvas(target);
        gfx->drawMesh(back, glm::mat4(1.f), greenTexture, Color(1.f));
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(target->newImageData());
        REQUIRE(image.get() != nullptr);
        writeParityArtifact(*image, artifact, gfx->getBackendName());
        return image;
    };
    auto culled   = renderCulling(false, "surface_backface_culled");
    auto twoSided = renderCulling(true, "surface_double_sided");
    REQUIRE(pixel(*culled, 32, 32)[1] < 32);
    REQUIRE(pixel(*twoSided, 32, 32)[1] > 180);

    auto renderDepthWrite = [&](bool depthWrite, const char *artifact) {
        Canvas *target = gfx->newCanvas(64, 64);
        gfx->begin3DFrameToCanvas(target);
        gfx->setMesh3DSurface(SurfaceMode::Transparent, BlendMode::Alpha, depthWrite, false, 0.5f, "cutoff");
        gfx->drawMesh(front, glm::mat4(1.f), redTexture, Color(1.f));
        glm::mat4 farther(1.f);
        farther[3][2] = 0.6f;
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, false, 0.5f, "cutoff");
        gfx->drawMesh(front, farther, greenTexture, Color(1.f));
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(target->newImageData());
        REQUIRE(image.get() != nullptr);
        writeParityArtifact(*image, artifact, gfx->getBackendName());
        return image;
    };
    auto           writesDepth = renderDepthWrite(true, "surface_transparent_depth_write");
    auto           skipsDepth  = renderDepthWrite(false, "surface_transparent_no_depth_write");
    const uint8_t *blocked     = pixel(*writesDepth, 32, 32);
    const uint8_t *overwritten = pixel(*skipsDepth, 32, 32);
    REQUIRE(blocked[0] > 80);
    REQUIRE(blocked[0] > blocked[1] + 60);
    REQUIRE(overwritten[1] > 180);
    REQUIRE(overwritten[0] < 20);

    auto renderBlend = [&](BlendMode blend, Texture *overlay, const char *artifact) {
        Canvas *target = gfx->newCanvas(64, 64);
        gfx->begin3DFrameToCanvas(target);
        glm::mat4 farther(1.f);
        farther[3][2] = 0.6f;
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, false, 0.5f, "cutoff");
        gfx->drawMesh(front, farther, blueTexture, Color(1.f));
        gfx->setMesh3DSurface(SurfaceMode::Transparent, blend, false, false, 0.5f, "cutoff");
        gfx->drawMesh(front, glm::mat4(1.f), overlay, Color(1.f));
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(target->newImageData());
        REQUIRE(image.get() != nullptr);
        writeParityArtifact(*image, artifact, gfx->getBackendName());
        return image;
    };
    auto           additive      = renderBlend(BlendMode::Additive, redTexture, "surface_additive");
    auto           premultiplied = renderBlend(BlendMode::Premultiplied, redTexture, "surface_premultiplied");
    auto           multiply      = renderBlend(BlendMode::Multiply, grayTexture, "surface_multiply");
    const uint8_t *added         = pixel(*additive, 32, 32);
    REQUIRE(added[0] > 80);
    REQUIRE(added[2] > 180);
    const uint8_t *premul = pixel(*premultiplied, 32, 32);
    REQUIRE(premul[0] > 180);
    REQUIRE(premul[2] > 80);
    const uint8_t *multiplied = pixel(*multiply, 32, 32);
    REQUIRE(multiplied[0] < 20);
    REQUIRE(multiplied[2] > 60);
    REQUIRE(multiplied[2] < 200);
}

TEST_CASE("graphics.backendParity.alphaMaskThresholdSoftnessAndInversion") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    const uint8_t red[]  = {255, 0, 0, 255};
    const uint8_t ramp[] = {
        0, 0, 0, 255, 128, 128, 128, 255, 255, 255, 255, 255,
    };
    Texture *color = gfx->newTexture(1, 1, red);
    Texture *mask  = gfx->newTexture(3, 1, ramp);
    REQUIRE(color != nullptr);
    REQUIRE(mask != nullptr);
    gfx->setTextureSampler(mask, TextureSampler::nearest());
    std::unique_ptr<AlphaMask> alphaMask(gfx->newAlphaMask());
    REQUIRE(alphaMask.get() != nullptr);
    alphaMask->setThreshold(0.5f);
    alphaMask->setSoftness(0.15f);

    auto render = [&](bool inverted, const char *artifact) {
        alphaMask->setInverted(inverted);
        Canvas *canvas = gfx->newCanvas(64, 64);
        gfx->setCanvas(canvas);
        gfx->clear(Color(0.f, 0.f, 1.f, 1.f), std::nullopt, std::nullopt);
        alphaMask->draw(color, mask, 2.f, 8.f, 60.f, 48.f);
        gfx->setCanvas();
        std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
        REQUIRE(image.get() != nullptr);
        writeParityArtifact(*image, artifact, gfx->getBackendName());
        return image;
    };

    auto           normal     = render(false, "alpha_mask_normal");
    const uint8_t *normalLow  = pixel(*normal, 10, 32);
    const uint8_t *normalMid  = pixel(*normal, 32, 32);
    const uint8_t *normalHigh = pixel(*normal, 54, 32);
    REQUIRE(normalLow[2] > 240);
    REQUIRE(normalLow[0] < 15);
    REQUIRE(normalMid[0] > 80);
    REQUIRE(normalMid[2] > 80);
    REQUIRE(normalHigh[0] > 240);
    REQUIRE(normalHigh[2] < 15);

    auto           inverted     = render(true, "alpha_mask_inverted");
    const uint8_t *invertedLow  = pixel(*inverted, 10, 32);
    const uint8_t *invertedHigh = pixel(*inverted, 54, 32);
    REQUIRE(invertedLow[0] > 240);
    REQUIRE(invertedLow[2] < 15);
    REQUIRE(invertedHigh[2] > 240);
    REQUIRE(invertedHigh[0] < 15);
}
}  // namespace test_graphics_backend_parity
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_display_output)
#include "PrivateTestScope.h"
#include <cmath>
#include "graphics/DisplayOutputEncoding.h"
#include "graphics/Graphics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_display_output.cpp
namespace test_graphics_display_output {
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


TEST_CASE("graphics.displayOutput.apiValidation") {
    auto *gfx = Graphics::create();
    REQUIRE(gfx != nullptr);
    if (!gfx->isHeadless()) gfx->initHeadless(64, 64);

    CHECK(gfx->setDisplayOutputMode(Graphics::DisplayOutputMode::Sdr).ok());
    CHECK(gfx->setDisplayOutputMode(Graphics::DisplayOutputMode::Auto).ok());
    CHECK_EQ(gfx->getDisplayOutputMode(), Graphics::DisplayOutputMode::Auto);

    // Vulkan accepts the preference even when the surface cannot present HDR.
    // WebGPU rejects strict HDR modes as Unsupported.
    const auto hdr10 = gfx->setDisplayOutputMode(Graphics::DisplayOutputMode::Hdr10);
    if (gfx->getBackendName() == "vulkan") {
        CHECK(hdr10.ok());
        CHECK_EQ(gfx->getDisplayOutputMode(), Graphics::DisplayOutputMode::Hdr10);
    } else {
        CHECK(!hdr10.ok());
        CHECK_EQ(gfx->getDisplayOutputMode(), Graphics::DisplayOutputMode::Auto);
    }
    // Headless has no swapchain, so active present stays SDR.
    CHECK(!gfx->isDisplayHdrActive());
    CHECK_EQ(gfx->getActiveDisplayColorSpace(), Graphics::DisplayColorSpace::Sdr);

    CHECK(gfx->setDisplayHdrCalibration(180.f, 800.f).ok());
    CHECK(std::abs(gfx->getDisplayPaperWhiteNits() - 180.f) < 1e-3f);
    CHECK(std::abs(gfx->getDisplayPeakNits() - 800.f) < 1e-3f);
    CHECK(!gfx->setDisplayHdrCalibration(NAN, 1000.f).ok());

    auto support = gfx->queryDisplayOutputSupport();
    // Headless has no present surface; windowed backends report at least SDR.
    if (gfx->isHeadless()) {
        CHECK(!support.ok());
    } else {
        REQUIRE(support.ok());
        CHECK(support.value().sdr);
    }
}
}  // namespace test_graphics_display_output
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_fog)
#include "PrivateTestScope.h"
#include <cmath>
#include <vector>
#include "GraphicsParitySupport.h"
#include "graphics/AtmosphereVolume.h"
#include "graphics/Volumetric.h"
#include "graphics/fog/AnalyticalVolLight.h"
#include "graphics/fog/ContinuousArt.h"
#include "graphics/fog/FogInteractor.h"
#include "graphics/fog/FogRayMarch.h"
#include "graphics/fog/FogSystem.h"
#include "graphics/fog/ProceduralDust.h"
#include "graphics/fog/RealtimeFog.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_fog.cpp
namespace test_graphics_fog {
// graphics_fog.cpp
using eve::graphics::AtmosphereVolume;
using eve::graphics::fog::AnalyticBeam;
using eve::graphics::fog::BeerLightCache;
using eve::graphics::fog::ContinuousArtParams;
using eve::graphics::fog::FogCflReport;
using eve::graphics::fog::FogDensityField;
using eve::graphics::fog::FogInteractor;
using eve::graphics::fog::FogProfile;
using eve::graphics::fog::FogProxyShape;
using eve::graphics::fog::FogQuality;
using eve::graphics::fog::FogRayMarch;
using eve::graphics::fog::FogRayResult;
using eve::graphics::fog::FogSolidProxy;
using eve::graphics::fog::FogSystem;
using eve::graphics::fog::FogVolumeBound;
using eve::graphics::fog::FogWorldBounds;
using eve::graphics::fog::ProceduralDust;
using eve::graphics::fog::RealtimeFog;
using eve::graphics::fog::SceneWind;

namespace {

[[maybe_unused]] FogWorldBounds unitBox() {
    FogWorldBounds b;
    b.minimum = {-4.f, 0.f, -4.f};
    b.maximum = {4.f, 4.f, 4.f};
    return b;
}

[[maybe_unused]] FogSystem makeSeeded(FogQuality quality = FogQuality::Enhanced) {
    FogSystem system;
    REQUIRE(system.setQuality(quality).ok());
    REQUIRE(system.configureDomain(8, 6, 8, unitBox()).ok());
    REQUIRE(system.seedHeightFog(1.2f, 0.f, 0.4f, 0.25f, 11u).ok());
    return system;
}

}  // namespace


// graphics_fog_volumetric.cpp
using eve::graphics::Volumetric;
using eve::graphics::fog::FogQuality;
using eve::graphics::fog::FogSystem;
using eve::graphics::fog::FogWorldBounds;
using eve::graphics::parity_test::headlessGraphics;

TEST_CASE("graphics_fog.syncToVolumetricInjectsMedia") {
    auto *graphics = headlessGraphics();
    REQUIRE(graphics != nullptr);

    FogSystem system;
    REQUIRE(system.setQuality(FogQuality::Fast).ok());
    FogWorldBounds bounds;
    bounds.minimum = {-4.f, 0.f, -4.f};
    bounds.maximum = {4.f, 4.f, 4.f};
    REQUIRE(system.configureDomain(8, 6, 8, bounds).ok());
    REQUIRE(system.seedHeightFog(1.0f, 0.f, 0.3f, 0.2f).ok());

    Volumetric volume(graphics);
    volume.setMode("froxel");
    volume.setCamera(0.f, 3.f, 8.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 55.f, 1.777f, 0.1f, 40.f);
    volume.configureFroxelGrid(16, 9, 16, 0.1f, 40.f);

    auto written = system.syncToVolumetric(&volume, glm::vec3(0.2f, 1.f, 0.1f), glm::vec3(1.f), 1.2f);
    REQUIRE(written.ok());
    CHECK(written.value() > 0);
    CHECK(volume.getAtmosphereVolume() != nullptr);
    CHECK(volume.getAtmosphereVolume()->getFroxelCount() > 0u);
    // upload must succeed after sync (atlas cols/rows follow configureFroxelGrid).
    volume.uploadFroxel(graphics);

    CHECK(!system.syncToVolumetric(nullptr, glm::vec3(0.f, 1.f, 0.f), glm::vec3(1.f), 1.f).ok());
}

TEST_CASE("graphics_fog.injectToVolumetricKeepsHeightFog") {
    auto *graphics = headlessGraphics();
    REQUIRE(graphics != nullptr);

    FogSystem system;
    REQUIRE(system.setQuality(FogQuality::Fast).ok());
    FogWorldBounds bounds;
    bounds.minimum = {-4.f, 0.f, -4.f};
    bounds.maximum = {4.f, 4.f, 4.f};
    REQUIRE(system.configureDomain(8, 6, 8, bounds).ok());
    REQUIRE(system.seedHeightFog(1.0f, 0.f, 0.3f, 0.2f).ok());

    Volumetric volume(graphics);
    volume.setMode("froxel");
    volume.setCamera(0.f, 3.f, 8.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 55.f, 1.777f, 0.1f, 40.f);
    volume.configureFroxelGrid(16, 9, 16, 0.1f, 40.f);
    volume.injectFroxelHeightFog(0.035f, 0.72f, 0.82f, 0.98f, 0.f, 0.16f, -2.f, 10.f);
    const float before = volume.getAtmosphereVolume()->at(8, 4, 8).extinction;
    CHECK(before > 0.f);

    auto written = system.injectToVolumetric(&volume);
    REQUIRE(written.ok());
    CHECK(volume.getAtmosphereVolume()->at(8, 4, 8).extinction >= before);
    volume.integrateFroxel(0.55f, 0.62f, 0.78f, 1.f);
    volume.uploadFroxel(graphics);

    CHECK(!system.injectToVolumetric(nullptr).ok());
}
}  // namespace test_graphics_fog
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_gbuffer_depth)
#include "PrivateTestScope.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include "Fixtures.h"
#include "graphics/ClipSpace.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_gbuffer_depth.cpp
namespace test_graphics_gbuffer_depth {
TEST_CASE("graphics.gbufferPlaneDepthMatchesCameraRay") {
    eve::graphics::Graphics *gfx = nullptr;
    openHeadlessGfx(gfx, 160, 120);
    const float    positions[] = {-30, 0, -20, 30, 0, -20, 30, 0, 10, -30, 0, 10};
    const float    normals[]   = {0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0};
    const float    uv[]        = {0, 0, 1, 0, 1, 1, 0, 1};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const glm::vec3 eye(0, 7, 16);
    const auto      view       = glm::lookAtRH(eye, glm::vec3(0), glm::vec3(0, 1, 0));
    const auto      projection = eve::graphics::perspectiveVulkanRH_ZO(glm::radians(55.f), 160.f / 120.f, 0.1f, 100.f);
    const auto      vp         = projection * view;
    gfx->beginGBufferPass(160, 120);
    gfx->drawMeshGBuffer(mesh, glm::mat4(1.f), vp, 0.1f, 100.f);
    gfx->endGBufferPass();
    std::unique_ptr<eve::image::ImageData> depth(gfx->readGBufferToImageData("depth"));
    REQUIRE(depth != nullptr);
    const auto inverse = glm::inverse(vp);
    // Ground depth depends on the camera ray, not the triangle diagonal or
    // perspective interpolation of vertex z/w. RGBA8 readback allows 2 LSB.
    for (int y : {60, 75, 90}) {
        for (int x : {45, 80, 115}) {
            auto        farPoint  = inverse * glm::vec4((x + 0.5f) / 80.f - 1.f, (y + 0.5f) / 60.f - 1.f, 1, 1);
            const auto  direction = glm::vec3(farPoint) / farPoint.w - eye;
            const auto  hit       = eye + direction * (-eye.y / direction.y);
            const float viewDepth = -(view * glm::vec4(hit, 1)).z;
            const float expected  = (viewDepth - 0.1f) / 99.9f;
            const float actual    = depth->getPixel(x, y).r;
            CHECK(std::fabs(actual - expected) <= 2.f / 255.f);
        }
    }
}
}  // namespace test_graphics_gbuffer_depth
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_headless)
#include "PrivateTestScope.h"
#include <functional>
#include <memory>
#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_headless.cpp
namespace test_graphics_headless {
using namespace eve::graphics;

namespace {

bool expectException(const std::function<void()> &fn) {
    try {
        fn();
    } catch (const eve::Exception &) {
        return true;
    }
    return false;
}

}  // namespace

// initHeadless is re-entrant (the shared Graphics singleton persists across
// test cases): a second call updates the viewport instead of throwing.
TEST_CASE("graphics.headless.initCanvasDrawAndErrors") {
    auto *gfx = Graphics::create();
    REQUIRE(gfx != nullptr);

    // Invalid sizes and double-init must throw.
    CHECK(expectException([&] { gfx->initHeadless(0, 240); }));
    CHECK(expectException([&] { gfx->initHeadless(320, -1); }));

    gfx->initHeadless(320, 240);
    CHECK(gfx->isHeadless());
    const std::string backend          = gfx->getBackendName();
    const bool        supportedBackend = backend == "vulkan" || backend == "webgpu";
    CHECK(supportedBackend);
    CHECK(gfx->getWidth() == 320);
    CHECK(gfx->getHeight() == 240);
    // Re-entrant initHeadless resizes the logical viewport.
    gfx->initHeadless(640, 480);
    CHECK(gfx->getWidth() == 640);
    CHECK(gfx->getHeight() == 480);
    gfx->initHeadless(320, 240);
    CHECK(gfx->getWidth() == 320);
    CHECK(gfx->getHeight() == 240);

    // Render a solid rect into an offscreen canvas and read it back.
    Canvas *rt = gfx->newCanvas(64, 64);
    REQUIRE(rt != nullptr);

    gfx->setCanvas(rt);
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    // RGBA-float overload matches the script-facing drawSolidRect name.
    gfx->drawSolidRect(8.f, 8.f, 32.f, 32.f, 1.f, 0.f, 0.f, 1.f);
    gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> img(rt->newImageData());
    REQUIRE(img.get() != nullptr);
    const int w = img->getWidth();
    const int h = img->getHeight();
    REQUIRE(w == 64);
    REQUIRE(h == 64);
    const auto *px = static_cast<const unsigned char *>(img->getData());

    int lit = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char *p = px + (static_cast<size_t>(y) * w + x) * 4;
            if (p[0] > 128 && p[1] < 64 && p[2] < 64) ++lit;
        }
    }
    CHECK(lit > 0);  // the red rectangle was rendered into the canvas

    // Background stays the clear color outside the rect.
    const unsigned char *corner = px + (size_t(1) * w + 1) * 4;
    CHECK(corner[0] < 32);
    CHECK(corner[1] < 32);
    CHECK(corner[2] < 32);

    // present() must be a no-op (no swapchain), not a crash; drawing after
    // present also stays valid.
    gfx->present();
    const uint8_t white[4] = {255, 255, 255, 255};
    Texture      *tex      = gfx->newTexture(1, 1, white);
    REQUIRE(tex != nullptr);
    // RGBA-float overload matches the script-facing drawTexturedRect name.
    gfx->drawTexturedRect(tex, 0.f, 0.f, 16.f, 16.f, 1.f, 1.f, 1.f, 1.f);
    gfx->present();
    gfx->clearScreen();
}
}  // namespace test_graphics_headless
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_map_fog)
#include "PrivateTestScope.h"
#include <cstdint>
#include <memory>
#include <vector>
#include "GraphicsParitySupport.h"
#include "graphics/Canvas.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "graphics/MapFog.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "zeroerr/unittest.h"
// graphics_map_fog.cpp
namespace test_graphics_map_fog {
using eve::graphics::Color;
using eve::graphics::Graphics;
using eve::graphics::MapFog;
using eve::graphics::Texture;
using eve::graphics::parity_test::headlessGraphics;

namespace {

const uint8_t *pixel(eve::image::ImageData &img, int x, int y) {
    return static_cast<const uint8_t *>(img.getData()) + size_t((y * img.getWidth() + x) * 4);
}

}  // namespace

TEST_CASE("graphics.mapFog.unlockedCellsStayClear") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);

    // 2x1 mask: left fogged (R=0), right unlocked (R=1).
    const std::vector<uint8_t> maskRgba = {
        0,   0, 0, 255,  // fogged
        255, 0, 0, 255,  // unlocked
    };
    Texture *mask = gfx->newTexture(2, 1, maskRgba.data());
    REQUIRE(mask != nullptr);

    std::unique_ptr<MapFog> fog(gfx->newMapFog());
    REQUIRE(fog.get() != nullptr);
    fog->setMaskTexture(mask);
    fog->setCloudTexture(fog->makeCloudTexture(64));
    fog->setTime(0.25f);
    fog->setShadowEnabled(false);
    fog->setFogAlpha(1.f);
    fog->setFogColor(1.f, 1.f, 1.f);
    fog->setDistort(0.f);
    fog->setEdgeSoftness(0.05f);

    auto *canvas = gfx->newCanvas(64, 32);
    REQUIRE(canvas != nullptr);
    gfx->setCanvas(canvas);
    gfx->clear(Color(0.f, 0.f, 1.f, 1.f), std::nullopt, std::nullopt);
    fog->draw(0.f, 0.f, 64.f, 32.f);
    gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image.get() != nullptr);

    const uint8_t *left  = pixel(*image, 8, 16);
    const uint8_t *right = pixel(*image, 56, 16);

    // Fogged side should no longer be pure background blue.
    CHECK(int(left[2]) < 200);
    // Unlocked side should remain close to the clear blue background.
    CHECK(int(right[2]) > 200);
    CHECK(int(right[0]) < 40);
}

TEST_CASE("graphics.mapFog.makeCloudTextureIsRepeatable") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    std::unique_ptr<MapFog> fog(gfx->newMapFog());
    REQUIRE(fog.get() != nullptr);
    Texture *cloud = fog->makeCloudTexture(32);
    REQUIRE(cloud != nullptr);
    CHECK(cloud->getWidth() == 32);
    CHECK(cloud->getHeight() == 32);
    fog->setCloudTiling(2.f, 3.f);
    fog->setCloudSpeed(0.01f, 0.02f);
    CHECK(fog->getCloudTileA() == 2.f);
    CHECK(fog->getCloudTileB() == 3.f);
}
}  // namespace test_graphics_map_fog
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
namespace test_graphics_pbr_surface {
using namespace eve::graphics;
TEST_CASE("graphics.pbrSurface.atomicValidationAndAbsentBackend") {
    Material   material;
    PbrSurface surface;
    surface.specularFactor = .25f;
    REQUIRE(material.setPbrSurface(surface).ok());
    material.setShadingModel("unlit");
    REQUIRE(!material.getReceiveLight());
    material.setShadingModel("pbr");
    REQUIRE(material.getReceiveLight());
    surface.specularFactor = 2;
    REQUIRE(!material.setPbrSurface(surface).ok());
    REQUIRE_EQ(material.pbrSurface().specularFactor, .25f);
    surface.specularFactor       = .5f;
    surface.textures[0].rotation = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!validatePbrSurface(surface).ok());
    auto *gfx = Graphics::create();
    REQUIRE(gfx->Graphics::setMesh3DPbrSurface(nullptr).ok());
    surface = PbrSurface{};
    REQUIRE(!gfx->Graphics::setMesh3DPbrSurface(&surface).ok());
}


TEST_CASE("graphics.pbrSurface.extensionFactorsAndUvChangePixels") {
    auto *gfx = Graphics::create();
    REQUIRE(gfx != nullptr);
    gfx->initHeadless(128, 128);
    auto *canvas = gfx->newCanvas(128, 128);
    REQUIRE(canvas != nullptr);
    const float    positions[] = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]        = {.1f, .1f, .1f, .1f, .1f, .1f, .1f, .1f};
    const float    uv1[]       = {.9f, .1f, .9f, .1f, .9f, .1f, .9f, .1f};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto          *mesh        = gfx->newMeshFromArrays(positions, normals, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->setTexcoordSet(1, uv1).ok());
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->setMesh3DCameraPos({-.4f, .3f, 2});
    Lighting3DPack lighting{};
    lighting.count               = 1;
    lighting.ambient             = {.15f, .15f, .15f, 0};
    lighting.lights[0].posRadius = {.8f, .1f, .4f, 0};
    lighting.lights[0].color     = {2, 2, 2, 0};
    auto render                  = [&](const PbrSurface &s) {
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DLighting(lighting);
        gfx->setMesh3DMaterial(.8f, .75f);
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
        REQUIRE(gfx->setMesh3DPbrSurface(&s).ok());
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(.8f, .6f, .4f, 1));
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
        REQUIRE(pixels != nullptr);
        const auto *data = static_cast<const unsigned char *>(pixels->getData());
        return std::vector<unsigned char>(data, data + 128 * 128 * 4);
    };
    auto difference = [](const auto &a, const auto &b) {
        size_t changed = 0;
        for (size_t i = 0; i < a.size(); i += 4)
            if (std::abs(int(a[i]) - int(b[i])) + std::abs(int(a[i + 1]) - int(b[i + 1])) +
                    std::abs(int(a[i + 2]) - int(b[i + 2])) >
                0)
                ++changed;
        return changed;
    };
    PbrSurface base;
    base.vegetationColor.backfaceNormalMode = PbrVegetationBackfaceNormalMode::Same;
    const auto reference                    = render(base);
    for (int extension = 0; extension < 6; extension++) {
        auto s = base;
        if (extension == 0) s.specularFactor = 0;
        if (extension == 1) {
            s.anisotropyStrength = .6f;
            s.anisotropyRotation = .6f;
        }
        if (extension == 2) {
            s.clearcoatFactor    = 1;
            s.clearcoatRoughness = .2f;
        }
        if (extension == 3) {
            s.emissive         = {.15f, .02f, .01f};
            s.emissiveStrength = 3;
        }
        if (extension == 4) s.ior = 2.5f;
        if (extension == 5) s.unlit = true;
        auto pixels = render(s);
        std::fprintf(stderr, "PBR extension %d changed %zu pixels\n", extension, difference(reference, pixels));
        REQUIRE(difference(reference, pixels) > 100);
    }
    const uint8_t colors[] = {255, 0, 0, 255, 0, 255, 0, 255};
    auto         *texture  = gfx->newTexture(2, 1, colors);
    REQUIRE(texture != nullptr);
    auto s                  = base;
    s.unlit                 = true;
    s.textures[0].texture   = texture;
    s.textures[0].minFilter = 9728;
    s.textures[0].magFilter = 9728;
    // Begin with the vertex UV0 path so the first UV1 draw also exercises buffer growth.
    auto red               = render(s);
    s.textures[0].texcoord = 1;
    const auto firstGreen  = render(s);
    s.textures[0].texcoord = 0;
    REQUIRE_EQ(difference(red, render(s)), size_t(0));
    s.textures[0].texcoord = 1;
    auto         green     = render(s);
    const size_t center    = (64 * 128 + 64) * 4;
    std::fprintf(stderr, "PBR UV0 RGB=%u,%u,%u UV1 RGB=%u,%u,%u changed=%zu\n", unsigned(red[center]),
                 unsigned(red[center + 1]), unsigned(red[center + 2]), unsigned(green[center]),
                 unsigned(green[center + 1]), unsigned(green[center + 2]), difference(red, green));

    s.textures[0].texcoord = 0;
    s.textures[0].offset   = {.8f, 0};
    auto transformed       = render(s);
    std::fprintf(stderr, "PBR first UV1 RGB=%u,%u,%u transformed UV0 RGB=%u,%u,%u\n", unsigned(firstGreen[center]),
                 unsigned(firstGreen[center + 1]), unsigned(firstGreen[center + 2]), unsigned(transformed[center]),
                 unsigned(transformed[center + 1]), unsigned(transformed[center + 2]));
    REQUIRE(difference(red, green) > 1000);
    REQUIRE_EQ(difference(firstGreen, green), size_t(0));
    REQUIRE_EQ(difference(green, transformed), size_t(0));
    // Every frame owns its parameter and descriptor snapshot; repeated changes must stay deterministic.
    s.textures[0].offset = {0, 0};
    REQUIRE_EQ(difference(red, render(s)), size_t(0));
    REQUIRE_EQ(difference(reference, render(base)), size_t(0));
    // Channel semantics are observable: each texture role is compared against an absent binding.
    const uint8_t sample[] = {30, 190, 90, 80};
    auto         *map      = gfx->newTexture(1, 1, sample);
    REQUIRE(map != nullptr);
    for (size_t role = 0; role < 11; role++) {
        auto plain                   = base;
        plain.emissive               = {.2f, .2f, .2f};
        plain.clearcoatFactor        = 1;
        plain.clearcoatRoughness     = .5f;
        plain.anisotropyStrength     = .8f;
        const auto without           = render(plain);
        plain.textures[role].texture = map;
        auto with                    = render(plain);
        std::fprintf(stderr, "PBR texture %zu changed %zu pixels\n", role, difference(without, with));
        REQUIRE(difference(without, with) > 100);
    }
    // Deferred backends must snapshot the scalar material state for each draw.
    // Changing the second draw must not retroactively shade the first one.
    auto *isolatedCanvas = gfx->newCanvas(128, 128);
    auto *pairedCanvas   = gfx->newCanvas(128, 128);
    REQUIRE(isolatedCanvas != nullptr);
    REQUIRE(pairedCanvas != nullptr);
    glm::mat4 leftModel(.45f);
    leftModel[3] = {-0.5f, 0.f, 0.f, 1.f};
    glm::mat4 rightModel(.45f);
    rightModel[3] = {0.5f, 0.f, 0.f, 1.f};
    auto drawPair = [&](Canvas *target, bool includeRight) {
        gfx->begin3DFrameToCanvas(target);
        gfx->setMesh3DLighting(lighting);
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
        REQUIRE(gfx->setMesh3DPbrSurface(&base).ok());
        gfx->setMesh3DMaterial(0.f, 1.f);
        gfx->drawMesh(mesh, leftModel, nullptr, Color(.8f, .6f, .4f, 1));
        if (includeRight) {
            auto alteredLighting            = lighting;
            alteredLighting.ambient         = {.8f, .1f, .1f, 0.f};
            alteredLighting.lights[0].color = {.1f, 2.f, .1f, 0.f};
            gfx->setMesh3DLighting(alteredLighting);
            gfx->setMesh3DCameraPos({.8f, -.6f, 1.2f});
            gfx->setMesh3DMaterial(1.f, .1f);
            gfx->drawMesh(mesh, rightModel, nullptr, Color(.8f, .6f, .4f, 1));
        }
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(target->newImageData());
        REQUIRE(image != nullptr);
        const auto *bytes = static_cast<const unsigned char *>(image->getData());
        return std::vector<unsigned char>(bytes, bytes + 128 * 128 * 4);
    };
    const auto   isolated   = drawPair(isolatedCanvas, false);
    const auto   paired     = drawPair(pairedCanvas, true);
    const size_t leftCenter = (64 * 128 + 32) * 4;
    for (size_t channel = 0; channel < 3; ++channel)
        REQUIRE_EQ(isolated[leftCenter + channel], paired[leftCenter + channel]);
    gfx->setMesh3DLighting(lighting);
    gfx->setMesh3DCameraPos({-.4f, .3f, 2});
    const uint16_t joints[]  = {203, 0, 0, 0, 203, 0, 0, 0, 203, 0, 0, 0, 203, 0, 0, 0};
    const float    weights[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    REQUIRE(gfx->setMeshSkinningData(mesh, joints, weights, 4));
    std::vector<float> palette(204 * 16, 0);
    for (int j = 0; j < 204; j++)
        for (int k : {0, 5, 10, 15}) palette[j * 16 + k] = 1;
    palette[203 * 16 + 12] = .3f;
    REQUIRE(mesh->setSkinPalette(palette.data(), 204).ok());
    REQUIRE(difference(reference, render(base)) > 100);
}
}  // namespace test_graphics_pbr_surface
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_raytracing_alignment)
#include "PrivateTestScope.h"
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <vector>
#include "common/Capability.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/IRayTracing.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_raytracing_alignment.cpp
namespace test_graphics_raytracing_alignment {
TEST_CASE("graphics.raytracing.reflectionHitVisibility") {
    using namespace eve::graphics;
    REQUIRE(eve::image::Image::create() != nullptr);
    auto *gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    auto *rt = eve::cap::query<IRayTracing>();
    if (!rt || !rt->isAvailable()) {
        std::fprintf(stderr, "RT pixel probe: hardware/provider unavailable; no GPU output assertion executed\n");
        return;
    }

    // A tilted mirror reflects an off-center sample towards a translated vertical
    // triangle. Identity projection makes projected hit coordinates observable.
    const float    positions[] = {0.f, -2.f, -2.f, 0.f, 2.f, -2.f, 0.f, 0.f, 2.f};
    const uint32_t indices[]   = {0, 1, 2};
    rt->clearScene();
    auto added =
        rt->addTriangleMesh(positions, 3, indices, 3, glm::translate(glm::mat4(1.f), glm::vec3(0.5f, 0.f, 0.f)));
    REQUIRE(added.ok());
    auto built = rt->rebuildScene();
    REQUIRE(built.ok());

    std::vector<uint8_t> scene(64 * 64 * 4, 255);
    std::vector<uint8_t> normals(64 * 64 * 4, 255);
    for (int i = 0; i < 64 * 64; ++i) {
        scene[i * 4] = scene[i * 4 + 1] = 0;
        normals[i * 4] = normals[i * 4 + 2] = 218;
        normals[i * 4 + 1]                  = 128;
        normals[i * 4 + 3]                  = 56;  // smooth metal: packed G-buffer PBR
    }
    for (int y = 16; y <= 20; ++y)
        for (int x = 46; x <= 50; ++x) {
            scene[(y * 64 + x) * 4]     = 255;
            scene[(y * 64 + x) * 4 + 2] = 0;
        }
    auto *color = gfx->newTexture(64, 64, scene.data());
    auto *dest  = gfx->newHDRCanvas(64, 64);
    for (int variant : {0, 1, 2}) {
        const bool occluded   = variant == 1;
        const bool dielectric = variant == 2;
        if (dielectric)
            for (int i = 0; i < 64 * 64; ++i) normals[i * 4 + 3] = 0;
        auto                *normal = gfx->newTexture(64, 64, normals.data());
        std::vector<uint8_t> depths(64 * 64 * 4, 255);
        depths[(18 * 64 + 21) * 4] = 64;
        for (int y = 16; y <= 20; ++y)
            for (int x = 46; x <= 50; ++x) depths[(y * 64 + x) * 4] = occluded ? 26 : 64;
        auto       *depth   = gfx->newTexture(64, 64, depths.data());
        const float centerX = 21.5f / 64.f * 2.f - 1.f;
        const float centerY = 18.5f / 64.f * 2.f - 1.f;
        auto        applied = rt->applyReflections(gfx, color, depth, normal, dest, glm::mat4(1.f), glm::mat4(1.f),
                                                   glm::vec3(centerX, centerY, 1.f));
        REQUIRE(applied.ok());
        REQUIRE(applied.code() != eve::StatusCode::NoOp);
        std::unique_ptr<eve::image::ImageData> image(dest->newHDRImageData());
        REQUIRE(image != nullptr);
        const auto pixel = image->getPixel(21, 18);
        if (occluded) {
            REQUIRE(pixel.a < 0.1f);
        } else if (dielectric) {
            REQUIRE(pixel.a > 0.02f);
            REQUIRE(pixel.a < 0.06f);
            REQUIRE(std::abs(pixel.r - pixel.a) < 0.002f);
        } else {
            REQUIRE(pixel.a > 0.9f);
            REQUIRE(pixel.r > 0.9f);
            REQUIRE(pixel.g < 0.1f);
        }
    }
    rt->clearScene();
}
}  // namespace test_graphics_raytracing_alignment
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_skin_palette_render)
#include "PrivateTestScope.h"
#include <array>
#include <memory>
#include <vector>
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// graphics_skin_palette_render.cpp
namespace test_graphics_skin_palette_render {
TEST_CASE("graphics.skinPalette.largeJointMovesRenderedPixels") {
    using namespace eve::graphics;
    auto *gfx = Graphics::create();
    REQUIRE(gfx != nullptr);
    gfx->initHeadless(128, 128);
    Canvas *canvas = gfx->newCanvas(128, 128);
    REQUIRE(canvas != nullptr);
    const float    positions[] = {-0.2f, -0.2f, 0.5f, 0.2f, -0.2f, 0.5f, 0.f, 0.2f, 0.5f};
    const uint32_t indices[]   = {0, 1, 2, 2, 1, 0};
    auto          *mesh        = gfx->newMeshFromArrays(positions, nullptr, nullptr, 3, indices, 6);
    REQUIRE(mesh != nullptr);
    const float weights[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    std::vector<unsigned char> first;
    for (int count : {129, 204, 1024, 4096, 204}) {
        const uint16_t last     = static_cast<uint16_t>(count - 1);
        const uint16_t joints[] = {last, 0, 0, 0, last, 0, 0, 0, last, 0, 0, 0};
        REQUIRE(gfx->setMeshSkinningData(mesh, joints, weights, 3));
        std::vector<float> palette(size_t(count) * 16, 0.f);
        for (int i = 0; i < count; ++i)
            for (int j : {0, 5, 10, 15}) palette[size_t(i) * 16 + j] = 1.f;
        for (float translation : {-0.5f, 0.5f}) {
            palette[size_t(count - 1) * 16 + 12] = translation;
            REQUIRE(mesh->setSkinPalette(palette.data(), count).ok());
            gfx->begin3DFrameToCanvas(canvas);
            gfx->drawMesh(mesh, glm::mat4(1.f), nullptr, Color(1.f, 0.2f, 0.1f, 1.f));
            gfx->end3DFrameToCanvas();
            std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
            REQUIRE(pixels != nullptr);
            const auto  *data = static_cast<const unsigned char *>(pixels->getData());
            const size_t size = 128u * 128u * 4u;
            if (translation < 0)
                first.assign(data, data + size);
            else {
                size_t changed = 0;
                for (size_t i = 0; i < size; i += 4)
                    if (data[i] != first[i] || data[i + 1] != first[i + 1] || data[i + 2] != first[i + 2]) ++changed;
                REQUIRE(changed > 200);
            }
        }
    }
}

TEST_CASE("graphics.skinPalette.influenceLimitChangesRenderedDeformation") {
    using namespace eve::graphics;
    auto *gfx = Graphics::create();
    REQUIRE(gfx != nullptr);
    gfx->initHeadless(128, 128);
    Canvas *canvas = gfx->newCanvas(128, 128);
    REQUIRE(canvas != nullptr);
    const float    positions[] = {-0.15f, -0.15f, 0.5f, 0.15f, -0.15f, 0.5f, 0.f, 0.15f, 0.5f};
    const uint32_t indices[]   = {0, 1, 2, 2, 1, 0};
    Mesh          *mesh        = gfx->newMeshFromArrays(positions, nullptr, nullptr, 3, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint16_t joints[]  = {0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0};
    const float    weights[] = {.75f, .25f, 0.f, 0.f, .75f, .25f, 0.f, 0.f, .75f, .25f, 0.f, 0.f};
    REQUIRE(gfx->setMeshSkinningData(mesh, joints, weights, 3));
    std::array<float, 32> palette{};
    for (int bone = 0; bone < 2; ++bone)
        for (int diagonal : {0, 5, 10, 15}) palette[static_cast<size_t>(bone) * 16 + diagonal] = 1.f;
    palette[12]      = -.45f;
    palette[16 + 12] = .45f;
    REQUIRE(mesh->setSkinPalette(palette.data(), 2).ok());
    gfx->setMesh3DViewProj(glm::mat4(1.f));

    std::vector<unsigned char> oneInfluence;
    for (SkinInfluenceLimit limit : {SkinInfluenceLimit::One, SkinInfluenceLimit::Two}) {
        gfx->setMesh3DSkinInfluenceLimit(limit);
        gfx->begin3DFrameToCanvas(canvas);
        gfx->drawMesh(mesh, glm::mat4(1.f), nullptr, Color(1.f, .2f, .1f, 1.f));
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
        REQUIRE(pixels != nullptr);
        const auto *data = static_cast<const unsigned char *>(pixels->getData());
        if (limit == SkinInfluenceLimit::One) {
            oneInfluence.assign(data, data + 128u * 128u * 4u);
        } else {
            size_t changed = 0;
            for (size_t i = 0; i < oneInfluence.size(); i += 4)
                if (data[i] != oneInfluence[i] || data[i + 1] != oneInfluence[i + 1] ||
                    data[i + 2] != oneInfluence[i + 2])
                    ++changed;
            REQUIRE(changed > 150);
        }
    }
}
}  // namespace test_graphics_skin_palette_render
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_graphics_texture_mips)
#include "PrivateTestScope.h"
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
// graphics_texture_mips.cpp
namespace test_graphics_texture_mips {
TEST_CASE("graphics.texture.explicit_mips_preserve_every_uploaded_level") {
    using namespace eve;
    using namespace eve::graphics;
    auto *gfx = Graphics::create();
    gfx->initHeadless(32, 32);
    std::cout << "Explicit mip contract backend: " << gfx->getBackendName() << '\n';
    auto *canvas = gfx->newCanvas(32, 32);
    REQUIRE(canvas != nullptr);
    asset_graphics::GraphicsImageFactoryAdapter images(*gfx);
    // Rectangular, non-power-of-two input also exercises the one-texel tail.
    const std::array<std::array<uint8_t, 4>, 3> colors{{{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}}};
    std::vector<uint8_t>                        bytes;
    unsigned                                    w = 7, h = 3;
    for (const auto &color : colors) {
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
        {reinterpret_cast<const uint8_t *>(definition.data()), definition.size()}, bytes, 4096);
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
    auto *texture = uploaded.value().texture;
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
    auto *gfx = Graphics::create();
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
    auto *gfx = Graphics::create();
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
    auto *gfx = Graphics::create();
    gfx->initHeadless(16, 16);
    const std::array<uint8_t, 4>   rgba{255, 255, 255, 255};
    const std::array<uint16_t, 16> half{0x3c00u, 0x3c00u, 0x3c00u, 0x3c00u, 0x3c00u, 0x3c00u, 0x3c00u, 0x3c00u,
                                        0x3c00u, 0x3c00u, 0x3c00u, 0x3c00u, 0x3c00u, 0x3c00u, 0x3c00u, 0x3c00u};
    auto                          *texture2d = gfx->newTexture(1, 1, rgba.data());
    auto                           array     = gfx->newTextureArrayRgba16f(1, 1, 4, half);
    auto                           volume    = gfx->newTexture3DRgba8(1, 1, 1, rgba);
    REQUIRE(texture2d != nullptr);
    REQUIRE(array.ok());
    REQUIRE(volume.ok());

    PbrSurface surface;
    surface.textures[0].texture = array.value();
    REQUIRE(!gfx->setMesh3DPbrSurface(&surface).ok());
    surface.textures[0].texture      = nullptr;
    surface.vegetationExtras.texture = texture2d;
    REQUIRE(!gfx->setMesh3DPbrSurface(&surface).ok());
    surface.vegetationExtras.texture = array.value();
    surface.vegetationExtras.layer   = 3;
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    surface.vegetationExtras.layer = 4;
    REQUIRE(!gfx->setMesh3DPbrSurface(&surface).ok());
    surface.vegetationExtras      = {};
    surface.vegetationAlpha.noise = texture2d;
    REQUIRE(!gfx->setMesh3DPbrSurface(&surface).ok());
    surface.vegetationAlpha.noise = volume.value();
    REQUIRE(gfx->setMesh3DPbrSurface(&surface).ok());
    surface.vegetationAlpha.noise  = nullptr;
    surface.vegetationMotion.noise = volume.value();
    REQUIRE(!gfx->setMesh3DPbrSurface(&surface).ok());
    REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
    REQUIRE(gfx->releaseTexture(volume.value()));
    REQUIRE(gfx->releaseTexture(array.value()));
    REQUIRE(gfx->releaseTexture(texture2d));
}
}  // namespace test_graphics_texture_mips
#endif
