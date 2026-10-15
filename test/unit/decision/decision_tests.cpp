#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_condition)
#include "PrivateTestScope.h"
#include <map>
#include <optional>
#include <string>
#include "decision/Condition.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// condition.cpp
namespace test_condition {
namespace {

class TestContext final : public eve::decision::EvaluationContext {
public:
    std::map<std::string, eve::Value>                     values;
    std::map<std::string, bool>                           tags;
    std::map<std::string, eve::Value>                     attributes;
    std::map<std::string, eve::Value>                     resources;
    std::map<std::string, eve::Value>                     states;
    std::map<std::string, bool>                           authorities;
    std::map<std::string, eve::decision::ConditionResult> policies;

    [[nodiscard]] std::optional<eve::Value> value(std::string_view key) const override {
        auto it = values.find(std::string(key));
        return it == values.end() ? std::nullopt : std::optional<eve::Value>(it->second);
    }
    [[nodiscard]] std::optional<bool> hasTag(std::string_view tag) const override {
        auto it = tags.find(std::string(tag));
        return it == tags.end() ? std::nullopt : std::optional<bool>(it->second);
    }
    [[nodiscard]] std::optional<eve::Value> attribute(std::string_view key) const override {
        auto it = attributes.find(std::string(key));
        return it == attributes.end() ? std::nullopt : std::optional<eve::Value>(it->second);
    }
    [[nodiscard]] std::optional<eve::Value> resource(std::string_view key) const override {
        auto it = resources.find(std::string(key));
        return it == resources.end() ? std::nullopt : std::optional<eve::Value>(it->second);
    }
    [[nodiscard]] std::optional<eve::Value> state(std::string_view key) const override {
        auto it = states.find(std::string(key));
        return it == states.end() ? std::nullopt : std::optional<eve::Value>(it->second);
    }
    [[nodiscard]] std::optional<bool> authority(std::string_view scope) const override {
        auto it = authorities.find(std::string(scope));
        return it == authorities.end() ? std::nullopt : std::optional<bool>(it->second);
    }
    [[nodiscard]] std::optional<eve::decision::ConditionResult> policy(std::string_view name,
                                                                       const eve::Value&) const override {
        auto it = policies.find(std::string(name));
        return it == policies.end() ? std::nullopt : std::optional<eve::decision::ConditionResult>(it->second);
    }
};

}  // namespace

TEST_CASE("decision.condition.supportsAllLeafNodes") {
    using namespace eve::decision;
    TestContext context;
    context.values.emplace("level", 7);
    context.tags.emplace("fire", true);
    context.attributes.emplace("power", 12.0);
    context.resources.emplace("mana", 20.0);
    context.states.emplace("phase", "ready");
    context.authorities.emplace("cast", true);
    context.policies.emplace("safe", ConditionResult::success(eve::Value(true)));

    const Condition condition = Condition::all({
        Condition::compare("level", CompareOperator::GreaterEqual, 7),
        Condition::hasTag("fire"),
        Condition::hasAttribute("power"),
        Condition::hasResource("mana"),
        Condition::stateEquals("phase", "ready"),
        Condition::authorityCheck("cast"),
        Condition::policyCall("safe"),
    });
    const auto      result    = condition.evaluate(context);
    REQUIRE(result.passed());
    CHECK_EQ(static_cast<int>(result.reasonCode()), static_cast<int>(ConditionReasonCode::Passed));
    CHECK(result.details().isObject());
}

TEST_CASE("decision.condition.compoundsExplainFailureWithoutMutation") {
    using namespace eve::decision;
    TestContext context;
    context.values.emplace("level", 2);
    context.tags.emplace("fire", false);
    context.states.emplace("phase", "ready");
    const auto before = context.values;

    const auto any =
        Condition::any({Condition::compare("level", CompareOperator::Greater, 9), Condition::hasTag("fire")});
    const auto anyResult = any.evaluate(context);
    REQUIRE(!anyResult.passed());
    CHECK_EQ(static_cast<int>(anyResult.reasonCode()), static_cast<int>(ConditionReasonCode::NoChildPassed));
    CHECK(anyResult.details().isObject());

    const auto negated   = Condition::not_(Condition::stateEquals("phase", "ready"));
    const auto notResult = negated.evaluate(context);
    REQUIRE(!notResult.passed());
    CHECK_EQ(static_cast<int>(notResult.reasonCode()), static_cast<int>(ConditionReasonCode::Negated));
    CHECK_EQ(context.values, before);
}

TEST_CASE("decision.condition.scriptDeclarationIsExplicit") {
    using namespace eve::decision;
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
    using namespace eve::decision;
    TestContext context;
    const auto  tag     = Condition::hasTag("missing").evaluate(context);
    const auto  compare = Condition::compare("missing", CompareOperator::Equal, 1).evaluate(context);
    REQUIRE(!tag.passed());
    REQUIRE(!compare.passed());
    CHECK_EQ(static_cast<int>(tag.reasonCode()), static_cast<int>(ConditionReasonCode::TagUnavailable));
    CHECK_EQ(static_cast<int>(compare.reasonCode()), static_cast<int>(ConditionReasonCode::MissingValue));
}
}  // namespace test_condition
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_decision)
#include <cmath>
#include "PrivateTestScope.h"
#include "common/ProtobufDynamicValue.h"
#include "decision/Decision.h"
#include "decision_context.pb.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// decision.cpp
namespace test_decision {
using namespace eve::decision;

TEST_CASE("decision.blackboardsAndFsmAreExplicit") {
    DecisionContext context;

    auto setResult = context.set("ai", "enemy", "\"tank-1\"");
    REQUIRE(setResult.ok());
    CHECK_EQ(context.get("ai", "enemy", "null"), std::string("\"tank-1\""));

    auto stateResult = context.setState("unit", "idle");
    REQUIRE(stateResult.ok());
    auto transitionResult = context.addTransition("unit", "idle", "enemy_seen", "attack");
    REQUIRE(transitionResult.ok());
    auto triggerResult = context.trigger("unit", "enemy_seen");
    REQUIRE(triggerResult.ok());
    REQUIRE(triggerResult.value());
    CHECK_EQ(context.state("unit"), std::string("attack"));
}

TEST_CASE("decision.utilityIsDeterministic") {
    CHECK(std::abs(DecisionContext::utility("1:2,0:1") - .6666667f) < .001f);
    CHECK_EQ(DecisionContext::choose("retreat=0.8:1;attack=0.8:1"), std::string("attack"));
}

TEST_CASE("decision.influenceAndSnapshotRoundTrip") {
    DecisionContext context;

    auto gridResult = context.newGrid("threat", 2, 2, 10, 0, 0);
    REQUIRE(gridResult.ok());
    auto setCellResult = context.setCell("threat", 1, 0, 2);
    REQUIRE(setCellResult.ok());
    auto addCellResult = context.addCell("threat", 1, 0, 3);
    REQUIRE(addCellResult.ok());
    CHECK_EQ(context.sample("threat", 15, 5, -1), 5.f);

    auto            snapshot = context.snapshotJson();
    DecisionContext restored;
    auto            restoreResult = restored.restoreJson(snapshot);
    REQUIRE(restoreResult.ok());
    CHECK_EQ(restored.snapshotJson(), snapshot);

    auto beforeInvalidRestore = restored.snapshotJson();
    auto rejected             = restored.restoreJson("{}");
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Failed);
    CHECK_EQ(restored.snapshotJson(), beforeInvalidRestore);
}
TEST_CASE("decision.protobufSnapshotRejectsInvalidWithoutMutation") {
    DecisionContext source;
    REQUIRE(source.set("ai", "target", "\"tank\"").ok());
    REQUIRE_EQ(source.get("ai", "target", "null"), std::string("\"tank\""));
    REQUIRE(source.setState("unit", "idle").ok());
    REQUIRE(source.addTransition("unit", "idle", "seen", "attack").ok());
    REQUIRE(source.newGrid("threat", 2, 1, 4, 0, 0).ok());
    REQUIRE(source.setCell("threat", 1, 0, 3).ok());
    auto bytes = source.snapshotBinary();
    REQUIRE(bytes.ok());
    auto repeatedBytes = source.snapshotBinary();
    REQUIRE(repeatedBytes.ok());
    CHECK_EQ(bytes.value(), repeatedBytes.value());

    DecisionContext restored;
    REQUIRE(restored.restoreBinary(bytes.value()).ok());
    CHECK_EQ(restored.snapshotJson(), source.snapshotJson());
    REQUIRE(restored.trigger("unit", "seen").ok());
    CHECK_EQ(restored.state("unit"), std::string("attack"));

    eve::decision::proto::DecisionContextSave invalid;
    REQUIRE(invalid.ParseFromString(bytes.value()));
    REQUIRE_EQ(invalid.version(), 3u);
    REQUIRE_EQ(invalid.boards_size(), 1);
    REQUIRE_EQ(invalid.boards(0).name(), "ai");
    CHECK(invalid.boards(0).scalar_json().empty());
    const auto& scalars = invalid.boards(0).scalar_values();
    REQUIRE_EQ(scalars.size(), 1u);
    for (const auto& [key, scalar] : scalars) {
        CHECK_EQ(key, "target");
        CHECK_EQ(scalar.string_value(), "tank");
    }
    invalid.set_version(4);
    std::string future;
    REQUIRE(invalid.SerializeToString(&future));
    CHECK(!restored.restoreBinary(future).ok());
    CHECK_EQ(restored.state("unit"), std::string("attack"));

    invalid.set_version(3);
    invalid.mutable_grids(0)->set_width(3);
    std::string malformed;
    REQUIRE(invalid.SerializeToString(&malformed));
    CHECK(!restored.restoreBinary(malformed).ok());
    CHECK_EQ(restored.sample("threat", 5, 0, -1), 3.f);

    invalid.mutable_grids(0)->set_width(2);
    invalid.GetReflection()->MutableUnknownFields(&invalid)->AddVarint(100, 1);
    std::string withUnknown;
    REQUIRE(invalid.SerializeToString(&withUnknown));
    CHECK(!restored.restoreBinary(withUnknown).ok());
    CHECK_EQ(restored.sample("threat", 5, 0, -1), 3.f);

    eve::decision::proto::DecisionContextSave legacy;
    REQUIRE(legacy.ParseFromString(bytes.value()));
    legacy.set_version(2);
    for (auto& board : *legacy.mutable_boards()) {
        for (const auto& [key, scalar] : board.scalar_values()) {
            auto decoded = eve::proto_support::decodeDynamicValue(scalar);
            REQUIRE(decoded.ok());
            auto json = decoded.value().toJson();
            REQUIRE(json.ok());
            (*board.mutable_scalar_json())[key] = json.value();
        }
        board.mutable_scalar_values()->clear();
    }
    std::string legacyBytes;
    REQUIRE(legacy.SerializeToString(&legacyBytes));
    DecisionContext migrated;
    REQUIRE(migrated.restoreBinary(legacyBytes).ok());
    CHECK_EQ(migrated.get("ai", "target", "null"), std::string("\"tank\""));

    REQUIRE(restored.restoreJson(source.snapshotJson()).ok());
    REQUIRE(
        restored
            .restoreJson(R"({"version":1,"boards":{"ai":{"target":"tank"}},"states":{},"transitions":[],"grids":[]})")
            .ok());
    CHECK_EQ(restored.get("ai", "target", "null"), std::string("\"tank\""));
}
}  // namespace test_decision
#endif
