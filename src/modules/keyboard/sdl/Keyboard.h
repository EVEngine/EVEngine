#pragma once

#include "keyboard/Keyboard.h"

namespace eve::keyboard::sdl {

class Keyboard : public eve::keyboard::Keyboard {
public:
    Keyboard();
    ~Keyboard() override;

    void setKeyRepeat(bool enable) override;
    bool hasKeyRepeat() const override;

    bool isDown(const std::string& key) const override;
    bool isDown(const std::vector<std::string>& keys) const override;

    bool isScancodeDown(const std::string& scancode) const override;
    bool isScancodeDown(const std::vector<std::string>& scancodes) const override;

    std::string getKeyFromScancode(const std::string& scancode) const override;
    std::string getScancodeFromKey(const std::string& key) const override;

    void setTextInput(bool enable) override;
    void setTextInput(bool enable, double x, double y, double w, double h) override;
    bool hasTextInput() const override;
    bool hasScreenKeyboard() const override;

private:
    // Consumed by event::sdl::Event when converting SDL_KEYDOWN repeats.
    bool keyRepeat_ = false;
};

}  // namespace eve::keyboard::sdl
