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
    auto* gfx = Graphics::create();
    REQUIRE(gfx->Graphics::setMesh3DPbrSurface(nullptr).ok());
    surface = PbrSurface{};
    REQUIRE(!gfx->Graphics::setMesh3DPbrSurface(&surface).ok());
}
TEST_CASE("graphics.pbrSurface.extensionFactorsAndUvChangePixels") {
    auto* gfx = Graphics::create();
    REQUIRE(gfx != nullptr);
    gfx->initHeadless(128, 128);
    auto* canvas = gfx->newCanvas(128, 128);
    REQUIRE(canvas != nullptr);
    const float    positions[] = {-.9f, -.9f, .5f, .9f, -.9f, .5f, .9f, .9f, .5f, -.9f, .9f, .5f};
    const float    normals[]   = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const float    uv[]        = {.1f, .1f, .1f, .1f, .1f, .1f, .1f, .1f};
    const float    uv1[]       = {.9f, .1f, .9f, .1f, .9f, .1f, .9f, .1f};
    const uint32_t indices[]   = {0, 2, 1, 0, 3, 2};
    auto*          mesh        = gfx->newMeshFromArrays(positions, normals, uv, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    REQUIRE(mesh->setTexcoordSet(1, uv1).ok());
    gfx->setMesh3DViewProj(glm::mat4(1));
    gfx->setMesh3DView(glm::mat4(1));
    gfx->setMesh3DCameraPos({.35f, .2f, 2});
    Lighting3DPack lighting{};
    lighting.count               = 1;
    lighting.ambient             = {.15f, .15f, .15f, 0};
    lighting.lights[0].posRadius = {.3f, .2f, 1, 0};
    lighting.lights[0].color     = {2, 2, 2, 0};
    auto render                  = [&](const PbrSurface& s) {
        gfx->begin3DFrameToCanvas(canvas);
        gfx->setMesh3DLighting(lighting);
        gfx->setMesh3DMaterial(.15f, .4f);
        gfx->setMesh3DSurface(SurfaceMode::Opaque, BlendMode::Opaque, true, true, .5f, "cutoff");
        REQUIRE(gfx->setMesh3DPbrSurface(&s).ok());
        gfx->drawMesh(mesh, glm::mat4(1), nullptr, Color(.2f, .15f, .1f, 1));
        REQUIRE(gfx->setMesh3DPbrSurface(nullptr).ok());
        gfx->end3DFrameToCanvas();
        std::unique_ptr<eve::image::ImageData> pixels(canvas->newImageData());
        REQUIRE(pixels != nullptr);
        const auto* data = static_cast<const unsigned char*>(pixels->getData());
        return std::vector<unsigned char>(data, data + 128 * 128 * 4);
    };
    auto difference = [](const auto& a, const auto& b) {
        size_t changed = 0;
        for (size_t i = 0; i < a.size(); i += 4)
            if (std::abs(int(a[i]) - int(b[i])) + std::abs(int(a[i + 1]) - int(b[i + 1])) +
                    std::abs(int(a[i + 2]) - int(b[i + 2])) >
                5)
                ++changed;
        return changed;
    };
    PbrSurface base;
    const auto reference = render(base);
    for (int extension = 0; extension < 6; extension++) {
        auto s = base;
        if (extension == 0) s.specularFactor = 0;
        if (extension == 1) {
            s.anisotropyStrength = 1;
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
    auto*         texture  = gfx->newTexture(2, 1, colors);
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
    s.textures[0].texcoord          = 1;
    auto         green              = render(s);
    const size_t center             = (64 * 128 + 64) * 4;
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
    auto*         map      = gfx->newTexture(1, 1, sample);
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
