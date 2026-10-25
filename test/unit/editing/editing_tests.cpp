// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "local-format",
  "origins": [
    "editing_property_schema_json.cpp"
  ]
}
*/
// clang-format on

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
