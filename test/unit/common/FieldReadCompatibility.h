#pragma once

#include "common/ValueRead.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <limits>
#include <string>

namespace field_read_compatibility {

inline void requiredStringsKeepEmptyAndOwningSemantics(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    const std::string  bytes("a\0b", 3);
    eve::Value::Object fields{
        {"empty", eve::Value::string("")}, {"bytes", eve::Value::string(bytes)}, {"wrong", eve::Value::integer(7)}};
    auto empty = eve::readStringMember(fields, "empty");
    REQUIRE(empty.ok());
    CHECK(empty.value().empty());
    for (const auto* name : {"missing", "wrong"}) {
        auto invalid = eve::readStringMember(fields, name);
        CHECK(!invalid.ok());
    }
    auto nonempty = eve::readNonEmptyStringMember(fields, "empty");
    CHECK(!nonempty.ok());
    auto copied = eve::readStringMember(fields, "bytes");
    REQUIRE(copied.ok());
    fields.clear();
    CHECK(copied.value() == bytes);
}

inline void nativeIntsKeepStrictKindsAndBounds(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    for (const auto number : {std::numeric_limits<int>::min(), -1, 0, std::numeric_limits<int>::max()}) {
        auto parsed = eve::readIntValue(eve::Value::integer(number));
        REQUIRE(parsed.ok());
        CHECK(parsed.value() == number);
    }
    for (const auto number :
         {std::int64_t(std::numeric_limits<int>::min()) - 1, std::int64_t(std::numeric_limits<int>::max()) + 1,
          std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max()}) {
        auto parsed = eve::readIntValue(eve::Value::integer(number));
        CHECK(!parsed.ok());
    }
    const eve::Value invalid[] = {
        eve::Value(),          eve::Value::number(42.0), eve::Value::boolean(true), eve::Value::string("42"),
        eve::Value::array({}), eve::Value::object({})};
    for (const auto& value : invalid) {
        auto parsed = eve::readIntValue(value);
        CHECK(!parsed.ok());
    }
}

inline void unsignedValueKeepsDecimalPrefixContract(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    auto zero = eve::scanUnsignedStringValue(eve::Value::string("000"));
    REQUIRE(zero.ok());
    CHECK(zero.value().value == 0);
    CHECK(zero.value().consumed == zero.value().size);
    auto maximum = eve::scanUnsignedStringValue(eve::Value::string("18446744073709551615"));
    REQUIRE(maximum.ok());
    CHECK(maximum.value().value == UINT64_MAX);
    const std::string suffixes[] = {"42tail", "42 ", std::string("42\0tail", 7)};
    for (const auto& text : suffixes) {
        auto parsed = eve::scanUnsignedStringValue(eve::Value::string(text));
        REQUIRE(parsed.ok());
        CHECK(parsed.value().value == 42);
        CHECK(parsed.value().consumed == 2);
        CHECK(parsed.value().size == text.size());
    }
    for (const auto* text : {"", " 42", "+42", "-1", "18446744073709551616"}) {
        auto parsed = eve::scanUnsignedStringValue(eve::Value::string(text));
        CHECK(!parsed.ok());
    }
    const eve::Value invalid[] = {
        eve::Value(),          eve::Value::integer(42), eve::Value::number(42.0), eve::Value::boolean(true),
        eve::Value::array({}), eve::Value::object({})};
    for (const auto& value : invalid) {
        auto parsed = eve::scanUnsignedStringValue(value);
        CHECK(!parsed.ok());
    }
}

}  // namespace field_read_compatibility
