#include "zeroerr/unittest.h"

#include "filesystem/FileData.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/RenderControl.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/TextureSampler.h"
#include "image/ImageData.h"
#include "image/Image.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

using namespace eve::graphics;

namespace {

const uint8_t *pixel(const eve::image::ImageData &image, int x, int y) {
    const auto *bytes = static_cast<const uint8_t *>(image.getData());
    return bytes + (size_t(y) * size_t(image.getWidth()) + size_t(x)) * 4u;
}

uint64_t imageRgbDifference(const eve::image::ImageData &a, const eve::image::ImageData &b) {
    REQUIRE(a.getWidth() == b.getWidth());
    REQUIRE(a.getHeight() == b.getHeight());
    const auto *aBytes = static_cast<const uint8_t *>(a.getData());
    const auto *bBytes = static_cast<const uint8_t *>(b.getData());
    uint64_t difference = 0;
    const size_t pixels = size_t(a.getWidth()) * size_t(a.getHeight());
    for (size_t i = 0; i < pixels; ++i) {
        for (size_t channel = 0; channel < 3; ++channel) {
            const int delta = int(aBytes[i * 4u + channel]) - int(bBytes[i * 4u + channel]);
            difference += uint64_t(delta < 0 ? -delta : delta);
        }
    }
    return difference;
}

Graphics *headlessGraphics() {
    Graphics *gfx = Graphics::create();
    if (!gfx->isHeadless()) gfx->initHeadless(64, 64);
    gfx->setViewportSize(64, 64, 64, 64);
    return gfx;
}

void writeParityArtifact(const eve::image::ImageData &image, const std::string &scene,
                         const std::string &backend) {
    const char *root = std::getenv("EVENGINE_RENDER_PARITY_DIR");
    if (!root || root[0] == '\0') return;

    eve::image::Image::create();
    std::unique_ptr<eve::filesystem::FileData> png(
        image.encode(medialoader::FormatHandler::ENCODED_PNG, (scene + ".png").c_str(), false));
    REQUIRE(png.get() != nullptr);

    const std::filesystem::path directory = std::filesystem::path(root) / backend;
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    REQUIRE(!ec);

    std::ofstream imageOut(directory / (scene + ".png"), std::ios::binary);
    REQUIRE(imageOut.good());
    imageOut.write(static_cast<const char *>(png->getData()),
                   static_cast<std::streamsize>(png->getSize()));
    REQUIRE(imageOut.good());

    std::ofstream manifest(directory / (scene + ".json"));
    REQUIRE(manifest.good());
    manifest << "{\n"
             << "  \"scene\": \"" << scene << "\",\n"
             << "  \"backend\": \"" << backend << "\",\n"
             << "  \"width\": " << image.getWidth() << ",\n"
             << "  \"height\": " << image.getHeight() << ",\n"
             << "  \"profile\": \"flat2d\"\n"
             << "}\n";
    REQUIRE(manifest.good());
}

}  // namespace

TEST_CASE("graphics.backendParity.textureUpdateAndAlphaBlend") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    const std::string backend = gfx->getBackendName();
    const bool supportedBackend = backend == "vulkan" || backend == "webgpu";
    CHECK(supportedBackend);

    const uint8_t red[4] = {255, 0, 0, 255};
    const uint8_t green[4] = {0, 255, 0, 255};
    Texture *texture = gfx->newTexture(1, 1, red);
    REQUIRE(texture != nullptr);
    Texture *stable = texture;
    REQUIRE(gfx->updateTexture(texture, 1, 1, green));
    CHECK(texture == stable);
    CHECK(!gfx->updateTexture(texture, 2, 1, green));

    Canvas *canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    gfx->setCanvas(canvas);
    gfx->clear(Color(0.f, 0.f, 1.f, 1.f), std::nullopt, std::nullopt);
    gfx->drawTexturedRect(texture, 8.f, 8.f, 24.f, 24.f, Color(1.f));
    gfx->drawSolidRect(32.f, 8.f, 24.f, 24.f, Color(1.f, 0.f, 0.f, 0.5f),
                       BlendMode::Alpha);
    gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image.get() != nullptr);
    const uint8_t *background = pixel(*image, 2, 2);
    CHECK(background[0] < 8);
    CHECK(background[1] < 8);
    CHECK(background[2] > 247);

    const uint8_t *updated = pixel(*image, 16, 16);
    CHECK(updated[0] < 8);
    CHECK(updated[1] > 247);
    CHECK(updated[2] < 8);

    const uint8_t *blended = pixel(*image, 40, 16);
    const bool redBlended = blended[0] >= 126 && blended[0] <= 129;
    CHECK(redBlended);
    CHECK(blended[1] < 8);
    const bool blueBlended = blended[2] >= 126 && blended[2] <= 129;
    CHECK(blueBlended);
    writeParityArtifact(*image, "texture_update_alpha_blend", backend);
}

