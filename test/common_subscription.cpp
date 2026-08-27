#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Subscription.h"

#include <type_traits>
#include <utility>
#include <vector>

TEST_CASE("common.subscriptionIsMoveOnlyAndCancellationIsIdempotent") {
    static_assert(!std::is_copy_constructible_v<eve::Subscription>);
    static_assert(!std::is_copy_assignable_v<eve::Subscription>);
    static_assert(std::is_move_constructible_v<eve::Subscription>);
    static_assert(std::is_move_assignable_v<eve::Subscription>);

    int cancellations = 0;
    eve::Subscription original([&cancellations]() noexcept { ++cancellations; });
    eve::Subscription moved(std::move(original));
    CHECK(original.disposed());
    CHECK(!moved.disposed());

    moved.dispose();
    moved.dispose();
    CHECK(moved.disposed());
    CHECK_EQ(cancellations, 1);
}

TEST_CASE("common.observerAllowsReentrantSubscribeAndDispose") {
    eve::Observer<int> observer;
    std::vector<int>   calls;
    eve::Subscription  first;
    eve::Subscription  second;
    eve::Subscription  added;

    first = observer.subscribe([&](const int &value) {
        calls.push_back(value * 10 + 1);
        second.dispose();
        first.dispose();
        added = observer.subscribe([&](const int &next) { calls.push_back(next * 10 + 3); });
    });
    second = observer.subscribe([&](const int &value) { calls.push_back(value * 10 + 2); });

    observer.notify(1);
    CHECK_EQ(calls, std::vector<int>({11}));
    CHECK_EQ(observer.size(), std::size_t{1});

    observer.notify(2);
    CHECK_EQ(calls, std::vector<int>({11, 23}));
    CHECK_EQ(observer.size(), std::size_t{1});
}

TEST_CASE("common.observerDoesNotRetainOwnerAfterDestruction") {
    eve::Subscription subscription;
    {
        eve::Observer<int> observer;
        subscription = observer.subscribe([](const int &) {});
        CHECK_EQ(observer.size(), std::size_t{1});
    }

    CHECK(!subscription.disposed());
    subscription.dispose();
    CHECK(subscription.disposed());
}
