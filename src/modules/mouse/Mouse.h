#pragma once

#include "common/Module.h"
#include "mouse/Cursor.h"

#include <vector>


namespace eve::image {
class ImageData;
}

namespace eve::mouse
{

class Mouse : public Module
{
public:
    Module_REG(Mouse);

	virtual ~Mouse();

	virtual Cursor *newCursor(eve::image::ImageData *data, int hotx, int hoty) = 0;
	virtual Cursor *getSystemCursor(std::string cursortype) = 0;

	virtual void setCursor(Cursor *cursor) = 0;
	virtual void setCursor() = 0;

	virtual Cursor *getCursor() const = 0;

	virtual bool isCursorSupported() const = 0;

	virtual double getX() const = 0;
	virtual double getY() const = 0;
	virtual void getPosition(double &x, double &y) const = 0;
	virtual void setX(double x) = 0;
	virtual void setY(double y) = 0;
	virtual void setPosition(double x, double y) = 0;
	virtual void setVisible(bool visible) = 0;
	virtual bool isDown(const std::vector<int> &buttons) const = 0;
	virtual bool isVisible() const = 0;
	virtual void setGrabbed(bool grab) = 0;
	virtual bool isGrabbed() const = 0;
	virtual bool setRelativeMode(bool relative) = 0;
	virtual bool getRelativeMode() const = 0;

}; // Mouse

} // eve::mouse


