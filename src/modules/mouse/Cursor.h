#pragma once

#include <string>

namespace eve::mouse
{

class Cursor 
{
public:
	virtual ~Cursor();

	/**
	 * @brief Returns a pointer to the implementation-dependent handle of this Cursor.
	 **/
	virtual void *getHandle() const = 0;

	/**
	 * @brief Returns whether this Cursor is system-defined or a custom image.
	 **/
	virtual bool isCustom() const = 0;

	/**
	 * @brief Returns the type of system cursor used, if this Cursor is using a
	 * system-defined image.
	 **/
	virtual std::string getSystemType() const = 0;
};

} // eve::mouse


