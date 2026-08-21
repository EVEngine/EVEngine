

#pragma once

#include "common/Data.h"
#include <cstdint>
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
	 * @brief Get a HashFunction instance for the given function.
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
	 * @brief Hash the input, producing an set of bytes as output.
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
