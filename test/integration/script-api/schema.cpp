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

[[maybe_unused]] bool registrationSucceeded(eve::Result<SchemaRegistrationStatus> result) {
    return result.ok() && result.value() == SchemaRegistrationStatus::Registered;
}

}  // namespace


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
