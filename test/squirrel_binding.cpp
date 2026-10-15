#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/ScriptError.h"
#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <utility>

using eve::Diagnostic;
using eve::DiagnosticCode;
using eve::Result;
using eve::Value;

TEST_CASE("squirrel_binding.value_round_trip_preserves_nested_value") {
    ssq::VM      vm(1024, ssq::Libs::ALL);
    Value::Array items;
    items.emplace_back(Value::Object{{"enabled", true}, {"label", "nested"}});
    items.emplace_back(std::int64_t{9});
    const Value original(Value::Object{
        {"name", "round-trip"},
        {"items", Value(std::move(items))},
        {"ratio", 1.25},
    });

    auto pushed = eve::script::pushValue(vm.getHandle(), original);
    REQUIRE(pushed.ok());
    auto restored = eve::script::valueFromSquirrel(vm.getHandle(), -1);
    REQUIRE(restored.ok());
    CHECK(restored.value() == original);
    sq_pop(vm.getHandle(), 1);
}

TEST_CASE("squirrel_binding.value_rejects_non_finite_numbers_with_path_and_source") {
    ssq::VM    vm(1024, ssq::Libs::ALL);
    const auto top = sq_gettop(vm.getHandle());
    sq_newarray(vm.getHandle(), 0);
    sq_pushfloat(vm.getHandle(), std::numeric_limits<SQFloat>::infinity());
    REQUIRE(SQ_SUCCEEDED(sq_arrayappend(vm.getHandle(), -2)));

    auto result = eve::script::valueFromSquirrel(vm.getHandle(), -1);
    CHECK(!result.ok());
    REQUIRE(result.error() != nullptr);
    CHECK(static_cast<int>(result.error()->code()) == static_cast<int>(DiagnosticCode::InvalidArgument));
    CHECK(result.error()->path() == "$[0]");
    CHECK(result.error()->source() == "squirrel.binding");
    sq_settop(vm.getHandle(), top);
}

TEST_CASE("squirrel_binding.value_enforces_depth_limit") {
    ssq::VM    vm(1024, ssq::Libs::ALL);
    const auto top = sq_gettop(vm.getHandle());
    sq_newarray(vm.getHandle(), 0);
    sq_newarray(vm.getHandle(), 0);
    REQUIRE(SQ_SUCCEEDED(sq_arrayappend(vm.getHandle(), -2)));

    eve::script::SquirrelValueOptions options;
    options.maxDepth = 1;
    auto result      = eve::script::valueFromSquirrel(vm.getHandle(), -1, options);
    CHECK(!result.ok());
    REQUIRE(result.error() != nullptr);
    CHECK(result.error()->path() == "$[0]");
    sq_settop(vm.getHandle(), top);
}

TEST_CASE("squirrel_binding.result_projection_and_explicit_ignore_are_uniform") {
    ssq::VM    vm(1024, ssq::Libs::ALL);
    ssq::Table eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);

    auto projected =
        eve::script::projectResult(vm.getHandle(),
                                   Result<int>::failure(Diagnostic::error(DiagnosticCode::NotFound, "missing value",
                                                                          "payload.name", {}, "test.source")),
                                   [](int value) { return Value(static_cast<std::int64_t>(value)); });
    CHECK(!projected.find("ok").toBool());
    CHECK(projected.find("checked").toBool());
    CHECK(projected.find("hasValue").toBool() == false);
    auto diagnostics = projected.find("diagnostics").toArray();
    REQUIRE(diagnostics.size() == 1u);
    const auto diagnostic = diagnostics.get<ssq::Table>(0);
    CHECK(diagnostic.find("path").toString() == "payload.name");
    CHECK(diagnostic.find("source").toString() == "test.source");

    eve.set("target", projected);
    vm.run(vm.compileSource("ignored <- eve.result.ignore(eve.target, \"optional feature\");\n",
                            "result-ignore-test.nut"));
    CHECK(vm.find("ignored").toBool());
    CHECK(projected.find("ignored").toBool());
    CHECK(projected.find("ignoreReason").toString() == "optional feature");
    CHECK(eve::script::ignoreResult(projected, "second explicit observation"));
}

