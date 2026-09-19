#include <cmath>
#include <limits>
#include <memory>
#include "asset/graphics/EvpackImageLoader.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/PbrSurface.h"
#include "graphics/Texture.h"
#include "graphics/VegetationNormal.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::graphics;
TEST_CASE("graphics.vegetation.normal_gpu_matches_filtered_cpu_reference") {
    auto* gfx = Graphics::create();
    gfx->initHeadless(64, 64);
    auto* canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    const float    p[]{-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    n[]{0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]{.4f, .5f, .4f, .5f, .4f, .5f, .4f, .5f};
    const uint32_t ix[]{0, 2, 1, 0, 3, 2};
    auto*          mesh = gfx->newMeshFromArrays(p, n, uv, 4, ix, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t packed[]{255, 128, 0, 0, 0, 128, 0, 255};
    auto*         texture = gfx->newTexture(2, 1, packed);
    REQUIRE(texture != nullptr);
    Lighting3DPack lighting{};
    lighting.count               = 1;
    lighting.ambient             = {.1f, .1f, .1f, 0};
    lighting.lights[0].posRadius = {.7f, .2f, 1, 0};
    lighting.lights[0].color     = {1, 1, 1, 0};
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->setMesh3DCameraPos({0, 0, 2});
    auto render = [&](const PbrSurface& surface) {
        auto drawSurface = surface;
        drawSurface.vegetationColor.backfaceNormalMode = PbrVegetationBackfaceNormalMode::Same;
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DLighting(lighting);
        gfx->setMesh3DMaterial(0, 1);
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
        REQUIRE(gfx->setMesh3DPbrSurface(&drawSurface).ok());
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(.6f, .6f, .6f, 1));
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
        REQUIRE(image != nullptr);
        return image->getPixel(32, 32);
    };
    float minimum = 1, maximum = 0;
    for (unsigned layout = 0; layout < 3; ++layout)
        for (float strength : {-2.f, 0.f, 1.f, 2.f}) {
            auto decoded = decodeVegetationNormal({.7f, 128.f / 255.f, 0, .3f},
                                                  static_cast<VegetationNormalEncoding>(layout), strength);
            REQUIRE(decoded.ok());
            auto    direction = glm::normalize(decoded.value());
            uint8_t referencePixel[4]{0, 0, 0, 255};
            for (unsigned c = 0; c < 3; ++c)
                referencePixel[c] = uint8_t(std::lround((direction[c] * .5f + .5f) * 255.f));
            auto* referenceTexture = gfx->newTexture(1, 1, referencePixel);
            REQUIRE(referenceTexture != nullptr);
            PbrSurface surface;
            auto&      binding  = surface.textures[size_t(PbrTextureSlot::Normal)];
            binding.texture     = texture;
            binding.srgbDecode  = false;
            binding.magFilter   = 9729;
            binding.minFilter   = 9729;
            surface.normalMode  = static_cast<PbrNormalMode>(layout + 1);
            surface.normalScale = strength;
            REQUIRE(!gfx->Graphics::setMesh3DPbrSurface(&surface).ok());
            const auto actual   = render(surface);
            minimum             = std::min(minimum, actual.r);
            maximum             = std::max(maximum, actual.r);
            surface.normalMode  = PbrNormalMode::TangentXYZ;
            surface.normalScale = 1;
            binding.texture     = referenceTexture;
            const auto expected = render(surface);
            CHECK(std::abs(actual.r - expected.r) < .015f);
            CHECK(std::abs(actual.g - expected.g) < .015f);
            CHECK(std::abs(actual.b - expected.b) < .015f);
            surface.normalMode = PbrNormalMode::VegetationRG;
            binding.srgbDecode = true;
            REQUIRE(!validatePbrSurface(surface).ok());
            binding.srgbDecode = false;
            binding.texture    = nullptr;
            REQUIRE(!validatePbrSurface(surface).ok());
        }
    CHECK(maximum - minimum > .05f);
    // A material UV mirror/rotation changes sampling, not the source TVE tangent frame.
    const float directionalUv[]{0, 0, 1, 0, 1, 1, 0, 1};
    mesh = gfx->newMeshFromArrays(p, n, directionalUv, 4, ix, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t directionalNormal[]{230, 128, 255, 255};
    auto*         directionalTexture = gfx->newTexture(1, 1, directionalNormal);
    REQUIRE(directionalTexture != nullptr);
    PbrSurface directional;
    directional.normalMode   = PbrNormalMode::VegetationRG;
    auto& normalBinding      = directional.textures[size_t(PbrTextureSlot::Normal)];
    normalBinding.texture    = directionalTexture;
    normalBinding.srgbDecode = false;
    const auto original      = render(directional);
    normalBinding.scale      = {-1, 2};
    const auto mirrored      = render(directional);
    CHECK(std::abs(original.r - mirrored.r) < .005f);
    normalBinding.rotation = 1.57079632679f;
    const auto rotated     = render(directional);
    CHECK(std::abs(original.r - rotated.r) < .005f);
    // The fixture is sensitive to the previous transformed-UV basis.
    normalBinding.rotation      = 0;
    directional.normalMode      = PbrNormalMode::TangentXYZ;
    const auto transformedBasis = render(directional);
    CHECK(std::abs(original.r - transformedBasis.r) > .05f);
    std::vector<float> tangents, bitangents;
    for (unsigned i = 0; i < 4; ++i) {
        tangents.insert(tangents.end(), {-1, 0, 0});
        bitangents.insert(bitangents.end(), {0, 1, 0});
    }
    REQUIRE(mesh->setTangentFrame(tangents, bitangents).ok());
    directional.normalMode = PbrNormalMode::VegetationRG;
    normalBinding.scale    = {1, 1};
    const auto authored    = render(directional);
    CHECK(std::abs(authored.r - transformedBasis.r) < .005f);
    CHECK(std::abs(authored.r - original.r) > .05f);
    auto invalid = tangents;
    invalid[0]   = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!mesh->setTangentFrame(invalid, bitangents).ok());
    REQUIRE_EQ(mesh->importedTangents(), tangents);
    CHECK(std::abs(render(directional).r - authored.r) < .005f);
    REQUIRE(!mesh->setTangentFrame(tangents, tangents).ok());
    REQUIRE(!mesh->setTangentFrame({}, bitangents).ok());
    REQUIRE(mesh->setTangentFrame({}, {}).ok());
    CHECK(std::abs(render(directional).r - original.r) < .005f);
    // An out-of-range UV must use the imported sampler without changing the tangent basis.
    const uint8_t strip[]{40, 128, 255, 255, 90, 128, 255, 255, 180, 128, 255, 255, 230, 128, 255, 255};
    auto*         stripTexture = gfx->newTexture(4, 1, strip);
    REQUIRE(stripTexture != nullptr);
    normalBinding.scale     = {0, 0};
    normalBinding.offset    = {1.375f, .5f};
    normalBinding.minFilter = normalBinding.magFilter = 9728;
    for (const auto wrap : {10497u, 33071u, 33648u}) {
        normalBinding.wrapS            = wrap;
        normalBinding.texture          = stripTexture;
        const auto     sampled         = render(directional);
        const unsigned pixel           = wrap == 10497u ? 1u : wrap == 33071u ? 3u : 2u;
        auto*          expectedTexture = gfx->newTexture(1, 1, strip + pixel * 4);
        REQUIRE(expectedTexture != nullptr);
        normalBinding.texture = expectedTexture;
        const auto expected   = render(directional);
        CHECK(std::abs(sampled.r - expected.r) < .005f);
        CHECK(std::abs(sampled.g - expected.g) < .005f);
        CHECK(std::abs(sampled.b - expected.b) < .005f);
    }
    std::vector<uint8_t> mipBytes;
    for (const auto level : {0, 1, 2}) {
        const unsigned size = 4u >> level;
        for (unsigned pixel = 0; pixel < size * size; ++pixel)
            mipBytes.insert(mipBytes.end(), {uint8_t(level == 2 ? 230 : 40), 128, 255, 255});
    }
    eve::asset_graphics::GraphicsImageFactoryAdapter imageFactory(*gfx);
    auto                                             mipped = imageFactory.uploadRgba8MipChain(4, 4, 3, mipBytes);
    REQUIRE(mipped.ok());
    REQUIRE_EQ(mipped.value()->getMipmapCount(), 3);
    normalBinding.texture   = mipped.value();
    normalBinding.scale     = {1024, 1024};
    normalBinding.offset    = {0, 0};
    normalBinding.minFilter = 9987;
    normalBinding.magFilter = 9729;
    const auto distant      = render(directional);
    normalBinding.texture   = directionalTexture;
    const auto lastLevel    = render(directional);
    CHECK(std::abs(distant.r - lastLevel.r) < .005f);
    CHECK(std::abs(distant.g - lastLevel.g) < .005f);
    CHECK(std::abs(distant.b - lastLevel.b) < .005f);
    normalBinding.texture   = mipped.value();
    normalBinding.scale     = {0, 0};
    normalBinding.minFilter = 9729;
    const auto baseLevel    = render(directional);
    CHECK(std::abs(baseLevel.r - distant.r) > .05f);
    REQUIRE(!imageFactory.uploadRgba8MipChain(4, 4, 2, mipBytes).ok());
    REQUIRE(!imageFactory.uploadRgba8MipChain(4, 4, 3, std::span(mipBytes).first(mipBytes.size() - 1)).ok());
    REQUIRE(!imageFactory.uploadRgba8MipChain(UINT32_MAX, 4, 3, mipBytes).ok());
    REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
    REQUIRE(imageFactory.releaseImage(mipped.value()).ok());
}
TEST_CASE("graphics.vegetation.normal_source_layout_strength_and_unnormalized_z") {
    const glm::vec4 sample(.75f, .25f, .1f, .5f);
    for (float strength : {-8.f, -1.f, 0.f, .2f, 1.f, 8.f}) {
        auto rg  = decodeVegetationNormal(sample, VegetationNormalEncoding::RedGreen, strength);
        auto rag = decodeVegetationNormal(sample, VegetationNormalEncoding::RedAlphaGreen, strength);
        auto ag  = decodeVegetationNormal(sample, VegetationNormalEncoding::AlphaGreen, strength);
        REQUIRE(rg.ok());
        REQUIRE(rag.ok());
        REQUIRE(ag.ok());
        REQUIRE_EQ(rg.value(), glm::vec3(.5f * strength, -.5f * strength, 1));
        REQUIRE_EQ(rag.value(), glm::vec3(-.25f * strength, -.5f * strength, 1));
        REQUIRE_EQ(ag.value(), glm::vec3(0, -.5f * strength, 1));
    }
    // Filtering must happen before the nonlinear R*A decode.
    auto filtered = decodeVegetationNormal(glm::vec4(.5f, .5f, 0, .5f), VegetationNormalEncoding::RedAlphaGreen, 1);
    REQUIRE(filtered.ok());
    REQUIRE_EQ(filtered.value(), glm::vec3(-.5f, 0, 1));
    auto left  = decodeVegetationNormal(glm::vec4(1, .5f, 0, 0), VegetationNormalEncoding::RedAlphaGreen, 1);
    auto right = decodeVegetationNormal(glm::vec4(0, .5f, 0, 1), VegetationNormalEncoding::RedAlphaGreen, 1);
    REQUIRE(left.ok());
    REQUIRE(right.ok());
    REQUIRE_EQ((left.value().x + right.value().x) * .5f, -1.f);
}
TEST_CASE("graphics.vegetation.normal_rejects_invalid_input_without_clamping") {
    const auto layout = VegetationNormalEncoding::RedGreen;
    for (float value : {-8.01f, 8.01f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        REQUIRE(!decodeVegetationNormal(glm::vec4(.5f), layout, value).ok());
    for (unsigned c = 0; c < 4; ++c)
        for (float value : {-.01f, 1.01f, std::numeric_limits<float>::quiet_NaN()}) {
            glm::vec4 sample(.5f);
            sample[c] = value;
            REQUIRE(!decodeVegetationNormal(sample, layout, 1).ok());
        }
    REQUIRE(!decodeVegetationNormal(glm::vec4(.5f), static_cast<VegetationNormalEncoding>(99), 1).ok());
}

TEST_CASE("graphics.vegetation.explicit_mip_provider_absence_is_observable") {
    struct BaseOnly final : IImageResourceFactory {
        unsigned              calls = 0;
        eve::Result<Texture*> uploadRgba8(uint32_t, uint32_t, const uint8_t*, bool) override {
            ++calls;
            return eve::Result<Texture*>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "fixture"));
        }
        eve::Result<void> releaseImage(Texture*) override { return eve::Result<void>::success(); }
    } factory;
    const std::array<uint8_t, 4> pixel{128, 128, 255, 255};
    auto                         result = factory.uploadRgba8MipChain(1, 1, 1, pixel);
    REQUIRE(!result.ok());
    REQUIRE_EQ(result.status().primaryDiagnostic()->code(), eve::DiagnosticCode::Unsupported);
    REQUIRE_EQ(factory.calls, 0u);
}
