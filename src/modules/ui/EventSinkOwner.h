#pragma once

namespace eve::ui {
class UI;

/**
 * @brief Bind the live UI module into the platform event sink.
 * @ownership Borrowed; cleared when the UI module is destroyed.
 * @lifetime Valid from UI construction until UI destruction.
 */
void bindUIEventSinkOwner(UI* owner) noexcept;

}  // namespace eve::ui
