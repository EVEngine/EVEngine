#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "authority/Authority.h"
#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::authority;

TEST_CASE("authority.decision.priorityThenDenyThenCreationOrder") {
    Store      store;
    const auto grant = store.grant("actor:1", "scope:1", "edit", "role:a", 10);
    const auto deny  = store.deny("actor:1", "scope:1", "edit", "policy:a", 10);
    CHECK(!store.can("actor:1", "scope:1", "edit"));
    CHECK_EQ(store.explain("actor:1", "scope:1", "edit").winningRuleId, deny);

    const auto elevated = store.grant("actor:1", "scope:1", "edit", "override", 11);
    const auto decision = store.explain("actor:1", "scope:1", "edit");
    CHECK(decision.allowed);
    CHECK_EQ(decision.winningRuleId, elevated);
    CHECK_EQ(decision.reason, std::string("granted"));
    CHECK_EQ(store.find(grant)->source, std::string("role:a"));
}

TEST_CASE("authority.decision.defaultDenyIsExplainable") {
    Store      store;
    const auto result = store.explain("unknown", "scope", "read");
    CHECK(!result.allowed);
    CHECK_EQ(result.reason, std::string("no_matching_rule"));
    CHECK(result.winningRuleId.empty());
}

TEST_CASE("authority.lifecycle.durationRevokeAndEvents") {
    Store      store;
    const auto temporary = store.grant("a", "s", "c", "temporary", 1, 0.5);
    store.deny("b", "s", "c", "policy", 1);
    store.update(0.5);
    CHECK(store.find(temporary) == nullptr);
    REQUIRE(store.eventCount() == 3);
    CHECK_EQ(eventKindName(store.eventAt(2)->kind), std::string("expired"));
    CHECK_EQ(store.eventAt(2)->reason, std::string("duration_elapsed"));
    CHECK_EQ(store.revokeBySource("policy"), 1);
    CHECK_EQ(eventKindName(store.eventAt(3)->kind), std::string("revoked"));
}

TEST_CASE("authority.query.reverseDimensionsAreDeterministic") {
    Store      store;
    const auto first  = store.grant("a", "s2", "read", "x");
    const auto second = store.grant("a", "s1", "write", "y");
    const auto third  = store.deny("b", "s1", "read", "z");
    REQUIRE(store.queryActor("a") == 2);
    CHECK_EQ(store.queryAt(0)->id, first);
    CHECK_EQ(store.queryAt(1)->id, second);
    REQUIRE(store.queryScope("s1") == 2);
    CHECK_EQ(store.queryAt(0)->id, second);
    CHECK_EQ(store.queryAt(1)->id, third);
    REQUIRE(store.queryCapability("read") == 2);
    CHECK_EQ(store.queryAt(0)->id, first);
    CHECK_EQ(store.queryAt(1)->id, third);
}

TEST_CASE("authority.snapshot.restoreIsDeterministicAndTransactional") {
    Store original;
    original.grant("a", "s", "read", "role", 3);
    original.deny("a", "s", "write", "policy", 4, 5.0);
    original.update(1.0);
    const auto snapshot = original.snapshotJson();

    Store restored;
    auto restoredResult = restored.restoreJson(snapshot);
    REQUIRE(restoredResult.ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
    CHECK(restored.can("a", "s", "read"));
    CHECK(!restored.can("a", "s", "write"));
    const auto before = restored.snapshotJson();
    auto rejected = restored.restoreJson("{\"version\":1}");
    CHECK(!rejected.ok());
    const auto* diagnostic = rejected.error();
    REQUIRE(diagnostic != nullptr);
    CHECK_EQ(diagnostic->code(), eve::DiagnosticCode::ParseError);
    CHECK_EQ(restored.snapshotJson(), before);

    auto malformed = restored.restoreJson("{");
    CHECK(!malformed.ok());
    const auto* parseDiagnostic = malformed.error();
    REQUIRE(parseDiagnostic != nullptr);
    CHECK_EQ(parseDiagnostic->code(), eve::DiagnosticCode::ParseError);
    CHECK(!parseDiagnostic->message().empty());
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("authority.input.invalidFactsDoNotMutate") {
    Store store;
    CHECK(store.grant("", "scope", "read", "source").empty());
    CHECK(store.deny("actor", "", "read", "source").empty());
    CHECK(store.grant("actor", "scope", "", "source").empty());
    CHECK_EQ(store.query("", "", ""), 0);
    CHECK_EQ(store.eventCount(), 0);
}

TEST_CASE("authority.script.explainQueryAndLifecycle") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Authority();
        local storeResult = module.newStore();
        local store = storeResult.ok ? storeResult.value : null;
        local grantResult = store != null ? store.grant("actor:7", "scope:2", "operate", "role:3", 5, 0.0) : { ok = false };
        local denyResult = store != null ? store.deny("actor:7", "scope:2", "operate", "policy:9", 5, 1.0) : { ok = false };
        local grantId = grantResult.ok ? grantResult.value : "";
        local denyId = denyResult.ok ? denyResult.value : "";
        local before = store != null ? store.explain("actor:7", "scope:2", "operate") : null;
        if (grantResult.ok && denyResult.ok && before != null) {
            store.update(1.0);
        }
        local after = store != null ? store.explain("actor:7", "scope:2", "operate") : null;
        if (grantResult.ok && denyResult.ok && before != null && after != null &&
            !before.isAllowed() && before.getWinningRuleId() == denyId &&
            after.isAllowed() && after.getWinningRuleId() == grantId &&
            store.queryActor("actor:7") == 1 && store.eventAt(2).getKind() == "expired") {
            result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
