#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "ui/UI.h"

#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("UI.tween.scriptOptionalEaseAndDelay") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    // Short arities must bind: historical 3-arg host calls and documented
    // ease-without-delay. A throw here means Squirrel rejected the arity.
    vm.run(vm.compileSource(R"(
        ui <- eve.UI();
        ui.beginBuild();
        ui.beginWindow("Panel", "root");
        ui.button("Go", "go");
        ui.end();
        ui.mountBuildAs("script-tween");
        ui.animateHostPos(10.0, 20.0, 0.0);
        ui.animateHostPos(30.0, 40.0, 0.0, "linear");
        ui.animateHostSize(100.0, 50.0, 0.0, "outCubic");
        ui.animateHostOverlayAlpha(0.5, 0.0);
        ui.animateItemOpacity("go", 0.25, 0.0, "linear");
        ui.animateItemPos("go", 5.0, 6.0, 0.0);
        ui.cancelItemTweens();
        hostCount <- ui.getHostTweenCount();
        itemCount <- ui.getItemTweenCount();
    )"));

    CHECK_EQ(vm.find("hostCount").toInt(), 0);
    CHECK_EQ(vm.find("itemCount").toInt(), 0);
}
