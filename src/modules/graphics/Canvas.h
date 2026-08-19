#pragma once

#include "graphics/Drawable.h"

#include <glm/vec4.hpp>
#include <optional>

namespace eve::image {
class ImageData;
}

namespace eve::graphics {

/**
 * @brief RGBA color, used by every graphics draw call.
 * Lives inside eve::graphics so including Canvas.h/Graphics.h cannot pollute
 * the global namespace (old code outside this namespace should either
 * qualify eve::graphics::Color or add a file-local using-declaration).
 */
using Color = glm::vec4;

class Texture;

class Canvas : public Drawable {
public:
    Canvas() {}
    ~Canvas() override {}

    virtual int getWidth() const = 0;
    virtual int getHeight() const = 0;

    /** @brief Sampleable color buffer; screen Canvas returns nullptr. */
    virtual Texture *getTexture() = 0;

    virtual void clear(std::optional<Color> color, std::optional<int> stencil,
                       std::optional<double> depth) = 0;

    virtual Color getPixel(int x, int y) = 0;

    /** Full RGBA8 copy; caller owns ImageData*. */
    virtual image::ImageData *newImageData() = 0;

    virtual void draw(Canvas *C, const glm::mat4 &matrix) const = 0;
};

}  // namespace eve::graphics
