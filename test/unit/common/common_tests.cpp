// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "cases": {
    "test-framework": [
      "common.zeroerr.preservesExpressionAndSource",
      "common.zeroerr.preservesAssertionCounters",
      "common.zeroerr.preservesFixtureRegistration",
      "common.zeroerr.expressionValueLifecycle",
      "common.zeroerr.testMetadataLifecycle",
      "common.zeroerr.assertionExceptionLifecycle",
      "common.zeroerr.formatPreservesGrammarAndOrder",
      "common.zeroerr.printerOwnsAndResetsStream",
      "common.zeroerr.printerBorrowsExternalStream",
      "common.zeroerr.printerFormattingModes",
      "common.zeroerrRegistration.borrowedDecoratorsKeepOrder",
      "common.zeroerrRegistration.borrowedNamesBecomeOwned",
      "common.zeroerrRegistration.skippedCase",
      "common.zeroerrRegistration.fixtureFactory"
    ]
  },
  "origins": [
    "capability.cpp",
    "protocol_dispatch.cpp",
    "common_diagnostic_storage.cpp",
    "common_diagnostic_vector_instances.cpp",
    "diagnostic_value.cpp",
    "ECS.cpp",
    "ecs_container_instances.cpp",
    "common_identity_version.cpp",
    "common_result_assertion.cpp",
    "value.cpp",
    "value_lifecycle.cpp",
    "common_zeroerr_diagnostics.cpp",
    "common_zeroerr_registration.cpp",
    "json.cpp",
    "result.cpp",
    "runtime_handle.cpp",
    "script_compiler.cpp",
    "script_structure.cpp",
    "state_value.cpp",
    "string_instances.cpp",
    "time_contract.cpp",
    "versioned_registry.cpp",
    "xml.cpp",
    "capability_owned.cpp",
    "condition.cpp",
    "sdf_field3d.cpp",
    "ecs_view_preparation.cpp"
  ]
}
*/
// clang-format on

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_capability) || \
    defined(EVE_TEST_ORIGIN_protocol_dispatch)
#include <initializer_list>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include "PrivateTestScope.h"
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

bool matchesLog(const std::vector<std::string>& actual, std::initializer_list<const char*> expected) {
    if (actual.size() != expected.size()) return false;
    auto cursor = expected.begin();
    for (const auto& value : actual)
        if (value != *cursor++) return false;
    return true;
}

}  // namespace

TEST_CASE("common.capability.spec.servicePublicationAndRevocation") {
    SUB_CASE("empty registry -> null query and zero listeners") {
        Reset reset;
        REQUIRE(eve::cap::query<IGreeter>() == nullptr);
        REQUIRE_EQ(eve::cap::listenerCount<IStep>(), 0u);
        REQUIRE(eve::cap::listenerAt<IStep>(0) == nullptr);
    };
    SUB_CASE("provide A then B -> query B; revoke old A -> B remains") {
        Reset   reset;
        Hello   first;
        Bonjour second;
        eve::cap::provide<IGreeter>(&first);
        REQUIRE(eve::cap::query<IGreeter>() == &first);
        eve::cap::provide<IGreeter>(&second);
        REQUIRE(eve::cap::query<IGreeter>() == &second);
        REQUIRE_EQ(eve::cap::query<IGreeter>()->greet(), "bonjour");
        eve::cap::revoke<IGreeter>(&first);
        REQUIRE(eve::cap::query<IGreeter>() == &second);
        eve::cap::revoke<IGreeter>(&second);
        REQUIRE(eve::cap::query<IGreeter>() == nullptr);
    };
}

TEST_CASE("common.capability.spec.listenerOrderAndSelection") {
    SUB_CASE("priorities 30, 10, 20 -> dispatch in 10, 20, 30 order") {
        Reset     reset;
        NamedStep late("late"), early("early"), middle("middle");
        eve::cap::addListener<IStep>(&late, 30);
        eve::cap::addListener<IStep>(&early, 10);
        eve::cap::addListener<IStep>(&middle, 20);
        std::vector<std::string> visited;
        eve::cap::forEach<IStep>([&](IStep* step) { step->run(visited); });
        REQUIRE(matchesLog(visited, {"early", "middle", "late"}));
    };
    SUB_CASE("equal priority -> insertion order; duplicate registration -> one listener") {
        Reset     reset;
        NamedStep a("a"), b("b");
        eve::cap::addListener<IStep>(&a);
        eve::cap::addListener<IStep>(&b);
        eve::cap::addListener<IStep>(&a);
        REQUIRE_EQ(eve::cap::listenerCount<IStep>(), 2u);
        std::vector<std::string> visited;
        eve::cap::forEach<IStep>([&](IStep* step) { step->run(visited); });
        REQUIRE(matchesLog(visited, {"a", "b"}));
        eve::cap::removeListener<IStep>(&a);
        REQUIRE_EQ(eve::cap::listenerCount<IStep>(), 1u);
        REQUIRE(eve::cap::listenerAt<IStep>(0) == &b);
    };
    SUB_CASE("first matching handler -> stop before later listeners; no match -> false") {
        Reset     reset;
        NamedStep png("png"), json("json"), wav("wav");
        eve::cap::addListener<IStep>(&png);
        eve::cap::addListener<IStep>(&json);
        eve::cap::addListener<IStep>(&wav);
        std::vector<std::string> visited;
        bool                     found = eve::cap::forEachUntil<IStep>([&](IStep* step) {
            step->run(visited);
            return step->handles("json");
        });
        REQUIRE(found);
        REQUIRE(matchesLog(visited, {"png", "json"}));
        REQUIRE(!eve::cap::forEachUntil<IStep>([](IStep* step) { return step->handles("gif"); }));
    };
    SUB_CASE("service and listener capability names -> independent slots") {
        Reset     reset;
        Hello     hello;
        NamedStep step("a");
        eve::cap::provide<IGreeter>(&hello);
        eve::cap::addListener<IStep>(&step);
        REQUIRE(eve::cap::query<IGreeter>() == &hello);
        REQUIRE_EQ(eve::cap::listenerCount<IStep>(), 1u);
        REQUIRE(eve::cap::query<IStep>() == nullptr);
        REQUIRE_EQ(eve::cap::listenerCount<IGreeter>(), 0u);
    };
}

TEST_CASE("common.capability.spec.dispatchMutationAndReentrancy") {
    SUB_CASE("remove future listener during dispatch -> current snapshot visits it, next does not") {
        Reset     reset;
        NamedStep a("a"), b("b");
        eve::cap::addListener<IStep>(&a);
        eve::cap::addListener<IStep>(&b);
        std::vector<std::string> visited;
        eve::cap::forEach<IStep>([&](IStep* step) {
            step->run(visited);
            eve::cap::removeListener<IStep>(&b);
        });
        REQUIRE(matchesLog(visited, {"a", "b"}));
        visited.clear();
        eve::cap::forEach<IStep>([&](IStep* step) { step->run(visited); });
        REQUIRE(matchesLog(visited, {"a"}));
    };
    SUB_CASE("remove current listener -> successor still visited") {
        Reset     reset;
        NamedStep a("a"), b("b"), c("c");
        eve::cap::addListener<IStep>(&a);
        eve::cap::addListener<IStep>(&b);
        eve::cap::addListener<IStep>(&c);
        std::vector<std::string> visited;
        eve::cap::forEach<IStep>([&](IStep* step) {
            step->run(visited);
            if (step == &a) eve::cap::removeListener<IStep>(&a);
        });
        REQUIRE(matchesLog(visited, {"a", "b", "c"}));
        REQUIRE_EQ(eve::cap::listenerCount<IStep>(), 2u);
    };
    SUB_CASE("register during dispatch -> only next dispatch sees new priority") {
        Reset     reset;
        NamedStep a("a"), b("b"), early("early");
        eve::cap::addListener<IStep>(&a, 10);
        eve::cap::addListener<IStep>(&b, 20);
        std::vector<std::string> visited;
        eve::cap::forEach<IStep>([&](IStep* step) {
            step->run(visited);
            if (step == &a) eve::cap::addListener<IStep>(&early, 0);
        });
        REQUIRE(matchesLog(visited, {"a", "b"}));
        visited.clear();
        eve::cap::forEach<IStep>([&](IStep* step) { step->run(visited); });
        REQUIRE(matchesLog(visited, {"early", "a", "b"}));
    };
    SUB_CASE("nested dispatch after registration -> nested view sees current registry") {
        Reset     reset;
        NamedStep a("a"), b("b"), early("early");
        eve::cap::addListener<IStep>(&a, 10);
        eve::cap::addListener<IStep>(&b, 20);
        std::vector<std::string> outer, nested;
        eve::cap::forEach<IStep>([&](IStep* step) {
            step->run(outer);
            if (step == &a) {
                eve::cap::addListener<IStep>(&early, 0);
                eve::cap::forEach<IStep>([&](IStep* current) { current->run(nested); });
            }
        });
        REQUIRE(matchesLog(outer, {"a", "b"}));
        REQUIRE(matchesLog(nested, {"early", "a", "b"}));
    };
}