TEST_CASE("graphics.backendParity.draw2dUvRotationAndBlendModes") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    const std::string backend = gfx->getBackendName();
    const bool supportedBackend = backend == "vulkan" || backend == "webgpu";
    CHECK(supportedBackend);

    const uint8_t stripes[] = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 255, 255,
    };
    Texture *texture = gfx->newTexture(4, 1, stripes);
    REQUIRE(texture != nullptr);
    gfx->setTextureSampler(texture, TextureSampler::nearest());

    Canvas *canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    gfx->setCanvas(canvas);
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);

    // Crop the second texel. Nearest sampling makes the expected green region exact.
    gfx->drawTexturedRectShaderUV(texture, nullptr, 2.f, 2.f, 18.f, 12.f, 0.25f, 0.f, 0.5f,
                                  1.f, Color(1.f), false, BlendMode::Opaque);

    // A 90-degree turn changes the wide rectangle into a narrow vertical footprint.
    gfx->drawSolidRectRotated(34.f, 10.f, 18.f, 6.f, 90.f, Color(1.f, 1.f, 0.f, 1.f),
                              BlendMode::Opaque);
    gfx->drawTexturedRectShaderUVRotated(texture, nullptr, 50.f, 10.f, 18.f, 6.f, 90.f, 0.f,
                                         0.f, 0.25f, 1.f, Color(1.f), false,
                                         BlendMode::Opaque);

    gfx->drawSolidRect(2.f, 28.f, 12.f, 12.f, Color(0.f, 0.f, 0.5f, 1.f), BlendMode::Opaque);
    gfx->drawSolidRect(2.f, 28.f, 12.f, 12.f, Color(0.5f, 0.f, 0.f, 1.f),
                       BlendMode::Additive);

    gfx->drawSolidRect(18.f, 28.f, 12.f, 12.f, Color(0.f, 0.f, 1.f, 1.f), BlendMode::Opaque);
    gfx->drawSolidRect(18.f, 28.f, 12.f, 12.f, Color(0.25f, 0.f, 0.f, 0.25f),
                       BlendMode::Premultiplied);

    gfx->drawSolidRect(34.f, 28.f, 12.f, 12.f, Color(0.5f, 1.f, 0.25f, 1.f),
                       BlendMode::Opaque);
    gfx->drawSolidRect(34.f, 28.f, 12.f, 12.f, Color(0.5f, 0.25f, 1.f, 1.f),
                       BlendMode::Multiply);
    gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image.get() != nullptr);

    const uint8_t *crop = pixel(*image, 10, 8);
    CHECK(crop[0] < 8);
    CHECK(crop[1] > 247);
    CHECK(crop[2] < 8);

    const uint8_t *rotatedSolid = pixel(*image, 34, 3);
    CHECK(rotatedSolid[0] > 247);
    CHECK(rotatedSolid[1] > 247);
    CHECK(rotatedSolid[2] < 8);
    const uint8_t *outsideSolid = pixel(*image, 27, 10);
    CHECK(outsideSolid[0] < 8);
    CHECK(outsideSolid[1] < 8);
    CHECK(outsideSolid[2] < 8);

    const uint8_t *rotatedTexture = pixel(*image, 50, 3);
    CHECK(rotatedTexture[0] > 247);
    CHECK(rotatedTexture[1] < 8);
    CHECK(rotatedTexture[2] < 8);

    const uint8_t *additive = pixel(*image, 8, 34);
    const bool additiveRed = additive[0] >= 126 && additive[0] <= 129;
    CHECK(additiveRed);
    CHECK(additive[1] < 8);
    const bool additiveBlue = additive[2] >= 126 && additive[2] <= 129;
    CHECK(additiveBlue);

    const uint8_t *premultiplied = pixel(*image, 24, 34);
    const bool premultipliedRed = premultiplied[0] >= 62 && premultiplied[0] <= 65;
    CHECK(premultipliedRed);
    CHECK(premultiplied[1] < 8);
    const bool premultipliedBlue = premultiplied[2] >= 190 && premultiplied[2] <= 193;
    CHECK(premultipliedBlue);

    const uint8_t *multiply = pixel(*image, 40, 34);
    const bool multiplyRed = multiply[0] >= 62 && multiply[0] <= 65;
    const bool multiplyGreen = multiply[1] >= 62 && multiply[1] <= 65;
    const bool multiplyBlue = multiply[2] >= 62 && multiply[2] <= 65;
    CHECK(multiplyRed);
    CHECK(multiplyGreen);
    CHECK(multiplyBlue);
    writeParityArtifact(*image, "draw2d_uv_rotation_blend_modes", backend);
}

