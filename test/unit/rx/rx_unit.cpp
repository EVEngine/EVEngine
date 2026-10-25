#include "zeroerr/unittest.h"

#include "rx/Rx.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using eve::rx::BehaviorSubject;
using eve::rx::Observer;
using eve::rx::ReactiveProperty;
using eve::rx::ReplaySubject;
using eve::rx::Subject;
using eve::rx::Subscription;
using eve::rx::Value;

TEST_CASE("rx.Value factory and converter contracts preserve type") {
    const auto integer = Value::makeInt(42);
    CHECK(integer.isInt());
    CHECK_EQ(integer.toInt(), std::int64_t{42});
    CHECK_EQ(integer.toString(), std::string("42"));
    CHECK(integer.toBool());
    CHECK_EQ(Value::makeFloat(3.5).toInt(), std::int64_t{3});
    CHECK_EQ(Value::makeString("hello").toString(), std::string("hello"));
    CHECK_EQ(Value::makeString("hello").toInt(), std::int64_t{0});
    CHECK(Value::makeNil().isNil());

    int        ownedElsewhere = 7;
    const auto borrowed       = Value::makePtr(&ownedElsewhere);
    CHECK(borrowed.isPtr());
    CHECK(borrowed.equals(Value::makePtr(&ownedElsewhere)));
    CHECK(!borrowed.equals(Value::makePtr(nullptr)));
    CHECK(Value::makeInt(1).equals(Value::makeInt(1)));
    REQUIRE(!Value::makeInt(1).equals(Value::makeFloat(1.0)));
}

TEST_CASE("rx.Subscription disposes once and transfers callback ownership") {
    int          calls = 0;
    Subscription handle([&]() { ++calls; });
    auto         moved = std::move(handle);
    CHECK(handle.isDisposed());
    CHECK(!moved.isDisposed());
    handle.dispose();
    CHECK_EQ(calls, 0);
    moved.dispose();
    moved.dispose();
    CHECK(moved.isDisposed());
    REQUIRE_EQ(calls, 1);
}

TEST_CASE("rx.Observer stops after the first terminal signal") {
    Observer<int>    observer;
    std::vector<int> values;
    int              errors    = 0;
    int              completed = 0;
    observer.onNext            = [&](const int& value) { values.push_back(value); };
    observer.onError           = [&](const std::string&) { ++errors; };
    observer.onCompleted       = [&]() { ++completed; };
    observer.next(1);
    observer.error("failure");
    observer.next(2);
    observer.error("again");
    observer.completed();
    REQUIRE_EQ(values.size(), std::size_t{1});
    CHECK_EQ(values[0], 1);
    CHECK_EQ(errors, 1);
    CHECK_EQ(completed, 0);
    CHECK(observer.isStopped());
}

TEST_CASE("rx.Subject subscription cancellation and terminal signals") {
    SUB_CASE("observers receive values in order until individually cancelled") {
        Subject<int>     source;
        std::vector<int> first;
        std::vector<int> second;
        CHECK(!source.hasObservers());
        auto a = source.subscribe([&](const int& value) { first.push_back(value); });
        auto b = source.subscribe([&](const int& value) { second.push_back(value); });
        CHECK_EQ(source.observerCount(), 2);
        source.onNext(1);
        a.dispose();
        CHECK_EQ(source.observerCount(), 1);
        source.onNext(2);
        b.dispose();
        CHECK(!source.hasObservers());
        REQUIRE_EQ(first.size(), std::size_t{1});
        CHECK_EQ(first[0], 1);
        REQUIRE_EQ(second.size(), std::size_t{2});
        CHECK_EQ(second[0], 1);
        CHECK_EQ(second[1], 2);
    };

    SUB_CASE("completion notifies once and prevents later values") {
        Subject<int>     source;
        std::vector<int> received;
        int              completed = 0;
        auto             subscription =
            source.subscribe([&](const int& value) { received.push_back(value); }, nullptr, [&]() { ++completed; });
        source.onNext(4);
        source.onCompleted();
        source.onNext(5);
        REQUIRE_EQ(received.size(), std::size_t{1});
        CHECK_EQ(received[0], 4);
        CHECK_EQ(completed, 1);
        CHECK_EQ(source.observerCount(), 0);
    };

    SUB_CASE("error forwards its payload and prevents later values") {
        Subject<int> source;
        std::string  error;
        int          values = 0;
        auto         subscription =
            source.subscribe([&](const int&) { ++values; }, [&](const std::string& value) { error = value; }, nullptr);
        source.onError("broken");
        source.onNext(1);
        CHECK_EQ(error, std::string("broken"));
        CHECK_EQ(values, 0);
        CHECK_EQ(source.observerCount(), 0);
    };
}

