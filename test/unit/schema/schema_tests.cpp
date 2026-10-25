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

#include "schema/SchemaRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {
using eve::Value;
using eve::schema::FieldDefinition;
using eve::schema::SchemaDefinition;
using eve::schema::SchemaRegistrationStatus;
using eve::schema::SchemaRegistry;
using eve::schema::ValueType;

bool registered(eve::Result<SchemaRegistrationStatus> result) {
    return result.ok() && result.value() == SchemaRegistrationStatus::Registered;
}

SchemaDefinition definition(int version) {
    SchemaDefinition value;
    value.id                   = "test:document";
    value.version              = version;
    value.additionalProperties = false;
    FieldDefinition number;
    number.name     = "number";
    number.type     = ValueType::Integer;
    number.required = true;
    value.fields.push_back(number);
    if (version >= 2) {
        FieldDefinition label;
        label.name     = "label";
        label.type     = ValueType::String;
        label.required = true;
        value.fields.push_back(label);
    }
    if (version >= 3) {
        FieldDefinition ready;
        ready.name     = "ready";
        ready.type     = ValueType::Boolean;
        ready.required = true;
        value.fields.push_back(ready);
    }
    return value;
}

Value numberValue(int number) {
    Value::Object object;
    object.emplace("number", Value(static_cast<std::int64_t>(number)));
    return Value(std::move(object));
}

eve::Result<Value> addField(const Value& input, const char* key, Value value) {
    const auto* object = input.getIf<Value::Object>();
    if (!object) {
        return eve::Result<Value>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "object required"));
    }
    Value::Object output = *object;
    output.emplace(key, std::move(value));
    return eve::Result<Value>::success(Value(std::move(output)));
}

void registerVersions() {
    REQUIRE(registered(SchemaRegistry::registerVersioned(definition(1))));
    REQUIRE(registered(SchemaRegistry::registerVersioned(definition(2))));
    REQUIRE(registered(SchemaRegistry::registerVersioned(definition(3))));
}

void registerForwardChain() {
    REQUIRE(SchemaRegistry::registerMigration("test:document", 1, 2, [](const Value& input) {
                return addField(input, "label", Value("legacy"));
            }).ok());
    REQUIRE(SchemaRegistry::registerMigration("test:document", 2, 3, [](const Value& input) {
                return addField(input, "ready", Value(true));
            }).ok());
}
}  // namespace

// Registry: ids enumerate lexically; versions resolve exactly; the id-only facade uses the newest version.
TEST_CASE("schema.registry.versionConflictRemovalAndInvalidDefinitionContract") {
    SUB_CASE("versioned entries and legacy latest resolution") {
        SchemaRegistry::clear();
        REQUIRE(registered(SchemaRegistry::registerVersioned(definition(1))));
        REQUIRE(registered(SchemaRegistry::registerVersioned(definition(2))));
        REQUIRE(registered(SchemaRegistry::registerFromJsonVersioned(R"({"id":"test:alpha","schemaVersion":4})")));
        CHECK_EQ(SchemaRegistry::count(), 2);
        CHECK_EQ(SchemaRegistry::versionCount(), 3);
        CHECK(SchemaRegistry::ids() == (std::vector<std::string>{"test:alpha", "test:document"}));
        CHECK(SchemaRegistry::versions("test:document") == (std::vector<int>{1, 2}));
        REQUIRE(SchemaRegistry::resolve("test:document", 1) != nullptr);
        REQUIRE(SchemaRegistry::resolve("test:document", 2) != nullptr);
        CHECK_EQ(SchemaRegistry::resolve("test:document", 1)->fields.size(), size_t{1});
        CHECK_EQ(SchemaRegistry::find("test:document")->version, 2);
        CHECK(SchemaRegistry::resolve("test:document", 99) == nullptr);
        CHECK(SchemaRegistry::resolve("missing", 1) == nullptr);
    };
    SUB_CASE("conflict does not replace but legacy registration does") {
        SchemaRegistry::clear();
        auto first  = definition(1);
        first.title = "original";
        REQUIRE(registered(SchemaRegistry::registerVersioned(first)));
        auto replacement  = first;
        replacement.title = "replacement";
        auto conflict     = SchemaRegistry::registerVersioned(replacement);
        CHECK(!conflict.ok());
        CHECK(static_cast<int>(conflict.code()) == static_cast<int>(eve::StatusCode::Conflict));
        CHECK_EQ(SchemaRegistry::resolve(first.id, 1)->title, std::string("original"));
        auto replaced = SchemaRegistry::registerSchema(replacement);
        REQUIRE(replaced.ok());
        CHECK(replaced.value() == SchemaRegistrationStatus::Replaced);
        CHECK_EQ(SchemaRegistry::resolve(first.id, 1)->title, std::string("replacement"));
    };
    SUB_CASE("removal can target one version or whole ID") {
        SchemaRegistry::clear();
        registerVersions();
        REQUIRE(SchemaRegistry::remove("test:document", 2).ok());
        CHECK(SchemaRegistry::resolve("test:document", 2) == nullptr);
        CHECK(SchemaRegistry::versions("test:document") == (std::vector<int>{1, 3}));
        CHECK_EQ(SchemaRegistry::count(), 1);
        CHECK_EQ(SchemaRegistry::versionCount(), 2);
        CHECK(!SchemaRegistry::remove("test:document", 2).ok());
        REQUIRE(SchemaRegistry::remove("test:document").ok());
        CHECK_EQ(SchemaRegistry::count(), 0);
        CHECK(!SchemaRegistry::remove("test:document").ok());
    };
    SUB_CASE("invalid definitions do not add entries") {
        SchemaRegistry::clear();
        CHECK(!SchemaRegistry::registerFromJsonVersioned(R"({"id":"","schemaVersion":1})").ok());
        CHECK(!SchemaRegistry::registerFromJsonVersioned(
                   R"({"id":"bad","schemaVersion":1,"fields":[{"name":"x","type":"mystery"}]})")
                   .ok());
        CHECK(!SchemaRegistry::registerFromJsonVersioned(
                   R"({"id":"bad","schemaVersion":1,"fields":[{"name":"x"},{"name":"x"}]})")
                   .ok());
        CHECK(!SchemaRegistry::registerFromJsonVersioned(R"({"id":"bad","version":1,"schemaVersion":2})").ok());
        CHECK_EQ(SchemaRegistry::count(), 0);
    };
}

