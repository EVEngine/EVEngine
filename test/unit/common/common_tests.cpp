#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_capability) || \
    defined(EVE_TEST_ORIGIN_protocol_dispatch)
#include "PrivateTestScope.h"
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include "common/Capability.h"
#include "common/CapabilityOwned.h"
#include "common/ProtocolDispatch.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: common_capabilities
namespace test_common_capabilities {
// capability.cpp
namespace {
namespace capability {
namespace {

class IGreeter {
public:
    static constexpr const char* capabilityName = "test.IGreeter";
    virtual ~IGreeter()                         = default;
    virtual std::string greet() const           = 0;
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
    static constexpr const char* capabilityName         = "test.IStep";
    virtual ~IStep()                                    = default;
    virtual void run(std::vector<std::string>& log)     = 0;
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
    Reset   reset;
    Hello   hello;
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
    Reset   reset;
    Hello   hello;
    Bonjour bonjour;
    eve::cap::provide<IGreeter>(&hello);
    eve::cap::provide<IGreeter>(&bonjour);
    // A module torn down after being replaced must not unregister its successor.
    eve::cap::revoke<IGreeter>(&hello);

    REQUIRE(eve::cap::query<IGreeter>() != nullptr);
    CHECK_EQ(eve::cap::query<IGreeter>()->greet(), "bonjour");
}

TEST_CASE("capability.listenersRunInPriorityOrder") {
    Reset     reset;
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
    Reset     reset;
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
    Reset     reset;
    NamedStep a("a");
    eve::cap::addListener<IStep>(&a);
    eve::cap::addListener<IStep>(&a);
    CHECK_EQ(eve::cap::listenerCount<IStep>(), 1u);
}

TEST_CASE("capability.removeListener") {
    Reset     reset;
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
    Reset     reset;
    NamedStep png("png"), json("json"), wav("wav");
    eve::cap::addListener<IStep>(&png);
    eve::cap::addListener<IStep>(&json);
    eve::cap::addListener<IStep>(&wav);

    std::vector<std::string> tried;
    const bool               handled = eve::cap::forEachUntil<IStep>([&](IStep* s) {
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
    Reset     reset;
    Hello     hello;
    NamedStep a("a");
    eve::cap::provide<IGreeter>(&hello);
    eve::cap::addListener<IStep>(&a);

    CHECK(eve::cap::query<IGreeter>() != nullptr);
    CHECK_EQ(eve::cap::listenerCount<IStep>(), 1u);
    // Distinct capability names never collide.
    CHECK(eve::cap::query<IStep>() == nullptr);
    CHECK_EQ(eve::cap::listenerCount<IGreeter>(), 0u);
}

TEST_CASE("capability.withdrawDuringDispatchAffectsNextDispatch") {
    Reset     reset;
    NamedStep a("a"), b("b");
    eve::cap::addListener<IStep>(&a);
    eve::cap::addListener<IStep>(&b);

    std::vector<std::string> log;
    eve::cap::forEach<IStep>([&](IStep* s) {
        s->run(log);
        eve::cap::removeListener<IStep>(&b);
    });

    // The active dispatch owns a stable pointer snapshot. Removing b changes
    // the registry for the next dispatch without changing this one.
    REQUIRE(log.size() == 2u);
    CHECK_EQ(log[0], "a");
    CHECK_EQ(log[1], "b");
    CHECK_EQ(eve::cap::listenerCount<IStep>(), 1u);

    log.clear();
    eve::cap::forEach<IStep>([&](IStep* s) { s->run(log); });
    REQUIRE(log.size() == 1u);
    CHECK_EQ(log[0], "a");
}

TEST_CASE("capability.withdrawCurrentListenerDoesNotSkipSuccessor") {
    Reset     reset;
    NamedStep a("a"), b("b"), c("c");
    eve::cap::addListener<IStep>(&a);
    eve::cap::addListener<IStep>(&b);
    eve::cap::addListener<IStep>(&c);

    std::vector<std::string> log;
    eve::cap::forEach<IStep>([&](IStep* s) {
        s->run(log);
        if (s == &a) eve::cap::removeListener<IStep>(&a);
    });

    REQUIRE(log.size() == 3u);
    CHECK_EQ(log[0], "a");
    CHECK_EQ(log[1], "b");
    CHECK_EQ(log[2], "c");
    CHECK_EQ(eve::cap::listenerCount<IStep>(), 2u);
}

TEST_CASE("capability.registrationDuringDispatchAffectsNextDispatch") {
    Reset     reset;
    NamedStep a("a"), b("b"), early("early");
    eve::cap::addListener<IStep>(&a, 10);
    eve::cap::addListener<IStep>(&b, 20);

    std::vector<std::string> log;
    eve::cap::forEach<IStep>([&](IStep* s) {
        s->run(log);
        if (s == &a) eve::cap::addListener<IStep>(&early, 0);
    });
    REQUIRE(log.size() == 2u);
    CHECK_EQ(log[0], "a");
    CHECK_EQ(log[1], "b");

    log.clear();
    eve::cap::forEach<IStep>([&](IStep* s) { s->run(log); });
    REQUIRE(log.size() == 3u);
    CHECK_EQ(log[0], "early");
    CHECK_EQ(log[1], "a");
    CHECK_EQ(log[2], "b");
}

TEST_CASE("capability.nestedDispatchUsesCurrentRegistry") {
    Reset     reset;
    NamedStep a("a"), b("b"), early("early");
    eve::cap::addListener<IStep>(&a, 10);
    eve::cap::addListener<IStep>(&b, 20);

    std::vector<std::string> outer;
    std::vector<std::string> nested;
    eve::cap::forEach<IStep>([&](IStep* s) {
        s->run(outer);
        if (s == &a) {
            eve::cap::addListener<IStep>(&early, 0);
            eve::cap::forEach<IStep>([&](IStep* inner) { inner->run(nested); });
        }
    });

    REQUIRE(outer.size() == 2u);
    CHECK_EQ(outer[0], "a");
    CHECK_EQ(outer[1], "b");
    REQUIRE(nested.size() == 3u);
    CHECK_EQ(nested[0], "early");
    CHECK_EQ(nested[1], "a");
    CHECK_EQ(nested[2], "b");
}

TEST_CASE("capability.providerRefBindsOnceAndIsNonCopyable") {
    Reset reset;
    static_assert(!std::is_copy_constructible_v<eve::cap::ProviderRef<IGreeter>>);
    static_assert(!std::is_copy_assignable_v<eve::cap::ProviderRef<IGreeter>>);

    Hello hello;
    eve::cap::provide<IGreeter>(&hello);
    eve::cap::ProviderRef<IGreeter> greeter = eve::cap::ProviderRef<IGreeter>::bind();
    // Avoid REQUIRE(greeter): zeroerr pretty-printing would copy the non-copyable ref.
    REQUIRE(greeter.get() != nullptr);
    CHECK_EQ(greeter->greet(), "hello");

    // Revoke does not auto-clear a cached ProviderRef; lifetime is the caller's.
    eve::cap::revoke<IGreeter>(&hello);
    CHECK(greeter.get() == &hello);
}

TEST_CASE("capability.ownedProvideAcquireAndStaleUnload") {
    Reset reset;
    auto  hello        = std::make_shared<Hello>();
    auto  handleResult = eve::cap::OwnedProviderRegistry<IGreeter>::provide("greeter.main", hello);
    REQUIRE(handleResult.ok());
    const auto handle      = std::move(handleResult).takeValue();
    auto       leaseResult = eve::cap::OwnedProviderRegistry<IGreeter>::acquire(handle);
    REQUIRE(leaseResult.ok());
    CHECK_EQ(leaseResult.value()->greet(), "hello");
    // Borrowed query slot mirrors the owned publication.
    REQUIRE(eve::cap::query<IGreeter>() != nullptr);
    CHECK_EQ(eve::cap::query<IGreeter>()->greet(), "hello");

    auto unloaded = eve::cap::OwnedProviderRegistry<IGreeter>::unload(handle);
    REQUIRE(unloaded.ok());
    auto stale = eve::cap::OwnedProviderRegistry<IGreeter>::acquire(handle);
    CHECK(!stale.ok());
    CHECK(eve::cap::query<IGreeter>() == nullptr);
}
}  // namespace capability
}  // namespace

// protocol_dispatch.cpp
namespace {
namespace protocol_dispatch {
static_assert(eve::DiagnosticValue<eve::ProtocolDispatch>);

TEST_CASE("common.protocolDispatch.hasExhaustiveStableOutcomes") {
    CHECK_EQ(eve::diagnosticValueName(eve::ProtocolDispatch::ReplySent), std::string_view("reply_sent"));
    CHECK_EQ(eve::diagnosticValueName(eve::ProtocolDispatch::NotificationHandled),
             std::string_view("notification_handled"));
    CHECK_EQ(eve::diagnosticValueName(eve::ProtocolDispatch::Rejected), std::string_view("rejected"));
    CHECK_EQ(eve::diagnosticValueName(eve::ProtocolDispatch::Terminate), std::string_view("terminate"));
    CHECK_EQ(eve::diagnosticValueCode(eve::ProtocolDispatch::Rejected), eve::DiagnosticCode::PreconditionViolation);
}
}  // namespace protocol_dispatch
}  // namespace
}  // namespace test_common_capabilities
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_common_diagnostic_storage)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <type_traits>
#include "common/Diagnostic.h"
#include "common/Result.h"
// common_diagnostic_storage.cpp
namespace test_common_diagnostic_storage {
static_assert(std::is_nothrow_move_constructible_v<eve::Status>);
static_assert(std::is_nothrow_move_assignable_v<eve::Status>);
static_assert(std::is_nothrow_default_constructible_v<eve::Status>);

TEST_CASE("common.diagnosticStorage.omittedAndExplicitDetailsMatch") {
    using namespace eve;
    Diagnostic simple(DiagnosticCode::InvalidArgument, Severity::Warning, "bad", "field");
    Diagnostic explicitEmpty(DiagnosticCode::InvalidArgument, Severity::Warning, "bad", "field", {});
    CHECK(simple.code() == explicitEmpty.code());
    CHECK(simple.severity() == explicitEmpty.severity());
    CHECK(simple.message() == explicitEmpty.message());
    CHECK(simple.path() == explicitEmpty.path());
    CHECK(simple.details().empty());
    CHECK(simple.source().empty());
    CHECK(Diagnostic::error(DiagnosticCode::Failed, "error").severity() == Severity::Error);
    CHECK(Diagnostic::warning(DiagnosticCode::Failed, "warning", "path").severity() == Severity::Warning);
    auto full = Diagnostic::error(DiagnosticCode::Failed, "error", "path", {{"a", "1"}, {"a", "2"}}, "test");
    REQUIRE(full.details().size() == 2u);
    CHECK(full.details()[1].second == "2");
    CHECK(full.source() == "test");
    full.addDetail("b", "3");
    CHECK(full.details().size() == 3u);
}

TEST_CASE("common.diagnosticStorage.statusCopyAndMoveOwnIndependentDiagnostics") {
    using namespace eve;
    auto status = Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "bad", "field"));
    CHECK(status.code() == StatusCode::Rejected);
    auto copy = status;
    status    = Status::success(StatusCode::Applied);
    CHECK(status.code() == StatusCode::Applied);
    CHECK(copy.describe() == "rejected: bad [field]");
    Status assigned;
    assigned = copy;
    copy     = Status::success();
    CHECK(assigned.diagnostics().size() == 1u);
    Status moved(std::move(assigned));
    CHECK(moved.code() == StatusCode::Rejected);
    Status destination;
    destination = std::move(moved);
    CHECK(destination.describe() == "rejected: bad [field]");
    CHECK(Status(StatusCode::Conflict, {Diagnostic::error(DiagnosticCode::Conflict, "collision")}).code() ==
          StatusCode::Conflict);
    auto result = Result<int>::failure(std::move(destination));
    CHECK(!result.ok());
    CHECK(result.code() == StatusCode::Rejected);
}
}  // namespace test_common_diagnostic_storage
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_common_diagnostic_vector_instances) || \
    defined(EVE_TEST_ORIGIN_diagnostic_value)
#include "PrivateTestScope.h"
#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include <atomic>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include "common/Diagnostic.h"
#include "common/DiagnosticValue.h"
#include "common/StringMapInstances.h"
#include "common/Subscription.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: common_diagnostics
namespace test_common_diagnostics {
// common_diagnostic_vector_instances.cpp
namespace {
namespace common_diagnostic_vector_instances {
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
}  // namespace common_diagnostic_vector_instances
}  // namespace

// diagnostic_value.cpp
namespace {
namespace diagnostic_value {
namespace {
enum class UnregisteredValue { Value };
}

static_assert(eve::DiagnosticValue<eve::Severity>);
static_assert(eve::DiagnosticValue<eve::DiagnosticCode>);
static_assert(eve::DiagnosticValue<eve::StatusCode>);
static_assert(!eve::DiagnosticValue<UnregisteredValue>);

TEST_CASE("common.diagnosticValue.stableNamesAndCodes") {
    CHECK_EQ(eve::diagnosticValueName(eve::Severity::Fatal), std::string_view("fatal"));
    CHECK_EQ(eve::diagnosticValueCode(eve::Severity::Fatal), eve::DiagnosticCode::Failed);
    CHECK_EQ(eve::diagnosticValueName(eve::StatusCode::Conflict), std::string_view("conflict"));
    CHECK_EQ(eve::diagnosticValueCode(eve::StatusCode::Conflict), eve::DiagnosticCode::Conflict);
}
}  // namespace diagnostic_value
}  // namespace
}  // namespace test_common_diagnostics
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ECS) || \
    defined(EVE_TEST_ORIGIN_ecs_container_instances)
#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include <ECS.hpp>
#include <cmath>
#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <type_traits>
#include <vector>
#include "PrivateTestScope.h"
#include "common/ECS.h"
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: common_ecs
namespace test_common_ecs {
// ECS.cpp
namespace {
namespace ECS {
class Node : public ecs::Entity {
public:
    ENTITY(Node, ecs::Entity)

    void release() override { ecs::DestroyEntity(this); }

    struct Position {
        float x = 0;
        float y = 0;
    };
    COMPONENT(Position, position)
};

class Movable : public Node {
public:
    ENTITY(Movable, Node)

    void release() override { ecs::DestroyEntity(this); }

    struct Velocity {
        float dx = 1;
        float dy = 1;
    };
    COMPONENT(Velocity, velocity)
};

[[maybe_unused]] static int countNodes() {
    int  n    = 0;
    auto view = ecs::View<Node, Node::Position>();
    for (auto it = view.begin(); it != view.end(); ++it) ++n;
    return n;
}

TEST_CASE("ECS.basicCreateAndComponent") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Node*    a = Node::create();
    Movable* b = Movable::create();

    a->position()->x = 10;
    a->position()->y = 0;

    b->position()->x  = 0;
    b->position()->y  = 10;
    b->velocity()->dx = 2;
    b->velocity()->dy = 2;

    CHECK(std::abs(a->position()->x - 10.f) < 1e-5f);
    CHECK(std::abs(b->position()->y - 10.f) < 1e-5f);
    CHECK(std::abs(b->velocity()->dx - 2.f) < 1e-5f);

    a->release();
    b->release();
}

TEST_CASE("ECS.viewIncludesSubclass") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Node::create();
    Movable::create();

    int  count = 0;
    auto view  = ecs::View<Node, Node::Position>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [pos] = *it;
        pos->x     = 0;
        pos->y     = 0;
        ++count;
    }
    CHECK_EQ(count, 2);
}

TEST_CASE("ECS.dynamicCreateDestroy") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Node*    a       = Node::create();
    Movable* b       = Movable::create();
    a->position()->x = 3.f;
    b->position()->x = 7.f;
    CHECK(ecs::is_entity_visible(a));
    CHECK(ecs::is_entity_visible(b));
    CHECK_EQ(countNodes(), 2);

    ecs::EntityHandle ha     = ecs::handle_of(a);
    const uint32_t    oldGen = a->generation;
    a->release();
    CHECK(!ecs::is_entity_visible(a));
    CHECK(ecs::try_get(ha) == nullptr);
    CHECK_EQ(countNodes(), 1);

    Node* c          = Node::create();
    c->position()->x = 11.f;
    CHECK(ecs::is_entity_visible(c));
    CHECK(ecs::try_get(ha) == nullptr);
    CHECK(ecs::try_get(ecs::handle_of(c)) == c);
    CHECK_EQ(countNodes(), 2);
    if (c == a) CHECK(c->generation != oldGen);

    b->release();
    CHECK_EQ(countNodes(), 1);
    c->release();
    CHECK_EQ(countNodes(), 0);
    CHECK(ecs::try_get(ecs::handle_of(c)) == nullptr);
}

TEST_CASE("ECS.deferredCreateDestroyDuringView") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Node* a          = Node::create();
    a->position()->x = 1.f;
    CHECK_EQ(countNodes(), 1);

    {
        ecs::ScopedDefer defer;
        Node*            spawned = Node::create();
        CHECK((spawned->flags & ecs::kEntityStaging) != 0);
        CHECK_EQ(countNodes(), 1);

        a->release();
        CHECK((a->flags & ecs::kEntityDead) != 0);
        CHECK_EQ(countNodes(), 0);
        // spawned is in the staging table; do not keep the pointer after commit.
    }

    CHECK_EQ(countNodes(), 1);
}

TEST_CASE("ECS.stagingHandleSurvivesCommit") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Node* live                         = Node::create();
    live->position()->x                = 1.f;
    const ecs::EntityHandle liveHandle = ecs::handle_of(live);

    ecs::EntityHandle stagedHandle;
    {
        ecs::ScopedDefer defer;
        Node*            staged = Node::create();
        staged->position()->x   = 11.f;
        stagedHandle            = ecs::handle_of(staged);
        CHECK(stagedHandle.id != liveHandle.id);
        CHECK(ecs::try_get(stagedHandle) == staged);
        CHECK(ecs::try_get(liveHandle) == live);
    }

    ecs::Entity* published = ecs::try_get(stagedHandle);
    CHECK(published != nullptr);
    CHECK(published != live);
    CHECK(ecs::try_get(liveHandle) == live);
    CHECK(std::abs(static_cast<Node*>(published)->position()->x - 11.f) < 1e-5f);
    CHECK_EQ(countNodes(), 2);
}

