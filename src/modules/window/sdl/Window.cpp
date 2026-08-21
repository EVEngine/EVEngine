#include "Window.h"

#include <SDL2/SDL_syswm.h>
#ifndef EVENGINE_WEBGPU
#include <SDL2/SDL_vulkan.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <vector>

#include "common/Capability.h"
#include "common/Exception.h"
#include "common/StartupTiming.h"
#include "common/WindowSurfaceHost.h"
#include "common/config.h"

#ifdef EVENGINE_ANDROID
#include "android/android.h"
#endif

#ifdef EVENGINE_IOS
#include "ios/ios.h"
#endif

#if defined(EVENGINE_WINDOWS)
#include <windows.h>
#endif

#ifdef EVENGINE_MACOSX
#include "macosx/macosx.h"
#endif

namespace eve {

namespace window {
namespace sdl {

namespace {

Uint32 messageBoxFlag(const std::string& type) {
    if (type == "error") return SDL_MESSAGEBOX_ERROR;
    if (type == "warning") return SDL_MESSAGEBOX_WARNING;
    return SDL_MESSAGEBOX_INFORMATION;
}

}  // namespace

Window::Window() : open(false) {
#if defined(__EMSCRIPTEN__)
    // Bind SDL keyboard to the render canvas instead of the whole window, so
    // typing in the playground editor / REPL never reaches the game. The
    // canvas must be focused (tabindex + click) to deliver keys.
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "canvas");
#endif
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
        throw Exception("Could not initialize SDL video subsystem (%s)", SDL_GetError());
}

Window::~Window() { SDL_QuitSubSystem(SDL_INIT_VIDEO); }

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

    if (getDisplayCount() > 0)
        f.display = static_cast<uint8_t>(std::min(std::max(int(f.display), 0), getDisplayCount() - 1));
    if (f.width == 0 || f.height == 0) {
        SDL_DisplayMode mode = {};
        SDL_GetDesktopDisplayMode(f.display, &mode);
        f.width  = mode.w;
        f.height = mode.h;
    }

    Uint32 sdlflags = 0;
#ifndef EVENGINE_WEBGPU
    sdlflags = SDL_WINDOW_VULKAN;
#endif

    // On Android, disable fullscreen first on window creation so it's
    // possible to change the orientation by specifying portait width and
    // height, otherwise SDL will pick the current orientation dimensions when
    // fullscreen flag is set. Don't worry, we'll set it back later when user
    // also requested fullscreen after the window is created.
    // See https://github.com/love2d/love-android/issues/196
#ifdef EVENGINE_ANDROID
    bool fullscreen = f.fullscreen;

    f.fullscreen = false;
    f.desktop_mode = true;
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

    {
        StartupStage stage("window: SDL_CreateWindow");
        if (!createWindowAndContext(x, y, f.width, f.height, sdlflags, f.msaa, f.stencil, f.depth)) return false;
    }

    // Make sure the window keeps any previously set icon.
    if (!iconRgba.empty()) setIconRGBA(iconRgba.data(), iconWidth, iconHeight);

    // Make sure the mouse keeps its previous grab setting.
    // setMouseGrab(mouseGrabbed);

    // Enforce minimum window dimensions.
    SDL_SetWindowMinimumSize(window, f.minwidth, f.minheight);

    if (f.use_position || f.centered) SDL_SetWindowPosition(window, x, y);

    SDL_RaiseWindow(window);

    // setVSync(f.vsync);

#if defined(EVENGINE_ANDROID) || defined(EVENGINE_IOS)
    // Fullscreen mobile: logical size must match the real window aspect, otherwise
    // an 800x600 desktop config is stretched onto a portrait (or ultrawide) surface.
    {
        int lw = 0, lh = 0;
        SDL_GetWindowSize(window, &lw, &lh);
        if (lw > 0 && lh > 0) {
            f.width = static_cast<uint16_t>(lw);
            f.height = static_cast<uint16_t>(lh);
        }
    }
#endif

    updateSettings(f, false);

