/**
 * Copyright (c) 2006-2021 LOVE Development Team
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 **/

#pragma once

#include "common/Data.h"
#include <string>

namespace eve
{
namespace data
{

class HashFunction
{
public:

	struct Value
	{
		char data[64]; // Maximum possible size (SHA512).
		size_t size;
	};

	/**
	 * Get a HashFunction instance for the given function.
	 *
	 * @param[in] function The selected hash function.
	 * @return An instance of HashFunction for the given function, or NULL if
	 *         not available.
	 * 
	 * Available functions:
	 * - "md5"
	 * - "sha1"
	 * - "sha224"
	 * - "sha256"
	 * - "sha384"
	 * - "sha512"
	 **/
	static HashFunction *getHashFunction(std::string function);

	virtual ~HashFunction() {}

	/**
	 * Hash the input, producing an set of bytes as output.
	 *
	 * @param[in] function The selected hash function.
	 * @param[in] input The input data to hash.
	 * @param[in] length The length of the input data.
	 * @param[out] output The result of the hash function.
	 **/
	virtual void hash(std::string function, const char *input, uint64_t length, Value &output) const = 0;

	/**
	 * @param[in] function The requested hash function.
	 * @return Whether this HashFunction instance implements the given function.
	 **/
	virtual bool isSupported(std::string function) const = 0;

protected:

	HashFunction() {}

}; // HashFunction

} // data
} // eve
