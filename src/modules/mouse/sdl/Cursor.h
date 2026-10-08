#pragma once
#include "mouse/Cursor.h"
#include <SDL2/SDL_mouse.h>
#include <map>

namespace eve::image {
	class ImageData;
}

namespace eve::mouse::sdl {

/** @brief SDL mouse cursor (custom image or named system cursor). */
class Cursor : public eve::mouse::Cursor
{
public:
	/** @brief Builds a custom cursor from image data with hotspot (hotx, hoty). */
	Cursor(image::ImageData *imageData, int hotx, int hoty);
	/** @brief Builds a system cursor (type such as "arrow" / "ibeam"). */
	Cursor(std::string cursortype);
	/** @brief Frees the underlying SDL_Cursor. */
	~Cursor();

	/** @brief Underlying SDL_Cursor handle. */
	void *getHandle() const;
	/** @brief True when this cursor was built from custom image data. */
	bool isCustom() const override { return is_custom; }
	/** @brief System cursor type name, or empty for custom cursors. */
	std::string getSystemType() const override { return systemType; }

private:
	SDL_Cursor *cursor;
	bool is_custom;
	std::string systemType;

	static std::map<std::string, SDL_SystemCursor> systemCursorEntries;
};

} // eve::mouse::sdl
