#include "ui/NinePatch.h"

#include "image/ImageData.h"

#include <algorithm>
#include <utility>

namespace eve::ui {
namespace {

using Color = image::ImageData::Colorf;

bool marker(const Color &color) {
    return color.a >= 0.5f && color.r <= 0.1f && color.g <= 0.1f &&
           color.b <= 0.1f;
}

bool transparent(const Color &color) { return color.a <= 0.1f; }

struct MarkerRun {
    int first = -1;
    int last = -1;
};

template <typename PixelAt>
bool scanRun(int count, PixelAt pixelAt, bool required, const char *edge,
             MarkerRun &run, std::string *error) {
    bool ended = false;
    for (int i = 0; i < count; ++i) {
        const Color color = pixelAt(i);
        if (marker(color)) {
            if (ended) {
                if (error) *error = std::string(edge) + " has multiple stretch runs";
                return false;
            }
            if (run.first < 0) run.first = i;
            run.last = i;
        } else {
            if (!transparent(color)) {
                if (error) *error = std::string(edge) + " contains a non-marker pixel";
                return false;
            }
            if (run.first >= 0) ended = true;
        }
    }
    if (required && run.first < 0) {
        if (error) *error = std::string(edge) + " is missing a stretch marker";
        return false;
    }
    return true;
}

}  // namespace

bool parseNinePatch(const image::ImageData &source, NinePatchInfo &out,
                    std::string *error) {
    out = {};
    if (source.getFormat() != "RGBA8") {
        if (error) *error = ".9.png source must decode as RGBA8";
        return false;
    }
    if (source.getWidth() < 3 || source.getHeight() < 3) {
        if (error) *error = ".9.png must include a one-pixel frame around content";
        return false;
    }

    const int width = source.getWidth() - 2;
    const int height = source.getHeight() - 2;
    for (const auto &[x, y] : {std::pair{0, 0}, std::pair{source.getWidth() - 1, 0},
                              std::pair{0, source.getHeight() - 1},
                              std::pair{source.getWidth() - 1,
                                        source.getHeight() - 1}}) {
        if (!transparent(source.getPixel(x, y))) {
            if (error) *error = ".9.png corner marker pixels must be transparent";
            return false;
        }
    }

    MarkerRun stretchX;
    MarkerRun stretchY;
    MarkerRun contentX;
    MarkerRun contentY;
    if (!scanRun(width, [&](int i) { return source.getPixel(i + 1, 0); }, true,
                 "top edge", stretchX, error) ||
        !scanRun(height, [&](int i) { return source.getPixel(0, i + 1); }, true,
                 "left edge", stretchY, error) ||
        !scanRun(width,
                 [&](int i) { return source.getPixel(i + 1, source.getHeight() - 1); },
                 false, "bottom edge", contentX, error) ||
        !scanRun(height,
                 [&](int i) { return source.getPixel(source.getWidth() - 1, i + 1); },
                 false, "right edge", contentY, error))
        return false;

    out.width = width;
    out.height = height;
    out.borderLeft = stretchX.first;
    out.borderRight = width - stretchX.last - 1;
    out.borderTop = stretchY.first;
    out.borderBottom = height - stretchY.last - 1;
    out.paddingLeft = contentX.first >= 0 ? contentX.first : out.borderLeft;
    out.paddingRight = contentX.first >= 0 ? width - contentX.last - 1 : out.borderRight;
    out.paddingTop = contentY.first >= 0 ? contentY.first : out.borderTop;
    out.paddingBottom = contentY.first >= 0 ? height - contentY.last - 1
                                            : out.borderBottom;
    return true;
}

std::unique_ptr<image::ImageData> stripNinePatchBorder(
    const image::ImageData &source) {
    if (source.getWidth() < 3 || source.getHeight() < 3) return nullptr;
    auto result = std::make_unique<image::ImageData>(source.getWidth() - 2,
                                                     source.getHeight() - 2,
                                                     source.getFormat());
    result->paste(const_cast<image::ImageData *>(&source), 0, 0, 1, 1,
                  source.getWidth() - 2, source.getHeight() - 2);
    return result;
}

}  // namespace eve::ui
