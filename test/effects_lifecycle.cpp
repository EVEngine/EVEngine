#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "effects/Effects.h"

#include <cmath>
#include <cstdint>
#include <string>

using namespace eve::effects;

TEST_CASE("effects.definitionPayload.usesCanonicalValueObject") {
    EffectDefinition definition;
    definition.id = "weather.wet";
    definition.payload.setString("name", "A\nB");
    definition.payload.setNumber("weight", 2.5);
    definition.payload.setBool("enabled", true);
    definition.payload.setNull("optional");

    CHECK(definition.payload.object().at("name").isString());
    CHECK(definition.payload.object().at("weight").isDouble());
    CHECK(definition.payload.object().at("enabled").isBool());
    CHECK(definition.payload.object().at("optional").isNull());
    CHECK_EQ(definition.payload.toJson(),
             std::string("{\"enabled\":true,\"name\":\"A\\nB\",\"optional\":null,\"weight\":2.5}"));

    CHECK(definition.payload.setJson("array", "[1, {\"ok\": true}]").ok());
    CHECK(definition.payload.object().at("array").isArray());
    CHECK_EQ(definition.payload.getJson("array"), std::string("[1,{\"ok\":true}]"));
    CHECK(!definition.payload.setJson("invalid", "not-json").ok());
}

TEST_CASE("effects.layers.definitionInstanceAndContainer.copyDefinitionData") {
    EffectDefinition definition;
    definition.id                = "damage.over.time";
    definition.stackKey          = "damage";
    definition.duration          = 5.0;
    definition.magnitude         = 2.0;
    definition.policy.stackMode  = StackMode::Accumulate;
    definition.policy.stackCount = StackCountPolicy::Increment;
    definition.policy.duration   = DurationPolicy::Extend;
    definition.policy.magnitude  = MagnitudePolicy::Add;
    definition.policy.maxStacks  = 3;
    definition.payload.setString("kind", "periodic");
    definition.tags = {"debuff", "damage"};

    EffectContainer container;
    auto            firstResult = container.apply(definition, "unit:1", "source:1");
    REQUIRE(firstResult.ok());
    const std::string first = std::move(firstResult).takeValue();
    definition.duration     = 99.0;
    definition.magnitude    = 99.0;
    definition.payload.setString("kind", "mutated-after-apply");

    auto secondResult = container.apply(definition, "unit:1", "source:2");
    REQUIRE(secondResult.ok());
    CHECK_EQ(std::move(secondResult).takeValue(), first);
    REQUIRE(container.find(first) != nullptr);
    CHECK_EQ(container.find(first)->stackCount, std::uint32_t{2});
    CHECK_EQ(container.find(first)->remaining, 104.0);
    CHECK_EQ(container.find(first)->magnitude, 101.0);
    CHECK_EQ(container.find(first)->source, std::string("source:1"));
    CHECK_EQ(container.find(first)->payload.getJson("kind"), std::string("\"periodic\""));
    CHECK(container.find(first)->hasTag("damage"));
    REQUIRE(container.eventAt(1) != nullptr);
    CHECK_EQ(container.eventAt(1)->kind, EffectEventKind::Stacked);
}

TEST_CASE("effects.policies.stackDurationMagnitudeAndOverflow.areIndependent") {
    EffectDefinition definition;
    definition.id                = "policy.test";
    definition.duration          = 4.0;
    definition.magnitude         = 3.0;
    definition.policy.stackMode  = StackMode::Accumulate;
    definition.policy.stackCount = StackCountPolicy::Increment;
    definition.policy.duration   = DurationPolicy::Keep;
    definition.policy.magnitude  = MagnitudePolicy::Max;
    definition.policy.maxStacks  = 2;
    definition.policy.overflow   = OverflowPolicy::Reject;

    EffectContainer container;
    auto            firstResult = container.apply(definition, "unit:2", "source");
    REQUIRE(firstResult.ok());
    const std::string id = std::move(firstResult).takeValue();
    container.update(1.0).ignore("test elapsed time");

    definition.duration  = 100.0;
    definition.magnitude = 5.0;
    auto secondResult    = container.apply(definition, "unit:2", "source");
    REQUIRE(secondResult.ok());
    std::move(secondResult).takeValue();
    CHECK_EQ(container.find(id)->stackCount, std::uint32_t{2});
    CHECK_EQ(container.find(id)->remaining, 3.0);
    CHECK_EQ(container.find(id)->magnitude, 5.0);

    auto rejectedResult = container.apply(definition, "unit:2", "source");
    CHECK(!rejectedResult.ok());
    CHECK_EQ(rejectedResult.code(), eve::StatusCode::Conflict);
    CHECK_EQ(container.find(id)->stackCount, uint32_t{2});
    CHECK_EQ(container.eventCount(), 2);
}

TEST_CASE("effects.executor.advancesOnlyLifecycleAndReportsExpiry") {
    EffectDefinition definition;
    definition.id       = "temporary";
    definition.duration = 1.0;

    EffectContainer container;
    auto            idResult = container.apply(definition, "unit:3", "source");
    REQUIRE(idResult.ok());
    const std::string id = std::move(idResult).takeValue();

    EffectExecutor executor;
    auto           invalidResult = executor.advance(container, -1.0);
    CHECK(!invalidResult.ok());
    CHECK_EQ(invalidResult.code(), eve::StatusCode::Rejected);

    auto updateResult = executor.advance(container, 1.0);
    REQUIRE(updateResult.ok());
    const auto summary = std::move(updateResult).takeValue();
    CHECK_EQ(summary.expired, std::uint32_t{1});
    CHECK(std::abs(summary.elapsedSeconds - 1.0) < 1e-12);
    CHECK(container.find(id) == nullptr);
    REQUIRE(container.eventAt(1) != nullptr);
    CHECK_EQ(container.eventAt(1)->kind, EffectEventKind::Expired);
}