TEST_CASE("ECS.viewEntityAndRow") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    Node*    a       = Node::create();
    Movable* b       = Movable::create();
    a->position()->x = 3.f;
    b->position()->x = 5.f;

    int   n    = 0;
    float sum  = 0.f;
    auto  view = ecs::View<Node, Node::Position>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        Node* e = it.entity();
        CHECK(e != nullptr);
        auto [rowE, pos] = it.row();
        CHECK(rowE == e);
        CHECK(pos != nullptr);
        sum += pos->x;
        ++n;
    }
    CHECK_EQ(n, 2);
    CHECK(std::abs(sum - 8.f) < 1e-5f);

    {
        auto kill = ecs::View<Node, Node::Position>();
        for (auto it = kill.begin(); it != kill.end(); ++it) {
            if (it.entity()->position()->x == 3.f) ecs::DestroyEntity(it.entity());
        }
    }
    CHECK_EQ(countNodes(), 1);
}

// ---------------------------------------------------------------------------
// Script side: extend C++ entity classes from Squirrel, drive them with a
// script System, and have eve.view() return the C++ entities.
// ---------------------------------------------------------------------------

static const char* kEcsScriptBridgeContent = R"SQ(
function testInheritCppEntityAndAccess() {
    class Enemy extends eve.Node {}
    local e = Enemy()
    if (e == null) return false
    if (!e.isAlive()) return false
    // ids are 0-based registry indices; every live entity has a stable id.
    if (e.getId() < 0) return false

    e.getPos().setX(10.0)
    e.getPos().setY(20.0)
    if (e.getPosX() != 10.0) return false
    if (e.getPosY() != 20.0) return false

    e.setPosX(30.0)
    if (e.getPosX() != 30.0) return false
    return true
}

function testSystemOverCppEntities() {
    class Enemy extends eve.Movable {}
    class MoveSys extends eve.System {
        constructor() { base.constructor(Enemy) }
        function update(dt) {
            foreach (e in entities()) {
                e.setPosX(e.getPosX() + e.getVelX() * dt)
                e.setPosY(e.getPosY() + e.getVelY() * dt)
            }
        }
    }

    local a = Enemy()
    local b = Enemy()
    a.setPosX(0.0)
    a.setVelX(1.0)
    b.setPosX(10.0)
    b.setVelX(2.0)

    local sys = MoveSys()
    sys.update(2.0)
    if (a.getPosX() != 2.0) return false
    if (b.getPosX() != 14.0) return false
    return true
}

function testCppCreatedEntitiesVisible() {
    class Enemy extends eve.Node {}
    local all = eve.view(Enemy)
    if (all.len() < 2) return false
    local found42 = false
    foreach (e in all) {
        if (e.getPosX() == 42.0) found42 = true
        e.setPosX(e.getPosX() + 1.0)
    }
    if (!found42) return false
    return true
}

function testDestroyFromScript() {
    class Enemy extends eve.Node {}
    local e = Enemy()
    if (!e.isAlive()) return false
    e.destroy()
    if (e.isAlive()) return false
    return true
}
)SQ";

namespace {

// Register a C++ ECS entity type so eve.view() (and script Systems built on it)
// can enumerate live instances and hand them to scripts as wrapped instances.
template <typename T>
[[maybe_unused]] void registerCppEntityClassForScript(const ssq::Class& cls) {
    eve::registerCppEntityView(cls, [](ssq::Array& out) {
        HSQUIRRELVM vm = out.getHandle();
        sq_pushobject(vm, out.getRaw());
        ecs::Table* table = ecs::current();
        if (table == nullptr) {
            sq_pop(vm, 1);
            return;
        }
        ecs::IComponentManager& cm  = table->getOrCreateManager<T>();
        auto*                   reg = cm.getOrCreateRegistryComponentBuffer<T>();
        // Depth-first walk of the registry and its subclass/sibling registries,
        // matching ecs::View<T> semantics.
        std::vector<ecs::IComponentBuffer*> stack;
        stack.push_back(reg);
        while (!stack.empty()) {
            ecs::IComponentBuffer* buf = stack.back();
            stack.pop_back();
            auto* r = dynamic_cast<ecs::IRegistryComponentBuffer*>(buf);
            if (r != nullptr) {
                for (uint32_t i = 0; i < r->entity_count(); ++i) {
                    ecs::Entity* ent = r->entity_at(i);
                    if (ent != nullptr && ecs::is_entity_visible(ent)) {
                        ssq::detail::pushByPtr<T>(vm, static_cast<T*>(ent));
                        sq_arrayappend(vm, -2);
                    }
                }
            }
            if (buf->children != nullptr) stack.push_back(buf->children);
            if (buf->next != nullptr) stack.push_back(buf->next);
        }
        sq_pop(vm, 1);
    });
}

class ScriptEcsBridgeFixture {
public:
    ScriptEcsBridgeFixture() : vm(2048, ssq::Libs::ALL) {
        eve::ModuleManager::expose(vm);
        registerCppBindings();
        ssq::Script s = vm.compileSource(kEcsScriptBridgeContent);
        vm.run(s);
    }

protected:
    // Register C++ Node/Movable as inheritable script classes and expose
    // component accessors so scripts can read/write the C++ entities.
    void registerCppBindings() {
        ssq::Table eve(vm.find("eve"));

        auto nodeCls = eve.addClass("Node", std::function<Node*()>([]() -> Node* { return Node::create(); }), false);
        registerCppEntityClassForScript<Node>(nodeCls);
        nodeCls.addFunc("getId", [](Node* self) { return int(self->id); });
        nodeCls.addFunc("isAlive", [](Node* self) { return ecs::is_entity_visible(self); });
        nodeCls.addFunc("destroy", [](Node* self) { self->release(); });
        nodeCls.addFunc("getPosX", [](Node* self) { return self->position()->x; });
        nodeCls.addFunc("getPosY", [](Node* self) { return self->position()->y; });
        nodeCls.addFunc("setPosX", [](Node* self, float v) { self->position()->x = v; });
        nodeCls.addFunc("setPosY", [](Node* self, float v) { self->position()->y = v; });
        nodeCls.addFunc("getPos", [](Node* self) -> Node::Position* { return &*self->position(); });

        auto movableCls =
            eve.addClass("Movable", std::function<Movable*()>([]() -> Movable* { return Movable::create(); }), false);
        registerCppEntityClassForScript<Movable>(movableCls);
        movableCls.addFunc("getId", [](Movable* self) { return int(self->id); });
        movableCls.addFunc("isAlive", [](Movable* self) { return ecs::is_entity_visible(self); });
        movableCls.addFunc("destroy", [](Movable* self) { self->release(); });
        movableCls.addFunc("getPosX", [](Movable* self) { return self->position()->x; });
        movableCls.addFunc("getPosY", [](Movable* self) { return self->position()->y; });
        movableCls.addFunc("setPosX", [](Movable* self, float v) { self->position()->x = v; });
        movableCls.addFunc("setPosY", [](Movable* self, float v) { self->position()->y = v; });
        movableCls.addFunc("getVelX", [](Movable* self) { return self->velocity()->dx; });
        movableCls.addFunc("getVelY", [](Movable* self) { return self->velocity()->dy; });
        movableCls.addFunc("setVelX", [](Movable* self, float v) { self->velocity()->dx = v; });
        movableCls.addFunc("setVelY", [](Movable* self, float v) { self->velocity()->dy = v; });

        // Component structs exposed as script classes so getPos() returns a
        // live view of the C++ component storage.
        auto posCls = eve.addClass(
            "Position", std::function<Node::Position*()>([]() -> Node::Position* { return nullptr; }), false);
        posCls.addFunc("getX", [](Node::Position* self) { return self->x; });
        posCls.addFunc("getY", [](Node::Position* self) { return self->y; });
        posCls.addFunc("setX", [](Node::Position* self, float v) { self->x = v; });
        posCls.addFunc("setY", [](Node::Position* self, float v) { self->y = v; });
    }

    ssq::VM vm;
};

}  // namespace


// ---------------------------------------------------------------------------
// Storage contract.
//
// An entity record only has to be constructible from the arguments it is
// created with, and destructible. An attached component only has to be
// movable. Neither has to be copyable, and a component only has to be
// default constructible when its value is asked for without arguments.
//
// These types are deliberately minimal examples of each shape. They exist so a
// later change cannot quietly put the old requirements back: storing a
// component as std::optional<T>, or deriving the entity record buffer from the
// component slot buffer, both fail to compile here.
// ---------------------------------------------------------------------------

/** @brief Entity root with no default constructor and no copy. */
class StrictNode : public ecs::Entity {
public:
    ENTITY(StrictNode, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }

    explicit StrictNode(int seed) : seed_(seed) {}
    StrictNode(const StrictNode&)                = delete;
    StrictNode& operator=(const StrictNode&)     = delete;
    StrictNode(StrictNode&&) noexcept            = default;
    StrictNode& operator=(StrictNode&&) noexcept = default;

    /** @brief Value passed to create(), proving the ctor argument arrived. */
    [[nodiscard]] int seed() const { return seed_; }

private:
    int seed_ = 0;
};

static_assert(!std::is_default_constructible_v<StrictNode>, "entity root must stay non-default-constructible");
static_assert(!std::is_copy_constructible_v<StrictNode>, "entity root must stay move-only");

/** @brief Component that is move-only but still default constructible. */
struct MoveOnlyComponent {
    float x = 0.f;

    MoveOnlyComponent()                                        = default;
    MoveOnlyComponent(const MoveOnlyComponent&)                = delete;
    MoveOnlyComponent& operator=(const MoveOnlyComponent&)     = delete;
    MoveOnlyComponent(MoveOnlyComponent&&) noexcept            = default;
    MoveOnlyComponent& operator=(MoveOnlyComponent&&) noexcept = default;
};

static_assert(!std::is_copy_constructible_v<MoveOnlyComponent>, "component must stay move-only");

/** @brief Entity root carrying a move-only component. */
class MoveOnlyHolder : public ecs::Entity {
public:
    ENTITY(MoveOnlyHolder, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
    COMPONENT(MoveOnlyComponent, moveOnly)
};

/** @brief Component with no default constructor and no copy. */
struct SeededComponent {
    int value = 0;

    explicit SeededComponent(int v) : value(v) {}
    SeededComponent()                                      = delete;
    SeededComponent(const SeededComponent&)                = delete;
    SeededComponent& operator=(const SeededComponent&)     = delete;
    SeededComponent(SeededComponent&&) noexcept            = default;
    SeededComponent& operator=(SeededComponent&&) noexcept = default;
};

static_assert(!std::is_default_constructible_v<SeededComponent>, "component must stay non-default-constructible");

/** @brief Entity root carrying a component that must be constructed with an argument. */
class SeededHolder : public ecs::Entity {
public:
    ENTITY(SeededHolder, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
    COMPONENT(SeededComponent, seeded)
};

TEST_CASE("ECS.moveOnlyEntityRootAndMoveOnlyComponent") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    // A record whose constructor needs an argument, with copy deleted.
    StrictNode* strict = StrictNode::create(42);
    CHECK_EQ(strict->seed(), 42);

    // The existing first-touch idiom still materialises a component, even
    // though that component is move-only.
    MoveOnlyHolder* holder = MoveOnlyHolder::create();
    holder->moveOnly()->x  = 3.f;
    CHECK(std::abs(holder->moveOnly()->x - 3.f) < 1e-5f);

    strict->release();
    holder->release();
}

TEST_CASE("ECS.nonDefaultConstructibleComponentNeedsExplicitConstruction") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    SeededHolder* seeded = SeededHolder::create();
    SeededHolder* empty  = SeededHolder::create();

    CHECK(!seeded->seeded().has());
    CHECK(!empty->seeded().has());

    seeded->seeded().emplace(7);
    CHECK(seeded->seeded().has());
    CHECK_EQ(seeded->seeded()->value, 7);

    // Nothing may conjure a value for a component that has no default
    // constructor, so the other entity stays empty.
    CHECK(!empty->seeded().has());

    // A View over such a component therefore yields only the entity that
    // really holds a value, rather than materialising one per entity.
    int  seen = 0;
    auto view = ecs::View<SeededHolder, SeededComponent>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [component] = *it;
        CHECK(component != nullptr);
        CHECK_EQ(component->value, 7);
        ++seen;
    }
    CHECK_EQ(seen, 1);

    seeded->release();
    empty->release();
}

TEST_CASE("ECS.deferredPublishTransfersNonDefaultComponent") {
    ecs::Table       world;
    ecs::ScopedTable guard(world);

    {
        ecs::ScopedDefer defer;
        SeededHolder*    staged = SeededHolder::create();
        staged->seeded().emplace(11);
        CHECK((staged->flags & ecs::kEntityStaging) != 0);
        CHECK_EQ(staged->seeded()->value, 11);
    }

    // The record and its component survived publish_from, which is the only
    // caller of transfer_slot.
    int  found = 0;
    auto view  = ecs::View<SeededHolder, SeededComponent>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [component] = *it;
        if (component->value == 11) ++found;
    }
    CHECK_EQ(found, 1);
}
}  // namespace ECS
}  // namespace

