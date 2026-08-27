#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "schema/Schema.h"
#include "schema/SchemaRegistry.h"

#include <simplesquirrel/simplesquirrel.hpp>

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

bool registrationSucceeded(eve::Result<SchemaRegistrationStatus> result) {
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
    v2.version           = 2;
    v2.title             = "Unit v2";

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
    auto conflict = SchemaRegistry::registerVersioned(definition);
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
    CHECK(registrationSucceeded(SchemaRegistry::registerFromJsonVersioned(
        R"({"id":"unit","schemaVersion":4})")));
    REQUIRE(SchemaRegistry::resolve("unit", 4) != nullptr);
    CHECK_EQ(SchemaRegistry::resolve("unit", 4)->version, 4);

    auto conflict = SchemaRegistry::registerFromJsonVersioned(
        R"({"id":"conflicting","version":1,"schemaVersion":2})");
    CHECK(!conflict.ok());
    CHECK_EQ(conflict.code(), eve::StatusCode::Failed);
}

TEST_CASE("schema.registry.legacyFacadeKeepsVersionsAndRemovesExactly") {
    SchemaRegistry::clear();
    SchemaDefinition v1;
    v1.id      = "unit";
    v1.version = 1;
    SchemaDefinition v2 = v1;
    v2.version           = 2;
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
    auto badType = SchemaRegistry::registerFromJson(
        R"({"id":"bad","fields":[{"name":"x","type":"mystery"}]})");
    CHECK(!badType.ok());
    auto duplicate = SchemaRegistry::registerFromJson(
        R"({"id":"duplicate","fields":[{"name":"x"},{"name":"x"}]})");
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

TEST_CASE("schema.module.scriptBinding") {
    SchemaRegistry::clear();
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    auto script = vm.compileSource(R"(
        function schemaBindingTest() {
            local s = eve.Schema();
            s.clear();
            local registered = s.registerJson(@"{""id"":""rank"",""version"":3,""fields"":[{""name"":""level"",""type"":""integer"",""required"":true}]}");
            if (!registered.ok || !eve.result.isChecked(registered)) return false;
            if (s.getSchemaId(0) != "rank" || s.getSchemaVersion("rank") != 3) return false;
            if (!s.hasVersion("rank", 3) || s.getSchemaVersionCount("rank") != 1 ||
                s.getSchemaVersionAt("rank", 0) != 3) return false;
            if (s.getFieldName("rank", 0) != "level" || !s.getFieldRequired("rank", 0)) return false;
            local valid = s.validateJson("rank", @"{""level"":2}");
            if (!valid.ok || !eve.result.isChecked(valid)) return false;
            local invalid = s.validateJson("rank", @"{""level"":2.5}");
            if (invalid.ok || !eve.result.isChecked(invalid)) return false;
            if (s.getValidationErrorCode(0) != "type") return false;
            local accepted = s.validateJsonVersioned("rank", 3, @"{""level"":2}");
            if (!accepted.ok || accepted.code != "applied" || !eve.result.isChecked(accepted)) return false;
            local rejected = s.validateJsonVersioned("rank", 3, @"{""level"":2.5}");
            return !rejected.ok && rejected.diagnostics.len() == 1 &&
                   rejected.diagnostics[0].code == "invalid_argument" &&
                   rejected.diagnostics[0].path == "/level" && eve.result.isChecked(rejected);
        }
    )");
    vm.run(script);
    CHECK(vm.callFunc(vm.findFunc("schemaBindingTest"), vm).toBool());
}
