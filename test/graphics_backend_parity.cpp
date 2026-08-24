#include "zeroerr/unittest.h"

#include "filesystem/FileData.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/RenderControl.h"
#include "graphics/Texture.h"
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
