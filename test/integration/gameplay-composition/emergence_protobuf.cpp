#include "common/ConditionKeys.h"
#include "emergence/FactStore.h"
#include "emergence/RuleEngine.h"
#include "emergence_catalogue.pb.h"
#include "emergence_runtime.pb.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

TEST_CASE("emergence.factStoreTypedProtobufRoundTripAndLegacyImport") {
    eve::emergence::FactStore source;
    (void)source.setValue("large", eve::Value(std::numeric_limits<std::int64_t>::max()));
    (void)source.setTag("ready", true);
    (void)source.setAttribute("strength", eve::Value(7));
    (void)source.setResource("gold", eve::Value(12));
    (void)source.setState("phase", eve::Value("active"));
    (void)source.setAuthority("village", true);
    (void)source.setPolicy("safe", eve::condition::ConditionResult::success(
                                       eve::Value(true), eve::Value::Object{{"source", eve::Value("policy")}}));

    auto bytes = source.snapshotBinary();
    REQUIRE(bytes.ok());
    auto repeated = source.snapshotBinary();
    REQUIRE(repeated.ok());
    CHECK_EQ(bytes.value(), repeated.value());
    eve::emergence::FactStore restored;
    REQUIRE(restored.restoreBinary(bytes.value()).ok());
    CHECK_EQ(restored.snapshotBinary().expect("restored fact snapshot"), bytes.value());
    auto large = restored.value("large");
    REQUIRE(large.has_value());
    CHECK_EQ(large->asInt(), std::numeric_limits<std::int64_t>::max());
    auto policy = restored.policy("safe", eve::Value::Object{});
    REQUIRE(policy.has_value());
    CHECK(policy->passed());

    eve::emergence::proto::FactStoreSave invalid;
    REQUIRE(invalid.ParseFromString(bytes.value()));
    invalid.GetReflection()->MutableUnknownFields(&invalid)->AddVarint(100, 1);
    std::string unknown;
    REQUIRE(invalid.SerializeToString(&unknown));
    CHECK(!restored.restoreBinary(unknown).ok());
    CHECK_EQ(restored.snapshotBinary().expect("unchanged fact snapshot"), bytes.value());

    eve::emergence::FactStore fromJson;
    REQUIRE(fromJson.restoreJson(source.snapshotJson()).ok());
    CHECK_EQ(fromJson.snapshotBinary().expect("Protobuf JSON facts"), bytes.value());
    REQUIRE(fromJson.restoreJson(R"({"schema":"eve.emergence.facts","version":1,"values":{"gold":7}})").ok());
    CHECK_EQ(fromJson.valueCount(), 1);
}

TEST_CASE("emergence.ruleEngineTypedProtobufRestoresAtomically") {
    eve::emergence::RuleDefinition rule;
    rule.id        = "gate";
    rule.condition = eve::condition::tagPresent("go");
    eve::emergence::RuleEngine source;
    REQUIRE(source.replaceCatalogue({rule}).ok());
    REQUIRE(source.setTag("go", true).ok());
    auto bytes = source.snapshotBinary();
    REQUIRE(bytes.ok());

    eve::emergence::RuleEngine restored;
    REQUIRE(restored.replaceCatalogue({rule}).ok());
    REQUIRE(restored.restoreBinary(bytes.value()).ok());
    CHECK_EQ(restored.snapshotBinary().expect("restored rule snapshot"), bytes.value());
    CHECK_EQ(restored.facts().hasTag("go"), std::optional<bool>(true));

    eve::emergence::proto::RuleEngineSave invalid;
    REQUIRE(invalid.ParseFromString(bytes.value()));
    invalid.mutable_rules(0)->set_id("missing");
    std::string wrongCatalogue;
    REQUIRE(invalid.SerializeToString(&wrongCatalogue));
    CHECK(!restored.restoreBinary(wrongCatalogue).ok());
    CHECK_EQ(restored.snapshotBinary().expect("unchanged rule snapshot"), bytes.value());

    eve::emergence::proto::RuleEngineSave nestedUnknown;
    REQUIRE(nestedUnknown.ParseFromString(bytes.value()));
    nestedUnknown.mutable_facts()
        ->GetReflection()
        ->MutableUnknownFields(nestedUnknown.mutable_facts())
        ->AddVarint(100, 1);
    std::string unknownFacts;
    REQUIRE(nestedUnknown.SerializeToString(&unknownFacts));
    CHECK(!restored.restoreBinary(unknownFacts).ok());
    CHECK_EQ(restored.snapshotBinary().expect("unchanged nested fact snapshot"), bytes.value());

    REQUIRE(restored.restoreJson(source.snapshotJson()).ok());
    CHECK_EQ(restored.snapshotBinary().expect("Protobuf JSON rule snapshot"), bytes.value());
    REQUIRE(
        restored
            .restoreJson(
                R"({"schema":"eve.emergence.runtime","version":1,"facts":{"schema":"eve.emergence.facts","version":1,"tags":{"go":true}},"rules":[{"id":"gate","passed":false,"fired":false,"disabled":false,"cooldownUntil":0}]})")
            .ok());
    CHECK_EQ(restored.facts().hasTag("go"), std::optional<bool>(true));
}

