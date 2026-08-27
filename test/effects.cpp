#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "effects/Effects.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <string>

using namespace eve::effects;

TEST_CASE("effects.apply.stableFactsAndDeterministicQueries") {
    EffectContainer effects;
    auto firstResult  = effects.apply("unit:7", "weather.wet", "zone:2", 3, 10.0, "surface", StackPolicy::Stack);
    auto secondResult = effects.apply("unit:7", "policy.tax", "state:1", 9, 0.0, "policy", StackPolicy::Stack);
    REQUIRE(firstResult.ok());
    REQUIRE(secondResult.ok());
    const auto first  = std::move(firstResult).takeValue();
    const auto second = std::move(secondResult).takeValue();

    CHECK_EQ(first, std::string("effect-0000000000000001"));
    CHECK_EQ(second, std::string("effect-0000000000000002"));
    REQUIRE(effects.subjectCount("unit:7") == 2);
    CHECK_EQ(effects.subjectAt("unit:7", 0)->id, first);
    CHECK_EQ(effects.subjectAt("unit:7", 1)->id, second);
    CHECK_EQ(effects.find(first)->type, std::string("weather.wet"));
    CHECK_EQ(effects.find(first)->source, std::string("zone:2"));
    CHECK_EQ(effects.find(second)->remaining, -1.0);
}

TEST_CASE("effects.stackPolicy.replaceStackRefresh") {
    EffectContainer effects;
    auto oldResult     = effects.apply("base:1", "policy.a", "leader:1", 1, 5.0, "administration", StackPolicy::Stack);
    auto stackedResult = effects.apply("base:1", "policy.b", "leader:2", 2, 7.0, "administration", StackPolicy::Stack);
    REQUIRE(oldResult.ok());
    REQUIRE(stackedResult.ok());
    const auto old     = std::move(oldResult).takeValue();
    const auto stacked = std::move(stackedResult).takeValue();
    CHECK_EQ(effects.effectCount(), 2);

    auto refreshedResult =
        effects.apply("base:1", "ignored.new.type", "ignored:new", 8, 12.0, "administration", StackPolicy::Refresh);
    REQUIRE(refreshedResult.ok());
    const auto refreshed = std::move(refreshedResult).takeValue();
    CHECK_EQ(refreshed, old);
    CHECK_EQ(effects.find(old)->type, std::string("policy.a"));
    CHECK_EQ(effects.find(old)->source, std::string("leader:1"));
    CHECK_EQ(effects.find(old)->priority, 8);
    CHECK_EQ(effects.find(old)->remaining, 12.0);
    CHECK_EQ(effects.effectCount(), 2);

    auto replacementResult =
        effects.apply("base:1", "policy.c", "leader:3", 4, 3.0, "administration", StackPolicy::Replace);
    REQUIRE(replacementResult.ok());
    const auto replacement = std::move(replacementResult).takeValue();
    CHECK(effects.find(old) == nullptr);
    CHECK(effects.find(stacked) == nullptr);
    REQUIRE(effects.effectCount() == 1);
    CHECK_EQ(effects.effectAt(0)->id, replacement);
    CHECK_EQ(effects.eventAt(2)->kind == EffectEventKind::Refreshed, true);
    CHECK_EQ(effects.eventAt(3)->reason, std::string("replaced"));
    CHECK_EQ(effects.eventAt(4)->reason, std::string("replaced"));
    CHECK_EQ(effects.eventAt(5)->kind == EffectEventKind::Applied, true);
}

