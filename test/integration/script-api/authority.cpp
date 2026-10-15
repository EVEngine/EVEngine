#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "authority/Authority.h"
#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::authority;


TEST_CASE("authority.script.explainQueryAndLifecycle") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Authority();
        local storeResult = module.newStore();
        local store = storeResult.ok ? storeResult.value : null;
        local grantResult = store != null ? store.grant("actor:7", "scope:2", "operate", "role:3", 5, 0.0) : { ok = false };
        local denyResult = store != null ? store.deny("actor:7", "scope:2", "operate", "policy:9", 5, 1.0) : { ok = false };
        local grantId = grantResult.ok ? grantResult.value : "";
        local denyId = denyResult.ok ? denyResult.value : "";
        local before = store != null ? store.explain("actor:7", "scope:2", "operate") : null;
        if (grantResult.ok && denyResult.ok && before != null) {
            store.update(1.0);
        }
        local after = store != null ? store.explain("actor:7", "scope:2", "operate") : null;
        if (grantResult.ok && denyResult.ok && before != null && after != null &&
            !before.isAllowed() && before.getWinningRuleId() == denyId &&
            after.isAllowed() && after.getWinningRuleId() == grantId &&
            store.queryActor("actor:7") == 1 && store.eventAt(2).getKind() == "expired") {
            result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
