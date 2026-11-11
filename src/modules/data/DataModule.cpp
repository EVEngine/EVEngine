

#include "DataModule.h"
#include "HashFunction.h"
#include "common/Exception.h"

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

namespace eve
{
namespace data
{

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
