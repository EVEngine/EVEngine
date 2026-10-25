#include "effects/Effects.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <cstdint>
#include <string>

using namespace eve::effects;

TEST_CASE("effects.legacy_apply_creates_stable_facts_in_creation_order") {
    EffectContainer container;
    const auto      first  = container.apply("unit:7", "weather.wet", "zone:2", 3, 10.0, "surface", StackPolicy::Stack);
    const auto      second = container.apply("unit:7", "policy.tax", "state:1", 9, 0.0, "policy", StackPolicy::Stack);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value(), std::string("effect-0000000000000001"));
    CHECK_EQ(second.value(), std::string("effect-0000000000000002"));
    CHECK_EQ(container.subjectCount("unit:7"), 2);
    REQUIRE(container.subjectAt("unit:7", 0) != nullptr);
    REQUIRE(container.subjectAt("unit:7", 1) != nullptr);
    CHECK_EQ(container.subjectAt("unit:7", 0)->id, first.value());
    CHECK_EQ(container.subjectAt("unit:7", 1)->id, second.value());
    CHECK_EQ(container.find(first.value())->type, std::string("weather.wet"));
    CHECK_EQ(container.find(first.value())->source, std::string("zone:2"));
    CHECK_EQ(container.find(second.value())->remaining, -1.0);
    CHECK(container.effectAt(2) == nullptr);
}

TEST_CASE("effects.legacy_stack_refresh_and_replace_have_distinct_identity_rules") {
    EffectContainer container;
    const auto first  = container.apply("base:1", "policy.a", "leader:1", 1, 5.0, "administration", StackPolicy::Stack);
    const auto second = container.apply("base:1", "policy.b", "leader:2", 2, 7.0, "administration", StackPolicy::Stack);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(container.effectCount(), 2);

    const auto refreshed =
        container.apply("base:1", "ignored", "ignored", 8, 12.0, "administration", StackPolicy::Refresh);
    REQUIRE(refreshed.ok());
    CHECK_EQ(refreshed.value(), first.value());
    CHECK_EQ(container.find(first.value())->type, std::string("policy.a"));
    CHECK_EQ(container.find(first.value())->source, std::string("leader:1"));
    CHECK_EQ(container.find(first.value())->priority, 8);
    CHECK_EQ(container.find(first.value())->remaining, 12.0);
    CHECK_EQ(container.effectCount(), 2);

    const auto replacement =
        container.apply("base:1", "policy.c", "leader:3", 4, 3.0, "administration", StackPolicy::Replace);
    REQUIRE(replacement.ok());
    CHECK(container.find(first.value()) == nullptr);
    CHECK(container.find(second.value()) == nullptr);
    CHECK_EQ(container.effectCount(), 1);
    CHECK_EQ(container.effectAt(0)->id, replacement.value());
    REQUIRE_EQ(container.eventCount(), 6);
    CHECK(container.eventAt(2)->kind == EffectEventKind::Refreshed);
    CHECK_EQ(container.eventAt(3)->reason, std::string("replaced"));
    CHECK_EQ(container.eventAt(4)->reason, std::string("replaced"));
    CHECK(container.eventAt(5)->kind == EffectEventKind::Applied);
}

TEST_CASE("effects.lifecycle_finite_effects_expire_and_permanent_effect_survives") {
    EffectContainer container;
    const auto      first     = container.apply("army:1", "one", "", 0, .5, "", StackPolicy::Stack);
    const auto      permanent = container.apply("army:1", "permanent", "", 0, 0.0, "", StackPolicy::Stack);
    const auto      second    = container.apply("army:1", "two", "", 0, .5, "", StackPolicy::Stack);
    REQUIRE(first.ok());
    REQUIRE(permanent.ok());
    REQUIRE(second.ok());
    container.clearEvents();
    const auto halfway = container.update(.25);
    REQUIRE(halfway.ok());
    CHECK_EQ(container.find(first.value())->remaining, .25);
    const auto expired = container.update(.25);
    REQUIRE(expired.ok());
    CHECK(container.find(first.value()) == nullptr);
    CHECK(container.find(second.value()) == nullptr);
    CHECK(container.find(permanent.value()) != nullptr);
    REQUIRE_EQ(container.eventCount(), 2);
    CHECK_EQ(container.eventAt(0)->effectId, first.value());
    CHECK_EQ(container.eventAt(1)->effectId, second.value());
    CHECK_EQ(container.eventAt(0)->sequence, std::uint64_t{4});
    CHECK(container.eventAt(0)->kind == EffectEventKind::Expired);
}

TEST_CASE("effects.payload_and_tags_are_canonical_and_failed_json_is_not_stored") {
    EffectDefinition definition;
    definition.id = "weather.wet";
    definition.payload.setString("name", "A\nB");
    definition.payload.setNumber("weight", 2.5);
    definition.payload.setBool("enabled", true);
    definition.payload.setNull("optional");
    REQUIRE(definition.payload.setJson("array", "[1, {\"ok\": true}]").ok());
    CHECK(!definition.payload.setJson("bad", "not-json").ok());
    CHECK(!definition.payload.has("bad"));
    CHECK_EQ(definition.payload.getJson("array"), std::string("[1,{\"ok\":true}]"));
    CHECK_EQ(definition.payload.toJson(),
             std::string("{\"array\":[1,{\"ok\":true}],\"enabled\":true,\"name\":\"A\\nB\","
                         "\"optional\":null,\"weight\":2.5}"));

    EffectContainer container;
    const auto      id = container.apply(definition, "unit:2", "system");
    REQUIRE(id.ok());
    auto* instance = container.find(id.value());
    REQUIRE(instance != nullptr);
    REQUIRE(instance->addTag("visible").ok());
    REQUIRE(instance->addTag("administrative").ok());
    CHECK_EQ(instance->addTag("visible").code(), eve::StatusCode::NoOp);
    CHECK_EQ(instance->tagAt(0), std::string("administrative"));
    CHECK_EQ(instance->tagAt(1), std::string("visible"));
    REQUIRE(container.taggedAt("unit:2", "visible", 0) != nullptr);
    CHECK_EQ(container.taggedAt("unit:2", "visible", 0)->id, id.value());
}

