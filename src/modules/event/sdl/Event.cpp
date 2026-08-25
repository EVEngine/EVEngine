
#include "Event.h"

#include "common/Capability.h"
#include "common/Exception.h"
#include "common/Module.h"
#include "common/Profile.h"
#include "common/config.h"
#include "event/PlatformEventSink.h"

#include <SDL2/SDL.h>

namespace eve::event::sdl
{

Event::Event()
{
	if (SDL_InitSubSystem(SDL_INIT_EVENTS) < 0)
		throw eve::Exception("Could not initialize SDL events subsystem (%s)", SDL_GetError());
	wakeEventType_ = SDL_RegisterEvents(1);
	if (wakeEventType_ == static_cast<Uint32>(-1)) {
		SDL_QuitSubSystem(SDL_INIT_EVENTS);
		throw eve::Exception("Could not reserve SDL wake event (%s)", SDL_GetError());
	}
}

Event::~Event()
{
	SDL_QuitSubSystem(SDL_INIT_EVENTS);
}

void Event::pump()
{
	EV_PROFILE_MODULE("event", "Event::pump");
	SDL_Event e;

	while (SDL_PollEvent(&e))
	{
		Message *msg = convert(e);
		if (msg)
			push(msg);
		// Ownership stays in the queue until poll()/clear() — do not delete here.
	}

	// Per-frame work that modules hang off the pump (audio streaming, ...).
	cap::forEach<IPlatformEventSink>([](IPlatformEventSink *sink) { sink->onPumpFinished(); });
}

Message *Event::wait()
{
	for (;;) {
		if (Message *queued = poll())
			return queued;

		SDL_Event e;
		if (SDL_WaitEvent(&e) != 1)
			return nullptr;
		if (e.type == wakeEventType_)
			continue;

		if (Message *message = convert(e))
			return message;
	}
}

void Event::wakeWaiters()
{
	SDL_Event e{};
	e.type = wakeEventType_;
	e.user.type = wakeEventType_;
	e.user.data1 = this;
	SDL_PushEvent(&e);
}

void Event::clear()
{
	SDL_Event e;

	while (SDL_PollEvent(&e))
	{
		// Do nothing with 'e' ...
	}

	eve::event::Event::clear();
}

Message *Event::convert(const SDL_Event &e)
{
	// Modules that own an event family translate it themselves; see
	// event/PlatformEventSink.h. Only window-close and quit are handled here,
	// because they are about the event loop rather than any one module.
	Message *translated = nullptr;
	const bool consumed = cap::forEachUntil<IPlatformEventSink>([&](IPlatformEventSink *sink) {
		if (sink->observePlatformEvent(&e))
			return true;
		translated = sink->translatePlatformEvent(&e);
		return translated != nullptr;
	});
	if (translated)
		return translated;
	if (consumed)
		return nullptr;

	switch (e.type) {
	case SDL_QUIT:
		return new Message("quit");
	case SDL_WINDOWEVENT:
		if (e.window.event == SDL_WINDOWEVENT_CLOSE)
			return new Message("quit");
		break;
	default:
		break;
	}
	return nullptr;
}

}
