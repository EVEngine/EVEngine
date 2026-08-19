#pragma once

#include "common/Module.h"

#include <vector>
#include <limits>

namespace eve::touch
{

class Touch : public Module
{
public:
	Module_REG(Touch);
	struct TouchInfo
	{
		int64_t id;  // Identifier. Only unique for the duration of the touch-press.
		double x;  // Position in pixels along the x-axis.
		double y;  // Position in pixels along the y-axis.
		double dx; // Amount in pixels moved along the x-axis.
		double dy; // Amount in pixels moved along the y-axis.
		double pressure;
	};

	virtual ~Touch() {}

	/**
	 * @brief Gets all currently active touches.
	 **/
	virtual const std::vector<TouchInfo> &getTouches() const = 0;

	/**
	 * @brief Gets a specific touch, using its ID.
	 **/
	virtual const TouchInfo &getTouch(int64_t id) const = 0;

	int getTouchCount() const { return int(getTouches().size()); }
	double getTouchX(int index) const;
	double getTouchY(int index) const;

}; // Touch

} // eve::touch


