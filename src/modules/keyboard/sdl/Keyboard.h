#pragma once

#include "keyboard/Keyboard.h"

// SDL
#include <SDL2/SDL_keyboard.h>

namespace eve::keyboard::sdl
{

class Keyboard : public love::keyboard::Keyboard
{
public:

	Keyboard();

	// Implements Module.
	const char *getName() const;

	void setKeyRepeat(bool enable);
	bool hasKeyRepeat() const;
	bool isDown(const std::vector<Key> &keylist) const;
	bool isScancodeDown(const std::vector<Scancode> &scancodelist) const;

	Key getKeyFromScancode(Scancode scancode) const;
	Scancode getScancodeFromKey(Key key) const;

	void setTextInput(bool enable);
	void setTextInput(bool enable, double x, double y, double w, double h);
	bool hasTextInput() const;
	bool hasScreenKeyboard() const;

	static bool getConstant(Scancode in, SDL_Scancode &out);
	static bool getConstant(SDL_Scancode in, Scancode &out);

private:

	// Whether holding down a key triggers repeated key press events.
	// The real implementation is in love::event::sdl::Event::Convert.
	bool key_repeat;

	static const SDL_Keycode *createKeyMap();
	static const SDL_Keycode *keymap;

	static EnumMap<Scancode, SDL_Scancode, SDL_NUM_SCANCODES>::Entry scancodeEntries[];
	static EnumMap<Scancode, SDL_Scancode, SDL_NUM_SCANCODES> scancodes;

}; // Keyboard

} // 


