
#include "Event.h"

#include "filesystem/Filesystem.h"
#include "graphics/Graphics.h"
#include "window/Window.h"
#include "common/Exception.h"
#include "common/config.h"

#include <SDL2/SDL.h>
#include <cmath>

namespace eve::event::sdl
{

// SDL reports mouse coordinates in the window coordinate system in OS X, but
// we want them in pixel coordinates (may be different with high-DPI enabled.)
static void windowToDPICoords(double *x, double *y)
{
	auto window = Module::getInstance<window::Window>();
	// if (window)
	// 	window->windowToDPICoords(x, y);
}

#ifndef EVE_MACOSX
static void normalizedToDPICoords(double *x, double *y)
{
	double w = 1.0, h = 1.0;

	auto window = Module::getInstance<window::Window>();
	if (window)
	{
		w = window->getWidth();
		h = window->getHeight();
		// window->windowToDPICoords(&w, &h);
	}

	if (x)
		*x = ((*x) * w);
	if (y)
		*y = ((*y) * h);
}
#endif

// SDL's event watch callbacks trigger when the event is actually posted inside
// SDL, unlike with SDL_PollEvents. This is useful for some events which require
// handling inside the function which triggered them on some backends.
static int SDLCALL watchAppEvents(void * /*udata*/, SDL_Event *event)
{
	auto gfx = Module::getInstance<graphics::Graphics>();

	switch (event->type)
	{
	// On iOS, calling any OpenGL ES function after the function which triggers
	// SDL_APP_DIDENTERBACKGROUND is called will kill the app, so we handle it
	// with an event watch callback, which will be called inside that function.
	case SDL_APP_DIDENTERBACKGROUND:
	case SDL_APP_WILLENTERFOREGROUND:
		// if (gfx)
		// 	gfx->setActive(event->type == SDL_APP_WILLENTERFOREGROUND);
		break;
	default:
		break;
	}

	return 1;
}


Event::Event()
{
	if (SDL_InitSubSystem(SDL_INIT_EVENTS) < 0)
		throw eve::Exception("Could not initialize SDL events subsystem (%s)", SDL_GetError());

	SDL_AddEventWatch(watchAppEvents, this);
}

Event::~Event()
{
	SDL_DelEventWatch(watchAppEvents, this);
	SDL_QuitSubSystem(SDL_INIT_EVENTS);
}

void Event::pump()
{
	exceptionIfInRenderPass("eve.event.pump");

	SDL_Event e;

	while (SDL_PollEvent(&e))
	{
		Message *msg = convert(e);
		if (msg)
		{
			push(msg);
			delete msg;
		}
	}
}

Message *Event::wait()
{
	exceptionIfInRenderPass("eve.event.wait");

	SDL_Event e;

	if (SDL_WaitEvent(&e) != 1)
		return nullptr;

	return convert(e);
}

void Event::clear()
{
	exceptionIfInRenderPass("eve.event.clear");

	SDL_Event e;

	while (SDL_PollEvent(&e))
	{
		// Do nothing with 'e' ...
	}

	eve::event::Event::clear();
}

void Event::exceptionIfInRenderPass(const char *name)
{
	// Some core OS graphics functionality (e.g. swap buffers on some platforms)
	// happens inside SDL_PumpEvents - which is called by SDL_PollEvent and
	// friends. It's probably a bad idea to call those functions while a Canvas
	// is active.
	auto gfx = Module::getInstance<graphics::Graphics>();
	// if (gfx != nullptr && gfx->isCanvasActive())
	// 	throw eve::Exception("%s cannot be called while a Canvas is active in eve.graphics.", name);
}

Message *Event::convert(const SDL_Event &e)
{
	Message *msg = nullptr;

	std::vector<ssq::Object> vargs;
	vargs.reserve(4);

	return msg;
}

} 
