#pragma once

#include <string>

namespace eve::image {
class ImageData;
}  // namespace eve::image

namespace eve::stylize {

/** CPU NPR helpers — returns a new RGBA8 ImageData (caller owns). */
image::ImageData *processImageCpu(image::ImageData *src, const std::string &style);

}  // namespace eve::stylize