// ecs_container_instances.cpp
namespace {
namespace ecs_container_instances {
namespace {
class ContainerBase : public ecs::Entity {
public:
    ENTITY(ContainerBase, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
    struct Position {
        int value = 0;
    };
    COMPONENT(Position, position)
};

class ContainerDerived : public ContainerBase {
public:
    ENTITY(ContainerDerived, ContainerBase)
    struct Detail {
        int value = 0;
    };
    COMPONENT(Detail, detail)
};
}  // namespace

TEST_CASE("ecs.containers_keep_table_isolation_and_stale_handle_detection") {
    ecs::Table first;
    ecs::Table second;
    auto*      a         = ContainerDerived::create(first);
    auto*      b         = ContainerDerived::create(second);
    a->position()->value = 7;
    b->position()->value = 9;
    const auto handle    = ecs::handle_of(a);
    CHECK(ecs::try_get(handle) == a);
    CHECK(first.getManager(typeid(ContainerDerived)) != second.getManager(typeid(ContainerDerived)));
    {
        ecs::ScopedTable scope(second);
        CHECK_EQ(a->position()->value, 7);
        CHECK_EQ(b->position()->value, 9);
    }
    ecs::DestroyEntity(a);
    CHECK(ecs::try_get(handle) == nullptr);
    auto* replacement = ContainerDerived::create(first);
    CHECK_EQ(replacement->id, handle.id);
    CHECK(replacement->generation != handle.generation);
    CHECK(ecs::try_get(ecs::handle_of(replacement)) == replacement);
}

TEST_CASE("ecs.containers_publish_deferred_inherited_components") {
    ecs::Table table;
    {
        ecs::ScopedDefer scope(table);
        auto*            entity   = ContainerDerived::create(table);
        entity->position()->value = 4;
        entity->detail()->value   = 8;
        int visible               = 0;
        for (auto [position] : ecs::View<ContainerBase, ContainerBase::Position>(table)) {
            (void)position;
            ++visible;
        }
        CHECK_EQ(visible, 0);
    }
    int visible = 0;
    for (auto [position, detail] :
         ecs::View<ContainerDerived, ContainerBase::Position, ContainerDerived::Detail>(table)) {
        CHECK_EQ(position->value, 4);
        CHECK_EQ(detail->value, 8);
        ++visible;
    }
    CHECK_EQ(visible, 1);
}

TEST_CASE("ecs.handle_containers_preserve_generation_and_table_projection") {
    static_assert(std::is_aggregate_v<ecs::EntityHandle>);
    ecs::Table                     first;
    ecs::Table                     second;
    auto*                          entity   = ContainerDerived::create(first);
    const auto                     original = ecs::handle_of(entity);
    std::vector<ecs::EntityHandle> handles(64, original);
    auto                           copied = handles;
    handles.front()                       = {&second, std::type_index(typeid(ContainerDerived)), 99, 88};
    CHECK(copied.front().table == &first);
    CHECK(copied.front().id == original.id);
    CHECK(copied.front().generation == original.generation);
    CHECK(ecs::try_get(copied.front()) == entity);
    auto moved = std::move(copied);
    copied.clear();
    copied.push_back({});
    CHECK(copied.front().table == nullptr);
    std::vector<ecs::EntityHandle> assigned;
    assigned = handles;
    CHECK(assigned.front().table == &second);
    assigned = std::move(moved);
    assigned.reserve(512);
    assigned.erase(assigned.begin());
    CHECK(assigned.size() == 63u);
    CHECK(assigned.front().type == std::type_index(typeid(ContainerDerived)));
    ecs::DestroyEntity(entity);
    CHECK(ecs::try_get(assigned.front()) == nullptr);
    auto* replacement = ContainerDerived::create(first);
    CHECK(replacement->id == original.id);
    CHECK(replacement->generation != assigned.front().generation);
    CHECK(ecs::try_get(assigned.front()) == nullptr);
    CHECK(ecs::try_get(ecs::handle_of(replacement)) == replacement);
}
}  // namespace ecs_container_instances
}  // namespace
}  // namespace test_common_ecs
#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_common_identity_version)
#include "PrivateTestScope.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include "common/EventSequence.h"
#include "common/Generation.h"
#include "common/Identity.h"
#include "common/Revision.h"
#include "common/SchemaVersion.h"
#include "common/Time.h"
#include "common/UuidV7.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// common_identity_version.cpp
namespace test_common_identity_version {
static_assert(!std::is_convertible_v<eve::PersistentId, eve::ContentId>);
static_assert(!std::is_convertible_v<eve::PersistentId, std::string>);
static_assert(!std::is_convertible_v<eve::SchemaVersion, std::uint64_t>);
static_assert(!std::is_convertible_v<eve::Generation, eve::Revision>);
static_assert(!std::is_convertible_v<eve::EventSequence, eve::Generation>);
static_assert(!std::is_convertible_v<eve::SimulationTick, eve::FrameIndex>);

TEST_CASE("common.identity.persistentIdRoundTripAndNil") {
    const auto nil = eve::PersistentId::nil();
    CHECK(nil.isNil());
    CHECK_EQ(nil.format(), "00000000-0000-0000-0000-000000000000");

    const auto parsed = eve::PersistentId::parse("01020304-0506-0708-090a-0b0c0d0e0f10");
    REQUIRE(parsed.has_value());
    CHECK(!parsed->isNil());
    CHECK_EQ(parsed->format(), "01020304-0506-0708-090a-0b0c0d0e0f10");
    CHECK(parsed == eve::PersistentId::parse(parsed->format()));
    CHECK(parsed == eve::PersistentId::parse("01020304-0506-0708-090A-0B0C0D0E0F10"));
    CHECK(parsed->hash() != nil.hash());

    CHECK(!eve::PersistentId::parse("01020304-0506-0708-090a-0b0c0d0e0f1").has_value());
    CHECK(!eve::PersistentId::parse("{01020304-0506-0708-090a-0b0c0d0e0f10}").has_value());
    CHECK(!eve::PersistentId::parse("01020304-0506-0708-090a-0b0c0d0e0fzz").has_value());
}

TEST_CASE("common.identity.uuidV7UsesInjectedEntropyAndClock") {
    using namespace std::chrono_literals;
    const auto timestamp = std::chrono::system_clock::time_point{std::chrono::milliseconds{0x010203040506}};

    eve::UuidV7Generator generator(
        [](std::span<std::uint8_t> bytes) {
            for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(0xa0 + i);
            return true;
        },
        [timestamp] { return timestamp; });

    const auto id = generator.generate();
    REQUIRE(id.has_value());
    CHECK_EQ(id->format(), "01020304-0506-70a1-a2a3-a4a5a6a7a8a9");
    const auto bytes = id->bytes();
    CHECK_EQ(bytes[6] & 0xf0u, 0x70u);
    CHECK_EQ(bytes[8] & 0xc0u, 0x80u);

    eve::UuidV7Generator unavailable([](std::span<std::uint8_t>) { return false; }, [timestamp] { return timestamp; });
    CHECK(!unavailable.generate().has_value());

    const auto negative = generator.generate(std::chrono::system_clock::time_point{} - 1ms);
    CHECK(!negative.has_value());
}

TEST_CASE("common.identity.logicalAndContentIdsHaveSeparateBoundaries") {
    const auto logical = eve::LogicalId::parse("rpg:skill.fire");
    REQUIRE(logical.has_value());
    CHECK(logical->isValid());
    CHECK_EQ(logical->namespaceName(), "rpg");
    CHECK_EQ(logical->name(), "skill.fire");
    CHECK_EQ(logical->format(), "rpg:skill.fire");
    CHECK(eve::LogicalId::fromParts("rpg", "skill.fire") == logical);

    CHECK(!eve::LogicalId::parse("rpg").has_value());
    CHECK(!eve::LogicalId::parse("rpg:").has_value());
    CHECK(!eve::LogicalId::parse("rpg:skill:fire").has_value());
    CHECK(!eve::LogicalId::parse("rpg/skill:fire").has_value());

    const std::array<std::uint8_t, 16> digest{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    const auto                         content = eve::ContentId::fromBytes(digest);
    REQUIRE(content.has_value());
    CHECK(!content->isNil());
    CHECK_EQ(content->format(), "00010203-0405-0607-0809-0a0b0c0d0e0f");
    CHECK(content == eve::ContentId::parse(content->format()));
    CHECK(!eve::ContentId::fromBytes(std::span<const std::uint8_t>(digest).first(15)).has_value());

    std::unordered_map<eve::ContentId, int> values;
    values.emplace(*content, 42);
    CHECK_EQ(values.at(*content), 42);
}

TEST_CASE("common.versionTypesDoNotWrapOrMixDomains") {
    const eve::SchemaVersion schemaVersion(7);
    const eve::Generation    generation(7);
    CHECK_EQ(schemaVersion.value(), 7u);
    CHECK_EQ(generation.value(), 7u);
    CHECK(schemaVersion != eve::SchemaVersion(8));
    CHECK(generation < eve::Generation(8));

    const auto next = schemaVersion.incremented();
    REQUIRE(next.has_value());
    CHECK_EQ(next->value(), 8u);

    const eve::Revision maximum(std::numeric_limits<std::uint64_t>::max());
    CHECK(!maximum.incremented().has_value());

    const eve::EventSequence sequence(3);
    const eve::FrameIndex    frame(4);
    CHECK_EQ(sequence.value(), 3u);
    CHECK_EQ(frame.value(), 4u);

    std::unordered_map<eve::SimulationTick, eve::FrameIndex> frames;
    frames.emplace(eve::SimulationTick(12), eve::FrameIndex(24));
    CHECK_EQ(frames.at(eve::SimulationTick(12)).value(), 24u);
}
}  // namespace test_common_identity_version
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_common_result_assertion)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include <iostream>
#include <sstream>
#include <string>
#include "common/Result.h"
// common_result_assertion.cpp
namespace test_common_result_assertion {
namespace {
struct AssertionOutput {
    std::ostringstream text;
    std::streambuf*    previous = std::cerr.rdbuf(text.rdbuf());
    ~AssertionOutput() { restore(); }
    void restore() {
        if (previous) {
            std::cerr.rdbuf(previous);
            previous = nullptr;
        }
    }
};
}  // namespace


TEST_CASE("common.result.assertionPreservesSourceAndExpression") {
#if !defined(ZEROERR_NO_ASSERT)
    bool            caught = false;
    AssertionOutput output;
    const unsigned  line = __LINE__ + 2;
    try {
        eve::detail::checkResultInvariant(false, "originalPredicate", __FILE__, line, "original context");
    } catch (const zeroerr::AssertionData& failure) {
        caught = true;
        CHECK(std::string(failure.file) == __FILE__);
        CHECK(failure.line == line);
        CHECK(failure.cond == "originalPredicate");
        CHECK(!failure.passed);
        CHECK(failure.message.find("false") != std::string::npos);
    }
    output.restore();
    CHECK(caught);
    CHECK(output.text.str().find("original context") != std::string::npos);
#else
    CHECK(true);
#endif
}

TEST_CASE("common.result.expectPreservesFailureAndHeaderLocation") {
#if !defined(ZEROERR_NO_ASSERT)
    bool            caught = false;
    AssertionOutput output;
    auto result = eve::Result<int>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, "broken input"));
    try {
        (void)std::move(result).expect("operation context");
    } catch (const zeroerr::AssertionData& failure) {
        caught = true;
        CHECK(std::string(failure.file).find("Result.h") != std::string::npos);
        CHECK(failure.line > 0);
        CHECK(failure.cond == "false");
        CHECK(!failure.passed);
    }
    output.restore();
    CHECK(caught);
    CHECK(output.text.str().find("operation context") != std::string::npos);
    CHECK(output.text.str().find("broken input") != std::string::npos);
#else
    CHECK(true);
#endif
}
}  // namespace test_common_result_assertion
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_value) || defined(EVE_TEST_ORIGIN_value_lifecycle)
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <locale>
#include <random>
#include <sstream>
#include <type_traits>
#include "PrivateTestScope.h"
#include "common/Json.h"
#include "common/Value.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: common_values
namespace test_common_values {
// value.cpp
namespace {
namespace value {
using eve::Value;

TEST_CASE("value.json_round_trip_preserves_all_supported_kinds") {
    Value::Array array;
    array.emplace_back();
    array.emplace_back(true);
    array.emplace_back(std::int64_t{-7});
    array.emplace_back(1.25);
    array.emplace_back("line\n中🚀");

    Value value(Value::Object{{"z", "last"}, {"a", Value(std::move(array))}});
    auto  encoded = value.toJson();
    REQUIRE(encoded.ok());
    CHECK_EQ(encoded.value(), R"({"a":[null,true,-7,1.25,"line\n中🚀"],"z":"last"})");

    auto parsed = Value::fromJson(encoded.value());
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == value);
}

TEST_CASE("value.object_order_and_numeric_kinds_are_deterministic") {
    auto parsed = Value::fromJson(R"({"z":9223372036854775807,"a":1.0,"negativeZero":-0.0})");
    REQUIRE(parsed.ok());
    const Value::Object* object = parsed.value().getIf<Value::Object>();
    REQUIRE(object != nullptr);
    CHECK(object->find("z")->second.getIf<std::int64_t>() != nullptr);
    CHECK(object->find("a")->second.getIf<double>() != nullptr);
    const double* negativeZero = object->find("negativeZero")->second.getIf<double>();
    REQUIRE(negativeZero != nullptr);
    CHECK(std::signbit(*negativeZero));

    auto encoded = parsed.value().toJson();
    REQUIRE(encoded.ok());
    CHECK_EQ(encoded.value(), R"({"a":1.0,"negativeZero":-0.0,"z":9223372036854775807})");
}

TEST_CASE("value_uses_the_common_json_parser_and_serializer") {
    std::string         error;
    eve::json::Document document =
        eve::json::Document::parse(R"({"large":9223372036854775807,"fraction":1.0})", &error);
    REQUIRE(document.valid());
    CHECK(document.root().get("large").isIntegerLiteral());
    CHECK(document.root().get("large").isInt64());
    CHECK_EQ(document.root().get("large").asInt64(), std::int64_t{9223372036854775807});
    CHECK(!eve::json::Document::parse(R"({"x":1,"x":2})").valid());
    CHECK(!eve::json::Document::parse("Infinity").valid());

    Value value      = Value::fromJson(R"({"z":2,"a":1})").expect("value parse");
    auto  commonJson = eve::json::stringify(value);
    REQUIRE(commonJson.ok());
    CHECK_EQ(commonJson.value(), R"({"a":1,"z":2})");
    auto valueJson = value.toJson();
    REQUIRE(valueJson.ok());
    CHECK_EQ(valueJson.value(), commonJson.value());
}

TEST_CASE("value.rejects_duplicate_keys_invalid_json_and_non_finite_numbers") {
    CHECK(!Value::fromJson(R"({"x":1,"x":2})").ok());
    CHECK(!Value::fromJson(R"([1,])").ok());
    CHECK(!Value::fromJson("1e9999").ok());
    CHECK(!Value::fromJson("NaN").ok());

    auto nan = Value(std::numeric_limits<double>::quiet_NaN()).toJson();
    CHECK(!nan.ok());
    CHECK_EQ(static_cast<int>(nan.code()), static_cast<int>(eve::StatusCode::Failed));
    REQUIRE(nan.error() != nullptr);
    CHECK_EQ(static_cast<int>(nan.error()->code()), static_cast<int>(eve::DiagnosticCode::SerializationError));

    auto infinity = Value(std::numeric_limits<double>::infinity()).toJson();
    CHECK(!infinity.ok());
    CHECK_EQ(static_cast<int>(infinity.error()->code()), static_cast<int>(eve::DiagnosticCode::SerializationError));
}

TEST_CASE("presentation_value_is_the_common_value_without_a_second_storage") {
    static_assert(std::is_same_v<eve::Value, eve::Value>);
    eve::Value value(eve::Value::Object{{"count", 4}});
    auto       encoded = value.toJson();
    REQUIRE(encoded.ok());
    auto parsed = eve::Value::fromJson(encoded.value());
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == value);
}

TEST_CASE("value.double_json_keeps_existing_canonical_bytes") {
    const auto matchesLegacy = [&](double number) {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream.precision(std::numeric_limits<double>::max_digits10);
        stream << number;
        std::string expected = stream.str();
        if (expected.find_first_of(".eE") == std::string::npos) expected += ".0";
        auto encoded = Value(number).toJson();
        REQUIRE(encoded.ok());
        CHECK_EQ(encoded.value(), expected);
    };
    constexpr std::array<double, 10> boundaries = {0.0,
                                                   -0.0,
                                                   0.1,
                                                   1.0,
                                                   -1.0,
                                                   1e-5,
                                                   1e20,
                                                   std::numeric_limits<double>::min(),
                                                   std::numeric_limits<double>::max(),
                                                   std::numeric_limits<double>::denorm_min()};
    for (double number : boundaries) matchesLegacy(number);
    std::mt19937_64 random(42);
    for (int index = 0; index < 4096; ++index) {
        const double number = std::bit_cast<double>(random());
        if (std::isfinite(number)) matchesLegacy(number);
    }
}
}  // namespace value
}  // namespace

