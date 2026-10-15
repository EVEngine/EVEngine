#include <cmath>
#include <limits>
#include <type_traits>
#include "common/Json.h"
#include "common/Value.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// value.cpp
namespace {
namespace value {
using eve::Value;

TEST_CASE("value.json_round_trip_preserves_all_supported_kinds") {
    Value::Array array;
    array.emplace_back();
    array.emplace_back(true);
    array.emplace_back(std::int64_t{-7});
    array.emplace_back(1.25);
    array.emplace_back("line\n中🚀");

    Value value(Value::Object{{"z", "last"}, {"a", Value(std::move(array))}});
    auto  encoded = value.toJson();
    REQUIRE(encoded.ok());
    CHECK_EQ(encoded.value(), R"({"a":[null,true,-7,1.25,"line\n中🚀"],"z":"last"})");

    auto parsed = Value::fromJson(encoded.value());
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == value);
}

TEST_CASE("value.object_order_and_numeric_kinds_are_deterministic") {
    auto parsed = Value::fromJson(R"({"z":9223372036854775807,"a":1.0,"negativeZero":-0.0})");
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
    std::string         error;
    eve::json::Document document =
        eve::json::Document::parse(R"({"large":9223372036854775807,"fraction":1.0})", &error);
    REQUIRE(document.valid());
    CHECK(document.root().get("large").isIntegerLiteral());
    CHECK(document.root().get("large").isInt64());
    CHECK_EQ(document.root().get("large").asInt64(), std::int64_t{9223372036854775807});
    CHECK(!eve::json::Document::parse(R"({"x":1,"x":2})").valid());
    CHECK(!eve::json::Document::parse("Infinity").valid());

    Value value      = Value::fromJson(R"({"z":2,"a":1})").expect("value parse");
    auto  commonJson = eve::json::stringify(value);
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
    CHECK_EQ(static_cast<int>(nan.error()->code()), static_cast<int>(eve::DiagnosticCode::SerializationError));

    auto infinity = Value(std::numeric_limits<double>::infinity()).toJson();
    CHECK(!infinity.ok());
    CHECK_EQ(static_cast<int>(infinity.error()->code()), static_cast<int>(eve::DiagnosticCode::SerializationError));
}

TEST_CASE("presentation_value_is_the_common_value_without_a_second_storage") {
    static_assert(std::is_same_v<eve::Value, eve::Value>);
    eve::Value value(eve::Value::Object{{"count", 4}});
    auto       encoded = value.toJson();
    REQUIRE(encoded.ok());
    auto parsed = eve::Value::fromJson(encoded.value());
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == value);
}
}  // namespace value
}  // namespace

