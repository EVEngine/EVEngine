#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/Resource.h"
#include "common/AsyncWork.h"

#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

/** Payload = a per-path generation counter observed at load time. */
class TestResource : public eve::Resource {
public:
    TestResource() : eve::Resource("") { ++alive; }
    ~TestResource() override { --alive; }

    void adopt(eve::Resource &replacement) override {
        auto &other = static_cast<TestResource &>(replacement);
        if (other.failOnAdopt) throw std::runtime_error("injected adopt failure");
        std::swap(value, other.value);
    }

    int  value = 0;
    bool failOnAdopt = false;

    /** @brief Live instances, so a case can assert "destroyed exactly once". */
    static int alive;
};

/** Fake asset provider: claims "*.dat" and serves a fresh generation each load. */
class TestProvider : public eve::caps::IAssetReloader {
public:
    const char *reloadKind() const override { return "test"; }

    bool handlesPath(const std::string &normPath) const override {
        const std::string path = eve::ResourceManager::pathOfKey(normPath);
        return path.size() >= 4 && path.compare(path.size() - 4, 4, ".dat") == 0;
    }

    eve::Result<bool> reload(const std::string &) override {
        // ResourceManager owns the test cache refresh; this provider only
        // supplies detached candidates through load().
        return eve::Result<bool>::success(false);
    }

    eve::Resource *load(const std::string &key) override {
        if (!handlesPath(key)) return nullptr;
        if (failures.count(key)) return nullptr;
        auto *r = new TestResource();
        r->value = ++generation[key];
        r->failOnAdopt = adoptFailures.count(key) != 0;
        return r;
    }

    static std::map<std::string, int> generation;
    static std::set<std::string>      failures;
    static std::set<std::string>      adoptFailures;
};

std::map<std::string, int> TestProvider::generation;
std::set<std::string> TestProvider::failures;
std::set<std::string> TestProvider::adoptFailures;
int                        TestResource::alive = 0;

/** Every case starts from an empty cache + empty capability registry. */
struct Reset {
    Reset() {
        eve::ResourceManager::getInstance().clear();
        eve::cap::detail::clearAllRaw();
    }
    ~Reset() {
        eve::ResourceManager::getInstance().clear();
        eve::cap::detail::clearAllRaw();
    }
    Reset(const Reset &) = delete;
    Reset &operator=(const Reset &) = delete;
};

TestProvider &provider() {
    static TestProvider p;
    eve::cap::addListener<eve::caps::IAssetReloader>(&p, eve::caps::IAssetReloader::kCache);
    TestProvider::generation.clear();
    TestProvider::failures.clear();
    TestProvider::adoptFailures.clear();
    return p;
}

eve::Resource *get(const std::string &key) {
    return eve::ResourceManager::getInstance().get(key);
}

}  // namespace

TEST_CASE("resource.normalizePath") {
    CHECK_EQ(eve::ResourceManager::normalizePath("./a/b.nut"), std::string("a/b.nut"));
    CHECK_EQ(eve::ResourceManager::normalizePath("a\\b.json"), std::string("a/b.json"));
    CHECK_EQ(eve::ResourceManager::normalizePath("x/"), std::string("x"));
}

TEST_CASE("resource.makeKeyAndPathOfKey") {
    CHECK_EQ(eve::ResourceManager::makeKey("a/b.png"), std::string("a/b.png"));
    CHECK_EQ(eve::ResourceManager::makeKey("a\\b.png", "size=16"), std::string("a/b.png?size=16"));
    CHECK_EQ(eve::ResourceManager::pathOfKey("a/b.png?size=16"), std::string("a/b.png"));
    CHECK_EQ(eve::ResourceManager::pathOfKey("./a/b.png?size=16"), std::string("a/b.png"));
}

TEST_CASE("resource.getUnknownKeyIsNull") {
    Reset reset;
    provider();
    CHECK(get("no_such_kind.zzz") == nullptr);
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 0u);
}

