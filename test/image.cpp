#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "data/ByteData.h"
#include "filesystem/FileData.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "medialoader/image/FormatHandler.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <string>

namespace {

eve::image::Image* img() {
    return eve::image::Image::create();
}

using Colorf = eve::image::ImageData::Colorf;

bool nearColor(const Colorf& a, const Colorf& b, float eps = 1.0f / 255.0f) {
    return std::fabs(a.r - b.r) <= eps && std::fabs(a.g - b.g) <= eps &&
           std::fabs(a.b - b.b) <= eps && std::fabs(a.a - b.a) <= eps;
}

bool expectException(const std::function<void()>& fn) {
    try {
        fn();
    } catch (const eve::Exception&) {
        return true;
    }
    return false;
}

}  // namespace

TEST_CASE("image.create") {
    auto* module = img();
    REQUIRE(module != nullptr);
    CHECK_EQ(module->getName(), std::string("Image"));
    CHECK(!module->getFormatHandlers().empty());
}

TEST_CASE("image.newImageData.sizeAndDefaults") {
    auto* module = img();
    std::unique_ptr<eve::image::ImageData> data(module->newImageData(4, 3));
    REQUIRE(data.get() != nullptr);
    CHECK_EQ(data->getWidth(), 4);
    CHECK_EQ(data->getHeight(), 3);
    CHECK_EQ(data->getFormat(), std::string("RGBA8"));
    CHECK_EQ(data->getPixelSize(), 4u);
    CHECK_EQ(data->getSize(), 4u * 3u * 4u);
    REQUIRE(data->getData() != nullptr);
    CHECK(std::memcmp(data->getData(), std::string(4 * 3 * 4, '\0').c_str(), data->getSize()) == 0);
    CHECK(!data->isSRGB());
    CHECK(data->inside(0, 0));
    CHECK(data->inside(3, 2));
    CHECK(!data->inside(-1, 0));
    CHECK(!data->inside(4, 0));
    CHECK(!data->inside(0, 3));
}

TEST_CASE("image.setPixel.getPixel.roundTrip") {
    auto* module = img();
    std::unique_ptr<eve::image::ImageData> data(module->newImageData(2, 2));
    Colorf in{0.25f, 0.5f, 0.75f, 1.0f};
    data->setPixel(1, 0, in);
    Colorf out = data->getPixel(1, 0);
    CHECK(nearColor(in, out));
    data->setPixel(0, 1, in);
    Colorf viaRef;
    data->getPixel(0, 1, viaRef);
    CHECK(nearColor(in, viaRef));
}

TEST_CASE("image.setPixel.outOfRange") {
    auto* module = img();
    std::unique_ptr<eve::image::ImageData> data(module->newImageData(2, 2));
    Colorf c{1, 1, 1, 1};
    CHECK(expectException([&] { data->setPixel(2, 0, c); }));
    CHECK(expectException([&] { data->getPixel(-1, 0); }));
}

TEST_CASE("image.invalidFormat") {
    auto* module = img();
    CHECK(!eve::image::ImageData::validPixelFormat("NOT_A_FORMAT"));
    CHECK(expectException([&] { module->newImageData(2, 2, "NOT_A_FORMAT"); }));
}

TEST_CASE("image.invalidSize") {
    auto* module = img();
    // Negative dimensions overflow allocation size and fail.
    CHECK(expectException([&] { module->newImageData(-1, 4); }));
    CHECK(expectException([&] { module->newImageData(4, -1); }));
}

TEST_CASE("image.newImageDataFromData.garbage") {
    auto* module = img();
    const char garbage[] = "this is not encoded image data";
    eve::data::ByteData bytes(garbage, sizeof(garbage) - 1);
    CHECK(expectException([&] { module->newImageData(&bytes); }));
}

TEST_CASE("image.isCompressed.nonCompressed") {
    auto* module = img();
    const char plain[] = "plain rgba bytes, not dds/ktx/pvr";
    eve::data::ByteData bytes(plain, sizeof(plain) - 1);
    CHECK(!module->isCompressed(&bytes));
}

