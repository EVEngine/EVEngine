#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "orders/CommandQueue.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::orders;

namespace {
[[maybe_unused]] std::string takeId(eve::Result<std::string> result) {
    REQUIRE(result.hasValue());
    return std::move(result).takeValue();
}
}  // namespace


TEST_CASE("orders.script.queueAndPayloadApi") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Orders();
        local queueResult = module.newQueueOwned();
        local queue = queueResult.ok ? queueResult.value : null;
        local idResult = queue == null ? { ok=false } : queue.append("custom_action", 7, 1.0);
        local id = idResult.ok ? idResult.value : "";
        local order = queue.current();
        local payloadResult = order.getPayload().setJson("context", "{\"zone\":3}");
        if (queueResult.ok && idResult.ok && payloadResult.ok && order.getId() == id && order.getKind() == "custom_action" &&
            order.getPayload().getJson("context") == "{\"zone\":3}") {
            local completed = queue.complete(id);
            if (completed.ok) result = order.getState();
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("completed"));
}

TEST_CASE("orders.script.ownedQueueReplace") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Orders();
        local queueResult = module.newQueueOwned();
        if (!queueResult.ok) throw queueResult.status.summary;
        local queue = queueResult.value;
        local first = queue.append("move", 1, 0.0);
        if (!first.ok) throw first.status.summary;
        local replaced = queue.replace("move", 10, 0.0);
        if (!replaced.ok) throw replaced.status.summary;
        local order = queue.current();
        if (order != null && order.getId() == replaced.value && order.getKind() == "move" &&
            order.getState() == "active") {
            local payloadResult = order.getPayload().setJson("x", "400");
            if (payloadResult.ok && order.getPayload().has("x"))
                result = "replaced";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("replaced"));
}
