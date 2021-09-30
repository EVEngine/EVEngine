#pragma once

#include "Cursor.h"
#include "common/Module.h"
#include "image/ImageData.h"

// C++
#include <vector>

namespace eve
{
namespace mouse
{

class Mouse : public Module
{
public:

	virtual ~Mouse() {}

	// Implements Module.
	virtual std::string getName() const { return "mouse"; }

	virtual Cursor *newCursor(image::ImageData *data, int hotx, int hoty) = 0;
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

} // mouse
} // eve


