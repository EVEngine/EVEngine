#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "policyregistry/PolicyRegistry.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace eve::policyregistry;


TEST_CASE("policyregistry.scriptDiscoversButDoesNotStoreCallbacks") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.PolicyRegistryModule();
        local registry = module.newRegistry();
        local callback = function(subject) { return subject + ":handled"; };
        local inserted = registry.insert("administration", "governor", 1, 8, true, "script",
                                         "governor.policy", "{\"owner\":\"game-script\"}");
        if (inserted.ok) {
            local generation = inserted.value.generation;
            local resolved = registry.resolve("administration", "governor");
            local snapshot = registry.snapshotJson();
            local copy = module.newRegistry();
            local restored = copy.restoreJson(snapshot);
            local resolvedCopy = copy.resolveHandle("administration", "governor", generation);
            if (resolved.ok && restored.ok && copy.select("administration").getName() == "governor" &&
                resolvedCopy.ok && resolvedCopy.value.kind == "script")
                result = callback("general-7");
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("general-7:handled"));
}