TEST_CASE("graphics.backendParity.lighting2dNormalMapAndMultipleLights") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    const std::string backend = gfx->getBackendName();
    const bool supportedBackend = backend == "vulkan" || backend == "webgpu";
    CHECK(supportedBackend);

    const uint8_t white[] = {255, 255, 255, 255};
    const uint8_t flatNormal[] = {128, 128, 255, 255};
    const uint8_t rightNormal[] = {255, 128, 128, 255};
    Texture *albedo = gfx->newTexture(1, 1, white);
    Texture *flat = gfx->newTexture(1, 1, flatNormal);
    Texture *right = gfx->newTexture(1, 1, rightNormal);
    REQUIRE(albedo != nullptr);
    REQUIRE(flat != nullptr);
    REQUIRE(right != nullptr);

    Lighting2DUBO directional{};
    directional.ambient = Color(0.05f, 0.05f, 0.05f, 0.f);
    directional.meta = Color(1.f, 64.f, 64.f, 0.f);
    directional.lights[0].posRadius = Color(1.f, 0.f, 0.f, 0.f);
    directional.lights[0].color = Color(0.5f, 0.5f, 0.5f, 0.f);

    Canvas *normalCanvas = gfx->newCanvas(64, 64);
    REQUIRE(normalCanvas != nullptr);
    gfx->setCanvas(normalCanvas);
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    gfx->setLighting2D(directional);
    gfx->drawTexturedRectLitUV(albedo, flat, 4.f, 8.f, 24.f, 24.f, 0.f, 0.f, 1.f, 1.f,
                               Color(1.f));
    gfx->drawTexturedRectLitUV(albedo, right, 36.f, 8.f, 24.f, 24.f, 0.f, 0.f, 1.f, 1.f,
                               Color(1.f));
    gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> normalImage(normalCanvas->newImageData());
    REQUIRE(normalImage.get() != nullptr);
    const uint8_t *flatLit = pixel(*normalImage, 16, 20);
    const uint8_t *rightLit = pixel(*normalImage, 48, 20);
    CHECK(flatLit[0] > 32);
    CHECK(rightLit[0] > flatLit[0] + 50);
    CHECK(rightLit[1] > flatLit[1] + 50);
    CHECK(rightLit[2] > flatLit[2] + 50);
    writeParityArtifact(*normalImage, "lighting2d_normal_map", backend);

    Lighting2DUBO points{};
    points.ambient = Color(0.02f, 0.02f, 0.02f, 0.f);
    points.meta = Color(2.f, 64.f, 64.f, 0.f);
    points.lights[0].posRadius = Color(10.f, 48.f, 0.f, 28.f);
    points.lights[0].color = Color(0.9f, 0.f, 0.f, 0.f);
    points.lights[1].posRadius = Color(54.f, 48.f, 0.f, 28.f);
    points.lights[1].color = Color(0.f, 0.f, 0.9f, 0.f);

    Canvas *pointCanvas = gfx->newCanvas(64, 64);
    REQUIRE(pointCanvas != nullptr);
    gfx->setCanvas(pointCanvas);
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    gfx->setLighting2D(points);
    gfx->drawTexturedRectLitUV(albedo, flat, 2.f, 36.f, 60.f, 24.f, 0.f, 0.f, 1.f, 1.f,
                               Color(1.f));
    gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> pointImage(pointCanvas->newImageData());
    REQUIRE(pointImage.get() != nullptr);
    const uint8_t *leftLight = pixel(*pointImage, 10, 48);
    const uint8_t *rightLight = pixel(*pointImage, 54, 48);
    CHECK(leftLight[0] > leftLight[2] + 80);
    CHECK(rightLight[2] > rightLight[0] + 80);
    CHECK(leftLight[0] > 120);
    CHECK(rightLight[2] > 120);
    writeParityArtifact(*pointImage, "lighting2d_multiple_lights", backend);
}

