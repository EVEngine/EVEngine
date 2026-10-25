// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "emergence_perf.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "common/Condition.h"
#include "common/ConditionKeys.h"
#include "common/Diagnostic.h"
#include "common/IEmergenceActionHandler.h"
#include "emergence/RuleEngine.h"

#include <string>
#include <vector>

using namespace eve::emergence;

namespace {

RuleDefinition goldRule(std::string id, int threshold, std::string event) {
    RuleDefinition rule;
    rule.id        = std::move(id);
    rule.condition = eve::condition::Condition::compare("gold", eve::condition::CompareOperator::GreaterEqual,
                                                        eve::Value(threshold));
    rule.actions.push_back({"emit", eve::Value::Object{{"type", eve::Value(std::move(event))}}});
    return rule;
}

class RecordingHandler final : public eve::IEmergenceActionHandler {
public:
    std::vector<std::string> topics;
    bool                     reject = false;

    [[nodiscard]] eve::Result<bool> tryHandle(std::string_view kind, const eve::Value& args) override {
        if (kind != "quest.notify") return eve::Result<bool>::success(false);
        if (reject)
            return eve::Result<bool>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected handler failure", "quest.notify"));
        const auto* object = args.getIf<eve::Value::Object>();
        if (!object) return eve::Result<bool>::success(false);
        auto topic = object->find("topic");
        if (topic == object->end() || !topic->second.isString()) return eve::Result<bool>::success(false);
        topics.push_back(topic->second.asString());
        return eve::Result<bool>::success(true);
    }
};

}  // namespace

TEST_CASE("emergence.RuleEngine catalogue commits valid rules and rejects invalid replacements atomically") {
    RuleEngine engine;
    auto       committed = engine.replaceCatalogue({goldRule("rich", 100, "story.rich")});
    REQUIRE(committed.ok());
    CHECK_EQ(committed.value(), 1);
    CHECK(engine.contains("rich"));
    CHECK(engine.find("missing") == nullptr);

    RuleDefinition invalid = goldRule("bad", 1, "bad");
    invalid.id.clear();
    CHECK(!engine.replaceCatalogue({invalid}).ok());
    CHECK_EQ(engine.ruleCount(), 1);
    CHECK(engine.find("rich") != nullptr);
    CHECK(!engine.replaceCatalogue({goldRule("same", 1, "a"), goldRule("same", 2, "b")}).ok());
    CHECK_EQ(engine.ruleCount(), 1);
}

TEST_CASE("emergence.RuleEngine rises only after a false-to-true fact transition") {
    RuleEngine engine;
    REQUIRE(engine.replaceCatalogue({goldRule("rich", 100, "story.rich")}).ok());
    REQUIRE(engine.setValue("gold", eve::Value(50)).ok());
    auto drained = engine.drain(1);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 0);

    REQUIRE(engine.setValue("gold", eve::Value(120)).ok());
    drained = engine.drain(2);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 1);
    REQUIRE(engine.activationAt(0) != nullptr);
    CHECK_EQ(engine.activationAt(0)->ruleId, std::string("rich"));

    REQUIRE(engine.setValue("gold", eve::Value(200)).ok());
    drained = engine.drain(3);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 0);
    REQUIRE(engine.setValue("gold", eve::Value(10)).ok());
    REQUIRE(engine.drain(4).ok());
    REQUIRE(engine.setValue("gold", eve::Value(150)).ok());
    drained = engine.drain(5);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 1);
    CHECK_EQ(engine.activationCount(), 2);
}

TEST_CASE("emergence.RuleEngine fact actions cascade inside one drain") {
    RuleDefinition unlock;
    unlock.id        = "unlock";
    unlock.condition = eve::condition::tagPresent("key.found");
    unlock.actions.push_back(
        {"fact.set", eve::Value::Object{
                         {"domain", eve::Value("state")}, {"key", eve::Value("door")}, {"value", eve::Value("open")}}});
    RuleDefinition celebrate;
    celebrate.id        = "celebrate";
    celebrate.condition = eve::condition::stateEquals("door", eve::Value("open"));
    celebrate.actions.push_back({"emit", eve::Value::Object{{"type", eve::Value("fanfare")}}});
    RuleEngine engine;
    REQUIRE(engine.replaceCatalogue({unlock, celebrate}).ok());
    REQUIRE(engine.setTag("key.found", true).ok());
    auto drained = engine.drain(10);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 2);
    REQUIRE(engine.activationAt(0) != nullptr);
    REQUIRE(engine.activationAt(1) != nullptr);
    CHECK_EQ(engine.activationAt(0)->ruleId, std::string("unlock"));
    CHECK_EQ(engine.activationAt(1)->ruleId, std::string("celebrate"));
}

