// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"graphics","default":"render-offscreen"}
#include "common/config.h"

#ifdef EVENGINE_WEBGPU

#include <cstdint>
#include <iostream>

#include "GraphicsParitySupport.h"
#include "graphics/PbrSurface.h"
#include "graphics/RenderControl.h"
#include "graphics/webgpu/Graphics.h"
#include "graphics/webgpu/InitFlow.h"
#include "graphics/webgpu/PbrVariantPlan.h"
#include "graphics/webgpu/PbrVariantSource.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::graphics;
using namespace eve::graphics::webgpu;

namespace {
Texture* marker(std::uintptr_t value) { return reinterpret_cast<Texture*>(value); }

}  // namespace

TEST_CASE("graphics.webgpu.baselineLimitUsesForwardPasses") {
    webgpu::Graphics gfx;
    CHECK(!gfx.supportsGBufferPost());
    CHECK(!gfx.supportsDeferredLighting());
    CHECK(!gfx.supportsGpuDriven3D());

    RenderControl control;
    control.attach(&gfx);
    CHECK(!control.isEnabled("gbuffer"));
    CHECK(!control.isEnabled("ao"));
    control.compile();
    CHECK(!control.hasPass("gbuffer"));
    CHECK(!control.hasPass("decal"));
    CHECK(control.hasPass("forward"));
    auto mode = control.setLightingMode(LightingMode::Hybrid);
    REQUIRE(mode.ok());
    control.compile();
    CHECK(!control.hasPass("gbuffer"));
    CHECK(!control.hasPass("deferredLighting"));
    CHECK(control.hasPass("forward"));
    auto unsupported = control.requestFeature("gpuDriven", true);
    CHECK(!unsupported.ok());
    CHECK(!control.isEnabled("gpuDriven"));
    auto disabled = control.requestFeature("gpuDriven", false);
    CHECK(disabled.ok());
    auto forward = control.requestFeature("forward", true);
    CHECK(forward.ok());
}

TEST_CASE("graphics.webgpu.baselineColorBudgetCreatesDevice") {
    auto instance = InitFlow::createInstance();
    auto adapter  = InitFlow::requestAdapter(std::move(instance), {});
    auto device   = InitFlow::requestDevice(std::move(adapter), 32);
    REQUIRE(bool(device.device));
    wgpu::Limits granted{};
    REQUIRE(device.device.GetLimits(&granted) == wgpu::Status::Success);
    CHECK_EQ(granted.maxColorAttachmentBytesPerSample, 32u);
    CHECK(!device.caps.supportsFiveTargetGBuffer());
}

TEST_CASE("graphics.webgpu.baselineColorBudgetRendersForward") {
    auto             instance = InitFlow::createInstance();
    auto             adapter  = InitFlow::requestAdapter(std::move(instance), {});
    auto             device   = InitFlow::requestDevice(std::move(adapter), 32);
    webgpu::Graphics gfx;
    gfx.initHeadlessFromDevice(64, 64, std::move(device));
    REQUIRE(!gfx.supportsGBufferPost());
    auto* control = gfx.getRenderControl();
    REQUIRE(control != nullptr);
    CHECK(!control->hasPass("gbuffer"));
    CHECK(control->hasPass("forward"));

    Canvas* canvas2d = gfx.newCanvas(64, 64);
    REQUIRE(canvas2d != nullptr);
    gfx.setCanvas(canvas2d);
    gfx.clear(Color(0.f, 0.f, 1.f, 1.f), std::nullopt, std::nullopt);
    gfx.drawSolidRect(16.f, 16.f, 32.f, 32.f, Color(1.f, 0.f, 0.f, 1.f));
    gfx.setCanvas(nullptr);
    std::unique_ptr<eve::image::ImageData> image2d(canvas2d->newImageData());
    REQUIRE(image2d != nullptr);
    CHECK(eve::graphics::parity_test::pixel(*image2d, 32, 32)[0] > 240);

    const float    positions[] = {-1.f, -1.f, 0.5f, 1.f, -1.f, 0.5f, 1.f, 1.f, 0.5f, -1.f, 1.f, 0.5f};
    const float    normals[]   = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
    const float    uvs[]       = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const uint32_t indices[]   = {0, 2, 1, 2, 0, 3};
    Mesh*          mesh        = gfx.newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t red[]  = {255, 0, 0, 255};
    Texture*      albedo = gfx.newTexture(1, 1, red);
    REQUIRE(albedo != nullptr);
    Lighting3DPack lighting{};
    lighting.ambient = glm::vec4(1.f, 1.f, 1.f, 0.f);
    gfx.setMesh3DLighting(lighting);
    gfx.setMesh3DViewProj(glm::mat4(1.f));
    gfx.setMesh3DView(glm::mat4(1.f));
    gfx.setMesh3DCameraPos(glm::vec3(0.f, 0.f, 3.f));
    gfx.setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, 0.5f, "cutoff");
    Canvas* canvas3d = gfx.newCanvas(64, 64);
    REQUIRE(canvas3d != nullptr);
    gfx.begin3DFrameToCanvas(canvas3d);
    gfx.drawMesh(mesh, glm::mat4(1.f), albedo, Color(1.f));
    gfx.end3DFrameToCanvas();
    std::unique_ptr<eve::image::ImageData> image3d(canvas3d->newImageData());
    REQUIRE(image3d != nullptr);
    CHECK(eve::graphics::parity_test::pixel(*image3d, 32, 32)[0] > 180);
}

