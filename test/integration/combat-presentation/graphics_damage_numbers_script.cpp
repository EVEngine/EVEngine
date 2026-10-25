// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "graphics",
  "default": "script-binding"
}
*/
// clang-format on
#include "common/SquirrelOwnership.h"
#include "graphics/DamageNumbers.h"
#include "graphics/DamageNumbersBindings.h"
#include "graphics/Graphics.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <functional>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("damageNumbersScript.numericArgumentsAndStyleFields") {
    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    table    = vm.addTable("eve");
    auto    graphics = table.addClass<eve::graphics::Graphics>(
        "Graphics", std::function<eve::graphics::Graphics*()>([] { return nullptr; }), false);
    eve::graphics::exposeFontAndDamageNumberBindings(table, graphics);
    auto instance =
        eve::script::makeOwnedSquirrelInstance(vm.getHandle(), std::make_unique<eve::graphics::DamageNumbers>());
    REQUIRE(instance.ok());
    vm.set("numbers", std::move(instance).takeValue());
    vm.run(vm.compileSource(R"(
        local style = eve.DamageNumberStyle();
        style.lifetime = 2.0;
        style.fadeIn = 0.1;
        style.fadeOut = 0.4;
        style.mergeWindow = 0.2;
        configured <- numbers.setStyle("critical", style);
        spawned <- numbers.spawn(12.0, 100.0, 200.0, "critical", "enemy", 4.0);
        advanced <- numbers.update(0.25);
        captured <- numbers.snapshot();
        sampleText <- captured.value[0].text;
        badKind <- numbers.spawn(1.0, 0.0, 0.0, "typo", "", 0.0);
        badTime <- numbers.update(-1.0);
        count <- numbers.count();
        numbers.clear();
        cleared <- numbers.count();
    )"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK(vm.find("spawned").toTable().get<bool>("ok"));
    CHECK(vm.find("advanced").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("sampleText").toString(), std::string("-12!"));
    CHECK(!vm.find("badKind").toTable().get<bool>("ok"));
    CHECK(!vm.find("badTime").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("count").toInt(), 1);
    CHECK_EQ(vm.find("cleared").toInt(), 0);
}