TEST_CASE("emergence.RuleEngine external handler failure preserves activation latch for retry") {
    RecordingHandler handler;
    handler.reject = true;
    eve::cap::addListener<eve::IEmergenceActionHandler>(&handler, 0);
    RuleDefinition rule;
    rule.id        = "once";
    rule.once      = true;
    rule.condition = eve::condition::tagPresent("go");
    rule.actions.push_back({"quest.notify", eve::Value::Object{{"topic", eve::Value("start")}}});
    RuleEngine engine;
    REQUIRE(engine.replaceCatalogue({rule}).ok());
    REQUIRE(engine.setTag("go", true).ok());
    auto failed = engine.drain(1);
    CHECK(!failed.ok());
    CHECK_EQ(engine.activationCount(), 0);
    CHECK_EQ(engine.dirtyCount(), 1);
    CHECK(handler.topics.empty());

    handler.reject = false;
    auto retried   = engine.drain(2);
    REQUIRE(retried.ok());
    CHECK_EQ(retried.value(), 1);
    REQUIRE_EQ(handler.topics.size(), std::size_t{1});
    CHECK_EQ(handler.topics[0], std::string("start"));
    auto suppressed = engine.drain(3);
    REQUIRE(suppressed.ok());
    CHECK_EQ(suppressed.value(), 0);
    eve::cap::removeListener<eve::IEmergenceActionHandler>(&handler);
}

TEST_CASE("emergence.RuleEngine JSON catalogue derives watched keys and executes matching facts") {
    constexpr const char* json = R"({"schema":"eve.emergence.rules","version":1,"rules":[{
      "id":"late.night","priority":10,"fireMode":"rising",
      "condition":{"kind":"all","children":[
        {"kind":"compare","key":"hour","operator":"ge","expected":22},
        {"kind":"has_tag","key":"village.quiet"}]},
      "actions":[{"kind":"story.begin","args":{"eventId":"night.watch"}}]}]})";
    RuleEngine            engine;
    auto                  accepted = engine.replaceCatalogueJson(json);
    REQUIRE(accepted.ok());
    CHECK_EQ(accepted.value(), 1);
    REQUIRE(engine.find("late.night") != nullptr);
    CHECK_EQ(engine.find("late.night")->watchKeys.size(), std::size_t{2});
    REQUIRE(engine.setValue("hour", eve::Value(22)).ok());
    REQUIRE(engine.setTag("village.quiet", true).ok());
    auto result = engine.drain(1);
    REQUIRE(result.ok());
    CHECK_EQ(result.value(), 1);
    CHECK_EQ(engine.activationAt(0)->actions[0].kind, std::string("story.begin"));
    CHECK(!engine.replaceCatalogueJson(R"({"schema":"eve.emergence.rules","version":2,"rules":[]})").ok());
    CHECK_EQ(engine.ruleCount(), 1);
}

TEST_CASE("emergence.RuleEngine restore rejects unknown fields without changing observable state") {
    RuleEngine engine;
    REQUIRE(engine.replaceCatalogue({goldRule("rich", 1, "x")}).ok());
    REQUIRE(engine.setValue("gold", eve::Value(1)).ok());
    REQUIRE(engine.drain(1).ok());
    const auto snapshot  = engine.snapshotJson();
    auto       malformed = snapshot;
    const auto insertion = malformed.find("\"schema\"");
    REQUIRE(insertion != std::string::npos);
    malformed.insert(insertion, "\"extra\":true,");
    CHECK(!engine.restoreJson(malformed).ok());
    CHECK_EQ(engine.snapshotJson(), snapshot);

    engine.clearActivations();
    CHECK_EQ(engine.activationCount(), 0);
    REQUIRE(engine.restoreJson(snapshot).ok());
    CHECK_EQ(engine.activationCount(), 0);
}

TEST_CASE("emergence.RuleEngine indexed wake evaluates only rules watching the changed key") {
    RuleEngine                  engine;
    std::vector<RuleDefinition> rules;
    for (int i = 0; i < 100; ++i) rules.push_back(goldRule("threshold." + std::to_string(i), 1000 + i, "unreachable"));
    REQUIRE(engine.replaceCatalogue(std::move(rules)).ok());
    REQUIRE(engine.setValue("unwatched", eve::Value(1)).ok());
    auto drained = engine.drain(1);
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 0);
    CHECK_EQ(engine.lastDrainEvaluations(), std::uint64_t{0});
    REQUIRE(engine.setValue("gold", eve::Value(0)).ok());
    drained = engine.drain(2);
    REQUIRE(drained.ok());
    CHECK_EQ(engine.lastDrainEvaluations(), std::uint64_t{100});
}
