#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Json.h"

#include <string>

using namespace eve::json;

TEST_CASE("json.parseScalars") {
    Document doc = Document::parse(R"({
      "s": "hello",
      "i": 42,
      "neg": -7,
      "f": 1.5,
      "exp": 2e3,
      "t": true,
      "f2": false,
      "n": null
    })");
    REQUIRE(doc.valid());
    Value r = doc.root();
    CHECK(r.isObject());
    CHECK_EQ(r.size(), 8u);

    CHECK_EQ(r.getString("s"), "hello");
    CHECK_EQ(r.getInt("i"), 42);
    CHECK_EQ(r.getInt("neg"), -7);
    CHECK_EQ(r.getFloat("f"), 1.5f);
    CHECK_EQ(r.getDouble("exp"), 2000.0);
    CHECK(r.getBool("t"));
    CHECK(!r.getBool("f2"));
    CHECK(r.get("n").isNull());

    CHECK(r.isObject());
    CHECK(r.get("s").isString());
    CHECK(r.get("i").isNumber());
    CHECK(r.get("t").isBool());
}

TEST_CASE("json.missingKeysFallBack") {
    Document doc = Document::parse(R"({"a": 1})");
    REQUIRE(doc.valid());
    Value r = doc.root();

    CHECK(!r.has("nope"));
    CHECK_EQ(r.getInt("nope", 9), 9);
    CHECK_EQ(r.getFloat("nope", 2.5f), 2.5f);
    CHECK_EQ(r.getString("nope", "dflt"), "dflt");
    CHECK(r.getBool("nope", true));
    // A missing key yields a null Value, and chaining off it stays safe.
    CHECK(!r.get("nope"));
    CHECK_EQ(r.get("nope").get("deeper").getInt("deepest", 3), 3);
}

TEST_CASE("json.wrongTypeFallsBack") {
    Document doc = Document::parse(R"({"s": "abc", "o": {"k": 1}, "a": [1,2]})");
    REQUIRE(doc.valid());
    Value r = doc.root();

    // A non-numeric string is not a number.
    CHECK_EQ(r.getInt("s", 5), 5);
    // Objects and arrays are not scalars.
    CHECK_EQ(r.getString("o", "dflt"), "dflt");
    CHECK_EQ(r.getInt("a", 7), 7);
    // Indexing a non-array, or out of range, yields null.
    CHECK(!r.get("o").at(0));
    CHECK(!r.get("a").at(2));
}

TEST_CASE("json.lenientScalarConversion") {
    Document doc = Document::parse(R"({"num": "12", "real": "1.25", "n": 3, "b": true})");
    REQUIRE(doc.valid());
    Value r = doc.root();

    // Numeric strings read as numbers, matching the Poco-based helpers this
    // facade replaces.
    CHECK_EQ(r.getInt("num", 0), 12);
    CHECK_EQ(r.getFloat("real", 0.f), 1.25f);
    // Numbers read back as strings.
    CHECK_EQ(r.getString("n"), "3");
    CHECK_EQ(r.getString("b"), "true");
}

TEST_CASE("json.integerExactness") {
    // Values a double cannot represent exactly must still round-trip through
    // getInt, and out-of-range ones fall back rather than wrap.
    Document doc = Document::parse(R"({"big": 2147483647, "over": 4294967296, "trunc": 2.9})");
    REQUIRE(doc.valid());
    Value r = doc.root();

    CHECK_EQ(r.getInt("big", 0), 2147483647);
    CHECK_EQ(r.getInt("over", -1), -1);
    CHECK_EQ(r.getInt("trunc", 0), 2);
}

TEST_CASE("json.arrays") {
    Document doc = Document::parse(R"({
      "tags": ["a", "b", "c"],
      "sizes": [1, 2, 3],
      "coords": [0.5, 1.5],
      "mixed": ["x", 2, true]
    })");
    REQUIRE(doc.valid());
    Value r = doc.root();

    auto tags = r.getStringArray("tags");
    REQUIRE(tags.size() == 3u);
    CHECK_EQ(tags[0], "a");
    CHECK_EQ(tags[2], "c");

    auto sizes = r.getIntArray("sizes");
    REQUIRE(sizes.size() == 3u);
    CHECK_EQ(sizes[1], 2);

    auto coords = r.getFloatArray("coords");
    REQUIRE(coords.size() == 2u);
    CHECK_EQ(coords[0], 0.5f);

    // Mixed arrays stringify elementwise.
    auto mixed = r.getStringArray("mixed");
    REQUIRE(mixed.size() == 3u);
    CHECK_EQ(mixed[1], "2");

    CHECK(r.getStringArray("absent").empty());
    CHECK(r.getIntArray("tags").size() == 3u);  // non-numeric strings -> 0
}

