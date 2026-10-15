#include "zeroerr/assert.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_condition)
#include <map>
#include <optional>
#include <string>
#include "PrivateTestScope.h"
#include "condition/Condition.h"
#include "condition/ConditionCodec.h"
#include "condition/ConditionKeys.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
// condition.cpp
namespace test_condition {
namespace {

class TestContext : public eve::condition::EvaluationContext {
public:
    std::map<std::string, eve::Value>                     values;
    std::map<std::string, eve::condition::ConditionResult> policies;

    [[nodiscard]] std::optional<eve::Value> value(std::string_view key) const override {
        auto it = values.find(std::string(key));
        return it == values.end() ? std::nullopt : std::optional<eve::Value>(it->second);
    }
    [[nodiscard]] std::optional<eve::condition::ConditionResult> policy(std::string_view  name,
                                                                       const eve::Value& arguments) const override {
        (void)arguments;
        auto it = policies.find(std::string(name));
        return it == policies.end() ? std::nullopt : std::optional<eve::condition::ConditionResult>(it->second);
    }
};

}  // namespace

TEST_CASE("decision.condition.supportsAllLeafNodes") {
    using namespace eve::condition;
    TestContext context;
    context.values.emplace("level", 7);
    context.values.emplace(tagKey("fire"), true);
    context.values.emplace(attrKey("power"), 12.0);
    context.values.emplace(resourceKey("mana"), 20.0);
    context.values.emplace(stateKey("phase"), "ready");
    context.policies.emplace("safe", ConditionResult::success(eve::Value(true)));

    const Condition condition = Condition::all({
        Condition::compare("level", CompareOperator::GreaterEqual, 7),
        tagPresent("fire"),
        attributeExists("power"),
        resourceExists("mana"),
        stateEquals("phase", "ready"),
        authorityAllow("cast"),
        Condition::policyCall("safe"),
    });
    class AuthorityContext final : public TestContext {
    public:
        [[nodiscard]] std::optional<ConditionResult> policy(std::string_view  name,
                                                            const eve::Value& arguments) const override {
            if (auto resolved = resolveAuthorityAllow(name, arguments, [](std::string_view scope) {
                    return scope == "cast" ? std::optional<bool>(true) : std::nullopt;
                })) {
                return resolved;
            }
            return TestContext::policy(name, arguments);
        }
    };
    AuthorityContext authContext;
    authContext.values   = context.values;
    authContext.policies = context.policies;
    const auto result    = condition.evaluate(authContext);
    REQUIRE(result.passed());
    CHECK_EQ(static_cast<int>(result.reasonCode()), static_cast<int>(ConditionReasonCode::Passed));
    CHECK(result.details().isObject());
}

TEST_CASE("decision.condition.compoundsExplainFailureWithoutMutation") {
    using namespace eve::condition;
    TestContext context;
    context.values.emplace("level", 2);
    context.values.emplace(tagKey("fire"), false);
    context.values.emplace(stateKey("phase"), "ready");
    const auto before = context.values;

    const auto any = Condition::any({Condition::compare("level", CompareOperator::Greater, 9), tagPresent("fire")});
    const auto anyResult = any.evaluate(context);
    REQUIRE(!anyResult.passed());
    CHECK_EQ(static_cast<int>(anyResult.reasonCode()), static_cast<int>(ConditionReasonCode::NoChildPassed));
    CHECK(anyResult.details().isObject());

    const auto negated   = Condition::not_(stateEquals("phase", "ready"));
    const auto notResult = negated.evaluate(context);
    REQUIRE(!notResult.passed());
    CHECK_EQ(static_cast<int>(notResult.reasonCode()), static_cast<int>(ConditionReasonCode::Negated));
    CHECK_EQ(context.values, before);
}

TEST_CASE("decision.condition.scriptDeclarationIsExplicit") {
    using namespace eve::condition;
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
    using namespace eve::condition;
    TestContext context;
    const auto  tag     = tagPresent("missing").evaluate(context);
    const auto  compare = Condition::compare("missing", CompareOperator::Equal, 1).evaluate(context);
    REQUIRE(!tag.passed());
    REQUIRE(!compare.passed());
    CHECK_EQ(static_cast<int>(tag.reasonCode()), static_cast<int>(ConditionReasonCode::MissingValue));
    CHECK_EQ(static_cast<int>(compare.reasonCode()), static_cast<int>(ConditionReasonCode::MissingValue));
}

TEST_CASE("decision.condition.legacyCodecExpandsDomainLeaves") {
    using namespace eve::condition;
    auto decoded = decodeCondition(eve::Value::Object{{"kind", "has_tag"}, {"key", "fire"}});
    REQUIRE(decoded.ok());
    CHECK_EQ(static_cast<int>(decoded.value().kind()), static_cast<int>(ConditionKind::Compare));
    CHECK_EQ(decoded.value().key(), std::string("tag:fire"));
    auto authority = decodeCondition(eve::Value::Object{{"kind", "authority_check"}, {"key", "cast"}});
    REQUIRE(authority.ok());
    CHECK_EQ(static_cast<int>(authority.value().kind()), static_cast<int>(ConditionKind::PolicyCall));
    CHECK_EQ(authority.value().key(), std::string(kAuthorityAllowPolicy));
}
}  // namespace test_condition
#endif
