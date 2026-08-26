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

}  // namespace

TEST_CASE("schema.registry.metadataAndStableEnumeration") {
    SchemaRegistry::clear();
    std::string error;
    CHECK(SchemaRegistry::registerFromJson(unitSchema, &error));
    CHECK(error.empty());
    CHECK(SchemaRegistry::registerFromJson(R"({"id":"ability","version":1})"));
    CHECK_EQ(SchemaRegistry::count(), 2);
    CHECK_EQ(SchemaRegistry::ids()[0], "ability");
    const auto* schema = SchemaRegistry::find("unit");
    REQUIRE(schema != nullptr);
    CHECK_EQ(schema->version, 2);
    CHECK_EQ(schema->fields.size(), size_t(4));
    CHECK(schema->fields[0].required);
    CHECK_EQ(schema->fields[3].reference, "weapon");
}

TEST_CASE("schema.registry.rejectsMalformedDefinitions") {
    SchemaRegistry::clear();
    std::string error;
    CHECK(!SchemaRegistry::registerFromJson(R"({"id":"","version":1})", &error));
    CHECK(!error.empty());
    CHECK(!SchemaRegistry::registerFromJson(R"({"id":"bad","fields":[{"name":"x","type":"mystery"}]})", &error));
    CHECK(!SchemaRegistry::registerFromJson(R"({"id":"duplicate","fields":[{"name":"x"},{"name":"x"}]})", &error));
    CHECK_EQ(SchemaRegistry::count(), 0);
}

TEST_CASE("schema.validation.returnsStructuredErrors") {
    SchemaRegistry::clear();
    REQUIRE(SchemaRegistry::registerFromJson(unitSchema));
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
            if (!s.registerJson(@"{""id"":""rank"",""version"":3,""fields"":[{""name"":""level"",""type"":""integer"",""required"":true}]}")) return false;
            if (s.getSchemaId(0) != "rank" || s.getSchemaVersion("rank") != 3) return false;
            if (s.getFieldName("rank", 0) != "level" || !s.getFieldRequired("rank", 0)) return false;
            if (!s.validateJson("rank", @"{""level"":2}")) return false;
            if (s.validateJson("rank", @"{""level"":2.5}")) return false;
            return s.getValidationErrorCode(0) == "type";
        }
    )");
    vm.run(script);
    CHECK(vm.callFunc(vm.findFunc("schemaBindingTest"), vm).toBool());
}

TEST_CASE("schema.module.reflectFromClass") {
    SchemaRegistry::clear();
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    auto script = vm.compileSource(R"(
        class CardDefinition </ id="card.definition", version=1, title="Card Definition", additionalProperties=false /> {
            </ required=true />
            id = ""
            </ required=true />
            name = ""
            </ values=["creature","spell"] />
            kind = "creature"
            </ min=0, max=20 />
            cost = 0
            </ min=0, max=99 />
            attack = 0
            </ min=0, max=99 />
            health = 0
        }

        function schemaReflectTest() {
            local s = eve.Schema();
            s.clear();
            if (!s.registerFromClass(CardDefinition)) return s.getLastError();
            if (s.getSchemaVersion("card.definition") != 1) return "version";
            if (s.getSchemaTitle("card.definition") != "Card Definition") return "title";
            if (s.getSchemaAdditionalProperties("card.definition")) return "additionalProperties";
            if (s.getFieldCount("card.definition") != 6) return "count";
            if (s.getFieldType("card.definition", 3) != "integer") return "type";
            if (!s.getFieldRequired("card.definition", 0)) return "required";
            if (s.getFieldEnumCount("card.definition", 2) != 2) return "enum";
            if (!s.getFieldHasMinimum("card.definition", 3)) return "min";
            if (s.getFieldMaximum("card.definition", 3) != 20) return "max";
            local ok = @"{""id"":""card.scout"",""name"":""Scout"",""kind"":""creature"",""cost"":2,""attack"":3,""health"":2}";
            if (!s.validateJson("card.definition", ok)) return "valid";
            local bad = @"{""id"":""x"",""name"":""y"",""kind"":""air"",""cost"":25}";
            if (s.validateJson("card.definition", bad)) return "invalid";
            return s.getValidationErrorCode(0) == "enum";
        }
    )");
    vm.run(script);
    CHECK(vm.callFunc(vm.findFunc("schemaReflectTest"), vm).toBool());
}
