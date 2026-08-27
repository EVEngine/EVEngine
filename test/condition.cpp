#include "decision/Condition.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <map>
#include <optional>
#include <string>

namespace {

class TestContext final : public eve::decision::EvaluationContext {
public:
    std::map<std::string, eve::Value> values;
    std::map<std::string, bool> tags;
    std::map<std::string, eve::Value> attributes;
    std::map<std::string, eve::Value> resources;
    std::map<std::string, eve::Value> states;
    std::map<std::string, bool> authorities;
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
    [[nodiscard]] std::optional<eve::decision::ConditionResult> policy(
        std::string_view name, const eve::Value&) const override {
        auto it = policies.find(std::string(name));
        return it == policies.end() ? std::nullopt
                                     : std::optional<eve::decision::ConditionResult>(it->second);
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
    const auto result = condition.evaluate(context);
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

    const auto any = Condition::any({Condition::compare("level", CompareOperator::Greater, 9),
                                     Condition::hasTag("fire")});
    const auto anyResult = any.evaluate(context);
    REQUIRE(!anyResult.passed());
    CHECK_EQ(static_cast<int>(anyResult.reasonCode()), static_cast<int>(ConditionReasonCode::NoChildPassed));
    CHECK(anyResult.details().isObject());

    const auto negated = Condition::not_(Condition::stateEquals("phase", "ready"));
    const auto notResult = negated.evaluate(context);
    REQUIRE(!notResult.passed());
    CHECK_EQ(static_cast<int>(notResult.reasonCode()), static_cast<int>(ConditionReasonCode::Negated));
    CHECK_EQ(context.values, before);
}

TEST_CASE("decision.condition.scriptDeclarationIsExplicit") {
    using namespace eve::decision;
    ScriptConditionDeclaration declaration;
    declaration.name = "game.canCast";
    declaration.dependencies = {"actor.mana", "world.tick"};
    declaration.determinism = DeterminismLevel::TickDeterministic;
    const auto condition = Condition::policyCall("game.canCast", eve::Value::Object{{"mode", "safe"}},
                                                  declaration);
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
    const auto tag = Condition::hasTag("missing").evaluate(context);
    const auto compare = Condition::compare("missing", CompareOperator::Equal, 1).evaluate(context);
    REQUIRE(!tag.passed());
    REQUIRE(!compare.passed());
    CHECK_EQ(static_cast<int>(tag.reasonCode()), static_cast<int>(ConditionReasonCode::TagUnavailable));
    CHECK_EQ(static_cast<int>(compare.reasonCode()), static_cast<int>(ConditionReasonCode::MissingValue));
}
