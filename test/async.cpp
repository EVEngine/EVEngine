#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "event/Event.h"
#include "scripts.h"
#include "thread/Thread.h"
#include "timer/Timer.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <memory>
#include <string>

namespace {

void exposeCore(ssq::VM &vm) {
    eve::ModuleManager::expose(vm);
}

std::string runAsyncSnippet(const std::string &body) {
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
        event <- eve.Event();
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

TEST_CASE("async.promise.resolveThen") {
    std::string out = runAsyncSnippet(R"(
        result <- "fail";
        local p = Promise.resolve(42);
        p.then(function(v) { result = "" + v; });
        async_pump();
        async_pump();
    )");
    CHECK_EQ(out, std::string("42"));
}

TEST_CASE("async.setTimeout") {
    std::string out = runAsyncSnippet(R"(
        result <- "pending";
        setTimeout(function() { result = "fired"; }, 5);
        local t0 = timer.getTime();
        while (timer.getTime() - t0 < 0.05) {
            // busy wait wall time; timer uses SDL performance counter
        }
        // Advance timer.step so getTime keeps moving; fire timers.
        for (local i = 0; i < 5; i++) {
            timer.step();
            async_pump();
        }
        // If still pending, force due by pumping after sleep via thread
        if (result == "pending") {
            local pool = thread.getPool();
            local task = pool.submitSleep(20);
            task.wait();
            timer.step();
            async_pump();
            async_pump();
        }
    )");
    CHECK_EQ(out, std::string("fired"));
}

TEST_CASE("async.asyncSleep") {
    std::string out = runAsyncSnippet(R"(
        result <- "pending";
        asyncSleep(1).then(function(v) { result = "ok"; });
        local pool = thread.getPool();
        local task = pool.submitSleep(15);
        task.wait();
        for (local i = 0; i < 8; i++) {
            timer.step();
            async_pump();
        }
    )");
    CHECK_EQ(out, std::string("ok"));
}

TEST_CASE("async.asyncDelay.worker") {
    std::string out = runAsyncSnippet(R"(
        result <- "pending";
        asyncDelay(5, "hello").then(function(v) { result = v; });
        local pool = thread.getPool();
        local task = pool.submitSleep(30);
        task.wait();
        for (local i = 0; i < 10; i++) {
            async_pump();
        }
    )");
    CHECK_EQ(out, std::string("hello"));
}

TEST_CASE("async.postMain.event") {
    auto *th = eve::thread::Thread::create();
    auto *ev = eve::event::Event::create();
    th->postMain("asynctest", "payload");
    CHECK_EQ(ev->pollName(), std::string("asynctest"));
    CHECK_EQ(ev->getLastData(), std::string("payload"));
}

TEST_CASE("async.Promise.all") {
    std::string out = runAsyncSnippet(R"(
        result <- "fail";
        Promise.all([Promise.resolve(1), Promise.resolve(2)]).then(function(arr) {
            result = "" + arr[0] + "," + arr[1];
        });
        async_pump();
        async_pump();
        async_pump();
    )");
    CHECK_EQ(out, std::string("1,2"));
}

TEST_CASE("async.asyncSeq") {
    std::string out = runAsyncSnippet(R"(
        result <- "fail";
        asyncSeq([
            function() { return Promise.resolve(1); },
            function(v) { return Promise.resolve(v + 2); },
            function(v) { return v * 10; },
        ]).then(function(v) { result = "" + v; });
        async_pump();
        async_pump();
        async_pump();
        async_pump();
        async_pump();
    )");
    CHECK_EQ(out, std::string("30"));
}
