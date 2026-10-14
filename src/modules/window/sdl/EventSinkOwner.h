#pragma once

namespace eve::window {
class Window;
}

namespace eve::window::sdl {

/**
 * @brief Bind the live Window module into the SDL event sink.
 * @ownership Borrowed; cleared when the Window module is destroyed.
 * @lifetime Valid from Window construction until Window destruction.
 */
void bindWindowEventSinkOwner(eve::window::Window* owner) noexcept;

}  // namespace eve::window::sdl
