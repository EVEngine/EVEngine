#include "emergence/RuleEngine.h"

#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "common/IEconomy.h"
#include "common/IEmergenceActionHandler.h"
#include "common/Value.h"
#include "decision/Condition.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>
#include <vector>

namespace {

class QuestNotifyHandler final : public eve::IEmergenceActionHandler {
public:
    std::vector<std::string> topics;

    [[nodiscard]] eve::Result<bool> tryHandle(std::string_view kind, const eve::Value& args) override {
        if (kind != "quest.notify") return eve::Result<bool>::success(false);
        const auto* object = args.getIf<eve::Value::Object>();
        REQUIRE(object != nullptr);
        const auto it = object->find("topic");
        REQUIRE(it != object->end());
        REQUIRE(it->second.isString());
        topics.push_back(it->second.asString());
        return eve::Result<bool>::success(true);
    }
};

class FailingHandler final : public eve::IEmergenceActionHandler {
public:
    [[nodiscard]] eve::Result<bool> tryHandle(std::string_view kind, const eve::Value&) override {
        if (kind != "fail.always") return eve::Result<bool>::success(false);
        return eve::Result<bool>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, "forced action failure",
                                                                 "fail.always", {}, "emergence.test"));
    }
};

class StubEconomy final : public eve::economy::IEconomy {
public:
    int  credit(int, const std::string&, int amount) override { return amount; }
    bool debit(int, const std::string&, int) override { return true; }
    int  get(int, const std::string&) const override { return 0; }
    int  getCap(int, const std::string&) const override { return 0; }
    int  getWasted(int, const std::string&) const override { return 0; }
    int  getIncome(int, const std::string&) const override { return 0; }
    int  getExpense(int, const std::string&) const override { return 0; }
};

eve::emergence::RuleDefinition makeGoldRule(std::string id, int threshold, std::string emitType) {
    using namespace eve::decision;
    using namespace eve::emergence;
    RuleDefinition rule;
    rule.id        = std::move(id);
    rule.condition = Condition::compare("gold", CompareOperator::GreaterEqual, eve::Value(threshold));
    rule.actions.push_back(EmergenceAction{"emit", eve::Value::Object{{"type", eve::Value(std::move(emitType))}}});
    return rule;
}

}  // namespace

TEST_CASE("emergence.ruleEngine.risingEdgeFiresOnceUntilReset") {
    using namespace eve::emergence;
    RuleEngine engine;
    auto       committed = engine.replaceCatalogue({makeGoldRule("rich", 100, "story.rich")});
    REQUIRE(committed.ok());
    CHECK_EQ(committed.value(), 1);

    REQUIRE(engine.setValue("gold", eve::Value(50)).ok());
    auto drained = engine.drain(1);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 0);

    REQUIRE(engine.setValue("gold", eve::Value(120)).ok());
    drained = engine.drain(2);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 1);
    REQUIRE(engine.activationAt(0) != nullptr);
    CHECK_EQ(engine.activationAt(0)->ruleId, "rich");

    // Still true: rising edge must not re-fire.
    REQUIRE(engine.setValue("gold", eve::Value(200)).ok());
    drained = engine.drain(3);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 0);

    // Drop below threshold then rise again.
    REQUIRE(engine.setValue("gold", eve::Value(10)).ok());
    REQUIRE(engine.drain(4).ok());
    REQUIRE(engine.setValue("gold", eve::Value(150)).ok());
    drained = engine.drain(5);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 1);
}

