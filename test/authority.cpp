#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "authority/Authority.h"
#include "common/Module.h"
#include "common/SnapshotHash.h"

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
    auto  restoredResult = restored.restoreJson(snapshot);
    REQUIRE(restoredResult.ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);
    CHECK(restored.can("a", "s", "read"));
    CHECK(!restored.can("a", "s", "write"));
    const auto before = restored.snapshotJson();
    auto       rejected = restored.restoreJson("{\"version\":1}");
    REQUIRE(!rejected.ok());
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

TEST_CASE("authority.snapshot.protobufRoundTripAndFailedRestoreAreTransactional") {
    Store source;
    source.grant("actor", "scene", "edit", "role", 3);
    source.deny("actor", "scene", "delete", "policy", 5, 2.0);
    source.update(0.5);
    auto encoded = source.snapshotBinary();
    REQUIRE(encoded.ok());

    Store restored;
    REQUIRE(restored.restoreBinary(encoded.value()).ok());
    CHECK(restored.can("actor", "scene", "edit"));
    CHECK(!restored.can("actor", "scene", "delete"));
    auto repeated = restored.snapshotBinary();
    REQUIRE(repeated.ok());
    CHECK_EQ(repeated.value(), encoded.value());

    const auto before = restored.snapshotJson();
    CHECK(!restored.restoreBinary("\x00\x01\x02").ok());
    CHECK_EQ(restored.snapshotJson(), before);
    std::string truncated = encoded.value();
    truncated.pop_back();
    CHECK(!restored.restoreBinary(truncated).ok());
    CHECK_EQ(restored.snapshotJson(), before);
    std::string futureField = encoded.value();
    futureField.append("\xA0\x06\x01", 3);  // Unknown field 100, varint 1.
    CHECK(!restored.restoreBinary(futureField).ok());
    CHECK_EQ(restored.snapshotJson(), before);
}

TEST_CASE("authority.snapshot.legacyJsonIsReadButRewrittenWithProtobufSchema") {
    const std::string legacy =
        R"({"version":1,"nextId":"2","nextOrder":"2","nextSequence":"2","rules":[{"id":"authority-0000000000000001","actor":"actor","scope":"scene","capability":"edit","source":"role","effect":"grant","priority":3,"duration":0,"remaining":-1,"order":"1"}]})";
    Store restored;
    REQUIRE(restored.restoreJson(legacy).ok());
    CHECK(restored.can("actor", "scene", "edit"));
    const auto rewritten = restored.snapshotJson();
    CHECK(rewritten.find("authority:store") != std::string::npos);
    CHECK(rewritten != legacy);
    Store copy;
    REQUIRE(copy.restoreJson(rewritten).ok());
    CHECK(copy.can("actor", "scene", "edit"));
}

TEST_CASE("authority.snapshot.typedEnvelopeVerifiesDigestAndIdentity") {
    const auto identity = eve::PersistentId::parse("11111111-2222-4333-8444-555555555555");
    REQUIRE(identity.has_value());
    const auto hash = eve::snapshotContentHashProvider();
    Store      source(*identity);
    source.grant("actor", "scene", "edit", "role", 3);
    auto wire = source.snapshotEnvelopeBinary(hash);
    REQUIRE(wire.ok());

    Store restored(*identity);
    REQUIRE(restored.restoreSnapshotBinary(wire.value(), hash).ok());
    CHECK(restored.can("actor", "scene", "edit"));
    const auto  stable   = restored.snapshotJson();
    std::string tampered = wire.value();
    const auto  position = tampered.find("actor");
    REQUIRE(position != std::string::npos);
    tampered[position] = 'x';
    auto rejected      = restored.restoreSnapshotBinary(tampered, hash);
    REQUIRE(!rejected.ok());
    CHECK_EQ(rejected.error()->code(), eve::DiagnosticCode::HashMismatch);
    CHECK_EQ(restored.snapshotJson(), stable);

    auto json = source.snapshotEnvelopeJson(hash);
    REQUIRE(json.ok());
    CHECK(json.value().find("\"payload\"") != std::string::npos);
    Store fromJson(*identity);
    REQUIRE(fromJson.restoreSnapshotJson(json.value(), hash).ok());
    CHECK_EQ(fromJson.snapshotJson(), source.snapshotJson());

    auto parsedJson = eve::Value::fromJson(json.value());
    REQUIRE(parsedJson.ok());
    auto* fields = parsedJson.value().getIf<eve::Value::Object>();
    REQUIRE(fields != nullptr);
    (*fields)["revision"] = eve::Value("900");
    auto changedHeader    = parsedJson.value().toJson();
    REQUIRE(changedHeader.ok());
    auto headerFailure = fromJson.restoreSnapshotJson(changedHeader.value(), hash);
    REQUIRE(!headerFailure.ok());
    CHECK_EQ(headerFailure.error()->code(), eve::DiagnosticCode::HashMismatch);
    CHECK_EQ(fromJson.snapshotJson(), source.snapshotJson());

    auto compatible = source.snapshot(hash);
    REQUIRE(compatible.ok());
    auto oldEnvelope = eve::serializeSnapshotEnvelope(compatible.value());
    REQUIRE(oldEnvelope.ok());
    Store fromOldEnvelope(*identity);
    REQUIRE(fromOldEnvelope.restoreSnapshotJson(oldEnvelope.value(), hash).ok());
    CHECK_EQ(fromOldEnvelope.snapshotJson(), source.snapshotJson());

    const auto foreignId = eve::PersistentId::parse("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    REQUIRE(foreignId.has_value());
    Store foreign(*foreignId);
    CHECK(!foreign.restoreSnapshotBinary(wire.value(), hash).ok());
    CHECK(foreign.snapshotJson() != source.snapshotJson());
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
