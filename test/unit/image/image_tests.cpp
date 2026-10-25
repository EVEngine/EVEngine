#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_image)
#include "PrivateTestScope.h"
#include "common/Exception.h"
#include "data/ByteData.h"
#include "filesystem/FileData.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "medialoader/image/FormatHandler.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>

namespace test_image_contract {
using eve::image::ImageData;
using Color = ImageData::Colorf;

bool near(const Color& actual, const Color& expected, float tolerance = 1.f / 255.f) {
    return std::fabs(actual.r - expected.r) <= tolerance && std::fabs(actual.g - expected.g) <= tolerance &&
           std::fabs(actual.b - expected.b) <= tolerance && std::fabs(actual.a - expected.a) <= tolerance;
}

template <class Operation>
bool throwsImageException(Operation operation) {
    try {
        operation();
    } catch (const eve::Exception&) {
        return true;
    }
    return false;
}

TEST_CASE("image.newImageData: dimensions format and ownership determine the returned pixels") {
    auto* module = eve::image::Image::create();
    REQUIRE(module != nullptr);
    CHECK_EQ(module->getName(), std::string("Image"));

    SUB_CASE("default RGBA8 allocation is zero initialized with bounded coordinates") {
        std::unique_ptr<ImageData> image(module->newImageData(4, 3));
        REQUIRE(image);
        CHECK_EQ(image->getWidth(), 4);
        CHECK_EQ(image->getHeight(), 3);
        CHECK_EQ(image->getFormat(), std::string("RGBA8"));
        CHECK_EQ(image->getPixelSize(), 4u);
        CHECK_EQ(image->getSize(), std::size_t{48});
        REQUIRE(image->getData() != nullptr);
        CHECK_EQ(std::memcmp(image->getData(), std::string(48, '\0').data(), 48), 0);
        CHECK(image->inside(3, 2));
        CHECK(!image->inside(4, 2));
        CHECK(!image->inside(-1, 0));
    };

    SUB_CASE("borrowed input bytes are copied and remain valid when the caller edits its buffer") {
        unsigned char              raw[8]{10, 20, 30, 40, 50, 60, 70, 80};
        std::unique_ptr<ImageData> image(module->newImageData(2, 1, "RGBA8", raw, false));
        REQUIRE(image);
        raw[0] = 99;
        CHECK_EQ(static_cast<const unsigned char*>(image->getData())[0], 10);
    };

    SUB_CASE("negative dimensions invalid formats and undecodable bytes fail") {
        CHECK(!ImageData::validPixelFormat("NOT_A_FORMAT"));
        CHECK(throwsImageException([&] { module->newImageData(-1, 4); }));
        CHECK(throwsImageException([&] { module->newImageData(4, -1); }));
        CHECK(throwsImageException([&] { module->newImageData(2, 2, "NOT_A_FORMAT"); }));
        const char          garbage[] = "not an encoded image";
        eve::data::ByteData bytes(garbage, sizeof(garbage) - 1);
        CHECK(!module->isCompressed(&bytes));
        CHECK(throwsImageException([&] { module->newImageData(&bytes); }));
    };
}

TEST_CASE("image.ImageData pixels: edits clone paste and format conversion have observable values") {
    auto*       module = eve::image::Image::create();
    const Color probe{0.25f, 0.5f, 0.75f, 1.f};

    SUB_CASE("setPixel round-trips and rejects out-of-bounds coordinates") {
        std::unique_ptr<ImageData> image(module->newImageData(2, 2));
        image->setPixel(1, 0, probe);
        CHECK(near(image->getPixel(1, 0), probe));
        CHECK(throwsImageException([&] { image->setPixel(2, 0, probe); }));
        CHECK(throwsImageException([&] { image->getPixel(-1, 0); }));
    };

    SUB_CASE("clone owns independent pixels and paste copies the requested rectangle") {
        std::unique_ptr<ImageData> source(module->newImageData(2, 1));
        source->setPixel(1, 0, probe);
        std::unique_ptr<ImageData> copy(source->clone());
        REQUIRE(copy);
        source->setPixel(1, 0, Color{0.f, 0.f, 0.f, 1.f});
        CHECK(near(copy->getPixel(1, 0), probe));
        std::unique_ptr<ImageData> destination(module->newImageData(3, 2));
        destination->paste(copy.get(), 1, 1, 1, 0, 1, 1);
        CHECK(near(destination->getPixel(1, 1), probe));
        CHECK(!near(destination->getPixel(0, 0), probe));
    };

    SUB_CASE("RGBA8 and RGBA32F can paste in both directions with bounded quantization") {
        CHECK(ImageData::canPaste("RGBA8", "RGBA32F"));
        CHECK(ImageData::canPaste("RGBA32F", "RGBA8"));
        CHECK(!ImageData::canPaste("R8", "RGBA32F"));
        std::unique_ptr<ImageData> rgba8(module->newImageData(1, 1, "RGBA8"));
        std::unique_ptr<ImageData> rgba32f(module->newImageData(1, 1, "RGBA32F"));
        rgba8->setPixel(0, 0, probe);
        rgba32f->paste(rgba8.get(), 0, 0, 0, 0, 1, 1);
        CHECK(near(rgba32f->getPixel(0, 0), probe));
        rgba8->paste(rgba32f.get(), 0, 0, 0, 0, 1, 1);
        CHECK(near(rgba8->getPixel(0, 0), probe));
    };

    SUB_CASE("full-color and reduced-channel formats expose their channel contract") {
        for (const char* format : {"RGBA16", "RGBA16F", "RGBA32F", "RGBA4", "RGB5A1", "RGB565"}) {
            REQUIRE(ImageData::validPixelFormat(format));
            std::unique_ptr<ImageData> image(module->newImageData(1, 1, format));
            REQUIRE(image);
            image->setPixel(0, 0, Color{0.3f, 0.6f, 0.9f, 1.f});
            CHECK(near(image->getPixel(0, 0), Color{0.3f, 0.6f, 0.9f, 1.f}, 0.11f));
        }
        std::unique_ptr<ImageData> redOnly(module->newImageData(1, 1, "R8"));
        redOnly->setPixel(0, 0, probe);
        const auto result = redOnly->getPixel(0, 0);
        CHECK_LT(std::fabs(result.r - probe.r), 0.01f);
        CHECK_LT(std::fabs(result.g), 0.01f);
        CHECK_LT(std::fabs(result.b), 0.01f);
        CHECK_EQ(result.a, 1.f);
    };
}

TEST_CASE("image.ImageData encode: lossless formats decode to their original pixel data") {
    auto* module = eve::image::Image::create();
    for (auto format : {medialoader::FormatHandler::ENCODED_PNG, medialoader::FormatHandler::ENCODED_TGA}) {
        std::unique_ptr<ImageData> input(module->newImageData(2, 2));
        input->setPixel(1, 1, Color{1.f, 0.f, 0.f, 1.f});
        const char* filename = format == medialoader::FormatHandler::ENCODED_PNG ? "unit.png" : "unit.tga";
        std::unique_ptr<eve::filesystem::FileData> encoded(input->encode(format, filename, false));
        REQUIRE(encoded);
        REQUIRE_GT(encoded->getSize(), 0u);
        std::unique_ptr<ImageData> decoded(module->newImageData(encoded.get()));
        REQUIRE(decoded);
        CHECK_EQ(decoded->getWidth(), 2);
        CHECK_EQ(decoded->getHeight(), 2);
        CHECK(near(decoded->getPixel(1, 1), Color{1.f, 0.f, 0.f, 1.f}));
    }
    std::unique_ptr<ImageData> floating(module->newImageData(2, 2, "RGBA32F"));
    CHECK(throwsImageException(
        [&] { floating->encode(medialoader::FormatHandler::ENCODED_PNG, "unsupported.png", false); }));
}

TEST_CASE("image.newCubeFaces: a 3-by-4 cross maps its six face pixels in documented order") {
    auto*                      module = eve::image::Image::create();
    std::unique_ptr<ImageData> cross(module->newImageData(3, 4));
    struct Face {
        int   x, y;
        Color color;
    };
    const Face faces[]{
        {1, 1, {1.f, 0.f, 0.f, 1.f}}, {1, 3, {0.f, 1.f, 0.f, 1.f}}, {1, 0, {0.f, 0.f, 1.f, 1.f}},
        {1, 2, {1.f, 1.f, 0.f, 1.f}}, {0, 1, {1.f, 0.f, 1.f, 1.f}}, {2, 1, {0.f, 1.f, 1.f, 1.f}},
    };
    for (const auto& face : faces) cross->setPixel(face.x, face.y, face.color);
    auto result = module->newCubeFaces(cross.get());
    REQUIRE_EQ(result.size(), std::size_t{6});
    for (std::size_t index = 0; index < result.size(); ++index) {
        CHECK_EQ(result[index]->getWidth(), 1);
        CHECK_EQ(result[index]->getHeight(), 1);
        CHECK(near(result[index]->getPixel(0, 0), faces[index].color));
    }
    std::unique_ptr<ImageData> wrongShape(module->newImageData(5, 7));
    CHECK(throwsImageException([&] { module->newCubeFaces(wrongShape.get()); }));
}

TEST_CASE("image.ImageData rotate: filter and expansion control geometry and output palette") {
    auto*                      module = eve::image::Image::create();
    std::unique_ptr<ImageData> source(module->newImageData(3, 2));
    const Color                red{1.f, 0.f, 0.f, 1.f}, green{0.f, 1.f, 0.f, 1.f};
    source->setPixel(2, 0, red);
    source->setPixel(0, 1, green);
    // SUB_CASE executes synchronously; capture the borrowed pixel pointer, not the unique owner.
    auto* pixels = source.get();

    SUB_CASE("nearest clockwise quarter-turn expands 3-by-2 to 2-by-3 and relocates corner markers") {
        std::unique_ptr<ImageData> result(pixels->rotate(1.57079632679f, "nearest", true));
        REQUIRE(result);
        CHECK_EQ(result->getWidth(), 2);
        CHECK_EQ(result->getHeight(), 3);
        CHECK(near(result->getPixel(1, 2), red));
        CHECK(near(result->getPixel(0, 0), green));
    };

    SUB_CASE("zero rotation without expansion preserves dimensions and the source pixel") {
        std::unique_ptr<ImageData> result(pixels->rotate(0.f, "nearest", false));
        REQUIRE(result);
        CHECK_EQ(result->getWidth(), 3);
        CHECK_EQ(result->getHeight(), 2);
        CHECK(near(result->getPixel(2, 0), red));
    };

    SUB_CASE("rotsprite keeps source colors rather than introducing blended colors") {
        std::unique_ptr<ImageData> result(pixels->rotate(1.57079632679f, "rotsprite", true));
        REQUIRE(result);
        for (int y = 0; y < result->getHeight(); ++y)
            for (int x = 0; x < result->getWidth(); ++x) {
                const auto pixel = result->getPixel(x, y);
                const bool belongsToPalette =
                    near(pixel, red) || near(pixel, green) || near(pixel, Color{0.f, 0.f, 0.f, 0.f});
                CHECK(belongsToPalette);
            }
    };

    SUB_CASE("unknown resampling filters are rejected") {
        CHECK(throwsImageException([&] { pixels->rotate(0.1f, "cubic", true); }));
    };
}

TEST_CASE("image.ImageData paintCircleUv: V flips vertically and optional U wrap crosses the seam") {
    auto*                      module = eve::image::Image::create();
    std::unique_ptr<ImageData> image(module->newImageData(8, 8));
    const Color                red{1.f, 0.f, 0.f, 1.f};
    auto                       top = image->paintCircleUv(0.5f, 1.f, 0.f, red);
    REQUIRE(top.ok());
    CHECK_EQ(top.value().centerY, 0);
    CHECK_EQ(top.value().changedPixelCount, 1);
    CHECK(near(image->getPixel(top.value().centerX, 0), red));
    auto seam = image->paintCircleUv(0.f, 0.5f, 1.1f, red, true, false);
    REQUIRE(seam.ok());
    CHECK_GE(seam.value().changedPixelCount, 4);
    CHECK(near(image->getPixel(7, 4), red));
    auto invalid = image->paintCircleUv(0.5f, 0.5f, -1.f, red);
    CHECK(!invalid.ok());
    CHECK_EQ(invalid.error()->code(), eve::DiagnosticCode::InvalidArgument);
}
}  // namespace test_image_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_image_scale)
#include "PrivateTestScope.h"
#include "image/ImageData.h"

