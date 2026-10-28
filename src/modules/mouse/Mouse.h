#pragma once

#include "common/Export.h"
#include "common/Module.h"
#include "mouse/Cursor.h"

#include <vector>


namespace eve::image {
class ImageData;
}

namespace eve::mouse
{

/**
 * @brief Queries and manipulates the mouse cursor; creates custom cursors.
 */
class EVENGINE_API_BACKENDS Mouse : public Module {
public:
    Module_REG(Mouse);

	/** @brief Releases cursors owned by this module. */
	virtual ~Mouse();

	/** @brief Builds a custom cursor from image pixels with hotspot (hotx, hoty). */
	virtual Cursor *newCursor(eve::image::ImageData *data, int hotx, int hoty) = 0;
	/** @brief Returns a named system cursor (e.g. "arrow", "ibeam"); may cache. */
	virtual Cursor *getSystemCursor(std::string cursortype) = 0;

	/** @brief Installs a cursor as the active OS cursor. */
	virtual void setCursor(Cursor *cursor) = 0;
	/** @brief Restores the default system arrow cursor. */
	virtual void setCursor() = 0;

	/** @brief Currently installed cursor, or null for the default. */
	virtual Cursor *getCursor() const = 0;

	/** @brief True when the platform supports software/hardware cursors. */
	virtual bool isCursorSupported() const = 0;

	/** @brief Cursor X in window logical coordinates. */
	virtual double getX() const = 0;
	/** @brief Cursor Y in window logical coordinates. */
	virtual double getY() const = 0;
	/** @brief Writes cursor position in window logical coordinates. */
	virtual void getPosition(double &x, double &y) const = 0;
	/** @brief Warps the cursor X while keeping Y. */
	virtual void setX(double x) = 0;
	/** @brief Warps the cursor Y while keeping X. */
	virtual void setY(double y) = 0;
	/** @brief Warps the cursor to a logical window position. */
	virtual void setPosition(double x, double y) = 0;
	/** @brief Shows or hides the OS cursor. */
	virtual void setVisible(bool visible) = 0;
	/** @brief True if any listed mouse button index is currently down. */
	virtual bool isDown(const std::vector<int> &buttons) const = 0;
	/** @brief True when the OS cursor is visible. */
	virtual bool isVisible() const = 0;
	/** @brief Confines the cursor to the window when grab is true. */
	virtual void setGrabbed(bool grab) = 0;
	/** @brief True when cursor grab is active. */
	virtual bool isGrabbed() const = 0;
	/** @brief Enables relative mouse mode (hidden cursor, motion deltas). */
	virtual bool setRelativeMode(bool relative) = 0;
	/** @brief True when relative mouse mode is active. */
	virtual bool getRelativeMode() const = 0;
	/** @brief Pixel delta since the last getMovementX() (also latches Y for getMovementY). */
	virtual double getMovementX() = 0;
	/** @brief Pixel delta latched by the most recent getMovementX(); call X then Y each frame. */
	virtual double getMovementY() const = 0;

        /** @brief Horizontal scroll from the latest platform pump; event-thread read, non-consuming. */
        float getWheelX() const;
        /** @brief Vertical scroll from the latest platform pump; event-thread read, non-consuming. */
        float getWheelY() const;

};  // Mouse

} // eve::mouse
