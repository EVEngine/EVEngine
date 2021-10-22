#include "mouse/sdl/Mouse.h"
#include "window/sdl/Window.h"

#include <SDL2/SDL_mouse.h>

namespace eve::mouse::sdl {

// SDL reports mouse coordinates in the window coordinate system in OS X, but
// we want them in pixel coordinates (may be different with high-DPI enabled.)
static void windowToDPICoords(double *x, double *y)
{
	auto window = getModInst(window,Window);
	// if (window)
	// 	window->windowToDPICoords(x, y);
}

// And vice versa for setting mouse coordinates.
static void DPIToWindowCoords(double *x, double *y)
{
	auto window = getModInst(window,Window);
	// if (window)
		// window->DPIToWindowCoords(x, y);
}


Mouse::Mouse()
	: curCursor(nullptr)
{
	// SDL may need the video subsystem in order to clean up the cursor when
	// quitting. Subsystems are reference-counted.
	SDL_InitSubSystem(SDL_INIT_VIDEO);
}

Mouse::~Mouse()
{
	if (curCursor)
		setCursor();

	for (auto &c : systemCursors)
		delete c.second;

	SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

eve::mouse::Cursor *Mouse::newCursor(eve::image::ImageData *data, int hotx, int hoty)
{
	return new Cursor(data, hotx, hoty);
}

eve::mouse::Cursor *Mouse::getSystemCursor(std::string cursortype)
{
	Cursor *cursor = nullptr;
	auto it = systemCursors.find(cursortype);

	if (it != systemCursors.end())
		cursor = it->second;
	else
	{
		cursor = new Cursor(cursortype);
		systemCursors[cursortype] = cursor;
	}

	return cursor;
}

void Mouse::setCursor(eve::mouse::Cursor *cursor)
{
	curCursor = cursor;
	SDL_SetCursor((SDL_Cursor *) cursor->getHandle());
}

void Mouse::setCursor()
{
	curCursor = nullptr;
	SDL_SetCursor(SDL_GetDefaultCursor());
}

eve::mouse::Cursor *Mouse::getCursor() const
{
	return curCursor;
}


bool Mouse::isCursorSupported() const
{
	return SDL_GetDefaultCursor() != nullptr;
}

double Mouse::getX() const
{
	int x;
	SDL_GetMouseState(&x, nullptr);

	double dx = (double) x;
	windowToDPICoords(&dx, nullptr);

	return dx;
}

double Mouse::getY() const
{
	int y;
	SDL_GetMouseState(nullptr, &y);

	double dy = (double) y;
	windowToDPICoords(nullptr, &dy);

	return dy;
}

void Mouse::getPosition(double &x, double &y) const
{
	int mx, my;
	SDL_GetMouseState(&mx, &my);

	x = (double) mx;
	y = (double) my;
	windowToDPICoords(&x, &y);
}

void Mouse::setPosition(double x, double y)
{
	auto window = getModInst(window,Window);

	SDL_Window *handle = nullptr;
	// if (window)
	// 	handle = (SDL_Window *) window->getHandle();

	DPIToWindowCoords(&x, &y);
	SDL_WarpMouseInWindow(handle, (int) x, (int) y);

	// SDL_WarpMouse doesn't directly update SDL's internal mouse state in Linux
	// and Windows, so we call SDL_PumpEvents now to make sure the next
	// getPosition call always returns the updated state.
	SDL_PumpEvents();
}

void Mouse::setX(double x)
{
	setPosition(x, getY());
}

void Mouse::setY(double y)
{
	setPosition(getX(), y);
}

void Mouse::setVisible(bool visible)
{
	SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);
}

bool Mouse::isDown(const std::vector<int> &buttons) const
{
	Uint32 buttonstate = SDL_GetMouseState(nullptr, nullptr);

	for (int button : buttons)
	{
		if (button <= 0)
			continue;

		// We use button index 2 to represent the right mouse button, but SDL
		// uses 2 to represent the middle mouse button.
		switch (button)
		{
		case 2:
			button = SDL_BUTTON_RIGHT;
			break;
		case 3:
			button = SDL_BUTTON_MIDDLE;
			break;
		}

		if (buttonstate & SDL_BUTTON(button))
			return true;
	}

	return false;
}

bool Mouse::isVisible() const
{
	return SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE;
}

void Mouse::setGrabbed(bool grab)
{
	auto window = getModInst(window,Window);
	// if (window)
	// 	window->setMouseGrab(grab);
}

bool Mouse::isGrabbed() const
{
	auto window = getModInst(window,Window);
	// if (window)
	// 	return window->isMouseGrabbed();
	// else
		return false;
}

bool Mouse::setRelativeMode(bool relative)
{
	return SDL_SetRelativeMouseMode(relative ? SDL_TRUE : SDL_FALSE) == 0;
}

bool Mouse::getRelativeMode() const
{
	return SDL_GetRelativeMouseMode() != SDL_FALSE;
}

} // eve::mouse::sdl
