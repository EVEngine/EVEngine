#include "Window.h"

#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_vulkan.h>

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

namespace eve {

namespace graphic{
    class Graphics;
}

namespace window {
namespace sdl {

Window::Window() {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
        throw Exception("Could not initialize SDL video subsystem (%s)", SDL_GetError());
}

Window::~Window() { SDL_QuitSubSystem(SDL_INIT_VIDEO); }

void Window::setGraphics(graphics::Graphics *graphics) { this->graphics = graphics; }

void Window::setSize(int width, int height) {
    WindowSettings f = settings;
    f.width          = width;
    f.height         = height;

    setWindowSettings(f);
}

int Window::getWidth() const { return settings.width; }

int Window::getHeight() const { return settings.height; }

bool Window::setWindowSettings(WindowSettings f) {
    f.minwidth  = std::max(f.minwidth, (uint16_t)1);
    f.minheight = std::max(f.minheight, (uint16_t)1);

    // f.display = std::min(std::max(f.display, 0), getDisplayCount() - 1);
    if (f.width == 0 || f.height == 0) {
        SDL_DisplayMode mode = {};
        SDL_GetDesktopDisplayMode(f.display, &mode);
        f.width  = mode.w;
        f.height = mode.h;
    }

    Uint32 sdlflags = SDL_WINDOW_VULKAN;

    // On Android, disable fullscreen first on window creation so it's
    // possible to change the orientation by specifying portait width and
    // height, otherwise SDL will pick the current orientation dimensions when
    // fullscreen flag is set. Don't worry, we'll set it back later when user
    // also requested fullscreen after the window is created.
    // See https://github.com/love2d/love-android/issues/196
#ifdef EVENGINE_ANDROID
    bool fullscreen = f.fullscreen;

    f.fullscreen = false;
    f.fstype     = FULLSCREEN_DESKTOP;
#endif

    if (f.fullscreen) {
        if (f.desktop_mode)
            sdlflags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        else {
            sdlflags |= SDL_WINDOW_FULLSCREEN;
            SDL_DisplayMode mode = {0, width, height, 0, nullptr};

            // Fullscreen window creation will bug out if no mode can be used.
            if (SDL_GetClosestDisplayMode(f.display, &mode, &mode) == nullptr) {
                // GetClosestDisplayMode will fail if we request a size larger
                // than the largest available display mode, so we'll try to use
                // the largest (first) mode in that case.
                if (SDL_GetDisplayMode(f.display, 0, &mode) < 0) return false;
            }

            width  = mode.w;
            height = mode.h;
        }
    }

    if (f.resizable) sdlflags |= SDL_WINDOW_RESIZABLE;
    if (f.borderless) sdlflags |= SDL_WINDOW_BORDERLESS;
    if (f.high_dpi) sdlflags |= SDL_WINDOW_ALLOW_HIGHDPI;
    if (f.always_on_top) sdlflags |= SDL_WINDOW_ALWAYS_ON_TOP;

    int x = f.x;
    int y = f.y;

    if (f.use_position) {
        // The position needs to be in the global coordinate space.
        SDL_Rect displaybounds = {};
        SDL_GetDisplayBounds(f.display, &displaybounds);
        x += displaybounds.x;
        y += displaybounds.y;
    } else {
        if (f.centered)
            x = y = SDL_WINDOWPOS_CENTERED_DISPLAY(f.display);
        else
            x = y = SDL_WINDOWPOS_UNDEFINED_DISPLAY(f.display);
    }

    close();

    if (!createWindowAndContext(x, y, f.width, f.height, sdlflags, f.msaa, f.stencil, f.depth)) return false;

    // Make sure the window keeps any previously set icon.
    // setIcon(icon.get());

    // Make sure the mouse keeps its previous grab setting.
    // setMouseGrab(mouseGrabbed);

    // Enforce minimum window dimensions.
    SDL_SetWindowMinimumSize(window, f.minwidth, f.minheight);

    if (f.use_position || f.centered) SDL_SetWindowPosition(window, x, y);

    SDL_RaiseWindow(window);

    // setVSync(f.vsync);

    updateSettings(f, false);

    if (graphics) {
        int pw = 0, ph = 0;
        SDL_Vulkan_GetDrawableSize(window, &pw, &ph);
        if (pw <= 0 || ph <= 0) {
            pw = f.width;
            ph = f.height;
        }
        pixelWidth  = pw;
        pixelHeight = ph;
        graphics->initWithWindow(window);
        graphics->setViewportSize(f.width, f.height, pw, ph);
    }
    return true;
}

WindowSettings Window::getWindowSettings() { return settings; }

bool Window::setFullscreen(bool fullscreen, bool desktop_mode) {
    if (!window) return false;

    // if (graphics && graphics->isCanvasActive())
    // 	throw Exception("love.window.setFullscreen cannot be called while a Canvas is active in love.graphics.");

    WindowSettings newsettings = settings;
    newsettings.fullscreen     = fullscreen;
    newsettings.desktop_mode   = desktop_mode;

    Uint32 sdlflags = 0;

    if (fullscreen) {
        if (desktop_mode)
            sdlflags = SDL_WINDOW_FULLSCREEN_DESKTOP;
        else {
            sdlflags = SDL_WINDOW_FULLSCREEN;

            SDL_DisplayMode mode = {};
            mode.w               = windowWidth;
            mode.h               = windowHeight;

            SDL_GetClosestDisplayMode(SDL_GetWindowDisplayIndex(window), &mode, &mode);
            SDL_SetWindowDisplayMode(window, &mode);
        }
    }

#ifdef EVENGINE_ANDROID
    android::setImmersive(fullscreen);
#endif

    if (SDL_SetWindowFullscreen(window, sdlflags) == 0) {
        // SDL_GL_MakeCurrent(window, context);
        updateSettings(newsettings, true);

        // Apparently this gets un-set when we exit fullscreen (at least in OS X).
        if (!fullscreen) SDL_SetWindowMinimumSize(window, settings.minwidth, settings.minheight);

        return true;
    }

    return false;
}

bool Window::setFullscreen(bool fullscreen) { return setFullscreen(fullscreen, settings.desktop_mode); }

bool Window::createWindowAndContext(int x, int y, int w, int h, Uint32 windowflags, int msaa, bool stencil, int depth) {
    window = SDL_CreateWindow(title.c_str(), x, y, w, h, windowflags);

    if (!window) {
        throw Exception("Window Create Failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

void Window::close() { close(true); }

void Window::close(bool allowExceptions) {
    if (graphics) {
        // if (allowExceptions && graphics->isCanvasActive())
        // 	throw Exception("close cannot be called while a Canvas is active in love.graphics.");

        // graphics->unSetMode();
    }

    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;

        // The old window may have generated pending events which are no longer
        // relevant. Destroy them all!
        SDL_FlushEvent(SDL_WINDOWEVENT);
    }

    open = false;
}

void Window::updateSettings(const WindowSettings &newsettings, bool updateGraphicsViewport) {
    settings = newsettings;
    if (window) {
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        SDL_Vulkan_GetDrawableSize(window, &pixelWidth, &pixelHeight);
    }
    if (updateGraphicsViewport && graphics && window) {
        graphics->setViewportSize(windowWidth, windowHeight, pixelWidth, pixelHeight);
    }
}

}  // namespace sdl
}  // namespace window
}  // namespace eve