TEST_CASE("common.capability.spec.borrowedAndOwnedProviderLifetime") {
    SUB_CASE("ProviderRef bind then revoke -> cached borrow remains caller-owned") {
        Reset reset;
        static_assert(!std::is_copy_constructible_v<eve::cap::ProviderRef<IGreeter>>);
        Hello hello;
        eve::cap::provide<IGreeter>(&hello);
        auto ref = eve::cap::ProviderRef<IGreeter>::bind();
        REQUIRE(ref.get() == &hello);
        eve::cap::revoke<IGreeter>(&hello);
        REQUIRE(ref.get() == &hello);
        REQUIRE(eve::cap::query<IGreeter>() == nullptr);
    };
    SUB_CASE("owned publish, acquire, unload -> lease remains valid and handle becomes stale") {
        Reset reset;
        auto  shared      = std::make_shared<Hello>();
        auto  publication = eve::cap::OwnedProviderRegistry<IGreeter>::provide("greeter.main", shared);
        REQUIRE(publication.ok());
        auto handle = std::move(publication).takeValue();
        auto lease  = eve::cap::OwnedProviderRegistry<IGreeter>::acquire(handle);
        REQUIRE(lease.ok());
        REQUIRE_EQ(lease.value()->greet(), "hello");
        REQUIRE(eve::cap::query<IGreeter>() == shared.get());
        REQUIRE(eve::cap::OwnedProviderRegistry<IGreeter>::unload(handle).ok());
        REQUIRE(!eve::cap::OwnedProviderRegistry<IGreeter>::acquire(handle).ok());
        REQUIRE(eve::cap::query<IGreeter>() == nullptr);
        REQUIRE_EQ(lease.value()->greet(), "hello");
    };
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

TEST_CASE("common.diagnosticStorage.spec.detailOwnershipAndMoves") {
    SUB_CASE("diagnostic storage omitted and explicit details -> match") {
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
    };
    SUB_CASE("diagnostic storage status -> copy and move own independent diagnostics") {
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
    };
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
TEST_CASE("common.diagnosticVector.spec.crossLibraryOwnership") {
    SUB_CASE("cross-library diagnostic vector -> owned copy and move lifetime") {
        std::vector<eve::Diagnostic> values;
        values.reserve(128);
        for (unsigned i = 0; i < 64; ++i)
            values.push_back(eve::Diagnostic::error(eve::DiagnosticCode::Failed, std::string(4096, 'a')));
        auto copied  = values;
        copied.at(0) = eve::Diagnostic::warning(eve::DiagnosticCode::Conflict, "changed");
        REQUIRE(values.at(0).message().size() == 4096u);
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
    };
    SUB_CASE("diagnostic details -> stable owned iteration after source mutation") {
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
    };
}


// common_string_map_instances.cpp
namespace {
using AttributeMap = std::unordered_map<std::string, std::string>;
static_assert(std::is_move_constructible_v<AttributeMap>);
static_assert(std::is_copy_constructible_v<AttributeMap>);
}  // namespace

TEST_CASE("common.stringMap.spec.crossLibraryStorageAndTransfer") {
    SUB_CASE("string -> map owning lifecycle across library") {
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
    };
    SUB_CASE("string -> map node transfer and iteration") {
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
    };
}


// common_subscription.cpp
TEST_CASE("common.observer.spec.moveCancellationAndReentrancy") {
    SUB_CASE("subscription -> is move only and cancellation is idempotent") {
        static_assert(!std::is_copy_constructible_v<eve::Subscription>);
        static_assert(!std::is_copy_assignable_v<eve::Subscription>);
        static_assert(std::is_move_constructible_v<eve::Subscription>);
        static_assert(std::is_move_assignable_v<eve::Subscription>);

        int               cancellations = 0;
        eve::Subscription original([&cancellations]() noexcept { ++cancellations; });
        eve::Subscription moved(std::move(original));
        REQUIRE(original.disposed());
        CHECK(!moved.disposed());

        moved.dispose();
        moved.dispose();
        CHECK(moved.disposed());
        CHECK_EQ(cancellations, 1);
    };
    SUB_CASE("observer -> allows reentrant subscribe and dispose") {
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
    };
    SUB_CASE("observer -> does not retain owner after destruction") {
        eve::Subscription subscription;
        {
            eve::Observer<int> observer;
            subscription = observer.subscribe([](const int&) {});
            CHECK_EQ(observer.size(), std::size_t{1});
        }

        CHECK(!subscription.disposed());
        subscription.dispose();
        CHECK(subscription.disposed());
    };
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
#include <type_traits>
#include <vector>
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ECS)
#include <simplesquirrel/simplesquirrel.hpp>
#include "PrivateTestScope.h"
#include "common/ECS.h"
#include "common/Module.h"
#endif
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// Responsibility: common_ecs
namespace test_common_ecs {
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ECS)
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

TEST_CASE("common.ecs.spec.creationViewsAndGenerations") {
    SUB_CASE("base and derived entities -> component values retained") {
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

        REQUIRE(std::abs(a->position()->x - 10.f) < 1e-5f);
        CHECK(std::abs(b->position()->y - 10.f) < 1e-5f);
        CHECK(std::abs(b->velocity()->dx - 2.f) < 1e-5f);

        a->release();
        b->release();
    };
    SUB_CASE("view -> includes subclass") {
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
    };
    SUB_CASE("destroyed entity and reused slot -> stale handle invalid") {
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
    };
}

TEST_CASE("common.ecs.spec.deferredPublicationAndRows") {
    SUB_CASE("create and destroy under defer -> visibility changes on commit") {
        ecs::Table       world;
        ecs::ScopedTable guard(world);

        Node* a          = Node::create();
        a->position()->x = 1.f;
        REQUIRE_EQ(countNodes(), 1);

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
    };
    SUB_CASE("staging -> handle survives commit") {
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
    };
    SUB_CASE("base and derived view -> correct entity rows") {
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
    };
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

TEST_CASE("common.ecs.spec.moveOnlyAndExplicitConstruction") {
    SUB_CASE("move-only entity and component -> construction and transfer retain values") {
        ecs::Table       world;
        ecs::ScopedTable guard(world);

        // A record whose constructor needs an argument, with copy deleted.
        StrictNode* strict = StrictNode::create(42);
        REQUIRE_EQ(strict->seed(), 42);

        // The existing first-touch idiom still materialises a component, even
        // though that component is move-only.
        MoveOnlyHolder* holder = MoveOnlyHolder::create();
        holder->moveOnly()->x  = 3.f;
        CHECK(std::abs(holder->moveOnly()->x - 3.f) < 1e-5f);

        strict->release();
        holder->release();
    };
    SUB_CASE("component without default constructor -> explicit construction works") {
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
    };
    SUB_CASE("deferred publish -> transfers non default component") {
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
    };
}


}  // namespace ECS
}  // namespace

#endif
#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ecs_container_instances)
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

TEST_CASE("common.ecsContainers.spec.tableIsolationAndDeferredInheritance") {
    SUB_CASE("containers -> keep table isolation and stale handle detection") {
        ecs::Table first;
        ecs::Table second;
        auto*      a         = ContainerDerived::create(first);
        auto*      b         = ContainerDerived::create(second);
        a->position()->value = 7;
        b->position()->value = 9;
        const auto handle    = ecs::handle_of(a);
        REQUIRE(ecs::try_get(handle) == a);
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
    };
    SUB_CASE("inherited staged component -> correct table publication") {
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
    };
    SUB_CASE("typed handle across table projection -> generation remains authoritative") {
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
    };
}


}  // namespace ecs_container_instances
}  // namespace
#endif
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

