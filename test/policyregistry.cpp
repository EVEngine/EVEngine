#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "policyregistry/PolicyRegistry.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::policyregistry;

TEST_CASE("policyregistry.registersCanonicalDescriptors") {
    PolicyRegistry registry;
    REQUIRE(registry.registerPolicy("movement", "ground", 2, 10, true, "builtin", "move.schema", "{\"z\":2,\"a\":1}"));
    const auto* policy = registry.resolve("movement", "ground");
    REQUIRE(policy != nullptr);
    CHECK_EQ(policy->version, 2);
    CHECK_EQ(policy->priority, 10);
    CHECK(policy->enabled);
    CHECK_EQ(implementationKindName(policy->kind), std::string("builtin"));
    CHECK_EQ(policy->metadataJson, std::string("{\"a\":1,\"z\":2}"));
    CHECK(!registry.registerPolicy("movement", "ground", 1, 0, true, "script", "", "{}"));
    CHECK(!registry.registerPolicy("", "bad", 1, 0, true, "script", "", "{}"));
    CHECK(!registry.registerPolicy("movement", "bad", 0, 0, true, "script", "", "{}"));
    CHECK(!registry.registerPolicy("movement", "bad", 1, 0, true, "remote", "", "{}"));
    CHECK(!registry.registerPolicy("movement", "bad", 1, 0, true, "script", "", "[]"));
}

TEST_CASE("policyregistry.selectsEnabledPriorityThenName") {
    PolicyRegistry registry;
    REQUIRE(registry.registerPolicy("targeting", "zeta", 1, 20, true, "script", "", "{}"));
    REQUIRE(registry.registerPolicy("targeting", "alpha", 1, 20, true, "batch", "", "{}"));
    REQUIRE(registry.registerPolicy("targeting", "disabled", 1, 99, false, "builtin", "", "{}"));
    REQUIRE(registry.select("targeting") != nullptr);
    CHECK_EQ(registry.select("targeting")->name, std::string("alpha"));
    REQUIRE(registry.enable("targeting", "disabled", true));
    CHECK_EQ(registry.select("targeting")->name, std::string("disabled"));
    REQUIRE(registry.enable("targeting", "disabled", false));
    CHECK_EQ(registry.select("targeting")->name, std::string("alpha"));
    CHECK(registry.select("missing") == nullptr);
}

TEST_CASE("policyregistry.generationsInvalidateStaleHandles") {
    PolicyRegistry registry;
    REQUIRE(registry.registerPolicy("economy", "tax", 1, 0, true, "script", "tax.v1", "{}"));
    const auto old = registry.handle("economy", "tax");
    REQUIRE(registry.resolveHandle(old) != nullptr);
    REQUIRE(registry.replacePolicy("economy", "tax", 2, 1, true, "batch", "tax.v2", "{}"));
    CHECK(registry.resolveHandle(old) == nullptr);
    CHECK_EQ(registry.resolve("economy", "tax")->generation, uint64_t{2});
    REQUIRE(registry.remove("economy", "tax"));
    REQUIRE(registry.registerPolicy("economy", "tax", 3, 2, true, "script", "tax.v3", "{}"));
    CHECK_EQ(registry.resolve("economy", "tax")->generation, uint64_t{4});
}

TEST_CASE("policyregistry.enumeratesDeterministically") {
    PolicyRegistry registry;
    REQUIRE(registry.registerPolicy("z", "last", 1, 0, true, "script", "", "{}"));
    REQUIRE(registry.registerPolicy("a", "two", 1, 0, true, "script", "", "{}"));
    REQUIRE(registry.registerPolicy("a", "one", 1, 0, true, "script", "", "{}"));
    CHECK_EQ(registry.size(), 3);
    CHECK_EQ(registry.countDomain("a"), 2);
    CHECK_EQ(registry.at(0)->domain, std::string("a"));
    CHECK_EQ(registry.atDomain("a", 0)->name, std::string("one"));
    CHECK_EQ(registry.atDomain("a", 1)->name, std::string("two"));
    CHECK_EQ(registry.at(2)->domain, std::string("z"));
}

TEST_CASE("policyregistry.eventsAreOrderedAndDescriptive") {
    PolicyRegistry registry;
    REQUIRE(registry.registerPolicy("ai", "default", 1, 0, true, "script", "", "{}"));
    REQUIRE(registry.enable("ai", "default", false));
    REQUIRE(registry.replacePolicy("ai", "default", 2, 5, true, "batch", "", "{}"));
    REQUIRE(registry.remove("ai", "default"));
    CHECK_EQ(registry.eventCount(), 4);
    CHECK_EQ(registry.eventAt(0)->name, std::string("policy_registered"));
    CHECK_EQ(registry.eventAt(1)->name, std::string("policy_enabled"));
    CHECK(!registry.eventAt(1)->enabled);
    CHECK_EQ(registry.eventAt(3)->name, std::string("policy_removed"));
    CHECK_EQ(registry.eventAt(3)->sequence, uint64_t{4});
    registry.clearEvents();
    REQUIRE(registry.registerPolicy("ai", "default", 3, 0, true, "script", "", "{}"));
    CHECK_EQ(registry.eventAt(0)->sequence, uint64_t{5});
}

TEST_CASE("policyregistry.snapshotRoundTripsTransactionally") {
    PolicyRegistry original;
    REQUIRE(original.registerPolicy("orders", "safe", 1, 5, true, "script", "orders.policy", "{\"label\":\"Safe\"}"));
    REQUIRE(original.registerPolicy("orders", "old", 1, 0, true, "builtin", "", "{}"));
    REQUIRE(original.remove("orders", "old"));
    const auto snapshot = original.snapshotJson();

    PolicyRegistry restored;
    REQUIRE(restored.restoreJson(snapshot));
    CHECK_EQ(restored.snapshotJson(), snapshot);
    CHECK_EQ(restored.select("orders")->name, std::string("safe"));
    REQUIRE(restored.registerPolicy("orders", "old", 2, 0, true, "batch", "", "{}"));
    CHECK_EQ(restored.resolve("orders", "old")->generation, uint64_t{3});

    const auto before = restored.snapshotJson();
    CHECK(!restored.restoreJson("{\"version\":1}"));
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
        if (registry.registerPolicy("administration", "governor", 1, 8, true, "script",
                                    "governor.policy", "{\"owner\":\"game-script\"}")) {
            local generation = registry.resolve("administration", "governor").getGeneration();
            local snapshot = registry.snapshotJson();
            local copy = module.newRegistry();
            if (copy.restoreJson(snapshot) && copy.select("administration").getName() == "governor" &&
                copy.resolveGeneration("administration", "governor", generation).getKind() == "script")
                result = callback("general-7");
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("general-7:handled"));
}
