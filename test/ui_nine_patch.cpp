#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "image/ImageData.h"
#include "ui/NinePatch.h"

#include <memory>
#include <string>

using eve::image::ImageData;
using eve::ui::NinePatchInfo;

namespace {

using Color = ImageData::Colorf;

void mark(ImageData &image, int x, int y) {
    image.setPixel(x, y, Color{0.f, 0.f, 0.f, 1.f});
}

}  // namespace

TEST_CASE("ui.nine_patch.parses_stretch_content_and_strips_marker_frame") {
    ImageData source(8, 7, "RGBA8");
    for (int y = 1; y < 6; ++y) {
        for (int x = 1; x < 7; ++x)
            source.setPixel(x, y, Color{0.2f, 0.4f, 0.8f, 1.f});
    }

    for (int x = 2; x <= 4; ++x) mark(source, x, 0);  // content x 1..3
    for (int y = 2; y <= 4; ++y) mark(source, 0, y);  // content y 1..3
    for (int x = 2; x <= 5; ++x) mark(source, x, 6);  // padding x 1..4
    for (int y = 3; y <= 5; ++y) mark(source, 7, y);  // padding y 2..4

    NinePatchInfo patchInfo;
    std::string error;
    REQUIRE(eve::ui::parseNinePatch(source, patchInfo, &error));
    CHECK(error.empty());
    CHECK_EQ(patchInfo.width, 6);
    CHECK_EQ(patchInfo.height, 5);
    CHECK_EQ(patchInfo.borderLeft, 1);
    CHECK_EQ(patchInfo.borderRight, 2);
    CHECK_EQ(patchInfo.borderTop, 1);
    CHECK_EQ(patchInfo.borderBottom, 1);
    CHECK_EQ(patchInfo.paddingLeft, 1);
    CHECK_EQ(patchInfo.paddingRight, 1);
    CHECK_EQ(patchInfo.paddingTop, 2);
    CHECK_EQ(patchInfo.paddingBottom, 0);

    std::unique_ptr<ImageData> cropped = eve::ui::stripNinePatchBorder(source);
    REQUIRE(bool(cropped));
    CHECK_EQ(cropped->getWidth(), 6);
    CHECK_EQ(cropped->getHeight(), 5);
    const Color pixel = cropped->getPixel(0, 0);
    CHECK(pixel.r > 0.19f);
    CHECK(pixel.b > 0.79f);
    CHECK(pixel.a > 0.99f);
}

TEST_CASE("ui.nine_patch.rejects_disjoint_stretch_runs") {
    ImageData source(7, 7, "RGBA8");
    mark(source, 1, 0);
    mark(source, 3, 0);
    mark(source, 0, 2);
    mark(source, 0, 3);

    NinePatchInfo patchInfo;
    std::string error;
    CHECK(!eve::ui::parseNinePatch(source, patchInfo, &error));
    CHECK(error.find("multiple stretch runs") != std::string::npos);
}
