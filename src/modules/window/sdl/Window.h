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

    void setSize(int width, int height) override;
    int  getWidth() const override;
    int  getHeight() const override;

    bool           setWindowSettings(WindowSettings settings) override;
    WindowSettings getWindowSettings() override;

    void close() override;

    bool setFullscreen(bool fullscreen, bool desktop_mode) override;
    bool setFullscreen(bool fullscreen) override;

private:
    graphics::Graphics *graphics = nullptr;

    int width = 800, height = 600;

    WindowSettings settings;

    std::string title;

    int windowWidth  = 800;
    int windowHeight = 600;
    int pixelWidth   = 800;
    int pixelHeight  = 600;

    bool open;
    bool mouseGrabbed;
    bool displayedWindowError;

    SDL_Window *window = nullptr;

    bool createWindowAndContext(int x, int y, int w, int h, Uint32 windowflags, int msaa, bool stencil, int depth);
    void updateSettings(const WindowSettings &newsettings, bool updateGraphicsViewport);
    void close(bool allowExceptions);

};  // Window

}  // namespace sdl
}  // namespace window
}  // namespace eve