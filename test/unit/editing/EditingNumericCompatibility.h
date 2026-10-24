#pragma once

#include "editing/EditingValueRead.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace editing_numeric_compatibility {

inline void preservesNumbersWithoutAddingValidation(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    auto integer = eve::editing::numericValue(eve::editing::Value(std::int64_t(9007199254740993LL)));
    REQUIRE(integer.ok());
    CHECK(integer.value() == static_cast<double>(std::int64_t(9007199254740993LL)));
    auto negative = eve::editing::numericValue(eve::editing::Value(-2.5));
    REQUIRE(negative.ok());
    CHECK(negative.value() == -2.5);
    auto negativeZero = eve::editing::numericValue(eve::editing::Value(-0.0));
    REQUIRE(negativeZero.ok());
    CHECK(std::signbit(negativeZero.value()));
    auto       infinity   = eve::editing::numericValue(eve::editing::Value(std::numeric_limits<double>::infinity()));
    auto       nan        = eve::editing::numericValue(eve::editing::Value(std::numeric_limits<double>::quiet_NaN()));
    const bool infinityOk = infinity.ok();
    const bool nanOk      = nan.ok();
    REQUIRE(infinityOk);
    REQUIRE(nanOk);
    CHECK(std::isinf(infinity.value()));
    CHECK(std::isnan(nan.value()));
}

inline void rejectsNonNumericKinds(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    const eve::editing::Value values[] = {eve::editing::Value(), eve::editing::Value(true), eve::editing::Value("42"),
                                          eve::editing::Value::Array{}, eve::editing::Value::Object{}};
    for (const auto& value : values) {
        auto parsed = eve::editing::numericValue(value);
        CHECK(!parsed.ok());
    }
}

}  // namespace editing_numeric_compatibility