TEST_CASE("graphics.backendParity.textureSamplingRepeatAndMipmaps") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    CHECK(gfx->getMaxAnisotropy() >= 1.f);
    const std::string backend = gfx->getBackendName();
    const bool supportedBackend = backend == "vulkan" || backend == "webgpu";
    CHECK(supportedBackend);

    const uint8_t redBlue[] = {255, 0, 0, 255, 0, 0, 255, 255};
    Texture *nearestTexture = gfx->newTexture(2, 1, redBlue);
    Texture *repeatTexture = gfx->newTexture(2, 1, redBlue);
    Texture *linearTexture = gfx->newTexture(2, 1, redBlue);
    REQUIRE(nearestTexture != nullptr);
    REQUIRE(repeatTexture != nullptr);
    REQUIRE(linearTexture != nullptr);

    uint8_t checker[4 * 4]{};
    for (int x = 0; x < 4; ++x) {
        uint8_t *p = checker + x * 4;
        p[x % 2 == 0 ? 0 : 1] = 255;
        p[3] = 255;
    }
    TextureCreateInfo mipInfo;
    mipInfo.generateMipmaps = true;
    Texture *mipped = gfx->newTexture(4, 1, checker, mipInfo);
    REQUIRE(mipped != nullptr);
    CHECK(mipped->getMipmapCount() == 3);
    const bool mipSamplingEnabled = mipped->getSampler().mipmap == MipmapMode::Linear;
    CHECK(mipSamplingEnabled);

    Canvas *canvas = gfx->newCanvas(64, 64);
    REQUIRE(canvas != nullptr);
    gfx->setCanvas(canvas);
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);

    gfx->setTextureSampler(nearestTexture, TextureSampler::nearest());
    gfx->drawTexturedRectShaderUV(nearestTexture, nullptr, 2.f, 2.f, 28.f, 12.f, 0.f, 0.f, 1.f, 1.f,
                                  Color(1.f), false, BlendMode::Opaque);

    TextureSampler repeat = TextureSampler::nearest();
    repeat.repeatU = true;
    gfx->setTextureSampler(repeatTexture, repeat);
    gfx->drawTexturedRectShaderUV(repeatTexture, nullptr, 34.f, 2.f, 28.f, 12.f, 0.f, 0.f, 2.f, 1.f,
                                  Color(1.f), false, BlendMode::Opaque);

    gfx->setTextureSampler(linearTexture, TextureSampler::linear());
    gfx->drawTexturedRectShaderUV(linearTexture, nullptr, 2.f, 20.f, 28.f, 12.f, 0.f, 0.f, 1.f, 1.f,
                                  Color(1.f), false, BlendMode::Opaque);

    TextureSampler lastMip = TextureSampler::nearest();
    lastMip.mipmap = MipmapMode::Nearest;
    lastMip.minLod = 2.f;
    lastMip.maxLod = 2.f;
    gfx->setTextureSampler(mipped, lastMip);
    gfx->drawTexturedRect(mipped, 34.f, 20.f, 28.f, 28.f, Color(1.f));
    gfx->setCanvas();

    std::unique_ptr<eve::image::ImageData> image(canvas->newImageData());
    REQUIRE(image.get() != nullptr);
    const uint8_t *nearestLeft = pixel(*image, 8, 8);
    const uint8_t *nearestRight = pixel(*image, 24, 8);
    CHECK(nearestLeft[0] > 247);
    CHECK(nearestLeft[2] < 8);
    CHECK(nearestRight[0] < 8);
    CHECK(nearestRight[2] > 247);

    const uint8_t *repeat0 = pixel(*image, 38, 8);
    const uint8_t *repeat1 = pixel(*image, 45, 8);
    const uint8_t *repeat2 = pixel(*image, 52, 8);
    const uint8_t *repeat3 = pixel(*image, 59, 8);
    CHECK(repeat0[0] > 247);
    CHECK(repeat1[2] > 247);
    CHECK(repeat2[0] > 247);
    CHECK(repeat3[2] > 247);

    const uint8_t *linearBoundary = pixel(*image, 16, 26);
    CHECK(linearBoundary[0] > 96);
    CHECK(linearBoundary[2] > 96);

    const uint8_t *mip = pixel(*image, 48, 34);
    const bool mipRed = mip[0] >= 126 && mip[0] <= 129;
    const bool mipGreen = mip[1] >= 126 && mip[1] <= 129;
    CHECK(mipRed);
    CHECK(mipGreen);
    CHECK(mip[2] < 8);
    writeParityArtifact(*image, "texture_sampling_repeat_mipmaps", backend);
}

