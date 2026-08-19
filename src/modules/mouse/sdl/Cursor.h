#pragma once
#include "mouse/Cursor.h"
#include <SDL2/SDL_mouse.h>
#include <map>

namespace eve::image {
	class ImageData;
}

namespace eve::mouse::sdl {

/** @brief SDL 鼠标光标实现（自定义图像或系统光标）。 */
class Cursor : public eve::mouse::Cursor
{
public:
	/** @brief 从图像数据创建自定义光标（热点 hotx/hoty）。 */
	Cursor(image::ImageData *imageData, int hotx, int hoty);
	/** @brief 创建系统光标（类型如 "arrow" / "ibeam"）。 */
	Cursor(std::string cursortype);
	~Cursor();

	/** @brief 底层 SDL_Cursor 句柄。 */
	void *getHandle() const;
	/** @brief 是否自定义 / 系统光标类型名。 */
	bool isCustom() const override { return is_custom; }
	std::string getSystemType() const override { return systemType; }

private:
	SDL_Cursor *cursor;
	bool is_custom;
	std::string systemType;

	static std::map<std::string, SDL_SystemCursor> systemCursorEntries;
};

} // eve::mouse::sdl


