#pragma once

namespace eve::keyboard {
class Keyboard;
}

namespace eve::keyboard::sdl {

/**
 * @brief Bind the live Keyboard module into the SDL event sink.
 * @ownership Borrowed; cleared when the Keyboard module is destroyed.
 * @lifetime Valid from Keyboard construction until Keyboard destruction.
 */
void bindKeyboardEventSinkOwner(eve::keyboard::Keyboard* owner) noexcept;

}  // namespace eve::keyboard::sdl