// Validation: every issue has a path and code; unknown fields and unsupported keywords are explicit.
TEST_CASE("schema.validation.typeRangeNestedReferenceAndFailurePathContract") {
    SUB_CASE("required range enum items and unknown properties") {
        SchemaRegistry::clear();
        REQUIRE(registered(SchemaRegistry::registerFromJsonVersioned(R"({
      "id":"test:unit","schemaVersion":1,"additionalProperties":false,
      "fields":[
        {"name":"id","type":"string","required":true,"minLength":1},
        {"name":"speed","type":"number","minimum":0,"maximum":100},
        {"name":"role","type":"string","enum":["tank","infantry"]},
        {"name":"weapons","type":"array","elementType":"string"}
      ]})")));
        CHECK(
            SchemaRegistry::validate("test:unit", 1, R"({"id":"heavy","speed":42,"role":"tank","weapons":["cannon"]})")
                .empty());
        const auto errors = SchemaRegistry::validate(
            "test:unit", 1, R"({"speed":120,"role":"scout","weapons":["gun",3],"extra":true})");
        REQUIRE(errors.size() == size_t{5});
        CHECK_EQ(errors[0].path, std::string("/id"));
        CHECK_EQ(errors[0].code, std::string("required"));
        CHECK_EQ(errors[1].code, std::string("maximum"));
        CHECK_EQ(errors[2].code, std::string("enum"));
        CHECK_EQ(errors[3].path, std::string("/weapons/1"));
        CHECK_EQ(errors[4].code, std::string("additional_property"));
        const auto missing = SchemaRegistry::validate("test:unit", 2, R"({})");
        REQUIRE(missing.size() == size_t{1});
        CHECK_EQ(missing[0].code, std::string("schema_version_not_found"));
    };
    SUB_CASE("nested reference array and discriminator paths") {
        SchemaRegistry::clear();
        REQUIRE(registered(SchemaRegistry::registerFromJsonVersioned(
            R"({"id":"test:address","schemaVersion":1,"additionalProperties":false,
            "fields":[{"name":"city","type":"string","required":true}]})")));
        REQUIRE(registered(SchemaRegistry::registerFromJsonVersioned(R"({
      "id":"test:profile","schemaVersion":1,"additionalProperties":false,
      "fields":[
        {"name":"address","ref":"test:address","refVersion":1,"required":true},
        {"name":"items","type":"array","minItems":1,"items":{"type":"object","additionalProperties":false,
          "fields":[{"name":"id","type":"string","required":true}]}},
        {"name":"shape","discriminator":"kind","discriminatorMapping":{"circle":0,"square":1},"union":[
          {"type":"object","additionalProperties":false,"fields":[
            {"name":"kind","type":"string","required":true,"enum":["circle"]},
            {"name":"radius","type":"number","required":true,"minimum":0}]},
          {"type":"object","additionalProperties":false,"fields":[
            {"name":"kind","type":"string","required":true,"enum":["square"]},
            {"name":"side","type":"number","required":true,"minimum":0}]}
        ]}
      ]})")));
        CHECK(SchemaRegistry::validate(
                  "test:profile", 1,
                  R"({"address":{"city":"Osaka"},"items":[{"id":"a"}],"shape":{"kind":"circle","radius":2}})")
                  .empty());
        const auto errors = SchemaRegistry::validate(
            "test:profile", 1, R"({"address":{"city":4},"items":[{}],"shape":{"kind":"triangle","radius":-1}})");
        REQUIRE(!errors.empty());
        CHECK_EQ(errors[0].path, std::string("/address/city"));
        CHECK_EQ(errors[0].code, std::string("type"));
    };
    SUB_CASE("unsupported keyword and reference cycle are reported") {
        SchemaRegistry::clear();
        auto unsupported =
            SchemaRegistry::registerFromJsonVersioned(R"({"id":"test:bad","schemaVersion":1,"patternProperties":{}})");
        CHECK(!unsupported.ok());
        REQUIRE(unsupported.error() != nullptr);
        CHECK(unsupported.error()->message().find("patternProperties") != std::string::npos);
        REQUIRE(registered(SchemaRegistry::registerFromJsonVersioned(
            R"({"id":"test:a","schemaVersion":1,"fields":[{"name":"b","ref":"test:b"}]})")));
        REQUIRE(registered(SchemaRegistry::registerFromJsonVersioned(
            R"({"id":"test:b","schemaVersion":1,"fields":[{"name":"a","ref":"test:a"}]})")));
        const auto errors = SchemaRegistry::validate("test:a", 1, R"({"b":{"a":{}}})");
        REQUIRE(!errors.empty());
        CHECK_EQ(errors.back().code, std::string("schema_ref_cycle"));
    };
}

// Migration: explicit forward edges define the only compatibility path and produce validated owning values.
TEST_CASE("schema.migration.pathValidationAndAtomicFailureContract") {
    SUB_CASE("exact and multihop paths produce canonical output") {
        SchemaRegistry::clear();
        registerVersions();
        registerForwardChain();
        auto exact = SchemaRegistry::queryCompatibility("test:document", 2, 2);
        REQUIRE(exact.ok());
        CHECK(exact.value().exact());
        CHECK(exact.value().versions == (std::vector<int>{2}));
        auto path = SchemaRegistry::queryCompatibility("test:document", 1, 3);
        REQUIRE(path.ok());
        CHECK(!path.value().exact());
        CHECK(path.value().versions == (std::vector<int>{1, 2, 3}));
        const Value input  = numberValue(7);
        auto        output = SchemaRegistry::migrate("test:document", 1, 3, input);
        REQUIRE(output.ok());
        CHECK(input == numberValue(7));
        const auto* object = output.value().getIf<Value::Object>();
        REQUIRE(object != nullptr);
        CHECK(object->at("label") == Value("legacy"));
        CHECK(object->at("ready") == Value(true));
        auto json = SchemaRegistry::migrateJson("test:document", 1, 3, R"({"number":7})");
        REQUIRE(json.ok());
        CHECK_EQ(json.value(), std::string(R"({"label":"legacy","number":7,"ready":true})"));
    };
    SUB_CASE("missing link unknown version and downgrade are failures") {
        SchemaRegistry::clear();
        registerVersions();
        CHECK(!SchemaRegistry::queryCompatibility("test:document", 1, 3).ok());
        auto unknown = SchemaRegistry::queryCompatibility("test:document", 1, 99);
        CHECK(!unknown.ok());
        CHECK(static_cast<int>(unknown.error()->code()) == static_cast<int>(eve::DiagnosticCode::UnknownVersion));
        auto downgrade = SchemaRegistry::queryCompatibility("test:document", 3, 1);
        CHECK(!downgrade.ok());
        CHECK(static_cast<int>(downgrade.error()->code()) == static_cast<int>(eve::DiagnosticCode::Unsupported));
    };
    SUB_CASE("invalid edges leave registered path untouched") {
        SchemaRegistry::clear();
        registerVersions();
        REQUIRE(SchemaRegistry::registerMigration("test:document", 1, 2, [](const Value& input) {
                    return addField(input, "label", Value("legacy"));
                }).ok());
        CHECK(!SchemaRegistry::registerMigration("test:document", 2, 1, [](const Value& input) {
                   return eve::Result<Value>::success(input);
               }).ok());
        CHECK(!SchemaRegistry::registerMigration("test:document", 2, 2, [](const Value& input) {
                   return eve::Result<Value>::success(input);
               }).ok());
        auto path = SchemaRegistry::queryCompatibility("test:document", 1, 2);
        REQUIRE(path.ok());
        CHECK(path.value().versions == (std::vector<int>{1, 2}));
    };
    SUB_CASE("callback and target validation failure preserve input and registry") {
        SchemaRegistry::clear();
        registerVersions();
        REQUIRE(SchemaRegistry::registerMigration("test:document", 1, 2, [](const Value&) -> eve::Result<Value> {
                    return eve::Result<Value>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected"));
                }).ok());
        const Value input  = numberValue(9);
        const auto  count  = SchemaRegistry::versionCount();
        auto        failed = SchemaRegistry::migrate("test:document", 1, 2, input);
        CHECK(!failed.ok());
        CHECK(input == numberValue(9));
        CHECK_EQ(SchemaRegistry::versionCount(), count);
        SchemaRegistry::clear();
        registerVersions();
        REQUIRE(SchemaRegistry::registerMigration("test:document", 1, 2, [](const Value& value) {
                    return eve::Result<Value>::success(value);
                }).ok());
        auto invalid = SchemaRegistry::migrate("test:document", 1, 2, input);
        CHECK(!invalid.ok());
        CHECK(static_cast<int>(invalid.error()->code()) == static_cast<int>(eve::DiagnosticCode::ParseError));
        CHECK(input == numberValue(9));
        CHECK_EQ(SchemaRegistry::versionCount(), count);
    };
}
