
#include "Event.h"

#include "filesystem/Filesystem.h"
#include "graphics/Graphics.h"
#include "window/Window.h"
#include "window/sdl/Window.h"
#ifndef EVENGINE_WEBGPU
#include "audio/Audio.h"
#endif
#include "touch/Touch.h"
#include "touch/sdl/Touch.h"
#include "keyboard/Keyboard.h"
#include "joystick/Joystick.h"
#include "joystick/Pad.h"
#include "ui/UI.h"
#include "common/Exception.h"
#include "common/Module.h"
#include "common/config.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <cmath>

namespace eve::event::sdl
{

// SDL reports mouse coordinates in the window coordinate system in OS X, but
// we want them in pixel coordinates (may be different with high-DPI enabled.)
static void windowToDPICoords(double *x, double *y)
{
	auto window = getModInst(window,Window);
	// if (window)
	// 	window->windowToDPICoords(x, y);
}

#ifndef EVENGINE_MACOSX
static void normalizedToDPICoords(double *x, double *y)
{
	double w = 1.0, h = 1.0;

	auto window = getModInst(window,Window);
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

static void syncWindowPixelSize()
{
	auto *win = getModInst(window, Window);
	auto *gfx = getModInst(graphics, Graphics);
	if (!win || !gfx)
		return;
	auto *sdlWin = dynamic_cast<eve::window::sdl::Window *>(win);
	if (!sdlWin)
		return;
	SDL_Window *native = static_cast<SDL_Window *>(sdlWin->getHandle());
	if (!native)
		return;

	int lw = 0, lh = 0, pw = 0, ph = 0;
	SDL_GetWindowSize(native, &lw, &lh);
	SDL_Vulkan_GetDrawableSize(native, &pw, &ph);
	if (pw <= 0 || ph <= 0) {
		pw = lw;
		ph = lh;
	}
	window::WindowSettings s = win->getWindowSettings();
	s.width = static_cast<uint16_t>(std::max(lw, 1));
	s.height = static_cast<uint16_t>(std::max(lh, 1));
	// Keep settings + graphics viewport aligned after rotation / resume.
	sdlWin->updateSettings(s, true);
}

// SDL's event watch callbacks trigger when the event is actually posted inside
// SDL, unlike with SDL_PollEvents. This is useful for some events which require
// handling inside the function which triggered them on some backends.
static int SDLCALL watchAppEvents(void * /*udata*/, SDL_Event *event)
{
	auto gfx = getModInst(graphics,Graphics);

	switch (event->type)
	{
	case SDL_APP_DIDENTERBACKGROUND:
		// Stop presenting: the native surface is being torn down.
		if (gfx)
			gfx->setActive(false);
		break;
	case SDL_APP_WILLENTERFOREGROUND:
	case SDL_APP_DIDENTERFOREGROUND:
		// The native window is recreated on resume; rebuild the render surface
		// and swapchain, then resume presenting.
		if (gfx) {
			gfx->requestSurfaceRecreate();
			gfx->setActive(true);
			syncWindowPixelSize();
		}
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
	wakeEventType_ = SDL_RegisterEvents(1);
	if (wakeEventType_ == static_cast<Uint32>(-1)) {
		SDL_QuitSubSystem(SDL_INIT_EVENTS);
		throw eve::Exception("Could not reserve SDL wake event (%s)", SDL_GetError());
	}

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
		if (auto *ui = eve::ModuleManager::getInstance<eve::ui::UI>("UI"))
			ui->processEvent(&e);
		Message *msg = convert(e);
		if (msg)
			push(msg);
		// Ownership stays in the queue until poll()/clear() — do not delete here.
	}

#ifndef EVENGINE_WEBGPU
	if (auto *audio = eve::ModuleManager::getInstance<eve::audio::Audio>("Audio"))
		audio->pump();
#endif
}

Message *Event::wait()
{
	exceptionIfInRenderPass("eve.event.wait");

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
	auto gfx = getModInst(graphics,Graphics);
	// if (gfx != nullptr && gfx->isCanvasActive())
	// 	throw eve::Exception("%s cannot be called while a Canvas is active in eve.graphics.", name);
}

Message *Event::convert(const SDL_Event &e)
{
	switch (e.type) {
	case SDL_QUIT:
		return new Message("quit");
	case SDL_WINDOWEVENT:
		switch (e.window.event) {
		case SDL_WINDOWEVENT_CLOSE:
			return new Message("quit");
		case SDL_WINDOWEVENT_SIZE_CHANGED:
		case SDL_WINDOWEVENT_RESIZED:
			syncWindowPixelSize();
			break;
		default:
			break;
		}
		break;
	case SDL_KEYDOWN: {
		if (e.key.repeat) {
			auto *kb = getModInst(keyboard, Keyboard);
			if (kb && !kb->hasKeyRepeat())
				break;
		}
		const char *keyName = SDL_GetKeyName(e.key.keysym.sym);
		const char *scanName = SDL_GetScancodeName(e.key.keysym.scancode);
		return new Message("keypressed",
		                   {Variant::makeString(keyName ? keyName : ""),
		                    Variant::makeString(scanName ? scanName : ""),
		                    Variant::makeInt(e.key.repeat ? 1 : 0)});
	}
	case SDL_KEYUP: {
		const char *keyName = SDL_GetKeyName(e.key.keysym.sym);
		const char *scanName = SDL_GetScancodeName(e.key.keysym.scancode);
		return new Message("keyreleased",
		                   {Variant::makeString(keyName ? keyName : ""),
		                    Variant::makeString(scanName ? scanName : "")});
	}
	case SDL_TEXTINPUT:
		return new Message("textinput", {Variant::makeString(e.text.text)});
	case SDL_TEXTEDITING:
		return new Message("textedited",
		                   {Variant::makeString(e.edit.text),
		                    Variant::makeInt(e.edit.start),
		                    Variant::makeInt(e.edit.length)});
	case SDL_JOYDEVICEADDED: {
		auto *joy = getModInst(joystick, Joystick);
		if (!joy) break;
		auto *pad = joy->addJoystick(e.jdevice.which);
		if (!pad) break;
		return new Message("joystickadded",
		                   {Variant::makeInt(pad->getID()),
		                    Variant::makeString(pad->getName()),
		                    Variant::makeInt(pad->isGamepad() ? 1 : 0)});
	}
	case SDL_JOYDEVICEREMOVED: {
		auto *joy = getModInst(joystick, Joystick);
		if (!joy) break;
		auto *pad = joy->getJoystickFromID(e.jdevice.which);
		if (!pad) break;
		int id = pad->getID();
		std::string name = pad->getName();
		joy->removeJoystick(pad);
		return new Message("joystickremoved",
		                   {Variant::makeInt(id), Variant::makeString(name)});
	}
	case SDL_JOYBUTTONDOWN:
	case SDL_JOYBUTTONUP: {
		auto *joy = getModInst(joystick, Joystick);
		if (!joy) break;
		auto *pad = joy->getJoystickFromID(e.jbutton.which);
		if (!pad) break;
		return new Message(e.type == SDL_JOYBUTTONDOWN ? "joystickpressed" : "joystickreleased",
		                   {Variant::makeInt(pad->getID()), Variant::makeInt(e.jbutton.button)});
	}
	case SDL_JOYAXISMOTION: {
		auto *joy = getModInst(joystick, Joystick);
		if (!joy) break;
		auto *pad = joy->getJoystickFromID(e.jaxis.which);
		if (!pad) break;
		float value = eve::joystick::clampAxis(e.jaxis.value / 32768.0f);
		return new Message("joystickaxis",
		                   {Variant::makeInt(pad->getID()), Variant::makeInt(e.jaxis.axis),
		                    Variant::makeInt(static_cast<int64_t>(std::lround(value * 1000.0f)))});
	}
	case SDL_JOYHATMOTION: {
		auto *joy = getModInst(joystick, Joystick);
		if (!joy) break;
		auto *pad = joy->getJoystickFromID(e.jhat.which);
		if (!pad) break;
		std::string hat = pad->getHat(e.jhat.hat);
		// Prefer live hat from the event mask via re-query; getHat reads current state.
		return new Message("joystickhat",
		                   {Variant::makeInt(pad->getID()), Variant::makeInt(e.jhat.hat),
		                    Variant::makeString(hat)});
	}
	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP: {
		auto *joy = getModInst(joystick, Joystick);
		if (!joy) break;
		auto *pad = joy->getJoystickFromID(e.cbutton.which);
		if (!pad) break;
		const char *btn =
			SDL_GameControllerGetStringForButton(static_cast<SDL_GameControllerButton>(e.cbutton.button));
		return new Message(e.type == SDL_CONTROLLERBUTTONDOWN ? "gamepadpressed" : "gamepadreleased",
		                   {Variant::makeInt(pad->getID()),
		                    Variant::makeString(btn ? btn : "")});
	}
	case SDL_CONTROLLERAXISMOTION: {
		auto *joy = getModInst(joystick, Joystick);
		if (!joy) break;
		auto *pad = joy->getJoystickFromID(e.caxis.which);
		if (!pad) break;
		const char *axis =
			SDL_GameControllerGetStringForAxis(static_cast<SDL_GameControllerAxis>(e.caxis.axis));
		float value = eve::joystick::clampAxis(e.caxis.value / 32768.0f);
		return new Message("gamepadaxis",
		                   {Variant::makeInt(pad->getID()),
		                    Variant::makeString(axis ? axis : ""),
		                    Variant::makeInt(static_cast<int64_t>(std::lround(value * 1000.0f)))});
	}
	case SDL_FINGERDOWN:
	case SDL_FINGERUP:
	case SDL_FINGERMOTION: {
		auto *touchMod = dynamic_cast<eve::touch::sdl::Touch *>(
			eve::ModuleManager::getInstance<eve::touch::Touch>("Touch"));
		if (touchMod) {
			eve::touch::Touch::TouchInfo info{};
			info.id = static_cast<int64_t>(e.tfinger.fingerId);
			info.x = e.tfinger.x;
			info.y = e.tfinger.y;
			info.dx = e.tfinger.dx;
			info.dy = e.tfinger.dy;
			info.pressure = e.tfinger.pressure;
#ifndef EVENGINE_MACOSX
			normalizedToDPICoords(&info.x, &info.y);
			normalizedToDPICoords(&info.dx, &info.dy);
#endif
			touchMod->onEvent(e.type, info);
		}
		break;
	}
	default:
		break;
	}
	return nullptr;
}

}