TEST_CASE("graphics.webgpu.invalidWgslReplacementKeepsLastGoodPipeline") {
    webgpu::Graphics gfx;
    gfx.initHeadless(64, 64);
    const std::string validFragment = R"wgsl(
struct FSIn { @location(0) color: vec4f, @location(1) uv: vec2f, };
@group(0) @binding(0) var mainTex: texture_2d<f32>;
@group(0) @binding(2) var mainSamp: sampler;
@fragment fn fs_main(in: FSIn) -> @location(0) vec4f {
    return textureSample(mainTex, mainSamp, in.uv) * in.color;
}

)wgsl";
    Shader*           shader        = gfx.newShaderFromWgsl({}, validFragment);
    REQUIRE(shader != nullptr);
    void*       originalHandle = shader->gpuHandle;
    std::string greenFragment  = validFragment;
    const auto  sample         = greenFragment.find("textureSample(mainTex, mainSamp, in.uv)");
    REQUIRE(sample != std::string::npos);
    greenFragment.replace(sample, std::string("textureSample(mainTex, mainSamp, in.uv)").size(),
                          "vec4f(0.0, 1.0, 0.0, 1.0)");
    auto validReplacement = gfx.replaceShaderFromWgsl(*shader, {}, greenFragment);
    REQUIRE(validReplacement.ok());
    CHECK(shader->gpuHandle == originalHandle);
    auto replaced =
        gfx.replaceShaderFromWgsl(*shader, {}, "@fragment fn fs_main() -> @location(0) vec4f { invalid_wgsl }");
    REQUIRE(!replaced.ok());
    CHECK(shader->gpuHandle == originalHandle);

    const uint8_t red[]   = {255, 0, 0, 255};
    Texture*      texture = gfx.newTexture(1, 1, red);
    Canvas*       canvas  = gfx.newCanvas(64, 64);
    REQUIRE(texture != nullptr);
    REQUIRE(canvas != nullptr);
    gfx.setCanvas(canvas);
    gfx.drawTexturedRectShader(texture, shader, 8.f, 8.f, 48.f, 48.f, Color(1.f));
    gfx.setCanvas(nullptr);
    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image != nullptr);
    const auto* center = eve::graphics::parity_test::pixel(*image, 32, 32);
    CHECK(center[0] < 8);
    CHECK(center[1] > 240);
}

TEST_CASE("graphics.webgpu.invalidWgslCreationDoesNotPublishShader") {
    webgpu::Graphics gfx;
    gfx.initHeadless(64, 64);
    const std::string invalid  = "@fragment fn fs_main() -> @location(0) vec4f { invalid_wgsl }";
    bool              rejected = false;
    try {
        (void)gfx.newShaderFromWgsl({}, invalid);
    } catch (const eve::Exception&) {
        rejected = true;
    }
    REQUIRE(rejected);
    rejected = false;
    try {
        (void)gfx.newMeshShaderFromWgsl({}, invalid);
    } catch (const eve::Exception&) {
        rejected = true;
    }
    REQUIRE(rejected);
    rejected = false;
    try {
        (void)gfx.newHairShaderFromWgsl({}, invalid);
    } catch (const eve::Exception&) {
        rejected = true;
    }
    REQUIRE(rejected);

    Shader* mesh = gfx.newMeshShaderFromWgsl({}, {});
    REQUIRE(mesh != nullptr);
    REQUIRE(gfx.configureMeshShaderSurface(*mesh, BlendMode::Opaque, false, true).ok());
}

