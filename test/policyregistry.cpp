#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "policyregistry/PolicyRegistry.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace eve::policyregistry;

TEST_CASE("policyregistry.registersCanonicalDescriptors") {
    PolicyRegistry registry;
    REQUIRE(registry.insert("movement", "ground", 2, 10, true, "builtin", "move.schema", "{\"z\":2,\"a\":1}").ok());
    auto policy = registry.resolve("movement", "ground");
    REQUIRE(policy.ok());
    const auto& descriptor = policy.value().get();
    CHECK_EQ(descriptor.version.value(), uint64_t{2});
    CHECK_EQ(descriptor.priority, 10);
    CHECK(descriptor.enabled);
    CHECK_EQ(implementationKindName(descriptor.kind), std::string("builtin"));
    CHECK_EQ(descriptor.metadataJson, std::string("{\"a\":1,\"z\":2}"));
    CHECK(!registry.insert("movement", "ground", 1, 0, true, "script", "", "{}").ok());
    CHECK(!registry.insert("", "bad", 1, 0, true, "script", "", "{}").ok());
    CHECK(!registry.insert("movement", "bad", 0, 0, true, "script", "", "{}").ok());
    CHECK(!registry.insert("movement", "bad", 1, 0, true, "remote", "", "{}").ok());
    CHECK(!registry.insert("movement", "bad", 1, 0, true, "script", "", "[]").ok());
}

TEST_CASE("policyregistry.selectsEnabledPriorityThenName") {
    PolicyRegistry registry;
    REQUIRE(registry.insert("targeting", "zeta", 1, 20, true, "script", "", "{}").ok());
    REQUIRE(registry.insert("targeting", "alpha", 1, 20, true, "batch", "", "{}").ok());
    REQUIRE(registry.insert("targeting", "disabled", 1, 99, false, "builtin", "", "{}").ok());
    REQUIRE(registry.select("targeting") != nullptr);
    CHECK_EQ(registry.select("targeting")->name, std::string("alpha"));
    REQUIRE(registry.enable("targeting", "disabled", true).ok());
    CHECK_EQ(registry.select("targeting")->name, std::string("disabled"));
    REQUIRE(registry.enable("targeting", "disabled", false).ok());
    CHECK_EQ(registry.select("targeting")->name, std::string("alpha"));
    CHECK(registry.select("missing") == nullptr);
}

TEST_CASE("policyregistry.generationsInvalidateStaleHandles") {
    PolicyRegistry registry;
    REQUIRE(registry.insert("economy", "tax", 1, 0, true, "script", "tax.v1", "{}").ok());
    auto oldResult = registry.handle("economy", "tax");
    REQUIRE(oldResult.ok());
    const auto old = std::move(oldResult).takeValue();
    REQUIRE(registry.resolveHandle(old).ok());
    REQUIRE(registry.replace("economy", "tax", 2, 1, true, "batch", "tax.v2", "{}").ok());
    CHECK(!registry.resolveHandle(old).ok());
    auto current = registry.resolve("economy", "tax");
    REQUIRE(current.ok());
    CHECK_EQ(current.value().get().generation.value(), uint64_t{2});
    REQUIRE(registry.remove("economy", "tax").ok());
    REQUIRE(registry.insert("economy", "tax", 3, 2, true, "script", "tax.v3", "{}").ok());
    current = registry.resolve("economy", "tax");
    REQUIRE(current.ok());
    CHECK_EQ(current.value().get().generation.value(), uint64_t{4});
}

TEST_CASE("policyregistry.registryParityKeepsTombstones") {
    PolicyRegistry registry;
    auto           inserted = registry.insert("movement", "ground", 1, 2, true, "builtin", "", "{}");
    REQUIRE(inserted.ok());
    const auto first = std::move(inserted).takeValue();

    auto disabled = registry.enable("movement", "ground", false);
    REQUIRE(disabled.ok());
    const auto second = std::move(disabled).takeValue();
    CHECK_EQ(second.generation.value(), uint64_t{2});
    CHECK(registry.isStale(first));
    auto current = registry.resolve("movement", "ground");
    REQUIRE(current.ok());
    CHECK(!current.value().get().enabled);
    CHECK_EQ(registry.eventAt(1)->name, std::string("policy_enabled"));

    auto removed = registry.remove("movement", "ground");
    REQUIRE(removed.ok());
    const auto tombstone = std::move(removed).takeValue();
    CHECK_EQ(tombstone.generation.value(), uint64_t{3});
    CHECK(registry.isTombstone("movement", "ground"));
    auto stale = registry.resolveHandle(second);
    CHECK(!stale.ok());
    CHECK_EQ(stale.error()->code(), eve::DiagnosticCode::StaleHandle);

    auto revived = registry.insert("movement", "ground", 2, 3, true, "script", "", "{}");
    REQUIRE(revived.ok());
    CHECK_EQ(std::move(revived).takeValue().generation.value(), uint64_t{4});
}

