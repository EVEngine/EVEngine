

#include "DataModule.h"
#include "common/b64.h"
#include "common/Exception.h"
#include "HashFunction.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <Poco/Exception.h>
#include <Poco/DOM/DOMParser.h>
#include <Poco/DOM/DOMWriter.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/XML/XMLWriter.h>

// STL
#include <cmath>
#include <functional>
#include <iostream>
#include <list>
#include <sstream>

namespace eve
{
namespace data
{

Module_IMPL(DataModule, new DataModule());

void DataModule::expose(ssq::Table& table)
{
	auto cls = table.addClass(name, DataModule::create, false);
	expose(cls);

	auto json = table.addClass<JsonDocument>(
		"JsonDocument",
		std::function<JsonDocument*()>([]() { return new JsonDocument(); }),
		true);
	json.addFunc("empty", &JsonDocument::empty);
	json.addFunc("isObject", &JsonDocument::isObject);
	json.addFunc("isArray", &JsonDocument::isArray);

	auto xml = table.addClass<XmlDocument>(
		"XmlDocument",
		std::function<XmlDocument*()>([]() { return new XmlDocument(); }),
		true);
	xml.addFunc("empty", &XmlDocument::empty);
}

void DataModule::expose(ssq::Class& cls)
{
	cls.addFunc("getName", &DataModule::getName);
	cls.addFunc("newByteData",
		static_cast<ByteData* (DataModule::*)(size_t)>(&DataModule::newByteData));
	cls.addFunc("newDataView", &DataModule::newDataView);
	cls.addFunc("newJsonDocument", &DataModule::newJsonDocument);
	cls.addFunc("newXmlDocument", &DataModule::newXmlDocument);
	cls.addFunc("decodeJson",
		static_cast<JsonDocument* (DataModule::*)(const std::string&)>(&DataModule::decodeJson));
	cls.addFunc("decodeXml",
		static_cast<XmlDocument* (DataModule::*)(const std::string&)>(&DataModule::decodeXml));
	cls.addFunc("encodeJson", &DataModule::encodeJson);
	cls.addFunc("encodeXml", &DataModule::encodeXml);
}

} // data
} // eve

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

JsonDocument *DataModule::newJsonDocument()
{
	return new JsonDocument();
}

JsonDocument *DataModule::decodeJson(const std::string &text, std::string *error)
{
	try
	{
		Poco::JSON::Parser parser;
		Poco::Dynamic::Var result = parser.parse(text);
		return new JsonDocument(result);
	}
	catch (const Poco::Exception &ex)
	{
		if (error)
			*error = ex.displayText();
		return nullptr;
	}
}

JsonDocument *DataModule::decodeJson(const std::string &text)
{
	return decodeJson(text, nullptr);
}

JsonDocument *DataModule::decodeJson(Data *data, std::string *error)
{
	if (!data || !data->getData())
	{
		if (error)
			*error = "null data";
		return nullptr;
	}
	std::string text(static_cast<const char *>(data->getData()), data->getSize());
	return decodeJson(text, error);
}

std::string DataModule::encodeJson(JsonDocument *doc, bool pretty)
{
	if (!doc)
		return {};
	try
	{
		std::ostringstream oss;
		int indent = pretty ? 2 : 0;
		Poco::JSON::Stringifier::stringify(doc->root(), oss, indent);
		return oss.str();
	}
	catch (const Poco::Exception &)
	{
		return {};
	}
}

ByteData *DataModule::encodeJsonData(JsonDocument *doc, bool pretty)
{
	if (!doc)
		return nullptr;
	std::string out = encodeJson(doc, pretty);
	if (out.empty())
		return nullptr;
	return new ByteData(out.data(), out.size());
}

XmlDocument *DataModule::newXmlDocument()
{
	return new XmlDocument();
}

XmlDocument *DataModule::decodeXml(const std::string &text, std::string *error)
{
	try
	{
		Poco::XML::DOMParser parser;
		Poco::AutoPtr<Poco::XML::Document> pdoc = parser.parseString(text);
		return new XmlDocument(pdoc);
	}
	catch (const Poco::Exception &ex)
	{
		if (error)
			*error = ex.displayText();
		return nullptr;
	}
}

XmlDocument *DataModule::decodeXml(const std::string &text)
{
	return decodeXml(text, nullptr);
}

XmlDocument *DataModule::decodeXml(Data *data, std::string *error)
{
	if (!data || !data->getData())
	{
		if (error)
			*error = "null data";
		return nullptr;
	}
	std::string text(static_cast<const char *>(data->getData()), data->getSize());
	return decodeXml(text, error);
}

std::string DataModule::encodeXml(XmlDocument *doc, bool pretty)
{
	if (!doc || !doc->get())
		return {};
	try
	{
		Poco::XML::DOMWriter writer;
		if (pretty)
			writer.setOptions(Poco::XML::XMLWriter::PRETTY_PRINT);
		std::ostringstream oss;
		writer.writeNode(oss, doc->get());
		return oss.str();
	}
	catch (const Poco::Exception &)
	{
		return {};
	}
}

ByteData *DataModule::encodeXmlData(XmlDocument *doc, bool pretty)
{
	if (!doc)
		return nullptr;
	std::string out = encodeXml(doc, pretty);
	if (out.empty())
		return nullptr;
	return new ByteData(out.data(), out.size());
}


} // data
} // eve
