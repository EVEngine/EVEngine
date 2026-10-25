// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "schema_contract.cpp",
    "schema.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_schema_contract)
#include "PrivateTestScope.h"
#include <cstdint>
#include <string>
#include <utility>
#include "schema/SchemaRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// schema_contract.cpp
namespace test_schema_contract {
namespace {

using eve::Value;
using eve::schema::FieldDefinition;
using eve::schema::SchemaDefinition;
using eve::schema::SchemaRegistry;
using eve::schema::ValueType;

FieldDefinition requiredField(std::string name, ValueType type) {
    FieldDefinition field;
    field.name     = std::move(name);
    field.type     = type;
    field.required = true;
    return field;
}

SchemaDefinition versionedPayload(int version) {
    SchemaDefinition schema;
    schema.id                   = "test:migration";
    schema.version              = version;
    schema.additionalProperties = false;
    schema.fields.push_back(requiredField("number", ValueType::Integer));
    if (version >= 2) schema.fields.push_back(requiredField("label", ValueType::String));
    if (version >= 3) schema.fields.push_back(requiredField("ready", ValueType::Boolean));
    return schema;
}

Value objectValue(int number) {
    Value::Object object;
    object.emplace("number", Value(static_cast<std::int64_t>(number)));
    return Value(std::move(object));
}

bool registrationSucceeded(eve::Result<eve::schema::SchemaRegistrationStatus> result) {
    return result.ok() && result.value() == eve::schema::SchemaRegistrationStatus::Registered;
}

}  // namespace

TEST_CASE("schema.migration.multiHopCompatibilityAndCanonicalOutput") {
    SchemaRegistry::clear();
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(1))));
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(2))));
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(3))));

    auto first =
        SchemaRegistry::registerMigration("test:migration", 1, 2, [](const Value& input) -> eve::Result<Value> {
            const auto* object = input.getIf<Value::Object>();
            if (!object)
                return eve::Result<Value>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "expected object"));
            Value::Object next = *object;
            next.emplace("label", Value("legacy"));
            return eve::Result<Value>::success(Value(std::move(next)));
        });
    REQUIRE(first.ok());
    auto second =
        SchemaRegistry::registerMigration("test:migration", 2, 3, [](const Value& input) -> eve::Result<Value> {
            const auto* object = input.getIf<Value::Object>();
            if (!object)
                return eve::Result<Value>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "expected object"));
            Value::Object next = *object;
            next.emplace("ready", Value(true));
            return eve::Result<Value>::success(Value(std::move(next)));
        });
    REQUIRE(second.ok());

    auto compatibility = SchemaRegistry::queryCompatibility("test:migration", 1, 3);
    REQUIRE(compatibility.ok());
    CHECK(!compatibility.value().exact());
    CHECK_EQ(compatibility.value().versions, std::vector<int>({1, 2, 3}));

    const Value input    = objectValue(7);
    auto        migrated = SchemaRegistry::migrate("test:migration", 1, 3, input);
    REQUIRE(migrated.ok());
    CHECK_EQ(input, objectValue(7));
    const auto* result = migrated.value().getIf<Value::Object>();
    REQUIRE(result != nullptr);
    CHECK_EQ(result->at("label"), Value("legacy"));
    CHECK_EQ(result->at("ready"), Value(true));

    auto json = SchemaRegistry::migrateJson("test:migration", 1, 3, R"({"number":7})");
    REQUIRE(json.ok());
    CHECK_EQ(json.value(), std::string(R"({"label":"legacy","number":7,"ready":true})"));
}

TEST_CASE("schema.migration.missingUnknownAndDowngradeAreStructuredFailures") {
    SchemaRegistry::clear();
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(1))));
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(3))));

    auto missing = SchemaRegistry::queryCompatibility("test:migration", 1, 3);
    CHECK(!missing.ok());
    CHECK(static_cast<int>(missing.error()->code()) == static_cast<int>(eve::DiagnosticCode::Unsupported));

    auto unknown = SchemaRegistry::queryCompatibility("test:migration", 1, 99);
    CHECK(!unknown.ok());
    CHECK(static_cast<int>(unknown.error()->code()) == static_cast<int>(eve::DiagnosticCode::UnknownVersion));

    auto downgrade = SchemaRegistry::queryCompatibility("test:migration", 3, 1);
    CHECK(!downgrade.ok());
    CHECK(static_cast<int>(downgrade.error()->code()) == static_cast<int>(eve::DiagnosticCode::Unsupported));
}