TEST_CASE("policyregistry.subscriptionAndSnapshotEnvelopeAreCompatible") {
    PolicyRegistry           registry;
    std::vector<std::string> names;
    auto subscription = registry.subscribe([&](const PolicyEvent& event) { names.push_back(event.name); });
    REQUIRE(registry.insert("orders", "safe", 1, 1, true, "script", "", "{}").ok());
    REQUIRE(registry.enable("orders", "safe", false).ok());
    CHECK_EQ(names, std::vector<std::string>({"policy_registered", "policy_enabled"}));
    subscription.dispose();

    const auto hash = [](std::string_view) -> eve::Result<eve::ContentId> {
        eve::ContentId::Bytes bytes{};
        bytes[0] = 0x42;
        return eve::Result<eve::ContentId>::success(eve::ContentId(bytes));
    };
    auto envelope = registry.snapshotEnvelopeJson(hash);
    REQUIRE(envelope.ok());
    PolicyRegistry copy;
    auto           restored = copy.restoreSnapshotJson(std::move(envelope).takeValue(), hash);
    REQUIRE(restored.ok());
    auto safe = copy.resolve("orders", "safe");
    REQUIRE(safe.ok());
    CHECK(!safe.value().get().enabled);
}

TEST_CASE("policyregistry.mutationPreservesObserverWarning") {
    PolicyRegistry registry;
    auto           subscription =
        registry.subscribe([](const PolicyEvent&) { throw std::runtime_error("injected policy observer failure"); });

    auto result = registry.insert("orders", "safe", 1, 1, true, "script", "", "{}");
    REQUIRE(result.ok());
    CHECK_EQ(result.code(), eve::StatusCode::Applied);
    REQUIRE_EQ(result.diagnostics().size(), std::size_t{1});
    CHECK_EQ(result.diagnostics().front().code(), eve::DiagnosticCode::CallbackFailure);
    CHECK(registry.resolve("orders", "safe").ok());
}

TEST_CASE("policyregistry.enumeratesDeterministically") {
    PolicyRegistry registry;
    REQUIRE(registry.insert("z", "last", 1, 0, true, "script", "", "{}").ok());
    REQUIRE(registry.insert("a", "two", 1, 0, true, "script", "", "{}").ok());
    REQUIRE(registry.insert("a", "one", 1, 0, true, "script", "", "{}").ok());
    CHECK_EQ(registry.size(), 3);
    CHECK_EQ(registry.countDomain("a"), 2);
    CHECK_EQ(registry.at(0)->domain, std::string("a"));
    CHECK_EQ(registry.atDomain("a", 0)->name, std::string("one"));
    CHECK_EQ(registry.atDomain("a", 1)->name, std::string("two"));
    CHECK_EQ(registry.at(2)->domain, std::string("z"));
}

TEST_CASE("policyregistry.eventsAreOrderedAndDescriptive") {
    PolicyRegistry registry;
    REQUIRE(registry.insert("ai", "default", 1, 0, true, "script", "", "{}").ok());
    REQUIRE(registry.enable("ai", "default", false).ok());
    REQUIRE(registry.replace("ai", "default", 2, 5, true, "batch", "", "{}").ok());
    REQUIRE(registry.remove("ai", "default").ok());
    CHECK_EQ(registry.eventCount(), 4);
    CHECK_EQ(registry.eventAt(0)->name, std::string("policy_registered"));
    CHECK_EQ(registry.eventAt(1)->name, std::string("policy_enabled"));
    CHECK(!registry.eventAt(1)->enabled);
    CHECK_EQ(registry.eventAt(3)->name, std::string("policy_removed"));
    CHECK_EQ(registry.eventAt(3)->sequence.value(), uint64_t{4});
    registry.clearEvents();
    REQUIRE(registry.insert("ai", "default", 3, 0, true, "script", "", "{}").ok());
    CHECK_EQ(registry.eventAt(0)->sequence.value(), uint64_t{5});
}

TEST_CASE("policyregistry.snapshotRoundTripsTransactionally") {
    PolicyRegistry original;
    REQUIRE(original.insert("orders", "safe", 1, 5, true, "script", "orders.policy", "{\"label\":\"Safe\"}").ok());
    REQUIRE(original.insert("orders", "old", 1, 0, true, "builtin", "", "{}").ok());
    REQUIRE(original.remove("orders", "old").ok());
    const auto snapshot = original.snapshotJson();

    PolicyRegistry restored;
    REQUIRE(restored.restoreJson(snapshot).ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
    CHECK_EQ(restored.select("orders")->name, std::string("safe"));
    REQUIRE(restored.insert("orders", "old", 2, 0, true, "batch", "", "{}").ok());
    auto oldPolicy = restored.resolve("orders", "old");
    REQUIRE(oldPolicy.ok());
    CHECK_EQ(oldPolicy.value().get().generation.value(), uint64_t{3});

    const auto before = restored.snapshotJson();
    CHECK(!restored.restoreJson("{\"version\":1}").ok());
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("policyregistry.scriptDiscoversButDoesNotStoreCallbacks") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.PolicyRegistryModule();
        local registry = module.newRegistry();
        local callback = function(subject) { return subject + ":handled"; };
        local inserted = registry.insert("administration", "governor", 1, 8, true, "script",
                                         "governor.policy", "{\"owner\":\"game-script\"}");
        if (inserted.ok) {
            local generation = inserted.value.generation;
            local resolved = registry.resolve("administration", "governor");
            local snapshot = registry.snapshotJson();
            local copy = module.newRegistry();
            local restored = copy.restoreJson(snapshot);
            local resolvedCopy = copy.resolveHandle("administration", "governor", generation);
            if (resolved.ok && restored.ok && copy.select("administration").getName() == "governor" &&
                resolvedCopy.ok && resolvedCopy.value.kind == "script")
                result = callback("general-7");
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("general-7:handled"));
}
