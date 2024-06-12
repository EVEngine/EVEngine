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

#include "DataModule.h"
#include "common/b64.h"
#include "common/Exception.h"
#include "HashFunction.h"

// STL
#include <cmath>
#include <list>
#include <iostream>

namespace
{

static const char hexchars[] = "0123456789abcdef";

char *bytesToHex(const uint8_t *src, size_t srclen, size_t &dstlen)
{
	dstlen = srclen * 2;

	if (dstlen == 0)
		return nullptr;

	char *dst = nullptr;
	try
	{
		dst = new char[dstlen + 1];
	}
	catch (std::exception &)
	{
		throw eve::Exception("Out of memory.");
	}

	for (size_t i = 0; i < srclen; i++)
	{
		uint8_t b = src[i];
		dst[i * 2 + 0] = hexchars[b >> 4];
		dst[i * 2 + 1] = hexchars[b & 0xF];
	}

	dst[dstlen] = '\0';
	return dst;
}

uint8_t nibble(char c)
{
	if (c >= '0' && c <= '9')
		return (uint8_t) (c - '0');

	if (c >= 'A' && c <= 'F')
		return (uint8_t) (c - 'A' + 0x0a);

	if (c >= 'a' && c <= 'f')
		return (uint8_t) (c - 'a' + 0x0a);

	return 0;
}

uint8_t *hexToBytes(const char *src, size_t srclen, size_t &dstlen)
{
	if (srclen >= 2 && src[0] == '0' && (src[1] == 'x' || src[1] == 'X'))
	{
		src += 2;
		srclen -= 2;
	}

	dstlen = (srclen + 1) / 2;

	if (dstlen == 0)
		return nullptr;

	uint8_t *dst = nullptr;
	try
	{
		dst = new uint8_t[dstlen];
	}
	catch (std::exception &)
	{
		throw eve::Exception("Out of memory.");
	}

	for (size_t i = 0; i < dstlen; i++)
	{
		dst[i] = nibble(src[i * 2]) << 4;

		if (i * 2 + 1 < srclen)
			dst[i] |= nibble(src[i * 2 + 1]);
	}

	return dst;
}

} // anonymous namespace

namespace eve
{
namespace data
{

CompressedData *compress(std::string format, const char *rawbytes, size_t rawsize, int level)
{
	Compressor *compressor = Compressor::getCompressor(format);

	if (compressor == nullptr)
		throw eve::Exception("Invalid compression format.");

	size_t compressedsize = 0;
	char *cbytes = compressor->compress(format, rawbytes, rawsize, level, compressedsize);

	CompressedData *data = nullptr;

	try
	{
		data = new CompressedData(format, cbytes, compressedsize, rawsize, true);
	}
	catch (eve::Exception &)
	{
		delete[] cbytes;
		throw;
	}

	return data;
}

char *decompress(CompressedData *data, size_t &decompressedsize)
{
	size_t rawsize = data->getDecompressedSize();

	char *rawbytes = decompress(data->getFormat(), (const char *) data->getData(),
	                            data->getSize(), rawsize);

	decompressedsize = rawsize;
	return rawbytes;
}

char *decompress(std::string format, const char *cbytes, size_t compressedsize, size_t &rawsize)
{
	Compressor *compressor = Compressor::getCompressor(format);

	if (compressor == nullptr)
		throw eve::Exception("Invalid compression format.");

	return compressor->decompress(format, cbytes, compressedsize, rawsize);
}

char *encode(std::string format, const char *src, size_t srclen, size_t &dstlen, size_t linelen)
{
	if (format == "hex")
		return bytesToHex((const uint8_t *) src, srclen, dstlen);
	else
		return b64_encode(src, srclen, linelen, dstlen);
}

char *decode(std::string format, const char *src, size_t srclen, size_t &dstlen)
{
	if (format == "hex")
		return (char *) hexToBytes(src, srclen, dstlen);
	else
		return b64_decode(src, srclen, dstlen);
}

std::string hash(std::string function, Data *input)
{
	return hash(function, (const char*) input->getData(), input->getSize());
}

std::string hash(std::string function, const char *input, uint64_t size)
{
	HashFunction::Value output;
	hash(function, input, size, output);
	return std::string(output.data, output.size);
}

void hash(std::string function, Data *input, HashFunction::Value &output)
{
	hash(function, (const char*) input->getData(), input->getSize(), output);
}

void hash(std::string function, const char *input, uint64_t size, HashFunction::Value &output)
{
	HashFunction *hashfunction = HashFunction::getHashFunction(function);
	if (hashfunction == nullptr)
		throw eve::Exception("Invalid hash function.");

	hashfunction->hash(function, input, size, output);
}

DataModule::DataModule()
{
}

DataModule::~DataModule()
{
}

DataView *DataModule::newDataView(Data *data, size_t offset, size_t size)
{
	return new DataView(data, offset, size);
}

ByteData *DataModule::newByteData(size_t size)
{
	return new ByteData(size);
}

ByteData *DataModule::newByteData(const void *d, size_t size)
{
	return new ByteData(d, size);
}

ByteData *DataModule::newByteData(void *d, size_t size, bool own)
{
	return new ByteData(d, size, own);
}


} // data
} // eve
