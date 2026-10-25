// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "local-format",
  "origins": [
    "editing_property_schema_json.cpp",
    "common_result_header.cpp"
  ]
}
*/
// clang-format on

#include "PrivateTestScope.h"
// Check header isolation before any test assertion headers are loaded.
#include "common/Result.h"
#include "editing/EditingResult.h"
#if defined(EV_ASSERT) || defined(ZEROERR_ASSERT_EXP)
#error "Result headers must not expose the zeroerr assertion backend"
#endif

#include <zeroerr/unittest.h>
#include <type_traits>
// common_result_header.cpp
namespace test_common_result_header {


#if defined(ZEROERR_NO_ASSERT)
static_assert(std::is_nothrow_destructible_v<eve::Result<int>>);
#else
static_assert(!std::is_nothrow_destructible_v<eve::Result<int>>);
#endif

TEST_CASE("common.result.headersKeepDiagnosticsInImplementation") {
    auto value = eve::Result<int>::success(7);
    CHECK(value.value() == 7);
    auto unchanged = eve::editing::noOp();
    CHECK(unchanged.ok());
    auto failed = eve::editing::rejected<int>(eve::editing::RuleId("header.contract"), "expected");
    CHECK(!failed.ok());
    CHECK(failed.status().code() == eve::StatusCode::Rejected);
}
}  // namespace test_common_result_header


#include "PrivateTestScope.h"
#include "editing/EditingValueJson.h"
#include "editing/PropertySchemaJson.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace test_editing_property_schema_json {
using namespace eve::editing;

// Empty schema contract: a stable id produces an object schema with no fields,
// no required fields, and closed additional properties.
TEST_CASE("editing.schema.spec.empty_object_projection") {
    auto schema = emptyObjectPropertySchema("empty.payload");
    CHECK_EQ(schema.typeId, "empty.payload");
    CHECK_EQ(schema.version, 1u);
    auto projected = propertySchemaToJsonSchema(schema, "command.empty");
    REQUIRE(projected.ok());
    const auto* root = projected.value().getIf<Value::Object>();
    REQUIRE(root != nullptr);
    CHECK_EQ(*root->at("$id").getIf<std::string>(), "command.empty");
    CHECK_EQ(*root->at("type").getIf<std::string>(), "object");
    CHECK_EQ(*root->at("typeId").getIf<std::string>(), "empty.payload");
    CHECK_EQ(*root->at("additionalProperties").getIf<bool>(), false);
    CHECK(root->at("properties").getIf<Value::Object>()->empty());
    CHECK(root->at("required").getIf<Value::Array>()->empty());
}

// Field contract: primitive, enum and vector types retain their declared
// metadata. Mutable fields are required; read-only fields are not.
TEST_CASE("editing.schema.spec.field_types_constraints_and_required") {
    PropertySchema schema;
    schema.version = 3;
    PropertyDescriptor enabled;
    enabled.path           = PropertyPath("enabled");
    enabled.type           = PropertyType::Bool;
    enabled.displayNameKey = "Enabled";
    schema.properties.push_back(enabled);
    PropertyDescriptor mode;
    mode.path      = PropertyPath("mode");
    mode.type      = PropertyType::Enum;
    mode.enumItems = {"walk", "run"};
    mode.flags     = PropertyFlag::ReadOnly;
    schema.properties.push_back(mode);
    PropertyDescriptor position;
    position.path            = PropertyPath("position");
    position.type            = PropertyType::Vec3;
    position.numeric.minimum = -1.0;
    position.numeric.maximum = 1.0;
    schema.properties.push_back(position);
    auto projected = propertySchemaToJsonSchema(schema, "command.move");
    REQUIRE(projected.ok());
    const auto* root = projected.value().getIf<Value::Object>();
    REQUIRE(root != nullptr);
    CHECK_EQ(*root->at("typeId").getIf<std::string>(), "command.move");
    CHECK_EQ(*root->at("version").getIf<std::int64_t>(), 3);
    const auto* fields = root->at("properties").getIf<Value::Object>();
    REQUIRE(fields != nullptr);
    CHECK_EQ(*fields->at("enabled").getIf<Value::Object>()->at("type").getIf<std::string>(), "boolean");
    const auto* enumField = fields->at("mode").getIf<Value::Object>();
    REQUIRE(enumField != nullptr);
    CHECK_EQ(*enumField->at("type").getIf<std::string>(), "string");
    CHECK_EQ(*enumField->at("readOnly").getIf<bool>(), true);
    CHECK_EQ(enumField->at("enum").getIf<Value::Array>()->size(), std::size_t{2});
    const auto* vectorField = fields->at("position").getIf<Value::Object>();
    REQUIRE(vectorField != nullptr);
    CHECK_EQ(*vectorField->at("type").getIf<std::string>(), "array");
    CHECK_EQ(*vectorField->at("minItems").getIf<std::int64_t>(), 3);
    CHECK_EQ(*vectorField->at("maxItems").getIf<std::int64_t>(), 3);
    CHECK_EQ(*vectorField->at("minimum").getIf<double>(), -1.0);
    CHECK_EQ(*vectorField->at("maximum").getIf<double>(), 1.0);
    const auto* required = root->at("required").getIf<Value::Array>();
    REQUIRE(required != nullptr);
    CHECK_EQ(required->size(), std::size_t{2});
    CHECK_EQ(*(*required)[0].getIf<std::string>(), "enabled");
    CHECK_EQ(*(*required)[1].getIf<std::string>(), "position");
}

