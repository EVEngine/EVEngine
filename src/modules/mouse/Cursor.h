#pragma once

#include "image/ImageData.h"
#include "common/Object.h"
#include "common/StringMap.h"

namespace eve
{
namespace mouse
{

class Cursor : public Object
{
public:
	virtual ~Cursor();

	/**
	 * Returns a pointer to the implementation-dependent handle of this Cursor.
	 **/
	virtual void *getHandle() const = 0;

	/**
	 * Returns whether this Cursor is system-defined or a custom image.
	 **/
	virtual CursorType getType() const = 0;

	/**
	 * Returns the type type of system cursor used, if this Cursor is using a
	 * system-defined image.
	 **/
	virtual SystemCursor getSystemType() const = 0;
};

} // mouse
} // eve


