#include "decision/Condition.h"
#include "emergence/FactStore.h"
#include "emergence/RuleEngine.h"
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
    (void)source.setPolicy("safe", eve::decision::ConditionResult::success(
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
    rule.condition = eve::decision::Condition::hasTag("go");
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