TEST_CASE("resource.getCachesSameInstance") {
    Reset reset;
    provider();

    eve::Resource *a = get("a.dat");
    eve::Resource *b = get("a.dat");
    REQUIRE(a != nullptr);
    CHECK(a == b);
    CHECK_EQ(static_cast<TestResource *>(a)->value, 1);
    CHECK_EQ(TestProvider::generation.size(), 1u);
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 1u);
}

TEST_CASE("resource.paramsAreDistinctEntries") {
    Reset reset;
    provider();

    eve::Resource *a = get("a.dat?size=16");
    eve::Resource *b = get("a.dat?size=32");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a != b);
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 2u);
}

TEST_CASE("resource.unloadDropsEntryButKeepsHoldersAlive") {
    Reset reset;
    provider();

    eve::Resource *a = get("a.dat");
    REQUIRE(a != nullptr);
    eve::ref<eve::Resource> holder(a);  // an external holder keeps it alive
    eve::ResourceManager::getInstance().unload("a.dat");
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 0u);
    // The holder's ref keeps the object alive and usable.
    CHECK_EQ(static_cast<TestResource *>(a)->value, 1);

    // A later get() loads a fresh instance.
    eve::Resource *b = get("a.dat");
    REQUIRE(b != nullptr);
    CHECK(b != a);
    CHECK_EQ(static_cast<TestResource *>(b)->value, 2);
}

TEST_CASE("resource.unloadPathMatchesAnyParams") {
    Reset reset;
    provider();

    REQUIRE(get("a.dat?size=16") != nullptr);
    REQUIRE(get("a.dat?size=32") != nullptr);
    REQUIRE(get("b.dat") != nullptr);
    eve::ResourceManager::getInstance().unloadPath("a.dat");
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 1u);
}

TEST_CASE("resource.reloadRefreshesInPlace") {
    Reset reset;
    provider();

    eve::Resource *a = get("a.dat");
    REQUIRE(a != nullptr);
    TestResource *ptr = static_cast<TestResource *>(a);
    CHECK_EQ(ptr->value, 1);

    auto result = eve::ResourceManager::getInstance().reload("a.dat");
    CHECK(result.ok());
    CHECK(result.value());
    CHECK(a == ptr);  // identity stays stable
    CHECK_EQ(ptr->value, 2);                             // fresh payload adopted
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 1u);
}

TEST_CASE("resource.reloadWithoutCachedEntryIsNoop") {
    Reset reset;
    provider();

    CHECK(!eve::ResourceManager::getInstance().handlesPath("a.dat"));
    auto result = eve::ResourceManager::getInstance().reload("a.dat");
    CHECK(result.ok());
    CHECK(!result.value());
}

TEST_CASE("resource.reloadTriggersDependents") {
    Reset reset;
    provider();

    eve::Resource *a = get("a.dat");
    eve::Resource *b = get("b.dat");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(b->addDependency(*a).ok());
    CHECK_EQ(static_cast<TestResource *>(a)->value, 1);
    CHECK_EQ(static_cast<TestResource *>(b)->value, 1);

    auto result = eve::ResourceManager::getInstance().reload("a.dat");
    CHECK(result.ok());
    CHECK(result.value());
    CHECK_EQ(static_cast<TestResource *>(a)->value, 2);
    // The dependent entry refreshed transitively, in place.
    CHECK_EQ(static_cast<TestResource *>(b)->value, 2);
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 2u);
}

TEST_CASE("resource.reloadDependencyCycleIsSafe") {
    Reset reset;
    provider();

    eve::Resource *a = get("a.dat");
    eve::Resource *b = get("b.dat");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a->addDependency(*b).ok());
    CHECK(b->addDependency(*a).ok());

    auto result = eve::ResourceManager::getInstance().reload("a.dat");
    CHECK(result.ok());
    CHECK(result.value());
    CHECK_EQ(static_cast<TestResource *>(a)->value, 2);
    CHECK_EQ(static_cast<TestResource *>(b)->value, 2);
}

