#pragma once

#include "touch/Touch.h"


namespace eve::touch::sdl
{

/** @brief SDL 触摸后端实现（状态由事件驱动更新）。 */
class Touch : public eve::touch::Touch
{
public:
	virtual ~Touch() {}

	const std::vector<TouchInfo> &getTouches() const override;
	const TouchInfo &getTouch(int64_t id) const override;

	/** @brief 由事件转换器在 SDL 事件回调中更新触点状态（见事件模块）。 */
	void onEvent(uint32_t eventtype, const TouchInfo &info);

private:

	// All current touches.
	std::vector<TouchInfo> touches;

}; // Touch

} // eve::touch::sdl


