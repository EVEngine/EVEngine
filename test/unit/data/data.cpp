// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "data.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "data/ByteData.h"
#include "data/DataModule.h"
#include "data/DataView.h"
#include "data/JsonDocument.h"
#include "data/XmlDocument.h"

#include <cstring>
#include <memory>
#include <string>

namespace {

template <class Action>
bool throwsDataError(Action action) {
    try {
        action();
    } catch (const eve::Exception&) {
        return true;
    }
    return false;
}

std::string hex(const eve::data::HashFunction::Value& value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string    output;
    output.reserve(value.size * 2);
    for (std::size_t i = 0; i < value.size; ++i) {
        const auto byte = static_cast<unsigned char>(value.data[i]);
        output.push_back(digits[byte >> 4]);
        output.push_back(digits[byte & 15]);
    }
    return output;
}

}  // namespace

TEST_CASE("data.ByteData copies borrowed input and owns its clone") {
    char                input[] = {'a', 'b', 'c'};
    eve::data::ByteData borrowed(input, sizeof(input), false);
    input[0] = 'z';
    CHECK_EQ(borrowed.getSize(), sizeof(input));
    CHECK_EQ(static_cast<const char*>(borrowed.getData())[0], 'a');

    std::unique_ptr<eve::data::ByteData> clone(borrowed.clone());
    static_cast<char*>(borrowed.getData())[1] = 'x';
    CHECK_EQ(static_cast<const char*>(clone->getData())[1], 'b');
    REQUIRE(throwsDataError([]() { eve::data::ByteData empty(0); }));
}

TEST_CASE("data.DataModule byte factories preserve length and transferred ownership") {
    auto*                                module = eve::data::DataModule::create();
    std::unique_ptr<eve::data::ByteData> zeroed(module->newByteData(4));
    REQUIRE(zeroed != nullptr);
    CHECK_EQ(zeroed->getSize(), 4u);
    CHECK(std::memcmp(zeroed->getData(), "\0\0\0\0", 4) == 0);

    std::unique_ptr<eve::data::ByteData> copied(module->newByteData("xy", 2));
    REQUIRE(copied != nullptr);
    CHECK(std::memcmp(copied->getData(), "xy", 2) == 0);
    auto*                                transferred = new char[2]{'p', 'q'};
    std::unique_ptr<eve::data::ByteData> owned(module->newByteData(transferred, 2, true));
    REQUIRE(owned != nullptr);
    CHECK(owned->getData() == static_cast<void*>(transferred));
}

TEST_CASE("data.DataView exposes a bounded window of its live source") {
    auto*                                module = eve::data::DataModule::create();
    std::unique_ptr<eve::data::ByteData> bytes(module->newByteData("abcdef", 6));
    std::unique_ptr<eve::data::DataView> window(module->newDataView(bytes.get(), 2, 3));
    REQUIRE(window != nullptr);
    CHECK_EQ(window->getSize(), 3u);
    CHECK(std::memcmp(window->getData(), "cde", 3) == 0);
    static_cast<char*>(bytes->getData())[2] = 'X';
    CHECK_EQ(static_cast<const char*>(window->getData())[0], 'X');
    std::unique_ptr<eve::data::DataView> clone(window->clone());
    CHECK_EQ(static_cast<const char*>(clone->getData())[0], 'X');
    CHECK(throwsDataError([&]() { std::unique_ptr<eve::data::DataView> bad(module->newDataView(bytes.get(), 5, 2)); }));
    CHECK(throwsDataError([&]() { std::unique_ptr<eve::data::DataView> bad(module->newDataView(bytes.get(), 0, 0)); }));
}

TEST_CASE("data.JsonDocument: decode create encode and errors describe the document contract") {
    SUB_CASE("object and array roots decode with distinct structure and encode mutations") {
        auto*                                    module = eve::data::DataModule::create();
        std::string                              error;
        std::unique_ptr<eve::data::JsonDocument> object(module->decodeJson(R"({"width":640,"name":"hi"})", &error));
        REQUIRE(object != nullptr);
        CHECK(error.empty());
        CHECK(object->isObject());
        CHECK(!object->isArray());
        CHECK_EQ(object->object()->getValue<int>("width"), 640);
        object->object()->set("width", 1280);
        CHECK(module->encodeJson(object.get(), true).find("1280") != std::string::npos);

        std::unique_ptr<eve::data::JsonDocument> array(module->decodeJson("[1,2,3]"));
        REQUIRE(array != nullptr);
        CHECK(array->isArray());
        CHECK(!array->object());
        REQUIRE(array->array());
        CHECK_EQ(array->array()->size(), 3u);
    };

    SUB_CASE("malformed text reports an error while byte input decodes to an owning document") {
        auto*                                    module = eve::data::DataModule::create();
        std::string                              error;
        std::unique_ptr<eve::data::JsonDocument> malformed(module->decodeJson("{bad", &error));
        CHECK(malformed == nullptr);
        CHECK(!error.empty());
        CHECK(module->decodeJson("{bad") == nullptr);

        const char                               source[] = "{\"a\":1}";
        eve::data::ByteData                      bytes(source, sizeof(source) - 1);
        std::unique_ptr<eve::data::JsonDocument> parsed(module->decodeJson(&bytes));
        REQUIRE(parsed != nullptr);
        CHECK_EQ(parsed->object()->getValue<int>("a"), 1);
        std::unique_ptr<eve::data::ByteData> encoded(module->encodeJsonData(parsed.get(), false));
        REQUIRE(encoded != nullptr);
        CHECK(encoded->getSize() > 0);
    };

    SUB_CASE("newJsonDocument starts as a mutable object") {
        auto*                                    module = eve::data::DataModule::create();
        std::unique_ptr<eve::data::JsonDocument> document(module->newJsonDocument());
        REQUIRE(document != nullptr);
        CHECK(document->isObject());
        document->object()->set("key", "value");
        CHECK(module->encodeJson(document.get()).find("value") != std::string::npos);
    };
}