// ---------------------------------------------------------------------------
// Argument marshalling of the engine -> script call helpers (R-FACE-7)
// ---------------------------------------------------------------------------
//
// `callFunction` / `callFunctionResult` are the mandated way to call script from native
// code, so what they do with an argument is part of the contract. Two marshalling
// mistakes were live here: a string literal did not compile at all (simplesquirrel's
// pushByCopy cannot take `const char[N]`), and a `const char*` arrived in script as
// *userdata*, because ssq has no `const char*` pushValue overload.

namespace {

const char* kCallArgumentScript = R"SQ(
function argKind(v) { return typeof v }
function argText(v) { return "" + v }
function twoArgs(a, b) { return "" + a + "|" + b }
function boom() { throw "boom" }
)SQ";

void runCallArgumentScript(ssq::VM& vm) {
    eve::script::installScriptErrorHook(vm.getHandle());
    vm.run(vm.compileSource(kCallArgumentScript, "call-argument-test.nut"));
}

std::string topString(HSQUIRRELVM vm) {
    const SQChar* out = nullptr;
    if (sq_gettype(vm, -1) != OT_STRING) return {};
    if (SQ_FAILED(sq_getstring(vm, -1, &out))) return {};
    return out ? std::string(out) : std::string();
}

}  // namespace

TEST_CASE("squirrel_binding.call_argument_c_string_literal_arrives_as_string") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    runCallArgumentScript(vm);
    const SQInteger top = sq_gettop(vm.getHandle());
    ssq::Object     fn  = vm.findFunc("argText");

    REQUIRE(
        SQ_SUCCEEDED(eve::script::callFunctionResult(vm.getHandle(), fn.getRaw(), eve::script::rootTable, "literal")));
    REQUIRE(topString(vm.getHandle()) == "literal");
    sq_settop(vm.getHandle(), top);
}

TEST_CASE("squirrel_binding.call_argument_c_string_pointer_arrives_as_string") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    runCallArgumentScript(vm);
    const SQInteger top  = sq_gettop(vm.getHandle());
    ssq::Object     fn   = vm.findFunc("argKind");
    const char*     text = "dynamic";

    // Measured before the fix: `typeof v` was "userdata".
    REQUIRE(SQ_SUCCEEDED(eve::script::callFunctionResult(vm.getHandle(), fn.getRaw(), eve::script::rootTable, text)));
    REQUIRE(topString(vm.getHandle()) == "string");
    sq_settop(vm.getHandle(), top);
}

TEST_CASE("squirrel_binding.call_argument_std_strings_scalars_and_stack_discipline") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    runCallArgumentScript(vm);
    const SQInteger top = sq_gettop(vm.getHandle());

    ssq::Object       fn    = vm.findFunc("twoArgs");
    const std::string owned = "owned";
    REQUIRE(
        SQ_SUCCEEDED(eve::script::callFunctionResult(vm.getHandle(), fn.getRaw(), eve::script::rootTable, owned, 7)));
    REQUIRE(topString(vm.getHandle()) == "owned|7");
    sq_settop(vm.getHandle(), top);

    // callFunction() restores the stack it was given, on success and on a throwing callee.
    fn = vm.findFunc("argKind");
    REQUIRE(SQ_SUCCEEDED(eve::script::callFunction(vm.getHandle(), fn.getRaw(), eve::script::rootTable, "x")));
    REQUIRE(sq_gettop(vm.getHandle()) == top);

    fn               = vm.findFunc("boom");
    const int before = eve::script::reportedScriptCallFailures();
    REQUIRE(SQ_FAILED(eve::script::callFunction(vm.getHandle(), fn.getRaw(), eve::script::rootTable)));
    REQUIRE(sq_gettop(vm.getHandle()) == top);
    CHECK(eve::script::reportedScriptCallFailures() == before + 1);
}