// value_lifecycle.cpp
namespace {
namespace value_lifecycle {
static_assert(std::is_nothrow_default_constructible_v<eve::Value>);
static_assert(std::is_nothrow_move_constructible_v<eve::Value>);
static_assert(std::is_nothrow_move_assignable_v<eve::Value>);
static_assert(std::is_nothrow_default_constructible_v<eve::Diagnostic>);
static_assert(std::is_nothrow_move_constructible_v<eve::Diagnostic>);
static_assert(std::is_nothrow_move_assignable_v<eve::Diagnostic>);

TEST_CASE("value.lifecycle_copy_move_and_assignment_preserve_ownership") {
    eve::Value original(eve::Value::Object{{"items", eve::Value::Array{1, "two"}}});
    eve::Value copy(original);
    copy.find("items")->pushBack(3);
    CHECK_EQ(original.find("items")->arraySize(), std::size_t{2});
    CHECK_EQ(copy.find("items")->arraySize(), std::size_t{3});
    eve::Value moved(std::move(copy));
    CHECK_EQ(moved.find("items")->arraySize(), std::size_t{3});
    eve::Value assigned("previous string");
    assigned = original;
    CHECK(assigned == original);
    assigned = std::move(moved);
    CHECK_EQ(assigned.find("items")->arraySize(), std::size_t{3});
    auto encoded = assigned.toJson();
    REQUIRE(encoded.ok());
    CHECK_EQ(encoded.value(), std::string(R"({"items":[1,"two",3]})"));
}

TEST_CASE("value.diagnostic_copy_move_preserves_context_and_independent_details") {
    auto original =
        eve::Diagnostic::warning(eve::DiagnosticCode::Conflict, "message", "path", {{"key", "value"}}, "producer");
    eve::Diagnostic copy(original);
    copy.addDetail("second", "detail");
    CHECK_EQ(original.details().size(), std::size_t{1});
    CHECK_EQ(copy.details().size(), std::size_t{2});
    eve::Diagnostic moved(std::move(copy));
    eve::Diagnostic assigned;
    assigned = original;
    CHECK_EQ(assigned.details().size(), std::size_t{1});
    assigned = std::move(moved);
    CHECK_EQ(assigned.details().size(), std::size_t{2});
    CHECK(assigned.code() == eve::DiagnosticCode::Conflict);
    CHECK(assigned.severity() == eve::Severity::Warning);
    CHECK_EQ(assigned.message(), std::string("message"));
    CHECK_EQ(assigned.path(), std::string("path"));
    CHECK_EQ(assigned.source(), std::string("producer"));
}

TEST_CASE("value.initializer_factories_preserve_container_semantics") {
    const auto emptyArray  = eve::Value::array({});
    const auto emptyObject = eve::Value::object({});
    CHECK(emptyArray.isArray());
    CHECK_EQ(emptyArray.arraySize(), std::size_t{0});
    CHECK(emptyObject.isObject());
    CHECK(emptyObject.keys().empty());
    auto source = eve::Value::array({1, "two"});
    auto object = eve::Value::object({{"z", source}, {"a", 3}, {"a", 4}});
    source.pushBack(5);
    CHECK_EQ(object.find("z")->arraySize(), std::size_t{2});
    CHECK_EQ(object.find("a")->asInt(), std::int64_t{3});
    auto encoded = object.toJson();
    REQUIRE(encoded.ok());
    CHECK_EQ(encoded.value(), std::string(R"({"a":3,"z":[1,"two"]})"));
    eve::Value::Object members{{"moved", 7}};
    const auto         moved = eve::Value::object(std::move(members));
    CHECK_EQ(moved.find("moved")->asInt(), std::int64_t{7});
}

TEST_CASE("value.getIfInstances.directConstAndMutableBorrows") {
    using V = eve::Value;
    static_assert(noexcept(std::declval<V&>().getIf<std::string>()));
    static_assert(noexcept(std::declval<const V&>().getIf<std::string>()));
    V text(std::string(4096, 'a'));
    CHECK(static_cast<bool>(text.getIf<std::string>() == static_cast<const V&>(text).getIf<std::string>()));
    text.getIf<std::string>()->append("changed");
    CHECK(static_cast<const V&>(text).getIf<std::string>()->size() == 4103u);
    CHECK(text.getIf<bool>() == nullptr);
    V null;
    CHECK(null.getIf<std::monostate>() != nullptr);
    CHECK(static_cast<const V&>(null).getIf<std::monostate>() != nullptr);
    V boolean(true);
    *boolean.getIf<bool>() = false;
    CHECK(!*static_cast<const V&>(boolean).getIf<bool>());
    V integer(std::int64_t(42));
    *integer.getIf<std::int64_t>() = 43;
    CHECK(*static_cast<const V&>(integer).getIf<std::int64_t>() == 43);
    V number(2.5);
    *number.getIf<double>() = 3.5;
    CHECK(*static_cast<const V&>(number).getIf<double>() == 3.5);
    V array(V::Array{1, "two"});
    array.getIf<V::Array>()->push_back(true);
    CHECK(static_cast<const V&>(array).getIf<V::Array>()->size() == 3u);
    V object(V::Object{{"key", "value"}});
    (*object.getIf<V::Object>())["other"] = 4;
    CHECK(static_cast<const V&>(object).getIf<V::Object>()->size() == 2u);
    auto json = object.toJson();
    REQUIRE(json.ok());
    auto parsed = V::fromJson(json.value());
    REQUIRE(parsed.ok());
    CHECK(parsed.value() == object);
}
}  // namespace value_lifecycle
}  // namespace
}  // namespace test_common_values
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_common_zeroerr_diagnostics)
#include "PrivateTestScope.h"
#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
// common_zeroerr_diagnostics.cpp
namespace test_common_zeroerr_diagnostics {
static_assert(std::is_nothrow_move_constructible_v<zeroerr::ExprResult>);
static_assert(std::is_nothrow_move_assignable_v<zeroerr::ExprResult>);
static_assert(std::is_nothrow_move_constructible_v<zeroerr::TestCase>);
static_assert(std::is_nothrow_move_assignable_v<zeroerr::TestCase>);
static_assert(std::is_nothrow_move_constructible_v<zeroerr::AssertionData>);
static_assert(std::is_nothrow_move_assignable_v<zeroerr::AssertionData>);

TEST_CASE("common.zeroerr.preservesExpressionAndSource") {
    const unsigned         line = __LINE__;
    zeroerr::assert_info   info{zeroerr::assert_level::ZEROERR_FATAL_l, zeroerr::assert_throw::no_throw, false};
    zeroerr::AssertionData data(__FILE__, line, "left == right", info);
    int                    left = 2, right = 3;
    data.setResult(zeroerr::ExpressionDecomposer() << left == right);
    CHECK(!data.passed);
    CHECK(data.line == line);
    CHECK(data.file == std::string(__FILE__));
    CHECK(data.cond == "left == right");
    CHECK(data.message.find("2") != std::string::npos);
    CHECK(data.message.find("3") != std::string::npos);
    CHECK(data.log().find("left == right") != std::string::npos);
    bool caught = false;
    try {
        data();
    } catch (const zeroerr::AssertionData& failure) {
        caught = true;
        CHECK(failure.cond == data.cond);
    }
    CHECK(caught);
}

TEST_CASE("common.zeroerr.preservesAssertionCounters") {
    const auto before = _ZEROERR_TEST_CONTEXT->passed_as;
    CHECK(2 + 3 == 5);
    const auto after = _ZEROERR_TEST_CONTEXT->passed_as;
    CHECK(after == before + 1);
    auto knownFailure = [reporter = &_ZEROERR_TEST_CONTEXT->reporter] {
        zeroerr::TestContext context(*reporter);
        auto*                _ZEROERR_TEST_CONTEXT = &context;
        CHECK(2 == 3);
        return context.warning_as;
    };
    CHECK(knownFailure() == 1);
}

struct CompiledFixture {
    int value = 7;
};
TEST_CASE_FIXTURE(CompiledFixture, "common.zeroerr.preservesFixtureRegistration") { CHECK(value == 7); }

TEST_CASE("common.zeroerr.expressionValueLifecycle") {
    zeroerr::Expression<bool> plain(true);
    zeroerr::ExprResult       booleanResult = plain;
    CHECK(booleanResult.res);
    CHECK(booleanResult.decomp == "true");
    plain.lhs                        = false;
    zeroerr::ExprResult cachedResult = plain;
    CHECK(cachedResult.decomp == "true");
    zeroerr::Expression<bool> described(false, false, std::string("known decomposition"));
    zeroerr::ExprResult       describedResult = described;
    CHECK(!describedResult.res);
    CHECK(describedResult.decomp == "known decomposition");

    zeroerr::ExprResult original(true, "original decomposition");
    zeroerr::ExprResult copied(original);
    original.decomp = "changed";
    CHECK(copied.decomp == "original decomposition");
    zeroerr::ExprResult moved(std::move(copied));
    CHECK(moved.res);
    CHECK(moved.decomp == "original decomposition");
    zeroerr::ExprResult assigned(false);
    assigned = moved;
    CHECK(assigned.decomp == moved.decomp);
    assigned = std::move(moved);
    CHECK(assigned.res);
    copied = zeroerr::ExprResult(false, "reused");
    CHECK(!copied.res);
    CHECK(copied.decomp == "reused");
}

TEST_CASE("common.zeroerr.testMetadataLifecycle") {
    auto               payload = std::make_shared<int>(0);
    std::weak_ptr<int> lifetime(payload);
    {
        zeroerr::TestCase child("child", __FILE__, __LINE__, {});
        zeroerr::TestCase original("original", __FILE__, __LINE__, [payload](zeroerr::TestContext*) { ++*payload; },
                                   {});
        original.subcases.push_back(&child);
        original.decorators.push_back(zeroerr::skip());
        zeroerr::TestCase copied(original);
        original.name = "changed";
        CHECK(copied.name == "original");
        CHECK(copied.subcases == original.subcases);
        CHECK(copied.decorators == original.decorators);
        copied.func(_ZEROERR_TEST_CONTEXT);
        zeroerr::TestCase moved(std::move(copied));
        moved.func(_ZEROERR_TEST_CONTEXT);
        zeroerr::TestCase assigned("empty", __FILE__, __LINE__, {});
        assigned = original;
        assigned.func(_ZEROERR_TEST_CONTEXT);
        assigned = std::move(moved);
        assigned.func(_ZEROERR_TEST_CONTEXT);
        CHECK(assigned.name == "original");
        CHECK(assigned.subcases.front() == &child);
        CHECK(*payload == 4);
        payload.reset();
        CHECK(!lifetime.expired());
    }
    CHECK(lifetime.expired());
}

TEST_CASE("common.zeroerr.assertionExceptionLifecycle") {
    const unsigned         line = __LINE__;
    zeroerr::assert_info   info{zeroerr::assert_level::ZEROERR_FATAL_l, zeroerr::assert_throw::no_throw, false};
    zeroerr::AssertionData original(__FILE__, line, "condition", info);
    original.setResult(zeroerr::ExprResult(false, "decomposition"));
    zeroerr::AssertionData copied(original);
    original.message = "changed";
    CHECK(copied.message == "decomposition");
    zeroerr::AssertionData moved(std::move(copied));
    CHECK(moved.file == std::string(__FILE__));
    CHECK(moved.line == line);
    zeroerr::AssertionData assigned(__FILE__, line, "empty", info);
    assigned.setResult(zeroerr::ExprResult(true));
    assigned = moved;
    CHECK(assigned.message == "decomposition");
    assigned = std::move(moved);
    CHECK(!assigned.passed);
    bool caught = false;
    try {
        assigned();
    } catch (const zeroerr::AssertionData& failure) {
        caught = true;
        CHECK(failure.cond == "condition");
        CHECK(failure.message == "decomposition");
        CHECK(failure.file == std::string(__FILE__));
    }
    CHECK(caught);
    std::unique_ptr<std::exception> erased = std::make_unique<zeroerr::AssertionData>(assigned);
    erased.reset();
}

namespace {
struct OrderedPrintValue {
    int               id;
    std::vector<int>* calls;
    bool              fail = false;
};
std::ostream& operator<<(std::ostream& stream, const OrderedPrintValue& value) {
    value.calls->push_back(value.id);
    if (value.fail) throw std::runtime_error("printing failed");
    return stream << value.id;
}
}  // namespace

TEST_CASE("common.zeroerr.formatPreservesGrammarAndOrder") {
    std::vector<int>  calls;
    OrderedPrintValue first{1, &calls}, second{2, &calls}, unused{3, &calls};
    auto              text = zeroerr::format("{left}/{right}", first, second, unused);
    CHECK(text == "1/2");
    CHECK(calls == std::vector<int>({1, 2, 3}));
    CHECK(zeroerr::format("prefix {unfinished", "ignored") == "prefix ");
    CHECK(zeroerr::format("{{nested} tail", "value") == "value tail");
    CHECK(zeroerr::format("plain text", "unused") == "plain text");
    CHECK(zeroerr::format("{}:{}", "unquoted", 7) == "unquoted:7");
    CHECK(zeroerr::format("{value}", std::string("owned string")) == "owned string");
    CHECK(zeroerr::format("{}", std::string(4096, 'x')) == std::string(4096, 'x'));
    CHECK(zeroerr::format("{}", std::string("a\0b", 3)) == std::string("a\0b", 3));
    calls.clear();
    second.fail = true;
    bool caught = false;
    try {
        (void)zeroerr::format("{}", first, second, unused);
    } catch (const std::runtime_error& error) {
        caught = std::string(error.what()) == "printing failed";
    }
    CHECK(caught);
    CHECK(calls == std::vector<int>({1, 2}));
}

TEST_CASE("common.zeroerr.printerOwnsAndResetsStream") {
    zeroerr::Printer print;
    print.isQuoted   = false;
    print.line_break = "";
    CHECK(print("first").str() == "first");
    print.os.setstate(std::ios::failbit);
    CHECK(print("second").str() == "second");
    print.clear_stream_before_printing = false;
    CHECK(print("third").str() == "secondthird");
    const auto& constPrint = print;
    CHECK(static_cast<std::string>(constPrint) == "secondthird");
    std::ostringstream copied;
    copied << print;
    CHECK(copied.str() == "secondthird");
}

TEST_CASE("common.zeroerr.printerBorrowsExternalStream") {
    std::ostringstream stream;
    {
        zeroerr::Printer print(stream);
        print.isQuoted   = false;
        print.line_break = "";
        print("first");
        print("second");
        CHECK(stream.str() == "firstsecond");
        bool caught = false;
        try {
            (void)print.str();
        } catch (const std::runtime_error& error) {
            caught = std::string(error.what()) == "Printer is not using stringstream";
        }
        CHECK(caught);
    }
    stream << "after destruction";
    CHECK(stream.str() == "firstsecondafter destruction");
}

TEST_CASE("common.zeroerr.printerFormattingModes") {
    zeroerr::Printer print;
    print.indent    = 3;
    print.isCompact = false;
    CHECK(print.tab(2) == "      ");
    print.isCompact = true;
    CHECK(print.tab(2).empty());
    print.isQuoted = true;
    CHECK(std::string(print.quote()) == "\"");
    print.isQuoted = false;
    CHECK(std::string(print.quote()).empty());
    CHECK(zeroerr::Printer::demangle("plain-type-name") == "plain-type-name");
#ifdef __GNUG__
    CHECK(zeroerr::Printer::demangle("i") == "int");
#else
    CHECK(zeroerr::Printer::demangle("i") == "i");
#endif
}
}  // namespace test_common_zeroerr_diagnostics
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_common_zeroerr_registration)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
// common_zeroerr_registration.cpp
namespace test_common_zeroerr_registration {
#if defined(ZEROERR_COMPILED_DIAGNOSTICS)
namespace {
void testCallback(zeroerr::TestContext*) {}
}  // namespace
TEST_CASE("common.zeroerrRegistration.borrowedDecoratorsKeepOrder") {
    zeroerr::Decorator first, second;
    auto metadata = zeroerr::detail::makeTestCaseFromDecorators("probe", "source", 37, testCallback, {&first, &second});
    CHECK(metadata.decorators.size() == 2u);
    CHECK(metadata.decorators.at(0) == &first);
    CHECK(metadata.decorators.at(1) == &second);
    CHECK(metadata.name == "probe");
    CHECK(metadata.file == "source");
    CHECK(metadata.line == 37u);
    CHECK(static_cast<bool>(metadata.func));
    auto copied = metadata;
    metadata.decorators.clear();
    CHECK(copied.decorators.size() == 2u);
    CHECK(copied.decorators.at(0) == &first);
}
TEST_CASE("common.zeroerrRegistration.borrowedNamesBecomeOwned") {
    std::string name("name\0suffix", 11);
    std::string file("file\0suffix", 11);
    auto        metadata = zeroerr::detail::makeTestCaseFromDecorators(name, file, 91, testCallback, {});
    name.assign("changed");
    file.assign("changed");
    CHECK(metadata.name.size() == 11u);
    CHECK(metadata.file.size() == 11u);
    CHECK(metadata.name.substr(5) == "suffix");
    CHECK(metadata.file.substr(5) == "suffix");
    auto transient = zeroerr::detail::makeTestCaseFromDecorators(std::string(4096, 'n'), std::string(4096, 'f'), 92,
                                                                 testCallback, {});
    CHECK(transient.name.size() == 4096u);
    CHECK(transient.file.size() == 4096u);
    CHECK(transient.name.back() == 'n');
    CHECK(transient.file.back() == 'f');
    CHECK(transient.decorators.empty());
}
TEST_CASE("common.zeroerrRegistration.skippedCase", zeroerr::skip()) { CHECK(false); }
#endif

struct ZeroerrRegistrationFixture {
    int state = 73;
};
TEST_CASE_FIXTURE(ZeroerrRegistrationFixture, "common.zeroerrRegistration.fixtureFactory") { CHECK(state == 73); }
}  // namespace test_common_zeroerr_registration
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_json)
#include "PrivateTestScope.h"
#include <string>
#include "common/Json.h"
#include "common/StringMapInstances.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// json.cpp
namespace test_json {
using namespace eve::json;

TEST_CASE("json.parseScalars") {
    Document doc = Document::parse(R"({
      "s": "hello",
      "i": 42,
      "neg": -7,
      "f": 1.5,
      "exp": 2e3,
      "t": true,
      "f2": false,
      "n": null
    })");
    REQUIRE(doc.valid());
    Value r = doc.root();
    CHECK(r.isObject());
    CHECK_EQ(r.size(), 8u);

    CHECK_EQ(r.getString("s"), "hello");
    CHECK_EQ(r.getInt("i"), 42);
    CHECK_EQ(r.getInt("neg"), -7);
    CHECK_EQ(r.getFloat("f"), 1.5f);
    CHECK_EQ(r.getDouble("exp"), 2000.0);
    CHECK(r.getBool("t"));
    CHECK(!r.getBool("f2"));
    CHECK(r.get("n").isNull());

    CHECK(r.isObject());
    CHECK(r.get("s").isString());
    CHECK(r.get("i").isNumber());
    CHECK(r.get("t").isBool());
}

TEST_CASE("json.missingKeysFallBack") {
    Document doc = Document::parse(R"({"a": 1})");
    REQUIRE(doc.valid());
    Value r = doc.root();

    CHECK(!r.has("nope"));
    CHECK_EQ(r.getInt("nope", 9), 9);
    CHECK_EQ(r.getFloat("nope", 2.5f), 2.5f);
    CHECK_EQ(r.getString("nope", "dflt"), "dflt");
    CHECK(r.getBool("nope", true));
    // A missing key yields a null Value, and chaining off it stays safe.
    CHECK(!r.get("nope"));
    CHECK_EQ(r.get("nope").get("deeper").getInt("deepest", 3), 3);
}

TEST_CASE("json.wrongTypeFallsBack") {
    Document doc = Document::parse(R"({"s": "abc", "o": {"k": 1}, "a": [1,2]})");
    REQUIRE(doc.valid());
    Value r = doc.root();

    // A non-numeric string is not a number.
    CHECK_EQ(r.getInt("s", 5), 5);
    // Objects and arrays are not scalars.
    CHECK_EQ(r.getString("o", "dflt"), "dflt");
    CHECK_EQ(r.getInt("a", 7), 7);
    // Indexing a non-array, or out of range, yields null.
    CHECK(!r.get("o").at(0));
    CHECK(!r.get("a").at(2));
}

TEST_CASE("json.lenientScalarConversion") {
    Document doc = Document::parse(R"({"num": "12", "real": "1.25", "n": 3, "b": true})");
    REQUIRE(doc.valid());
    Value r = doc.root();

    // Numeric strings read as numbers, matching the Poco-based helpers this
    // facade replaces.
    CHECK_EQ(r.getInt("num", 0), 12);
    CHECK_EQ(r.getFloat("real", 0.f), 1.25f);
    // Numbers read back as strings.
    CHECK_EQ(r.getString("n"), "3");
    CHECK_EQ(r.getString("b"), "true");
}

TEST_CASE("json.integerExactness") {
    // Values a double cannot represent exactly must still round-trip through
    // getInt, and out-of-range ones fall back rather than wrap.
    Document doc = Document::parse(R"({"big": 2147483647, "over": 4294967296, "trunc": 2.9})");
    REQUIRE(doc.valid());
    Value r = doc.root();

    CHECK_EQ(r.getInt("big", 0), 2147483647);
    CHECK_EQ(r.getInt("over", -1), -1);
    CHECK_EQ(r.getInt("trunc", 0), 2);
}

TEST_CASE("json.arrays") {
    Document doc = Document::parse(R"({
      "tags": ["a", "b", "c"],
      "sizes": [1, 2, 3],
      "coords": [0.5, 1.5],
      "mixed": ["x", 2, true]
    })");
    REQUIRE(doc.valid());
    Value r = doc.root();

    auto tags = r.getStringArray("tags");
    REQUIRE(tags.size() == 3u);
    CHECK_EQ(tags[0], "a");
    CHECK_EQ(tags[2], "c");

    auto sizes = r.getIntArray("sizes");
    REQUIRE(sizes.size() == 3u);
    CHECK_EQ(sizes[1], 2);

    auto coords = r.getFloatArray("coords");
    REQUIRE(coords.size() == 2u);
    CHECK_EQ(coords[0], 0.5f);

    // Mixed arrays stringify elementwise.
    auto mixed = r.getStringArray("mixed");
    REQUIRE(mixed.size() == 3u);
    CHECK_EQ(mixed[1], "2");

    CHECK(r.getStringArray("absent").empty());
    CHECK(r.getIntArray("tags").size() == 3u);  // non-numeric strings -> 0
}

TEST_CASE("json.maps") {
    Document doc = Document::parse(R"({
      "extra": {"faction": "elf", "rank": "3"},
      "counts": {"wood": 5, "ore": 2}
    })");
    REQUIRE(doc.valid());
    Value r = doc.root();

    auto extra = r.getStringMap("extra");
    CHECK_EQ(extra.size(), 2u);
    CHECK_EQ(extra["faction"], "elf");

    auto counts = r.getIntMap("counts");
    CHECK_EQ(counts.size(), 2u);
    CHECK_EQ(counts["wood"], 5);

    CHECK(r.getStringMap("absent").empty());
}

TEST_CASE("json.nestedNavigation") {
    Document doc = Document::parse(R"({
      "effects": [
        {"id": "burn", "modifiers": [{"attribute": "hp", "value": -3}]},
        {"id": "chill"}
      ]
    })");
    REQUIRE(doc.valid());
    Value effects = doc.root().get("effects");
    REQUIRE(effects.isArray());
    CHECK_EQ(effects.size(), 2u);

    Value first = effects.at(0);
    CHECK_EQ(first.getString("id"), "burn");
    Value mods = first.get("modifiers");
    REQUIRE(mods.isArray());
    CHECK_EQ(mods.at(0).getString("attribute"), "hp");
    CHECK_EQ(mods.at(0).getDouble("value"), -3.0);

    // Absent nested arrays report empty rather than failing.
    CHECK_EQ(effects.at(1).get("modifiers").size(), 0u);
}

TEST_CASE("json.keysPreserveDocumentOrder") {
    Document doc = Document::parse(R"({"z": 1, "a": 2, "m": 3})");
    REQUIRE(doc.valid());
    auto keys = doc.root().keys();
    REQUIRE(keys.size() == 3u);
    CHECK_EQ(keys[0], "z");
    CHECK_EQ(keys[1], "a");
    CHECK_EQ(keys[2], "m");
}

TEST_CASE("json.rootArray") {
    Document doc = Document::parse(R"([{"id": "a"}, {"id": "b"}])");
    REQUIRE(doc.valid());
    Value r = doc.root();
    CHECK(r.isArray());
    CHECK(!r.isObject());
    REQUIRE(r.size() == 2u);
    CHECK_EQ(r.at(1).getString("id"), "b");
}

