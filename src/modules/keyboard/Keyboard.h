#pragma once

#include "common/Export.h"
#include "common/Module.h"

#include <string>
#include <vector>

namespace eve::keyboard {

/**
 * @brief Keyboard input. Key / scancode names follow SDL naming
 * (SDL_GetKeyName / SDL_GetScancodeName), e.g. "A", "Return", "Left Ctrl".
 */
class EVENGINE_API_BACKENDS Keyboard : public Module {
public:
    Module_REG(Keyboard);

    /** @brief Releases backend keyboard state. */
    virtual ~Keyboard();

    /** @brief Whether held keys emit repeated keypressed events. */
    virtual void setKeyRepeat(bool enable) = 0;
    /** @brief True when key-repeat events are enabled. */
    virtual bool hasKeyRepeat() const = 0;

    /** @brief True if any of the named keys is currently down. */
    virtual bool isDown(const std::string& key) const = 0;
    /** @brief True if any key in the list is currently down. */
    virtual bool isDown(const std::vector<std::string>& keys) const = 0;

    /** @brief True if any of the named physical scancodes is currently down. */
    virtual bool isScancodeDown(const std::string& scancode) const = 0;
    /** @brief True if any scancode in the list is currently down. */
    virtual bool isScancodeDown(const std::vector<std::string>& scancodes) const = 0;

    /** @brief Layout-aware key name for a physical scancode (SDL names). */
    virtual std::string getKeyFromScancode(const std::string& scancode) const = 0;
    /** @brief Physical scancode name for a layout-aware key (SDL names). */
    virtual std::string getScancodeFromKey(const std::string& key) const = 0;

    /** @brief Enables or disables text-input / IME composition. */
    virtual void setTextInput(bool enable) = 0;
    /** @brief Hint where text appears so on-screen keyboards avoid covering it (pixel coords). */
    virtual void setTextInput(bool enable, double x, double y, double w, double h) = 0;
    /** @brief True while text-input / IME is active. */
    virtual bool hasTextInput() const = 0;
    /** @brief True when the platform exposes an on-screen keyboard. */
    virtual bool hasScreenKeyboard() const = 0;
};

}  // namespace eve::keyboard
