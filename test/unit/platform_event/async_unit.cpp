// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "async.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "platform_event/PlatformEvent.h"
#include "scripts.h"
#include "thread/Thread.h"
#include "timer/Timer.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <memory>
#include <string>

namespace {

[[maybe_unused]] void exposeCore(ssq::VM& vm) { eve::ModuleManager::expose(vm); }

[[maybe_unused]] std::string runAsyncSnippet(const std::string& body) {
    ssq::VM vm(1024, ssq::Libs::ALL);
    exposeCore(vm);
    {
        ssq::Table eve(vm.find("eve"));
        eve.set("asyncScript", std::string(eve::async_content ? eve::async_content : ""));
    }

    // Minimal globals expected by async.nut / load prelude.
    std::string prelude = R"(
        timer <- eve.Timer();
        thread <- eve.Thread();
        event <- eve.PlatformEvent();
        if ("asyncScript" in eve && eve.asyncScript != "")
            compilestring(eve.asyncScript)();
    )";

    vm.run(vm.compileSource((prelude + "\n" + body).c_str()));
    // Return a global `result` string if set.
    try {
        return vm.find("result").toString();
    } catch (...) {
        return {};
    }
}

}  // namespace


TEST_CASE("async.postMain.event") {
    auto* th = eve::thread::Thread::create();
    auto* ev = eve::platform_event::PlatformEvent::create();
    th->postMain("asynctest", "payload");
    CHECK_EQ(ev->pollName(), std::string("asynctest"));
    CHECK_EQ(ev->getLastData(), std::string("payload"));
}
