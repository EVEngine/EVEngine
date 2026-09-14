#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "data/ByteData.h"
#include "filesystem/FileData.h"
#include "image/Image.h"
#include "image/ImageData.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {
#include "PathBesideSource.h"
EVE_DEFINE_PATH_BESIDE_SOURCE()

std::vector<char> fixture(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, const std::string &name) {
    std::ifstream input(pathBesideThisSource(("fixtures/resource_formats/" + name).c_str()), std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void decodePattern(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, const std::string &extension, bool alpha,
                   float tolerance, float divisor = 255.f) {
    const auto                             bytes = fixture(_ZEROERR_TEST_CONTEXT, "pattern." + extension);
    eve::data::ByteData                    source(bytes.data(), bytes.size());
    auto                                  *module = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> decoded(module->newImageData(&source));
    REQUIRE(decoded != nullptr);
    REQUIRE_EQ(decoded->getWidth(), 5);
    REQUIRE_EQ(decoded->getHeight(), 3);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 5; ++x) {
            const auto pixel = decoded->getPixel(x, y);
            CHECK(std::fabs(pixel.r - (17 + x * 37) / divisor) <= tolerance);
            CHECK(std::fabs(pixel.g - (23 + y * 71) / divisor) <= tolerance);
            CHECK(std::fabs(pixel.b - (11 + x * 13 + y * 29) / divisor) <= tolerance);
            CHECK(std::fabs(pixel.a - (alpha ? (x + y * 5) * 17 / 255.f : 1.f)) <= tolerance);
        }
    }
}

void exportPattern(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, medialoader::FormatHandler::EncodedFormat format,
                   const std::string &pixelFormat, const std::string &filename) {
    auto                                  *module = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> source(module->newImageData(5, 3, pixelFormat));
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x) {
            const float lowBits = pixelFormat == "RGBA16" ? (x + 1) / 65535.f : 0.f;
            source->setPixel(x, y,
                             {(17 + x * 37) / 255.f + lowBits, (23 + y * 71) / 255.f + lowBits,
                              (11 + x * 13 + y * 29) / 255.f + lowBits, (x + y * 5) * 17 / 255.f + lowBits});
        }
    std::unique_ptr<eve::filesystem::FileData> encoded(source->encode(format, filename.c_str(), false));
    REQUIRE(encoded != nullptr);
    std::unique_ptr<eve::image::ImageData> decoded(module->newImageData(encoded.get()));
    REQUIRE_EQ(decoded->getWidth(), source->getWidth());
    REQUIRE_EQ(decoded->getHeight(), source->getHeight());
    REQUIRE_EQ(decoded->getFormat(), pixelFormat);
    REQUIRE_EQ(decoded->getSize(), source->getSize());
    CHECK_EQ(std::memcmp(decoded->getData(), source->getData(), source->getSize()), 0);
    if (const char *directory = std::getenv("EVENGINE_FORMAT_EXPORT_DIR")) {
        std::filesystem::create_directories(directory);
        std::ofstream output(std::filesystem::path(directory) / filename, std::ios::binary);
        REQUIRE(output.good());
        output.write(static_cast<const char *>(encoded->getData()), static_cast<std::streamsize>(encoded->getSize()));
        REQUIRE(output.good());
    }
}
}  // namespace

TEST_CASE("resourceFormats.image.png.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "png", true, 1.f / 255.f);
}
TEST_CASE("resourceFormats.image.tga.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "tga", true, 1.f / 255.f);
}
TEST_CASE("resourceFormats.image.bmp.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "bmp", false, 1.f / 255.f);
}
TEST_CASE("resourceFormats.image.jpg.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "jpg", false, 3.f / 255.f);
}
TEST_CASE("resourceFormats.image.jpeg.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "jpeg", false, 3.f / 255.f);
}
TEST_CASE("resourceFormats.image.gif.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "gif", false, 1.f / 255.f);
}
TEST_CASE("resourceFormats.image.webp.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "webp", true, 1.f / 255.f);
}
TEST_CASE("resourceFormats.image.hdr.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "hdr", false, 1.f / 256.f, 256.f);
}
TEST_CASE("resourceFormats.image.exr.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "exr", false, 2.f / 255.f);
}
TEST_CASE("resourceFormats.image.png.rgba8Export") {
    exportPattern(_ZEROERR_TEST_CONTEXT, medialoader::FormatHandler::ENCODED_PNG, "RGBA8", "rgba8.png");
}
TEST_CASE("resourceFormats.image.png.rgba16Export") {
    exportPattern(_ZEROERR_TEST_CONTEXT, medialoader::FormatHandler::ENCODED_PNG, "RGBA16", "rgba16.png");
}
TEST_CASE("resourceFormats.image.tga.rgba8Export") {
    exportPattern(_ZEROERR_TEST_CONTEXT, medialoader::FormatHandler::ENCODED_TGA, "RGBA8", "rgba8.tga");
}

TEST_CASE("resourceFormats.image.truncatedHeadersRejectWithoutPoisoningDecoder") {
    auto *module = eve::image::Image::create();
    for (const auto *extension : {"png", "tga", "bmp", "jpg", "exr", "hdr", "gif", "webp"}) {
        auto bytes = fixture(_ZEROERR_TEST_CONTEXT, std::string("pattern.") + extension);
        bytes.resize(4);
        eve::data::ByteData source(bytes.data(), bytes.size());
        bool                rejected = false;
        try {
            std::unique_ptr<eve::image::ImageData> unexpected(module->newImageData(&source));
        } catch (const std::exception &) {
            rejected = true;
        }
        CHECK(rejected);
    }
    decodePattern(_ZEROERR_TEST_CONTEXT, "png", true, 1.f / 255.f);
}

TEST_CASE("resourceFormats.image.gif.animatedFirstFrame") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "animated.gif", false, 1.f / 255.f);
}

TEST_CASE("resourceFormats.image.webp.animatedFirstFrame") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "animated.webp", false, 1.f / 255.f);
}

TEST_CASE("resourceFormats.image.webp.lossyPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "lossy.webp", false, 0.15f);
}

TEST_CASE("resourceFormats.image.hdr.truncatedPayloadIsRejected") {
    auto bytes = fixture(_ZEROERR_TEST_CONTEXT, "pattern.hdr");
    bytes.pop_back();
    eve::data::ByteData source(bytes.data(), bytes.size());
    bool                rejected = false;
    try {
        std::unique_ptr<eve::image::ImageData> unexpected(eve::image::Image::create()->newImageData(&source));
    } catch (const std::exception &) {
        rejected = true;
    }
    REQUIRE(rejected);
}

TEST_CASE("resourceFormats.image.webp.truncatedPayloadIsRejected") {
    auto bytes = fixture(_ZEROERR_TEST_CONTEXT, "pattern.webp");
    bytes.resize(bytes.size() / 2);
    eve::data::ByteData source(bytes.data(), bytes.size());
    bool                rejected = false;
    try {
        std::unique_ptr<eve::image::ImageData> unexpected(eve::image::Image::create()->newImageData(&source));
    } catch (const std::exception &) {
        rejected = true;
    }
    REQUIRE(rejected);
}