TEST_CASE("resource.reloadAllVariantsIsTransactional") {
    Reset reset;
    provider();

    auto *small = static_cast<TestResource *>(get("a.dat?size=16"));
    auto *large = static_cast<TestResource *>(get("a.dat?size=32"));
    REQUIRE(small != nullptr);
    REQUIRE(large != nullptr);
    TestProvider::failures.insert("a.dat?size=32");

    auto result = eve::ResourceManager::getInstance().reload("a.dat");
    CHECK(result.ok());
    CHECK(!result.value());
    CHECK_EQ(small->value, 1);
    CHECK_EQ(large->value, 1);
}

TEST_CASE("resource.reloadDependentFailureRollsBackWholeGraph") {
    Reset reset;
    provider();

    auto *source = static_cast<TestResource *>(get("source.dat"));
    auto *derived = static_cast<TestResource *>(get("derived.dat"));
    REQUIRE(source != nullptr);
    REQUIRE(derived != nullptr);
    CHECK(derived->addDependency(*source).ok());
    TestProvider::failures.insert("derived.dat");

    auto result = eve::ResourceManager::getInstance().reload("source.dat");
    CHECK(result.ok());
    CHECK(!result.value());
    CHECK_EQ(source->value, 1);
    CHECK_EQ(derived->value, 1);
}

TEST_CASE("resource.reloadCommitFailureRollsBackEarlierEntries") {
    Reset reset;
    provider();

    auto *first = static_cast<TestResource *>(get("a.dat?variant=1"));
    auto *second = static_cast<TestResource *>(get("a.dat?variant=2"));
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    TestProvider::adoptFailures.insert("a.dat?variant=2");

    auto result = eve::ResourceManager::getInstance().reload("a.dat");
    CHECK(result.ok());
    CHECK(!result.value());
    CHECK_EQ(first->value, 1);
    CHECK_EQ(second->value, 1);
}

TEST_CASE("resource.requestNoOpWhenCached") {
    Reset reset;
    provider();
    REQUIRE(get("a.dat") != nullptr);
    auto queued = eve::ResourceManager::getInstance().request("a.dat");
    CHECK(queued.ok());
    CHECK_EQ(queued.code(), eve::StatusCode::NoOp);
}

TEST_CASE("resource.requestUnknownIsNotFound") {
    Reset reset;
    provider();
    auto queued = eve::ResourceManager::getInstance().request("no_such_kind.zzz");
    CHECK(!queued.ok());
    CHECK_EQ(queued.code(), eve::StatusCode::NotFound);
}

TEST_CASE("resource.peekDoesNotLoad") {
    Reset reset;
    provider();
    CHECK(!eve::ResourceManager::getInstance().peek("a.dat").has_value());
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 0u);
}

TEST_CASE("resource.requestAsyncThenWaitFor") {
    Reset reset;
    provider();

    std::mutex gate;
    gate.lock();
    class DelayedExecutor final : public eve::caps::IAsyncWorkExecutor {
    public:
        explicit DelayedExecutor(std::mutex *gate) : gate_(gate) {}
        eve::Result<void> submit(std::function<void()> work) override {
            std::thread([work = std::move(work), gate = gate_]() {
                std::lock_guard<std::mutex> hold(*gate);
                work();
            }).detach();
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        }
        std::mutex *gate_ = nullptr;
    } executor{&gate};
    eve::cap::provide<eve::caps::IAsyncWorkExecutor>(&executor);

    auto queued = eve::ResourceManager::getInstance().request("async.dat");
    CHECK(queued.ok());
    CHECK_EQ(queued.code(), eve::StatusCode::Applied);
    CHECK_EQ(eve::ResourceManager::getInstance().pendingCount(), 1u);
    CHECK(!eve::ResourceManager::getInstance().peek("async.dat").has_value());

    gate.unlock();
    auto ready = eve::ResourceManager::getInstance().waitFor("async.dat");
    REQUIRE(ready.ok());
    CHECK_EQ(static_cast<TestResource &>(ready.value().get()).value, 1);
    CHECK_EQ(eve::ResourceManager::getInstance().pendingCount(), 0u);
}