TEST_CASE("graphics.backendParity.webgpuCustomShaderLifetime") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    if (gfx->getBackendName() != "webgpu") {
        CHECK(true);
        return;
    }

    const char *frag = R"wgsl(
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
    Shader *shader = gfx->newShaderFromWgsl({}, frag);
    REQUIRE(shader != nullptr);
    REQUIRE(shader->gpuHandle != nullptr);

    const uint8_t white[4] = {255, 255, 255, 255};
    Texture *texture = gfx->newTexture(1, 1, white);
    Canvas *canvas = gfx->newCanvas(64, 64);
    REQUIRE(texture != nullptr);
    REQUIRE(canvas != nullptr);
    gfx->setCanvas(canvas);
    gfx->clear(Color(0.f, 0.f, 0.f, 1.f), std::nullopt, std::nullopt);
    gfx->drawTexturedRectShader(texture, shader, 8.f, 8.f, 48.f, 48.f,
                                Color(1.f, 1.f, 1.f, 1.f));
    gfx->setCanvas(nullptr);

    const Color center = canvas->getPixel(32, 32);
    CHECK_GT(center.r, 0.20f);
    CHECK_LT(center.r, 0.30f);
    CHECK_GT(center.g, 0.45f);
    CHECK_LT(center.g, 0.55f);
    CHECK(center.b > 0.95f);
}

