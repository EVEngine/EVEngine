#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "platform_event/PlatformEvent.h"
#include "rx/Rx.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <memory>
#include <string>
#include <vector>

using eve::rx::AnonymousObservable;
using eve::rx::BehaviorSubject;
using eve::rx::Observer;
using eve::rx::Observable;
using eve::rx::ReactiveProperty;
using eve::rx::ReplaySubject;
using eve::rx::Subject;
using eve::rx::Subscription;
using eve::rx::Value;

// ---------------------------------------------------------------------------
// Value basics
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Subject core
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// BehaviorSubject / ReplaySubject / ReactiveProperty
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// Script binding (Squirrel)
// ---------------------------------------------------------------------------
namespace {

[[maybe_unused]] std::string runRxSnippet(const std::string& body) {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    std::string prelude = R"(
        rx <- eve.Rx();
    )";
    vm.run(vm.compileSource((prelude + "\n" + body).c_str()));
    try {
        return vm.find("result").toString();
    } catch (...) {
        return {};
    }
}

}  // namespace

TEST_CASE("rx.script.subject.onNext") {
    std::string out = runRxSnippet(R"(
        result <- "none";
        local s = rx.newSubject();
        s.subscribe(function(v) { result = "got:" + v; });
        s.onNext(42);
    )");
    CHECK_EQ(out, std::string("got:42"));
}

TEST_CASE("rx.script.subject.operators") {
    std::string out = runRxSnippet(R"(
        result <- "none";
        local s = rx.newSubject();
        local even = s.filter(function(v) { return v % 2 == 0; });
        even.subscribe(function(v) { result = "even:" + v; });
        s.onNext(1);
        s.onNext(2);
        s.onNext(4);
    )");
    CHECK_EQ(out, std::string("even:4"));
}

TEST_CASE("rx.script.map") {
    std::string out = runRxSnippet(R"(
        result <- "none";
        local s = rx.newSubject();
        local doubled = s.map(function(v) { return v * 2; });
        doubled.subscribe(function(v) { result = "d:" + v; });
        s.onNext(21);
    )");
    CHECK_EQ(out, std::string("d:42"));
}

TEST_CASE("rx.script.behaviorSubject") {
    std::string out = runRxSnippet(R"(
        result <- "none";
        local bs = rx.newBehaviorSubject(5);
        bs.subscribe(function(v) { result = "v:" + v; });
        bs.setValue(9);
    )");
    CHECK_EQ(out, std::string("v:9"));
}

TEST_CASE("rx.script.take") {
    std::string out = runRxSnippet(R"(
        result <- "";
        local s = rx.newSubject();
        s.take(2).subscribe(function(v) { result += v; });
        s.onNext(1);
        s.onNext(2);
        s.onNext(3);
    )");
    CHECK_EQ(out, std::string("12"));
}

TEST_CASE("rx.script.replaySubject") {
    std::string out = runRxSnippet(R"(
        result <- "";
        local rs = rx.newReplaySubject(2);
        rs.onNext(1);
        rs.onNext(2);
        rs.onNext(3);
        rs.subscribe(function(v) { result += v; });
    )");
    CHECK_EQ(out, std::string("23"));
}

TEST_CASE("rx.script.fromEvent.pump") {
    std::string out = runRxSnippet(R"(
        result <- "";
        local ev = eve.PlatformEvent();
        local ev2 = eve.PlatformEvent();
        local stream = rx.fromEvent("quest");
        stream.subscribe(function(v) { result += v; });
        ev.pushData("quest", "A");
        ev.pushData("other", "X");
        ev2.pushData("quest", "B");
        rx.pump(ev);
        rx.pump(ev2);
    )");
    CHECK_EQ(out, std::string("AB"));
}