TEST_CASE("json.escapesAndUnicode") {
    Document doc =
        Document::parse(R"({"esc": "a\"b\\c\/d\be\ff\ng\rh\ti", "bmp": "\u5f00\u59cb", "astral": "\ud83d\ude00"})");
    REQUIRE(doc.valid());
    Value r = doc.root();

    CHECK_EQ(r.getString("esc"), "a\"b\\c/d\be\ff\ng\rh\ti");
    // BMP escapes decode to UTF-8.
    CHECK_EQ(r.getString("bmp"), "\xe5\xbc\x80\xe5\xa7\x8b");
    // Surrogate pairs combine into one code point (U+1F600).
    CHECK_EQ(r.getString("astral"), "\xf0\x9f\x98\x80");
}

TEST_CASE("json.malformedInputReportsError") {
    struct Case {
        const char* text;
    } bad[] = {
        {"{"},
        {"{\"a\":}"},
        {"[1,]"},
        {"{\"a\" 1}"},
        {"tru"},
        {"{\"a\":1} x"},
        {"\"unterminated"},
        {""},
        {"+1"},
        {"01"},
        {"1."},
        {"1e"},
        {"1e+"},
        {"1e9999"},
        {"\"raw\nnewline\""},
        {"\"\\ud800\""},
        {"\"\\udc00\""},
        {"\"\\ud800\\u0041\""},
    };
    for (const auto& c : bad) {
        std::string err;
        Document    doc = Document::parse(c.text, &err);
        CHECK(!doc.valid());
        CHECK(!err.empty());
        // An invalid document still answers safely.
        CHECK(!doc.root());
        CHECK_EQ(doc.root().getInt("anything", 4), 4);
    }
}

TEST_CASE("json.nestingDepthIsBounded") {
    std::string atLimit(256, '[');
    atLimit += '0';
    atLimit.append(256, ']');
    CHECK(Document::parse(atLimit).valid());

    std::string overLimit(257, '[');
    overLimit += '0';
    overLimit.append(257, ']');
    std::string error;
    CHECK(!Document::parse(overLimit, &error).valid());
    CHECK(!error.empty());
}

TEST_CASE("json.emptyContainers") {
    Document doc = Document::parse(R"({"o": {}, "a": []})");
    REQUIRE(doc.valid());
    Value r = doc.root();
    CHECK(r.get("o").isObject());
    CHECK_EQ(r.get("o").size(), 0u);
    CHECK(r.get("a").isArray());
    CHECK_EQ(r.get("a").size(), 0u);
    CHECK(r.get("o").keys().empty());
}

TEST_CASE("json.documentIsMovable") {
    Document a = Document::parse(R"({"v": 11})");
    REQUIRE(a.valid());
    Document b = std::move(a);
    CHECK(b.valid());
    CHECK_EQ(b.root().getInt("v"), 11);
}
}  // namespace test_json
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_result)
#include "PrivateTestScope.h"
#include <memory>
#include <string>
#include <type_traits>
#include "common/Result.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// result.cpp
namespace test_result {
using eve::Diagnostic;
using eve::DiagnosticCode;
using eve::Result;
using eve::Severity;
using eve::Status;
using eve::StatusCode;

static_assert(!std::is_copy_constructible_v<Result<int>>);
static_assert(!std::is_copy_assignable_v<Result<int>>);
static_assert(std::is_move_constructible_v<Result<int>>);
static_assert(!std::is_copy_constructible_v<Result<void>>);
static_assert(std::is_convertible_v<Result<int>, Result<const int>>);
static_assert(!std::is_convertible_v<Result<const int>, Result<int>>);
static_assert(std::is_convertible_v<Result<int*>, Result<const int*>>);
static_assert(!std::is_convertible_v<Result<const int*>, Result<int*>>);
static_assert(!std::is_convertible_v<Result<void>, Result<int>>);
static_assert(!std::is_convertible_v<Result<int>, Result<void>>);

TEST_CASE("common.result.successAndObservationAccessors") {
    auto result = Result<int>::success(42);

    CHECK(result.ok());
    CHECK(result.hasValue());
    CHECK(result.code() == StatusCode::Ok);
    CHECK(result.value() == 42);
    CHECK(result.status().isSuccess());
    CHECK(result.diagnostics().empty());
    CHECK(result.error() == nullptr);
}

TEST_CASE("common.result.failureCarriesStructuredDiagnostic") {
    auto diagnostic = Diagnostic::error(DiagnosticCode::InvalidArgument, "width must be positive", "window.width");
    diagnostic.addDetail("received", "0");
    auto result = Result<int>::failure(std::move(diagnostic));

    CHECK(!result.ok());
    CHECK(result.code() == StatusCode::Rejected);
    REQUIRE(result.error() != nullptr);
    CHECK(result.error()->code() == DiagnosticCode::InvalidArgument);
    CHECK(result.error()->severity() == Severity::Error);
    CHECK(result.error()->message() == "width must be positive");
    CHECK(result.error()->path() == "window.width");
    REQUIRE(result.error()->details().size() == 1u);
    CHECK(result.error()->details()[0].first == "received");
    CHECK(result.error()->details()[0].second == "0");
    CHECK(result.status().describe() == "rejected: width must be positive [window.width]");
}

TEST_CASE("common.result.ignoreAndExpectAreExplicitConsumption") {
    Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "best effort cleanup"))
        .ignore("cleanup is best effort");

    const int value = Result<int>::success(7).expect("initialization must succeed");
    CHECK(value == 7);

    CHECK_THROWS((Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "not available"))
                      .expect("feature must be available"),
                  0));
}

TEST_CASE("common.result.unobservedDestructionIsCaughtInDebug") {
#if !defined(ZEROERR_NO_ASSERT)
    CHECK_THROWS((
        [] {
            auto result = Result<int>::success(1);
            (void)result;
        }(),
        0));
#else
    CHECK(true);
#endif
}

TEST_CASE("common.result.moveTransfersObservationResponsibility") {
    auto source = Result<std::unique_ptr<int>>::success(std::make_unique<int>(9));
    auto moved  = std::move(source);
    REQUIRE(moved.ok());
    REQUIRE(moved.value().get() != nullptr);
    CHECK(*moved.value() == 9);

    auto assignedSource = Result<int>::success(11);
    auto assignedTarget = Result<int>::success(13);
    CHECK(assignedTarget.ok());
    assignedTarget = std::move(assignedSource);
    CHECK(assignedTarget.value() == 11);
}

TEST_CASE("common.result.moveAssignmentDoesNotOverwriteUncheckedTarget") {
#if !defined(ZEROERR_NO_ASSERT)
    auto target = Result<int>::success(1);
    auto source = Result<int>::success(2);
    CHECK_THROWS((target = std::move(source), 0));
    target.ignore("retain target after rejected move assignment");
    source.ignore("retain source after rejected move assignment");
#else
    CHECK(true);
#endif
}

TEST_CASE("common.result.compositionPreservesFailureStatus") {
    auto success = Result<int>::success(3).andThen(
        [](int value) { return Result<std::string>::success(std::to_string(value + 1)); });
    REQUIRE(success.ok());
    CHECK(success.value() == "4");

    auto failure   = Result<int>::failure(Diagnostic::error(DiagnosticCode::NotFound, "entity not found"));
    auto recovered = std::move(failure).orElse([&](const Status& status) {
        CHECK(status.code() == StatusCode::NotFound);
        return Result<int>::success(99);
    });
    CHECK(recovered.value() == 99);
}

TEST_CASE("common.result.constConversionConstrainsValue") {
    auto makeConstValue = []() -> Result<const int> { return Result<int>::success(4); };
    auto constValue     = makeConstValue();
    REQUIRE(constValue.ok());
    CHECK(constValue.value() == 4);

    int  storage          = 8;
    auto makeConstPointer = [](int* pointer) -> Result<const int*> { return Result<int*>::success(pointer); };
    auto constPointer     = makeConstPointer(&storage);
    REQUIRE(constPointer.ok());
    CHECK(*constPointer.value() == 8);

    auto assigned = Result<const int>::failure(Diagnostic::error(DiagnosticCode::Failed, "placeholder"));
    CHECK(!assigned.ok());
    assigned = Result<int>::success(11);
    CHECK(assigned.value() == 11);
    assigned = Result<const int>::success(13);
    CHECK(assigned.value() == 13);

    auto makeConstFailure = []() -> Result<const int> {
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::NotFound, "missing"));
    };
    auto failed = makeConstFailure();
    CHECK(!failed.ok());
    CHECK(failed.code() == StatusCode::NotFound);
}

TEST_CASE("common.result.voidStatusAndBoolCheck") {
    auto result = Result<void>::success(Status::success(StatusCode::Applied));
    CHECK(static_cast<bool>(result));
    CHECK(result.code() == StatusCode::Applied);
    result.value();

    auto failed = Result<void>::failure(
        Status::failure(StatusCode::Conflict, Diagnostic::error(DiagnosticCode::Conflict, "revision changed")));
    CHECK(!failed);
    REQUIRE(failed.error() != nullptr);
    CHECK(failed.error()->code() == DiagnosticCode::Conflict);
}