namespace test_image_scale_contract {
using eve::image::ImageData;
using eve::image::ImageScaleFilter;

TEST_CASE("image.ImageData scalePcg: successful resampling changes dimensions and failure preserves source") {
    SUB_CASE("point filtering maps source texel centers to a larger target") {
        ImageData image(3, 2, "RGBA32F");
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x) image.setPixel(x, y, ImageData::Colorf{float(x + y * 3), float(y), 0.f, 1.f});
        auto scaled = image.scalePcg(2, 4, ImageScaleFilter::Point);
        REQUIRE(scaled.ok());
        CHECK_EQ(image.getWidth(), 2);
        CHECK_EQ(image.getHeight(), 4);
        CHECK_EQ(image.getPixel(0, 0).r, 0.f);
        CHECK_EQ(image.getPixel(1, 3).r, 4.f);
    };

    SUB_CASE("bilinear filtering interpolates the four source texels at the target center") {
        ImageData image(2, 2, "RGBA32F");
        image.setPixel(0, 0, ImageData::Colorf{0.f, 0.f, 0.f, 1.f});
        image.setPixel(1, 0, ImageData::Colorf{1.f, 0.f, 0.f, 1.f});
        image.setPixel(0, 1, ImageData::Colorf{0.f, 1.f, 0.f, 1.f});
        image.setPixel(1, 1, ImageData::Colorf{1.f, 1.f, 0.f, 1.f});
        auto scaled = image.scalePcg(2, 2, ImageScaleFilter::Bilinear);
        REQUIRE(scaled.ok());
        CHECK_EQ(image.getPixel(1, 1).r, 0.5f);
        CHECK_EQ(image.getPixel(1, 1).g, 0.5f);
    };

    SUB_CASE("invalid size fails without changing dimensions or pixels") {
        ImageData image(2, 1, "RGBA32F");
        image.setPixel(1, 0, ImageData::Colorf{0.25f, 0.5f, 0.75f, 1.f});
        auto scaled = image.scalePcg(0, 2, ImageScaleFilter::Point);
        CHECK(!scaled.ok());
        CHECK_EQ(image.getWidth(), 2);
        CHECK_EQ(image.getHeight(), 1);
        CHECK_EQ(image.getPixel(1, 0).g, 0.5f);
    };
}
}  // namespace test_image_scale_contract
#endif