TEST_CASE("resource.getJoinsInFlightRequest") {
    Reset reset;
    provider();

    std::mutex gate;
    gate.lock();
    class DelayedExecutor final : public eve::caps::IAsyncWorkExecutor {
    public:
        explicit DelayedExecutor(std::mutex *gate) : gate_(gate) {}
        eve::Result<void> submit(std::function<void()> work) override {
            std::thread([work = std::move(work), gate = gate_]() {
                std::lock_guard<std::mutex> hold(*gate);
                work();
            }).detach();
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        }
        std::mutex *gate_ = nullptr;
    } executor{&gate};
    eve::cap::provide<eve::caps::IAsyncWorkExecutor>(&executor);

    auto queued = eve::ResourceManager::getInstance().request("join.dat");
    CHECK(queued.ok());
    CHECK_EQ(eve::ResourceManager::getInstance().pendingCount(), 1u);
    gate.unlock();
    eve::Resource *loaded = get("join.dat");
    REQUIRE(loaded != nullptr);
    CHECK_EQ(static_cast<TestResource *>(loaded)->value, 1);
}

TEST_CASE("resource.pinKeepsPayloadAliveAcrossUnload") {
    Reset reset;
    provider();

    auto *sound = static_cast<TestResource *>(get("sound.dat"));
    REQUIRE(sound != nullptr);
    const int liveBefore = TestResource::alive;

    auto pinned = eve::ResourceManager::getInstance().pin(*sound);
    REQUIRE(pinned.ok());
    eve::ResourcePin keepAlive = std::move(pinned).takeValue();
    CHECK_EQ(keepAlive.get(), sound);

    // The cache entry is gone, but the payload survives for the pin holder: this is
    // what keeps a SoundData alive while a Source still plays it (regression #1 of
    // the migration plan, which the previous ref<T> graph provided).
    eve::ResourceManager::getInstance().unload("sound.dat");
    CHECK(!eve::ResourceManager::getInstance().peek("sound.dat").has_value());
    CHECK_EQ(static_cast<TestResource *>(keepAlive.get())->value, 1);
    CHECK_EQ(TestResource::alive, liveBefore);

    // Releasing the last pin destroys the orphaned payload exactly once.
    keepAlive = eve::ResourcePin();
    CHECK_EQ(TestResource::alive, liveBefore - 1);
}

TEST_CASE("resource.pinRejectsUncachedResource") {
    Reset reset;
    provider();

    TestResource detached;
    auto         pinned = eve::ResourceManager::getInstance().pin(detached);
    CHECK(!pinned.ok());
    CHECK_EQ(pinned.code(), eve::StatusCode::NotFound);
}

TEST_CASE("resource.dependencyPinOutlivesDependencyCacheEntry") {
    Reset reset;
    provider();

    auto *source  = static_cast<TestResource *>(get("source.dat"));
    auto *derived = static_cast<TestResource *>(get("derived.dat"));
    REQUIRE(source != nullptr);
    REQUIRE(derived != nullptr);
    CHECK(derived->addDependency(*source).ok());
    const int liveBefore = TestResource::alive;

    // Dropping the dependency's own cache entry must not destroy it while the
    // dependent entry is alive (regression #2 of the migration plan).
    eve::ResourceManager::getInstance().unload("source.dat");
    CHECK_EQ(TestResource::alive, liveBefore);
    auto dependencies = derived->getDependencies();
    REQUIRE(dependencies.size() == 1u);
    CHECK_EQ(dependencies.front(), source);

    // clear() retires every slot, so the two keep-alives resolve in either slot
    // order and each payload is destroyed exactly once.
    eve::ResourceManager::getInstance().clear();
    CHECK_EQ(TestResource::alive, liveBefore - 2);
}
