#pragma once
#include "common/Export.h"


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

/** @brief ContainerType public API. */
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
 * @ownership Caller owns the returned CompressedData and releases it through its documented destroy path.
 * @lifetime The returned data is independent of rawbytes and remains valid until released.
 **/
EVENGINE_API_FOUNDATION CompressedData *compress(std::string format, const char *rawbytes, size_t rawsize,
                                                 int level = -1);

/**
 * @brief Decompresses existing compressed data into raw bytes.
 *
 * @param[in] data The compressed data to decompress.
 * @param[out] decompressedsize The size in bytes of the decompressed data.
 * @return The newly decompressed data (allocated with new[]).
 * @ownership Caller owns the returned buffer and releases it with delete[].
 * @lifetime The returned buffer is independent of the compressed input and remains valid until freed.
 **/
EVENGINE_API_FOUNDATION char *decompress(CompressedData *data, size_t &decompressedsize);

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
EVENGINE_API_FOUNDATION char *decompress(std::string format, const char *cbytes, size_t compressedsize,
                                         size_t &rawsize);

/**
 * @brief Encodes raw bytes (e.g. hex / base64) into a text buffer.
 * @param format The encoding format ("hex" or "base64").
 * @param src The raw bytes to encode.
 * @param srclen Size in bytes of src.
 * @param[out] dstlen Receives the encoded length in bytes.
 * @param linelen Line width for the encoded output (0 = single line).
 * @return The newly allocated encoded buffer (allocated with new[]; caller frees).
 * @ownership Caller owns the returned buffer and releases it with delete[].
 * @lifetime The returned buffer is independent of src and remains valid until freed.
 * @throws eve::Exception on an unsupported format.
 **/
EVENGINE_API_FOUNDATION char *encode(std::string format, const char *src, size_t srclen, size_t &dstlen,
                                     size_t linelen = 0);

/**
 * @brief Decodes a text buffer (hex / base64) back into raw bytes.
 * @param format The encoding format ("hex" or "base64").
 * @param src The encoded text.
 * @param srclen Size in bytes of src.
 * @param[out] dstlen Receives the decoded length in bytes.
 * @return The newly allocated decoded buffer (allocated with new[]; caller frees).
 * @ownership Caller owns the returned buffer and releases it with delete[].
 * @lifetime The returned buffer is independent of src and remains valid until freed.
 * @throws eve::Exception on an unsupported format or malformed input.
 **/
EVENGINE_API_FOUNDATION char *decode(std::string format, const char *src, size_t srclen, size_t &dstlen);

/**
 * @brief Hash the input, producing an set of bytes as output.
 *
 * @param[in] function The selected hash function.
 * @param[in] input The input data to hash.
 * @return An std::string of bytes, representing the result of the hash
 *         function.
 **/
EVENGINE_API_FOUNDATION std::string hash(std::string function, Data *input);
/** @brief Hash. */
EVENGINE_API_FOUNDATION std::string hash(std::string function, const char *input, uint64_t size);
/** @brief Hash. */
EVENGINE_API_FOUNDATION void        hash(std::string function, Data *input, HashFunction::Value &output);
/** @brief Hash. */
EVENGINE_API_FOUNDATION void hash(std::string function, const char *input, uint64_t size, HashFunction::Value &output);


/** @brief EVENGINE_API_FOUNDATION public API. */
class EVENGINE_API_FOUNDATION DataModule : public Module {
public:
	Module_REG(DataModule);
	/** @brief Data module. */
	DataModule();
	/** @brief Data module. */
	virtual ~DataModule();

	/** @brief Creates a data view. @ownership Caller deletes unless documented otherwise. */
	DataView *newDataView(Data *data, size_t offset, size_t size);
	/** @brief Creates a byte data. @ownership Caller deletes unless documented otherwise. */
	ByteData *newByteData(size_t size);
	/** @brief Creates a byte data. @ownership Caller deletes unless documented otherwise. */
	ByteData *newByteData(const void *d, size_t size);
	/** @brief Creates a byte data. @ownership Caller deletes unless documented otherwise. */
	ByteData *newByteData(void *d, size_t size, bool own);

	/** @brief Creates a json document. @ownership Caller deletes unless documented otherwise. */
	JsonDocument *newJsonDocument();
	/** @brief Decode json. */
	JsonDocument *decodeJson(const std::string &text);
	/** @brief Decode json. */
	JsonDocument *decodeJson(const std::string &text, std::string *error);
	/** @brief Decode json. */
	JsonDocument *decodeJson(Data *data, std::string *error = nullptr);
	/** @brief Encode json. */
	std::string   encodeJson(JsonDocument *doc, bool pretty = false);
	/** @brief Encode json data. */
	ByteData *    encodeJsonData(JsonDocument *doc, bool pretty = false);

	/** @brief Creates a xml document. @ownership Caller deletes unless documented otherwise. */
	XmlDocument *newXmlDocument();
	/** @brief Decode xml. */
	XmlDocument *decodeXml(const std::string &text);
	/** @brief Decode xml. */
	XmlDocument *decodeXml(const std::string &text, std::string *error);
	/** @brief Decode xml. */
	XmlDocument *decodeXml(Data *data, std::string *error = nullptr);
	/** @brief Encode xml. */
	std::string  encodeXml(XmlDocument *doc, bool pretty = false);
	/** @brief Encode xml data. */
	ByteData *   encodeXmlData(XmlDocument *doc, bool pretty = false);

};  // DataModule

} // data
} // eve
