#pragma once

#include "common/config.h"
#include "filesystem/FileData.h"
#include "graphics/Graphics.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/unittest.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

namespace eve::graphics::parity_test {

inline const char *expectedBackendName() {
#ifdef EVENGINE_WEBGPU
    return "webgpu";
#else
    return "vulkan";
#endif
}

inline const uint8_t *pixel(const eve::image::ImageData &image, int x, int y) {
    const auto *bytes = static_cast<const uint8_t *>(image.getData());
    return bytes + (size_t(y) * size_t(image.getWidth()) + size_t(x)) * 4u;
}

inline uint64_t imageRgbDifference(const eve::image::ImageData &a, const eve::image::ImageData &b) {
    REQUIRE(a.getWidth() == b.getWidth());
    REQUIRE(a.getHeight() == b.getHeight());
    const auto  *aBytes     = static_cast<const uint8_t *>(a.getData());
    const auto  *bBytes     = static_cast<const uint8_t *>(b.getData());
    uint64_t     difference = 0;
    const size_t pixels     = size_t(a.getWidth()) * size_t(a.getHeight());
    for (size_t i = 0; i < pixels; ++i) {
        for (size_t channel = 0; channel < 3; ++channel) {
            const int delta = int(aBytes[i * 4u + channel]) - int(bBytes[i * 4u + channel]);
            difference += uint64_t(delta < 0 ? -delta : delta);
        }
    }
    return difference;
}

inline Graphics *headlessGraphics() {
    Graphics *gfx = Graphics::create();
    if (!gfx->isHeadless()) gfx->initHeadless(64, 64);
    gfx->setViewportSize(64, 64, 64, 64);
    return gfx;
}

inline void writeParityArtifact(const eve::image::ImageData &image, const std::string &scene,
                                const std::string &backend) {
    const char *root = std::getenv("EVENGINE_RENDER_PARITY_DIR");
    if (!root || root[0] == '\0') return;

    [[maybe_unused]] auto *const               imageModule = eve::image::Image::create();
    std::unique_ptr<eve::filesystem::FileData> png(
        image.encode(medialoader::FormatHandler::ENCODED_PNG, (scene + ".png").c_str(), false));
    REQUIRE(png.get() != nullptr);

    const std::filesystem::path directory = std::filesystem::path(root) / backend;
    std::error_code             ec;
    std::filesystem::create_directories(directory, ec);
    REQUIRE(!ec);

    std::ofstream imageOut(directory / (scene + ".png"), std::ios::binary);
    REQUIRE(imageOut.good());
    imageOut.write(static_cast<const char *>(png->getData()), static_cast<std::streamsize>(png->getSize()));
    REQUIRE(imageOut.good());

    std::ofstream manifest(directory / (scene + ".json"));
    REQUIRE(manifest.good());
    const bool lit3d = scene.starts_with("pbr_") || scene.starts_with("surface_") || scene.starts_with("masked_") ||
                       scene.starts_with("gbuffer_") || scene.starts_with("decal_") || scene == "dither" ||
                       scene == "coverage";
    const double meanLimit = (lit3d ? 6.0 : 2.0) / 255.0;
    const double p99Limit  = (lit3d ? 20.0 : 8.0) / 255.0;
    manifest << "{\n"
             << "  \"schema\": \"evengine.render-parity\",\n"
             << "  \"version\": 1,\n"
             << "  \"scene\": \"" << scene << "\",\n"
             << "  \"backend\": \"" << backend << "\",\n"
             << "  \"width\": " << image.getWidth() << ",\n"
             << "  \"height\": " << image.getHeight() << ",\n"
             << "  \"contract\": {\n"
             << "    \"color_space\": \"srgb-linearized\",\n"
             << "    \"alpha_coverage_delta_max\": 0.005,\n"
             << "    \"mean_rgb_error_max\": " << meanLimit << ",\n"
             << "    \"p99_rgb_error_max\": " << p99Limit << "\n"
             << "  }\n"
             << "}\n";
    REQUIRE(manifest.good());
}

}  // namespace eve::graphics::parity_test
