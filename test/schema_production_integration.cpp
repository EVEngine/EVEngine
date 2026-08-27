#include "zeroerr/unittest.h"

#include "definitions/Definitions.h"
#include "policyregistry/PolicyRegistry.h"
#include "schema/SchemaRegistry.h"

namespace {

eve::schema::SchemaDefinition objectSchema(std::string id) {
    eve::schema::SchemaDefinition schema;
    schema.id = std::move(id);
    schema.version = 1;
    schema.additionalProperties = false;
    eve::schema::FieldDefinition enabled;
    enabled.name = "enabled";
    enabled.type = eve::schema::ValueType::Boolean;
    enabled.required = true;
    schema.fields.push_back(std::move(enabled));
    return schema;
}

}  // namespace

TEST_CASE("schema.productionBoundary.validatesDefinitionsBeforeMutation") {
    eve::schema::SchemaRegistry::clear();
    auto schemaRegistration = eve::schema::SchemaRegistry::registerVersioned(objectSchema("ability"));
    REQUIRE(schemaRegistration.ok());
    CHECK_EQ(schemaRegistration.value(), eve::schema::SchemaRegistrationStatus::Registered);

    eve::definitions::DefinitionRegistry registry;
    auto invalid = registry.insert("ability", "dash", 1, R"({"enabled":"yes"})");
    CHECK(!invalid.ok());
    CHECK_EQ(registry.size(), 0);
    invalid.ignore("expected schema rejection");

    auto valid = registry.insert("ability", "dash", 1, R"({"enabled":true})");
    REQUIRE(valid.ok());
    CHECK_EQ(registry.size(), 1);
}

TEST_CASE("schema.productionBoundary.validatesPolicyMetadataBeforeMutation") {
    eve::schema::SchemaRegistry::clear();
    auto schemaRegistration = eve::schema::SchemaRegistry::registerVersioned(objectSchema("policy:movement"));
    REQUIRE(schemaRegistration.ok());
    CHECK_EQ(schemaRegistration.value(), eve::schema::SchemaRegistrationStatus::Registered);

    eve::policyregistry::PolicyRegistry registry;
    auto invalid = registry.insert("movement", "ground", 1, 0, true, "builtin",
                                   "policy:movement", R"({"enabled":1})");
    CHECK(!invalid.ok());
    CHECK_EQ(registry.size(), 0);
    invalid.ignore("expected schema rejection");

    auto valid = registry.insert("movement", "ground", 1, 0, true, "builtin",
                                 "policy:movement", R"({"enabled":true})");
    REQUIRE(valid.ok());
    CHECK_EQ(registry.size(), 1);
}
