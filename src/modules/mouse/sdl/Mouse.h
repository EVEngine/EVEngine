#pragma once
#include <map>

#include "mouse/Mouse.h"
#include "mouse/sdl/Cursor.h"


namespace eve::mouse::sdl {

/** @brief SDL mouse backend implementing eve::mouse::Mouse. */
class Mouse : public eve::mouse::Mouse {
public:
    /** @brief Constructs the SDL mouse module with default cursor state. */
    Mouse();
    /** @brief Releases cached system cursors. */
    virtual ~Mouse();

    /** @brief @copydoc eve::mouse::Mouse::newCursor */
    eve::mouse::Cursor *newCursor(eve::image::ImageData *data, int hotx, int hoty) override;
    /** @brief @copydoc eve::mouse::Mouse::getSystemCursor */
    eve::mouse::Cursor *getSystemCursor(std::string cursortype) override;

    /** @brief @copydoc eve::mouse::Mouse::setCursor */
    void setCursor(eve::mouse::Cursor *cursor) override;
    /** @brief @copydoc eve::mouse::Mouse::setCursor */
    void setCursor() override;

    /** @brief @copydoc eve::mouse::Mouse::getCursor */
    eve::mouse::Cursor *getCursor() const override;

    /** @brief @copydoc eve::mouse::Mouse::isCursorSupported */
    bool isCursorSupported() const override;

    /** @brief @copydoc eve::mouse::Mouse::getX */
    double getX() const override;
    /** @brief @copydoc eve::mouse::Mouse::getY */
    double getY() const override;
    /** @brief @copydoc eve::mouse::Mouse::getPosition */
    void   getPosition(double &x, double &y) const override;
    /** @brief @copydoc eve::mouse::Mouse::setX */
    void   setX(double x) override;
    /** @brief @copydoc eve::mouse::Mouse::setY */
    void   setY(double y) override;
    /** @brief @copydoc eve::mouse::Mouse::setPosition */
    void   setPosition(double x, double y) override;
    /** @brief @copydoc eve::mouse::Mouse::setVisible */
    void   setVisible(bool visible) override;
    /** @brief @copydoc eve::mouse::Mouse::isDown */
    bool   isDown(const std::vector<int> &buttons) const override;
    /** @brief @copydoc eve::mouse::Mouse::isVisible */
    bool   isVisible() const override;
    /** @brief @copydoc eve::mouse::Mouse::setGrabbed */
    void   setGrabbed(bool grab) override;
    /** @brief @copydoc eve::mouse::Mouse::isGrabbed */
    bool   isGrabbed() const override;
    /** @brief @copydoc eve::mouse::Mouse::setRelativeMode */
    bool   setRelativeMode(bool relative) override;
    /** @brief @copydoc eve::mouse::Mouse::getRelativeMode */
    bool   getRelativeMode() const override;
    /** @brief @copydoc eve::mouse::Mouse::getMovementX */
    double getMovementX() override;
    /** @brief @copydoc eve::mouse::Mouse::getMovementY */
    double getMovementY() const override;

private:
    eve::mouse::Cursor *            curCursor;
    std::map<std::string, Cursor *> systemCursors;
    int                             lastRelY = 0;
};  // Mouse

}  // namespace eve::mouse::sdl
