#pragma once

#include "graphics/Color.h"
#include "graphics/Drawable.h"

#include <optional>

namespace eve::image {
class ImageData;
}

namespace eve::graphics {

class Texture;

class Canvas : public Drawable {
public:
    // Canvas adds a canvas-to-canvas composite overload; keep the base
    // Drawable::draw(Graphics*, mat4) visible (it is overridden by the
    // concrete canvas implementations).
    using Drawable::draw;

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
