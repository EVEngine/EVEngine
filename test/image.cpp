#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "data/ByteData.h"
#include "image/Image.h"
#include "image/ImageData.h"

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
    CHECK(eve::image::ImageData::getPixelSetFunction("RGBA8") != nullptr);
    CHECK(eve::image::ImageData::getPixelGetFunction("RGBA8") != nullptr);
}
