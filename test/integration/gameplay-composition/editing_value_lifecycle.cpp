#include <zeroerr/unittest.h>
#include <type_traits>
#include "editing/EditingValue.h"
#include "editing/EditingValueJson.h"

using EditingValue = eve::editing::Value;
static_assert(std::is_nothrow_move_constructible_v<EditingValue> ==
              std::is_nothrow_move_constructible_v<EditingValue::Storage>);
static_assert(std::is_nothrow_move_assignable_v<EditingValue> ==
              std::is_nothrow_move_assignable_v<EditingValue::Storage>);
static_assert(std::is_nothrow_default_constructible_v<EditingValue>);

TEST_CASE("editing.valueLifecycle.deepCopyMoveAndDeterministicJson") {
    EditingValue original(EditingValue::Object{{"z", EditingValue::Array{1, "text"}}, {"a", true}});
    EditingValue copy(original);
    (*original.getIf<EditingValue::Object>())["z"] = EditingValue(99);
    REQUIRE(copy.getIf<EditingValue::Object>() != nullptr);
    REQUIRE(copy.getIf<EditingValue::Object>()->at("z").getIf<EditingValue::Array>() != nullptr);
    CHECK(eve::editing::editorValueToJson(copy) == "{\"a\":true,\"z\":[1,\"text\"]}");
    EditingValue assigned;
    assigned = copy;
    CHECK(assigned == copy);
    EditingValue moved(std::move(assigned));
    CHECK(moved == copy);
    assigned = EditingValue("reused");
    CHECK(assigned.type() == EditingValue::Type::String);
    EditingValue destination;
    destination = std::move(moved);
    CHECK(destination == copy);
    auto parsed = eve::editing::editorValueFromJson(eve::editing::editorValueToJson(destination));
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == destination);
    CHECK(eve::editing::editorValueContentHash(parsed.value()) == eve::editing::editorValueContentHash(copy));
    CHECK(copy.isWithinLimits(3, 10, 100));
    CHECK(!copy.isWithinLimits(1, 10, 100));
}

TEST_CASE("editing.valueLifecycle.scalarKindsAndInvalidJsonRemainStable") {
    CHECK(EditingValue().type() == EditingValue::Type::Null);
    CHECK(EditingValue(nullptr).type() == EditingValue::Type::Null);
    CHECK(EditingValue(true).type() == EditingValue::Type::Bool);
    CHECK(EditingValue(1).type() == EditingValue::Type::Integer);
    CHECK(EditingValue(int64_t(2)).type() == EditingValue::Type::Integer);
    CHECK(EditingValue(1.5f).type() == EditingValue::Type::Number);
    CHECK(EditingValue(2.5).type() == EditingValue::Type::Number);
    CHECK(*EditingValue(static_cast<const char*>(nullptr)).getIf<std::string>() == "");
    CHECK(*EditingValue(std::string("owned")).getIf<std::string>() == "owned");
    auto rejected = eve::editing::editorValueFromJson("{broken");
    CHECK(!rejected.ok());
    CHECK(rejected.error() != nullptr);
}

TEST_CASE("editing.valueLifecycle.getIfKeepsDirectConstAndMutableBorrows") {
    using V = EditingValue;
    V text(std::string(4096, 'a'));
    CHECK(static_cast<bool>(static_cast<const V&>(text).getIf<std::string>() == text.getIf<std::string>()));
    text.getIf<std::string>()->append("changed");
    CHECK(static_cast<const V&>(text).getIf<std::string>()->size() == 4103u);
    CHECK(text.getIf<bool>() == nullptr);
    CHECK(static_cast<const V&>(text).getIf<V::Array>() == nullptr);
    V null;
    CHECK(null.getIf<std::monostate>() != nullptr);
    CHECK(static_cast<const V&>(null).getIf<std::monostate>() != nullptr);
    V boolean(true);
    *boolean.getIf<bool>() = false;
    CHECK(!*static_cast<const V&>(boolean).getIf<bool>());
    V integer(int64_t(42));
    *integer.getIf<int64_t>() = 43;
    CHECK(*static_cast<const V&>(integer).getIf<int64_t>() == 43);
    V number(2.5);
    *number.getIf<double>() = 3.5;
    CHECK(*static_cast<const V&>(number).getIf<double>() == 3.5);
    V array(V::Array{1, "two"});
    array.getIf<V::Array>()->push_back(true);
    CHECK(static_cast<const V&>(array).getIf<V::Array>()->size() == 3u);
    V object(V::Object{{"key", "value"}});
    (*object.getIf<V::Object>())["other"] = 4;
    CHECK(static_cast<const V&>(object).getIf<V::Object>()->size() == 2u);
}