TEST_CASE("effects.definition_apply_copies_payload_and_applies_independent_stack_policies") {
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
    const auto      first = container.apply(definition, "unit:1", "source:1");
    REQUIRE(first.ok());
    definition.duration  = 99.0;
    definition.magnitude = 99.0;
    definition.payload.setString("kind", "changed-after-apply");
    const auto second = container.apply(definition, "unit:1", "source:2");
    REQUIRE(second.ok());
    CHECK_EQ(second.value(), first.value());
    const auto* instance = container.find(first.value());
    REQUIRE(instance != nullptr);
    CHECK_EQ(instance->stackCount, std::uint32_t{2});
    CHECK_EQ(instance->remaining, 104.0);
    CHECK_EQ(instance->magnitude, 101.0);
    CHECK_EQ(instance->source, std::string("source:1"));
    CHECK_EQ(instance->payload.getJson("kind"), std::string("\"periodic\""));
    CHECK(instance->hasTag("damage"));
    CHECK(container.eventAt(1)->kind == EffectEventKind::Stacked);
}

TEST_CASE("effects.stack_limit_rejection_leaves_instance_and_events_unchanged") {
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
    const auto      first = container.apply(definition, "unit:2", "source");
    REQUIRE(first.ok());
    REQUIRE(container.update(1.0).ok());
    definition.duration  = 100.0;
    definition.magnitude = 5.0;
    REQUIRE(container.apply(definition, "unit:2", "source").ok());
    const auto* before = container.find(first.value());
    REQUIRE(before != nullptr);
    CHECK_EQ(before->stackCount, std::uint32_t{2});
    CHECK_EQ(before->remaining, 3.0);
    CHECK_EQ(before->magnitude, 5.0);
    const auto eventCount = container.eventCount();
    const auto rejected   = container.apply(definition, "unit:2", "source");
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Conflict);
    CHECK_EQ(container.find(first.value())->stackCount, std::uint32_t{2});
    CHECK_EQ(container.eventCount(), eventCount);
}

TEST_CASE("effects.executor_rejects_negative_time_and_reports_expiry") {
    EffectDefinition definition;
    definition.id       = "temporary";
    definition.duration = 1.0;
    EffectContainer container;
    const auto      id = container.apply(definition, "unit:3", "source");
    REQUIRE(id.ok());
    EffectExecutor executor;
    const auto     invalid = executor.advance(container, -1.0);
    CHECK(!invalid.ok());
    CHECK_EQ(invalid.code(), eve::StatusCode::Rejected);
    CHECK(container.find(id.value()) != nullptr);
    const auto advanced = executor.advance(container, 1.0);
    REQUIRE(advanced.ok());
    CHECK_EQ(advanced.value().expired, std::uint32_t{1});
    CHECK(std::abs(advanced.value().elapsedSeconds - 1.0) < 1e-12);
    CHECK(container.find(id.value()) == nullptr);
    CHECK(container.eventAt(1)->kind == EffectEventKind::Expired);
}

TEST_CASE("effects.dispel_filters_category_strength_and_lock_in_priority_order") {
    EffectDefinition weak;
    weak.id                 = "weak";
    weak.priority           = 2;
    weak.tags               = {"dispel:magic"};
    EffectDefinition strong = weak;
    strong.id               = "strong";
    strong.priority         = 5;
    EffectDefinition locked = weak;
    locked.id               = "locked";
    locked.priority         = 4;
    locked.tags.push_back("effect:undispellable");
    EffectDefinition other = weak;
    other.id               = "poison";
    other.priority         = 3;
    other.tags             = {"dispel:poison"};
    EffectContainer container;
    const auto      weakId   = container.apply(weak, "unit:4", "caster");
    const auto      strongId = container.apply(strong, "unit:4", "caster");
    const auto      lockedId = container.apply(locked, "unit:4", "caster");
    const auto      otherId  = container.apply(other, "unit:4", "caster");
    REQUIRE(weakId.ok());
    REQUIRE(strongId.ok());
    REQUIRE(lockedId.ok());
    REQUIRE(otherId.ok());
    const auto handle = container.handleFor(strongId.value());
    REQUIRE(handle.ok());
    const auto removed = container.dispel("unit:4", "dispel:magic", 5, 2);
    REQUIRE(removed.ok());
    REQUIRE_EQ(removed.value().size(), 2U);
    CHECK_EQ(removed.value()[0], strongId.value());
    CHECK_EQ(removed.value()[1], weakId.value());
    CHECK(container.find(strongId.value()) == nullptr);
    CHECK(container.find(weakId.value()) == nullptr);
    CHECK(container.find(lockedId.value()) != nullptr);
    CHECK(container.find(otherId.value()) != nullptr);
    const auto stale = container.resolve(handle.value());
    CHECK(!stale.ok());
    CHECK_EQ(stale.status().primaryDiagnostic()->code(), eve::DiagnosticCode::StaleHandle);
}