TEST_CASE("data.XmlDocument: parsing encoding and errors describe the document contract") {
    SUB_CASE("valid XML preserves attributes through mutation and encoding") {
        auto*                                   module = eve::data::DataModule::create();
        std::string                             error;
        std::unique_ptr<eve::data::XmlDocument> document(
            module->decodeXml("<root a=\"1\"><child>hi</child></root>", &error));
        REQUIRE(document != nullptr);
        CHECK(error.empty());
        auto* root = document->get()->documentElement();
        REQUIRE(root != nullptr);
        CHECK_EQ(std::string(root->getAttribute("a")), std::string("1"));
        root->setAttribute("a", "2");
        CHECK(module->encodeXml(document.get(), true).find("a=\"2\"") != std::string::npos);
        std::unique_ptr<eve::data::ByteData> encoded(module->encodeXmlData(document.get()));
        REQUIRE(encoded != nullptr);
        CHECK(encoded->getSize() > 0);
    };

    SUB_CASE("incomplete input fails and byte input creates a document") {
        auto*                                   module = eve::data::DataModule::create();
        std::string                             error;
        std::unique_ptr<eve::data::XmlDocument> malformed(module->decodeXml("<root>", &error));
        CHECK(malformed == nullptr);
        CHECK(!error.empty());
        CHECK(module->decodeXml("<root>") == nullptr);
        const char                              source[] = "<r>x</r>";
        eve::data::ByteData                     bytes(source, sizeof(source) - 1);
        std::unique_ptr<eve::data::XmlDocument> parsed(module->decodeXml(&bytes));
        REQUIRE(parsed != nullptr);
        CHECK(parsed->get()->documentElement() != nullptr);
        std::unique_ptr<eve::data::XmlDocument> empty(module->newXmlDocument());
        REQUIRE(empty != nullptr);
        CHECK(!empty->empty());
        CHECK(module->encodeXmlData(nullptr) == nullptr);
    };
}

TEST_CASE("data.compress lz4 round-trip owns independent compressed bytes") {
    const char                                 source[] = "aaaaaaaaaaaaaaaaevengine-compress";
    const auto                                 size     = sizeof(source) - 1;
    std::unique_ptr<eve::data::CompressedData> compressed(eve::data::compress("lz4", source, size));
    REQUIRE(compressed != nullptr);
    CHECK_EQ(compressed->getFormat(), std::string("lz4"));
    CHECK_EQ(compressed->getDecompressedSize(), size);
    std::unique_ptr<eve::data::CompressedData> clone(compressed->clone());
    CHECK_EQ(clone->getSize(), compressed->getSize());
    std::size_t             restoredSize = 0;
    std::unique_ptr<char[]> restored(eve::data::decompress(clone.get(), restoredSize));
    // zeroerr prints decomposed char-array pointers as C strings; this buffer is binary.
    REQUIRE(static_cast<bool>(restored));
    CHECK_EQ(restoredSize, size);
    CHECK(std::memcmp(restored.get(), source, size) == 0);
    CHECK(
        throwsDataError([]() { std::unique_ptr<eve::data::CompressedData> bad(eve::data::compress("nope", "x", 1)); }));
}

TEST_CASE("data.encode hex and base64 round-trip exact binary length") {
    const char source[] = {'H', '\0', 'i'};
    for (const auto* format : {"hex", "base64"}) {
        std::size_t             encodedSize = 0;
        std::unique_ptr<char[]> encoded(eve::data::encode(format, source, sizeof(source), encodedSize));
        REQUIRE(static_cast<bool>(encoded));
        CHECK(encodedSize > sizeof(source));
        std::size_t             decodedSize = 0;
        std::unique_ptr<char[]> decoded(eve::data::decode(format, encoded.get(), encodedSize, decodedSize));
        REQUIRE(static_cast<bool>(decoded));
        CHECK_EQ(decodedSize, sizeof(source));
        CHECK(std::memcmp(decoded.get(), source, sizeof(source)) == 0);
    }
    CHECK(throwsDataError([]() {
        std::size_t             size = 0;
        std::unique_ptr<char[]> bad(eve::data::encode("bad", "x", 1, size));
    }));
    CHECK(throwsDataError([]() {
        std::size_t             size = 0;
        std::unique_ptr<char[]> bad(eve::data::decode("bad", "eA==", 4, size));
    }));
}

TEST_CASE("data.hash uses known digests for empty and nonempty inputs") {
    struct Case {
        const char* algorithm;
        const char* input;
        std::size_t size;
        const char* expected;
    };
    const Case cases[] = {
        {"md5", "abc", 3, "900150983cd24fb0d6963f7d28e17f72"},
        {"sha1", "", 0, "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
        {"sha256", "abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"sha512", "", 0,
         "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
         "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"},
    };
    for (const auto& example : cases) {
        eve::data::HashFunction::Value digest{};
        eve::data::hash(example.algorithm, example.input, example.size, digest);
        CHECK_EQ(hex(digest), std::string(example.expected));
    }
    eve::data::ByteData bytes("abc", 3);
    CHECK_EQ(eve::data::hash("md5", &bytes), eve::data::hash("md5", "abc", 3));
    REQUIRE(throwsDataError([]() { (void)eve::data::hash("unknown", "abc", 3); }));
}
