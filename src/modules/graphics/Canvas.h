#pragma once

#include "graphics/Color.h"
#include "graphics/Drawable.h"

#include <optional>

namespace eve::image {
class ImageData;
}

namespace eve::graphics {

class Texture;

/** @brief Canvas public API. */
class Canvas : public Drawable {
public:
    // Canvas adds a canvas-to-canvas composite overload; keep the base
    // Drawable::draw(Graphics*, mat4) visible (it is overridden by the
    // concrete canvas implementations).
    using Drawable::draw;

    /** @brief Constructs a Canvas. */
    Canvas() {}
    /** @brief Releases Canvas resources. */
    ~Canvas() override {}

    /** @brief Returns the width. */
    virtual int getWidth() const = 0;
    /** @brief Returns the height. */
    virtual int getHeight() const = 0;

    /** @brief Sampleable color buffer; screen Canvas returns nullptr. */
    virtual Texture *getTexture() = 0;

    /** @brief Clears . */
    virtual void clear(std::optional<Color> color, std::optional<int> stencil,
                       std::optional<double> depth) = 0;

    /** @brief Returns the pixel. */
    virtual Color getPixel(int x, int y) = 0;

    /** Full RGBA8 copy; caller owns ImageData*. */
    /** @brief Creates a image data. @ownership Caller deletes unless documented otherwise. */
    virtual image::ImageData *newImageData() = 0;

    /**
     * @brief Copy an HDR Canvas as linear RGBA16F without tone mapping.
     * @return Caller-owned ImageData, or nullptr when this Canvas is not HDR.
     * @ownership The caller owns the returned image data.
     */
    virtual image::ImageData *newHDRImageData() { return nullptr; }

    /** @brief Draws . */
    virtual void draw(Canvas *C, const glm::mat4 &matrix) const = 0;
};

}  // namespace eve::graphics
