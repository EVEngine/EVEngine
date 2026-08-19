#pragma once

#include <string>

namespace eve
{
namespace data
{

/**
 * Base class for backends for different compression formats.
 **/
class Compressor
{
public:

	/**
	 * Gets a Compressor that can compress and decompress a specific format.
	 * Returns null if there are no supported compressors for the given format.
	 * Available formats are:
	 * - "lz4"
	 * - "zlib"
	 * - "gzip"
	 * - "deflate"
	 **/
	static Compressor *getCompressor(std::string format);

	virtual ~Compressor() {}

	/**
	 * Compresses input data, and returns the compressed result.
	 *
	 * @param[in] format The format to compress to.
	 * @param[in] data The input (uncompressed) data.
	 * @param[in] dataSize The size in bytes of the input data.
	 * @param[in] level The amount of compression to apply (between 0 and 9.)
	 *            A value of -1 indicates the default amount of compression.
	 *            Specific formats may not use every level.
	 * @param[out] compressedSize The size in bytes of the compressed result.
	 *
	 * @return The newly compressed data (allocated with new[]).
	 **/
	virtual char *compress(std::string format, const char *data, size_t dataSize, int level, size_t &compressedSize) = 0;

	/**
	 * Decompresses compressed data, and returns the decompressed result.
	 *
	 * @param[in] format The format the compressed data is in.
	 * @param[in] data The input (compressed) data.
	 * @param[in] dataSize The size in bytes of the compressed data.
	 * @param[in,out] decompressedSize On input, the size in bytes of the
	 *               original uncompressed data, or 0 if unknown. On return, the
	 *               size in bytes of the decompressed data.
	 *
	 * @return The decompressed data (allocated with new[]).
	 **/
	virtual char *decompress(std::string format, const char *data, size_t dataSize, size_t &decompressedSize) = 0;

	/**
	 * Gets whether a specific format is supported by this backend.
	 **/
	virtual bool isSupported(std::string format) const = 0;

protected:

	Compressor() {}

}; // Compressor

} // data
} // eve
