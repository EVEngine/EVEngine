#pragma once

#include "keyboard/Keyboard.h"

namespace eve::keyboard::sdl {

/** @brief SDL keyboard backend implementing eve::keyboard::Keyboard. */
class Keyboard : public eve::keyboard::Keyboard {
public:
    /** @brief Constructs the SDL keyboard module. */
    Keyboard();
    /** @brief Releases keyboard backend state. */
    ~Keyboard() override;

    /** @brief @copydoc eve::keyboard::Keyboard::setKeyRepeat */
    void setKeyRepeat(bool enable) override;
    /** @brief @copydoc eve::keyboard::Keyboard::hasKeyRepeat */
    bool hasKeyRepeat() const override;

    /** @brief @copydoc eve::keyboard::Keyboard::isDown */
    bool isDown(const std::string& key) const override;
    /** @brief @copydoc eve::keyboard::Keyboard::isDown */
    bool isDown(const std::vector<std::string>& keys) const override;

    /** @brief @copydoc eve::keyboard::Keyboard::isScancodeDown */
    bool isScancodeDown(const std::string& scancode) const override;
    /** @brief @copydoc eve::keyboard::Keyboard::isScancodeDown */
    bool isScancodeDown(const std::vector<std::string>& scancodes) const override;

    /** @brief @copydoc eve::keyboard::Keyboard::getKeyFromScancode */
    std::string getKeyFromScancode(const std::string& scancode) const override;
    /** @brief @copydoc eve::keyboard::Keyboard::getScancodeFromKey */
    std::string getScancodeFromKey(const std::string& key) const override;

    /** @brief @copydoc eve::keyboard::Keyboard::setTextInput */
    void setTextInput(bool enable) override;
    /** @brief @copydoc eve::keyboard::Keyboard::setTextInput */
    void setTextInput(bool enable, double x, double y, double w, double h) override;
    /** @brief @copydoc eve::keyboard::Keyboard::hasTextInput */
    bool hasTextInput() const override;
    /** @brief @copydoc eve::keyboard::Keyboard::hasScreenKeyboard */
    bool hasScreenKeyboard() const override;

private:
    // Consumed by platform_event::sdl::Event when converting SDL_KEYDOWN repeats.
    bool keyRepeat_ = false;
};

}  // namespace eve::keyboard::sdl
