#include "keyboard/Keyboard.h"
#include "keyboard/sdl/Keyboard.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::keyboard {

Module_IMPL(Keyboard, new sdl::Keyboard());

Keyboard::~Keyboard() {}

void Keyboard::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Keyboard::create, false);
    expose(cls);
}

void Keyboard::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Keyboard::getName);
    cls.addFunc("setKeyRepeat", &Keyboard::setKeyRepeat);
    cls.addFunc("hasKeyRepeat", &Keyboard::hasKeyRepeat);
    cls.addFunc("isDown", static_cast<bool (Keyboard::*)(const std::string&) const>(&Keyboard::isDown));
    cls.addFunc("isScancodeDown",
                static_cast<bool (Keyboard::*)(const std::string&) const>(&Keyboard::isScancodeDown));
    cls.addFunc("getKeyFromScancode", &Keyboard::getKeyFromScancode);
    cls.addFunc("getScancodeFromKey", &Keyboard::getScancodeFromKey);
    cls.addFunc("setTextInput", static_cast<void (Keyboard::*)(bool)>(&Keyboard::setTextInput));
    cls.addFunc("hasTextInput", &Keyboard::hasTextInput);
    cls.addFunc("hasScreenKeyboard", &Keyboard::hasScreenKeyboard);
}

}  // namespace eve::keyboard
