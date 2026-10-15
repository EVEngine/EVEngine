#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "data/ByteData.h"
#include "data/DataModule.h"
#include "data/DataView.h"
#include "data/JsonDocument.h"
#include "data/XmlDocument.h"
#include "common/Exception.h"

#include <cstring>
#include <memory>
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

TEST_CASE("data.ByteData.roundTrip") {
    const char raw[] = "hello";
    eve::data::ByteData bytes(raw, sizeof(raw) - 1);
    CHECK_EQ(bytes.getSize(), 5u);
    CHECK(std::memcmp(bytes.getData(), raw, 5) == 0);
    std::unique_ptr<eve::data::ByteData> cloned(bytes.clone());
    REQUIRE(cloned.get() != nullptr);
    CHECK_EQ(cloned->getSize(), 5u);
    CHECK(std::memcmp(cloned->getData(), raw, 5) == 0);
}

TEST_CASE("data.ByteData.sizeZeroThrows") {
    bool threw = false;
    try {
        eve::data::ByteData z(0);
        (void)z;
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("data.ByteData.ownFalseCopies") {
    char buf[] = {'a', 'b', 'c'};
    eve::data::ByteData bytes(buf, 3, false);
    buf[0] = 'z';
    CHECK_EQ(static_cast<char*>(bytes.getData())[0], 'a');
}

TEST_CASE("data.DataView.windowAndBounds") {
    auto* dm = eve::data::DataModule::create();
    const char raw[] = "abcdef";
    std::unique_ptr<eve::data::ByteData> base(dm->newByteData(raw, 6));
    std::unique_ptr<eve::data::DataView> view(dm->newDataView(base.get(), 2, 3));
    REQUIRE(view.get() != nullptr);
    CHECK_EQ(view->getSize(), 3u);
    CHECK(std::memcmp(view->getData(), "cde", 3) == 0);
    std::unique_ptr<eve::data::DataView> cloned(view->clone());
    CHECK_EQ(cloned->getSize(), 3u);

    bool threw = false;
    try {
        dm->newDataView(base.get(), 5, 10);
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        dm->newDataView(base.get(), 0, 0);
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("data.DataModule.newByteDataFactories") {
    auto* dm = eve::data::DataModule::create();
    std::unique_ptr<eve::data::ByteData> a(dm->newByteData(4));
    REQUIRE(a.get() != nullptr);
    CHECK_EQ(a->getSize(), 4u);

    const char raw[] = "xy";
    std::unique_ptr<eve::data::ByteData> b(dm->newByteData(raw, 2));
    CHECK(std::memcmp(b->getData(), "xy", 2) == 0);

    char* owned = new char[2]{'p', 'q'};
    std::unique_ptr<eve::data::ByteData> c(dm->newByteData(owned, 2, true));
    CHECK(std::memcmp(c->getData(), "pq", 2) == 0);
}

TEST_CASE("data.compress.lz4RoundTrip") {
    const char raw[] = "aaaaaaaaaaaaaaaaevengine-compress";
    const size_t n = sizeof(raw) - 1;
    std::unique_ptr<eve::data::CompressedData> cdata(
        eve::data::compress("lz4", raw, n, -1));
    REQUIRE(cdata.get() != nullptr);
    CHECK_EQ(cdata->getFormat(), std::string("lz4"));
    CHECK_EQ(cdata->getDecompressedSize(), n);
    CHECK_GT(cdata->getSize(), 0u);

    size_t outn = 0;
    std::unique_ptr<char[]> out(eve::data::decompress(cdata.get(), outn));
    REQUIRE(out.get() != nullptr);
    CHECK_EQ(outn, n);
    CHECK(std::memcmp(out.get(), raw, n) == 0);

    std::unique_ptr<eve::data::CompressedData> cloned(cdata->clone());
    CHECK_EQ(cloned->getFormat(), std::string("lz4"));
}

TEST_CASE("data.compress.invalidFormatThrows") {
    bool threw = false;
    try {
        eve::data::compress("nope", "x", 1, -1);
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(eve::data::Compressor::getCompressor("nope") == nullptr);
    CHECK(eve::data::Compressor::getCompressor("lz4") != nullptr);
}

TEST_CASE("data.decompress.invalidFormatThrows") {
    bool threw = false;
    try {
        size_t rawsize = 0;
        eve::data::decompress("nope", "x", 1, rawsize);
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("data.encode.hexAndBase64RoundTrip") {
    const char raw[] = "Hi";
    size_t dstlen = 0;
    std::unique_ptr<char[]> hex(eve::data::encode("hex", raw, 2, dstlen));
    REQUIRE(hex.get() != nullptr);
    CHECK_GT(dstlen, 0u);

    size_t backn = 0;
    std::unique_ptr<char[]> back(eve::data::decode("hex", hex.get(), dstlen, backn));
    REQUIRE(back.get() != nullptr);
    CHECK_EQ(backn, 2u);
    CHECK(std::memcmp(back.get(), raw, 2) == 0);

    dstlen = 0;
    std::unique_ptr<char[]> b64(eve::data::encode("base64", raw, 2, dstlen));
    REQUIRE(b64.get() != nullptr);
    backn = 0;
    std::unique_ptr<char[]> back2(eve::data::decode("base64", b64.get(), dstlen, backn));
    REQUIRE(back2.get() != nullptr);
    CHECK_EQ(backn, 2u);
    CHECK(std::memcmp(back2.get(), raw, 2) == 0);
}

TEST_CASE("data.hash.md5KnownAndInvalid") {
    const char* msg = "abc";
    // MD5("abc") = 900150983cd24fb0d6963f7d28e17f72
    std::string dig = eve::data::hash("md5", msg, 3);
    CHECK_EQ(dig.size(), 16u);
    static const unsigned char expect[16] = {
        0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0,
        0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72};
    CHECK(std::memcmp(dig.data(), expect, 16) == 0);

    eve::data::ByteData bytes(msg, 3);
    std::string dig2 = eve::data::hash("md5", &bytes);
    CHECK(dig == dig2);

    eve::data::HashFunction::Value val{};
    eve::data::hash("md5", msg, 3, val);
    CHECK_EQ(val.size, 16u);

    CHECK(eve::data::HashFunction::getHashFunction("md5") != nullptr);
    CHECK(eve::data::HashFunction::getHashFunction("nope") == nullptr);

    bool threw = false;
    try {
        eve::data::hash("nope", msg, 3);
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}

namespace {

std::string hashToHex(const eve::data::HashFunction::Value &v) {
    static const char *kHex = "0123456789abcdef";
    std::string out;
    out.reserve(v.size * 2);
    for (size_t i = 0; i < v.size; ++i) {
        const unsigned char b = static_cast<unsigned char>(v.data[i]);
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0xF]);
    }
    return out;
}

}  // namespace

TEST_CASE("data.hash.shaFamilyKnownVectors") {
    struct Vec {
        const char *fn;
        const char *in;
        size_t len;
        const char *hex;
    };
    const Vec cases[] = {
        {"sha1", "", 0u, "da39a3ee5e6b4b0d3255bfef95601890afd80709"},
        {"sha1", "abc", 3u, "a9993e364706816aba3e25717850c26c9cd0d89d"},
        {"sha224", "abc", 3u, "23097d223405d8228642a477bda255b32aadbce4bda0b3f7e36c9da7"},
        {"sha256", "", 0u,
         "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"sha256", "abc", 3u,
         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"sha384", "abc", 3u,
         "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed"
         "8086072ba1e7cc2358baeca134c825a7"},
        {"sha512", "", 0u,
         "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
         "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"},
        {"sha512", "abc", 3u,
         "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
         "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"},
    };
    for (const auto &c : cases) {
        eve::data::HashFunction *hf = eve::data::HashFunction::getHashFunction(c.fn);
        REQUIRE(hf != nullptr);
        CHECK(hf->isSupported(c.fn));
        eve::data::HashFunction::Value v{};
        hf->hash(c.fn, c.in, c.len, v);
        CHECK_EQ(v.size, std::strlen(c.hex) / 2);
        CHECK_EQ(hashToHex(v), std::string(c.hex));
    }
}

TEST_CASE("data.hash.wrongFunctionThrows") {
    const char *msg = "abc";
    eve::data::HashFunction *md5 = eve::data::HashFunction::getHashFunction("md5");
    REQUIRE(md5 != nullptr);
    eve::data::HashFunction::Value v{};
    bool threw = false;
    try {
        md5->hash("sha256", msg, 3, v);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);

    eve::data::HashFunction *sha256 = eve::data::HashFunction::getHashFunction("sha256");
    REQUIRE(sha256 != nullptr);
    CHECK(sha256->isSupported("sha224"));
    CHECK(!sha256->isSupported("md5"));
    threw = false;
    try {
        sha256->hash("sha512", msg, 3, v);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);

    eve::data::HashFunction *sha512 = eve::data::HashFunction::getHashFunction("sha512");
    REQUIRE(sha512 != nullptr);
    CHECK(sha512->isSupported("sha384"));
    CHECK(!sha512->isSupported("sha1"));
    threw = false;
    try {
        sha512->hash("md5", msg, 3, v);
    } catch (const eve::Exception &) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("data.Json.decodeWithoutErrorPtr") {
    auto* dm = eve::data::DataModule::create();
    std::unique_ptr<eve::data::JsonDocument> ok(dm->decodeJson("{\"n\":1}"));
    REQUIRE(ok.get() != nullptr);
    CHECK(ok->isObject());
    std::unique_ptr<eve::data::JsonDocument> bad(dm->decodeJson("{bad"));
    CHECK(bad.get() == nullptr);
}

TEST_CASE("data.Xml.decodeWithoutErrorPtrAndEncodeData") {
    auto* dm = eve::data::DataModule::create();
    std::unique_ptr<eve::data::XmlDocument> ok(dm->decodeXml("<r/>"));
    REQUIRE(ok.get() != nullptr);
    CHECK(!ok->empty());
    std::unique_ptr<eve::data::ByteData> encoded(dm->encodeXmlData(ok.get(), false));
    REQUIRE(encoded.get() != nullptr);
    CHECK_GT(encoded->getSize(), 0u);

    std::unique_ptr<eve::data::XmlDocument> bad(dm->decodeXml("<r>"));
    CHECK(bad.get() == nullptr);

    CHECK(dm->encodeXmlData(nullptr, false) == nullptr);
}

TEST_CASE("data.JsonDocument.queryBoundaries") {
    auto* dm = eve::data::DataModule::create();
    std::unique_ptr<eve::data::JsonDocument> arr(dm->decodeJson("[1]", nullptr));
    REQUIRE(arr.get() != nullptr);
    CHECK(arr->isArray());
    CHECK(!arr->isObject());
    CHECK(arr->array());
    CHECK(!arr->object());
}