TEST_CASE("common.result.statusCopiesRetainImmutableDiagnosticStorage") {
    auto original = Status::failure(Diagnostic::error(DiagnosticCode::Failed, "retained diagnostic", "test"));
    auto copied   = original;
    original      = Status::success(StatusCode::NoOp);
    CHECK(original.diagnostics().empty());
    REQUIRE(copied.primaryDiagnostic() != nullptr);
    CHECK_EQ(copied.primaryDiagnostic()->message(), "retained diagnostic");
    auto moved = std::move(copied);
    CHECK(moved.isFailure());
    CHECK_EQ(moved.diagnostics().size(), 1u);
}
}  // namespace test_result
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_runtime_handle)
#include "PrivateTestScope.h"
#include <cstdint>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include "common/RuntimeHandle.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// runtime_handle.cpp
namespace test_runtime_handle_unit {
namespace {

struct UiTag {};
struct PhysicsTag {};

using UiHandle      = eve::RuntimeHandle<UiTag>;
using PhysicsHandle = eve::RuntimeHandle<PhysicsTag>;

static_assert(!std::is_convertible_v<UiHandle, std::uint64_t>);
static_assert(!std::is_convertible_v<std::uint64_t, UiHandle>);
static_assert(!std::is_convertible_v<UiHandle, PhysicsHandle>);
static_assert(!std::is_convertible_v<PhysicsHandle, UiHandle>);
static_assert(std::is_constructible_v<UiHandle, std::uint32_t, std::uint32_t>);
static_assert(!std::is_constructible_v<UiHandle, std::uint64_t>);

}  // namespace

TEST_CASE("common.runtimeHandle.invalidAndPackedBoundary") {
    constexpr UiHandle invalid = UiHandle::invalid();
    CHECK(invalid.isInvalid());
    CHECK(!invalid.isValid());
    CHECK_EQ(invalid.index(), UiHandle::invalidIndex);
    CHECK_EQ(invalid.generation(), UiHandle::invalidGeneration);

    constexpr UiHandle handle(7u, 3u);
    CHECK(handle.isValid());
    CHECK_EQ(handle.index(), 7u);
    CHECK_EQ(handle.generation(), 3u);
    CHECK_EQ(UiHandle::fromPacked(handle.packed()), handle);
    CHECK(UiHandle::fromPacked(0).isInvalid());
}

TEST_CASE("common.runtimeHandle.bumpRejectsOverflow") {
    constexpr UiHandle first(4u, 1u);
    const auto         bumped = first.nextGeneration();
    REQUIRE(bumped.has_value());
    CHECK_EQ(bumped->index(), first.index());
    CHECK_EQ(bumped->generation(), 2u);

    const auto fromInvalidGeneration = UiHandle::nextGeneration(0u);
    REQUIRE(fromInvalidGeneration.has_value());
    CHECK_EQ(*fromInvalidGeneration, 1u);

    constexpr auto maximum = std::numeric_limits<UiHandle::generation_type>::max();
    CHECK(!UiHandle::nextGeneration(maximum).has_value());
    CHECK(!UiHandle(4u, maximum).nextGeneration().has_value());
    CHECK(!UiHandle::invalid().nextGeneration().has_value());
}

TEST_CASE("common.runtimeHandle.hashAndTypeIsolation") {
    const UiHandle first(2u, 1u);
    const UiHandle second(2u, 2u);
    CHECK(first != second);

    std::unordered_set<UiHandle> values;
    values.insert(first);
    values.insert(second);
    CHECK_EQ(values.size(), size_t(2));
    CHECK(values.find(first) != values.end());
    CHECK(values.find(second) != values.end());

    const PhysicsHandle other(2u, 1u);
    CHECK(other.isValid());
    CHECK_EQ(other.index(), first.index());
    CHECK_EQ(other.generation(), first.generation());
}
}  // namespace test_runtime_handle_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_script_compiler)
#include "PrivateTestScope.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include "common/BindingContracts.h"
#include "common/Runtime.h"
#include "common/ScriptCompiler.h"
#include "common/ScriptModule.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// script_compiler.cpp
namespace test_script_compiler_unit {
using namespace eve;


TEST_CASE("scriptCompiler.metadataIgnoresCommentedLanguageForms") {
    const auto metadata = script::ScriptCompiler::analyze(
        "// import { ghost } from \"game:/missing.nut\"\n"
        "/* export function hidden() {}\n"
        "persist ignored = 1 */\n"
        "local text = \"// await remains text\"\n"
        "import { answer } from \"game:/real.nut\"\n",
        "game:/comments.nut");
    CHECK_EQ(metadata.imports.size(), size_t(1));
    CHECK_EQ(metadata.imports[0], std::string("game:/real.nut"));
    CHECK(metadata.exports.empty());
    CHECK(metadata.persistRoots.empty());
    CHECK(metadata.awaitLocations.empty());
}


TEST_CASE("scriptCompiler.bindingContractsAreReplaceableAndSorted") {
    script::BindingContractRegistry registry;
    script::BindingContract         camera;
    camera.module      = "camera";
    camera.scriptClass = "CameraController";
    camera.method      = "setYaw";
    camera.parameters.push_back({"yaw", "float", false, std::nullopt, script::ScriptUnit::Radians, {}});
    camera.returnType = "void";
    registry.registerContract(camera);

    const script::BindingContract* found = registry.find("camera/CameraController.setYaw");
    CHECK(found != nullptr);
    CHECK_EQ(found->parameters[0].name, std::string("yaw"));
    CHECK_EQ(static_cast<int>(found->parameters[0].unit), static_cast<int>(script::ScriptUnit::Radians));
    CHECK_EQ(registry.snapshot().size(), size_t(1));
    CHECK(registry.unregisterContract("camera/CameraController.setYaw"));
}

TEST_CASE("scriptCompiler.bindingContractSearchRanksExactMethod") {
    script::BindingContractRegistry registry;
    script::BindingContract         camera;
    camera.module      = "camera";
    camera.scriptClass = "CameraController";
    camera.method      = "setYaw";
    camera.parameters.push_back({"yaw", "float", false, std::nullopt, script::ScriptUnit::Radians, {}});
    camera.returnType      = "void";
    camera.documentationId = "camera.CameraController.setYaw";
    registry.registerContract(camera);

    script::BindingContract other;
    other.module      = "camera";
    other.scriptClass = "CameraController";
    other.method      = "setPitch";
    other.returnType  = "void";
    registry.registerContract(other);

    const auto hits = registry.search("setYaw");
    REQUIRE(hits.size() == size_t(1));
    CHECK_EQ(hits[0].contract.method, std::string("setYaw"));
    CHECK(hits[0].score > 100);
    CHECK(hits[0].contract.toJson().find("\"unit\":\"Radians\"") != std::string::npos);

    const auto filtered = registry.search("set", "camera", "CameraController", 8);
    CHECK_EQ(filtered.size(), size_t(2));
}

TEST_CASE("scriptCompiler.generatedBindingContractsAreSearchable") {
    const auto hits = script::generatedBindingContracts().search("newWorld", {}, {}, 8);
    CHECK(!hits.empty());
    CHECK(hits[0].contract.toJson().find("\"method\":\"") != std::string::npos);
}


TEST_CASE("scriptCompiler.recordsInspectorPropertyMetadata") {
    const script::ScriptMetadata metadata = script::ScriptCompiler::analyze(
        "class CharacterData {\n"
        "  @editor(\"slider\", min: 0, max: 100, step: 1)\n"
        "  @unit(\"hp\")\n"
        "  hp: float = 100.0\n"
        "  @editor(\"combo\")\n"
        "  job: \"warrior\" | \"mage\" | \"rogue\" = \"warrior\"\n"
        "}\n",
        "game:/character.nut");

    CHECK_EQ(metadata.properties.size(), size_t(2));
    const script::ScriptPropertyMetadata& hp = metadata.properties[0];
    CHECK_EQ(hp.name, std::string("hp"));
    CHECK_EQ(hp.attributes.at("editor"), std::string("slider"));
    CHECK_EQ(hp.attributes.at("min"), std::string("0"));
    CHECK_EQ(hp.attributes.at("unit"), std::string("hp"));

    const script::ScriptPropertyMetadata& job = metadata.properties[1];
    CHECK_EQ(job.choices.size(), size_t(3));
    CHECK_EQ(job.attributes.at("options"), std::string("warrior,mage,rogue"));
}


TEST_CASE("scriptCompiler.rawVmBridgeRecordsToolingMetadata") {
    Runtime         runtime(512, ssq::Libs::ALL);
    const char*     source = "local replValue: int = 7\n";
    const SQInteger top    = sq_gettop(runtime.handle());
    const SQRESULT  result = script::ScriptCompiler::compileBuffer(
        runtime.handle(), source, static_cast<SQInteger>(std::strlen(source)), "console_repl.nut", SQTrue);
    CHECK(SQ_SUCCEEDED(result));
    sq_settop(runtime.handle(), top);
    const auto metadata = runtime.scriptCompiler().metadata("console_repl.nut");
    REQUIRE(metadata.has_value());
    CHECK_EQ(metadata->get().symbols.size(), size_t(1));
    CHECK_EQ(metadata->get().symbols[0].name, std::string("replValue"));
}


TEST_CASE("scriptCompiler.validatesConfigModulesWithoutExecution") {
    const auto diagnostics = script::ScriptCompiler::validateProjectConfig(
        "config <- { modules = [\"gfx\", \"physics\", \"typo\"], optionalModules = [\"audio\"] }\n", "game:/config.nut",
        {"gfx", "physics", "audio"}, {"gfx", "audio"});
    CHECK_EQ(diagnostics.size(), size_t(2));
    CHECK_EQ(diagnostics[0].code, std::string("EVE1002"));
    CHECK_EQ(diagnostics[0].related, std::string("physics"));
    CHECK_EQ(diagnostics[1].code, std::string("EVE1001"));
    CHECK_EQ(diagnostics[1].related, std::string("typo"));
}
}  // namespace test_script_compiler_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_script_structure)
#include "PrivateTestScope.h"
#include "common/ScriptCompiler.h"
#include "common/ScriptStructure.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// script_structure.cpp
namespace test_script_structure_unit {
TEST_CASE("script.structure.lexesCallsWithoutCommentOrStringGhosts") {
    auto result = eve::script::analyzeScriptStructure(R"SQ(
        // function ghost() { fake.emit("event.fake"); }
        local text = @"function ghost() { fake.emit(""event.fake""); }";
        function dispatch(envelope: GameEventRecord) {
            if (envelope.getType() != "event.real") return false;
            return handler(log.at(log.size() - 1));
        }
    )SQ");
    REQUIRE(result.diagnostics.empty());
    REQUIRE_EQ(result.functions.size(), size_t(1));
    CHECK_EQ(result.functions[0].name, std::string("dispatch"));
    CHECK_EQ(result.functions[0].parameters[0].type, std::string("GameEventRecord"));
    CHECK_EQ(result.calls.size(), size_t(4));
    bool filter = false;
    for (const auto& call : result.calls) {
        if (call.comparison == "!=") {
            CHECK_EQ(call.comparedValue.value, std::string("event.real"));
            filter = true;
        }
        CHECK_EQ(call.scope, std::string("dispatch"));
    }
    CHECK(filter);
}
TEST_CASE("script.structure.scopesAliasesAndPreservesDynamicExpressions") {
    auto result = eve::script::analyzeScriptStructure(R"SQ(
        persist marker = null
        initialize <- function() {
            local scene = eve.Scene();
            marker = scene.getNodeRefAt("host", "node");
            local event = prefix + dynamicName;
            local target = nodes[index];
        }
    )SQ");
    REQUIRE(result.diagnostics.empty());
    CHECK_EQ(result.functions[0].name, std::string("initialize"));
    bool dynamic = false, local = false, global = false;
    for (const auto& binding : result.bindings) {
        if (binding.name == "event") dynamic = binding.expression.kind == "dynamic";
        if (binding.name == "scene") local = binding.local && binding.scope == "initialize";
        if (binding.name == "marker" && binding.scope == "initialize") global = !binding.local;
    }
    CHECK(dynamic);
    CHECK(local);
    CHECK(global);
    auto compilerMetadata = eve::script::ScriptCompiler::analyze("function retained() { target.run(); }", "unit.nut");
    CHECK_EQ(compilerMetadata.structure.calls.size(), size_t(1));
}
TEST_CASE("script.structure.rejectsMalformedAndExcessiveNesting") {
    CHECK(!eve::script::analyzeScriptStructure("function bad() { emit(\"x\");").diagnostics.empty());
    CHECK(!eve::script::analyzeScriptStructure("/* unfinished").diagnostics.empty());
    CHECK(!eve::script::analyzeScriptStructure(std::string(65, '(') + "x" + std::string(65, ')')).diagnostics.empty());
}
TEST_CASE("script.structure.reflectsFieldsWithoutLeakingClassMethodsOrMembers") {
    auto result = eve::script::analyzeScriptStructure(R"SQ(
        class NPC {
            health: int = 100;
            marker = null;
            function update() { local marker = unknown(); marker.setVisible(true); }
        }
        function update() { return 1; }
    )SQ");
    REQUIRE(result.diagnostics.empty());
    REQUIRE_EQ(result.classes.size(), size_t(1));
    CHECK_EQ(result.functions[0].name, std::string("NPC.update"));
    CHECK_EQ(result.functions[1].name, std::string("update"));
    bool field = false;
    for (const auto& binding : result.bindings)
        if (binding.name == "health") {
            CHECK(binding.local);
            CHECK_EQ(binding.scope, std::string("NPC."));
            CHECK_EQ(binding.declaredType, std::string("int"));
            field = true;
        }
    CHECK(field);
    CHECK_EQ(result.bindings.size(), size_t(3));
}
TEST_CASE("script.structure.parameterDefaultsDoNotDeclareGlobalAliases") {
    auto result = eve::script::analyzeScriptStructure(
        "function callback(marker: SceneNodeRef = null, text = \"x\") { marker.setVisible(true); }");
    REQUIRE(result.diagnostics.empty());
    CHECK(result.bindings.empty());
    CHECK_EQ(result.functions[0].parameters.size(), size_t(2));
}
}  // namespace test_script_structure_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_state_value)
#include "PrivateTestScope.h"
#include <string>
#include <vector>
#include "common/StateValue.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// state_value.cpp
namespace test_state_value {
using namespace eve;

TEST_CASE("stateValue.factoriesAndKinds") {
    CHECK(StateValue::null().isNull());
    CHECK(StateValue::boolean(true).asBool());
    CHECK_EQ(StateValue::integer(42).asInt(), int64_t(42));
    CHECK(StateValue::number(1.5).asDouble() == 1.5);
    CHECK_EQ(StateValue::string("hi").asString(), std::string("hi"));
    CHECK(StateValue::array().isArray());
    CHECK(StateValue::object().isObject());
}

TEST_CASE("stateValue.objectSetFindKeys") {
    StateValue o = StateValue::object();
    o.set("a", StateValue::integer(1));
    o.set("b", StateValue::string("x"));

    const StateValue* a = o.find("a");
    REQUIRE(a != nullptr);
    CHECK_EQ(a->asInt(), int64_t(1));
    CHECK(o.find("missing") == nullptr);

    const std::vector<std::string> keys = o.keys();
    REQUIRE(keys.size() == 2u);
    CHECK_EQ(keys[0], std::string("a"));
    CHECK_EQ(keys[1], std::string("b"));

    // set() replaces in place without reordering or duplicating.
    o.set("a", StateValue::integer(2));
    CHECK_EQ(o.find("a")->asInt(), int64_t(2));
    CHECK_EQ(o.keys().size(), 2u);
}

TEST_CASE("stateValue.arrayOps") {
    StateValue a = StateValue::array();
    a.pushBack(StateValue::integer(10));
    a.pushBack(StateValue::string("v"));

    REQUIRE(a.arraySize() == 2u);
    CHECK_EQ(a.at(0).asInt(), int64_t(10));
    CHECK_EQ(a.at(1).asString(), std::string("v"));
}

TEST_CASE("stateValue.dottedPathGetSet") {
    StateValue o = StateValue::object();
    CHECK(o.setPath("player.level", StateValue::integer(3)));
    const StateValue* level = o.get("player.level");
    REQUIRE(level != nullptr);
    CHECK_EQ(level->asInt(), int64_t(3));

    StateValue items = StateValue::array();
    items.pushBack(StateValue::string("sword"));
    o.set("items", std::move(items));

    const StateValue* first = o.get("items[0]");
    REQUIRE(first != nullptr);
    CHECK_EQ(first->asString(), std::string("sword"));

    CHECK(o.get("player.missing") == nullptr);
    CHECK(o.get("items[5]") == nullptr);
    CHECK(o.get("player.level.x") == nullptr);  // scalar segment is not an object

    // Array element replacement on the final segment.
    CHECK(o.setPath("items[0]", StateValue::string("axe")));
    CHECK_EQ(o.get("items[0]")->asString(), std::string("axe"));

    // Non-object roots cannot take object paths; arrays must be pre-sized.
    StateValue n = StateValue::null();
    CHECK(!n.setPath("a.b", StateValue::integer(1)));
    CHECK(!o.setPath("items[2]", StateValue::integer(1)));
}

TEST_CASE("stateValue.mergeDefaults") {
    StateValue o = StateValue::object();
    o.set("keep", StateValue::integer(1));

    StateValue defaults = StateValue::object();
    defaults.set("keep", StateValue::integer(99));
    defaults.set("new", StateValue::string("d"));
    StateValue nested = StateValue::object();
    nested.set("x", StateValue::boolean(true));
    defaults.set("nested", nested);

    CHECK(o.mergeDefaults(defaults));
    CHECK_EQ(o.find("keep")->asInt(), int64_t(1));  // existing values untouched
    CHECK_EQ(o.find("new")->asString(), std::string("d"));
    const StateValue* n = o.find("nested");
    REQUIRE(n != nullptr);
    REQUIRE(n->isObject());
    CHECK(n->find("x")->asBool());

    // Merging into a non-object is a no-op; a second merge adds nothing.
    CHECK(!StateValue::integer(1).mergeDefaults(defaults));
    CHECK(!o.mergeDefaults(defaults));
}

TEST_CASE("stateValue.equality") {
    CHECK(StateValue::integer(1) == StateValue::integer(1));
    CHECK(StateValue::integer(1) != StateValue::integer(2));

    StateValue a = StateValue::object();
    a.set("k", StateValue::string("v"));
    StateValue b = StateValue::object();
    b.set("k", StateValue::string("v"));
    CHECK(a == b);
    b.set("k", StateValue::string("w"));
    CHECK(a != b);

    CHECK(StateValue::null() == StateValue::null());
}
}  // namespace test_state_value
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_string_instances)
#include "PrivateTestScope.h"
#include <zeroerr/unittest.h>
#include "common/Result.h"
#include "common/Value.h"
// string_instances.cpp
namespace test_string_instances {
TEST_CASE("common.stringInstances.crossBoundaryGrowthCopyAndMove") {
    std::string input(8192, 'x');
    eve::Value  value(input);
    input.assign("changed");
    CHECK(value.asString().size() == 8192u);
    std::string output(value.asString());
    output.append(4096, 'y');
    CHECK(output.size() == 12288u);
    CHECK(value.asString().size() == 8192u);
    eve::Value moved(std::move(output));
    CHECK(moved.asString().back() == 'y');
    output.assign("reused");
    CHECK(output == "reused");
}

TEST_CASE("common.stringInstances.statusFormattingRemainsStable") {
    auto status = eve::Status::failure(
        eve::StatusCode::Rejected, eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "bad value", "field"));
    CHECK(status.describe() == "rejected: bad value [field]");
    CHECK(eve::Status::success().describe() == "ok");
}

TEST_CASE("common.stringInstances.resultFailureRetainsContextAndLocation") {
#if !defined(ZEROERR_NO_ASSERT)
    try {
        eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, "bad value"))
            .expect(std::string_view("context\0ignored", 15));
        CHECK(false);
    } catch (const zeroerr::AssertionData& error) {
        CHECK(std::string(error.file).find("Result.h") != std::string::npos);
        CHECK(error.line > 0u);
        CHECK(error.cond == "false");
        CHECK(error.message == "false");
    }
#else
    CHECK(true);
#endif
}
}  // namespace test_string_instances
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_time_contract)
#include "PrivateTestScope.h"
#include <limits>
#include "common/Time.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// time_contract.cpp
namespace test_time_contract_unit {
namespace {

class ManualTimeSource final : public eve::ITimeSource {
public:
    eve::MonotonicTimestamp monotonicNow() const override { return monotonic_; }
    eve::WallClockTimestamp wallClockNow() const override { return wallClock_; }

    void advance(std::int64_t nanoseconds) {
        monotonic_ = eve::MonotonicTimestamp(monotonic_.nanoseconds() + nanoseconds);
    }

    void setMonotonic(std::int64_t nanoseconds) { monotonic_ = eve::MonotonicTimestamp(nanoseconds); }
    void setWallClock(std::int64_t nanoseconds) { wallClock_ = eve::WallClockTimestamp(nanoseconds); }

private:
    eve::MonotonicTimestamp monotonic_;
    eve::WallClockTimestamp wallClock_;
};

}  // namespace

TEST_CASE("time.durationAndTimestampUseStrongFixedUnits") {
    auto duration = eve::Duration::fromSeconds(1.25);
    REQUIRE(duration.ok());
    CHECK_EQ(duration.value().nanoseconds(), std::int64_t(1250000000));
    CHECK_EQ(duration.value().seconds(), 1.25);

    eve::MonotonicTimestamp later(150);
    auto                    elapsed = later.since(eve::MonotonicTimestamp(50));
    REQUIRE(elapsed.ok());
    CHECK_EQ(elapsed.value().nanoseconds(), std::int64_t(100));

    auto backwards = eve::MonotonicTimestamp(10).since(eve::MonotonicTimestamp(11));
    CHECK(!backwards.ok());

    auto maximum =
        eve::MonotonicTimestamp(std::numeric_limits<std::int64_t>::max()).since(eve::MonotonicTimestamp::zero());
    REQUIRE(maximum.ok());
    CHECK_EQ(maximum.value().nanoseconds(), std::numeric_limits<std::int64_t>::max());

    auto tooWide = eve::MonotonicTimestamp(std::numeric_limits<std::int64_t>::max())
                       .since(eve::MonotonicTimestamp(std::numeric_limits<std::int64_t>::min()));
    CHECK(!tooWide.ok());
    CHECK_EQ(tooWide.code(), eve::StatusCode::Failed);
    REQUIRE(tooWide.error() != nullptr);
    CHECK_EQ(tooWide.error()->code(), eve::DiagnosticCode::InvariantViolation);
}

TEST_CASE("time.simulationClock.fixedStepPauseAndSlowMotion") {
    ManualTimeSource     source;
    eve::SimulationClock clock(source, eve::Duration::fromNanoseconds(100));

    auto first = clock.sample();
    REQUIRE(first.ok());
    CHECK(first.value().empty());
    CHECK_EQ(clock.currentTick().value(), std::uint64_t(0));
    CHECK_EQ(clock.frameIndex().value(), std::uint64_t(1));

    source.advance(350);
    auto steps = clock.sample();
    REQUIRE(steps.ok());
    CHECK_EQ(steps.value().size(), std::size_t(3));
    CHECK_EQ(steps.value()[0].tick.value(), std::uint64_t(1));
    CHECK_EQ(steps.value()[2].tick.value(), std::uint64_t(3));
    CHECK_EQ(steps.value()[0].delta.nanoseconds(), std::int64_t(100));

    clock.setPaused(true);
    source.advance(1000);
    auto paused = clock.sample();
    REQUIRE(paused.ok());
    CHECK(paused.value().empty());
    CHECK_EQ(clock.currentTick().value(), std::uint64_t(3));

    clock.setPaused(false);
    source.advance(100);
    auto resumed = clock.sample();
    REQUIRE(resumed.ok());
    CHECK_EQ(resumed.value().size(), std::size_t(1));
    CHECK_EQ(resumed.value()[0].tick.value(), std::uint64_t(4));

    clock.reset();
    auto rate = clock.setRate(0.5);
    REQUIRE(rate.ok());
    auto rateStart = clock.sample();
    REQUIRE(rateStart.ok());
    source.advance(250);
    auto slow = clock.sample();
    REQUIRE(slow.ok());
    CHECK_EQ(slow.value().size(), std::size_t(1));
    CHECK_EQ(clock.currentTick().value(), std::uint64_t(1));

    auto badRate = clock.setRate(-1.0);
    CHECK(!badRate.ok());
}

TEST_CASE("time.clockUsesMonotonicOnlyAndWallClockIsMetadata") {
    ManualTimeSource firstSource;
    ManualTimeSource secondSource;
    firstSource.setWallClock(1000);
    secondSource.setWallClock(9000000);
    eve::SimulationClock first(firstSource, eve::Duration::fromNanoseconds(100));
    eve::SimulationClock second(secondSource, eve::Duration::fromNanoseconds(100));

    REQUIRE(first.sample().ok());
    REQUIRE(second.sample().ok());
    firstSource.advance(250);
    secondSource.advance(250);
    auto firstSteps  = first.sample();
    auto secondSteps = second.sample();
    REQUIRE(firstSteps.ok());
    REQUIRE(secondSteps.ok());
    CHECK_EQ(firstSteps.value().size(), secondSteps.value().size());
    CHECK_EQ(first.currentTick(), second.currentTick());
    CHECK_EQ(firstSource.wallClockNow().unixNanoseconds(), std::int64_t(1000));
    CHECK_EQ(secondSource.wallClockNow().unixNanoseconds(), std::int64_t(9000000));
}

TEST_CASE("time.clockRejectsBackwardSourceWithoutStateMutation") {
    ManualTimeSource     source;
    eve::SimulationClock clock(source, eve::Duration::fromNanoseconds(100));
    REQUIRE(clock.sample().ok());
    source.advance(100);
    auto accepted = clock.sample();
    REQUIRE(accepted.ok());
    const auto tick  = clock.currentTick();
    const auto frame = clock.frameIndex();

    source.advance(-200);
    auto rejected = clock.sample();
    CHECK(!rejected.ok());
    CHECK_EQ(clock.currentTick(), tick);
    CHECK_EQ(clock.frameIndex(), frame);
}

TEST_CASE("time.clockRejectsTimestampDistanceOutsideDurationWithoutStateMutation") {
    ManualTimeSource source;
    source.setMonotonic(std::numeric_limits<std::int64_t>::min());
    eve::SimulationClock clock(source, eve::Duration::fromNanoseconds(100));
    REQUIRE(clock.sample().ok());
    const auto tick  = clock.currentTick();
    const auto frame = clock.frameIndex();

    source.setMonotonic(std::numeric_limits<std::int64_t>::max());
    auto rejected = clock.sample();
    CHECK(!rejected.ok());
    REQUIRE(rejected.error() != nullptr);
    CHECK_EQ(rejected.error()->code(), eve::DiagnosticCode::InvariantViolation);
    CHECK_EQ(clock.currentTick(), tick);
    CHECK_EQ(clock.frameIndex(), frame);
}

TEST_CASE("time.clockRejectsExtremeCatchUpWithoutMultiplicationOverflow") {
    ManualTimeSource     source;
    eve::SimulationClock clock(source, eve::Duration::fromNanoseconds(1));
    const auto           beforeTick  = clock.currentTick();
    const auto           beforeFrame = clock.frameIndex();

    auto extreme = clock.advance(eve::Duration::fromNanoseconds(std::numeric_limits<std::int64_t>::max()));
    CHECK(!extreme.ok());
    CHECK_EQ(extreme.code(), eve::StatusCode::Failed);
    REQUIRE(extreme.error() != nullptr);
    CHECK_EQ(extreme.error()->code(), eve::DiagnosticCode::InvariantViolation);
    CHECK_EQ(clock.currentTick(), beforeTick);
    CHECK_EQ(clock.frameIndex(), beforeFrame);

    eve::SimulationClock boundary(source, eve::Duration::fromNanoseconds(std::numeric_limits<std::int64_t>::max()));
    auto oneStep = boundary.advance(eve::Duration::fromNanoseconds(std::numeric_limits<std::int64_t>::max()));
    REQUIRE(oneStep.ok());
    CHECK_EQ(oneStep.value().size(), std::size_t(1));
    CHECK_EQ(boundary.currentTick().value(), std::uint64_t(1));
}
}  // namespace test_time_contract_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_versioned_registry)
#include "PrivateTestScope.h"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include "common/VersionedRegistry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// versioned_registry.cpp
namespace test_versioned_registry_unit {
namespace {

struct Value {
    int             payload;
    eve::Generation generation;