    if (auto* surfaceHost = eve::cap::query<IWindowSurfaceHost>()) {
        int pw = 0, ph = 0;
#ifdef EVENGINE_WEBGPU
        // No Vulkan drawable-size helper on WebGPU; the window size is the
        // canvas size (the browser applies DPI scaling itself).
        SDL_GetWindowSize(window, &pw, &ph);
#else
        SDL_Vulkan_GetDrawableSize(window, &pw, &ph);
#endif
        if (pw <= 0 || ph <= 0) {
            pw = f.width;
            ph = f.height;
        }
        pixelWidth  = pw;
        pixelHeight = ph;
        {
            StartupStage stage("window: graphics initWithWindow");
            surfaceHost->initWithWindow(window);
        }
        surfaceHost->setViewportSize(f.width, f.height, pw, ph);
    }
    open = true;
    return true;
}

WindowSettings Window::getWindowSettings() { return settings; }

bool Window::setFullscreenDesktop(bool fullscreen) {
    return setFullscreenInternal(fullscreen, true);
}

bool Window::setFullscreenExclusive(bool fullscreen) {
    return setFullscreenInternal(fullscreen, false);
}

bool Window::setFullscreenInternal(bool fullscreen, bool desktop_mode) {
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

    // Freshly-created windows (and CI macOS runners) can refuse FULLSCREEN_* until
    // the window is shown and the event queue has been pumped.
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    SDL_PumpEvents();

    int rc = SDL_SetWindowFullscreen(window, sdlflags);
    if (rc != 0) {
        SDL_Delay(16);
        SDL_PumpEvents();
        rc = SDL_SetWindowFullscreen(window, sdlflags);
    }

    if (rc == 0) {
        // SDL_GL_MakeCurrent(window, context);
        updateSettings(newsettings, true);

        // Apparently this gets un-set when we exit fullscreen (at least in OS X).
        if (!fullscreen) SDL_SetWindowMinimumSize(window, settings.minwidth, settings.minheight);

        return true;
    }

    return false;
}

bool Window::isOpen() const { return open; }

void Window::setWindowTitle(const std::string& t) {
    title = t;
    if (window) SDL_SetWindowTitle(window, title.c_str());
}

const std::string& Window::getWindowTitle() const { return title; }

void Window::setPosition(int x, int y, int display) {
    if (!window) return;
    int count = SDL_GetNumVideoDisplays();
    if (display < 0 || display >= count) display = 0;
    SDL_Rect bounds = {};
    SDL_GetDisplayBounds(display, &bounds);
    SDL_SetWindowPosition(window, bounds.x + x, bounds.y + y);
    settings.x            = x;
    settings.y            = y;
    settings.display      = static_cast<uint8_t>(display);
    settings.use_position = true;
}

void Window::getPosition(int& x, int& y, int& display) {
    if (!window) {
        x       = settings.x;
        y       = settings.y;
        display = settings.display;
        return;
    }
    int wx = 0, wy = 0;
    SDL_GetWindowPosition(window, &wx, &wy);
    display = SDL_GetWindowDisplayIndex(window);
    if (display < 0) display = 0;
    SDL_Rect bounds = {};
    SDL_GetDisplayBounds(display, &bounds);
    x = wx - bounds.x;
    y = wy - bounds.y;
}

void Window::minimize() { if (window) SDL_MinimizeWindow(window); }
void Window::maximize() { if (window) SDL_MaximizeWindow(window); }
void Window::restore() { if (window) SDL_RestoreWindow(window); }

bool Window::isMaximized() const {
    return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
}
bool Window::isMinimized() const {
    return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0;
}
bool Window::hasFocus() const {
    return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
}
bool Window::hasMouseFocus() const {
    return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_MOUSE_FOCUS) != 0;
}
bool Window::isVisible() const {
    return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_SHOWN) != 0
           && (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) == 0;
}

void Window::setVSync(int vsync) {
    settings.vsync = static_cast<uint8_t>(vsync < 0 ? 0 : (vsync > 255 ? 255 : vsync));
}

