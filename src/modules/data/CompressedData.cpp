#include "CompressedData.h"

namespace eve
{
namespace data
{

CompressedData::CompressedData(Compressor::Format format, char *cdata, size_t compressedsize, size_t rawsize, bool own)
	: format(format)
	, data(nullptr)
	, dataSize(compressedsize)
	, originalSize(rawsize)
{
	if (own)
		data = cdata;
	else
	{
		try
		{
			data = new char[dataSize];
		}
		catch (std::bad_alloc &)
		{
			throw love::Exception("Out of memory.");
		}

		memcpy(data, cdata, dataSize);
	}
}

CompressedData::CompressedData(const CompressedData &c)
	: format(c.format)
	, data(nullptr)
	, dataSize(c.dataSize)
	, originalSize(c.originalSize)
{
	try
	{
		data = new char[dataSize];
	}
	catch (std::bad_alloc &)
	{
		throw love::Exception("Out of memory.");
	}

	memcpy(data, c.data, dataSize);
}

CompressedData::~CompressedData()
{
	delete[] data;
}

CompressedData *CompressedData::clone() const
{
	return new CompressedData(*this);
}

Compressor::Format CompressedData::getFormat() const
{
	return format;
}

size_t CompressedData::getDecompressedSize() const
{
	return originalSize;
}

void *CompressedData::getData() const
{
	return data;
}

size_t CompressedData::getSize() const
{
	return dataSize;
}

} // data
} // eve