TEST_CASE("schema.migration.rejectsCyclesAndDowngradeEdgesWithoutRegistryMutation") {
    SchemaRegistry::clear();
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(1))));
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(2))));
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(3))));
    REQUIRE(SchemaRegistry::registerMigration("test:migration", 1, 2, [](const Value& input) {
                return eve::Result<Value>::success(input);
            }).ok());

    auto cycle = SchemaRegistry::registerMigration(
        "test:migration", 2, 1, [](const Value& input) { return eve::Result<Value>::success(input); });
    CHECK(!cycle.ok());
    CHECK(static_cast<int>(cycle.error()->code()) == static_cast<int>(eve::DiagnosticCode::Unsupported));
    auto self = SchemaRegistry::registerMigration(
        "test:migration", 2, 2, [](const Value& input) { return eve::Result<Value>::success(input); });
    CHECK(!self.ok());
    CHECK(static_cast<int>(self.error()->code()) == static_cast<int>(eve::DiagnosticCode::Conflict));

    auto path = SchemaRegistry::queryCompatibility("test:migration", 1, 2);
    REQUIRE(path.ok());
    CHECK_EQ(path.value().versions, std::vector<int>({1, 2}));
}

TEST_CASE("schema.migration.callbackFailurePreservesInputAndRegistry") {
    SchemaRegistry::clear();
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(1))));
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(2))));
    REQUIRE(SchemaRegistry::registerMigration("test:migration", 1, 2, [](const Value&) -> eve::Result<Value> {
                return eve::Result<Value>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected migration failure"));
            }).ok());

    const Value input  = objectValue(9);
    const int   before = SchemaRegistry::versionCount();
    auto        failed = SchemaRegistry::migrate("test:migration", 1, 2, input);
    CHECK(!failed.ok());
    CHECK(static_cast<int>(failed.error()->code()) == static_cast<int>(eve::DiagnosticCode::Failed));
    CHECK_EQ(input, objectValue(9));
    CHECK_EQ(SchemaRegistry::versionCount(), before);
    CHECK(SchemaRegistry::queryCompatibility("test:migration", 1, 2).ok());
}

TEST_CASE("schema.migration.invalidOutputPreservesInputAndRegistry") {
    SchemaRegistry::clear();
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(1))));
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(2))));
    REQUIRE(SchemaRegistry::registerMigration("test:migration", 1, 2, [](const Value& input) {
                return eve::Result<Value>::success(input);
            }).ok());

    const Value input  = objectValue(11);
    const int   before = SchemaRegistry::versionCount();
    auto        failed = SchemaRegistry::migrate("test:migration", 1, 2, input);
    CHECK(!failed.ok());
    CHECK(static_cast<int>(failed.error()->code()) == static_cast<int>(eve::DiagnosticCode::ParseError));
    CHECK_EQ(input, objectValue(11));
    CHECK_EQ(SchemaRegistry::versionCount(), before);
    CHECK(SchemaRegistry::queryCompatibility("test:migration", 1, 2).ok());
}

TEST_CASE("schema.validation.supportsNestedItemsRefsAndDiscriminatedUnion") {
    SchemaRegistry::clear();
    const std::string address = R"({
        "id":"test:address", "schemaVersion":1, "additionalProperties":false,
        "fields":[{"name":"city","type":"string","required":true}]
    })";
    REQUIRE(registrationSucceeded(SchemaRegistry::registerFromJsonVersioned(address)));
    const std::string profile = R"({
        "id":"test:profile", "schemaVersion":1, "additionalProperties":false,
        "fields":[
          {"name":"address","ref":"test:address","refVersion":1,"required":true},
          {"name":"items","type":"array","minItems":1,"items":{
             "type":"object","additionalProperties":false,
             "fields":[{"name":"id","type":"string","required":true}]}},
          {"name":"shape","discriminator":"kind","discriminatorMapping":{"circle":0,"square":1},"union":[
             {"type":"object","additionalProperties":false,"fields":[
                {"name":"kind","type":"string","required":true,"enum":["circle"]},
                {"name":"radius","type":"number","required":true,"minimum":0}]},
             {"type":"object","additionalProperties":false,"fields":[
                {"name":"kind","type":"string","required":true,"enum":["square"]},
                {"name":"side","type":"number","required":true,"minimum":0}]}
          ]}
        ]
    })";
    REQUIRE(registrationSucceeded(SchemaRegistry::registerFromJsonVersioned(profile)));

    const std::string valid =
        R"({"address":{"city":"Osaka"},"items":[{"id":"a"}],"shape":{"kind":"circle","radius":2}})";
    CHECK(SchemaRegistry::validate("test:profile", 1, valid).empty());
    const auto invalid = SchemaRegistry::validate(
        "test:profile", 1, R"({"address":{"city":4},"items":[{}],"shape":{"kind":"triangle","radius":-1}})");
    CHECK(!invalid.empty());
    CHECK_EQ(invalid[0].path, std::string("/address/city"));
    CHECK_EQ(invalid[0].code, std::string("type"));
}

