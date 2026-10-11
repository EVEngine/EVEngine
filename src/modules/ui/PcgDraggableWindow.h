#pragma once
#include "common/Result.h"
#include "ui/PcgUiStatus.h"
namespace ssq { class Table; }
namespace eve::ui {
/** @brief Caller-owned state for Pcg's draggable photo-mode window. */
class PcgDraggableWindow {
public:
    /** @brief Configure geometry and remember the exact reset position atomically. */
    [[nodiscard]] Result<void> configure(float x, float y, float width, float height,
                                         float screenWidth, float screenHeight, float canvasScale);
    /** @brief Apply one pointer delta divided by canvas scale and clamp to Pcg bounds. */
    [[nodiscard]] Result<void> drag(float deltaX, float deltaY);
    /** @brief Process pointer-down; middleButtonDown restores the initial position. */
    void pointerDown(bool middleButtonDown) noexcept;
    /** @brief Consume the request to move the window to the front. */
    PcgUiRequestStatus consumeBringToFront() noexcept;
    /** @brief Return current anchored X. */ float getX() const noexcept { return x_; }
    /** @brief Return current anchored Y. */ float getY() const noexcept { return y_; }
private:
    float x_=0.f,y_=0.f,resetX_=0.f,resetY_=0.f,width_=0.f,height_=0.f,screenWidth_=0.f,screenHeight_=0.f,scale_=1.f;
    bool configured_=false,bringToFront_=false;
};
/** @brief Register Pcg draggable-window bindings. */
void exposePcgDraggableWindowBindings(ssq::Table& table);
}
