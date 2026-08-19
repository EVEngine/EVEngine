#pragma once
#include "mouse/Cursor.h"
#include <SDL2/SDL_mouse.h>
#include <map>

namespace eve::image {
	class ImageData;
}

namespace eve::mouse::sdl {

class Cursor : public eve::mouse::Cursor
{
public:
	Cursor(image::ImageData *imageData, int hotx, int hoty);
	Cursor(std::string cursortype);
	~Cursor();

	void *getHandle() const;
	bool isCustom() const override { return is_custom; }
	std::string getSystemType() const override { return systemType; }

private:
	SDL_Cursor *cursor;
	bool is_custom;
	std::string systemType;

	static std::map<std::string, SDL_SystemCursor> systemCursorEntries;
};

} // eve::mouse::sdl


