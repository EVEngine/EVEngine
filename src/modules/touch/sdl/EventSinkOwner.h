#pragma once

namespace eve::touch::sdl {

class Touch;

/**
 * @brief Bind the live Touch module into the SDL event sink.
 * @ownership Borrowed; cleared when the Touch module is destroyed.
 * @lifetime Valid from Touch construction until Touch destruction.
 */
void bindTouchEventSinkOwner(Touch* owner) noexcept;

}  // namespace eve::touch::sdl
