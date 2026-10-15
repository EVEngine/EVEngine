#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "definitions/Definitions.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>
#include <string>

using namespace eve::definitions;


TEST_CASE("definitions.scriptRegisterReplaceEnumerateAndRestore") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Definitions();
        local registry = module.newRegistry();
        local inserted = registry.insert("general", "g-1", 1, "{\"rank\":2}");
        local replaced = registry.replace("general", "g-1", 2, "{\"rank\":3}");
        if (inserted.ok && replaced.ok) {
            local snapshot = registry.snapshotJson();
            local copy = module.newRegistry();
            local restored = copy.restoreJson(snapshot);
            local resolved = copy.resolve("general", "g-1");
            local resolvedHandle = copy.resolveHandle("general", "g-1", 2);
            if (restored.ok && copy.countType("general") == 1 && resolved.ok &&
                resolved.value.version == 2 && resolvedHandle.ok &&
                resolvedHandle.value.version == 2 && resolvedHandle.value.generation == 2 &&
                copy.eventAt(1).getName() == "definition_reloaded")
                result = copy.atType("general", 0).getJson();
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("{\"rank\":3}"));
}