TEST_CASE("emergence.ruleCatalogueTypedProtobufAndLegacyJson") {
    eve::condition::ScriptConditionDeclaration declaration;
    declaration.name         = "safe";
    declaration.dependencies = {"tag:ready"};
    declaration.determinism  = eve::condition::DeterminismLevel::TickDeterministic;
    eve::emergence::RuleDefinition rule;
    rule.id            = "gate";
    rule.priority      = -3;
    rule.fireMode      = eve::emergence::FireMode::Level;
    rule.once          = true;
    rule.cooldownTicks = 42;
    rule.condition     = eve::condition::Condition::all(
        {eve::condition::Condition::compare("score", eve::condition::CompareOperator::GreaterEqual, eve::Value(7)),
             eve::condition::stateEquals("phase", eve::Value("active")),
             eve::condition::Condition::policyCall("safe", eve::Value::Object{{"strict", eve::Value(true)}}, declaration)});
    rule.actions.push_back({"emit", eve::Value::Object{{"name", eve::Value("opened")}}});
    eve::emergence::RuleEngine source;
    REQUIRE(source.replaceCatalogue({rule}).ok());
    auto bytes = source.snapshotCatalogueBinary();
    REQUIRE(bytes.ok());
    CHECK_EQ(source.snapshotCatalogueBinary().expect("deterministic catalogue"), bytes.value());

    eve::emergence::RuleEngine restored;
    REQUIRE(restored.replaceCatalogueBinary(bytes.value()).ok());
    CHECK_EQ(restored.snapshotCatalogueBinary().expect("catalogue round trip"), bytes.value());
    REQUIRE(restored.find("gate") != nullptr);
    CHECK_EQ(restored.find("gate")->priority, -3);
    const auto* actionName = restored.find("gate")->actions.front().args.find("name");
    REQUIRE(actionName != nullptr);
    CHECK_EQ(actionName->asString(), "opened");

    eve::emergence::proto::RuleCatalogueSave malformed;
    REQUIRE(malformed.ParseFromString(bytes.value()));
    malformed.mutable_rules(0)
        ->mutable_condition()
        ->GetReflection()
        ->MutableUnknownFields(malformed.mutable_rules(0)->mutable_condition())
        ->AddVarint(101, 1);
    std::string unknown;
    REQUIRE(malformed.SerializeToString(&unknown));
    CHECK(!restored.replaceCatalogueBinary(unknown).ok());
    CHECK_EQ(restored.snapshotCatalogueBinary().expect("catalogue unchanged"), bytes.value());

    eve::emergence::proto::RuleCatalogueSave future;
    REQUIRE(future.ParseFromString(bytes.value()));
    future.set_version(3);
    std::string futureBytes;
    REQUIRE(future.SerializeToString(&futureBytes));
    CHECK(!restored.replaceCatalogueBinary(futureBytes).ok());
    CHECK_EQ(restored.snapshotCatalogueBinary().expect("future catalogue unchanged"), bytes.value());

    auto json = source.snapshotCatalogueJson();
    REQUIRE(json.ok());
    REQUIRE(restored.replaceCatalogueJson(json.value()).ok());
    CHECK_EQ(restored.snapshotCatalogueBinary().expect("Protobuf JSON catalogue"), bytes.value());

    REQUIRE(
        restored
            .replaceCatalogueJson(
                R"({"schema":"eve.emergence.rules","version":1,"rules":[{"id":"legacy","condition":{"kind":"has_tag","key":"ready"}}]})")
            .ok());
    CHECK(restored.contains("legacy"));
}
