#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "common/IEconomy.h"
#include "common/IEmergenceActionHandler.h"
#include "common/Value.h"
#include "decision/Condition.h"
#include "emergence/RuleEngine.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// emergence_perf.cpp
TEST_CASE("emergence.perf.tenThousandRulesSingleKeyWakeIsSublinear") {
    using namespace eve::decision;
    using namespace eve::emergence;

    constexpr int               kRules = 10000;
    RuleEngine                  engine;
    std::vector<RuleDefinition> rules;
    rules.reserve(static_cast<std::size_t>(kRules));
    for (int i = 0; i < kRules; ++i) {
        RuleDefinition rule;
        rule.id = "rule." + std::to_string(i);
        // Each rule watches a distinct value key — changing one key must evaluate ~1 rule.
        rule.condition = Condition::compare("flag." + std::to_string(i), CompareOperator::Equal, eve::Value(true));
        rule.actions.push_back(EmergenceAction{"emit", eve::Value::Object{{"type", eve::Value(rule.id)}}});
        rules.push_back(std::move(rule));
    }
    // One shared hot key watched by 100 rules to verify fan-out stays proportional to W(k).
    for (int i = 0; i < 100; ++i) {
        RuleDefinition rule;
        rule.id        = "shared." + std::to_string(i);
        rule.condition = Condition::compare("shared.hot", CompareOperator::GreaterEqual, eve::Value(1));
        rule.once      = true;
        rule.actions.push_back(EmergenceAction{"emit", eve::Value::Object{{"type", eve::Value(rule.id)}}});
        rules.push_back(std::move(rule));
    }

    auto committed = engine.replaceCatalogue(std::move(rules));
    REQUIRE(committed.ok());
    CHECK_EQ(committed.value(), kRules + 100);

    const auto t0 = std::chrono::steady_clock::now();
    REQUIRE(engine.setValue("flag.42", eve::Value(true)).ok());
    auto       drained = engine.drain(1);
    const auto t1      = std::chrono::steady_clock::now();
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 1);
    CHECK_EQ(engine.lastDrainEvaluations(), 1u);
    const auto coldNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

    engine.clearActivations();
    const auto t2 = std::chrono::steady_clock::now();
    REQUIRE(engine.setValue("shared.hot", eve::Value(5)).ok());
    drained       = engine.drain(2);
    const auto t3 = std::chrono::steady_clock::now();
    REQUIRE(drained.ok());
    CHECK_EQ(drained.value(), 100);
    CHECK_EQ(engine.lastDrainEvaluations(), 100u);
    const auto hotNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t3 - t2).count();

    // Full-scan baseline would evaluate ~10100 rules; require at least 50x fewer evaluations.
    CHECK(engine.lastDrainEvaluations() * 50 < static_cast<std::uint64_t>(kRules + 100));

    std::fprintf(stderr, "[emergence.perf] rules=%d cold_eval=%llu cold_ns=%lld hot_eval=%llu hot_ns=%lld\n", kRules,
                 static_cast<unsigned long long>(1), static_cast<long long>(coldNs),
                 static_cast<unsigned long long>(100), static_cast<long long>(hotNs));
}

// emergence_rule_engine.cpp
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

[[maybe_unused]] eve::emergence::RuleDefinition makeGoldRule(std::string id, int threshold, std::string emitType) {
    using namespace eve::decision;
    using namespace eve::emergence;
    RuleDefinition rule;
    rule.id        = std::move(id);
    rule.condition = Condition::compare("gold", CompareOperator::GreaterEqual, eve::Value(threshold));
    rule.actions.push_back(EmergenceAction{"emit", eve::Value::Object{{"type", eve::Value(std::move(emitType))}}});
    return rule;
}

}  // namespace
