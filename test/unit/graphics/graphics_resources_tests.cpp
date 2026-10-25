// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "MaterialRenderControl.cpp",
    "MeshShaderRasterState.cpp",
    "SkyTextureResources.cpp",
    "TextureCellBomb.cpp",
    "ShaderInstances.cpp",
    "ShaderResources.cpp",
    "virtual_texture_blend.cpp",
    "RenderImageAudit.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_MaterialRenderControl)
#include "PrivateTestScope.h"
#include <cmath>
#include "graphics/GBuffer.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderControl.h"
#include "graphics/RenderSystem3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// MaterialRenderControl.cpp
namespace test_MaterialRenderControl {
using namespace eve::graphics;

TEST_CASE("material.shadingModelAndParams") {
    Material mat;
    CHECK(mat.getShadingModel() == "pbr");
    mat.setShadingModel("unlit");
    CHECK(mat.getShadingModel() == "unlit");
    CHECK(mat.getReceiveLight() == false);

    mat.setShadingModel("hair");
    CHECK(mat.getShadingModel() == "hair");
    CHECK(mat.isTransparentHair() == true);

    mat.setShadingModel("nope");
    CHECK(mat.getShadingModel() == "pbr");

    mat.setMetallic(1.5f);
    CHECK(mat.getMetallic() == 1.f);
    mat.setRoughness(0.01f);
    CHECK(mat.getRoughness() >= 0.04f);

    mat.setFloat("outlineWidth", 1.25f);
    CHECK(mat.hasParam("outlineWidth"));
    CHECK(std::fabs(mat.getFloat("outlineWidth") - 1.25f) < 1e-5f);
}

TEST_CASE("material.surfaceModesAndTransparencyPolicies") {
    Material mat;
    CHECK(mat.getSurfaceMode() == "opaque");
    mat.setSurfaceMode("masked");
    mat.setAlphaCutoff(0.37f);
    mat.setDoubleSided(true);
    CHECK(mat.getSurfaceMode() == "masked");
    CHECK(std::fabs(mat.getAlphaCutoff() - 0.37f) < 1e-5f);
    CHECK(mat.getDoubleSided());
    mat.setSurfaceMode("transparent");
    mat.setBlendMode("premultiplied");
    mat.setSortPriority(12);
    CHECK(mat.getBlendMode() == "premultiplied");
    CHECK(mat.getSortPriority() == 12);
    mat.setAlphaTechnique("dither");
    CHECK(mat.getAlphaTechnique() == "dither");
}

TEST_CASE("renderControl.compileFeaturesToPasses") {
    RenderControl rc;
    CHECK(rc.supports("gbuffer"));
    CHECK(rc.supports("ao"));
    CHECK(rc.supports("gi"));
    CHECK(rc.supports("aa"));
    CHECK(rc.supports("msaa"));
    CHECK(rc.supports("shadow"));
    CHECK(rc.supports("clusteredDeferred"));
    CHECK(!rc.supports("deferred"));
    CHECK(rc.getLightingMode() == LightingMode::ForwardPlus);
    CHECK(!rc.isDeferredLightingAvailable());

    rc.compile();
    CHECK(rc.isCompiled());
    CHECK(rc.hasPass("shadow"));
    CHECK(rc.hasPass("forward"));
    CHECK(rc.hasPass("hair"));
    CHECK(rc.hasPass("gbuffer"));
    CHECK(!rc.hasPass("deferredLighting"));
    CHECK(rc.getEffectiveLightingMode() == LightingMode::ForwardPlus);
    CHECK(rc.isEnabled("ao"));
    CHECK(rc.isEnabled("gi"));
    CHECK(rc.isEnabled("aa"));
    CHECK(rc.isEnabled("msaa"));

    rc.disable("msaa");
    CHECK(!rc.isEnabled("msaa"));
    rc.enable("msaa");
    CHECK(rc.isEnabled("msaa"));

    rc.disable("ao");
    rc.disable("gi");
    rc.disable("gbuffer");
    CHECK(rc.isDirty());
    rc.compile();
    CHECK(!rc.hasPass("gbuffer"));
    CHECK(!rc.isEnabled("ao"));
    CHECK(!rc.isEnabled("gi"));

    rc.enable("gbuffer");
    CHECK(rc.isDirty());
    rc.compile();
    CHECK(rc.hasPass("gbuffer"));
    CHECK(rc.getPassCount() >= 3);

    // Pass order: shadow → gbuffer → forward → hair
    int shadowIdx = -1, gbIdx = -1, fwdIdx = -1;
    for (int i = 0; i < rc.getPassCount(); ++i) {
        const std::string n = rc.getPassName(i);
        if (n == "shadow") shadowIdx = i;
        if (n == "gbuffer") gbIdx = i;
        if (n == "forward") fwdIdx = i;
    }
    CHECK(shadowIdx >= 0);
    CHECK(gbIdx > shadowIdx);
    CHECK(fwdIdx > gbIdx);

    rc.enable("gbufferAlbedo");
    CHECK(rc.isEnabled("gbuffer"));
    rc.disable("gbuffer");
    CHECK(!rc.isEnabled("gbufferAlbedo"));
}

TEST_CASE("renderControl.lightingModeHybridFallback") {
    RenderControl rc;
    CHECK(rc.getLightingMode() == LightingMode::ForwardPlus);
    CHECK(!rc.isEnabled("clusteredDeferred"));

    auto okHybrid = rc.setLightingMode(LightingMode::Hybrid);
    CHECK(okHybrid.ok());
    CHECK(rc.getLightingMode() == LightingMode::Hybrid);
    CHECK(rc.isEnabled("clusteredDeferred"));
    CHECK(rc.isEnabled("gbuffer"));
    CHECK(rc.isEnabled("gbufferAlbedo"));
    CHECK(rc.isDirty());

    rc.compile();
    // Detached RenderControl has no Graphics backend — Hybrid falls back to
    // ForwardPlus and is observable via hasHybridLightingFallback().
    CHECK(!rc.isDeferredLightingAvailable());
    CHECK(rc.getEffectiveLightingMode() == LightingMode::ForwardPlus);
    CHECK(rc.hasHybridLightingFallback());
    CHECK(!rc.hasPass("deferredLighting"));
    CHECK(rc.hasPass("forward"));
    CHECK(rc.hasPass("gbuffer"));

    auto bad = rc.setLightingMode("nope");
    CHECK(!bad.ok());
    CHECK(rc.getLightingMode() == LightingMode::Hybrid);

    auto okName = rc.setLightingMode("forwardPlus");
    CHECK(okName.ok());
    CHECK(rc.getLightingMode() == LightingMode::ForwardPlus);
    rc.compile();
    CHECK(rc.getEffectiveLightingMode() == LightingMode::ForwardPlus);
    CHECK(!rc.hasHybridLightingFallback());

    rc.enable("clusteredDeferred");
    CHECK(rc.getLightingMode() == LightingMode::Hybrid);
    rc.disable("clusteredDeferred");
    CHECK(rc.getLightingMode() == LightingMode::ForwardPlus);
}

TEST_CASE("renderControl.lightingPresetNames") {
    RenderControl rc;
    auto          mobile = rc.applyLightingPreset("mobile");
    CHECK(mobile.ok());
    CHECK(rc.getLightingPreset() == LightingPreset::Mobile);
    CHECK(rc.getLightingMode() == LightingMode::ForwardPlus);

    auto ci = rc.applyLightingPreset(LightingPreset::Ci);
    CHECK(ci.ok());
    CHECK(rc.getLightingPreset() == LightingPreset::Ci);
    CHECK(rc.getLightingMode() == LightingMode::ForwardPlus);

    // Detached: Desktop cannot promote to Hybrid (no deferred backend).
    auto desktop = rc.applyLightingPreset("desktop");
    CHECK(desktop.ok());
    CHECK(rc.getLightingPreset() == LightingPreset::Desktop);
    CHECK(rc.getLightingMode() == LightingMode::ForwardPlus);

    auto bad = rc.applyLightingPreset("web");
    CHECK(!bad.ok());
}

TEST_CASE("renderControl.atmospherePassDependencies") {
    RenderControl rc;
    CHECK(rc.supports("atmosphere"));
    CHECK(rc.supports("volumetricFog"));
    rc.enable("fogTemporal");
    rc.compile();
    CHECK(rc.isEnabled("gbuffer"));
    CHECK(rc.isEnabled("atmosphere"));
    CHECK(rc.isEnabled("volumetricFog"));
    CHECK(rc.hasPass("atmosphere"));
    CHECK(rc.hasPass("fogMedia"));
    CHECK(rc.hasPass("fogLighting"));
    CHECK(rc.hasPass("fogTemporal"));
    CHECK(rc.hasPass("fogIntegrate"));
    CHECK(rc.hasPass("fogComposite"));
    rc.disable("volumetricFog");
    CHECK(!rc.isEnabled("fogTemporal"));
}

TEST_CASE("renderControl.reflectionChainDependenciesAndQuality") {
    RenderControl rc;
    CHECK(rc.getPostProcessQuality() == "high");
    CHECK(rc.getReflectionQuality() == "high");

    rc.setPostProcessQuality("ultra");
    CHECK(rc.getPostProcessQuality() == "ultra");
    CHECK(rc.getReflectionQuality() == "ultra");
    rc.setReflectionQuality("invalid");
    CHECK(rc.getPostProcessQuality() == "high");

    rc.enable("reflectionChain");
    CHECK(rc.isEnabled("reflectionChain"));
    CHECK(rc.isEnabled("gbuffer"));
    CHECK(rc.isEnabled("aa"));
    CHECK(rc.isEnabled("taa"));
    CHECK(rc.isEnabled("rtgi"));
    CHECK(rc.isEnabled("ssr"));
    CHECK(!rc.isEnabled("msaa"));

    rc.disable("ssr");
    CHECK(!rc.isEnabled("ssr"));
    CHECK(!rc.isEnabled("reflectionChain"));

    rc.enable("reflectionChain");
    rc.disable("reflectionChain");
    CHECK(!rc.isEnabled("reflectionChain"));
    CHECK(!rc.isEnabled("taa"));
    CHECK(!rc.isEnabled("rtgi"));
    CHECK(!rc.isEnabled("ssr"));
    CHECK(rc.isEnabled("aa"));
}

TEST_CASE("camera3d.boxProjectedEnvironmentProbeState") {
    Camera3D *camera = Camera3D::createCamera();
    REQUIRE(camera != nullptr);
    CHECK(!camera->hasEnvProbe());
    camera->setEnvProbe(1.f, 2.f, 3.f, 4.f, -5.f, 6.f);
    CHECK(!camera->hasEnvProbe());
    CHECK(std::fabs(camera->getEnvProbeCenterX() - 1.f) < 1e-5f);
    CHECK(std::fabs(camera->getEnvProbeCenterY() - 2.f) < 1e-5f);
    CHECK(std::fabs(camera->getEnvProbeCenterZ() - 3.f) < 1e-5f);
    CHECK(camera->getEnvProbeExtentY() == 0.f);
    camera->setEnvProbe(1.f, 2.f, 3.f, 4.f, 5.f, 6.f);
    CHECK(camera->hasEnvProbe());
    CHECK(std::fabs(camera->getEnvProbeExtentX() - 4.f) < 1e-5f);
    CHECK(std::fabs(camera->getEnvProbeExtentY() - 5.f) < 1e-5f);
    CHECK(std::fabs(camera->getEnvProbeExtentZ() - 6.f) < 1e-5f);
    camera->clearEnvProbe();
    CHECK(!camera->hasEnvProbe());
}

TEST_CASE("renderable3d.materialAndParts") {
    auto *ent = Renderable3D::create();
    REQUIRE(ent != nullptr);
    Material body;
    body.setShadingModel("pbr");
    body.setMetallic(0.8f);
    Material hair;
    hair.setShadingModel("hair");

    ent->setMaterial(&body);
    CHECK(ent->getMaterial() == &body);
    CHECK(ent->meshRenderer()->effectiveHair() == false);

    ent->setPart(0, "body", nullptr, &body);
    // null mesh does not grow partCount
    CHECK(ent->getPartCount() == 0);

    // Use a fake non-null mesh pointer only for slot bookkeeping (not drawn here).
    Mesh dummyMesh;
    ent->setPart(0, "body", &dummyMesh, &body);
    ent->setPart(1, "hair", &dummyMesh, &hair);
    CHECK(ent->getPartCount() == 2);
    CHECK(ent->getPartName(0) == "body");
    CHECK(ent->getPartName(1) == "hair");
    CHECK(ent->getPartMaterial(1) == &hair);
    CHECK(ent->getPartMaterial(1)->isTransparentHair());

    ent->clearParts();
    CHECK(ent->getPartCount() == 0);
}

TEST_CASE("gbuffer.bufferQueries") {
    GBuffer gb;
    CHECK(!gb.isValid());
    CHECK(!gb.hasBuffer("depth"));

    Texture depth, normal, albedo;
    gb.setTargets(128, 96, &depth, &normal, &albedo);
    CHECK(gb.isValid());
    CHECK(gb.getWidth() == 128);
    CHECK(gb.hasBuffer("depth"));
    CHECK(gb.hasBuffer("normal"));
    CHECK(gb.hasBuffer("albedo"));
    CHECK(gb.getBuffer("depth") == &depth);
    CHECK(!gb.hasBuffer("hwDepth"));
    CHECK(!gb.hasBuffer("pbrParams"));
    CHECK(!gb.hasBuffer("emissive"));
    CHECK(gb.getBuffer("missing") == nullptr);

    Texture hw;
    gb.setTargets(128, 96, &depth, &normal, &albedo, &hw);
    CHECK(gb.hasBuffer("hwDepth"));
    CHECK(gb.getHwDepthTexture() == &hw);
    CHECK(gb.getBuffer("hwDepth") == &hw);
    CHECK(!gb.hasBuffer("pbrParams"));

    Texture pbr, emissive;
    gb.setTargets(128, 96, &depth, &normal, &albedo, &hw, &pbr, &emissive);
    CHECK(gb.hasBuffer("pbrParams"));
    CHECK(gb.hasBuffer("emissive"));
    CHECK(gb.getPbrParamsTexture() == &pbr);
    CHECK(gb.getEmissiveTexture() == &emissive);
    CHECK(gb.getBuffer("pbrParams") == &pbr);
    CHECK(gb.getBuffer("emissive") == &emissive);

    gb.clear();
    CHECK(!gb.isValid());
    CHECK(!gb.hasBuffer("pbrParams"));
    CHECK(!gb.hasBuffer("emissive"));
}
}  // namespace test_MaterialRenderControl
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_MeshShaderRasterState)
#include "PrivateTestScope.h"
#include <limits>
#include "graphics/MeshShaderRasterState.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// MeshShaderRasterState.cpp
namespace test_MeshShaderRasterState {
TEST_CASE("graphics.mesh raster state admits source overlays and rejects invalid snapshots") {
    using namespace eve::graphics;
    MeshShaderRasterState state;
    REQUIRE(validateMeshShaderRasterState(state).ok());
    state.depthCompare   = MeshDepthCompare::Always;
    state.colorWriteMask = 7;
    REQUIRE(validateMeshShaderRasterState(state).ok());
    state.depthCompare      = MeshDepthCompare::LessEqual;
    state.depthBiasConstant = -1;
    state.depthBiasSlope    = -1;
    REQUIRE(validateMeshShaderRasterState(state).ok());
    state.colorWriteMask = 16;
    REQUIRE(!validateMeshShaderRasterState(state).ok());
    state.colorWriteMask = 15;
    state.depthBiasSlope = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!validateMeshShaderRasterState(state).ok());
    state.depthBiasSlope = 0;
    state.depthCompare   = static_cast<MeshDepthCompare>(999);
    REQUIRE(!validateMeshShaderRasterState(state).ok());
}
}  // namespace test_MeshShaderRasterState
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_SkyTextureResources)
#include "PrivateTestScope.h"
#include <array>
#include "graphics/ShaderResources.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// SkyTextureResources.cpp
namespace test_SkyTextureResources {
using namespace eve::graphics;

TEST_CASE("graphics.sky volume layout shrinks all three mip dimensions without losing HDR bytes") {
    std::array<std::byte, 344> bytes{};
    ShaderImageInput           image;
    image.dimension = ShaderImageDimension::Image3D;
    image.format    = ShaderImageFormat::RGBA16Float;
    image.width     = 2;
    image.height    = 4;
    image.depth     = 4;
    image.mipLevels = 3;
    // 32 + 4 + 1 RGBA16F texels.
    image.bytes  = std::span(bytes).first(296);
    auto regions = shaderImageRegions(image);
    REQUIRE(regions.ok());
    REQUIRE(regions.value().size() == 3);
    REQUIRE(regions.value()[0].size == 256);
    REQUIRE(regions.value()[1].offset == 256);
    REQUIRE(regions.value()[1].depth == 2);
    REQUIRE(regions.value()[1].size == 32);
    REQUIRE(regions.value()[2].offset == 288);
    REQUIRE(regions.value()[2].size == 8);
    REQUIRE(regions.value()[2].depth == 1);
    image.bytes = std::span(bytes).first(295);
    REQUIRE(!shaderImageRegions(image).ok());
}

TEST_CASE("graphics.sky volume depth determines legal mip count") {
    std::array<std::byte, 120> bytes{};
    ShaderImageInput           image;
    image.dimension = ShaderImageDimension::Image3D;
    image.format    = ShaderImageFormat::RGBA16Unorm;
    image.width = image.height = 1;
    image.depth                = 8;
    image.mipLevels            = 4;
    image.bytes                = bytes;
    auto regions               = shaderImageRegions(image);
    REQUIRE(regions.ok());
    REQUIRE(regions.value()[3].offset == 112);
    image.dimension = ShaderImageDimension::Image2D;
    REQUIRE(!shaderImageRegions(image).ok());
    image.dimension = ShaderImageDimension::Image3D;
    image.layers    = 2;
    REQUIRE(!shaderImageRegions(image).ok());
    image.layers = 1;
    image.format = ShaderImageFormat::BC3;
    REQUIRE(!shaderImageRegions(image).ok());
}

TEST_CASE("graphics.sky float32 textures preserve full channel storage and reject oversized volumes") {
    std::array<std::byte, 32> bytes{};
    ShaderImageInput          image;
    image.format = ShaderImageFormat::RGBA32Float;
    image.width  = 2;
    image.height = 1;
    image.bytes  = bytes;
    auto regions = shaderImageRegions(image);
    REQUIRE(regions.ok());
    REQUIRE(regions.value()[0].size == 32);
    image.dimension = ShaderImageDimension::Image3D;
    image.width = image.height = image.depth = 2048;
    REQUIRE(!shaderImageRegions(image).ok());
}
}  // namespace test_SkyTextureResources
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_TextureCellBomb)
#include "PrivateTestScope.h"
#include <cmath>
#include "graphics/TextureCellBomb.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// TextureCellBomb.cpp
namespace test_TextureCellBomb_unit {
using eve::graphics::texBombHash22;
using eve::graphics::TexCellBombParams;
using eve::graphics::texCellBombSampleUV;

TEST_CASE("TextureCellBomb.hash22InUnitInterval") {
    for (int y = -3; y <= 5; ++y) {
        for (int x = -3; x <= 5; ++x) {
            float hx = 0.f, hy = 0.f;
            texBombHash22(float(x), float(y), hx, hy);
            CHECK(hx >= 0.f);
            CHECK(hx < 1.f);
            CHECK(hy >= 0.f);
            CHECK(hy < 1.f);
        }
    }
}

TEST_CASE("TextureCellBomb.strengthZeroIsIdentity") {
    TexCellBombParams p;
    p.cellScale = 8.f;
    p.strength  = 0.f;
    p.rotAmount = 1.f;
    float ou = -1.f, ov = -1.f;
    texCellBombSampleUV(0.37f, 0.81f, p, ou, ov);
    CHECK(std::fabs(ou - 0.37f) < 1e-6f);
    CHECK(std::fabs(ov - 0.81f) < 1e-6f);
}

TEST_CASE("TextureCellBomb.strengthPerturbsUV") {
    TexCellBombParams p;
    p.cellScale = 4.f;
    p.strength  = 1.f;
    p.rotAmount = 1.f;
    float ou = 0.f, ov = 0.f;
    texCellBombSampleUV(0.25f, 0.25f, p, ou, ov);
    const float du = ou - 0.25f;
    const float dv = ov - 0.25f;
    CHECK(du * du + dv * dv > 1e-6f);
}

TEST_CASE("TextureCellBomb.neighborCellsDiffer") {
    TexCellBombParams p;
    p.cellScale = 2.f;
    p.strength  = 1.f;
    p.rotAmount = 0.5f;
    float a0 = 0.f, a1 = 0.f, b0 = 0.f, b1 = 0.f;
    // Same fractional position in adjacent cells → different offsets.
    texCellBombSampleUV(0.1f, 0.1f, p, a0, a1);
    texCellBombSampleUV(0.6f, 0.1f, p, b0, b1);
    CHECK(std::fabs((a0 - 0.1f) - (b0 - 0.6f)) + std::fabs((a1 - 0.1f) - (b1 - 0.1f)) > 1e-4f);
}
}  // namespace test_TextureCellBomb_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ShaderInstances) || \
    defined(EVE_TEST_ORIGIN_ShaderResources)