TEST_CASE("json.maps") {
    Document doc = Document::parse(R"({
      "extra": {"faction": "elf", "rank": "3"},
      "counts": {"wood": 5, "ore": 2}
    })");
    REQUIRE(doc.valid());
    Value r = doc.root();

    auto extra = r.getStringMap("extra");
    CHECK_EQ(extra.size(), 2u);
    CHECK_EQ(extra["faction"], "elf");

    auto counts = r.getIntMap("counts");
    CHECK_EQ(counts.size(), 2u);
    CHECK_EQ(counts["wood"], 5);

    CHECK(r.getStringMap("absent").empty());
}

TEST_CASE("json.nestedNavigation") {
    Document doc = Document::parse(R"({
      "effects": [
        {"id": "burn", "modifiers": [{"attribute": "hp", "value": -3}]},
        {"id": "chill"}
      ]
    })");
    REQUIRE(doc.valid());
    Value effects = doc.root().get("effects");
    REQUIRE(effects.isArray());
    CHECK_EQ(effects.size(), 2u);

    Value first = effects.at(0);
    CHECK_EQ(first.getString("id"), "burn");
    Value mods = first.get("modifiers");
    REQUIRE(mods.isArray());
    CHECK_EQ(mods.at(0).getString("attribute"), "hp");
    CHECK_EQ(mods.at(0).getDouble("value"), -3.0);

    // Absent nested arrays report empty rather than failing.
    CHECK_EQ(effects.at(1).get("modifiers").size(), 0u);
}

TEST_CASE("json.keysPreserveDocumentOrder") {
    Document doc = Document::parse(R"({"z": 1, "a": 2, "m": 3})");
    REQUIRE(doc.valid());
    auto keys = doc.root().keys();
    REQUIRE(keys.size() == 3u);
    CHECK_EQ(keys[0], "z");
    CHECK_EQ(keys[1], "a");
    CHECK_EQ(keys[2], "m");
}

TEST_CASE("json.rootArray") {
    Document doc = Document::parse(R"([{"id": "a"}, {"id": "b"}])");
    REQUIRE(doc.valid());
    Value r = doc.root();
    CHECK(r.isArray());
    CHECK(!r.isObject());
    REQUIRE(r.size() == 2u);
    CHECK_EQ(r.at(1).getString("id"), "b");
}

TEST_CASE("json.escapesAndUnicode") {
    Document doc = Document::parse(
        R"({"esc": "a\"b\\c\/d\be\ff\ng\rh\ti", "bmp": "\u5f00\u59cb", "astral": "\ud83d\ude00"})");
    REQUIRE(doc.valid());
    Value r = doc.root();

    CHECK_EQ(r.getString("esc"), "a\"b\\c/d\be\ff\ng\rh\ti");
    // BMP escapes decode to UTF-8.
    CHECK_EQ(r.getString("bmp"), "\xe5\xbc\x80\xe5\xa7\x8b");
    // Surrogate pairs combine into one code point (U+1F600).
    CHECK_EQ(r.getString("astral"), "\xf0\x9f\x98\x80");
}

TEST_CASE("json.malformedInputReportsError") {
    struct Case {
        const char* text;
    } bad[] = {
        {"{"},        {"{\"a\":}"},   {"[1,]"},      {"{\"a\" 1}"},
        {"tru"},      {"{\"a\":1} x"}, {"\"unterminated"}, {""},
    };
    for (const auto& c : bad) {
        std::string err;
        Document doc = Document::parse(c.text, &err);
        CHECK(!doc.valid());
        CHECK(!err.empty());
        // An invalid document still answers safely.
        CHECK(!doc.root());
        CHECK_EQ(doc.root().getInt("anything", 4), 4);
    }
}

TEST_CASE("json.emptyContainers") {
    Document doc = Document::parse(R"({"o": {}, "a": []})");
    REQUIRE(doc.valid());
    Value r = doc.root();
    CHECK(r.get("o").isObject());
    CHECK_EQ(r.get("o").size(), 0u);
    CHECK(r.get("a").isArray());
    CHECK_EQ(r.get("a").size(), 0u);
    CHECK(r.get("o").keys().empty());
}

TEST_CASE("json.documentIsMovable") {
    Document a = Document::parse(R"({"v": 11})");
    REQUIRE(a.valid());
    Document b = std::move(a);
    CHECK(b.valid());
    CHECK_EQ(b.root().getInt("v"), 11);
}
