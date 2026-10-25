#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include "common/ValueRead.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace scalar_read_compatibility {
inline void booleansKeepFalsePayload(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    for (bool value : {false, true}) {
        auto parsed = eve::readBoolValue(eve::Value::boolean(value));
        REQUIRE(parsed.ok());
        CHECK(parsed.value() == value);
    }
    for (const auto& value : {eve::Value(), eve::Value::integer(0), eve::Value::number(1.0), eve::Value::string("true"),
                              eve::Value::array({}), eve::Value::object({})}) {
        auto parsed = eve::readBoolValue(value);
        CHECK(!parsed.ok());
    }
}
inline void signedIntegersKeepFullRange(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    for (std::int64_t value : {INT64_MIN, std::int64_t(-1), std::int64_t(0), INT64_MAX}) {
        auto parsed = eve::readInt64Value(eve::Value::integer(value));
        REQUIRE(parsed.ok());
        CHECK(parsed.value() == value);
    }
    for (const auto& value : {eve::Value(), eve::Value::boolean(false), eve::Value::number(1.0),
                              eve::Value::string("1"), eve::Value::array({}), eve::Value::object({})}) {
        auto parsed = eve::readInt64Value(value);
        CHECK(!parsed.ok());
    }
}
inline void unsignedIntegersKeepBounds(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    for (std::int64_t value : {std::int64_t(0), std::int64_t(UINT32_MAX)}) {
        auto parsed = eve::readUInt32Value(eve::Value::integer(value));
        REQUIRE(parsed.ok());
        CHECK(parsed.value() == static_cast<std::uint32_t>(value));
    }
    for (std::int64_t value : {INT64_MIN, std::int64_t(-1), std::int64_t(UINT32_MAX) + 1, INT64_MAX}) {
        auto parsed = eve::readUInt32Value(eve::Value::integer(value));
        CHECK(!parsed.ok());
    }
    for (const auto& value : {eve::Value(), eve::Value::boolean(false), eve::Value::number(1.0),
                              eve::Value::string("1"), eve::Value::array({}), eve::Value::object({})}) {
        auto parsed = eve::readUInt32Value(value);
        CHECK(!parsed.ok());
    }
}
inline void finiteFloatsKeepRoundingAndRange(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    const double maximum = static_cast<double>(std::numeric_limits<float>::max());
    for (double value : {0.0, -0.0, maximum, -maximum, std::numeric_limits<double>::denorm_min(),
                         -std::numeric_limits<double>::denorm_min()}) {
        auto parsed = eve::readFiniteFloatValue(eve::Value::number(value));
        REQUIRE(parsed.ok());
        CHECK(parsed.value() == static_cast<float>(value));
        CHECK(std::signbit(parsed.value()) == std::signbit(value));
    }
    for (double value : {std::nextafter(maximum, INFINITY), std::nextafter(-maximum, -INFINITY),
                         std::numeric_limits<double>::max(), std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
        auto parsed = eve::readFiniteFloatValue(eve::Value::number(value));
        CHECK(!parsed.ok());
    }
    const std::int64_t midpoint = (std::int64_t(1) << 60) + (std::int64_t(1) << 36) + 1;
    for (std::int64_t value : {INT64_MIN, INT64_MAX, midpoint}) {
        auto parsed = eve::readFiniteFloatValue(eve::Value::integer(value));
        REQUIRE(parsed.ok());
        CHECK(parsed.value() == static_cast<float>(static_cast<double>(value)));
    }
    auto rounded = eve::readFiniteFloatValue(eve::Value::integer(midpoint));
    REQUIRE(rounded.ok());
    CHECK(rounded.value() == std::ldexp(1.0f, 60));
    for (const auto& value : {eve::Value(), eve::Value::boolean(false), eve::Value::string("1"), eve::Value::array({}),
                              eve::Value::object({})}) {
        auto parsed = eve::readFiniteFloatValue(value);
        CHECK(!parsed.ok());
    }
}
}  // namespace scalar_read_compatibility