#include "PrivateTestScope.h"
#include <array>
#include <cstring>
#include <limits>
#include "graphics/ShaderResources.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: graphics_shader_resources
namespace test_graphics_shader_resources {
// ShaderInstances.cpp
namespace {
namespace ShaderInstances {
TEST_CASE("graphics.shader instance matrices reject malformed nonfinite and singular records") {
    std::array<float, 16> matrix{1, 0, 0, 0, 0, 2, 0, 0, 0, 0, -3, 0, 100, 50, -20, 1};
    auto                  bytes = std::as_bytes(std::span(matrix));
    auto                  valid = eve::graphics::shaderInstanceMatrixCount(bytes);
    REQUIRE(valid.ok());
    CHECK_EQ(valid.value(), 1u);
    REQUIRE(eve::graphics::shaderInstanceMatrixCount({}).ok());
    CHECK(!eve::graphics::shaderInstanceMatrixCount(bytes.first(63)).ok());
    matrix[0] = 0;
    CHECK(!eve::graphics::shaderInstanceMatrixCount(bytes).ok());
    matrix[0] = 1;
    matrix[3] = 1;
    CHECK(!eve::graphics::shaderInstanceMatrixCount(bytes).ok());
    matrix[3]  = 0;
    matrix[12] = std::numeric_limits<float>::infinity();
    CHECK(!eve::graphics::shaderInstanceMatrixCount(bytes).ok());
    matrix[12] = 100;
    std::array<std::byte, 65> unaligned;
    std::memcpy(unaligned.data() + 1, matrix.data(), 64);
    REQUIRE(eve::graphics::shaderInstanceMatrixCount(std::span(unaligned).subspan(1)).ok());
}
}  // namespace ShaderInstances
}  // namespace

// ShaderResources.cpp
namespace {
namespace ShaderResources {
TEST_CASE("graphics.shader resources preserve BC3 tail mips and layer boundaries") {
    std::array<std::byte, 160>      bytes{};
    eve::graphics::ShaderImageInput image;
    image.width     = 8;
    image.height    = 4;
    image.layers    = 2;
    image.mipLevels = 4;
    image.dimension = eve::graphics::ShaderImageDimension::Array2D;
    image.format    = eve::graphics::ShaderImageFormat::BC3;
    image.bytes     = bytes;
    auto result     = eve::graphics::shaderImageRegions(image);
    REQUIRE(result.ok());
    REQUIRE(result.value().size() == 8);
    REQUIRE(result.value()[0].size == 32);
    REQUIRE(result.value()[3].size == 16);
    REQUIRE(result.value()[4].offset == 80);
    REQUIRE(result.value()[7].offset + result.value()[7].size == bytes.size());
    image.format = eve::graphics::ShaderImageFormat::BC7Srgb;
    auto bc7     = eve::graphics::shaderImageRegions(image);
    REQUIRE(bc7.ok());
    REQUIRE(bc7.value()[4].offset == 80);
    image.bytes = std::span(bytes).first(159);
    REQUIRE(!eve::graphics::shaderImageRegions(image).ok());
}

TEST_CASE("graphics.shader resources reject invalid dimensions and preserve R16 byte width") {
    std::array<std::byte, 4>        bytes{};
    eve::graphics::ShaderImageInput image;
    image.width  = 2;
    image.height = 1;
    image.format = eve::graphics::ShaderImageFormat::R16;
    image.bytes  = bytes;
    auto result  = eve::graphics::shaderImageRegions(image);
    REQUIRE(result.ok());
    REQUIRE(result.value()[0].size == 4);
    image.mipLevels = 3;
    REQUIRE(!eve::graphics::shaderImageRegions(image).ok());
    image.mipLevels = 1;
    image.dimension = eve::graphics::ShaderImageDimension::Cube;
    REQUIRE(!eve::graphics::shaderImageRegions(image).ok());
    image.dimension = eve::graphics::ShaderImageDimension::Image2D;
    image.width     = 0;
    REQUIRE(!eve::graphics::shaderImageRegions(image).ok());
}
}  // namespace ShaderResources
}  // namespace
}  // namespace test_graphics_shader_resources
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_virtual_texture_blend)
#include "PrivateTestScope.h"
#include <cmath>
#include <memory>
#include "graphics/VirtualTextureBlend.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// virtual_texture_blend.cpp
namespace test_virtual_texture_blend_unit {
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
    const auto &set = built.value();
    CHECK_EQ(set.pageExtent, 6);
    CHECK_EQ(set.residentPages, 1u);
    CHECK_EQ(set.pageTable->getWidth(), 2);
    CHECK_EQ(set.pageTable->getHeight(), 2);
    CHECK(set.pageTable->getPixel(0, 0).b < 0.05f);
    CHECK(set.pageTable->getPixel(1, 0).b > 0.95f);
    CHECK(set.albedoAtlas->getPixel(0, 0).b > 0.75f);
    CHECK(set.normalAtlas->getPixel(0, 0).b > 0.95f);
}
}  // namespace test_virtual_texture_blend_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_RenderImageAudit)
#include "PrivateTestScope.h"
#include <memory>
#include "RenderImageAudit.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// RenderImageAudit.cpp
namespace test_image_audit {
using eve::image::ImageData;
[[maybe_unused]] ImageData *makeRgba(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    auto *img = new ImageData(w, h, "RGBA8");
    auto *p   = static_cast<uint8_t *>(img->getData());
    for (int i = 0; i < w * h; ++i) {
        p[i * 4 + 0] = r;
        p[i * 4 + 1] = g;
        p[i * 4 + 2] = b;
        p[i * 4 + 3] = 255;
    }
    return img;
}

[[maybe_unused]] void fillRect(ImageData *img, int x0, int y0, int rw, int rh, uint8_t r, uint8_t g, uint8_t b) {
    const int w = img->getWidth();
    const int h = img->getHeight();
    auto     *p = static_cast<uint8_t *>(img->getData());
    for (int y = y0; y < y0 + rh && y < h; ++y) {
        for (int x = x0; x < x0 + rw && x < w; ++x) {
            uint8_t *q = p + (y * w + x) * 4;
            q[0]       = r;
            q[1]       = g;
            q[2]       = b;
            q[3]       = 255;
        }
    }
}

[[maybe_unused]] bool hasKind(const RenderAuditResult &r, RenderDefect::Kind k) {
    for (const auto &d : r.defects)
        if (d.kind == k) return true;
    return false;
}


TEST_CASE("graphics.imageAudit.syntheticDefects") {
    RenderAuditBg bg{0.08f, 0.09f, 0.11f};
    const uint8_t geoR = 180, geoG = 180, geoB = 185;

    {
        std::unique_ptr<ImageData> img(makeRgba(64, 64, geoR, geoG, geoB));
        RenderAuditConfig          cfg{"synthetic", "clean", "", 0};
        auto                       r = auditImage(*img, cfg, bg);
        CHECK(!r.empty);
        CHECK(r.defects.empty());
        CHECK(r.occupancy > 0.9f);
    }

    {
        std::unique_ptr<ImageData> img(makeRgba(64, 64, geoR, geoG, geoB));
        // One full interior 16x16 tile; unaligned holes split across tiles and
        // stay above kEmptyTile so detectHoles never fires.
        fillRect(img.get(), 16, 16, 16, 16, 0, 0, 0);
        RenderAuditConfig cfg{"synthetic", "hole", "", 0};
        auto              r = auditImage(*img, cfg, bg);
        REQUIRE(hasKind(r, RenderDefect::Kind::Incomplete));
        bool locOk = false;
        for (const auto &d : r.defects) {
            if (d.kind != RenderDefect::Kind::Incomplete) continue;
            const int cx = d.x + d.w / 2;
            const int cy = d.y + d.h / 2;
            if (cx >= 16 && cx < 48 && cy >= 16 && cy < 48) locOk = true;
        }
        CHECK(locOk);
    }

    {
        std::unique_ptr<ImageData> img(makeRgba(64, 64, geoR, geoG, geoB));
        fillRect(img.get(), 8, 8, 12, 12, 255, 0, 255);
        RenderAuditConfig cfg{"synthetic", "magenta", "", 0};
        auto              r = auditImage(*img, cfg, bg);
        REQUIRE(hasKind(r, RenderDefect::Kind::Damage));
        REQUIRE(r.hasSevere());
    }

    {
        std::unique_ptr<ImageData> img(makeRgba(64, 64, geoR, geoG, geoB));
        fillRect(img.get(), 0, 30, 64, 1, 20, 23, 28);
        RenderAuditConfig cfg{"synthetic", "tearing", "", 0};
        auto              r = auditImage(*img, cfg, bg);
        REQUIRE(hasKind(r, RenderDefect::Kind::Damage));
    }

    {
        std::unique_ptr<ImageData> a(makeRgba(64, 64, geoR, geoG, geoB));
        std::unique_ptr<ImageData> b(makeRgba(64, 64, geoR, geoG, geoB));
        fillRect(b.get(), 16, 16, 24, 24, 40, 40, 40);
        RenderAuditConfig cfg{"synthetic", "flicker", "", 0};
        auto              r = auditFlicker(*a, *b, cfg, bg);
        REQUIRE(hasKind(r, RenderDefect::Kind::Flicker));
        bool locOk = false;
        for (const auto &d : r.defects) {
            if (d.kind != RenderDefect::Kind::Flicker) continue;
            const int cx = d.x + d.w / 2;
            const int cy = d.y + d.h / 2;
            if (cx >= 12 && cx < 44 && cy >= 12 && cy < 44) locOk = true;
        }
        CHECK(locOk);
    }

    {
        std::unique_ptr<ImageData> img(makeRgba(64, 64, 20, 23, 28));
        RenderAuditConfig          cfg{"synthetic", "empty", "", 0};
        auto                       r = auditImage(*img, cfg, bg);
        REQUIRE(r.empty);
        REQUIRE(r.hasSevere());
    }
}

}  // namespace test_image_audit
#endif
