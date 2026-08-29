#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/Resource.h"

#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

/** Payload = a per-path generation counter observed at load time. */
class TestResource : public eve::Resource {
public:
    TestResource() : eve::Resource("") {}

    void adopt(eve::Resource &replacement) override {
        auto &other = static_cast<TestResource &>(replacement);
        if (other.failOnAdopt) throw std::runtime_error("injected adopt failure");
        std::swap(value, other.value);
    }

    int  value = 0;
    bool failOnAdopt = false;
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
    b->addDependency(eve::ref<eve::Resource>(a));
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
    a->addDependency(eve::ref<eve::Resource>(b));
    b->addDependency(eve::ref<eve::Resource>(a));

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
    derived->addDependency(eve::ref<eve::Resource>(source));
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
