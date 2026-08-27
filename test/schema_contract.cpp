#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "schema/SchemaBuiltins.h"
#include "schema/SchemaRegistry.h"

#include <cstdint>
#include <string>
#include <utility>

namespace {

using eve::Value;
using eve::schema::FieldDefinition;
using eve::schema::SchemaDefinition;
using eve::schema::SchemaRegistry;
using eve::schema::ValueType;

FieldDefinition requiredField(std::string name, ValueType type) {
    FieldDefinition field;
    field.name = std::move(name);
    field.type = type;
    field.required = true;
    return field;
}

SchemaDefinition versionedPayload(int version) {
    SchemaDefinition schema;
    schema.id = "test:migration";
    schema.version = version;
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

    auto first = SchemaRegistry::registerMigration(
        "test:migration", 1, 2, [](const Value& input) -> eve::Result<Value> {
            const auto* object = input.getIf<Value::Object>();
            if (!object) return eve::Result<Value>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "expected object"));
            Value::Object next = *object;
            next.emplace("label", Value("legacy"));
            return eve::Result<Value>::success(Value(std::move(next)));
        });
    REQUIRE(first.ok());
    auto second = SchemaRegistry::registerMigration(
        "test:migration", 2, 3, [](const Value& input) -> eve::Result<Value> {
            const auto* object = input.getIf<Value::Object>();
            if (!object) return eve::Result<Value>::failure(
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

    const Value input = objectValue(7);
    auto migrated = SchemaRegistry::migrate("test:migration", 1, 3, input);
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
    CHECK(static_cast<int>(missing.error()->code()) ==
          static_cast<int>(eve::DiagnosticCode::Unsupported));

    auto unknown = SchemaRegistry::queryCompatibility("test:migration", 1, 99);
    CHECK(!unknown.ok());
    CHECK(static_cast<int>(unknown.error()->code()) ==
          static_cast<int>(eve::DiagnosticCode::UnknownVersion));

    auto downgrade = SchemaRegistry::queryCompatibility("test:migration", 3, 1);
    CHECK(!downgrade.ok());
    CHECK(static_cast<int>(downgrade.error()->code()) ==
          static_cast<int>(eve::DiagnosticCode::Unsupported));
}

TEST_CASE("schema.migration.rejectsCyclesAndDowngradeEdgesWithoutRegistryMutation") {
    SchemaRegistry::clear();
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(1))));
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(2))));
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(3))));
    REQUIRE(SchemaRegistry::registerMigration("test:migration", 1, 2,
                                              [](const Value& input) { return eve::Result<Value>::success(input); })
                .ok());

    auto cycle = SchemaRegistry::registerMigration("test:migration", 2, 1,
                                                   [](const Value& input) {
                                                       return eve::Result<Value>::success(input);
                                                   });
    CHECK(!cycle.ok());
    CHECK(static_cast<int>(cycle.error()->code()) ==
          static_cast<int>(eve::DiagnosticCode::Unsupported));
    auto self = SchemaRegistry::registerMigration("test:migration", 2, 2,
                                                  [](const Value& input) {
                                                      return eve::Result<Value>::success(input);
                                                  });
    CHECK(!self.ok());
    CHECK(static_cast<int>(self.error()->code()) ==
          static_cast<int>(eve::DiagnosticCode::Conflict));

    auto path = SchemaRegistry::queryCompatibility("test:migration", 1, 2);
    REQUIRE(path.ok());
    CHECK_EQ(path.value().versions, std::vector<int>({1, 2}));
}

TEST_CASE("schema.migration.callbackFailurePreservesInputAndRegistry") {
    SchemaRegistry::clear();
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(1))));
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(2))));
    REQUIRE(SchemaRegistry::registerMigration(
                "test:migration", 1, 2, [](const Value&) -> eve::Result<Value> {
                    return eve::Result<Value>::failure(
                        eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected migration failure"));
                })
                .ok());

    const Value input = objectValue(9);
    const int before = SchemaRegistry::versionCount();
    auto failed = SchemaRegistry::migrate("test:migration", 1, 2, input);
    CHECK(!failed.ok());
    CHECK(static_cast<int>(failed.error()->code()) ==
          static_cast<int>(eve::DiagnosticCode::Failed));
    CHECK_EQ(input, objectValue(9));
    CHECK_EQ(SchemaRegistry::versionCount(), before);
    CHECK(SchemaRegistry::queryCompatibility("test:migration", 1, 2).ok());
}

TEST_CASE("schema.migration.invalidOutputPreservesInputAndRegistry") {
    SchemaRegistry::clear();
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(1))));
    REQUIRE(registrationSucceeded(SchemaRegistry::registerVersioned(versionedPayload(2))));
    REQUIRE(SchemaRegistry::registerMigration(
                "test:migration", 1, 2,
                [](const Value& input) { return eve::Result<Value>::success(input); })
                .ok());

    const Value input = objectValue(11);
    const int before = SchemaRegistry::versionCount();
    auto failed = SchemaRegistry::migrate("test:migration", 1, 2, input);
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

    const std::string valid = R"({"address":{"city":"Osaka"},"items":[{"id":"a"}],"shape":{"kind":"circle","radius":2}})";
    CHECK(SchemaRegistry::validate("test:profile", 1, valid).empty());
    const auto invalid = SchemaRegistry::validate(
        "test:profile", 1,
        R"({"address":{"city":4},"items":[{}],"shape":{"kind":"triangle","radius":-1}})");
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

TEST_CASE("schema.builtins.registerFormatsAndGenerateStableContracts") {
    SchemaRegistry::clear();
    auto standard = eve::schema::registerStandardSchemas();
    REQUIRE(standard.ok());
    CHECK_EQ(SchemaRegistry::versionCount(), 3);
    CHECK(SchemaRegistry::resolve("snapshot:envelope", 1) != nullptr);
    CHECK(SchemaRegistry::resolve("game_event:envelope", 1) != nullptr);
    CHECK(SchemaRegistry::resolve("definitions:metadata", 1) != nullptr);

    const auto documentation = SchemaRegistry::generateDocumentation("snapshot:envelope", 1);
    REQUIRE(documentation.ok());
    const auto documentationAgain = SchemaRegistry::generateDocumentation("snapshot:envelope", 1);
    REQUIRE(documentationAgain.ok());
    CHECK_EQ(documentation.value(), documentationAgain.value());
    CHECK(documentation.value().find("Eve Schema v1") != std::string::npos);

    const auto contract = SchemaRegistry::generateBindingContract("game_event:envelope", 1);
    REQUIRE(contract.ok());
    const auto contractAgain = SchemaRegistry::generateBindingContract("game_event:envelope", 1);
    REQUIRE(contractAgain.ok());
    CHECK_EQ(contract.value(), contractAgain.value());
    CHECK(contract.value().find("game_event:envelope") != std::string::npos);
}
