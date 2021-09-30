#pragma once

#include "common/Data.h"
#include "Compressor.h"

namespace eve
{
namespace data
{

/**
 * Stores byte data compressed via DataModule::compress.
 **/
class CompressedData : public eve::Data
{
public:
	/**
	 * Constructor just stores already-compressed data in the object.
	 **/
	CompressedData(Compressor::Format format, char *cdata, size_t compressedsize, size_t rawsize, bool own = true);
	CompressedData(const CompressedData &c);
	virtual ~CompressedData();

	/**
	 * Gets the format that was used to compress the data.
	 **/
	Compressor::Format getFormat() const;

	/**
	 * Gets the original (uncompressed) size of the compressed data. May return
	 * 0 if the uncompressed size is unknown.
	 **/
	size_t getDecompressedSize() const;

	// Implements Data.
	CompressedData *clone() const override;
	void *getData() const override;
	size_t getSize() const override;

private:

	Compressor::Format format;

	char *data;
	size_t dataSize;

	size_t originalSize;

}; // CompressedData

} // data
} // eve