TEST_CASE("emergence.ruleEngine.factSetCascadesWithinDrain") {
    using namespace eve::decision;
    using namespace eve::emergence;
    RuleEngine     engine;
    RuleDefinition unlock;
    unlock.id        = "unlock.door";
    unlock.condition = Condition::hasTag("key.found");
    unlock.actions.push_back(EmergenceAction{
        "fact.set", eve::Value::Object{
                        {"domain", eve::Value("state")}, {"key", eve::Value("door")}, {"value", eve::Value("open")}}});
    RuleDefinition celebrate;
    celebrate.id        = "celebrate";
    celebrate.condition = Condition::stateEquals("door", eve::Value("open"));
    celebrate.actions.push_back(EmergenceAction{"emit", eve::Value::Object{{"type", eve::Value("fanfare")}}});
    REQUIRE(engine.replaceCatalogue({unlock, celebrate}).ok());

    REQUIRE(engine.setTag("key.found", true).ok());
    auto drained = engine.drain(10);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 2);
    CHECK_EQ(engine.activationAt(0)->ruleId, "unlock.door");
    CHECK_EQ(engine.activationAt(1)->ruleId, "celebrate");
}

TEST_CASE("emergence.ruleEngine.questHandlerConsumesNotifyAction") {
    using namespace eve::decision;
    using namespace eve::emergence;
    QuestNotifyHandler handler;
    eve::cap::addListener<eve::IEmergenceActionHandler>(&handler, 0);

    RuleEngine     engine;
    RuleDefinition rule;
    rule.id        = "intro.done";
    rule.condition = Condition::hasTag("intro.complete");
    rule.actions.push_back(EmergenceAction{
        "quest.notify",
        eve::Value::Object{{"topic", eve::Value("intro")}, {"target", eve::Value("")}, {"amount", eve::Value(1)}}});
    REQUIRE(engine.replaceCatalogue({rule}).ok());
    REQUIRE(engine.setTag("intro.complete", true).ok());
    auto drained = engine.drain(1);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 1);
    REQUIRE_EQ(handler.topics.size(), 1u);
    CHECK_EQ(handler.topics[0], "intro");

    eve::cap::removeListener<eve::IEmergenceActionHandler>(&handler);
}

TEST_CASE("emergence.ruleEngine.replaceCatalogueJsonRoundTrip") {
    using namespace eve::emergence;
    RuleEngine  engine;
    const char* json      = R"({
      "schema": "eve.emergence.rules",
      "version": 1,
      "rules": [
        {
          "id": "late.night",
          "priority": 10,
          "fireMode": "rising",
          "condition": {
            "kind": "all",
            "children": [
              {"kind": "compare", "key": "hour", "operator": "ge", "expected": 22},
              {"kind": "has_tag", "key": "village.quiet"}
            ]
          },
          "actions": [
            {"kind": "story.begin", "args": {"eventId": "night.watch"}}
          ]
        }
      ]
    })";
    auto        committed = engine.replaceCatalogueJson(json);
    REQUIRE(committed.ok());
    CHECK_EQ(committed.value(), 1);
    REQUIRE(engine.find("late.night") != nullptr);
    CHECK_EQ(engine.find("late.night")->watchKeys.size(), 2u);

    REQUIRE(engine.setValue("hour", eve::Value(22)).ok());
    REQUIRE(engine.setTag("village.quiet", true).ok());
    auto drained = engine.drain(1);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 1);
    CHECK_EQ(engine.activationAt(0)->actions[0].kind, "story.begin");
}

TEST_CASE("emergence.ruleEngine.failedActionDoesNotCommitLatches") {
    using namespace eve::decision;
    using namespace eve::emergence;
    FailingHandler handler;
    eve::cap::addListener<eve::IEmergenceActionHandler>(&handler, 0);

    RuleEngine     engine;
    RuleDefinition rule;
    rule.id        = "once.fail";
    rule.once      = true;
    rule.condition = Condition::hasTag("go");
    rule.actions.push_back(EmergenceAction{"fail.always", eve::Value::Object{}});
    REQUIRE(engine.replaceCatalogue({rule}).ok());
    REQUIRE(engine.setTag("go", true).ok());

    auto drained = engine.drain(1);
    CHECK(!drained.ok());
    CHECK_EQ(engine.activationCount(), 0);
    CHECK_EQ(engine.dirtyCount(), 1);

    // Retry after removing the failing handler: rising/once latches must still be free.
    eve::cap::removeListener<eve::IEmergenceActionHandler>(&handler);
    drained = engine.drain(2);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 1);
    CHECK_EQ(engine.activationAt(0)->ruleId, "once.fail");
}