TEST_CASE("common.identity.spec.persistentLogicalContentAndVersionTypes") {
    SUB_CASE("persistent ID bytes -> roundtrip; nil stays nil") {
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
    };
    SUB_CASE("identity uuid v7 -> uses injected entropy and clock") {
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

        eve::UuidV7Generator unavailable([](std::span<std::uint8_t>) { return false; },
                                         [timestamp] { return timestamp; });
        CHECK(!unavailable.generate().has_value());

        const auto negative = generator.generate(std::chrono::system_clock::time_point{} - 1ms);
        CHECK(!negative.has_value());
    };
    SUB_CASE("logical and content ID text -> distinct validation domains") {
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
    };
    SUB_CASE("version types -> do not wrap or mix domains") {
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
    };
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


TEST_CASE("common.resultAssertion.spec.sourceExpressionAndFailure") {
    SUB_CASE("result assertion -> preserves source and expression") {
#if !defined(ZEROERR_NO_ASSERT)
        bool            caught = false;
        AssertionOutput output;
        const unsigned  line = __LINE__ + 2;
        try {
            eve::detail::checkResultInvariant(false, "originalPredicate", __FILE__, line, "original context");
        } catch (const zeroerr::AssertionData& failure) {
            caught = true;
            REQUIRE(std::string(failure.file) == __FILE__);
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
    };
    SUB_CASE("result expect -> preserves failure and header location") {
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
    };
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

TEST_CASE("common.value.spec.jsonKindsAndCanonicalWire") {
    SUB_CASE("json round trip -> preserves all supported kinds") {
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
    };
    SUB_CASE("object order and numeric kinds -> are deterministic") {
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
    };
    SUB_CASE("value -> uses the common json parser and serializer") {
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
    };
    SUB_CASE("duplicate key, malformed JSON, or nonfinite number -> rejected") {
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
    };
    SUB_CASE("presentation value -> is the common value without a second storage") {
        static_assert(std::is_same_v<eve::Value, eve::Value>);
        eve::Value value(eve::Value::Object{{"count", 4}});
        auto       encoded = value.toJson();
        REQUIRE(encoded.ok());
        auto parsed = eve::Value::fromJson(encoded.value());
        REQUIRE(parsed.ok());
        CHECK(parsed.value() == value);
    };
    SUB_CASE("double json -> keeps existing canonical bytes") {
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
    };
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

TEST_CASE("common.value.spec.ownershipFactoriesAndBorrows") {
    SUB_CASE("lifecycle -> copy move and assignment preserve ownership") {
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
    };
    SUB_CASE("diagnostic -> copy move preserves context and independent details") {
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
    };
    SUB_CASE("initializer factories -> preserve container semantics") {
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
    };
    SUB_CASE("const and mutable typed value -> corresponding borrowed instance") {
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
    };
}


}  // namespace value_lifecycle
}  // namespace
}  // namespace test_common_values

#include "FieldDecodeCompatibility.h"
#include "FieldReadCompatibility.h"
#include "JsonReadCompatibility.h"
#include "NumericDoubleCompatibility.h"
#include "ScalarReadCompatibility.h"
namespace test_json_read_compatibility {
TEST_CASE("common.jsonRead.spec.wireAndRequiredFields") {
    SUB_CASE("json read control bytes -> preserve wire spelling") {
        json_read_compatibility::controlBytesPreserveWireSpelling(_ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("json read quote -> preserves utf8 and punctuation") {
        json_read_compatibility::quotePreservesUtf8AndPunctuation(_ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("json read unsigned syntax -> keeps legacy acceptance and suffix") {
        json_read_compatibility::unsignedSyntaxKeepsLegacyAcceptanceAndSuffix(_ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("json read artifact fields -> keep required string and decimal contract") {
        json_read_compatibility::artifactFieldsKeepRequiredStringAndDecimalContract(_ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("field read required strings -> keep empty and owning semantics") {
        field_read_compatibility::requiredStringsKeepEmptyAndOwningSemantics(_ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("field read native ints -> keep strict kinds and bounds") {
        field_read_compatibility::nativeIntsKeepStrictKindsAndBounds(_ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("field read unsigned value -> keeps decimal prefix contract") {
        field_read_compatibility::unsignedValueKeepsDecimalPrefixContract(_ZEROERR_TEST_CONTEXT);
    };
}

TEST_CASE("common.jsonRead.spec.scalarBoundsAndCoercion") {
    SUB_CASE("scalar read booleans -> keep false payload") {
        scalar_read_compatibility::booleansKeepFalsePayload(_ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("scalar read signed integers -> keep full range") {
        scalar_read_compatibility::signedIntegersKeepFullRange(_ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("scalar read unsigned integers -> keep bounds") {
        scalar_read_compatibility::unsignedIntegersKeepBounds(_ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("scalar read finite floats -> keep rounding and range") {
        scalar_read_compatibility::finiteFloatsKeepRoundingAndRange(_ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("field decode json fields -> keep wire kinds and bytes") {
        field_decode_compatibility::jsonFieldsKeepWireKindsAndBytes(_ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("field decode json numbers -> keep float boundaries and rounding") {
        field_decode_compatibility::jsonNumbersKeepFloatBoundariesAndRounding(_ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("numeric double -> preserves nonfinite values and integer rounding") {
        numeric_double_compatibility::preservesNonfiniteValuesAndIntegerRounding(_ZEROERR_TEST_CONTEXT);
    };
    SUB_CASE("numeric double -> rejects coercion with structured status") {
        numeric_double_compatibility::rejectsCoercionWithStructuredStatus(_ZEROERR_TEST_CONTEXT);
    };
}


}  // namespace test_json_read_compatibility
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

TEST_CASE("common.json.spec.scalarsDefaultsAndNumbers") {
    SUB_CASE("JSON scalar tokens -> typed values") {
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
    };
    SUB_CASE("missing keys -> fall back") {
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
    };
    SUB_CASE("wrong type -> falls back") {
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
    };
    SUB_CASE("coercible scalar token -> documented converted value") {
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
    };
    SUB_CASE("integer boundary text -> exact value or failure") {
        // Values a double cannot represent exactly must still round-trip through
        // getInt, and out-of-range ones fall back rather than wrap.
        Document doc = Document::parse(R"({"big": 2147483647, "over": 4294967296, "trunc": 2.9})");
        REQUIRE(doc.valid());
        Value r = doc.root();

        CHECK_EQ(r.getInt("big", 0), 2147483647);
        CHECK_EQ(r.getInt("over", -1), -1);
        CHECK_EQ(r.getInt("trunc", 0), 2);
    };
}

TEST_CASE("common.json.spec.arraysObjectsAndOrder") {
    SUB_CASE("JSON array -> indexed elements and length") {
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
    };
    SUB_CASE("JSON object map -> named values") {
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
    };
    SUB_CASE("nested object path -> leaf value") {
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
    };
    SUB_CASE("keys -> preserve document order") {
        Document doc = Document::parse(R"({"z": 1, "a": 2, "m": 3})");
        REQUIRE(doc.valid());
        auto keys = doc.root().keys();
        REQUIRE(keys.size() == 3u);
        CHECK_EQ(keys[0], "z");
        CHECK_EQ(keys[1], "a");
        CHECK_EQ(keys[2], "m");
    };
}

TEST_CASE("common.json.spec.syntaxDepthAndOwnership") {
    SUB_CASE("top-level array -> indexed root values") {
        Document doc = Document::parse(R"([{"id": "a"}, {"id": "b"}])");
        REQUIRE(doc.valid());
        Value r = doc.root();
        CHECK(r.isArray());
        CHECK(!r.isObject());
        REQUIRE(r.size() == 2u);
        CHECK_EQ(r.at(1).getString("id"), "b");
    };
    SUB_CASE("escaped Unicode JSON -> decoded string") {
        Document doc =
            Document::parse(R"({"esc": "a\"b\\c\/d\be\ff\ng\rh\ti", "bmp": "\u5f00\u59cb", "astral": "\ud83d\ude00"})");
        REQUIRE(doc.valid());
        Value r = doc.root();

        CHECK_EQ(r.getString("esc"), "a\"b\\c/d\be\ff\ng\rh\ti");
        // BMP escapes decode to UTF-8.
        CHECK_EQ(r.getString("bmp"), "\xe5\xbc\x80\xe5\xa7\x8b");
        // Surrogate pairs combine into one code point (U+1F600).
        CHECK_EQ(r.getString("astral"), "\xf0\x9f\x98\x80");
    };
    SUB_CASE("malformed input -> reports error") {
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
    };
    SUB_CASE("nesting depth -> is bounded") {
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
    };
    SUB_CASE("empty array and object -> distinct valid kinds") {
        Document doc = Document::parse(R"({"o": {}, "a": []})");
        REQUIRE(doc.valid());
        Value r = doc.root();
        CHECK(r.get("o").isObject());
        CHECK_EQ(r.get("o").size(), 0u);
        CHECK(r.get("a").isArray());
        CHECK_EQ(r.get("a").size(), 0u);
        CHECK(r.get("o").keys().empty());
    };
    SUB_CASE("document -> is movable") {
        Document a = Document::parse(R"({"v": 11})");
        REQUIRE(a.valid());
        Document b = std::move(a);
        CHECK(b.valid());
        CHECK_EQ(b.root().getInt("v"), 11);
    };
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

TEST_CASE("common.result.spec.successFailureAndConsumption") {
    SUB_CASE("successful Result -> value and empty diagnostics") {
        auto result = Result<int>::success(42);

        CHECK(result.ok());
        CHECK(result.hasValue());
        CHECK(result.code() == StatusCode::Ok);
        CHECK(result.value() == 42);
        CHECK(result.status().isSuccess());
        CHECK(result.diagnostics().empty());
        CHECK(result.error() == nullptr);
    };
    SUB_CASE("result failure -> carries structured diagnostic") {
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
    };
    SUB_CASE("result -> ignore and expect are explicit consumption") {
        Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "best effort cleanup"))
            .ignore("cleanup is best effort");

        const int value = Result<int>::success(7).expect("initialization must succeed");
        CHECK(value == 7);

        CHECK_THROWS((Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "not available"))
                          .expect("feature must be available"),
                      0));
    };
    SUB_CASE("result unobserved destruction -> is caught in debug") {
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
    };
}

TEST_CASE("common.result.spec.moveCompositionAndConstConversion") {
    SUB_CASE("result -> move transfers observation responsibility") {
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
    };
    SUB_CASE("result -> move assignment does not overwrite unchecked target") {
#if !defined(ZEROERR_NO_ASSERT)
        auto target = Result<int>::success(1);
        auto source = Result<int>::success(2);
        CHECK_THROWS((target = std::move(source), 0));
        target.ignore("retain target after rejected move assignment");
        source.ignore("retain source after rejected move assignment");
#else
        CHECK(true);
#endif
    };
    SUB_CASE("result composition -> preserves failure status") {
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
    };
    SUB_CASE("mutable Result -> const view; reverse type forbidden") {
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
    };
}

TEST_CASE("common.result.spec.voidAndDiagnosticStorage") {
    SUB_CASE("void success and failure -> status and boolean outcome") {
        auto result = Result<void>::success(Status::success(StatusCode::Applied));
        CHECK(static_cast<bool>(result));
        CHECK(result.code() == StatusCode::Applied);
        result.value();

        auto failed = Result<void>::failure(
            Status::failure(StatusCode::Conflict, Diagnostic::error(DiagnosticCode::Conflict, "revision changed")));
        CHECK(!failed);
        REQUIRE(failed.error() != nullptr);
        CHECK(failed.error()->code() == DiagnosticCode::Conflict);
    };
    SUB_CASE("result status -> copies retain immutable diagnostic storage") {
        auto original = Status::failure(Diagnostic::error(DiagnosticCode::Failed, "retained diagnostic", "test"));
        auto copied   = original;
        original      = Status::success(StatusCode::NoOp);
        CHECK(original.diagnostics().empty());
        REQUIRE(copied.primaryDiagnostic() != nullptr);
        CHECK_EQ(copied.primaryDiagnostic()->message(), "retained diagnostic");
        auto moved = std::move(copied);
        CHECK(moved.isFailure());
        CHECK_EQ(moved.diagnostics().size(), 1u);
    };
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

TEST_CASE("common.runtimeHandle.spec.invalidGenerationAndTypeIsolation") {
    SUB_CASE("runtime -> handle invalid and packed boundary") {
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
    };
    SUB_CASE("runtime -> handle bump rejects overflow") {
        constexpr UiHandle first(4u, 1u);
        const auto         bumped = first.nextGeneration();
        REQUIRE(bumped.has_value());
        CHECK_EQ(bumped->index(), first.index());
        CHECK_EQ(bumped->generation(), 2u);

        const auto fromInvalidGeneration = UiHandle::nextGeneration(0u);
        REQUIRE(fromInvalidGeneration.has_value());
        CHECK_EQ(*fromInvalidGeneration, 1u);

        CHECK(!UiHandle::nextGeneration(std::numeric_limits<UiHandle::generation_type>::max()).has_value());
        CHECK(!UiHandle(4u, std::numeric_limits<UiHandle::generation_type>::max()).nextGeneration().has_value());
        CHECK(!UiHandle::invalid().nextGeneration().has_value());
    };
    SUB_CASE("runtime -> handle hash and type isolation") {
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
    };
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


TEST_CASE("common.scriptCompiler.spec.metadataContractsAndSearch") {
    SUB_CASE("metadata -> ignores commented language forms") {
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
    };
    SUB_CASE("binding contracts -> are replaceable and sorted") {
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
    };
    SUB_CASE("binding contract -> search ranks exact method") {
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
    };
    SUB_CASE("generated binding contracts -> are searchable") {
        const auto hits = script::generatedBindingContracts().search("newWorld", {}, {}, 8);
        CHECK(!hits.empty());
        CHECK(hits[0].contract.toJson().find("\"method\":\"") != std::string::npos);
    };
    SUB_CASE("binding contract -> search splits slash and dot") {
        script::BindingContractRegistry registry;
        script::BindingContract         reset;
        reset.module          = "dialogue";
        reset.method          = "reset";
        reset.returnType      = "void";
        reset.documentationId = "dialogue.reset";
        registry.registerContract(reset);

        script::BindingContract publish;
        publish.module          = "dialogueFlow";
        publish.method          = "publishGameplay";
        publish.returnType      = "table";
        publish.documentationId = "dialogueFlow.publishGameplay";
        registry.registerContract(publish);

        const auto dotted = registry.search("dialogue.reset", {}, {}, 4);
        REQUIRE(!dotted.empty());
        CHECK_EQ(dotted[0].contract.key(), std::string("dialogue/reset"));

        const auto legacy = registry.search("dialogue publishGameplay", {}, {}, 4);
        REQUIRE(!legacy.empty());
        CHECK_EQ(legacy[0].contract.key(), std::string("dialogueFlow/publishGameplay"));
    };
}

TEST_CASE("common.scriptCompiler.spec.inspectorVmAndConfigValidation") {
    SUB_CASE("script inspector declaration -> recorded property metadata") {
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
    };
    SUB_CASE("raw vm bridge -> records tooling metadata") {
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
    };
    SUB_CASE("config module source -> validated without execution") {
        const auto diagnostics = script::ScriptCompiler::validateProjectConfig(
            "config <- { modules = [\"gfx\", \"physics\", \"typo\"], optionalModules = [\"audio\"] }\n",
            "game:/config.nut", {"gfx", "physics", "audio"}, {"gfx", "audio"});
        CHECK_EQ(diagnostics.size(), size_t(2));
        CHECK_EQ(diagnostics[0].code, std::string("EVE1002"));
        CHECK_EQ(diagnostics[0].related, std::string("physics"));
        CHECK_EQ(diagnostics[1].code, std::string("EVE1001"));
        CHECK_EQ(diagnostics[1].related, std::string("typo"));
    };
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
TEST_CASE("common.scriptStructure.spec.lexingAliasScopeAndInvalidInput") {
    SUB_CASE("source calls with comments and strings -> only executable calls") {
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
    };
    SUB_CASE("structure scopes aliases and -> preserves dynamic expressions") {
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
        auto compilerMetadata =
            eve::script::ScriptCompiler::analyze("function retained() { target.run(); }", "unit.nut");
        CHECK_EQ(compilerMetadata.structure.calls.size(), size_t(1));
    };
    SUB_CASE("structure -> rejects malformed and excessive nesting") {
        CHECK(!eve::script::analyzeScriptStructure("function bad() { emit(\"x\");").diagnostics.empty());
        CHECK(!eve::script::analyzeScriptStructure("/* unfinished").diagnostics.empty());
        CHECK(!eve::script::analyzeScriptStructure(std::string(65, '(') + "x" + std::string(65, ')'))
                   .diagnostics.empty());
    };
    SUB_CASE("structure -> reflects fields without leaking class methods or members") {
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
    };
    SUB_CASE("structure parameter defaults -> do not declare global aliases") {
        auto result = eve::script::analyzeScriptStructure(
            "function callback(marker: SceneNodeRef = null, text = \"x\") { marker.setVisible(true); }");
        REQUIRE(result.diagnostics.empty());
        CHECK(result.bindings.empty());
        CHECK_EQ(result.functions[0].parameters.size(), size_t(2));
    };
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

TEST_CASE("common.stateValue.spec.containersPathsMergeAndEquality") {
    SUB_CASE("state value factory input -> expected kind") {
        CHECK(StateValue::null().isNull());
        CHECK(StateValue::boolean(true).asBool());
        CHECK_EQ(StateValue::integer(42).asInt(), int64_t(42));
        CHECK(StateValue::number(1.5).asDouble() == 1.5);
        CHECK_EQ(StateValue::string("hi").asString(), std::string("hi"));
        CHECK(StateValue::array().isArray());
        CHECK(StateValue::object().isObject());
    };
    SUB_CASE("object insertion -> key lookup and iteration") {
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
    };
    SUB_CASE("array push and update -> ordered values") {
        StateValue a = StateValue::array();
        a.pushBack(StateValue::integer(10));
        a.pushBack(StateValue::string("v"));

        REQUIRE(a.arraySize() == 2u);
        CHECK_EQ(a.at(0).asInt(), int64_t(10));
        CHECK_EQ(a.at(1).asString(), std::string("v"));
    };
    SUB_CASE("nested path write -> same leaf on read") {
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
    };
    SUB_CASE("partial object plus defaults -> missing values filled") {
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
    };
    SUB_CASE("equal and unequal state values -> matching comparison results") {
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
    };
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
TEST_CASE("common.stringInstances.spec.crossBoundaryOwnershipAndStatus") {
    SUB_CASE("string instances cross boundary growth -> copy and move") {
        std::string input(8192, 'x');
        eve::Value  value(input);
        input.assign("changed");
        REQUIRE(value.asString().size() == 8192u);
        std::string output(value.asString());
        output.append(4096, 'y');
        CHECK(output.size() == 12288u);
        CHECK(value.asString().size() == 8192u);
        eve::Value moved(std::move(output));
        CHECK(moved.asString().back() == 'y');
        output.assign("reused");
        CHECK(output == "reused");
    };
    SUB_CASE("string instances status formatting -> remains stable") {
        auto status =
            eve::Status::failure(eve::StatusCode::Rejected,
                                 eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "bad value", "field"));
        CHECK(status.describe() == "rejected: bad value [field]");
        CHECK(eve::Status::success().describe() == "ok");
    };
    SUB_CASE("string instances result failure -> retains context and location") {
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
    };
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

TEST_CASE("common.time.spec.fixedUnitsAndSimulationStep") {
    SUB_CASE("duration and timestamp -> use strong fixed units") {
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
    };
    SUB_CASE("elapsed time, pause, scale -> expected fixed simulation ticks") {
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
    };
    SUB_CASE("clock -> uses monotonic only and wall clock is metadata") {
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
    };
}

TEST_CASE("common.time.spec.invalidClockUpdatesAreAtomic") {
    SUB_CASE("clock -> rejects backward source without state mutation") {
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
    };
    SUB_CASE("clock -> rejects timestamp distance outside duration without state mutation") {
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
    };
    SUB_CASE("clock -> rejects extreme catch up without multiplication overflow") {
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
    };
}


}  // namespace test_time_contract_unit
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_versioned_registry)
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include "PrivateTestScope.h"
#include "common/VersionedRegistry.h"
namespace test_versioned_registry_contract {
struct Value {
    int             payload;
    eve::Generation generation;
    Value() = delete;
    explicit Value(int n) : payload(n) {}
};
struct EventData {
    eve::SchemaVersion version;
};
using Registry = eve::VersionedRegistry<std::string, Value, EventData>;
Registry registryWithProjection() {
    return Registry([](Value& value, eve::Generation generation) { value.generation = generation; });
}

TEST_CASE("Versioned registry advances generations through replace tombstone and revival") {
    auto       registry = registryWithProjection();
    const auto inserted = registry.insert("item", Value{1}, EventData{eve::SchemaVersion(1)});
    REQUIRE(inserted.ok());
    const auto first = inserted.value();
    CHECK_EQ(first.generation.value(), uint64_t{1});
    CHECK_EQ(registry.resolve(first).value().get().payload, 1);
    CHECK(!registry.insert("item", Value{2}, EventData{eve::SchemaVersion(2)}).ok());
    const auto replaced = registry.replace("item", Value{3}, EventData{eve::SchemaVersion(3)});
    REQUIRE(replaced.ok());
    CHECK(registry.isStale(first));
    CHECK_EQ(replaced.value().generation.value(), uint64_t{2});
    const auto removed = registry.remove(std::string("item"), EventData{eve::SchemaVersion(3)});
    REQUIRE(removed.ok());
    CHECK(registry.isTombstone("item"));
    CHECK(registry.isStale(removed.value()));
    CHECK_EQ(removed.value().generation.value(), uint64_t{3});
    const auto revived = registry.insert("item", Value{4}, EventData{eve::SchemaVersion(4)});
    REQUIRE(revived.ok());
    CHECK_EQ(revived.value().generation.value(), uint64_t{4});
    CHECK_EQ(registry.size(), 1u);
    CHECK_EQ(registry.tombstoneCount(), 0u);
}

TEST_CASE("Versioned registry snapshot preserves event order values and tombstones") {
    auto source = registryWithProjection();
    REQUIRE(source.insert("b", Value{1}, EventData{eve::SchemaVersion(1)}).ok());
    REQUIRE(source.insert("a", Value{2}, EventData{eve::SchemaVersion(2)}).ok());
    REQUIRE(source.remove(std::string("a"), EventData{eve::SchemaVersion(2)}).ok());
    REQUIRE_EQ(source.eventCount(), 3u);
    CHECK_EQ(source.eventAt(0)->operation, eve::RegistryOperation::Insert);
    CHECK(source.eventAt(2)->isTombstone());
    auto restored = registryWithProjection();
    REQUIRE(restored.restoreState(source.snapshotState()).ok());
    CHECK_EQ(restored.eventCount(), 3u);
    CHECK(restored.isTombstone("a"));
    const auto b = restored.resolve("b");
    REQUIRE(b.ok());
    CHECK_EQ(b.value().get().payload, 1);
    CHECK_EQ(b.value().get().generation.value(), uint64_t{1});
}

TEST_CASE("Versioned registry observer receives canonical sequence and callback failure reports applied state") {
    auto                                registry = registryWithProjection();
    std::vector<eve::RegistryOperation> operations;
    auto observer = registry.subscribe([&](const Registry::Event& event) { operations.push_back(event.operation); });
    auto failing  = registry.subscribe([](const Registry::Event&) { throw std::runtime_error("observer"); });
    const auto applied = registry.insert("item", Value{1}, EventData{eve::SchemaVersion(1)});
    REQUIRE(applied.ok());
    CHECK_EQ(applied.code(), eve::StatusCode::Applied);
    REQUIRE_EQ(applied.diagnostics().size(), 1u);
    CHECK_EQ(applied.diagnostics().front().code(), eve::DiagnosticCode::CallbackFailure);
    CHECK(registry.contains("item"));
    CHECK_EQ(registry.eventCount(), 1u);
    CHECK_EQ(operations.size(), 1u);
    observer.dispose();
    failing.dispose();
    REQUIRE(registry.remove(std::string("item"), EventData{eve::SchemaVersion(1)}).ok());
    CHECK_EQ(operations.size(), 1u);
}

TEST_CASE("Versioned registry failed generation projection preserves target image") {
    auto source = registryWithProjection();
    REQUIRE(source.insert("source", Value{1}, EventData{eve::SchemaVersion(1)}).ok());
    bool     fail = false;
    Registry target([&](Value& value, eve::Generation generation) {
        value.generation = generation;
        if (fail) throw std::runtime_error("projection");
    });
    REQUIRE(target.insert("old", Value{9}, EventData{eve::SchemaVersion(1)}).ok());
    const auto eventsBefore = target.eventCount();
    fail                    = true;
    const auto rejected     = target.restoreState(source.snapshotState());
    fail                    = false;
    CHECK(!rejected.ok());
    CHECK(target.contains("old"));
    CHECK(!target.contains("source"));
    CHECK_EQ(target.eventCount(), eventsBefore);
}
}  // namespace test_versioned_registry_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_xml)
#include "PrivateTestScope.h"
#include "common/Xml.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// xml.cpp
namespace test_xml {
using namespace eve::xml;

TEST_CASE("common.xml.spec.treeEntitiesAndInvalidInput") {
    SUB_CASE("XML attributes and child tags -> parsed tree") {
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
    };
    SUB_CASE("self-closing XML with entities -> decoded content") {
        Document doc = Document::parse(R"(<root note="a&amp;b&lt;c">
              <child/>
            </root>)");
        REQUIRE(doc.valid());
        CHECK_EQ(doc.root().getAttribute("note"), "a&b<c");
        CHECK_EQ(doc.root().children("child").size(), 1u);
    };
    SUB_CASE("invalid -> returns empty") {
        std::string error;
        Document    doc = Document::parse("<root><unclosed>", &error);
        CHECK(!doc.valid());
        CHECK(!error.empty());
        CHECK(!doc.root());
    };
}


}  // namespace test_xml
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_capability_owned)
#include <memory>
#include <type_traits>
#include "PrivateTestScope.h"
#include "common/Capability.h"
#include "common/CapabilityOwned.h"
namespace test_owned_capability_contract {
class IWidget {
public:
    static constexpr const char* capabilityName = "rewrite.test.IWidget";
    virtual ~IWidget()                          = default;
    virtual int value() const                   = 0;
};
class Widget final : public IWidget {
public:
    explicit Widget(int n) : n_(n) {}
    int value() const override { return n_; }

private:
    int n_;
};
using Registry = eve::cap::OwnedProviderRegistry<IWidget>;
struct Reset {
    Reset() { eve::cap::detail::clearAllRaw(); }
    ~Reset() { eve::cap::detail::clearAllRaw(); }
};

TEST_CASE("Owned capability reports absent and null providers before publication") {
    Reset      reset;
    const auto missing = Registry::acquire("rewrite.widget");
    REQUIRE(!missing.ok());
    CHECK_EQ(missing.status().diagnostics().front().code(), eve::DiagnosticCode::Unsupported);
    std::shared_ptr<IWidget> empty;
    const auto               rejected = Registry::provide("rewrite.widget", empty);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.status().diagnostics().front().code(), eve::DiagnosticCode::InvalidArgument);
    CHECK(!Registry::acquire("rewrite.widget").ok());
}

TEST_CASE("Owned capability lease survives retirement while stale handle cannot address republished provider") {
    Reset      reset;
    const auto first = Registry::provide("rewrite.widget", std::make_shared<Widget>(7));
    REQUIRE(first.ok());
    auto lease = Registry::acquire(first.value());
    REQUIRE(lease.ok());
    CHECK_EQ(lease.value()->value(), 7);
    CHECK_EQ(eve::cap::query<IWidget>()->value(), 7);
    CHECK(!Registry::provide("rewrite.widget", std::make_shared<Widget>(8)).ok());
    REQUIRE(Registry::unload(first.value()).ok());
    CHECK(!Registry::acquire(first.value()).ok());
    CHECK_EQ(lease.value()->value(), 7);
    const auto next = Registry::provide("rewrite.widget", std::make_shared<Widget>(9));
    REQUIRE(next.ok());
    CHECK(next.value().generation > first.value().generation);
    CHECK(!Registry::unload(first.value()).ok());
    const auto live = Registry::acquire("rewrite.widget");
    REQUIRE(live.ok());
    CHECK_EQ(live.value()->value(), 9);
}

TEST_CASE("Caller owned provider binds once and moved references release their source") {
    static_assert(!std::is_copy_constructible_v<eve::cap::ProviderRef<IWidget>>);
    static_assert(std::is_move_constructible_v<eve::cap::ProviderRef<IWidget>>);
    Reset reset;
    CHECK(!eve::cap::ProviderRef<IWidget>::bind());
    Widget widget{42};
    eve::cap::provide<IWidget>(&widget);
    auto first = eve::cap::ProviderRef<IWidget>::bind();
    REQUIRE(first.get() == &widget);
    auto moved = std::move(first);
    CHECK(first.get() == nullptr);
    CHECK_EQ(moved->value(), 42);
}
}  // namespace test_owned_capability_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_condition)
#include <map>
#include <optional>
#include "PrivateTestScope.h"
#include "common/Condition.h"
#include "common/ConditionCodec.h"
#include "common/ConditionKeys.h"
namespace test_condition_contract {
using namespace eve::condition;
class Context : public EvaluationContext {
public:
    std::map<std::string, eve::Value>      values;
    std::map<std::string, ConditionResult> policies;
    std::optional<eve::Value>              value(std::string_view key) const override {
        const auto it = values.find(std::string(key));
        return it == values.end() ? std::nullopt : std::optional<eve::Value>(it->second);
    }
    std::optional<ConditionResult> policy(std::string_view name, const eve::Value&) const override {
        const auto it = policies.find(std::string(name));
        return it == policies.end() ? std::nullopt : std::optional<ConditionResult>(it->second);
    }
};

TEST_CASE("Conditions evaluate typed leaves and explain missing values separately from false") {
    Context context;
    context.values.emplace("level", 7);
    context.values.emplace(tagKey("fire"), true);
    context.values.emplace(attrKey("power"), 12.0);
    context.values.emplace(resourceKey("mana"), 20.0);
    context.values.emplace(stateKey("phase"), "ready");
    context.policies.emplace("safe", ConditionResult::success(eve::Value(true)));
    const auto condition = Condition::all({Condition::compare("level", CompareOperator::GreaterEqual, 7),
                                           tagPresent("fire"), attributeExists("power"), resourceExists("mana"),
                                           stateEquals("phase", "ready"), Condition::policyCall("safe")});
    REQUIRE(condition.evaluate(context).passed());
    const auto missing = tagPresent("ice").evaluate(context);
    CHECK(!missing.passed());
    CHECK(missing.reasonCode() == ConditionReasonCode::MissingValue);
    context.values.emplace(tagKey("ice"), false);
    const auto falseValue = tagPresent("ice").evaluate(context);
    CHECK(!falseValue.passed());
    CHECK_NE(falseValue.reasonCode(), ConditionReasonCode::MissingValue);
}

TEST_CASE("Compound conditions explain failed any and negation without mutating context") {
    Context context;
    context.values.emplace("level", 2);
    context.values.emplace(tagKey("fire"), false);
    context.values.emplace(stateKey("phase"), "ready");
    const auto before   = context.values;
    const auto any = Condition::any({Condition::compare("level", CompareOperator::Greater, 9), tagPresent("fire")});
    const auto rejected = any.evaluate(context);
    REQUIRE(!rejected.passed());
    CHECK(rejected.reasonCode() == ConditionReasonCode::NoChildPassed);
    const auto negated = Condition::not_(stateEquals("phase", "ready")).evaluate(context);
    CHECK(!negated.passed());
    CHECK(negated.reasonCode() == ConditionReasonCode::Negated);
    CHECK(context.values == before);
}

TEST_CASE("Condition legacy codec expands tags and authority queries into canonical nodes") {
    const auto tag = decodeCondition(eve::Value::Object{{"kind", "has_tag"}, {"key", "fire"}});
    REQUIRE(tag.ok());
    CHECK(tag.value().kind() == ConditionKind::Compare);
    CHECK_EQ(tag.value().key(), std::string("tag:fire"));
    const auto authority = decodeCondition(eve::Value::Object{{"kind", "authority_check"}, {"key", "cast"}});
    REQUIRE(authority.ok());
    CHECK(authority.value().kind() == ConditionKind::PolicyCall);
    CHECK_EQ(authority.value().key(), std::string(kAuthorityAllowPolicy));
}
}  // namespace test_condition_contract
#endif


#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_sdf_field3d)
#include <cmath>
#include "PrivateTestScope.h"
#include "common/SdfField3D.h"
namespace test_sdf_field3d_contract {
eve::SdfField3DBlob sphereBlob() {
    eve::SdfField3DBlob blob;
    blob.kind  = eve::SdfField3DSampleKind::VertexGrid;
    blob.width = blob.height = blob.depth = 8;
    blob.cellSize                         = .25f;
    blob.originX = blob.originY = blob.originZ = -1;
    blob.outsideDistance                       = 10;
    for (int z = 0; z < 8; ++z)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const float px = -1 + x * .25f, py = -1 + y * .25f, pz = -1 + z * .25f;
                blob.distances.push_back(std::sqrt(px * px + py * py + pz * pz) - .5f);
            }
    return blob;
}

TEST_CASE("SDF field blob codec preserves geometry samples and rejects unsupported schema") {
    const auto input = sphereBlob();
    REQUIRE(eve::validateSdfField3DBlob(input).ok());
    const auto encoded = eve::encodeSdfField3DBlob(input);
    REQUIRE(encoded.ok());
    const auto decoded = eve::decodeSdfField3DBlob(encoded.value());
    REQUIRE(decoded.ok());
    CHECK_EQ(decoded.value().width, 8);
    CHECK_EQ(decoded.value().cellSize, .25f);
    CHECK(decoded.value().distances == input.distances);
    auto wrongSchema = encoded.value();
    wrongSchema.set("schemaId", std::string("eve.sdf.other"));
    CHECK(!eve::decodeSdfField3DBlob(wrongSchema).ok());
    auto shortData = input;
    shortData.distances.pop_back();
    CHECK(!eve::validateSdfField3DBlob(shortData).ok());
}

class AnalyticSphere final : public eve::ISdfField3D {
public:
    float sample(float x, float y, float z) const override { return std::sqrt(x * x + y * y + z * z) - .5f; }
    eve::SdfField3DGradientStatus gradient(float x, float y, float z, float& nx, float& ny, float& nz) const override {
        const float length = std::sqrt(x * x + y * y + z * z);
        if (length <= 1e-8f) return eve::SdfField3DGradientStatus::Undefined;
        nx = x / length;
        ny = y / length;
        nz = z / length;
        return eve::SdfField3DGradientStatus::Defined;
    }
    unsigned revision() const override { return 1; }
    void     bounds(float& minX, float& minY, float& minZ, float& maxX, float& maxY, float& maxZ) const override {
        minX = minY = minZ = -1;
        maxX = maxY = maxZ = 1;
    }
};

TEST_CASE("SDF field interface distinguishes undefined center gradient from outward normal") {
    AnalyticSphere field;
    REQUIRE(std::fabs(field.sample(0, 0, 0) + .5f) < 1e-5f);
    float nx = 0, ny = 0, nz = 0;
    CHECK(field.gradient(0, 0, 0, nx, ny, nz) == eve::SdfField3DGradientStatus::Undefined);
    CHECK(field.gradient(1, 0, 0, nx, ny, nz) == eve::SdfField3DGradientStatus::Defined);
    CHECK(std::fabs(nx - 1.f) < 1e-5f);
    float minX, minY, minZ, maxX, maxY, maxZ;
    field.bounds(minX, minY, minZ, maxX, maxY, maxZ);
    CHECK_EQ(minX, -1.f);
    CHECK_EQ(maxZ, 1.f);
    CHECK_EQ(field.revision(), 1u);
}
}  // namespace test_sdf_field3d_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_ecs_view_preparation)
#include <ECS.hpp>
#include <functional>
#include <stdexcept>
namespace test_ecs_view_contract {
struct Prepared {
    inline static int                   constructed = 0;
    inline static int                   throwAt     = -1;
    inline static std::function<void()> onConstruct;
    int                                 value = 7;
    Prepared() {
        if (constructed == throwAt) throw std::runtime_error("injected constructor failure");
        ++constructed;
        if (onConstruct) onConstruct();
    }
};
class Base : public ecs::Entity {
public:
    ENTITY(Base, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
};
class Derived : public Base {
public:
    ENTITY(Derived, Base)
};
struct Reset {
    Reset() {
        Prepared::constructed = 0;
        Prepared::throwAt     = -1;
        Prepared::onConstruct = {};
    }
    ~Reset() {
        Prepared::throwAt     = -1;
        Prepared::onConstruct = {};
    }
};
struct Explicit {
    explicit Explicit(int n) : value(n) {}
    int value;
};

TEST_CASE("ECS component preparation crosses word boundaries once and restores reset slots") {
    Reset      reset;
    ecs::Table table;
    auto*      buffer = table.getOrCreateManager<Derived>().getOrCreateComponentBuffer<Prepared>();
    buffer->ensure_space(193);
    for (unsigned count : {0u, 1u, 63u, 64u, 65u, 127u, 128u, 129u, 193u}) {
        buffer->engageUpTo(count);
        REQUIRE_EQ(Prepared::constructed, int(count));
        buffer->engageUpTo(count);
        CHECK_EQ(Prepared::constructed, int(count));
    }
    buffer->at(62).value = 91;
    buffer->reset_slot(63);
    buffer->reset_slot(64);
    buffer->reset_slot(128);
    buffer->engageUpTo(65);
    CHECK_EQ(Prepared::constructed, 195);
    CHECK_EQ(buffer->at(62).value, 91);
    CHECK(!buffer->has(128));
    buffer->engageUpTo(193);
    CHECK_EQ(Prepared::constructed, 196);
    CHECK(buffer->has(128));
}

TEST_CASE("ECS view constructor failure retries missing components and releases only its defer scope") {
    Reset      reset;
    ecs::Table table;
    for (int i = 0; i < 130; ++i) REQUIRE(Derived::create(table) != nullptr);
    Prepared::throwAt = 64;
    bool threw        = false;
    try {
        auto view = ecs::View<Base, Prepared>(table);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    REQUIRE(threw);
    CHECK_EQ(Prepared::constructed, 64);
    CHECK(!table.is_deferred());
    Prepared::throwAt = -1;
    int seen          = 0;
    for (auto [value] : ecs::View<Base, Prepared>(table)) {
        CHECK_EQ(value->value, 7);
        ++seen;
    }
    CHECK_EQ(seen, 130);
    CHECK_EQ(Prepared::constructed, 130);
}

TEST_CASE("ECS nested view failure preserves outer defer and later publication") {
    Reset             reset;
    ecs::Table        table;
    ecs::EntityHandle pending;
    {
        ecs::ScopedDefer outer(table);
        pending           = ecs::handle_of(Derived::create(table));
        Prepared::throwAt = 0;
        bool threw        = false;
        try {
            auto view = ecs::View<Base, Prepared>(table);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        REQUIRE(threw);
        CHECK(table.is_deferred());
        CHECK(!ecs::is_entity_visible(ecs::try_get(pending)));
        Prepared::throwAt = -1;
    }
    CHECK(!table.is_deferred());
    CHECK(ecs::is_entity_visible(ecs::try_get(pending)));
}

TEST_CASE("ECS views include derived entities but exclude staged and destroyed entities") {
    Reset      reset;
    ecs::Table table;
    ecs::Table other;
    auto*      base    = Base::create(table);
    auto*      derived = Derived::create(table);
    REQUIRE(base != nullptr);
    REQUIRE(derived != nullptr);
    int count = 0;
    for (auto [value] : ecs::View<Base, Prepared>(table)) {
        CHECK_EQ(value->value, 7);
        ++count;
    }
    CHECK_EQ(count, 2);
    {
        auto empty = ecs::View<Base, Prepared>(other);
        CHECK(empty.begin() == empty.end());
    }
    {
        ecs::ScopedDefer defer(table);
        Derived::create(table);
        ecs::DestroyEntity(derived);
        count = 0;
        for (auto [value] : ecs::View<Base, Prepared>(table)) {
            (void)value;
            ++count;
        }
        CHECK_EQ(count, 1);
    }
    count = 0;
    for (auto [value] : ecs::View<Base, Prepared>(table)) {
        (void)value;
        ++count;
    }
    CHECK_EQ(count, 2);
}

TEST_CASE("ECS view supports explicit only components and constructor side effects") {
    Reset      reset;
    ecs::Table table;
    auto*      first  = Derived::create(table);
    auto*      second = Derived::create(table);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    auto* explicitBuffer = table.getOrCreateManager<Derived>().getOrCreateComponentBuffer<Explicit>();
    REQUIRE(explicitBuffer->emplace(second->id, 42));
    int explicitCount = 0;
    for (auto [value] : ecs::View<Base, Explicit>(table)) {
        CHECK_EQ(value->value, 42);
        ++explicitCount;
    }
    CHECK_EQ(explicitCount, 1);
    auto* prepared        = first->getComponentManager().getOrCreateComponentBuffer<Prepared>();
    Prepared::onConstruct = [&] {
        if (Prepared::constructed == 2) prepared->reset_slot(first->id);
    };
    { auto view = ecs::View<Base, Prepared>(table); }
    CHECK(!prepared->has(first->id));
    Prepared::onConstruct = {};
    { auto view = ecs::View<Base, Prepared>(table); }
    CHECK(prepared->has(first->id));
    CHECK_EQ(Prepared::constructed, 3);
}
}  // namespace test_ecs_view_contract
#endif

#include <limits>
#include <type_traits>
#include "common/Identity.h"
#include "common/RuntimeHandle.h"

namespace test_identity_value_audit_unit {
namespace {
template <class Id>
Id parsedId(const char* text) {
    const auto parsed = Id::parse(text);
    return parsed ? *parsed : Id::nil();
}
}  // namespace
static_assert(!std::is_convertible_v<eve::AssetGuid, eve::DocumentId>);
static_assert(!std::is_convertible_v<eve::EventId, eve::TransactionId>);
static_assert(!std::is_convertible_v<eve::TransactionId, eve::OperationId>);
static_assert(!std::is_convertible_v<eve::SceneObjectId, eve::PersistentId>);


TEST_CASE("identity.audit.domainIdsShareOneUuidImplementation") {
    constexpr const char* text  = "01020304-0506-0708-090a-0b0c0d0e0f10";
    const auto            asset = parsedId<eve::AssetGuid>(text);
    REQUIRE(!asset.isNil());
    CHECK_EQ(asset.format(), text);
    CHECK_EQ(eve::DocumentId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::SceneObjectId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::ArtifactId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::EventId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::TransactionId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::OperationId::fromUuid(asset).format(), text);
    CHECK_EQ(eve::EffectId::fromUuid(asset).format(), text);

    CHECK(!eve::AssetGuid::parse("asset-01020304-0506-0708-090a-0b0c0d0e0f10").has_value());
    CHECK(!eve::AssetGuid::parse("{01020304-0506-0708-090a-0b0c0d0e0f10}").has_value());
}

TEST_CASE("identity.audit.logicalNamespaceIsCanonicalAndNamesRemainCaseSensitive") {
    REQUIRE(eve::LogicalId::parse("rpg:Skill.Fire").has_value());
    CHECK(!eve::LogicalId::parse("RPG:Skill.Fire").has_value());
    CHECK(!eve::LogicalId::parse("rpg_:Skill.Fire").has_value());
    CHECK(!eve::LogicalId::parse("rpg:skill/fire").has_value());
    CHECK(eve::LogicalId::fromParts("game_event", "Damage.Applied").has_value());
}

TEST_CASE("identity.audit.runtimeHandleGenerationMakesStaleReuseObservable") {
    struct RegistryTag {};
    using Handle = eve::RuntimeHandle<RegistryTag>;

    constexpr Handle oldHandle(4u, 1u);
    const auto       next = oldHandle.nextGeneration();
    REQUIRE(next.has_value());
    CHECK(next.value() != oldHandle);
    CHECK_EQ(next->index(), oldHandle.index());
    CHECK_EQ(next->generation(), 2u);
    CHECK(!Handle(4u, std::numeric_limits<Handle::generation_type>::max()).nextGeneration().has_value());
    CHECK_EQ(Handle::fromPacked(oldHandle.packed()), oldHandle);
}
}  // namespace test_identity_value_audit_unit
