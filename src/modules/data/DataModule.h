#pragma once

#include "CompressedData.h"
#include "Compressor.h"
#include "HashFunction.h"
#include "DataView.h"
#include "ByteData.h"
#include "JsonDocument.h"
#include "XmlDocument.h"

#include "common/Module.h"

#include <string>

namespace eve
{
namespace data
{

enum ContainerType
{
	CONTAINER_DATA,
	CONTAINER_STRING,
	CONTAINER_MAX_ENUM
};

/**
 * @brief Compresses a block of memory using the given compression format.
 *
 * @param format The compression format to use.
 * @param rawbytes The data to compress.
 * @param rawsize The size in bytes of the data to compress.
 * @param level The amount of compression to apply (between 0 and 9.)
 *              A value of -1 indicates the default amount of compression.
 *              Specific formats may not use every level.
 * @return The newly compressed data.
 **/
CompressedData *compress(std::string format, const char *rawbytes, size_t rawsize, int level = -1);

/**
 * @brief Decompresses existing compressed data into raw bytes.
 *
 * @param[in] data The compressed data to decompress.
 * @param[out] decompressedsize The size in bytes of the decompressed data.
 * @return The newly decompressed data (allocated with new[]).
 **/
char *decompress(CompressedData *data, size_t &decompressedsize);

/**
 * @brief Decompresses existing compressed data into raw bytes.
 *
 * @param[in] format The compression format the data is in.
 * @param[in] cbytes The compressed data to decompress.
 * @param[in] compressedsize The size in bytes of the compressed data.
 * @param[in,out] rawsize On input, the size in bytes of the original
 *               uncompressed data, or 0 if unknown. On return, the size in
 *               bytes of the newly decompressed data.
 * @return The newly decompressed data (allocated with new[]).
 **/
char *decompress(std::string format, const char *cbytes, size_t compressedsize, size_t &rawsize);

/**
 * @brief Encodes raw bytes (e.g. hex / base64) into a text buffer.
 * @param format The encoding format ("hex" or "base64").
 * @param src The raw bytes to encode.
 * @param srclen Size in bytes of src.
 * @param[out] dstlen Receives the encoded length in bytes.
 * @param linelen Line width for the encoded output (0 = single line).
 * @return The newly allocated encoded buffer (allocated with new[]; caller frees).
 * @throws eve::Exception on an unsupported format.
 **/
char *encode(std::string format, const char *src, size_t srclen, size_t &dstlen, size_t linelen = 0);

/**
 * @brief Decodes a text buffer (hex / base64) back into raw bytes.
 * @param format The encoding format ("hex" or "base64").
 * @param src The encoded text.
 * @param srclen Size in bytes of src.
 * @param[out] dstlen Receives the decoded length in bytes.
 * @return The newly allocated decoded buffer (allocated with new[]; caller frees).
 * @throws eve::Exception on an unsupported format or malformed input.
 **/
char *decode(std::string format, const char *src, size_t srclen, size_t &dstlen);

/**
 * @brief Hash the input, producing an set of bytes as output.
 *
 * @param[in] function The selected hash function.
 * @param[in] input The input data to hash.
 * @return An std::string of bytes, representing the result of the hash
 *         function.
 **/
std::string hash(std::string function, Data *input);
std::string hash(std::string function, const char *input, uint64_t size);
void hash(std::string function, Data *input, HashFunction::Value &output);
void hash(std::string function, const char *input, uint64_t size, HashFunction::Value &output);



class DataModule : public Module
{
public:
	Module_REG(DataModule);
	DataModule();
	virtual ~DataModule();

	DataView *newDataView(Data *data, size_t offset, size_t size);
	ByteData *newByteData(size_t size);
	ByteData *newByteData(const void *d, size_t size);
	ByteData *newByteData(void *d, size_t size, bool own);

	JsonDocument *newJsonDocument();
	JsonDocument *decodeJson(const std::string &text);
	JsonDocument *decodeJson(const std::string &text, std::string *error);
	JsonDocument *decodeJson(Data *data, std::string *error = nullptr);
	std::string   encodeJson(JsonDocument *doc, bool pretty = false);
	ByteData *    encodeJsonData(JsonDocument *doc, bool pretty = false);

	XmlDocument *newXmlDocument();
	XmlDocument *decodeXml(const std::string &text);
	XmlDocument *decodeXml(const std::string &text, std::string *error);
	XmlDocument *decodeXml(Data *data, std::string *error = nullptr);
	std::string  encodeXml(XmlDocument *doc, bool pretty = false);
	ByteData *   encodeXmlData(XmlDocument *doc, bool pretty = false);

}; // DataModule

} // data
} // eve
