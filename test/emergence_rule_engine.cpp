#include "emergence/RuleEngine.h"

#include "common/Capability.h"
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
        "fact.set", eve::Value::Object{{"domain", eve::Value("state")},
                                       {"key", eve::Value("door")},
                                       {"value", eve::Value("open")}}});
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
    rule.actions.push_back(
        EmergenceAction{"quest.notify", eve::Value::Object{{"topic", eve::Value("intro")},
                                                           {"target", eve::Value("")},
                                                           {"amount", eve::Value(1)}}});
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
    RuleEngine engine;
    const char* json = R"({
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
    auto committed = engine.replaceCatalogueJson(json);
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
