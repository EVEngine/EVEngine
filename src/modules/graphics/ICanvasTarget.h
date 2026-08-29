#pragma once

namespace eve::graphics {
class Canvas;
/** @brief Narrow render-target binding boundary for optional tooling modules. */
class ICanvasTarget {
public:
    virtual ~ICanvasTarget() = default;
    /** @brief Bind an offscreen Canvas, or nullptr to restore the screen target. */
    virtual void setCanvas(Canvas* canvas) = 0;
    /** @brief Return the currently bound Canvas, or nullptr for the screen target. @return Borrowed pointer owned by the graphics backend. @lifetime Valid until the target is rebound or backend teardown. */
    virtual Canvas* getCanvas() const = 0;
};
}  // namespace eve::graphics