// value_lifecycle.cpp
namespace {
namespace value_lifecycle {
static_assert(std::is_nothrow_default_constructible_v<eve::Value>);
static_assert(std::is_nothrow_move_constructible_v<eve::Value>);
static_assert(std::is_nothrow_move_assignable_v<eve::Value>);
static_assert(std::is_nothrow_default_constructible_v<eve::Diagnostic>);
static_assert(std::is_nothrow_move_constructible_v<eve::Diagnostic>);
static_assert(std::is_nothrow_move_assignable_v<eve::Diagnostic>);

TEST_CASE("value.lifecycle_copy_move_and_assignment_preserve_ownership") {
    eve::Value original(eve::Value::Object{{"items", eve::Value::Array{1, "two"}}});
    eve::Value copy(original);
    copy.find("items")->pushBack(3);
    CHECK_EQ(original.find("items")->arraySize(), std::size_t{2});
    CHECK_EQ(copy.find("items")->arraySize(), std::size_t{3});
    eve::Value moved(std::move(copy));
    CHECK_EQ(moved.find("items")->arraySize(), std::size_t{3});
    eve::Value assigned("previous string");
    assigned = original;
    CHECK(assigned == original);
    assigned = std::move(moved);
    CHECK_EQ(assigned.find("items")->arraySize(), std::size_t{3});
    auto encoded = assigned.toJson();
    REQUIRE(encoded.ok());
    CHECK_EQ(encoded.value(), std::string(R"({"items":[1,"two",3]})"));
}

TEST_CASE("value.diagnostic_copy_move_preserves_context_and_independent_details") {
    auto original =
        eve::Diagnostic::warning(eve::DiagnosticCode::Conflict, "message", "path", {{"key", "value"}}, "producer");
    eve::Diagnostic copy(original);
    copy.addDetail("second", "detail");
    CHECK_EQ(original.details().size(), std::size_t{1});
    CHECK_EQ(copy.details().size(), std::size_t{2});
    eve::Diagnostic moved(std::move(copy));
    eve::Diagnostic assigned;
    assigned = original;
    CHECK_EQ(assigned.details().size(), std::size_t{1});
    assigned = std::move(moved);
    CHECK_EQ(assigned.details().size(), std::size_t{2});
    CHECK(assigned.code() == eve::DiagnosticCode::Conflict);
    CHECK(assigned.severity() == eve::Severity::Warning);
    CHECK_EQ(assigned.message(), std::string("message"));
    CHECK_EQ(assigned.path(), std::string("path"));
    CHECK_EQ(assigned.source(), std::string("producer"));
}

TEST_CASE("value.initializer_factories_preserve_container_semantics") {
    const auto emptyArray  = eve::Value::array({});
    const auto emptyObject = eve::Value::object({});
    CHECK(emptyArray.isArray());
    CHECK_EQ(emptyArray.arraySize(), std::size_t{0});
    CHECK(emptyObject.isObject());
    CHECK(emptyObject.keys().empty());
    auto source = eve::Value::array({1, "two"});
    auto object = eve::Value::object({{"z", source}, {"a", 3}, {"a", 4}});
    source.pushBack(5);
    CHECK_EQ(object.find("z")->arraySize(), std::size_t{2});
    CHECK_EQ(object.find("a")->asInt(), std::int64_t{3});
    auto encoded = object.toJson();
    REQUIRE(encoded.ok());
    CHECK_EQ(encoded.value(), std::string(R"({"a":3,"z":[1,"two"]})"));
    eve::Value::Object members{{"moved", 7}};
    const auto         moved = eve::Value::object(std::move(members));
    CHECK_EQ(moved.find("moved")->asInt(), std::int64_t{7});
}

TEST_CASE("value.getIfInstances.directConstAndMutableBorrows") {
    using V = eve::Value;
    static_assert(noexcept(std::declval<V&>().getIf<std::string>()));
    static_assert(noexcept(std::declval<const V&>().getIf<std::string>()));
    V text(std::string(4096, 'a'));
    CHECK(static_cast<bool>(text.getIf<std::string>() == static_cast<const V&>(text).getIf<std::string>()));
    text.getIf<std::string>()->append("changed");
    CHECK(static_cast<const V&>(text).getIf<std::string>()->size() == 4103u);
    CHECK(text.getIf<bool>() == nullptr);
    V null;
    CHECK(null.getIf<std::monostate>() != nullptr);
    CHECK(static_cast<const V&>(null).getIf<std::monostate>() != nullptr);
    V boolean(true);
    *boolean.getIf<bool>() = false;
    CHECK(!*static_cast<const V&>(boolean).getIf<bool>());
    V integer(std::int64_t(42));
    *integer.getIf<std::int64_t>() = 43;
    CHECK(*static_cast<const V&>(integer).getIf<std::int64_t>() == 43);
    V number(2.5);
    *number.getIf<double>() = 3.5;
    CHECK(*static_cast<const V&>(number).getIf<double>() == 3.5);
    V array(V::Array{1, "two"});
    array.getIf<V::Array>()->push_back(true);
    CHECK(static_cast<const V&>(array).getIf<V::Array>()->size() == 3u);
    V object(V::Object{{"key", "value"}});
    (*object.getIf<V::Object>())["other"] = 4;
    CHECK(static_cast<const V&>(object).getIf<V::Object>()->size() == 2u);
    auto json = object.toJson();
    REQUIRE(json.ok());
    auto parsed = V::fromJson(json.value());
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == object);
}
}  // namespace value_lifecycle
}  // namespace
