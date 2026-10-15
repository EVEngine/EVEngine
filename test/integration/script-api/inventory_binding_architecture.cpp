#include <simplesquirrel/vm.hpp>
#include "common/Module.h"
#include "inventory/Bag.h"
#include "inventory/Inventory.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("inventory.script.architectureExtraState") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"SQ(
        architectureBag <- eve.Inventory().newBag(2)
        architectureBag.setExtra("quest", "ready")
        architectureValue <- architectureBag.getExtra("quest", "missing")
        architectureFallback <- architectureBag.getExtra("unknown", "missing")
    )SQ"));
    CHECK_EQ(vm.get<std::string>("architectureValue"), std::string("ready"));
    CHECK_EQ(vm.get<std::string>("architectureFallback"), std::string("missing"));
    auto* bag = vm.get<eve::inventory::Bag*>("architectureBag");
    REQUIRE(bag != nullptr);
    CHECK_EQ(bag->getExtra("quest", "missing"), std::string("ready"));
}