TEST_CASE("image.newImageData.withBuffer") {
    auto* module = img();
    unsigned char raw[8] = {10, 20, 30, 40, 50, 60, 70, 80};
    std::unique_ptr<eve::image::ImageData> copied(
        module->newImageData(2, 1, "RGBA8", raw, false));
    REQUIRE(copied.get() != nullptr);
    CHECK(std::memcmp(copied->getData(), raw, sizeof(raw)) == 0);
    raw[0] = 99;
    CHECK(static_cast<unsigned char*>(copied->getData())[0] == 10);

    unsigned char* owned = new unsigned char[4]{1, 2, 3, 4};
    std::unique_ptr<eve::image::ImageData> owning(
        module->newImageData(1, 1, "RGBA8", owned, true));
    REQUIRE(owning.get() != nullptr);
    CHECK(std::memcmp(owning->getData(), owned, 4) == 0);
}

TEST_CASE("image.cloneAndPaste") {
    auto* module = img();
    std::unique_ptr<eve::image::ImageData> src(module->newImageData(3, 2));
    Colorf red{1.0f, 0.0f, 0.0f, 1.0f};
    src->setPixel(2, 1, red);

    std::unique_ptr<eve::image::ImageData> clone(src->clone());
    REQUIRE(clone.get() != nullptr);
    CHECK(nearColor(clone->getPixel(2, 1), red));

    std::unique_ptr<eve::image::ImageData> dst(module->newImageData(4, 3));
    dst->paste(src.get(), 0, 0, 0, 0, src->getWidth(), src->getHeight());
    CHECK(nearColor(dst->getPixel(2, 1), red));
    CHECK(eve::image::ImageData::canPaste("RGBA8", "RGBA8"));
    CHECK(!eve::image::ImageData::canPaste("R8", "RGBA8"));
    // Avoid CHECK(fn != nullptr): zeroerr cannot pretty-print function pointers on AppleClang.
    const bool hasSetFn = eve::image::ImageData::getPixelSetFunction("RGBA8") != nullptr;
    const bool hasGetFn = eve::image::ImageData::getPixelGetFunction("RGBA8") != nullptr;
    CHECK(hasSetFn);
    CHECK(hasGetFn);
}

TEST_CASE("image.encode.pngRoundTrip") {
    auto* module = img();
    std::unique_ptr<eve::image::ImageData> src(module->newImageData(2, 2));
    Colorf red{1.0f, 0.0f, 0.0f, 1.0f};
    src->setPixel(1, 1, red);

    std::unique_ptr<eve::filesystem::FileData> encoded(
        src->encode(medialoader::FormatHandler::ENCODED_PNG, "ut_roundtrip.png", false));
    REQUIRE(encoded.get() != nullptr);
    CHECK(encoded->getSize() > 0u);

    std::unique_ptr<eve::image::ImageData> decoded(module->newImageData(encoded.get()));
    REQUIRE(decoded.get() != nullptr);
    CHECK_EQ(decoded->getWidth(), 2);
    CHECK_EQ(decoded->getHeight(), 2);
    CHECK(nearColor(decoded->getPixel(1, 1), red));
}

TEST_CASE("image.encode.unsupportedFormatThrows") {
    auto* module = img();
    std::unique_ptr<eve::image::ImageData> src(module->newImageData(2, 2, "RGBA32F"));
    // No encoder currently supports encoding float pixel data into PNG.
    CHECK(expectException([&] {
        src->encode(medialoader::FormatHandler::ENCODED_PNG, "ut_bad.png", false);
    }));
}

TEST_CASE("image.newCubeFaces.crossLayout") {
    auto* module = img();
    // 3x4 "+" cross layout, one texel per face; see Image::newCubeFaces mapping.
    std::unique_ptr<eve::image::ImageData> src(module->newImageData(3, 4));
    struct Face { int x, y; Colorf color; };
    const Face faces[] = {
        {1, 1, {1.0f, 0.0f, 0.0f, 1.0f}},  // +x
        {1, 3, {0.0f, 1.0f, 0.0f, 1.0f}},  // -x
        {1, 0, {0.0f, 0.0f, 1.0f, 1.0f}},  // +y
        {1, 2, {1.0f, 1.0f, 0.0f, 1.0f}},  // -y
        {0, 1, {1.0f, 0.0f, 1.0f, 1.0f}},  // +z
        {2, 1, {0.0f, 1.0f, 1.0f, 1.0f}},  // -z
    };
    for (const auto& f : faces) src->setPixel(f.x, f.y, f.color);

    auto result = module->newCubeFaces(src.get());
    REQUIRE(result.size() == 6u);
    for (size_t i = 0; i < 6; ++i) {
        CHECK_EQ(result[i]->getWidth(), 1);
        CHECK_EQ(result[i]->getHeight(), 1);
        CHECK(nearColor(result[i]->getPixel(0, 0), faces[i].color));
    }
}

