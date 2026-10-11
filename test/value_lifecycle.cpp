#include "common/Value.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <type_traits>

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
