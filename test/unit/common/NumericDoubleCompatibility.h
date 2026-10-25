#pragma once
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include "common/ValueRead.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace numeric_double_compatibility {
inline void preservesNonfiniteValuesAndIntegerRounding(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    const double nan = std::bit_cast<double>(std::uint64_t(0x7ff8000000000042));
    for (double value : {0.0, -0.0, 1.25, std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::max(),
                         std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), nan}) {
        auto parsed = eve::readNumericDoubleValue(eve::Value::number(value));
        REQUIRE(parsed.ok());
        CHECK(std::bit_cast<std::uint64_t>(parsed.value()) == std::bit_cast<std::uint64_t>(value));
    }
    for (std::int64_t value : {INT64_MIN, INT64_MAX, std::int64_t(0), (std::int64_t(1) << 53) + 1}) {
        auto parsed = eve::readNumericDoubleValue(eve::Value::integer(value));
        REQUIRE(parsed.ok());
        CHECK(parsed.value() == static_cast<double>(value));
    }
}
inline void rejectsCoercionWithStructuredStatus(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    for (const auto& value : {eve::Value(), eve::Value::boolean(false), eve::Value::string("1.5"),
                              eve::Value::array({}), eve::Value::object({})}) {
        auto parsed = eve::readNumericDoubleValue(value);
        REQUIRE(!parsed.ok());
        const auto& diagnostics = parsed.status().diagnostics();
        REQUIRE(diagnostics.size() == 1);
        CHECK(diagnostics[0].code() == eve::DiagnosticCode::TypeMismatch);
    }
}
}  // namespace numeric_double_compatibility
