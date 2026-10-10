#pragma once

namespace eve::joystick {
class Joystick;
}

namespace eve::joystick::sdl {

/**
 * @brief Bind the live Joystick module into the SDL event sink.
 * @ownership Borrowed; the Joystick module owns itself and clears the bind on destroy.
 * @lifetime Valid from Joystick construction until Joystick destruction.
 */
void bindJoystickEventSinkOwner(eve::joystick::Joystick* owner) noexcept;

}  // namespace eve::joystick::sdl