TEST_CASE("effects.update.expiresFiniteFactsInCreationOrder") {
    EffectContainer effects;
    auto            firstResult     = effects.apply("army:1", "one", "", 0, 0.5, "", StackPolicy::Stack);
    auto            permanentResult = effects.apply("army:1", "permanent", "", 0, 0.0, "", StackPolicy::Stack);
    auto            secondResult    = effects.apply("army:1", "two", "", 0, 0.5, "", StackPolicy::Stack);
    REQUIRE(firstResult.ok());
    REQUIRE(permanentResult.ok());
    REQUIRE(secondResult.ok());
    const auto first     = std::move(firstResult).takeValue();
    const auto permanent = std::move(permanentResult).takeValue();
    const auto second    = std::move(secondResult).takeValue();
    effects.clearEvents();

    REQUIRE(effects.update(0.25).ok());
    CHECK_EQ(effects.find(first)->remaining, 0.25);
    REQUIRE(effects.update(0.25).ok());
    CHECK(effects.find(first) == nullptr);
    CHECK(effects.find(second) == nullptr);
    CHECK(effects.find(permanent) != nullptr);
    REQUIRE(effects.eventCount() == 2);
    CHECK_EQ(effects.eventAt(0)->effectId, first);
    CHECK_EQ(effects.eventAt(1)->effectId, second);
    CHECK_EQ(effects.eventAt(0)->sequence, uint64_t{4});
    CHECK_EQ(effects.eventAt(0)->kind == EffectEventKind::Expired, true);
}

TEST_CASE("effects.tagsAndPayload.areDeterministic") {
    EffectContainer effects;
    auto            idResult = effects.apply("unit:2", "condition", "system", 0, 0.0, "", StackPolicy::Stack);
    REQUIRE(idResult.ok());
    const auto      id     = std::move(idResult).takeValue();
    auto*           effect = effects.find(id);
    REQUIRE(effect != nullptr);
    CHECK(effect->addTag("visible").ok());
    CHECK(effect->addTag("administrative").ok());
    CHECK(effect->addTag("visible").status().code() == eve::StatusCode::NoOp);
    CHECK_EQ(effect->tagAt(0), std::string("administrative"));
    CHECK_EQ(effect->tagAt(1), std::string("visible"));
    CHECK_EQ(effects.taggedAt("unit:2", "visible", 0)->id, id);

    effect->payload.setString("name", "A\nB");
    effect->payload.setNumber("weight", 2.5);
    CHECK(effect->payload.setJson("data", "[1,2]").ok());
    CHECK(!effect->payload.setJson("bad", "oops").ok());
    CHECK_EQ(effect->payload.toJson(), std::string("{\"data\":[1,2],\"name\":\"A\\nB\",\"weight\":2.5}"));
}

TEST_CASE("effects.remove.emitsStableEvent") {
    EffectContainer effects;
    auto            idResult = effects.apply("subject", "type", "source", 0, 0.0, "", StackPolicy::Stack);
    REQUIRE(idResult.ok());
    const auto id = std::move(idResult).takeValue();
    CHECK(effects.remove(id, "subject_deleted").ok());
    CHECK(!effects.remove(id).ok());
    REQUIRE(effects.eventCount() == 2);
    CHECK_EQ(effects.eventAt(1)->kind == EffectEventKind::Removed, true);
    CHECK_EQ(effects.eventAt(1)->reason, std::string("subject_deleted"));
}

TEST_CASE("effects.script.containerLifecycle") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Effects();
        local effectsResult = module.newContainer();
        local effects = effectsResult.ok ? effectsResult.value : null;
        local idResult = { ok = false, value = "" };
        if (effects != null) {
            idResult = effects.apply("base:4", "policy.production", "leader:8", 5, 1.0,
                                     "production-policy", "refresh");
        }
        local id = idResult.ok ? idResult.value : "";
        local effect = effects != null && idResult.ok ? effects.find(id) : null;
        local tagResult = { ok = false };
        local payloadResult = { ok = false };
        if (effect != null) {
            tagResult = effect.addTag("economic");
            payloadResult = effect.getPayload().setJson("rate", "1.25");
        }
        local sameResult = { ok = false, value = "" };
        if (effects != null) {
            sameResult = effects.apply("base:4", "ignored", "ignored", 7, 2.0,
                                       "production-policy", "refresh");
        }
        local same = sameResult.ok ? sameResult.value : "";
        if (effectsResult.ok && idResult.ok && tagResult.ok && payloadResult.ok && sameResult.ok && same == id &&
            effect != null && effect.getType() == "policy.production" &&
            effect.getSource() == "leader:8" && effect.hasTag("economic") &&
            effect.getPayload().getJson("rate") == "1.25" &&
            effects.eventAt(1).getKind() == "refreshed") {
            result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}