TEST_CASE("graphics.backendParity.dynamicMeshUpdate") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);

    const float positions[] = {
        -1.f, -1.f, 0.f,
         1.f, -1.f, 0.f,
         0.f,  1.f, 0.f,
    };
    const uint32_t indices[] = {0, 1, 2};
    Mesh *mesh = gfx->newMeshFromArrays(positions, nullptr, nullptr, 3, indices, 3);
    REQUIRE(mesh != nullptr);
    void *stableGpuHandle = mesh->gpuHandle;

    const float moved[] = {
        -2.f, -1.f, 0.f,
         2.f, -1.f, 0.f,
         0.f,  2.f, 0.f,
    };
    REQUIRE(gfx->updateMeshVertices(mesh, moved, nullptr, nullptr, 3, nullptr, 0));
    CHECK(mesh->gpuHandle == stableGpuHandle);
    CHECK(mesh->gpuVertexCount == 3);
    CHECK(mesh->indexCount == 3);

    const uint32_t invalid[] = {0, 1, 3};
    CHECK(!gfx->updateMeshVertices(mesh, moved, nullptr, nullptr, 3, invalid, 3));
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
    const uint32_t indices[] = {0, 1, 2, 2, 3, 0};
    Mesh *mesh = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);

    const uint8_t cutoutTexture[] = {255, 0, 0, 0, 255, 0, 0, 255};
    Texture *albedo = gfx->newTexture(2, 1, cutoutTexture);
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
    const uint8_t *encodedNormal = pixel(*normal, 48, 16);
    const bool normalXCentered = encodedNormal[0] >= 127 && encodedNormal[0] <= 128;
    const bool normalYCentered = encodedNormal[1] >= 127 && encodedNormal[1] <= 128;
    REQUIRE(normalXCentered);
    REQUIRE(normalYCentered);
    REQUIRE(encodedNormal[2] > 247);
    writeParityArtifact(*normal, "gbuffer_world_normal", gfx->getBackendName());

    std::unique_ptr<eve::image::ImageData> depth(gfx->readGBufferToImageData("depth"));
    REQUIRE(depth.get() != nullptr);
    const uint8_t *linearDepth = pixel(*depth, 48, 16);
    const bool expectedLinearDepth = linearDepth[0] <= 1;
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
    const float uvs[] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const uint32_t indices[] = {0, 1, 2, 2, 3, 0};
    Mesh *mesh = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);
    const uint8_t white[] = {255, 255, 255, 255};
    const uint8_t red[] = {200, 20, 20, 255};
    Texture *base = gfx->newTexture(1, 1, white);
    Texture *decal = gfx->newTexture(1, 1, red);
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
    const uint8_t *center = pixel(*image, 32, 32);
    const bool decalCenterRed = center[0] > 150 && center[1] < 80;
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
    const uint8_t *compositedCenter = pixel(*composited, 32, 32);
    const bool centerHasRedDecal = compositedCenter[0] > compositedCenter[1] + 80;
    REQUIRE(centerHasRedDecal);
    const uint8_t *compositedCorner = pixel(*composited, 2, 2);
    const bool cornerHasBase = compositedCorner[0] > 180 && compositedCorner[1] > 180;
    REQUIRE(cornerHasBase);
    writeParityArtifact(*composited, "decal_forward_composite", gfx->getBackendName());
}

TEST_CASE("graphics.backendParity.pbrNormalParallaxAndCellBomb") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    const std::string backend = gfx->getBackendName();
    const bool supportedBackend = backend == "vulkan" || backend == "webgpu";
    CHECK(supportedBackend);

    const float positions[] = {
        -1.f, -1.f, 0.5f, 1.f, -1.f, 0.5f, 1.f, 1.f, 0.5f, -1.f, 1.f, 0.5f,
    };
    const float normals[] = {
        0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
    };
    const float uvs[] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const uint32_t indices[] = {0, 1, 2, 2, 3, 0};
    Mesh *mesh = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);

    const uint8_t stripes[] = {
        255, 32, 16, 255, 255, 32, 16, 255, 16, 32, 255, 255, 16, 32, 255, 255,
        255, 32, 16, 255, 255, 32, 16, 255, 16, 32, 255, 255, 16, 32, 255, 255,
    };
    const uint8_t flatNormal[] = {128, 128, 255, 255};
    const uint8_t tiltedNormal[] = {255, 128, 128, 255};
    const uint8_t raisedHeight[] = {64, 64, 64, 255};
    Texture *albedo = gfx->newTexture(8, 1, stripes);
    Texture *flat = gfx->newTexture(1, 1, flatNormal);
    Texture *tilted = gfx->newTexture(1, 1, tiltedNormal);
    Texture *height = gfx->newTexture(1, 1, raisedHeight);
    REQUIRE(albedo != nullptr);
    REQUIRE(flat != nullptr);
    REQUIRE(tilted != nullptr);
    REQUIRE(height != nullptr);
    gfx->setTextureSampler(albedo, TextureSampler::nearest());

    Lighting3DPack lighting{};
    lighting.ambient = glm::vec4(0.02f, 0.02f, 0.02f, 0.f);
    lighting.count = 1;
    lighting.lights[0].posRadius = glm::vec4(1.f, 0.f, 0.15f, 0.f);
    lighting.lights[0].color = glm::vec4(0.85f, 0.85f, 0.85f, 1.f);
    gfx->setMesh3DLighting(lighting);
    gfx->setMesh3DViewProj(glm::mat4(1.f));
    gfx->setMesh3DView(glm::mat4(1.f));
    gfx->setMesh3DCameraPos(glm::vec3(1.2f, 0.f, 3.f));
    gfx->setMesh3DMaterial(0.f, 0.8f);

    auto render = [&](Texture *normal, Texture *heightMap, float parallax, float bombStrength,
                      const char *artifact) {
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

    auto baseline = render(flat, height, 0.f, 0.f, "pbr_material_baseline");
    auto normalMapped = render(tilted, height, 0.f, 0.f, "pbr_material_normal_map");
    auto parallaxMapped = render(flat, height, 0.3f, 0.f, "pbr_material_parallax");
    auto cellBombed = render(flat, height, 0.f, 1.f, "pbr_material_cell_bomb");

    CHECK(imageRgbDifference(*baseline, *normalMapped) > 100000u);
    CHECK(imageRgbDifference(*baseline, *parallaxMapped) > 50000u);
    CHECK(imageRgbDifference(*baseline, *cellBombed) > 50000u);
}

