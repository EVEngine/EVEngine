#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Value.h"
#include "common/Json.h"
#include "common/Value.h"

#include <cmath>
#include <limits>
#include <type_traits>

using eve::Value;

TEST_CASE("value.json_round_trip_preserves_all_supported_kinds") {
    Value::Array array;
    array.emplace_back();
    array.emplace_back(true);
    array.emplace_back(std::int64_t{-7});
    array.emplace_back(1.25);
    array.emplace_back("line\n中🚀");

    Value value(Value::Object{{"z", "last"}, {"a", Value(std::move(array))}});
    auto encoded = value.toJson();
    REQUIRE(encoded.ok());
    CHECK_EQ(encoded.value(), R"({"a":[null,true,-7,1.25,"line\n中🚀"],"z":"last"})");

    auto parsed = Value::fromJson(encoded.value());
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == value);
}

TEST_CASE("value.object_order_and_numeric_kinds_are_deterministic") {
    auto parsed = Value::fromJson(
        R"({"z":9223372036854775807,"a":1.0,"negativeZero":-0.0})");
    REQUIRE(parsed.ok());
    const Value::Object* object = parsed.value().getIf<Value::Object>();
    REQUIRE(object != nullptr);
    CHECK(object->find("z")->second.getIf<std::int64_t>() != nullptr);
    CHECK(object->find("a")->second.getIf<double>() != nullptr);
    const double* negativeZero = object->find("negativeZero")->second.getIf<double>();
    REQUIRE(negativeZero != nullptr);
    CHECK(std::signbit(*negativeZero));

    auto encoded = parsed.value().toJson();
    REQUIRE(encoded.ok());
    CHECK_EQ(encoded.value(), R"({"a":1.0,"negativeZero":-0.0,"z":9223372036854775807})");
}

TEST_CASE("value_uses_the_common_json_parser_and_serializer") {
    std::string error;
    eve::json::Document document = eve::json::Document::parse(
        R"({"large":9223372036854775807,"fraction":1.0})", &error);
    REQUIRE(document.valid());
    CHECK(document.root().get("large").isIntegerLiteral());
    CHECK(document.root().get("large").isInt64());
    CHECK_EQ(document.root().get("large").asInt64(), std::int64_t{9223372036854775807});
    CHECK(!eve::json::Document::parse(R"({"x":1,"x":2})").valid());
    CHECK(!eve::json::Document::parse("Infinity").valid());

    Value value = Value::fromJson(R"({"z":2,"a":1})").expect("value parse");
    auto commonJson = eve::json::stringify(value);
    REQUIRE(commonJson.ok());
    CHECK_EQ(commonJson.value(), R"({"a":1,"z":2})");
    auto valueJson = value.toJson();
    REQUIRE(valueJson.ok());
    CHECK_EQ(valueJson.value(), commonJson.value());
}

TEST_CASE("value.rejects_duplicate_keys_invalid_json_and_non_finite_numbers") {
    CHECK(!Value::fromJson(R"({"x":1,"x":2})").ok());
    CHECK(!Value::fromJson(R"([1,])").ok());
    CHECK(!Value::fromJson("1e9999").ok());
    CHECK(!Value::fromJson("NaN").ok());

    auto nan = Value(std::numeric_limits<double>::quiet_NaN()).toJson();
    CHECK(!nan.ok());
    CHECK_EQ(static_cast<int>(nan.code()), static_cast<int>(eve::StatusCode::Failed));
    REQUIRE(nan.error() != nullptr);
    CHECK_EQ(static_cast<int>(nan.error()->code()),
             static_cast<int>(eve::DiagnosticCode::SerializationError));

    auto infinity = Value(std::numeric_limits<double>::infinity()).toJson();
    CHECK(!infinity.ok());
    CHECK_EQ(static_cast<int>(infinity.error()->code()),
             static_cast<int>(eve::DiagnosticCode::SerializationError));
}

TEST_CASE("presentation_value_is_the_common_value_without_a_second_storage") {
    static_assert(std::is_same_v<eve::Value, eve::Value>);
    eve::Value value(eve::Value::Object{{"count", 4}});
    auto encoded = value.toJson();
    REQUIRE(encoded.ok());
    auto parsed = eve::Value::fromJson(encoded.value());
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == value);
}
