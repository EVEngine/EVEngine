#pragma once

#include "common/Export.h"

#include <memory>
#include <string>

namespace eve::image {
class ImageData;
}

namespace eve::ui {

/**
 * @brief Parsed Android-style .9.png stretch and content metadata.
 *
 * Source images contain a one-pixel marker frame. The top and left marker
 * runs define the stretchable center; bottom and right runs define the content
 * rectangle. All values below address the cropped image, excluding markers.
 */
struct NinePatchInfo {
    int width = 0;
    int height = 0;
    int borderLeft = 0;
    int borderTop = 0;
    int borderRight = 0;
    int borderBottom = 0;
    int paddingLeft = 0;
    int paddingTop = 0;
    int paddingRight = 0;
    int paddingBottom = 0;
};

/**
 * @brief Parse a raw, uncompiled Android .9.png image.
 * @param source Decoded RGBA8 image including the one-pixel marker frame.
 * @param out Parsed stretch borders and content padding.
 * @param error Optional diagnostic on failure.
 * @return True when the marker frame is valid and has one contiguous stretch
 * run on both the top and left edges.
 */
EVENGINE_API bool parseNinePatch(const image::ImageData &source, NinePatchInfo &out,
                                 std::string *error = nullptr);

/**
 * @brief Copy the drawable interior of a parsed .9.png without marker pixels.
 * @return Caller-owned cropped image, or nullptr if dimensions are invalid.
 */
EVENGINE_API std::unique_ptr<image::ImageData> stripNinePatchBorder(
    const image::ImageData &source);

}  // namespace eve::ui
