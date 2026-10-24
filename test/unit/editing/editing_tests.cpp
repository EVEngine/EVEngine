
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
#endif
