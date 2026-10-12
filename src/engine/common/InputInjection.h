#pragma once

/**
 * @file InputInjection.h
 * @brief Capability for agent / MCP playtest input injection.
 *
 * DevTools injects keyboard, mouse, wheel and text without depending on the
 * SDL event module. The platform_event provider pushes native events so
 * Keyboard::isDown / Mouse::isDown stay consistent with polled state after the
 * next pump.
 */

#include "common/Export.h"
#include "common/Result.h"

#include <string>
#include <string_view>

namespace eve {

/**
 * @brief Inject synthetic device input into the live platform event stream.
 * @thread Owner game-loop / MCP poll thread only.
 * @reentrancy Does not invoke scripts or unknown callbacks.
 * @ownership Implementations are owned by the providing module; callers must
 *            not retain the pointer across frames or capability revoke.
 */
class EVENGINE_API_FOUNDATION_INLINE IInputInjection {
public:
    static constexpr const char* capabilityName = "IInputInjection";

    /** @brief Releases IInputInjection resources. */
    virtual ~IInputInjection() = default;

    /**
     * @brief Queue a key press or release.
     * @param key SDL key name (e.g. "A", "Return", "Left Ctrl").
     * @param down True for keydown, false for keyup.
     * @param repeat Whether the keydown is a repeat event.
     * @return Success, or ParseError / Unsupported / Failed with diagnostics.
     */
    [[nodiscard]] virtual Result<void> injectKey(std::string_view key, bool down, bool repeat = false) = 0;

    /**
     * @brief Queue a mouse button press or release at a window position.
     * @param button SDL button index (1=left, 2=middle, 3=right).
     * @param down True for button down, false for button up.
     * @param x Window X in logical pixels.
     * @param y Window Y in logical pixels.
     */
    [[nodiscard]] virtual Result<void> injectMouseButton(int button, bool down, float x, float y) = 0;

    /**
     * @brief Queue absolute mouse motion (and optional relative deltas).
     * @param x Window X in logical pixels.
     * @param y Window Y in logical pixels.
     * @param dx Relative X delta reported to relative-mode consumers.
     * @param dy Relative Y delta reported to relative-mode consumers.
     */
    [[nodiscard]] virtual Result<void> injectMouseMove(float x, float y, float dx = 0.f, float dy = 0.f) = 0;

    /**
     * @brief Queue a mouse wheel tick.
     * @param x Horizontal scroll amount (SDL wheel units).
     * @param y Vertical scroll amount (SDL wheel units).
     */
    [[nodiscard]] virtual Result<void> injectWheel(float x, float y) = 0;

    /**
     * @brief Queue a UTF-8 text input event (IME / text fields).
     * @param text Non-empty UTF-8 payload; truncated by the provider if needed.
     */
    [[nodiscard]] virtual Result<void> injectText(std::string_view text) = 0;
};

}  // namespace eve
