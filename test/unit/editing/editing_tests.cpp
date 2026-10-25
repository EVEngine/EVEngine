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


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_editing_property_schema_json)
#include "PrivateTestScope.h"
#include "editing/EditingValueJson.h"
#include "editing/PropertySchemaJson.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// editing_property_schema_json.cpp
namespace test_editing_property_schema_json {
using namespace eve::editing;

TEST_CASE("editing.property_schema_json.projects_empty_object_and_fields") {
    PropertySchema empty     = emptyObjectPropertySchema();
    auto           emptyJson = propertySchemaToJsonSchema(empty, "emptyObject");
    REQUIRE(emptyJson.ok());
    const std::string encoded = editorValueToJson(emptyJson.value());
    CHECK(encoded.find("\"type\":\"object\"") != std::string::npos);
    CHECK(encoded.find("\"$id\":\"emptyObject\"") != std::string::npos);

    PropertySchema schema;
    schema.typeId = "demo.command";
    PropertyDescriptor flag;
    flag.path           = PropertyPath("enabled");
    flag.displayNameKey = "Enabled";
    flag.type           = PropertyType::Bool;
    schema.properties.push_back(flag);
    PropertyDescriptor mode;
    mode.path           = PropertyPath("mode");
    mode.displayNameKey = "Mode";
    mode.type           = PropertyType::Enum;
    mode.enumItems      = {"a", "b"};
    schema.properties.push_back(mode);

    auto projected = propertySchemaToJsonSchema(schema, "demo.command");
    REQUIRE(projected.ok());
    const std::string json = editorValueToJson(projected.value());
    CHECK(json.find("\"enabled\"") != std::string::npos);
    CHECK(json.find("\"boolean\"") != std::string::npos);
    CHECK(json.find("\"mode\"") != std::string::npos);
    CHECK(json.find("\"a\"") != std::string::npos);
}

TEST_CASE("editing.property_schema_json.rejects_empty_schema_id") {
    PropertySchema schema = emptyObjectPropertySchema();
    auto           failed = propertySchemaToJsonSchema(schema, "");
    CHECK(!failed.ok());
}
}  // namespace test_editing_property_schema_json

#include "EditingNumericCompatibility.h"
namespace test_editing_numeric_compatibility {
TEST_CASE("editing.numericRead.preservesNumbersWithoutAddingValidation") {
    editing_numeric_compatibility::preservesNumbersWithoutAddingValidation(_ZEROERR_TEST_CONTEXT);
}
TEST_CASE("editing.numericRead.rejectsNonNumericKinds") {
    editing_numeric_compatibility::rejectsNonNumericKinds(_ZEROERR_TEST_CONTEXT);
}
}  // namespace test_editing_numeric_compatibility
#endif

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
