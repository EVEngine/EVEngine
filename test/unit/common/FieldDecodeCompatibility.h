#pragma once
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include "common/ValueRead.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace field_decode_compatibility {
inline void jsonFieldsKeepWireKindsAndBytes(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    auto decoded = eve::Value::fromJson(
        R"({"empty":"","flag":false,"i":-9223372036854775808,"max":4294967295,"real":1.5,"integerLike":1.0,"bytes":"a\u0000b","id":"00042"})");
    REQUIRE(decoded.ok());
    const auto* fields = decoded.value().getIf<eve::Value::Object>();
    REQUIRE(fields != nullptr);
    auto flag = eve::readBoolValue(fields->at("flag"));
    REQUIRE(flag.ok());
    CHECK(!flag.value());
    auto integer = eve::readInt64Value(fields->at("i"));
    REQUIRE(integer.ok());
    CHECK(integer.value() == INT64_MIN);
    auto maximum = eve::readUInt32Value(fields->at("max"));
    REQUIRE(maximum.ok());
    CHECK(maximum.value() == UINT32_MAX);
    auto coerced = eve::readInt64Value(fields->at("integerLike"));
    CHECK(!coerced.ok());
    auto numericBoolean = eve::readBoolValue(fields->at("integerLike"));
    CHECK(!numericBoolean.ok());
    auto empty = eve::readStringMember(*fields, "empty");
    REQUIRE(empty.ok());
    CHECK(empty.value().empty());
    auto required = eve::readNonEmptyStringMember(*fields, "empty");
    CHECK(!required.ok());
    auto absent = eve::readStringMember(*fields, "missing");
    CHECK(!absent.ok());
    auto bytes = eve::readStringMember(*fields, "bytes");
    REQUIRE(bytes.ok());
    CHECK(bytes.value() == std::string("a\0b", 3));
    auto id = eve::scanUnsignedStringMember(*fields, "id");
    REQUIRE(id.ok());
    CHECK(id.value().value == 42);
    CHECK(id.value().consumed == 5);
    auto real = eve::readFiniteFloatValue(fields->at("real"));
    REQUIRE(real.ok());
    CHECK(real.value() == 1.5f);
}
inline void jsonNumbersKeepFloatBoundariesAndRounding(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    auto decoded =
        eve::Value::fromJson(R"([-0.0,3.4028234663852886e38,3.402823466385289e38,1e-320,1152921573326323713])");
    REQUIRE(decoded.ok());
    const auto* values = decoded.value().getIf<eve::Value::Array>();
    REQUIRE(values != nullptr);
    REQUIRE(values->size() == 5);
    auto negativeZero = eve::readFiniteFloatValue((*values)[0]);
    REQUIRE(negativeZero.ok());
    CHECK(negativeZero.value() == 0.0f);
    CHECK(std::signbit(negativeZero.value()));
    auto maximum = eve::readFiniteFloatValue((*values)[1]);
    REQUIRE(maximum.ok());
    CHECK(maximum.value() == std::numeric_limits<float>::max());
    auto overflow = eve::readFiniteFloatValue((*values)[2]);
    CHECK(!overflow.ok());
    auto underflow = eve::readFiniteFloatValue((*values)[3]);
    REQUIRE(underflow.ok());
    CHECK(underflow.value() == 0.0f);
    auto midpoint = eve::readFiniteFloatValue((*values)[4]);
    REQUIRE(midpoint.ok());
    CHECK(midpoint.value() == std::ldexp(1.0f, 60));
}
}  // namespace field_decode_compatibility
