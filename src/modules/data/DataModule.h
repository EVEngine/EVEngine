#pragma once
#include "common/Export.h"


#include "ByteData.h"
#include "CompressedData.h"
#include "Compressor.h"
#include "DataCodec.h"
#include "DataView.h"
#include "HashFunction.h"
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