TEST_CASE("rx.Observable operators transform limit and stop streams in pipeline order") {
    SUB_CASE("filter then map emits transformed matching values") {
        Subject<int>                              source;
        std::unique_ptr<eve::rx::Observable<int>> even(source.filter([](const int& value) { return value % 2 == 0; }));
        std::unique_ptr<eve::rx::Observable<std::string>> mapped(
            even->map<std::string>([](const int& value) { return std::to_string(value * 10); }));
        std::vector<std::string> received;
        auto subscription = mapped->subscribe([&](const std::string& value) { received.push_back(value); });
        for (int value : {1, 2, 3, 4}) source.onNext(value);
        REQUIRE_EQ(received.size(), std::size_t{2});
        CHECK_EQ(received[0], std::string("20"));
        CHECK_EQ(received[1], std::string("40"));
    };

    SUB_CASE("take and first complete after their requested value count") {
        Subject<int>                              source;
        std::unique_ptr<eve::rx::Observable<int>> firstTwo(source.take(2));
        std::unique_ptr<eve::rx::Observable<int>> firstOnly(source.first());
        std::vector<int>                          two;
        std::vector<int>                          one;
        int                                       twoCompleted = 0;
        int                                       oneCompleted = 0;
        auto                                      a =
            firstTwo->subscribe([&](const int& value) { two.push_back(value); }, nullptr, [&]() { ++twoCompleted; });
        auto b =
            firstOnly->subscribe([&](const int& value) { one.push_back(value); }, nullptr, [&]() { ++oneCompleted; });
        for (int value : {7, 8, 9}) source.onNext(value);
        REQUIRE_EQ(two.size(), std::size_t{2});
        CHECK_EQ(two[0], 7);
        CHECK_EQ(two[1], 8);
        REQUIRE_EQ(one.size(), std::size_t{1});
        CHECK_EQ(one[0], 7);
        CHECK_EQ(twoCompleted, 1);
        CHECK_EQ(oneCompleted, 1);
    };

    SUB_CASE("skip then distinct removes adjacent duplicates after the prefix") {
        Subject<int>                              source;
        std::unique_ptr<eve::rx::Observable<int>> skipped(source.skip(2));
        std::unique_ptr<eve::rx::Observable<int>> distinct(skipped->distinctUntilChanged());
        std::vector<int>                          values;
        auto subscription = distinct->subscribe([&](const int& value) { values.push_back(value); });
        for (int value : {9, 9, 1, 1, 2, 2, 1}) source.onNext(value);
        REQUIRE_EQ(values.size(), std::size_t{3});
        CHECK_EQ(values[0], 1);
        CHECK_EQ(values[1], 2);
        CHECK_EQ(values[2], 1);
    };

    SUB_CASE("takeUntil completes when the stopper emits") {
        Subject<int>                              source;
        Subject<int>                              stopper;
        std::unique_ptr<eve::rx::Observable<int>> limited(source.takeUntil(&stopper));
        std::vector<int>                          values;
        int                                       completed = 0;
        auto                                      subscription =
            limited->subscribe([&](const int& value) { values.push_back(value); }, nullptr, [&]() { ++completed; });
        source.onNext(1);
        stopper.onNext(0);
        source.onNext(2);
        REQUIRE_EQ(values.size(), std::size_t{1});
        CHECK_EQ(values[0], 1);
        CHECK_EQ(completed, 1);
        CHECK_EQ(source.observerCount(), 0);
    };
}

TEST_CASE("rx.BehaviorSubject replays latest value and ReactiveProperty exposes it") {
    BehaviorSubject<int> behavior(100);
    behavior.onNext(200);
    std::vector<int> values;
    auto             subscription = behavior.subscribe([&](const int& value) { values.push_back(value); });
    behavior.setValue(300);
    REQUIRE_EQ(values.size(), std::size_t{2});
    CHECK_EQ(values[0], 200);
    CHECK_EQ(values[1], 300);
    CHECK_EQ(behavior.getValue(), 300);

    ReactiveProperty<std::string> property("a");
    std::vector<std::string>      propertyValues;
    auto                          propertySubscription =
        property.asObservable()->subscribe([&](const std::string& value) { propertyValues.push_back(value); });
    property.set("b");
    CHECK_EQ(property.get(), std::string("b"));
    REQUIRE_EQ(propertyValues.size(), std::size_t{2});
    CHECK_EQ(propertyValues[0], std::string("a"));
    CHECK_EQ(propertyValues[1], std::string("b"));
}

TEST_CASE("rx.ReplaySubject replays only the newest bounded history") {
    ReplaySubject<int> replay(3);
    for (int value : {1, 2, 3, 4}) replay.onNext(value);
    std::vector<int> values;
    auto             subscription = replay.subscribe([&](const int& value) { values.push_back(value); });
    replay.onNext(5);
    REQUIRE_EQ(values.size(), std::size_t{4});
    CHECK_EQ(values[0], 2);
    CHECK_EQ(values[1], 3);
    CHECK_EQ(values[2], 4);
    CHECK_EQ(values[3], 5);
}
