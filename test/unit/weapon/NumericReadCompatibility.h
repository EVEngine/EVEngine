#pragma once
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include "weapon/detail/NumericRead.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace weapon_numeric_read_compatibility {
template <class T>
inline void checkFailure(eve::Result<std::optional<T>>& result, const std::string& message, const std::string& path,
                         const std::string& source, zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    REQUIRE(!result.ok());
    const auto& diagnostics = result.status().diagnostics();
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics[0].code() == eve::DiagnosticCode::InvalidArgument);
    CHECK(diagnostics[0].severity() == eve::Severity::Error);
    CHECK(diagnostics[0].message() == message);
    CHECK(diagnostics[0].path() == path);
    CHECK(diagnostics[0].source() == source);
    CHECK(diagnostics[0].details().empty());
}
inline void optionalDoubleKeepsAbsenceValuesAndDiagnostics(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    const std::string path("payload.speed\0tail", 18);
    for (const std::string source : {"weapon.carrier.recipe", "weapon.spell.fragments"}) {
        auto missing = eve::weapon::detail::readOptionalDouble({}, "speed", path, source);
        REQUIRE(missing.ok());
        CHECK(!missing.value().has_value());
        for (const auto& value : {eve::Value::number(-0.0), eve::Value::number(1.5), eve::Value::integer(INT64_MAX),
                                  eve::Value::integer(INT64_MIN)}) {
            auto parsed = eve::weapon::detail::readOptionalDouble({{"speed", value}}, "speed", path, source);
            REQUIRE(parsed.ok());
            REQUIRE(parsed.value().has_value());
            const double expected = value.isInt64() ? static_cast<double>(value.asInt()) : value.asDouble();
            CHECK(std::bit_cast<std::uint64_t>(*parsed.value()) == std::bit_cast<std::uint64_t>(expected));
        }
        for (double value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity()}) {
            auto parsed =
                eve::weapon::detail::readOptionalDouble({{"speed", eve::Value::number(value)}}, "speed", path, source);
            checkFailure(parsed, "Expected finite number", path, source, _ZEROERR_TEST_CONTEXT);
        }
        for (const auto& value : {eve::Value(), eve::Value::boolean(false), eve::Value::string("1"),
                                  eve::Value::array({}), eve::Value::object({})}) {
            auto parsed = eve::weapon::detail::readOptionalDouble({{"speed", value}}, "speed", path, source);
            checkFailure(parsed, "Expected number", path, source, _ZEROERR_TEST_CONTEXT);
        }
    }
}
inline void optionalIntKeepsIntegralDoubleAndErrorPrecedence(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    for (const std::string source : {"weapon.carrier.recipe", "weapon.spell.fragments"}) {
        auto missing = eve::weapon::detail::readOptionalInt({}, "count", "payload.count", source);
        REQUIRE(missing.ok());
        CHECK(!missing.value().has_value());
        for (const auto& value :
             {eve::Value::number(-0.0), eve::Value::number(42.0), eve::Value::number(static_cast<double>(INT32_MIN)),
              eve::Value::number(static_cast<double>(INT32_MAX)), eve::Value::integer(INT32_MIN),
              eve::Value::integer(INT32_MAX)}) {
            auto parsed = eve::weapon::detail::readOptionalInt({{"count", value}}, "count", "payload.count", source);
            REQUIRE(parsed.ok());
            REQUIRE(parsed.value().has_value());
            CHECK(*parsed.value() ==
                  (value.isInt64() ? static_cast<int>(value.asInt()) : static_cast<int>(value.asDouble())));
        }
        for (const auto& value : {eve::Value::integer(INT64_MIN), eve::Value::integer(INT64_MAX),
                                  eve::Value::number(static_cast<double>(INT32_MIN) - 1),
                                  eve::Value::number(static_cast<double>(INT32_MAX) + 1)}) {
            auto parsed = eve::weapon::detail::readOptionalInt({{"count", value}}, "count", "payload.count", source);
            checkFailure(parsed, "Integer out of range", "payload.count", source, _ZEROERR_TEST_CONTEXT);
        }
        for (const auto& value :
             {eve::Value(), eve::Value::boolean(true), eve::Value::string("42"), eve::Value::array({}),
              eve::Value::object({}), eve::Value::number(42.5),
              eve::Value::number(static_cast<double>(INT32_MAX) + 0.5), eve::Value::number(INFINITY),
              eve::Value::number(-INFINITY), eve::Value::number(std::numeric_limits<double>::quiet_NaN())}) {
            auto parsed = eve::weapon::detail::readOptionalInt({{"count", value}}, "count", "payload.count", source);
            checkFailure(parsed, "Expected integer", "payload.count", source, _ZEROERR_TEST_CONTEXT);
        }
    }
}
}  // namespace weapon_numeric_read_compatibility