TEST_CASE("graphics.backendParity.pbrEnvironmentAndCloudShadow") {
    Graphics *gfx = headlessGraphics();
    REQUIRE(gfx != nullptr);
    const std::string backend = gfx->getBackendName();
    const bool supportedBackend = backend == "vulkan" || backend == "webgpu";
    CHECK(supportedBackend);

    const float positions[] = {
        -1.f, -1.f, 0.5f, 1.f, -1.f, 0.5f, 1.f, 1.f, 0.5f, -1.f, 1.f, 0.5f,
    };
    const float normals[] = {
        0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
    };
    const float uvs[] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    const uint32_t indices[] = {0, 1, 2, 2, 3, 0};
    Mesh *mesh = gfx->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    REQUIRE(mesh != nullptr);

    const uint8_t white[] = {255, 255, 255, 255};
    const uint8_t flatNormal[] = {128, 128, 255, 255};
    const uint8_t greenCube[] = {
        16, 220, 32, 255, 16, 220, 32, 255, 16, 220, 32, 255,
        16, 220, 32, 255, 16, 220, 32, 255, 16, 220, 32, 255,
    };
    Texture *albedo = gfx->newTexture(1, 1, white);
    Texture *normal = gfx->newTexture(1, 1, flatNormal);
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
    auto environmentOn = render("pbr_environment_on");
    const uint8_t *reflected = pixel(*environmentOn, 32, 32);
    CHECK(reflected[1] > reflected[0] + 20);
    CHECK(imageRgbDifference(*environmentOff, *environmentOn) > 20000u);

    Lighting3DPack sun{};
    sun.ambient = glm::vec4(0.01f, 0.01f, 0.01f, 0.f);
    sun.count = 1;
    sun.lights[0].posRadius = glm::vec4(0.f, 0.f, 1.f, 0.f);
    sun.lights[0].color = glm::vec4(0.9f, 0.9f, 0.9f, 1.f);
    gfx->setMesh3DLighting(sun);
    gfx->setMesh3DMaterial(0.f, 0.8f);
    gfx->setMesh3DEnv(nullptr, 0.f);
    gfx->setCloudShadows(0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    auto cloudOff = render("pbr_cloud_shadow_off");
    gfx->setCloudShadows(1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 0.f);
    auto cloudOn = render("pbr_cloud_shadow_on");
    const uint8_t *lit = pixel(*cloudOff, 32, 32);
    const uint8_t *shadowed = pixel(*cloudOn, 32, 32);
    CHECK(lit[0] > shadowed[0] + 80);
    CHECK(imageRgbDifference(*cloudOff, *cloudOn) > 100000u);
}
