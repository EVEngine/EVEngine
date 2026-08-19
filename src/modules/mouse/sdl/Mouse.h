#pragma once
#include <map>

#include "mouse/Mouse.h"
#include "mouse/sdl/Cursor.h"


namespace eve::mouse::sdl {

/** @brief SDL 鼠标后端实现。 */
class Mouse : public eve::mouse::Mouse {
public:
    Mouse();
    virtual ~Mouse();

    eve::mouse::Cursor *newCursor(eve::image::ImageData *data, int hotx, int hoty) override;
    eve::mouse::Cursor *getSystemCursor(std::string cursortype) override;

    void setCursor(eve::mouse::Cursor *cursor) override;
    void setCursor() override;

    eve::mouse::Cursor *getCursor() const override;

    bool isCursorSupported() const override;

    double getX() const override;
    double getY() const override;
    void   getPosition(double &x, double &y) const override;
    void   setX(double x) override;
    void   setY(double y) override;
    void   setPosition(double x, double y) override;
    void   setVisible(bool visible) override;
    bool   isDown(const std::vector<int> &buttons) const override;
    bool   isVisible() const override;
    void   setGrabbed(bool grab) override;
    bool   isGrabbed() const override;
    bool   setRelativeMode(bool relative) override;
    bool   getRelativeMode() const override;

private:
    eve::mouse::Cursor *            curCursor;
    std::map<std::string, Cursor *> systemCursors;
};  // Mouse

}  // namespace eve::mouse::sdl
