#pragma once

#include <SDL2/SDL.h>

#include <string>

#include "window/Window.h"

namespace eve {
namespace window {
namespace sdl {

class Window final : public eve::window::Window {
public:
    Window();
    ~Window();

    void setGraphics(graphics::Graphics *graphics) override;

    bool setWindowSettings(int width = 800, int height = 600, const WindowSettings *settings = nullptr) override;
    void getWindowSettings(int &width, int &height, WindowSettings &settings) override;

    void close() override;

    bool setFullscreen(bool fullscreen, bool desktop_mode) override;
    bool setFullscreen(bool fullscreen) override;

private:
    graphics::Graphics *graphics = nullptr;

    int width, height;

    WindowSettings settings;

    std::string title;

    int windowWidth  = 800;
    int windowHeight = 600;
    int pixelWidth   = 800;
    int pixelHeight  = 600;

    bool open;
    bool mouseGrabbed;
    bool displayedWindowError;

    SDL_Window *window;

    bool createWindowAndContext(int x, int y, int w, int h, Uint32 windowflags, int msaa, bool stencil, int depth);
    void updateSettings(const WindowSettings &newsettings, bool updateGraphicsViewport);
    void close(bool allowExceptions);

};  // Window

}  // namespace sdl
}  // namespace window
}  // namespace eve