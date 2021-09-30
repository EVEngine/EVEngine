#include "Window.h"

#include <SDL2/SDL_syswm.h>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <vector>

#include "common/Exception.h"
#include "graphics/Graphics.h"

#ifdef EVENGINE_ANDROID
#include "android.h"
#endif

#ifdef EVENGINE_IOS
#include "ios.h"
#endif

#if defined(EVENGINE_WINDOWS)
#include <windows.h>
#endif

#if defined(EVENGINE_MACOSX)
#include "macosx.h"
#endif

namespace eve {
namespace window {
namespace sdl {

Window::Window() {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
        throw Exception("Could not initialize SDL video subsystem (%s)", SDL_GetError());
}

Window::~Window() { SDL_QuitSubSystem(SDL_INIT_VIDEO); }

void Window::setGraphics(graphics::Graphics* graphics) { this->graphics = graphics; }

}  // namespace sdl
}  // namespace window
}  // namespace eve