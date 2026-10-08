#pragma once

namespace eve::graphics {
/** @brief Narrow solid-rectangle draw boundary for UI/debug tooling. */
class ISolidRectRenderer {
public:
    /** @brief Releases ISolidRectRenderer resources. */
    virtual ~ISolidRectRenderer() = default;
    /** @brief Draws solid rect rgba. */
    virtual void drawSolidRectRGBA(float x, float y, float width, float height,
                                   float red, float green, float blue, float alpha = 1.0F) = 0;
};
}  // namespace eve::graphics
