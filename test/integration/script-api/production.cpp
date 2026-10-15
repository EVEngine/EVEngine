#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "production/Production.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <limits>

using namespace eve::production;


TEST_CASE("production.script.queueLifecycle") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Production();
        local queueResult = module.newWorkQueue();
        local queue = queueResult.ok ? queueResult.value : null;
        local slots = queue == null ? { ok=false } : queue.setSlotCount("factory", 2);
        local idResult = queue == null ? { ok=false } : queue.enqueue(
            "factory", "vehicle", "tank", "{\"armor\":100}", 2.0, 5);
        local id = idResult.ok ? idResult.value : "";
        local advanced = queue == null ? { ok=false } : queue.advance(1, 2.0);
        local task = queue == null ? null : queue.find(id);
        local savedResult = queue == null ? { ok=false } : queue.snapshot();
        local saved = savedResult.ok ? savedResult.value : "";
        local context = task == null ? {} : task.getContext();
        if (queueResult.ok && slots.ok && idResult.ok && advanced.ok && savedResult.ok && task != null &&
            task.getState() == "completed" && context.armor == 100 &&
            queue.eventAt(queue.eventCount() - 1).getKind() == "completed" && queue.restore(saved).ok) result = "ok";
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