TEST_CASE("emergence.ruleEngine.setPolicyWakesAndSnapshotRoundTrip") {
    using namespace eve::decision;
    using namespace eve::emergence;
    RuleEngine     engine;
    RuleDefinition rule;
    rule.id        = "policy.gate";
    rule.condition = Condition::policyCall("safe");
    rule.actions.push_back(EmergenceAction{"emit", eve::Value::Object{{"type", eve::Value("ok")}}});
    REQUIRE(engine.replaceCatalogue({rule}).ok());

    REQUIRE(engine.setPolicy("safe", ConditionResult::success(eve::Value(true))).ok());
    auto drained = engine.drain(1);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 1);

    const auto snap = engine.snapshotJson();
    engine.clearActivations();
    REQUIRE(engine.setPolicy("safe", ConditionResult::failed(ConditionReasonCode::PolicyRejected)).ok());
    REQUIRE(engine.restoreJson(snap).ok());
    auto restored = engine.facts().policy("safe", eve::Value::Object{});
    REQUIRE(restored.has_value());
    CHECK(restored->passed());
}

TEST_CASE("emergence.ruleEngine.rejectsBadPolicyDependencyAndLooseNumbers") {
    using namespace eve::decision;
    using namespace eve::emergence;
    RuleEngine                 engine;
    ScriptConditionDeclaration declaration;
    declaration.name         = "custom";
    declaration.dependencies = {"gold"};  // missing domain prefix
    RuleDefinition bad;
    bad.id        = "bad.dep";
    bad.condition = Condition::policyCall("custom", eve::Value::Object{}, declaration);
    bad.actions.push_back(EmergenceAction{"emit", eve::Value::Object{}});
    CHECK(!engine.replaceCatalogue({bad}).ok());

    const char* badPriority = R"({
      "schema": "eve.emergence.rules",
      "version": 1,
      "rules": [{
        "id": "loose",
        "priority": 1.5,
        "condition": {"kind": "has_tag", "key": "x"},
        "actions": [{"kind": "emit", "args": {}}]
      }]
    })";
    CHECK(!engine.replaceCatalogueJson(badPriority).ok());

    StubEconomy economy;
    eve::cap::provide<eve::economy::IEconomy>(&economy);
    RuleDefinition pay;
    pay.id        = "pay";
    pay.condition = Condition::hasTag("bill");
    pay.actions.push_back(EmergenceAction{
        "economy.credit",
        eve::Value::Object{{"player", eve::Value("0")}, {"type", eve::Value("gold")}, {"amount", eve::Value(5)}}});
    REQUIRE(engine.replaceCatalogue({pay}).ok());
    REQUIRE(engine.setTag("bill", true).ok());
    CHECK(!engine.drain(1).ok());
    eve::cap::provide<eve::economy::IEconomy>(nullptr);
}

TEST_CASE("emergence.ruleEngine.restoreRejectsUnknownFields") {
    using namespace eve::emergence;
    RuleEngine engine;
    REQUIRE(engine.replaceCatalogue({makeGoldRule("rich", 1, "x")}).ok());
    REQUIRE(engine.setValue("gold", eve::Value(1)).ok());
    REQUIRE(engine.drain(1).ok());
    auto       snap     = engine.snapshotJson();
    auto       mutated  = snap;
    const auto insertAt = mutated.find("\"schema\"");
    REQUIRE(insertAt != std::string::npos);
    mutated.insert(insertAt, "\"extra\":true,");
    CHECK(!engine.restoreJson(mutated).ok());
    CHECK_EQ(engine.activationCount(), 1);
}
