#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "event/Event.h"
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
TEST_CASE("rx.Value.intRoundTrip") {
    Value v = Value::makeInt(42);
    CHECK(v.isInt());
    CHECK_EQ(v.toInt(), int64_t(42));
    CHECK(v.toString() == "42");
    CHECK(v.toBool());
    CHECK(!v.isNil());
}

TEST_CASE("rx.Value.floatBoolString") {
    Value f = Value::makeFloat(3.5);
    CHECK(f.isFloat());
    CHECK(f.toFloat() == 3.5);
    CHECK(f.toInt() == 3);

    Value b = Value::makeBool(true);
    CHECK(b.isBool());
    CHECK(b.toBool());

    Value s = Value::makeString("hello");
    CHECK(s.isString());
    CHECK(s.toString() == "hello");
    CHECK(s.toInt() == 0);
}

TEST_CASE("rx.Value.nilAndEquals") {
    Value n = Value::makeNil();
    CHECK(n.isNil());
    CHECK(n.equals(Value::makeNil()));

    Value a = Value::makeInt(1);
    Value b = Value::makeInt(1);
    Value c = Value::makeInt(2);
    CHECK(a.equals(b));
    CHECK(!a.equals(c));
    CHECK(!a.equals(Value::makeFloat(1.0)));  // type matters
}

// ---------------------------------------------------------------------------
// Subject core
// ---------------------------------------------------------------------------
TEST_CASE("rx.Subject.deliversOnNext") {
    Subject<int> subj;
    int received = 0;
    auto sub = subj.subscribe([&](const int& v) { received += v; });
    subj.onNext(1);
    subj.onNext(2);
    subj.onNext(3);
    CHECK_EQ(received, 6);
    sub.dispose();
}

TEST_CASE("rx.Subject.observerCount") {
    Subject<int> subj;
    CHECK_EQ(subj.observerCount(), 0);
    auto a = subj.subscribe([](const int&) {});
    auto b = subj.subscribe([](const int&) {});
    CHECK_EQ(subj.observerCount(), 2);
    a.dispose();
    CHECK_EQ(subj.observerCount(), 1);
    b.dispose();
    CHECK_EQ(subj.observerCount(), 0);
}

TEST_CASE("rx.Subject.disposeStopsDelivery") {
    Subject<int> subj;
    int received = 0;
    auto sub = subj.subscribe([&](const int& v) { received += v; });
    subj.onNext(5);
    sub.dispose();
    subj.onNext(10);
    CHECK_EQ(received, 5);
}

TEST_CASE("rx.Subject.onErrorOnCompleted") {
    Subject<int> subj;
    bool errored = false;
    bool completed = false;
    std::string err;
    auto sub = subj.subscribe([](const int&) {}, [&](const std::string& e) {
        errored = true;
        err = e;
    }, [&]() { completed = true; });

    subj.onError("boom");
    CHECK(errored);
    CHECK_EQ(err, std::string("boom"));
    // After error, onNext is suppressed.
    subj.onNext(1);
    CHECK(!completed);
    sub.dispose();
}

TEST_CASE("rx.Subject.onCompletedStopsDelivery") {
    Subject<int> subj;
    int received = 0;
    bool completed = false;
    auto sub = subj.subscribe([&](const int& v) { received += v; }, nullptr,
                              [&]() { completed = true; });
    subj.onNext(1);
    subj.onCompleted();
    subj.onNext(2);
    CHECK_EQ(received, 1);
    CHECK(completed);
    sub.dispose();
}

// ---------------------------------------------------------------------------
// Operators
// ---------------------------------------------------------------------------
TEST_CASE("rx.operators.map") {
    Subject<int> subj;
    int received = 0;
    auto mapped = subj.map<std::string>([](const int& v) { return std::to_string(v * 2); });
    auto sub = mapped->subscribe([&](const std::string& s) { received += std::stoi(s); });
    subj.onNext(3);
    subj.onNext(4);
    CHECK_EQ(received, 14);
    sub.dispose();
    delete mapped;
}

TEST_CASE("rx.operators.filter") {
    Subject<int> subj;
    int received = 0;
    auto filtered = subj.filter([](const int& v) { return v % 2 == 0; });
    auto sub = filtered->subscribe([&](const int& v) { received += v; });
    subj.onNext(1);
    subj.onNext(2);
    subj.onNext(3);
    subj.onNext(4);
    CHECK_EQ(received, 6);
    sub.dispose();
    delete filtered;
}

