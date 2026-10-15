#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "StatePatchTestSupport.h"
#include "common/Module.h"
#include "statepatch/StatePatch.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::statepatch;

namespace {}  // namespace


TEST_CASE("statepatch.script.batchConflictQueryAndSnapshot") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.StatePatch();
        local storeResult = module.newStore();
        local store = storeResult.ok ? storeResult.value : null;
        local firstResult = store != null ? store.newBatch() : { ok = false };
        local first = firstResult.ok ? firstResult.value : null;
        if (store != null && first != null && first.set("actor-1", "rank", "3").ok &&
            first.set("actor-1", "loyalty", "0.75").ok && store.commit(first).ok) {
            local conflictResult = store.newBatch();
            local conflict = conflictResult.ok ? conflictResult.value : null;
            if (conflict != null && conflict.set("actor-2", "rank", "1").ok &&
                conflict.setExpected("actor-1", "rank", "4", "2").ok &&
                !store.commit(conflict).ok && conflict.result().errorAt(0).getCode() == "conflict" &&
                !store.has("actor-2", "rank")) {
                local copyResult = module.newStore();
                local copy = copyResult.ok ? copyResult.value : null;
                local restoredResult = copy != null ? copy.restoreJson(store.snapshotJson()) : { ok = false };
                if (copy != null && restoredResult.ok && copy.queryKeys("actor-1") == 2)
                    result = copy.get("actor-1", "rank");
            }
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("3"));
}
