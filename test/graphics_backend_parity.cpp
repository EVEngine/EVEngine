#include "zeroerr/unittest.h"

#include "filesystem/FileData.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
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