TEST_CASE("rx.operators.take") {
    Subject<int> subj;
    std::vector<int> received;
    bool completed = false;
    auto taken = subj.take(2);
    auto sub = taken->subscribe([&](const int& v) { received.push_back(v); }, nullptr,
                                [&]() { completed = true; });
    subj.onNext(1);
    subj.onNext(2);
    subj.onNext(3);
    CHECK_EQ(received.size(), size_t(2));
    CHECK_EQ(received[0], 1);
    CHECK_EQ(received[1], 2);
    CHECK(completed);
    sub.dispose();
    delete taken;
}

TEST_CASE("rx.operators.skip") {
    Subject<int> subj;
    std::vector<int> received;
    auto skipped = subj.skip(2);
    auto sub = skipped->subscribe([&](const int& v) { received.push_back(v); });
    subj.onNext(1);
    subj.onNext(2);
    subj.onNext(3);
    subj.onNext(4);
    CHECK_EQ(received.size(), size_t(2));
    CHECK_EQ(received[0], 3);
    CHECK_EQ(received[1], 4);
    sub.dispose();
    delete skipped;
}

TEST_CASE("rx.operators.first") {
    Subject<int> subj;
    std::vector<int> received;
    bool completed = false;
    auto first = subj.first();
    auto sub = first->subscribe([&](const int& v) { received.push_back(v); }, nullptr,
                                [&]() { completed = true; });
    subj.onNext(10);
    subj.onNext(20);
    CHECK_EQ(received.size(), size_t(1));
    CHECK_EQ(received[0], 10);
    CHECK(completed);
    sub.dispose();
    delete first;
}

TEST_CASE("rx.operators.distinctUntilChanged") {
    Subject<int> subj;
    std::vector<int> received;
    auto d = subj.distinctUntilChanged();
    auto sub = d->subscribe([&](const int& v) { received.push_back(v); });
    subj.onNext(1);
    subj.onNext(1);
    subj.onNext(2);
    subj.onNext(2);
    subj.onNext(1);
    CHECK_EQ(received.size(), size_t(3));
    CHECK_EQ(received[0], 1);
    CHECK_EQ(received[1], 2);
    CHECK_EQ(received[2], 1);
    sub.dispose();
    delete d;
}

TEST_CASE("rx.operators.takeUntil") {
    Subject<int> source;
    Subject<int> stopper;
    int received = 0;
    auto taken = source.takeUntil(&stopper);
    auto sub = taken->subscribe([&](const int& v) { received += v; });
    source.onNext(1);
    stopper.onNext(0);
    source.onNext(10);
    CHECK_EQ(received, 1);
    sub.dispose();
    delete taken;
}

// ---------------------------------------------------------------------------
// BehaviorSubject / ReplaySubject / ReactiveProperty
// ---------------------------------------------------------------------------
TEST_CASE("rx.BehaviorSubject.replaysLatestOnSubscribe") {
    BehaviorSubject<int> bs(100);
    int received = 0;
    bs.onNext(200);
    auto sub = bs.subscribe([&](const int& v) { received = v; });
    CHECK_EQ(received, 200);  // latest replayed
    CHECK_EQ(bs.getValue(), 200);
    bs.setValue(300);
    CHECK_EQ(received, 300);
    sub.dispose();
}

TEST_CASE("rx.ReplaySubject.replaysBuffer") {
    ReplaySubject<int> rs(3);
    rs.onNext(1);
    rs.onNext(2);
    rs.onNext(3);
    rs.onNext(4);
    std::vector<int> received;
    auto sub = rs.subscribe([&](const int& v) { received.push_back(v); });
    CHECK_EQ(received.size(), size_t(3));  // capacity 3
    CHECK_EQ(received[0], 2);
    CHECK_EQ(received[1], 3);
    CHECK_EQ(received[2], 4);
    sub.dispose();
}

TEST_CASE("rx.ReactiveProperty.getSetNotify") {
    ReactiveProperty<int> prop(0);
    int received = 0;
    auto sub = prop.subscribe([&](const int& v) { received = v; });
    CHECK_EQ(prop.get(), 0);
    CHECK_EQ(received, 0);  // BehaviorSubject replays current on subscribe
    prop.set(7);
    CHECK_EQ(prop.get(), 7);
    CHECK_EQ(received, 7);
    sub.dispose();
}

TEST_CASE("rx.ReactiveProperty.asObservable") {
    ReactiveProperty<std::string> prop("a");
    int calls = 0;
    auto sub = prop.asObservable()->subscribe([&](const std::string&) { calls++; });
    prop.set("b");
    prop.set("c");
    CHECK(calls >= 3);  // initial replay + 2 sets
    sub.dispose();
}

// ---------------------------------------------------------------------------
// Script binding (Squirrel)
// ---------------------------------------------------------------------------
namespace {

std::string runRxSnippet(const std::string& body) {
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
        local ev = eve.Event();
        local ev2 = eve.Event();
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
