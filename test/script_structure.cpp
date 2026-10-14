#include "common/ScriptCompiler.h"
#include "common/ScriptStructure.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("script.structure.lexesCallsWithoutCommentOrStringGhosts") {
    auto result = eve::script::analyzeScriptStructure(R"SQ(
        // function ghost() { fake.emit("event.fake"); }
        local text = @"function ghost() { fake.emit(""event.fake""); }";
        function dispatch(envelope: GameEventRecord) {
            if (envelope.getType() != "event.real") return false;
            return handler(log.at(log.size() - 1));
        }
    )SQ");
    REQUIRE(result.diagnostics.empty());
    REQUIRE_EQ(result.functions.size(), size_t(1));
    CHECK_EQ(result.functions[0].name, std::string("dispatch"));
    CHECK_EQ(result.functions[0].parameters[0].type, std::string("GameEventRecord"));
    CHECK_EQ(result.calls.size(), size_t(4));
    bool filter = false;
    for (const auto& call : result.calls) {
        if (call.comparison == "!=") {
            CHECK_EQ(call.comparedValue.value, std::string("event.real"));
            filter = true;
        }
        CHECK_EQ(call.scope, std::string("dispatch"));
    }
    CHECK(filter);
}
TEST_CASE("script.structure.scopesAliasesAndPreservesDynamicExpressions") {
    auto result = eve::script::analyzeScriptStructure(R"SQ(
        persist marker = null
        initialize <- function() {
            local scene = eve.Scene();
            marker = scene.getNodeRefAt("host", "node");
            local event = prefix + dynamicName;
            local target = nodes[index];
        }
    )SQ");
    REQUIRE(result.diagnostics.empty());
    CHECK_EQ(result.functions[0].name, std::string("initialize"));
    bool dynamic = false, local = false, global = false;
    for (const auto& binding : result.bindings) {
        if (binding.name == "event") dynamic = binding.expression.kind == "dynamic";
        if (binding.name == "scene") local = binding.local && binding.scope == "initialize";
        if (binding.name == "marker" && binding.scope == "initialize") global = !binding.local;
    }
    CHECK(dynamic);
    CHECK(local);
    CHECK(global);
    auto compilerMetadata = eve::script::ScriptCompiler::analyze("function retained() { target.run(); }", "unit.nut");
    CHECK_EQ(compilerMetadata.structure.calls.size(), size_t(1));
}
TEST_CASE("script.structure.rejectsMalformedAndExcessiveNesting") {
    CHECK(!eve::script::analyzeScriptStructure("function bad() { emit(\"x\");").diagnostics.empty());
    CHECK(!eve::script::analyzeScriptStructure("/* unfinished").diagnostics.empty());
    CHECK(!eve::script::analyzeScriptStructure(std::string(65, '(') + "x" + std::string(65, ')')).diagnostics.empty());
}
TEST_CASE("script.structure.reflectsFieldsWithoutLeakingClassMethodsOrMembers") {
    auto result = eve::script::analyzeScriptStructure(R"SQ(
        class NPC {
            health: int = 100;
            marker = null;
            function update() { local marker = unknown(); marker.setVisible(true); }
        }
        function update() { return 1; }
    )SQ");
    REQUIRE(result.diagnostics.empty());
    REQUIRE_EQ(result.classes.size(), size_t(1));
    CHECK_EQ(result.functions[0].name, std::string("NPC.update"));
    CHECK_EQ(result.functions[1].name, std::string("update"));
    bool field = false;
    for (const auto& binding : result.bindings)
        if (binding.name == "health") {
            CHECK(binding.local);
            CHECK_EQ(binding.scope, std::string("NPC."));
            CHECK_EQ(binding.declaredType, std::string("int"));
            field = true;
        }
    CHECK(field);
    CHECK_EQ(result.bindings.size(), size_t(3));
}
TEST_CASE("script.structure.parameterDefaultsDoNotDeclareGlobalAliases") {
    auto result = eve::script::analyzeScriptStructure(
        "function callback(marker: SceneNodeRef = null, text = \"x\") { marker.setVisible(true); }");
    REQUIRE(result.diagnostics.empty());
    CHECK(result.bindings.empty());
    CHECK_EQ(result.functions[0].parameters.size(), size_t(2));
}
