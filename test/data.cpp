#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "data/ByteData.h"
#include "data/DataModule.h"
#include "data/JsonDocument.h"
#include "data/XmlDocument.h"

#include <cstring>
#include <string>

TEST_CASE("data.JsonObjectRoundTrip") {
    auto* dm = eve::data::DataModule::create();
    std::string err;
    auto* doc = dm->decodeJson(R"({"width":640,"name":"hi"})", &err);
    REQUIRE(doc != nullptr);
    CHECK(err.empty());
    CHECK(doc->isObject());
    CHECK(!doc->isArray());
    auto obj = doc->object();
    REQUIRE(obj);
    CHECK(obj->getValue<int>("width") == 640);
    obj->set("width", 1280);
    std::string out = dm->encodeJson(doc, true);
    CHECK(out.find("1280") != std::string::npos);
    delete doc;
}

TEST_CASE("data.JsonArrayRoot") {
    auto* dm = eve::data::DataModule::create();
    std::string err;
    auto* doc = dm->decodeJson("[1,2,3]", &err);
    REQUIRE(doc != nullptr);
    CHECK(doc->isArray());
    auto arr = doc->array();
    REQUIRE(arr);
    CHECK(arr->size() == 3);
    delete doc;
}

TEST_CASE("data.JsonInvalid") {
    auto* dm = eve::data::DataModule::create();
    std::string err;
    auto* doc = dm->decodeJson("{bad", &err);
    CHECK(doc == nullptr);
    CHECK(!err.empty());
}

TEST_CASE("data.JsonFromByteData") {
    auto* dm = eve::data::DataModule::create();
    const char* raw = "{\"a\":1}";
    eve::data::ByteData bytes(raw, std::strlen(raw));
    std::string err;
    auto* doc = dm->decodeJson(&bytes, &err);
    REQUIRE(doc != nullptr);
    CHECK(doc->object()->getValue<int>("a") == 1);
    auto* encoded = dm->encodeJsonData(doc, false);
    REQUIRE(encoded != nullptr);
    CHECK(encoded->getSize() > 0);
    delete encoded;
    delete doc;
}

TEST_CASE("data.JsonEmptyNew") {
    auto* dm = eve::data::DataModule::create();
    auto* doc = dm->newJsonDocument();
    REQUIRE(doc != nullptr);
    CHECK(doc->isObject());
    doc->object()->set("k", "v");
    CHECK(dm->encodeJson(doc).find("k") != std::string::npos);
    delete doc;
}

TEST_CASE("data.XmlRoundTrip") {
    auto* dm = eve::data::DataModule::create();
    std::string err;
    auto* doc = dm->decodeXml("<root a=\"1\"><child>hi</child></root>", &err);
    REQUIRE(doc != nullptr);
    CHECK(!doc->empty());
    auto* root = doc->get()->documentElement();
    REQUIRE(root != nullptr);
    CHECK(std::string(root->getAttribute("a")) == "1");
    root->setAttribute("a", "2");
    std::string out = dm->encodeXml(doc, true);
    CHECK(out.find("a=\"2\"") != std::string::npos);
    delete doc;
}

TEST_CASE("data.XmlInvalid") {
    auto* dm = eve::data::DataModule::create();
    std::string err;
    auto* doc = dm->decodeXml("<root>", &err);
    CHECK(doc == nullptr);
    CHECK(!err.empty());
}

TEST_CASE("data.XmlFromByteData") {
    auto* dm = eve::data::DataModule::create();
    const char* raw = "<r>x</r>";
    eve::data::ByteData bytes(raw, std::strlen(raw));
    std::string err;
    auto* doc = dm->decodeXml(&bytes, &err);
    REQUIRE(doc != nullptr);
    CHECK(doc->get()->documentElement() != nullptr);
    delete doc;
}

TEST_CASE("data.XmlEmptyNew") {
    auto* dm = eve::data::DataModule::create();
    auto* doc = dm->newXmlDocument();
    REQUIRE(doc != nullptr);
    CHECK(!doc->empty());
    delete doc;
}
