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

    void setGraphics(graphics::Graphics* graphics) override;

private:
    graphics::Graphics* graphics;

};  // Window

}  // namespace sdl
}  // namespace window
}  // namespace eve