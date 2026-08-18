#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"

#include <string>
#include <vector>

namespace {

class IGreeter {
public:
    static constexpr const char* capabilityName = "test.IGreeter";
    virtual ~IGreeter() = default;
    virtual std::string greet() const = 0;
};

class Hello : public IGreeter {
public:
    std::string greet() const override { return "hello"; }
};

class Bonjour : public IGreeter {
public:
    std::string greet() const override { return "bonjour"; }
};

class IStep {
public:
    static constexpr const char* capabilityName = "test.IStep";
    virtual ~IStep() = default;
    virtual void run(std::vector<std::string>& log) = 0;
    virtual bool handles(const std::string& kind) const = 0;
};

class NamedStep : public IStep {
public:
    explicit NamedStep(std::string name) : name_(std::move(name)) {}
    void run(std::vector<std::string>& log) override { log.push_back(name_); }
    bool handles(const std::string& kind) const override { return kind == name_; }

private:
    std::string name_;
};

/** Each case starts from an empty registry; entries are process-global. */
struct Reset {
    Reset() { eve::cap::detail::clearAllRaw(); }
    ~Reset() { eve::cap::detail::clearAllRaw(); }
};

}  // namespace

TEST_CASE("capability.absentQueryIsNull") {
    Reset reset;
    CHECK(eve::cap::query<IGreeter>() == nullptr);
    CHECK_EQ(eve::cap::listenerCount<IStep>(), 0u);
    CHECK(eve::cap::listenerAt<IStep>(0) == nullptr);
}

TEST_CASE("capability.provideAndQuery") {
    Reset reset;
    Hello hello;
    eve::cap::provide<IGreeter>(&hello);

    IGreeter* found = eve::cap::query<IGreeter>();
    REQUIRE(found != nullptr);
    CHECK_EQ(found->greet(), "hello");
}

TEST_CASE("capability.provideReplacesPrevious") {
    Reset reset;
    Hello hello;
    Bonjour bonjour;
    eve::cap::provide<IGreeter>(&hello);
    eve::cap::provide<IGreeter>(&bonjour);

    REQUIRE(eve::cap::query<IGreeter>() != nullptr);
    CHECK_EQ(eve::cap::query<IGreeter>()->greet(), "bonjour");
}

TEST_CASE("capability.revoke") {
    Reset reset;
    Hello hello;
    eve::cap::provide<IGreeter>(&hello);
    eve::cap::revoke<IGreeter>(&hello);
    CHECK(eve::cap::query<IGreeter>() == nullptr);
}

TEST_CASE("capability.revokeOfSupersededProviderIsIgnored") {
    Reset reset;
    Hello hello;
    Bonjour bonjour;
    eve::cap::provide<IGreeter>(&hello);
    eve::cap::provide<IGreeter>(&bonjour);
    // A module torn down after being replaced must not unregister its successor.
    eve::cap::revoke<IGreeter>(&hello);

    REQUIRE(eve::cap::query<IGreeter>() != nullptr);
    CHECK_EQ(eve::cap::query<IGreeter>()->greet(), "bonjour");
}

TEST_CASE("capability.listenersRunInPriorityOrder") {
    Reset reset;
    NamedStep late("late"), early("early"), middle("middle");
    eve::cap::addListener<IStep>(&late, 30);
    eve::cap::addListener<IStep>(&early, 10);
    eve::cap::addListener<IStep>(&middle, 20);

    std::vector<std::string> log;
    eve::cap::forEach<IStep>([&](IStep* s) { s->run(log); });

    REQUIRE(log.size() == 3u);
    CHECK_EQ(log[0], "early");
    CHECK_EQ(log[1], "middle");
    CHECK_EQ(log[2], "late");
}

TEST_CASE("capability.equalPriorityKeepsRegistrationOrder") {
    Reset reset;
    NamedStep a("a"), b("b"), c("c");
    eve::cap::addListener<IStep>(&a);
    eve::cap::addListener<IStep>(&b);
    eve::cap::addListener<IStep>(&c);

    std::vector<std::string> log;
    eve::cap::forEach<IStep>([&](IStep* s) { s->run(log); });

    REQUIRE(log.size() == 3u);
    CHECK_EQ(log[0], "a");
    CHECK_EQ(log[1], "b");
    CHECK_EQ(log[2], "c");
}

TEST_CASE("capability.addListenerIsIdempotent") {
    Reset reset;
    NamedStep a("a");
    eve::cap::addListener<IStep>(&a);
    eve::cap::addListener<IStep>(&a);
    CHECK_EQ(eve::cap::listenerCount<IStep>(), 1u);
}

TEST_CASE("capability.removeListener") {
    Reset reset;
    NamedStep a("a"), b("b");
    eve::cap::addListener<IStep>(&a);
    eve::cap::addListener<IStep>(&b);
    eve::cap::removeListener<IStep>(&a);

    std::vector<std::string> log;
    eve::cap::forEach<IStep>([&](IStep* s) { s->run(log); });
    REQUIRE(log.size() == 1u);
    CHECK_EQ(log[0], "b");
}

TEST_CASE("capability.forEachUntilStopsAtFirstHandler") {
    Reset reset;
    NamedStep png("png"), json("json"), wav("wav");
    eve::cap::addListener<IStep>(&png);
    eve::cap::addListener<IStep>(&json);
    eve::cap::addListener<IStep>(&wav);

    std::vector<std::string> tried;
    const bool handled = eve::cap::forEachUntil<IStep>([&](IStep* s) {
        tried.push_back("probe");
        return s->handles("json");
    });

    CHECK(handled);
    // png then json; wav is never probed.
    CHECK_EQ(tried.size(), 2u);

    tried.clear();
    CHECK(!eve::cap::forEachUntil<IStep>([&](IStep* s) { return s->handles("gif"); }));
}

TEST_CASE("capability.listenersAndServiceAreIndependent") {
    Reset reset;
    Hello hello;
    NamedStep a("a");
    eve::cap::provide<IGreeter>(&hello);
    eve::cap::addListener<IStep>(&a);

    CHECK(eve::cap::query<IGreeter>() != nullptr);
    CHECK_EQ(eve::cap::listenerCount<IStep>(), 1u);
    // Distinct capability names never collide.
    CHECK(eve::cap::query<IStep>() == nullptr);
    CHECK_EQ(eve::cap::listenerCount<IGreeter>(), 0u);
}

TEST_CASE("capability.withdrawDuringDispatchIsSafe") {
    Reset reset;
    NamedStep a("a"), b("b");
    eve::cap::addListener<IStep>(&a);
    eve::cap::addListener<IStep>(&b);

    std::vector<std::string> log;
    eve::cap::forEach<IStep>([&](IStep* s) {
        s->run(log);
        eve::cap::removeListener<IStep>(&b);
    });

    // "a" runs, removes "b", and the index-based walk simply finds nothing at 1.
    REQUIRE(log.size() == 1u);
    CHECK_EQ(log[0], "a");
    CHECK_EQ(eve::cap::listenerCount<IStep>(), 1u);
}
