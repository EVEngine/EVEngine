#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include <atomic>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include "common/Diagnostic.h"
#include "common/StringMapInstances.h"
#include "common/Subscription.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// common_diagnostic_vector_instances.cpp
TEST_CASE("common.diagnosticVectorInstances.owningLifecycleAcrossLibrary") {
    std::vector<eve::Diagnostic> values;
    values.reserve(128);
    for (unsigned i = 0; i < 64; ++i)
        values.push_back(eve::Diagnostic::error(eve::DiagnosticCode::Failed, std::string(4096, 'a')));
    auto copied  = values;
    copied.at(0) = eve::Diagnostic::warning(eve::DiagnosticCode::Conflict, "changed");
    CHECK(values.at(0).message().size() == 4096u);
    CHECK(copied.at(0).message() == "changed");
    auto moved = std::move(copied);
    copied.clear();
    copied.push_back(eve::Diagnostic::error(eve::DiagnosticCode::Failed, "reused"));
    std::vector<eve::Diagnostic> assigned;
    assigned = moved;
    assigned = std::move(moved);
    CHECK(assigned.size() == 64u);
    CHECK(assigned.at(0).message() == "changed");
    CHECK(copied.at(0).message() == "reused");
    assigned.erase(assigned.begin());
    assigned.reserve(256);
    CHECK(assigned.size() == 63u);
    CHECK(assigned.at(0).message().size() == 4096u);
}

TEST_CASE("common.diagnosticVectorInstances.detailOwnershipAndIteration") {
    auto diagnostic =
        eve::Diagnostic::error(eve::DiagnosticCode::Failed, "message", "path", {{"key", "value"}}, "source");
    std::vector<eve::Diagnostic> values{diagnostic};
    diagnostic.addDetail("second", "local");
    CHECK(values.at(0).details().size() == 1u);
    auto copied = values;
    copied.at(0).addDetail("second", "copied");
    CHECK(values.at(0).details().size() == 1u);
    CHECK(copied.at(0).details().size() == 2u);
    CHECK(copied.at(0).path() == "path");
    CHECK(copied.at(0).source() == "source");
    std::size_t messages = 0;
    for (const auto& item : copied) messages += item.message().size();
    CHECK(messages == 7u);
}

TEST_CASE("common.diagnosticVectorInstances.independentContainersOnThreads") {
    std::atomic<unsigned> failures{0};
    auto                  run = [&] {
        for (unsigned iteration = 0; iteration < 32; ++iteration) {
            std::vector<eve::Diagnostic> values;
            values.push_back(eve::Diagnostic::error(eve::DiagnosticCode::Failed, std::string(2048, 'a')));
            auto copied  = values;
            copied.at(0) = eve::Diagnostic::warning(eve::DiagnosticCode::Conflict, "changed");
            copied.reserve(128);
            if (values.at(0).message().size() != 2048 || copied.at(0).message() != "changed") ++failures;
        }
    };
    std::thread a(run), b(run), c(run), d(run);
    a.join();
    b.join();
    c.join();
    d.join();
    CHECK(failures.load() == 0u);
}

// common_string_map_instances.cpp
namespace {
using AttributeMap = std::unordered_map<std::string, std::string>;
static_assert(std::is_move_constructible_v<AttributeMap>);
static_assert(std::is_copy_constructible_v<AttributeMap>);
}  // namespace

TEST_CASE("common.stringMap.owningLifecycleAcrossLibrary") {
    AttributeMap original{{"long", std::string(4096, 'a')}, {"remove", "old"}};
    AttributeMap copy(original);
    original.at("long")[0] = 'b';
    CHECK(copy.at("long")[0] == 'a');
    AttributeMap moved(std::move(copy));
    CHECK(moved.at("long").size() == 4096);
    AttributeMap assigned;
    assigned = moved;
    CHECK(static_cast<bool>(assigned == moved));
    assigned = std::move(moved);
    CHECK(assigned.at("long")[0] == 'a');
    copy["reused"] = "after move";
    CHECK(copy.at("reused") == "after move");
    assigned.erase("remove");
    assigned.reserve(128);
    assigned.rehash(256);
    CHECK(assigned.size() == 1);
    CHECK(original.contains("remove"));
}

TEST_CASE("common.stringMap.nodeTransferAndIteration") {
    AttributeMap source{{"key", std::string(4096, 'x')}, {"other", "value"}};
    auto         node = source.extract("key");
    REQUIRE(!node.empty());
    node.key() = "renamed";
    AttributeMap destination;
    auto         inserted = destination.insert(std::move(node));
    REQUIRE(inserted.inserted);
    CHECK(inserted.position->second.size() == 4096);
    CHECK(!source.contains("key"));
    destination.merge(source);
    CHECK(source.empty());
    const AttributeMap& values = destination;
    CHECK(static_cast<bool>(values.find("renamed") != values.end()));
    CHECK(values.at("other") == "value");
    std::size_t count = 0;
    for (const auto& [key, value] : values) {
        CHECK(!key.empty());
        CHECK(!value.empty());
        ++count;
    }
    CHECK(count == 2);
}

TEST_CASE("common.stringMap.independentContainersOnThreads") {
    std::atomic<int>         failures{0};
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) {
        workers.emplace_back([worker, &failures] {
            for (int iteration = 0; iteration < 32; ++iteration) {
                const std::string expected(2048, static_cast<char>('a' + worker));
                AttributeMap      original{{"value", expected}};
                AttributeMap      copy(original);
                copy["iteration"] = std::to_string(iteration);
                copy.rehash(64);
                if (copy.at("value") != expected || original.size() != 1) ++failures;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    CHECK(failures.load() == 0);
}

// common_subscription.cpp
TEST_CASE("common.subscriptionIsMoveOnlyAndCancellationIsIdempotent") {
    static_assert(!std::is_copy_constructible_v<eve::Subscription>);
    static_assert(!std::is_copy_assignable_v<eve::Subscription>);
    static_assert(std::is_move_constructible_v<eve::Subscription>);
    static_assert(std::is_move_assignable_v<eve::Subscription>);

    int               cancellations = 0;
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

    first  = observer.subscribe([&](const int& value) {
        calls.push_back(value * 10 + 1);
        second.dispose();
        first.dispose();
        added = observer.subscribe([&](const int& next) { calls.push_back(next * 10 + 3); });
    });
    second = observer.subscribe([&](const int& value) { calls.push_back(value * 10 + 2); });

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
        subscription = observer.subscribe([](const int&) {});
        CHECK_EQ(observer.size(), std::size_t{1});
    }

    CHECK(!subscription.disposed());
    subscription.dispose();
    CHECK(subscription.disposed());
}