TEST_CASE("schema.validation.rejectsKeywordsOutsideEveSubset") {
    SchemaRegistry::clear();
    auto status = SchemaRegistry::registerFromJsonVersioned(
        R"({"id":"test:unsupported","schemaVersion":1,"patternProperties":{}})");
    CHECK(!status.ok());
    CHECK_EQ(status.code(), eve::StatusCode::Failed);
    REQUIRE(status.error() != nullptr);
    CHECK(status.error()->message().find("patternProperties") != std::string::npos);
}

TEST_CASE("schema.validation.detectsSchemaReferenceCycle") {
    SchemaRegistry::clear();
    REQUIRE(registrationSucceeded(SchemaRegistry::registerFromJsonVersioned(
        R"({"id":"test:a","schemaVersion":1,"fields":[{"name":"b","ref":"test:b"}]})")));
    REQUIRE(registrationSucceeded(SchemaRegistry::registerFromJsonVersioned(
        R"({"id":"test:b","schemaVersion":1,"fields":[{"name":"a","ref":"test:a"}]})")));
    const auto errors = SchemaRegistry::validate("test:a", 1, R"({"b":{"a":{}}})");
    REQUIRE(!errors.empty());
    CHECK_EQ(errors.back().code, std::string("schema_ref_cycle"));
}
}  // namespace test_schema_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_schema)
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "schema/Schema.h"
#include "schema/SchemaRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// schema.cpp
namespace test_schema_unit {
using namespace eve::schema;

namespace {

const char* unitSchema = R"({
  "id":"unit", "version":2, "title":"Unit", "additionalProperties":false,
  "fields":[
    {"name":"id", "type":"string", "required":true, "minLength":1},
    {"name":"speed", "type":"number", "minimum":0, "maximum":100},
    {"name":"role", "type":"string", "enum":["tank","infantry"]},
    {"name":"weapons", "type":"array", "elementType":"string", "reference":"weapon"}
  ]
})";

[[maybe_unused]] bool registrationSucceeded(eve::Result<SchemaRegistrationStatus> result) {
    return result.ok() && result.value() == SchemaRegistrationStatus::Registered;
}

}  // namespace

TEST_CASE("schema.registry.metadataAndStableEnumeration") {
    SchemaRegistry::clear();
    CHECK(SchemaRegistry::registerFromJson(unitSchema).ok());
    CHECK(SchemaRegistry::registerFromJson(R"({"id":"ability","version":1})").ok());
    CHECK_EQ(SchemaRegistry::count(), 2);
    CHECK_EQ(SchemaRegistry::ids()[0], "ability");
    const auto* schema = SchemaRegistry::find("unit");
    REQUIRE(schema != nullptr);
    CHECK_EQ(schema->version, 2);
    CHECK_EQ(schema->fields.size(), size_t(4));
    CHECK(schema->fields[0].required);
    CHECK_EQ(schema->fields[3].reference, "weapon");
}

TEST_CASE("schema.registry.supportsMultipleVersionsAndExactResolution") {
    SchemaRegistry::clear();
    SchemaDefinition v1;
    v1.id      = "unit";
    v1.version = 1;
    v1.title   = "Unit v1";
    FieldDefinition legacyField;
    legacyField.name     = "legacy";
    legacyField.type     = ValueType::String;
    legacyField.required = true;
    v1.fields.push_back(legacyField);
    SchemaDefinition v2 = v1;
    v2.version          = 2;
    v2.title            = "Unit v2";
    v2.fields[0].required = false;

    CHECK(registrationSucceeded(SchemaRegistry::registerVersioned(v1)));
    CHECK(registrationSucceeded(SchemaRegistry::registerVersioned(v2)));
    CHECK_EQ(SchemaRegistry::count(), 1);
    CHECK_EQ(SchemaRegistry::versionCount(), 2);
    CHECK_EQ(SchemaRegistry::versions("unit"), std::vector<int>({1, 2}));
    REQUIRE(SchemaRegistry::resolve("unit", 1) != nullptr);
    CHECK_EQ(SchemaRegistry::resolve("unit", 1)->title, "Unit v1");
    REQUIRE(SchemaRegistry::resolve("unit", 2) != nullptr);
    CHECK_EQ(SchemaRegistry::resolve("unit", 2)->title, "Unit v2");
    CHECK_EQ(SchemaRegistry::find("unit")->version, 2);
    CHECK_EQ(SchemaRegistry::validate("unit", 1, R"({})")[0].code, "required");
    CHECK(SchemaRegistry::validate("unit", 2, R"({})").empty());
    CHECK(!SchemaRegistry::validate("unit", 2, R"({"legacy": 7})").empty());
    CHECK(SchemaRegistry::validate("unit", R"({})").empty());
}