TEST_CASE("graphics.webgpu.pbrVariantPlansOnlyReachableResources") {
    PbrSurface surface;
    auto       empty = planPbrVariant(surface, false, false);
    REQUIRE(empty.ok());
    CHECK_EQ(empty.value().fragmentTextures, 0u);
    CHECK_EQ(empty.value().vertexTextures, 0u);

    surface.textures[std::size_t(PbrTextureSlot::BaseColor)].texture = marker(1);
    surface.textures[std::size_t(PbrTextureSlot::Normal)].texture    = marker(2);
    surface.vegetationDetail.value                                  = 1.f;
    surface.vegetationDetail.textures[0].texture                     = marker(3);
    surface.vegetationDetail.textures[1].texture                     = marker(4);
    auto detail = planPbrVariant(surface, true, true);
    REQUIRE(detail.ok());
    CHECK_EQ(detail.value().canonicalTextureMask, 0b101u);
    CHECK_EQ(detail.value().detailTextureMask, 0b011u);
    CHECK_EQ(detail.value().fragmentTextures, 6u);
    CHECK_EQ(detail.value().fragmentSamplers, 3u);
    CHECK_EQ(detail.value().canonicalSamplerRepresentatives[0], 0u);
    CHECK_EQ(detail.value().canonicalSamplerRepresentatives[2], 0u);
    CHECK_EQ(detail.value().detailSamplerRepresentatives[0], 0u);
    CHECK_EQ(detail.value().detailSamplerRepresentatives[1], 0u);

    surface.vegetationDetail.maskMode            = 1;
    surface.vegetationDetail.textures[2].texture = marker(5);
    auto masked = planPbrVariant(surface, true, true);
    REQUIRE(masked.ok());
    CHECK_EQ(masked.value().detailTextureMask, 0b111u);
    CHECK_EQ(masked.value().fragmentTextures, 7u);
    CHECK_EQ(masked.value().fragmentSamplers, 3u);
}

TEST_CASE("graphics.webgpu.pbrVariantSeparatesVertexBudgetAndRejectsOverflow") {
    PbrSurface surface;
    surface.vegetationVertex.source  = PbrVegetationDeformationSource::GpuFields;
    surface.vegetationVertex.texture = marker(1);
    surface.vegetationMotion.mode    = PbrVegetationMotionMode::Object;
    surface.vegetationMotion.texture = marker(2);
    surface.vegetationMotion.noise   = marker(3);
    auto vertex = planPbrVariant(surface, false, false, {3, 3});
    REQUIRE(vertex.ok());
    CHECK_EQ(vertex.value().vertexTextures, 3u);
    CHECK_EQ(vertex.value().fragmentTextures, 0u);
    REQUIRE(!planPbrVariant(surface, false, false, {2, 3}).ok());

    for (std::size_t slot = 0; slot < std::size_t(PbrTextureSlot::Count); ++slot)
        surface.textures[slot].texture = marker(slot + 10);
    surface.vegetationExtras.texture = marker(30);
    surface.vegetationColors.texture = marker(31);
    REQUIRE(planPbrVariant(surface, true, true, {17, 16}).ok());
    surface.vegetationAlpha.enabled = true;
    surface.vegetationAlpha.noise   = marker(32);
    REQUIRE(planPbrVariant(surface, true, true, {17, 16}).ok());
    surface.vegetationDetail.value               = 1.f;
    surface.vegetationDetail.textures[0].texture = marker(33);
    surface.vegetationDetail.textures[1].texture = marker(34);
    surface.textures[0].texture                  = nullptr;
    REQUIRE(planPbrVariant(surface, true, true, {17, 16}).ok());
    auto baselineLimit = planPbrVariant(surface, true, true, {16, 16});
    REQUIRE(!baselineLimit.ok());
    CHECK_EQ(baselineLimit.error()->code(), eve::DiagnosticCode::Unsupported);
    surface.textures[0].texture = marker(10);
    auto overflow = planPbrVariant(surface, true, true, {17, 16});
    REQUIRE(!overflow.ok());
    CHECK_EQ(overflow.error()->code(), eve::DiagnosticCode::Unsupported);
}

TEST_CASE("graphics.webgpu.pbrSurfaceRejectsForeignTexture") {
    auto* base = eve::graphics::parity_test::headlessGraphics();
    auto* gfx  = dynamic_cast<eve::graphics::webgpu::Graphics*>(base);
    REQUIRE(gfx != nullptr);

    Texture foreign;
    foreign.gpuHandle = reinterpret_cast<void*>(std::uintptr_t{1});
    PbrSurface surface;
    surface.textures[std::size_t(PbrTextureSlot::BaseColor)].texture = &foreign;
    CHECK(!gfx->setMesh3DPbrSurface(&surface).ok());
    surface.textures[std::size_t(PbrTextureSlot::BaseColor)].texture = reinterpret_cast<Texture*>(std::uintptr_t{1});
    CHECK(!gfx->setMesh3DPbrSurface(&surface).ok());
    surface.textures[std::size_t(PbrTextureSlot::BaseColor)].texture = &foreign;

    const std::uint8_t white[] = {255, 255, 255, 255};
    Texture*           owned   = gfx->newTexture(1, 1, white);
    REQUIRE(owned != nullptr);
    foreign.gpuHandle = owned->gpuHandle;
    CHECK(!gfx->setMesh3DPbrSurface(&surface).ok());
}


#endif
