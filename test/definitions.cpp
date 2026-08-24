#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "definitions/Definitions.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::definitions;

TEST_CASE("definitions.registerCanonicalizeAndResolve") {
    DefinitionRegistry registry;
    REQUIRE(registry.registerDefinition("unit", "tank", 2, "{ \"speed\": 4, \"armor\": 9 }"));
    const auto* value = registry.resolve("unit", "tank");
    REQUIRE(value != nullptr);
    CHECK_EQ(value->version, 2);
    CHECK_EQ(value->generation, uint64_t{1});
    CHECK_EQ(value->json, std::string("{\"armor\":9,\"speed\":4}"));
    CHECK(!registry.registerDefinition("unit", "tank", 3, "{}"));
    CHECK(!registry.registerDefinition("", "tank", 1, "{}"));
    CHECK(!registry.registerDefinition("unit", "bad", 0, "{}"));
    CHECK(!registry.registerDefinition("unit", "bad", 1, "{"));
}

TEST_CASE("definitions.handlesInvalidateButReferencesReload") {
    DefinitionRegistry registry;
    REQUIRE(registry.registerDefinition("unit", "scout", 1, "{\"speed\":5}"));
    const DefinitionHandle oldHandle = registry.handle("unit", "scout");
    const DefinitionRef    reference = registry.reference("unit", "scout");
    REQUIRE(registry.resolveHandle(oldHandle) != nullptr);

    REQUIRE(registry.replaceDefinition("unit", "scout", 2, "{\"speed\":7}"));
    CHECK(registry.resolveHandle(oldHandle) == nullptr);
    REQUIRE(registry.resolveRef(reference) != nullptr);
    CHECK_EQ(registry.resolveRef(reference)->version, 2);
    CHECK_EQ(registry.resolveRef(reference)->generation, uint64_t{2});

    REQUIRE(registry.remove("unit", "scout"));
    CHECK(registry.resolveRef(reference) == nullptr);
    REQUIRE(registry.registerDefinition("unit", "scout", 3, "{}"));
    CHECK_EQ(registry.resolveRef(reference)->generation, uint64_t{4});
}

TEST_CASE("definitions.enumerationIsTypeThenIdSorted") {
    DefinitionRegistry registry;
    REQUIRE(registry.registerDefinition("weapon", "cannon", 1, "{}"));
    REQUIRE(registry.registerDefinition("unit", "tank", 1, "{}"));
    REQUIRE(registry.registerDefinition("unit", "infantry", 1, "{}"));
    CHECK_EQ(registry.size(), 3);
    CHECK_EQ(registry.countType("unit"), 2);
    CHECK_EQ(registry.at(0)->type, std::string("unit"));
    CHECK_EQ(registry.at(0)->id, std::string("infantry"));
    CHECK_EQ(registry.atType("unit", 1)->id, std::string("tank"));
    CHECK_EQ(registry.at(2)->type, std::string("weapon"));
}

TEST_CASE("definitions.eventsDescribeReloadAndRemoval") {
    DefinitionRegistry registry;
    REQUIRE(registry.registerDefinition("policy", "tax", 1, "{}"));
    REQUIRE(registry.replaceDefinition("policy", "tax", 2, "{}"));
    REQUIRE(registry.remove("policy", "tax"));
    CHECK_EQ(registry.eventCount(), 3);
    CHECK_EQ(registry.eventAt(0)->name, std::string("definition_reloaded"));
    CHECK_EQ(registry.eventAt(1)->generation, uint64_t{2});
    CHECK_EQ(registry.eventAt(2)->name, std::string("definition_removed"));
    CHECK_EQ(registry.eventAt(2)->sequence, uint64_t{3});
    CHECK_EQ(registry.eventAt(2)->generation, uint64_t{3});
    registry.clearEvents();
    REQUIRE(registry.registerDefinition("policy", "tax", 3, "{}"));
    CHECK_EQ(registry.eventAt(0)->sequence, uint64_t{4});
}

TEST_CASE("definitions.snapshotRoundTripsAndRestoreIsTransactional") {
    DefinitionRegistry original;
    REQUIRE(original.registerDefinition("unit", "tank", 1, "{\"b\":2,\"a\":1}"));
    REQUIRE(original.registerDefinition("unit", "obsolete", 1, "{}"));
    REQUIRE(original.remove("unit", "obsolete"));
    const std::string snapshot = original.snapshotJson();

    DefinitionRegistry restored;
    REQUIRE(restored.restoreJson(snapshot));
    CHECK_EQ(restored.snapshotJson(), snapshot);
    CHECK_EQ(restored.resolve("unit", "tank")->json, std::string("{\"a\":1,\"b\":2}"));
    REQUIRE(restored.registerDefinition("unit", "obsolete", 2, "{}"));
    CHECK_EQ(restored.resolve("unit", "obsolete")->generation, uint64_t{3});

    const std::string before = restored.snapshotJson();
    CHECK(!restored.restoreJson("{\"version\":1}"));
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("definitions.scriptRegisterReplaceEnumerateAndRestore") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Definitions();
        local registry = module.newRegistry();
        if (registry.registerDefinition("general", "g-1", 1, "{\"rank\":2}") &&
            registry.replaceDefinition("general", "g-1", 2, "{\"rank\":3}")) {
            local snapshot = registry.snapshotJson();
            local copy = module.newRegistry();
            if (copy.restoreJson(snapshot) && copy.countType("general") == 1 &&
                copy.resolve("general", "g-1").getVersion() == 2 &&
                copy.resolveGeneration("general", "g-1", 2).getVersion() == 2 &&
                copy.eventAt(1).getName() == "definition_reloaded")
                result = copy.atType("general", 0).getJson();
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("{\"rank\":3}"));
}