TEST_CASE("schema.registry.rejectsExactVersionConflictWithoutOverwriting") {
    SchemaRegistry::clear();
    SchemaDefinition definition;
    definition.id      = "unit";
    definition.version = 3;
    definition.title   = "original";
    CHECK(registrationSucceeded(SchemaRegistry::registerVersioned(definition)));

    definition.title = "replacement";
    auto conflict    = SchemaRegistry::registerVersioned(definition);
    CHECK(!conflict.ok());
    CHECK_EQ(conflict.code(), eve::StatusCode::Conflict);
    REQUIRE(SchemaRegistry::resolve("unit", 3) != nullptr);
    CHECK_EQ(SchemaRegistry::resolve("unit", 3)->title, "original");

    auto replacement = SchemaRegistry::registerSchema(definition);
    REQUIRE(replacement.ok());
    CHECK_EQ(replacement.value(), SchemaRegistrationStatus::Replaced);
    CHECK_EQ(SchemaRegistry::resolve("unit", 3)->title, "replacement");
}

TEST_CASE("schema.registry.distinguishesMissingVersion") {
    SchemaRegistry::clear();
    SchemaDefinition definition;
    definition.id      = "unit";
    definition.version = 1;
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(definition)));

    CHECK(SchemaRegistry::resolve("unit", 2) == nullptr);
    const auto errors = SchemaRegistry::validate("unit", 2, R"({})");
    REQUIRE_EQ(errors.size(), size_t(1));
    CHECK_EQ(errors[0].code, "schema_version_not_found");
    CHECK(SchemaRegistry::resolve("missing", 1) == nullptr);
}

TEST_CASE("schema.registry.acceptsCanonicalSchemaVersionAndRejectsConflictingAliases") {
    SchemaRegistry::clear();
    CHECK(registrationSucceeded(SchemaRegistry::registerFromJsonVersioned(R"({"id":"unit","schemaVersion":4})")));
    REQUIRE(SchemaRegistry::resolve("unit", 4) != nullptr);
    CHECK_EQ(SchemaRegistry::resolve("unit", 4)->version, 4);

    auto conflict = SchemaRegistry::registerFromJsonVersioned(R"({"id":"conflicting","version":1,"schemaVersion":2})");
    CHECK(!conflict.ok());
    CHECK_EQ(conflict.code(), eve::StatusCode::Failed);
}

TEST_CASE("schema.registry.legacyFacadeKeepsVersionsAndRemovesExactly") {
    SchemaRegistry::clear();
    SchemaDefinition v1;
    v1.id               = "unit";
    v1.version          = 1;
    SchemaDefinition v2 = v1;
    v2.version          = 2;
    REQUIRE(SchemaRegistry::registerSchema(v1).ok());
    REQUIRE(SchemaRegistry::registerSchema(v2).ok());
    CHECK(SchemaRegistry::remove("unit", 1).ok());
    CHECK(SchemaRegistry::resolve("unit", 1) == nullptr);
    CHECK(SchemaRegistry::resolve("unit", 2) != nullptr);
    CHECK_EQ(SchemaRegistry::count(), 1);
    CHECK_EQ(SchemaRegistry::versionCount(), 1);
    CHECK(SchemaRegistry::remove("unit").ok());
    CHECK_EQ(SchemaRegistry::count(), 0);
}

TEST_CASE("schema.registry.rejectsMalformedDefinitions") {
    SchemaRegistry::clear();
    auto emptyId = SchemaRegistry::registerFromJson(R"({"id":"","version":1})");
    CHECK(!emptyId.ok());
    auto badType = SchemaRegistry::registerFromJson(R"({"id":"bad","fields":[{"name":"x","type":"mystery"}]})");
    CHECK(!badType.ok());
    auto duplicate = SchemaRegistry::registerFromJson(R"({"id":"duplicate","fields":[{"name":"x"},{"name":"x"}]})");
    CHECK(!duplicate.ok());
    CHECK_EQ(SchemaRegistry::count(), 0);
}

TEST_CASE("schema.validation.returnsStructuredErrors") {
    SchemaRegistry::clear();
    REQUIRE(SchemaRegistry::registerFromJson(unitSchema).ok());
    const auto valid =
        SchemaRegistry::validate("unit", R"({"id":"heavy_tank","speed":42,"role":"tank","weapons":["cannon"]})");
    CHECK(valid.empty());

    const auto errors =
        SchemaRegistry::validate("unit", R"({"speed":120,"role":"scout","weapons":["gun",3],"extra":true})");
    CHECK_EQ(errors.size(), size_t(5));
    CHECK_EQ(errors[0].path, "/id");
    CHECK_EQ(errors[0].code, "required");
    CHECK_EQ(errors[1].code, "maximum");
    CHECK_EQ(errors[2].code, "enum");
    CHECK_EQ(errors[3].path, "/weapons/1");
    CHECK_EQ(errors[4].code, "additional_property");
}
}  // namespace test_schema_unit
#endif
