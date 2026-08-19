#pragma once

#include "touch/Touch.h"


namespace eve::touch::sdl
{

class Touch : public eve::touch::Touch
{
public:
	virtual ~Touch() {}

	const std::vector<TouchInfo> &getTouches() const override;
	const TouchInfo &getTouch(int64_t id) const override;

	// SDL has functions to query the state of touch presses, but unfortunately
	// they are updated on a different thread in some backends, which causes
	// issues especially if the user is iterating through the current touches
	// when they're updated. So we only update our touch press state in
	// love::event::sdl::Event::convert.
	void onEvent(uint32_t eventtype, const TouchInfo &info);

private:

	// All current touches.
	std::vector<TouchInfo> touches;

}; // Touch

} // eve::touch::sdl