int Window::getVSync() const { return settings.vsync; }

int Window::getPixelWidth() const { return window ? pixelWidth : 0; }

int Window::getPixelHeight() const { return window ? pixelHeight : 0; }

double Window::getNativeDPIScale() const {
    if (!window || windowWidth <= 0) return 1.0;
    return double(pixelWidth) / double(windowWidth);
}

double Window::getDPIScale() const {
    if (!window) return 1.0;
    if (!settings.use_dpi_scale) return settings.dpi_scale > 0.f ? double(settings.dpi_scale) : 1.0;
    return getNativeDPIScale();
}

void Window::windowToPixelCoords(double* x, double* y) const {
    if (!x || !y) return;
    double s = getNativeDPIScale();
    *x *= s;
    *y *= s;
}

void Window::pixelToWindowCoords(double* x, double* y) const {
    if (!x || !y) return;
    double s = getNativeDPIScale();
    if (s == 0.0) return;
    *x /= s;
    *y /= s;
}

void Window::windowToDPICoords(double* x, double* y) const {
    if (!x || !y) return;
    double s = getDPIScale();
    if (s == 0.0) return;
    *x /= s;
    *y /= s;
}

void Window::DPIToWindowCoords(double* x, double* y) const {
    if (!x || !y) return;
    double s = getDPIScale();
    *x *= s;
    *y *= s;
}

double Window::toPixels(double x) const { return x * getNativeDPIScale(); }

double Window::fromPixels(double x) const {
    double s = getNativeDPIScale();
    return s == 0.0 ? x : x / s;
}

void Window::toPixelsXY(double wx, double wy, double& px, double& py) const {
    px = toPixels(wx);
    py = toPixels(wy);
}

void Window::fromPixelsXY(double px, double py, double& wx, double& wy) const {
    wx = fromPixels(px);
    wy = fromPixels(py);
}

int Window::getDisplayCount() const {
    int n = SDL_GetNumVideoDisplays();
    return n < 0 ? 0 : n;
}

std::string Window::getDisplayName(int display) const {
    if (display < 0 || display >= getDisplayCount()) return {};
    const char* n = SDL_GetDisplayName(display);
    return n ? std::string(n) : std::string();
}

std::string Window::getDisplayOrientation(int display) const {
    if (display < 0 || display >= getDisplayCount()) return "unknown";
    switch (SDL_GetDisplayOrientation(display)) {
        case SDL_ORIENTATION_LANDSCAPE: return "landscape";
        case SDL_ORIENTATION_LANDSCAPE_FLIPPED: return "landscapeFlipped";
        case SDL_ORIENTATION_PORTRAIT: return "portrait";
        case SDL_ORIENTATION_PORTRAIT_FLIPPED: return "portraitFlipped";
        default: return "unknown";
    }
}

std::vector<Window::WindowSize> Window::getFullscreenSizes(int display) const {
    std::vector<WindowSize> out;
    if (display < 0 || display >= getDisplayCount()) return out;
    int modes = SDL_GetNumDisplayModes(display);
    for (int i = 0; i < modes; ++i) {
        SDL_DisplayMode mode = {};
        if (SDL_GetDisplayMode(display, i, &mode) == 0) {
            WindowSize s;
            s.width  = mode.w;
            s.height = mode.h;
            if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
        }
    }
    return out;
}

void Window::getDesktopDimensions(int display, int& width, int& height) const {
    width = height = 0;
    if (display < 0 || display >= getDisplayCount()) return;
    SDL_DisplayMode mode = {};
    if (SDL_GetDesktopDisplayMode(display, &mode) == 0) {
        width  = mode.w;
        height = mode.h;
    }
}

bool Window::showMessageBox(const std::string& title, const std::string& message,
                            const std::string& type, bool attachToWindow) {
    SDL_Window* parent = (attachToWindow && window) ? window : nullptr;
    return SDL_ShowSimpleMessageBox(messageBoxFlag(type), title.c_str(), message.c_str(), parent) == 0;
}

