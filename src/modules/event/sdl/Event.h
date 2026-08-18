#pragma once

#include "event/Event.h"

#include <SDL2/SDL_events.h>
#include <map>

namespace eve::event::sdl
{

class Event final : public eve::event::Event
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
	 **/
	Message *wait() override;

	/**
	 * @brief Clears the event queue.
	 */
	void clear() override;

private:
	void wakeWaiters() override;
	void exceptionIfInRenderPass(const char *name);
	Message *convert(const SDL_Event &e);
	Uint32 wakeEventType_ = static_cast<Uint32>(-1);

}; // Event

} // eve::event::sdl


