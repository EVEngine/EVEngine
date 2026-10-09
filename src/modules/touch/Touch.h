#pragma once

#include "common/Export.h"
#include "common/Module.h"

#include <vector>
#include <limits>

namespace eve::touch
{

/**
 * @brief Active touch-point query interface (positions in window pixels).
 */
class EVENGINE_API_BACKENDS Touch : public Module {
public:
	Module_REG(Touch);
	/** @brief One active contact; id is unique only for the duration of the press. */
	struct TouchInfo
	{
		int64_t id;  // Identifier. Only unique for the duration of the touch-press.
		double x;  // Position in pixels along the x-axis.
		double y;  // Position in pixels along the y-axis.
		double dx; // Amount in pixels moved along the x-axis.
		double dy; // Amount in pixels moved along the y-axis.
		double pressure;
	};

	/** @brief Releases touch tracking state. */
	virtual ~Touch() {}

	/**
	 * @brief Gets all currently active touches.
	 **/
	virtual const std::vector<TouchInfo> &getTouches() const = 0;

	/**
	 * @brief Gets a specific touch, using its ID.
	 **/
	virtual const TouchInfo &getTouch(int64_t id) const = 0;

	/** @brief Number of currently active touches. */
	int getTouchCount() const { return int(getTouches().size()); }
	/** @brief X position of the touch at dense index, or NaN if out of range. */
	double getTouchX(int index) const;
	/** @brief Y position of the touch at dense index, or NaN if out of range. */
	double getTouchY(int index) const;

};  // Touch

} // eve::touch
