#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
    ssq::VM vm(1024, ssq::Libs::ALL);
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
    ssq::VM vm(1024, ssq::Libs::ALL);
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
    ssq::VM vm(1024, ssq::Libs::ALL);
    const auto top = sq_gettop(vm.getHandle());
    sq_newarray(vm.getHandle(), 0);
    sq_newarray(vm.getHandle(), 0);
    REQUIRE(SQ_SUCCEEDED(sq_arrayappend(vm.getHandle(), -2)));

    eve::script::SquirrelValueOptions options;
    options.maxDepth = 1;
    auto result = eve::script::valueFromSquirrel(vm.getHandle(), -1, options);
    CHECK(!result.ok());
    REQUIRE(result.error() != nullptr);
    CHECK(result.error()->path() == "$[0]");
    sq_settop(vm.getHandle(), top);
}

TEST_CASE("squirrel_binding.result_projection_and_explicit_ignore_are_uniform") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    ssq::Table eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);

    auto projected = eve::script::projectResult(
        vm.getHandle(),
        Result<int>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "missing value", "payload.name", {}, "test.source")),
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
    vm.run(vm.compileSource(
        "ignored <- eve.result.ignore(eve.target, \"optional feature\");\n",
        "result-ignore-test.nut"));
    CHECK(vm.find("ignored").toBool());
    CHECK(projected.find("ignored").toBool());
    CHECK(projected.find("ignoreReason").toString() == "optional feature");
    CHECK(eve::script::ignoreResult(projected, "second explicit observation"));
}
