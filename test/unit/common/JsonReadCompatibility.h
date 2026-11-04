#pragma once

#include "common/ValueRead.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <limits>
#include <string>

namespace json_read_compatibility {

inline void controlBytesPreserveWireSpelling(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    std::string controls;
    for (int byte = 0; byte < 32; ++byte) controls.push_back(static_cast<char>(byte));
    const std::string canonical =
        "\"\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\b\\t\\n\\u000b\\f\\r"
        "\\u000e\\u000f\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017"
        "\\u0018\\u0019\\u001a\\u001b\\u001c\\u001d\\u001e\\u001f\"";
    const std::string legacy =
        "\"\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\u0008\\t\\n\\u000b\\u000c\\r"
        "\\u000e\\u000f\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u0016\\u0017"
        "\\u0018\\u0019\\u001a\\u001b\\u001c\\u001d\\u001e\\u001f\"";
    CHECK(eve::json::quoteString(controls, eve::json::StringEscapeStyle::Canonical) == canonical);
    CHECK(eve::json::quoteString(controls, eve::json::StringEscapeStyle::UnicodeBackspaceFormFeed) == legacy);
    auto serialized = eve::Value::string(controls).toJson();
    REQUIRE(serialized.ok());
    CHECK(serialized.value() == canonical);
    auto       canonicalRoundTrip = eve::Value::fromJson(canonical);
    auto       legacyRoundTrip    = eve::Value::fromJson(legacy);
    const bool canonicalOk        = canonicalRoundTrip.ok();
    const bool legacyOk           = legacyRoundTrip.ok();
    REQUIRE(canonicalOk);
    REQUIRE(legacyOk);
    CHECK(canonicalRoundTrip.value().asString() == controls);
    CHECK(legacyRoundTrip.value().asString() == controls);
}

inline void quotePreservesUtf8AndPunctuation(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    const std::string input    = "中文/\"\\";
    const std::string expected = "\"中文/\\\"\\\\\"";
    CHECK(eve::json::quoteString(input, eve::json::StringEscapeStyle::Canonical) == expected);
    CHECK(eve::json::quoteString(input, eve::json::StringEscapeStyle::UnicodeBackspaceFormFeed) == expected);
    CHECK(eve::json::quoteString({}, eve::json::StringEscapeStyle::Canonical) == "\"\"");
}

inline void unsignedSyntaxKeepsLegacyAcceptanceAndSuffix(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    using eve::json::UnsignedIntegerSyntax;
    struct Example {
        std::string   text;
        std::uint64_t value;
        std::size_t   consumed;
    };
    const Example examples[] = {{"0", 0, 1},
                                {"18446744073709551615", UINT64_MAX, 20},
                                {"  +42tail", 42, 5},
                                {"-1", UINT64_MAX, 2},
                                {"0x10", 0, 1},
                                {"42 ", 42, 2},
                                {std::string("42\0tail", 7), 42, 2}};
    for (const auto& example : examples) {
        auto parsed = eve::json::scanUnsignedInteger(example.text, UnsignedIntegerSyntax::LegacyStoull);
        REQUIRE(parsed.ok());
        CHECK(parsed.value().value == example.value);
        CHECK(parsed.value().consumed == example.consumed);
        CHECK(parsed.value().size == example.text.size());
    }
    for (const std::string text : {"", " ", "abc", "18446744073709551616"}) {
        auto parsed = eve::json::scanUnsignedInteger(text, UnsignedIntegerSyntax::LegacyStoull);
        CHECK(!parsed.ok());
    }
    for (const std::string text : {"+42", " 42", "-1", "", "18446744073709551616"}) {
        auto parsed = eve::json::scanUnsignedInteger(text, UnsignedIntegerSyntax::Decimal);
        CHECK(!parsed.ok());
    }
    auto decimal = eve::json::scanUnsignedInteger("42suffix", UnsignedIntegerSyntax::Decimal);
    REQUIRE(decimal.ok());
    CHECK(decimal.value().value == 42);
    CHECK(decimal.value().consumed == 2);
    CHECK(decimal.value().size == 8);
}

inline void artifactFieldsKeepRequiredStringAndDecimalContract(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    eve::Value::Object fields{{"empty", eve::Value::string("")},
                              {"wrong", eve::Value::integer(42)},
                              {"max", eve::Value::string("18446744073709551615")},
                              {"suffix", eve::Value::string("42tail")},
                              {"sign", eve::Value::string("+42")},
                              {"overflow", eve::Value::string("18446744073709551616")}};
    for (const auto* name : {"missing", "empty", "wrong", "sign", "overflow"}) {
        auto parsed = eve::scanUnsignedStringMember(fields, name);
        CHECK(!parsed.ok());
    }
    auto maximum = eve::scanUnsignedStringMember(fields, "max");
    REQUIRE(maximum.ok());
    CHECK(maximum.value().value == UINT64_MAX);
    CHECK(maximum.value().consumed == maximum.value().size);
    auto suffix = eve::scanUnsignedStringMember(fields, "suffix");
    REQUIRE(suffix.ok());
    CHECK(suffix.value().value == 42);
    CHECK(suffix.value().consumed == 2);
    CHECK(suffix.value().size == 6);
    auto text = eve::readNonEmptyStringMember(fields, "max");
    REQUIRE(text.ok());
    fields.clear();
    CHECK(text.value() == "18446744073709551615");
}

}  // namespace json_read_compatibility