    Value() = delete;
    explicit Value(int value) : payload(value) {}
};

struct EventData {
    eve::SchemaVersion version;
};

static_assert(!std::is_default_constructible_v<Value>);

struct ThrowingValue {
    inline static bool throwOnCopy           = false;
    inline static bool throwOnMoveAssignment = false;

    int             payload;
    eve::Generation generation;

    ThrowingValue() = delete;
    explicit ThrowingValue(int value) : payload(value) {}

    ThrowingValue(const ThrowingValue& other) {
        if (throwOnCopy) throw std::runtime_error("injected value copy failure");
        payload    = other.payload;
        generation = other.generation;
    }

    ThrowingValue& operator=(const ThrowingValue& other) {
        if (throwOnCopy) throw std::runtime_error("injected value assignment failure");
        payload    = other.payload;
        generation = other.generation;
        return *this;
    }

    ThrowingValue(ThrowingValue&&) noexcept = default;

    ThrowingValue& operator=(ThrowingValue&& other) {
        if (throwOnMoveAssignment) throw std::runtime_error("injected value move assignment failure");
        payload    = other.payload;
        generation = other.generation;
        return *this;
    }
};

struct ThrowingEventData {
    inline static bool throwOnCopy      = false;
    inline static int  movesBeforeThrow = -1;

    eve::SchemaVersion version;

    ThrowingEventData() = default;
    explicit ThrowingEventData(eve::SchemaVersion value) : version(value) {}

    ThrowingEventData(const ThrowingEventData& other) {
        if (throwOnCopy) throw std::runtime_error("injected event copy failure");
        version = other.version;
    }

    ThrowingEventData& operator=(const ThrowingEventData& other) {
        if (throwOnCopy) throw std::runtime_error("injected event assignment failure");
        version = other.version;
        return *this;
    }

    ThrowingEventData(ThrowingEventData&& other) {
        if (movesBeforeThrow == 0) throw std::runtime_error("injected event move failure");
        if (movesBeforeThrow > 0) --movesBeforeThrow;
        version = other.version;
    }
    ThrowingEventData& operator=(ThrowingEventData&&) noexcept = default;
};

using Registry              = eve::VersionedRegistry<std::string, Value, EventData>;
using ThrowingValueRegistry = eve::VersionedRegistry<std::string, ThrowingValue, EventData>;
using ThrowingEventRegistry = eve::VersionedRegistry<std::string, Value, ThrowingEventData>;

Registry makeRegistry() {
    return Registry([](Value& value, eve::Generation generation) { value.generation = generation; });
}

}  // namespace

TEST_CASE("versionedRegistry.lifecycleUsesGenerationAndTombstone") {
    auto registry = makeRegistry();

    auto inserted = registry.insert("unit", Value{1}, EventData{eve::SchemaVersion(7)});
    REQUIRE(inserted.ok());
    const auto first = std::move(inserted).takeValue();
    CHECK_EQ(first.generation.value(), uint64_t{1});
    REQUIRE(registry.resolve(first).ok());
    CHECK_EQ(registry.resolve(first).value().get().generation.value(), uint64_t{1});

    auto duplicate = registry.insert("unit", Value{2}, EventData{eve::SchemaVersion(8)});
    CHECK(!duplicate.ok());
    CHECK_EQ(duplicate.code(), eve::StatusCode::Conflict);
    REQUIRE(duplicate.error() != nullptr);
    CHECK_EQ(duplicate.error()->code(), eve::DiagnosticCode::AlreadyExists);

    auto replaced = registry.replace("unit", Value{3}, EventData{eve::SchemaVersion(9)});
    REQUIRE(replaced.ok());
    const auto second = std::move(replaced).takeValue();
    CHECK_EQ(second.generation.value(), uint64_t{2});
    CHECK(registry.isStale(first));
    CHECK(!registry.isStale(second));

    auto removed = registry.remove(std::string("unit"), EventData{eve::SchemaVersion(9)});
    REQUIRE(removed.ok());
    const auto tombstone = std::move(removed).takeValue();
    CHECK_EQ(tombstone.generation.value(), uint64_t{3});
    CHECK(registry.isTombstone("unit"));
    CHECK(registry.isStale(tombstone));
    auto stale = registry.resolve(tombstone);
    CHECK(!stale.ok());
    CHECK_EQ(stale.error()->code(), eve::DiagnosticCode::StaleHandle);

    auto generation = registry.generationOf("unit");
    REQUIRE(generation.ok());
    CHECK_EQ(generation.value().value(), uint64_t{3});

    auto revived = registry.insert("unit", Value{4}, EventData{eve::SchemaVersion(10)});
    REQUIRE(revived.ok());
    const auto fourth = std::move(revived).takeValue();
    CHECK_EQ(fourth.generation.value(), uint64_t{4});
    CHECK(registry.isStale(tombstone));
    CHECK_EQ(registry.size(), std::size_t{1});
    CHECK_EQ(registry.tombstoneCount(), std::size_t{0});
}

TEST_CASE("versionedRegistry.eventsAndStateIncludeTombstones") {
    auto registry = makeRegistry();
    REQUIRE(registry.insert("b", Value{1}, EventData{eve::SchemaVersion(1)}).ok());
    REQUIRE(registry.insert("a", Value{2}, EventData{eve::SchemaVersion(2)}).ok());
    auto removed = registry.remove(std::string("a"), EventData{eve::SchemaVersion(2)});
    REQUIRE(removed.ok());

    CHECK_EQ(registry.eventCount(), std::size_t{3});
    REQUIRE(registry.eventAt(0) != nullptr);
    CHECK_EQ(registry.eventAt(0)->operation, eve::RegistryOperation::Insert);
    CHECK_EQ(registry.eventAt(0)->sequence.value(), uint64_t{1});
    REQUIRE(registry.eventAt(2) != nullptr);
    CHECK(registry.eventAt(2)->isTombstone());
    CHECK_EQ(registry.eventAt(2)->operation, eve::RegistryOperation::Remove);
    CHECK_EQ(registry.eventAt(2)->generation.value(), uint64_t{2});

    const auto image         = registry.snapshotState();
    auto       restored      = makeRegistry();
    auto       restoreResult = restored.restoreState(image);
    REQUIRE(restoreResult.ok());
    CHECK_EQ(restored.size(), registry.size());
    CHECK_EQ(restored.tombstoneCount(), registry.tombstoneCount());
    CHECK(restored.isTombstone("a"));
    CHECK_EQ(restored.eventCount(), registry.eventCount());
    auto restoredValue = restored.resolve("b");
    REQUIRE(restoredValue.ok());
    CHECK_EQ(restoredValue.value().get().generation.value(), uint64_t{1});
    auto restoredGeneration = restored.generationOf("a");
    REQUIRE(restoredGeneration.ok());
    CHECK_EQ(restoredGeneration.value().value(), uint64_t{2});
}

TEST_CASE("versionedRegistry.subscriptionIsCanonicalEventSource") {
    auto                                registry = makeRegistry();
    std::vector<eve::RegistryOperation> operations;
    auto                                subscription =
        registry.subscribe([&](const Registry::Event& event) { operations.push_back(event.operation); });
    REQUIRE(registry.insert("unit", Value{1}, EventData{eve::SchemaVersion(1)}).ok());
    REQUIRE(registry.replace("unit", Value{2}, EventData{eve::SchemaVersion(2)}).ok());
    auto removed = registry.remove(std::string("unit"), EventData{eve::SchemaVersion(2)});
    REQUIRE(removed.ok());
    CHECK_EQ(operations,
             std::vector<eve::RegistryOperation>(
                 {eve::RegistryOperation::Insert, eve::RegistryOperation::Replace, eve::RegistryOperation::Remove}));
    subscription.dispose();
    REQUIRE(registry.insert("other", Value{3}, EventData{eve::SchemaVersion(3)}).ok());
    CHECK_EQ(operations.size(), std::size_t{3});
}

TEST_CASE("versionedRegistry.preparationFailureLeavesCanonicalStateUntouched") {
    ThrowingValue::throwOnCopy = false;
    ThrowingValueRegistry registry;
    auto                  inserted = registry.insert("unit", ThrowingValue{1}, EventData{eve::SchemaVersion(1)});
    REQUIRE(inserted.ok());
    const auto original = std::move(inserted).takeValue();

    ThrowingValue::throwOnCopy = true;
    auto failed                = registry.replace("unit", ThrowingValue{2}, EventData{eve::SchemaVersion(2)});
    ThrowingValue::throwOnCopy = false;

    CHECK(!failed.ok());
    CHECK_EQ(failed.code(), eve::StatusCode::Failed);
    CHECK_EQ(registry.eventCount(), std::size_t{1});
    CHECK(!registry.isStale(original));
    auto value = registry.resolve(original);
    REQUIRE(value.ok());
    CHECK_EQ(value.value().get().payload, 1);

    ThrowingValue::throwOnMoveAssignment = true;
    auto moveAssignmentFailure           = registry.replace("unit", ThrowingValue{3}, EventData{eve::SchemaVersion(3)});
    ThrowingValue::throwOnMoveAssignment = false;
    CHECK(!moveAssignmentFailure.ok());
    CHECK_EQ(moveAssignmentFailure.code(), eve::StatusCode::Failed);
    CHECK_EQ(registry.eventCount(), std::size_t{1});
    auto unchanged = registry.resolve(original);
    REQUIRE(unchanged.ok());
    CHECK_EQ(unchanged.value().get().payload, 1);
}

TEST_CASE("versionedRegistry.eventPreparationFailureLeavesStateUntouched") {
    ThrowingEventRegistry registry;
    ThrowingEventData::throwOnCopy = true;
    auto failed                    = registry.insert("unit", Value{1}, ThrowingEventData{eve::SchemaVersion(1)});
    ThrowingEventData::throwOnCopy = false;

    CHECK(!failed.ok());
    CHECK_EQ(failed.code(), eve::StatusCode::Failed);
    CHECK_EQ(registry.eventCount(), std::size_t{0});
    CHECK_EQ(registry.size(), std::size_t{0});
}

TEST_CASE("versionedRegistry.eventMoveFailureLeavesStateUntouched") {
    ThrowingEventRegistry registry;
    ThrowingEventData     data{eve::SchemaVersion(1)};
    // One move constructs insert's parameter and one constructs mutate's
    // parameter; the third move constructs the candidate event.
    ThrowingEventData::movesBeforeThrow = 2;
    auto failed                         = registry.insert("unit", Value{1}, std::move(data));
    ThrowingEventData::movesBeforeThrow = -1;

    CHECK(!failed.ok());
    CHECK_EQ(failed.code(), eve::StatusCode::Failed);
    CHECK_EQ(registry.eventCount(), std::size_t{0});
    CHECK_EQ(registry.size(), std::size_t{0});
}

TEST_CASE("versionedRegistry.callbackFailureReportsAppliedWithoutRollback") {
    auto                            registry = makeRegistry();
    std::vector<eve::EventSequence> observed;
    auto observer = registry.subscribe([&](const Registry::Event& event) { observed.push_back(event.sequence); });
    auto throwingObserver =
        registry.subscribe([](const Registry::Event&) { throw std::runtime_error("injected observer failure"); });

    auto inserted = registry.insert("unit", Value{1}, EventData{eve::SchemaVersion(1)});
    REQUIRE(inserted.ok());
    CHECK_EQ(inserted.code(), eve::StatusCode::Applied);
    REQUIRE_EQ(inserted.diagnostics().size(), std::size_t{1});
    CHECK_EQ(inserted.diagnostics().front().code(), eve::DiagnosticCode::CallbackFailure);
    CHECK_EQ(observed.size(), std::size_t{1});
    CHECK(registry.contains("unit"));
    CHECK_EQ(registry.eventCount(), std::size_t{1});

    observer.dispose();
    throwingObserver.dispose();
}

TEST_CASE("versionedRegistry.restoreProjectorFailureIsTransactional") {
    auto source = makeRegistry();
    REQUIRE(source.insert("source", Value{1}, EventData{eve::SchemaVersion(1)}).ok());
    const auto image = source.snapshotState();

    bool     failProjection = false;
    Registry target([&](Value& value, eve::Generation generation) {
        value.generation = generation;
        if (failProjection) throw std::runtime_error("injected generation projection failure");
    });
    REQUIRE(target.insert("old", Value{9}, EventData{eve::SchemaVersion(1)}).ok());
    const auto oldState = target.snapshotState();

    failProjection = true;
    auto failed    = target.restoreState(image);
    failProjection = false;

    CHECK(!failed.ok());
    CHECK_EQ(failed.code(), eve::StatusCode::Failed);
    auto oldValue = target.resolve("old");
    REQUIRE(oldValue.ok());
    CHECK_EQ(oldValue.value().get().payload, 9);
    CHECK(!target.contains("source"));
    CHECK_EQ(target.eventCount(), oldState.events.size());
}
}  // namespace test_versioned_registry_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_xml)
#include "PrivateTestScope.h"
#include "common/Xml.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// xml.cpp
namespace test_xml {
using namespace eve::xml;

TEST_CASE("xml.parseAttributesAndChildren") {
    Document doc = Document::parse(R"(<?xml version="1.0"?>
    <tileset columns="4" tilewidth="16" tileheight="16">
      <image source="sheet.png"/>
      <tile id="0">
        <properties>
          <property name="walkable" type="bool" value="false"/>
        </properties>
        <objectgroup>
          <object x="1" y="2" width="3" height="4"/>
        </objectgroup>
      </tile>
      <wangsets>
        <wangset name="terrain">
          <wangtile tileid="0" wangid="1,0,0,0,0,0,0,0"/>
        </wangset>
      </wangsets>
    </tileset>
    )");
    REQUIRE(doc.valid());
    Element root = doc.root();
    CHECK_EQ(root.tagName(), "tileset");
    CHECK_EQ(root.getIntAttribute("columns", 0), 4);
    CHECK_EQ(root.getIntAttribute("tilewidth", 0), 16);

    auto images = root.elementsByTag("image");
    REQUIRE_EQ(images.size(), 1u);
    CHECK_EQ(images[0].getAttribute("source"), "sheet.png");

    auto tiles = root.children("tile");
    REQUIRE_EQ(tiles.size(), 1u);
    CHECK_EQ(tiles[0].getIntAttribute("id", -1), 0);
    CHECK(tiles[0].parent() == root);

    auto props = tiles[0].elementsByTag("property");
    REQUIRE_EQ(props.size(), 1u);
    CHECK_EQ(props[0].getAttribute("name"), "walkable");
    CHECK_EQ(props[0].getAttribute("value"), "false");

    auto objects = tiles[0].elementsByTag("object");
    REQUIRE_EQ(objects.size(), 1u);
    CHECK_EQ(objects[0].getIntAttribute("width", 0), 3);

    auto wang = root.elementsByTag("wangtile");
    REQUIRE_EQ(wang.size(), 1u);
    CHECK_EQ(wang[0].getAttribute("wangid"), "1,0,0,0,0,0,0,0");
}

TEST_CASE("xml.selfClosingAndEntities") {
    Document doc = Document::parse(R"(<root note="a&amp;b&lt;c">
      <child/>
    </root>)");
    REQUIRE(doc.valid());
    CHECK_EQ(doc.root().getAttribute("note"), "a&b<c");
    CHECK_EQ(doc.root().children("child").size(), 1u);
}

TEST_CASE("xml.invalidReturnsEmpty") {
    std::string error;
    Document    doc = Document::parse("<root><unclosed>", &error);
    CHECK(!doc.valid());
    CHECK(!error.empty());
    CHECK(!doc.root());
}
}  // namespace test_xml
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_capability_owned)
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/CapabilityOwned.h"
#include "common/Diagnostic.h"
#include "common/Status.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// capability_owned.cpp
namespace test_capability_owned {
namespace {

/**
 * Minimal capability interface for these cases. `capabilityName` is the only
 * contract the registry needs; the test never depends on a production interface.
 */
class IWidget {
public:
    static constexpr const char* capabilityName = "test.IWidget";
    virtual ~IWidget()                          = default;
    virtual int value() const                   = 0;
};

class Widget final : public IWidget {
public:
    explicit Widget(int initial) : value_(initial) {}
    int value() const override { return value_; }

private:
    int value_ = 0;
};

using Widgets = eve::cap::OwnedProviderRegistry<IWidget>;

constexpr const char* kWidgetId = "widget.main";

/** Each case starts and ends with the caller-owned and owned tables empty. */
struct CapabilityReset {
    CapabilityReset() { eve::cap::detail::clearAllRaw(); }
    ~CapabilityReset() { eve::cap::detail::clearAllRaw(); }
};

eve::DiagnosticCode firstDiagnosticCode(const eve::Status& status) {
    return status.diagnostics().empty() ? eve::DiagnosticCode::None : status.diagnostics().front().code();
}

}  // namespace

TEST_CASE("Capability.ownedAbsentIsUnsupported") {
    CapabilityReset reset;

    auto absent = Widgets::acquire(kWidgetId);
    REQUIRE(!absent.ok());
    CHECK_EQ(firstDiagnosticCode(absent.status()), eve::DiagnosticCode::Unsupported);

    // A handle for an id that was never published is unsupported, not stale.
    auto unknown = Widgets::acquire(eve::cap::OwnedProviderHandle{"widget.never", 1});
    CHECK(!unknown.ok());
    CHECK_EQ(firstDiagnosticCode(unknown.status()), eve::DiagnosticCode::Unsupported);

    // Unloading something that was never published is not a silent success.
    CHECK(!Widgets::unload(eve::cap::OwnedProviderHandle{"widget.never", 1}).ok());
    CHECK(!Widgets::acquire(kWidgetId).ok());
}

TEST_CASE("Capability.ownedPublishAcquireAndUnload") {
    CapabilityReset reset;

    auto published = Widgets::provide(kWidgetId, std::make_shared<Widget>(7));
    REQUIRE(published.ok());
    const eve::cap::OwnedProviderHandle handle = published.value();
    CHECK(handle.id == std::string(kWidgetId));
    CHECK(handle.generation != 0);

    auto lease = Widgets::acquire(handle);
    REQUIRE(lease.ok());
    REQUIRE(lease.value().operator bool());
    CHECK_EQ(lease.value()->value(), 7);
    CHECK(lease.value().handle() == handle);

    // Acquiring by id takes the newest published generation.
    auto newest = Widgets::acquire(kWidgetId);
    REQUIRE(newest.ok());
    CHECK_EQ(newest.value()->value(), 7);

    REQUIRE(Widgets::unload(handle).ok());
    auto retired = Widgets::acquire(handle);
    CHECK(!retired.ok());
    CHECK_EQ(firstDiagnosticCode(retired.status()), eve::DiagnosticCode::Conflict);
    CHECK(!Widgets::acquire(kWidgetId).ok());

    // Retiring twice is a stale handle, not a silent success.
    auto again = Widgets::unload(handle);
    CHECK(!again.ok());
    CHECK_EQ(firstDiagnosticCode(again.status()), eve::DiagnosticCode::Conflict);
}

TEST_CASE("Capability.ownedDuplicatePublishIsRejected") {
    CapabilityReset reset;

    auto first = Widgets::provide(kWidgetId, std::make_shared<Widget>(1));
    REQUIRE(first.ok());

    // An owned provider is replaced only after an explicit unload, so a second
    // publish of the same id cannot silently retire the live generation.
    auto duplicate = Widgets::provide(kWidgetId, std::make_shared<Widget>(2));
    CHECK(!duplicate.ok());
    CHECK_EQ(firstDiagnosticCode(duplicate.status()), eve::DiagnosticCode::Conflict);

    auto current = Widgets::acquire(kWidgetId);
    REQUIRE(current.ok());
    CHECK_EQ(current.value()->value(), 1);
}

TEST_CASE("Capability.ownedRepublishAfterUnloadAdvancesGeneration") {
    CapabilityReset reset;

    auto first = Widgets::provide(kWidgetId, std::make_shared<Widget>(1));
    REQUIRE(first.ok());
    REQUIRE(Widgets::unload(first.value()).ok());

    auto second = Widgets::provide(kWidgetId, std::make_shared<Widget>(2));
    REQUIRE(second.ok());
    CHECK(second.value().generation > first.value().generation);

    // The retired handle stays dead instead of aliasing the republished provider.
    auto stale = Widgets::acquire(first.value());
    CHECK(!stale.ok());
    CHECK_EQ(firstDiagnosticCode(stale.status()), eve::DiagnosticCode::Conflict);

    auto current = Widgets::acquire(kWidgetId);
    REQUIRE(current.ok());
    CHECK_EQ(current.value()->value(), 2);

    // Retiring the retired generation must not retire the live one.
    CHECK(!Widgets::unload(first.value()).ok());
    auto survivor = Widgets::acquire(kWidgetId);
    REQUIRE(survivor.ok());
    CHECK_EQ(survivor.value()->value(), 2);
}

TEST_CASE("Capability.ownedLeaseOutlivesRetirement") {
    CapabilityReset reset;

    auto published = Widgets::provide(kWidgetId, std::make_shared<Widget>(11));
    REQUIRE(published.ok());
    auto lease = Widgets::acquire(published.value());
    REQUIRE(lease.ok());

    REQUIRE(Widgets::unload(published.value()).ok());

    // Retirement stops new acquisitions but does not destroy an outstanding lease.
    CHECK(!Widgets::acquire(published.value()).ok());
    CHECK(lease.value().operator bool());
    CHECK_EQ(lease.value()->value(), 11);
}

TEST_CASE("Capability.ownedRejectsNullProvider") {
    CapabilityReset reset;

    std::shared_ptr<IWidget> empty;
    auto                     rejected = Widgets::provide(kWidgetId, empty);
    REQUIRE(!rejected.ok());
    CHECK_EQ(firstDiagnosticCode(rejected.status()), eve::DiagnosticCode::InvalidArgument);
    CHECK(!Widgets::acquire(kWidgetId).ok());
}

TEST_CASE("Capability.ownedPublishMirrorsTheBorrowedSlot") {
    CapabilityReset reset;

    Widget callerOwned{5};
    eve::cap::provide<IWidget>(&callerOwned);
    CHECK(eve::cap::query<IWidget>() == &callerOwned);

    // Publishing an owned provider also exposes it through the borrowed slot, so
    // existing `query` / `ProviderRef` consumers resolve the same instance.
    auto owned = Widgets::provide(kWidgetId, std::make_shared<Widget>(9));
    REQUIRE(owned.ok());
    IWidget* borrowed = eve::cap::query<IWidget>();
    REQUIRE(borrowed != nullptr);
    CHECK_EQ(borrowed->value(), 9);

    // Unloading revokes the borrowed pointer only while it is still the same one.
    REQUIRE(Widgets::unload(owned.value()).ok());
    CHECK(eve::cap::query<IWidget>() == nullptr);
}

TEST_CASE("Capability.providerRefBindsCallerOwnedProvider") {
    CapabilityReset reset;

    // Absent: an empty reference, not a null dereference.
    auto absent = eve::cap::ProviderRef<IWidget>::bind();
    CHECK(!static_cast<bool>(absent));
    CHECK(absent.get() == nullptr);

    Widget widget{42};
    eve::cap::provide<IWidget>(&widget);

    auto present = eve::cap::ProviderRef<IWidget>::bind();
    REQUIRE(static_cast<bool>(present));
    CHECK(present.get() == &widget);
    CHECK_EQ(present->value(), 42);
}

TEST_CASE("Capability.providerRefIsBindOnceAndMoveOnly") {
    // The type is the fence: it cannot be copied, cannot be produced without an
    // explicit bind, and has no refresh, so a per-frame lookup cannot hide in it.
    static_assert(!std::is_copy_constructible_v<eve::cap::ProviderRef<IWidget>>);
    static_assert(!std::is_copy_assignable_v<eve::cap::ProviderRef<IWidget>>);
    static_assert(std::is_move_constructible_v<eve::cap::ProviderRef<IWidget>>);
    // Storage as a member needs an empty state; only `bind()` names a provider.
    static_assert(std::is_default_constructible_v<eve::cap::ProviderRef<IWidget>>);
    static_assert(std::is_default_constructible_v<eve::cap::OwnedProviderLease<IWidget>>);

    CapabilityReset reset;
    Widget          widget{3};
    eve::cap::provide<IWidget>(&widget);

    auto first  = eve::cap::ProviderRef<IWidget>::bind();
    auto second = std::move(first);
    REQUIRE(second.get() == &widget);
    CHECK(first.get() == nullptr);
}

TEST_CASE("Capability.leaseMovedFromIsEmpty") {
    CapabilityReset reset;

    auto published = Widgets::provide(kWidgetId, std::make_shared<Widget>(21));
    REQUIRE(published.ok());

    auto acquired = Widgets::acquire(published.value());
    REQUIRE(acquired.ok());
    eve::cap::OwnedProviderLease<IWidget> lease = std::move(acquired).value();

    auto moved = std::move(lease);
    CHECK(moved.operator bool());
    CHECK_EQ(moved->value(), 21);
    // The moved-from lease must not keep addressing the old generation.
    CHECK(!lease.operator bool());
    CHECK(lease.handle().id.empty());
    CHECK_EQ(lease.handle().generation, std::uint64_t{0});
}
}  // namespace test_capability_owned
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_condition)
#include <map>
#include <optional>
#include <string>
#include "PrivateTestScope.h"
#include "common/Condition.h"
#include "common/ConditionCodec.h"
#include "common/ConditionKeys.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// condition.cpp
namespace test_condition {
namespace {

class TestContext : public eve::condition::EvaluationContext {
public:
    std::map<std::string, eve::Value>                      values;
    std::map<std::string, eve::condition::ConditionResult> policies;