// Document-schema contract: an authored object passes through as an owned
// value; flat properties cannot coexist with it.
TEST_CASE("editing.schema.spec.document_shape_is_exclusive") {
    PropertySchema schema;
    schema.documentSchema = Value::Object{{"type", "object"}, {"title", "Nested"}};
    auto projected        = propertySchemaToJsonSchema(schema, "document.id");
    REQUIRE(projected.ok());
    CHECK_EQ(projected.value(), schema.documentSchema);
    PropertyDescriptor flat;
    flat.path = PropertyPath("extra");
    schema.properties.push_back(flat);
    CHECK(!propertySchemaToJsonSchema(schema, "document.id").ok());
    schema.properties.clear();
    schema.documentSchema = Value("not-an-object");
    CHECK(!propertySchemaToJsonSchema(schema, "document.id").ok());
}

// Invalid input contract: an empty schema id or empty property path fails.
TEST_CASE("editing.schema.spec.rejects_missing_ids_and_paths") {
    auto schema = emptyObjectPropertySchema();
    CHECK(!propertySchemaToJsonSchema(schema, "").ok());
    PropertyDescriptor bad;
    schema.properties.push_back(bad);
    REQUIRE(!propertySchemaToJsonSchema(schema, "valid.id").ok());
}
}  // namespace test_editing_property_schema_json

#include "EditingNumericCompatibility.h"
namespace test_editing_numeric_compatibility {
TEST_CASE("editing.numeric.spec.valid_numbers_keep_original_values") {
    editing_numeric_compatibility::preservesNumbersWithoutAddingValidation(_ZEROERR_TEST_CONTEXT);
}
TEST_CASE("editing.numeric.spec.non_numeric_kinds_are_rejected") {
    editing_numeric_compatibility::rejectsNonNumericKinds(_ZEROERR_TEST_CONTEXT);
}
}  // namespace test_editing_numeric_compatibility

#include "zeroerr/assert.h"

#include <zeroerr/unittest.h>
#include <type_traits>
#include "editing/EditingValue.h"
#include "editing/EditingValueJson.h"
// editing_value_lifecycle.cpp
namespace test_editing_value_lifecycle {
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
}  // namespace test_editing_value_lifecycle

#include "zeroerr/assert.h"

#include <zeroerr/unittest.h>
#include <string>
#include "editing/EditingResult.h"
// editing_result_storage.cpp
namespace test_editing_result_storage {
TEST_CASE("editing.failureFactoryPreservesRuleAndStatus") {
    auto result = eve::editing::failed<int>(eve::StatusCode::Conflict, eve::DiagnosticCode::Conflict,
                                            eve::editing::RuleId("test.conflict"), "collision");
    CHECK(!result.ok());
    CHECK(result.code() == eve::StatusCode::Conflict);
    CHECK(result.diagnostics().size() == 1);
    const auto& diagnostic = result.diagnostics().front();
    CHECK(diagnostic.message() == "collision");
    CHECK(diagnostic.source() == "editing");
    CHECK(eve::editing::diagnosticRule(diagnostic).value() == "test.conflict");
}

TEST_CASE("editing.failureFactoryRejectsSuccessfulStatus") {
#if !defined(ZEROERR_NO_ASSERT)
    bool caught = false;
    try {
        auto result = eve::editing::failed<void>(eve::StatusCode::Applied, eve::editing::RuleId("test.invalid"),
                                                 "invalid category");
        result.ignore();
    } catch (const zeroerr::AssertionData& failure) {
        caught = true;
        CHECK(std::string(failure.file).find("EditingResult.h") != std::string::npos);
        CHECK(failure.cond == "eve::Status(status, {}).isFailure()");
        CHECK(!failure.passed);
    }
    CHECK(caught);
#else
    CHECK(true);
#endif
}

TEST_CASE("editing.ruleDiagnosticPreservesOwnedTextSeverityAndProjection") {
    std::string ruleText(4096, 'r');
    std::string message(4096, 'm');
    auto        diagnostic = eve::editing::ruleDiagnostic(eve::DiagnosticCode::Conflict, eve::editing::RuleId(ruleText),
                                                          eve::Severity::Warning, message);
    ruleText               = "changed";
    message                = "changed";
    CHECK(static_cast<bool>(diagnostic.code() == eve::DiagnosticCode::Conflict));
    CHECK(static_cast<bool>(diagnostic.severity() == eve::Severity::Warning));
    CHECK(diagnostic.message() == std::string(4096, 'm'));
    CHECK(diagnostic.path().empty());
    CHECK(diagnostic.source() == "editing");
    REQUIRE(diagnostic.details().size() == 1u);
    CHECK(diagnostic.details().front().first == "rule");
    CHECK(diagnostic.details().front().second == std::string(4096, 'r'));
    CHECK(eve::editing::diagnosticRule(diagnostic).value() == std::string(4096, 'r'));
}
}  // namespace test_editing_result_storage
