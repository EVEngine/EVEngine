#pragma once

namespace eve::graphics {

class Canvas;

/** @brief Narrow offscreen-Canvas allocation boundary for tools and optional modules. */
class ICanvasFactory {
public:
    virtual ~ICanvasFactory() = default;
    /** @brief Allocate a backend-owned sampleable Canvas with positive dimensions. */
    virtual Canvas* newCanvas(int width, int height) = 0;
};

}  // namespace eve::graphics