    [[nodiscard]] std::optional<eve::Value> value(std::string_view key) const override {
        auto it = values.find(std::string(key));
        return it == values.end() ? std::nullopt : std::optional<eve::Value>(it->second);
    }
    [[nodiscard]] std::optional<eve::condition::ConditionResult> policy(std::string_view  name,
                                                                        const eve::Value& arguments) const override {
        (void)arguments;
        auto it = policies.find(std::string(name));
        return it == policies.end() ? std::nullopt : std::optional<eve::condition::ConditionResult>(it->second);
    }
};

}  // namespace

TEST_CASE("decision.condition.supportsAllLeafNodes") {
    using namespace eve::condition;
    TestContext context;
    context.values.emplace("level", 7);
    context.values.emplace(tagKey("fire"), true);
    context.values.emplace(attrKey("power"), 12.0);
    context.values.emplace(resourceKey("mana"), 20.0);
    context.values.emplace(stateKey("phase"), "ready");
    context.policies.emplace("safe", ConditionResult::success(eve::Value(true)));

    const Condition condition = Condition::all({
        Condition::compare("level", CompareOperator::GreaterEqual, 7),
        tagPresent("fire"),
        attributeExists("power"),
        resourceExists("mana"),
        stateEquals("phase", "ready"),
        authorityAllow("cast"),
        Condition::policyCall("safe"),
    });
    class AuthorityContext final : public TestContext {
    public:
        [[nodiscard]] std::optional<ConditionResult> policy(std::string_view  name,
                                                            const eve::Value& arguments) const override {
            if (auto resolved = resolveAuthorityAllow(name, arguments, [](std::string_view scope) {
                    return scope == "cast" ? std::optional<bool>(true) : std::nullopt;
                })) {
                return resolved;
            }
            return TestContext::policy(name, arguments);
        }
    };
    AuthorityContext authContext;
    authContext.values   = context.values;
    authContext.policies = context.policies;
    const auto result    = condition.evaluate(authContext);
    REQUIRE(result.passed());
    CHECK_EQ(static_cast<int>(result.reasonCode()), static_cast<int>(ConditionReasonCode::Passed));
    CHECK(result.details().isObject());
}

TEST_CASE("decision.condition.compoundsExplainFailureWithoutMutation") {
    using namespace eve::condition;
    TestContext context;
    context.values.emplace("level", 2);
    context.values.emplace(tagKey("fire"), false);
    context.values.emplace(stateKey("phase"), "ready");
    const auto before = context.values;

    const auto any = Condition::any({Condition::compare("level", CompareOperator::Greater, 9), tagPresent("fire")});
    const auto anyResult = any.evaluate(context);
    REQUIRE(!anyResult.passed());
    CHECK_EQ(static_cast<int>(anyResult.reasonCode()), static_cast<int>(ConditionReasonCode::NoChildPassed));
    CHECK(anyResult.details().isObject());

    const auto negated   = Condition::not_(stateEquals("phase", "ready"));
    const auto notResult = negated.evaluate(context);
    REQUIRE(!notResult.passed());
    CHECK_EQ(static_cast<int>(notResult.reasonCode()), static_cast<int>(ConditionReasonCode::Negated));
    CHECK_EQ(context.values, before);
}

TEST_CASE("decision.condition.scriptDeclarationIsExplicit") {
    using namespace eve::condition;
    ScriptConditionDeclaration declaration;
    declaration.name         = "game.canCast";
    declaration.dependencies = {"actor.mana", "world.tick"};
    declaration.determinism  = DeterminismLevel::TickDeterministic;
    const auto condition     = Condition::policyCall("game.canCast", eve::Value::Object{{"mode", "safe"}}, declaration);
    REQUIRE(condition.isValid());
    REQUIRE(condition.scriptDeclaration().has_value());
    CHECK_EQ(condition.scriptDeclaration()->name, "game.canCast");
    CHECK_EQ(condition.scriptDeclaration()->dependencies.size(), 2);
    CHECK_EQ(static_cast<int>(condition.scriptDeclaration()->determinism),
             static_cast<int>(DeterminismLevel::TickDeterministic));
}

TEST_CASE("decision.conditionUnavailableIsDifferentFromFalse") {
    using namespace eve::condition;
    TestContext context;
    const auto  tag     = tagPresent("missing").evaluate(context);
    const auto  compare = Condition::compare("missing", CompareOperator::Equal, 1).evaluate(context);
    REQUIRE(!tag.passed());
    REQUIRE(!compare.passed());
    CHECK_EQ(static_cast<int>(tag.reasonCode()), static_cast<int>(ConditionReasonCode::MissingValue));
    CHECK_EQ(static_cast<int>(compare.reasonCode()), static_cast<int>(ConditionReasonCode::MissingValue));
}

TEST_CASE("decision.condition.legacyCodecExpandsDomainLeaves") {
    using namespace eve::condition;
    auto decoded = decodeCondition(eve::Value::Object{{"kind", "has_tag"}, {"key", "fire"}});
    REQUIRE(decoded.ok());
    CHECK_EQ(static_cast<int>(decoded.value().kind()), static_cast<int>(ConditionKind::Compare));
    CHECK_EQ(decoded.value().key(), std::string("tag:fire"));
    auto authority = decodeCondition(eve::Value::Object{{"kind", "authority_check"}, {"key", "cast"}});
    REQUIRE(authority.ok());
    CHECK_EQ(static_cast<int>(authority.value().kind()), static_cast<int>(ConditionKind::PolicyCall));
    CHECK_EQ(authority.value().key(), std::string(kAuthorityAllowPolicy));
}
}  // namespace test_condition
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_sdf_field3d)
#include <cmath>
#include <vector>
#include "PrivateTestScope.h"
#include "common/SdfField3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// sdf_field3d.cpp
namespace test_common_sdf_field3d {
using namespace eve;

namespace {
SdfField3DBlob makeSphereBlob(int resolution = 8) {
    SdfField3DBlob blob;
    blob.kind            = SdfField3DSampleKind::VertexGrid;
    blob.width           = resolution;
    blob.height          = resolution;
    blob.depth           = resolution;
    blob.cellSize        = 0.25f;
    blob.originX         = -1.f;
    blob.originY         = -1.f;
    blob.originZ         = -1.f;
    blob.outsideDistance = 10.f;
    blob.distances.resize(size_t(resolution * resolution * resolution));
    const float radius = 0.5f;
    for (int z = 0; z < resolution; ++z)
        for (int y = 0; y < resolution; ++y)
            for (int x = 0; x < resolution; ++x) {
                const float px = blob.originX + float(x) * blob.cellSize;
                const float py = blob.originY + float(y) * blob.cellSize;
                const float pz = blob.originZ + float(z) * blob.cellSize;
                blob.distances[size_t(x + resolution * (y + resolution * z))] =
                    std::sqrt(px * px + py * py + pz * pz) - radius;
            }
    return blob;
}
}  // namespace

TEST_CASE("common.sdfField3D.blobRoundTripPreservesSamples") {
    const auto blob = makeSphereBlob();
    REQUIRE(validateSdfField3DBlob(blob).ok());
    auto encoded = encodeSdfField3DBlob(blob);
    REQUIRE(encoded.ok());
    auto decoded = decodeSdfField3DBlob(encoded.value());
    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().width == blob.width);
    REQUIRE(decoded.value().height == blob.height);
    REQUIRE(decoded.value().depth == blob.depth);
    REQUIRE(decoded.value().cellSize == blob.cellSize);
    REQUIRE(decoded.value().originX == blob.originX);
    REQUIRE(decoded.value().distances == blob.distances);
}

TEST_CASE("common.sdfField3D.blobRejectsUnknownSchemaAndBadCounts") {
    auto encoded = encodeSdfField3DBlob(makeSphereBlob());
    REQUIRE(encoded.ok());
    auto object = encoded.takeValue();
    object.set("schemaId", std::string("eve.sdf.other"));
    REQUIRE(!decodeSdfField3DBlob(object).ok());

    auto shortBlob = makeSphereBlob();
    shortBlob.distances.pop_back();
    REQUIRE(!validateSdfField3DBlob(shortBlob).ok());
    REQUIRE(!encodeSdfField3DBlob(shortBlob).ok());
}

class AnalyticSphereSdf final : public ISdfField3D {
public:
    float sample(float x, float y, float z) const override { return std::sqrt(x * x + y * y + z * z) - 0.5f; }
    SdfField3DGradientStatus gradient(float x, float y, float z, float& outNx, float& outNy,
                                      float& outNz) const override {
        const float len = std::sqrt(x * x + y * y + z * z);
        if (len <= 1e-8f) return SdfField3DGradientStatus::Undefined;
        outNx = x / len;
        outNy = y / len;
        outNz = z / len;
        return SdfField3DGradientStatus::Defined;
    }
    unsigned revision() const override { return 1; }
    void     bounds(float& minX, float& minY, float& minZ, float& maxX, float& maxY, float& maxZ) const override {
        minX = minY = minZ = -1.f;
        maxX = maxY = maxZ = 1.f;
    }
};

TEST_CASE("common.sdfField3D.capabilityInterfaceSamplesGradient") {
    AnalyticSphereSdf field;
    REQUIRE(std::abs(field.sample(0.f, 0.f, 0.f) + 0.5f) < 1e-5f);
    float nx = 0, ny = 0, nz = 0;
    REQUIRE(field.gradient(1.f, 0.f, 0.f, nx, ny, nz) == SdfField3DGradientStatus::Defined);
    REQUIRE(std::abs(nx - 1.f) < 1e-5f);
    float minX, minY, minZ, maxX, maxY, maxZ;
    field.bounds(minX, minY, minZ, maxX, maxY, maxZ);
    REQUIRE(minX == -1.f);
    REQUIRE(maxZ == 1.f);
    REQUIRE(field.revision() == 1u);
}

}  // namespace test_common_sdf_field3d
#endif