int Window::showMessageBoxData(const MessageBoxData& data) {
    if (data.buttons.empty()) return -1;
    std::vector<SDL_MessageBoxButtonData> buttons;
    buttons.reserve(data.buttons.size());
    for (size_t i = 0; i < data.buttons.size(); ++i) {
        Uint32 flags = 0;
        if (int(i) == data.enterButtonIndex) flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
        if (int(i) == data.escapeButtonIndex) flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
        buttons.push_back({flags, int(i), data.buttons[i].c_str()});
    }
    SDL_MessageBoxData sdl = {};
    sdl.flags      = messageBoxFlag(data.type);
    sdl.window     = (data.attachToWindow && window) ? window : nullptr;
    sdl.title      = data.title.c_str();
    sdl.message    = data.message.c_str();
    sdl.numbuttons = int(buttons.size());
    sdl.buttons    = buttons.data();
    sdl.colorScheme = nullptr;
    int buttonid = -1;
    if (SDL_ShowMessageBox(&sdl, &buttonid) < 0) return -1;
    return buttonid;
}

void Window::requestAttention(bool continuous) {
#ifdef EVENGINE_MACOSX
    eve::macosx::requestAttention(continuous);
#else
    if (!window) return;
#if SDL_VERSION_ATLEAST(2, 0, 16)
    SDL_FlashWindow(window, continuous ? SDL_FLASH_UNTIL_FOCUSED : SDL_FLASH_BRIEFLY);
#endif
#endif
}

bool Window::setIconRGBA(const uint8_t* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0) return false;

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    const Uint32 rmask = 0xFF000000;
    const Uint32 gmask = 0x00FF0000;
    const Uint32 bmask = 0x0000FF00;
    const Uint32 amask = 0x000000FF;
#else
    const Uint32 rmask = 0x000000FF;
    const Uint32 gmask = 0x0000FF00;
    const Uint32 bmask = 0x00FF0000;
    const Uint32 amask = 0xFF000000;
#endif

    SDL_Surface* surface =
        SDL_CreateRGBSurfaceFrom(const_cast<uint8_t*>(rgba), width, height, 32, width * 4, rmask, gmask, bmask, amask);
    if (!surface) {
        throw Exception("Could not create window icon surface: %s", SDL_GetError());
    }

    if (window)
        SDL_SetWindowIcon(window, surface);

    SDL_FreeSurface(surface);

    iconRgba.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);
    iconWidth  = width;
    iconHeight = height;

#ifdef EVENGINE_MACOSX
    // SDL only updates the titlebar/mini window icon on macOS; refresh the Dock icon too.
    eve::macosx::setIconRGBA(rgba, width, height);
#endif

    return true;
}

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
    if (window) {
        // ImGui / swapchain teardown needs the native window and surface still
        // alive. Destroy the SDL window only after graphics has dropped them.
        if (auto* surfaceHost = eve::cap::query<IWindowSurfaceHost>()) surfaceHost->onNativeWindowDestroyed();

        SDL_DestroyWindow(window);
        window = nullptr;

        // The old window may have generated pending events which are no longer
        // relevant. Destroy them all!
        SDL_FlushEvent(SDL_WINDOWEVENT);
    }

    pixelWidth = pixelHeight = 0;
    windowWidth = windowHeight = 0;
    open = false;
}

void Window::updateSettings(const WindowSettings &newsettings, bool updateGraphicsViewport) {
    settings = newsettings;
    if (window) {
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
#ifdef EVENGINE_WEBGPU
        SDL_GetWindowSize(window, &pixelWidth, &pixelHeight);
#else
        SDL_Vulkan_GetDrawableSize(window, &pixelWidth, &pixelHeight);
#endif
    }
    if (updateGraphicsViewport && window) {
        if (auto* surfaceHost = eve::cap::query<IWindowSurfaceHost>())
            surfaceHost->setViewportSize(windowWidth, windowHeight, pixelWidth, pixelHeight);
    }
}

}  // namespace sdl
}  // namespace window
}  // namespace eve