TEST_CASE("image.newCubeFaces.invalidDimensionsThrows") {
    auto* module = img();
    std::unique_ptr<eve::image::ImageData> src(module->newImageData(5, 7));
    CHECK(expectException([&] { module->newCubeFaces(src.get()); }));
}

TEST_CASE("image.rotate.ninetyDegreesNearest") {
    auto* module = img();
    // 3x2 with a unique corner marker at (2,0).
    std::unique_ptr<eve::image::ImageData> src(module->newImageData(3, 2));
    Colorf red{1.0f, 0.0f, 0.0f, 1.0f};
    Colorf green{0.0f, 1.0f, 0.0f, 1.0f};
    src->setPixel(2, 0, red);
    src->setPixel(0, 1, green);

    // +90° with Math::rotate2* / Y-down → clockwise on screen.
    // Corner (2,0) relative to center (1.5,1.0): (+0.5,-1.0) → (+1.0,+0.5)
    // → destination pixel center ≈ (dstCx+1, dstCy+0.5).
    std::unique_ptr<eve::image::ImageData> rotated(
        src->rotate(float(M_PI) * 0.5f, "nearest", true));
    REQUIRE(rotated.get() != nullptr);
    // Expanded AABB of 3x2 at 90° is 2x3.
    CHECK_EQ(rotated->getWidth(), 2);
    CHECK_EQ(rotated->getHeight(), 3);

    // Find red: should land near the bottom of the tall canvas after clockwise 90°.
    int redCount = 0, greenCount = 0;
    int redX = -1, redY = -1, greenX = -1, greenY = -1;
    for (int y = 0; y < rotated->getHeight(); ++y) {
        for (int x = 0; x < rotated->getWidth(); ++x) {
            Colorf p = rotated->getPixel(x, y);
            if (nearColor(p, red)) {
                ++redCount;
                redX = x;
                redY = y;
            }
            if (nearColor(p, green)) {
                ++greenCount;
                greenX = x;
                greenY = y;
            }
        }
    }
    CHECK_EQ(redCount, 1);
    CHECK_EQ(greenCount, 1);
    // Clockwise 90°: former top-right becomes bottom-ish; former bottom-left becomes top-ish.
    CHECK(redY > greenY);
    (void)redX;
    (void)greenX;
}

TEST_CASE("image.rotate.identityAndExpandFalse") {
    auto* module = img();
    std::unique_ptr<eve::image::ImageData> src(module->newImageData(4, 4));
    Colorf c{0.2f, 0.4f, 0.6f, 1.0f};
    src->setPixel(1, 2, c);

    std::unique_ptr<eve::image::ImageData> same(
        src->rotate(0.f, "nearest", false));
    REQUIRE(same.get() != nullptr);
    CHECK_EQ(same->getWidth(), 4);
    CHECK_EQ(same->getHeight(), 4);
    CHECK(nearColor(same->getPixel(1, 2), c));

    // 45° without expand keeps size; marker stays near center area.
    std::unique_ptr<eve::image::ImageData> clipped(
        src->rotate(float(M_PI) * 0.25f, "nearest", false));
    REQUIRE(clipped.get() != nullptr);
    CHECK_EQ(clipped->getWidth(), 4);
    CHECK_EQ(clipped->getHeight(), 4);
}

TEST_CASE("image.rotate.bilinearAndBadFilter") {
    auto* module = img();
    std::unique_ptr<eve::image::ImageData> src(module->newImageData(2, 2));
    Colorf white{1.0f, 1.0f, 1.0f, 1.0f};
    src->setPixel(0, 0, white);
    src->setPixel(1, 0, white);
    src->setPixel(0, 1, white);
    src->setPixel(1, 1, white);

    std::unique_ptr<eve::image::ImageData> rotated(
        src->rotate(float(M_PI) * 0.25f, "linear", true));
    REQUIRE(rotated.get() != nullptr);
    CHECK(rotated->getWidth() >= 2);
    CHECK(rotated->getHeight() >= 2);

    // At least one opaque-ish sample should survive bilinear sampling.
    bool anyLit = false;
    for (int y = 0; y < rotated->getHeight() && !anyLit; ++y) {
        for (int x = 0; x < rotated->getWidth(); ++x) {
            if (rotated->getPixel(x, y).a > 0.1f) {
                anyLit = true;
                break;
            }
        }
    }
    CHECK(anyLit);

    CHECK(expectException([&] { src->rotate(0.1f, "cubic", true); }));
}
