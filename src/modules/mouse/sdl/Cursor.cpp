
#include "mouse/sdl/Cursor.h"
#include "common/Exception.h"
#include "common/config.h"

namespace eve::mouse::sdl
{

Cursor::Cursor(image::ImageData *data, int hotx, int hoty)
	: cursor(nullptr)
	, is_custom(true)
	, systemType("")
{
	// int w = data->getWidth();
	// int h = data->getHeight();
	// int pitch = w * 4;

	// SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(data->getData(), w, h, 32, pitch, rmask, gmask, bmask, amask);
	// if (!surface)
	// 	throw Exception("Cannot create cursor: out of memory!");

	// cursor = SDL_CreateColorCursor(surface, hotx, hoty);
	// SDL_FreeSurface(surface);

	if (!cursor)
		throw Exception("Cannot create cursor: %s", SDL_GetError());
}

Cursor::Cursor(std::string cursortype)
	: cursor(nullptr)
	, is_custom(false)
	, systemType(cursortype)
{
	if (auto i = systemCursorEntries.find(cursortype); i != systemCursorEntries.end())
		cursor = SDL_CreateSystemCursor(i->second);
	else
		throw Exception("Cannot create system cursor: invalid type.");

	if (!cursor)
		throw Exception("Cannot create system cursor: %s", SDL_GetError());
}

Cursor::~Cursor()
{
	if (cursor)
		SDL_FreeCursor(cursor);
}

void *Cursor::getHandle() const
{
	return cursor;
}

std::map<std::string, SDL_SystemCursor> Cursor::systemCursorEntries =
{
	{"ARROW", SDL_SYSTEM_CURSOR_ARROW},
	{"IBEAM", SDL_SYSTEM_CURSOR_IBEAM},
	{"WAIT", SDL_SYSTEM_CURSOR_WAIT},
	{"CROSSHAIR", SDL_SYSTEM_CURSOR_CROSSHAIR},
	{"WAITARROW", SDL_SYSTEM_CURSOR_WAITARROW},
	{"SIZENWSE", SDL_SYSTEM_CURSOR_SIZENWSE},
	{"SIZENESW", SDL_SYSTEM_CURSOR_SIZENESW},
	{"SIZEWE", SDL_SYSTEM_CURSOR_SIZEWE},
	{"SIZENS", SDL_SYSTEM_CURSOR_SIZENS},
	{"SIZEALL", SDL_SYSTEM_CURSOR_SIZEALL},
	{"NO", SDL_SYSTEM_CURSOR_NO},
	{"HAND", SDL_SYSTEM_CURSOR_HAND},
};

} // eve::mouse::sdl
