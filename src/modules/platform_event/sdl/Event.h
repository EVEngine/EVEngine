#pragma once

#include "platform_event/PlatformEvent.h"

#include <SDL2/SDL_events.h>
#include <map>

namespace eve::platform_event::sdl
{

class Event final : public eve::platform_event::PlatformEvent
{
public:
	Event();
	virtual ~Event();

	/**
	 * @brief Pumps the event queue. This function gathers all the pending input information
	 * from devices and places it on the event queue. Normally not needed if you poll
	 * for events.
	 **/
	void pump() override;

	/**
	 * @brief Waits for the next event (indefinitely). Useful for creating games where
	 * the screen and game state only needs updating when the user interacts with
		 * the window.
		 * @ownership Ownership of the returned Message transfers to the caller.
		 * @lifetime Valid until caller deletion; independent of the native event.
		 * @thread Platform event thread only.
		 * @reentrancy Registered sinks run synchronously during conversion.
	 **/
	Message *wait() override;

	/**
	 * @brief Clears the event queue.
	 */
	void clear() override;

private:
	void wakeWaiters() override;
	/**
	 * @brief Offers the event to registered sinks, then handles quit itself.
	 * @ownership The SDL event is borrowed for this call; returned Message ownership transfers to the caller.
	 * @lifetime The input and any sink borrow are valid only during this call.
	 */
	Message *convert(const SDL_Event &e);
	Uint32 wakeEventType_ = static_cast<Uint32>(-1);

}; // Event

} // eve::platform_event::sdl

