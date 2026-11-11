// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "attributes.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "attributes/AttributeProjection.h"
#include "attributes/AttributeProjectionCapture.h"
#include "attributes/AttributeSet.h"
#include "common/Capability.h"
#include "common/StateAccess.h"

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace {
using eve::attributes::AttributeModifier;
using eve::attributes::AttributeOperation;
using eve::attributes::AttributeOperationRegistry;
using eve::attributes::AttributeSet;

AttributeModifier mod(std::string id, std::string attr, std::string source, AttributeOperation op, double value,
                      int priority = 0) {
    return {std::move(id), std::move(attr), std::move(source), op, value, priority};
}

// Base contract: absent attributes return fallback; mutation creates from zero.
TEST_CASE("attributes.spec.base_and_names") {
    AttributeSet set("actor-7");
    CHECK_EQ(set.subject(), "actor-7");
    CHECK(!set.has("speed"));
    CHECK_EQ(set.getBase("speed", 91.0), 91.0);
    CHECK_EQ(set.getFinal("speed", 92.0), 92.0);
    set.modifyBase("speed", 3.0);
    CHECK_EQ(set.getBase("speed"), 3.0);
    set.setBase("speed", -4.0);
    CHECK_EQ(set.getFinal("speed"), -4.0);
    set.setBase("", 100.0);
    set.modifyBase("", 100.0);
    CHECK(!set.has(""));
    CHECK_EQ(set.attributeCount(), 1);
    CHECK_EQ(set.attributeAt(0), "speed");
    CHECK_EQ(set.attributeAt(-1), "");
    CHECK_EQ(set.attributeAt(1), "");
    set.setBase("armor", 8.0);
    CHECK_EQ(set.attributeAt(0), "armor");
    REQUIRE_EQ(set.attributeAt(1), "speed");
}

// Calculation contract: flat deltas precede summed additive percentages;
// independent multiplicative percentages compound regardless of insertion order.
TEST_CASE("attributes.spec.calculation_phases") {
    AttributeSet set;
    set.setBase("damage", 100.0);
    REQUIRE(set.addModifier(mod("mul-b", "damage", "gear", AttributeOperation::MultiplicativePercent, -0.2)).ok());
    REQUIRE(set.addModifier(mod("pct-b", "damage", "aura", AttributeOperation::AdditivePercent, 0.125)).ok());
    REQUIRE(set.addModifier(mod("flat", "damage", "weapon", AttributeOperation::Add, 20.0)).ok());
    REQUIRE(set.addModifier(mod("mul-a", "damage", "gear", AttributeOperation::MultiplicativePercent, 0.5)).ok());
    REQUIRE(set.addModifier(mod("pct-a", "damage", "aura", AttributeOperation::AdditivePercent, 0.125)).ok());
    // Multiplicative percentage aggregation can round at a different step.
    CHECK_LT(std::abs(set.getFinal("damage") - 180.0), 1e-10);  // (100 + 20) * 1.25 * 1.5 * 0.8
    set.modifyBase("damage", 10.0);
    CHECK_LT(std::abs(set.getFinal("damage") - 195.0), 1e-10);
}

// Ordered phase contract: priority ascends; equal priority follows insertion sequence.
TEST_CASE("attributes.spec.ordered_override_and_clamps") {
    AttributeSet set;
    set.setBase("score", 100.0);
    REQUIRE(set.addModifier(mod("floor", "score", "rules", AttributeOperation::ClampMin, 90.0, 30)).ok());
    REQUIRE(set.addModifier(mod("ceiling", "score", "rules", AttributeOperation::ClampMax, 40.0, 20)).ok());
    REQUIRE(set.addModifier(mod("override", "score", "rules", AttributeOperation::Override, 5.0, 10)).ok());
    CHECK_EQ(set.getFinal("score"), 90.0);
    REQUIRE(set.addModifier(mod("last", "score", "rules", AttributeOperation::Override, -2.0, 30)).ok());
    CHECK_EQ(set.getFinal("score"), -2.0);
    REQUIRE(set.removeModifier("last").ok());
    CHECK_EQ(set.getFinal("score"), 90.0);
}

// Input contract: a modifier needs a target, valid operation and finite value.
// Rejection leaves state and enumeration unchanged.
TEST_CASE("attributes.spec.invalid_input_is_atomic") {
    AttributeSet set;
    set.setBase("health", 20.0);
    CHECK(!set.addModifier(mod("no-target", "", "source", AttributeOperation::Add, 5.0)).ok());
    CHECK(!set.addModifier(
                  mod("infinite", "health", "source", AttributeOperation::Add, std::numeric_limits<double>::infinity()))
               .ok());
    CHECK(!set.addModifier(mod("no-policy", "health", "source", AttributeOperation::Custom, 1.0)).ok());
    CHECK_EQ(set.modifierCount(), 0);
    CHECK_EQ(set.attributeCount(), 1);
    CHECK_EQ(set.getFinal("health"), 20.0);
    CHECK(set.modifierAt(-1) == nullptr);
    REQUIRE(set.modifierAt(0) == nullptr);
}

// Identity contract: replacing an id moves it to the new target; generated ids
// are unique and enumeration follows sequence rather than hash-map iteration.
TEST_CASE("attributes.spec.identity_and_enumeration") {
    AttributeSet set("actor");
    auto         first  = set.addModifier(mod("", "armor", "gear", AttributeOperation::Add, 2.0));
    auto         second = set.addModifier(mod("", "armor", "gear", AttributeOperation::Add, 3.0));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(first.value() != second.value());
    REQUIRE(set.addModifier(mod(first.value(), "speed", "gear", AttributeOperation::Add, 7.0)).ok());
    CHECK_EQ(set.modifierCount(), 2);
    CHECK_EQ(set.getFinal("armor"), 3.0);
    CHECK_EQ(set.getFinal("speed"), 7.0);
    REQUIRE(set.modifierAt(0) != nullptr);
    REQUIRE(set.modifierAt(1) != nullptr);
    CHECK_EQ(set.modifierAt(0)->id, second.value());
    CHECK_EQ(set.modifierAt(1)->id, first.value());
    CHECK(set.modifierAt(2) == nullptr);
}

// Removal contract: scoped removal cannot touch other attributes; source removal
// counts matches, and absent matches return success with zero.
TEST_CASE("attributes.spec.scoped_removal_and_clear") {
    AttributeSet set;
    set.setBase("a", 10.0);
    REQUIRE(set.addModifier(mod("one", "a", "event", AttributeOperation::Add, 1.0)).ok());
    REQUIRE(set.addModifier(mod("two", "b", "event", AttributeOperation::Add, 2.0)).ok());
    REQUIRE(set.addModifier(mod("three", "b", "gear", AttributeOperation::Add, 3.0)).ok());
    CHECK(!set.removeModifier("a", "two").ok());
    CHECK(!set.removeModifier("missing").ok());
    CHECK(!set.removeModifier("").ok());
    auto scoped = set.removeBySource("event", "b");
    REQUIRE(scoped.ok());
    CHECK_EQ(scoped.value(), 1);
    CHECK_EQ(set.getFinal("a"), 11.0);
    CHECK_EQ(set.getFinal("b"), 3.0);
    auto remaining = set.removeBySource("event");
    REQUIRE(remaining.ok());
    CHECK_EQ(remaining.value(), 1);
    auto absent = set.removeBySource("event");
    REQUIRE(absent.ok());
    CHECK_EQ(absent.value(), 0);
    CHECK(!set.removeBySource("").ok());
    set.clearModifiers();
    CHECK_EQ(set.modifierCount(), 0);
    CHECK_EQ(set.getFinal("a"), 10.0);
    CHECK_EQ(set.getFinal("b"), 0.0);
}

// A supplied registry is evaluated on every call, so replacing a policy changes
// the output immediately. Without one, custom operations leave the value alone.
TEST_CASE("attributes.spec.custom_registry") {
    AttributeSet set;
    set.setBase("x", 10.0);
    auto custom     = mod("policy", "x", "game", AttributeOperation::Custom, 3.0);
    custom.policyId = "triple-plus";
    REQUIRE(set.addModifier(std::move(custom)).ok());
    CHECK_EQ(set.getFinal("x"), 10.0);
    AttributeOperationRegistry policies;
    CHECK_EQ(set.getFinal("x", 0.0, &policies), 10.0);
    policies.registerOperation("triple-plus", [](double current, double value) { return current * 3 + value; });
    CHECK_EQ(set.getFinal("x", 0.0, &policies), 33.0);
    policies.registerOperation("triple-plus", [](double current, double value) { return current - value; });
    CHECK_EQ(set.getFinal("x", 0.0, &policies), 7.0);
    policies.unregisterOperation("triple-plus");
    CHECK_EQ(set.getFinal("x", 0.0, &policies), 10.0);
}

// The compatibility facade converts factor-based multiply into percentage delta.
TEST_CASE("attributes.spec.legacy_factor_conversion") {
    AttributeSet set;
    set.setBase("x", 100.0);
    CHECK(!set.addModifier("", "x", "legacy", "add", 1.0));
    CHECK(!set.addModifier("bad", "x", "legacy", "divide", 2.0));
    REQUIRE(set.addModifier("factor", "x", "legacy", "multiply", 1.5));
    CHECK_EQ(set.getFinal("x"), 150.0);
    REQUIRE(set.addModifier("upper", "x", "legacy", "max", 120.0));
    CHECK_EQ(set.getFinal("x"), 120.0);
}

// Copies and moves must publish exactly one adapter per live set and withdraw it.
TEST_CASE("attributes.spec.capability_lifecycle") {
    eve::cap::detail::clearAllRaw();
    auto countQueries = [] {
        int count = 0;
        eve::cap::forEach<eve::IStateQuery>([&](eve::IStateQuery*) { ++count; });
        return count;
    };
    {
        AttributeSet original("subject");
        CHECK_EQ(countQueries(), 1);
        AttributeSet copy(original);
        CHECK_EQ(countQueries(), 2);
        AttributeSet moved(std::move(copy));
        CHECK_EQ(countQueries(), 2);
    }
    REQUIRE_EQ(countQueries(), 0);
}
TEST_CASE("attributes.spec.prepared_copy_is_invisible_and_cancellation_preserves_live_state") {
    AttributeSet source("stage-source");
    source.setBase("hp", 10);
    const auto queries   = eve::cap::listenerCount<eve::IStateQuery>();
    const auto mutations = eve::cap::listenerCount<eve::IStateMutation>();
    {
        auto candidate = source.prepareCopy();
        candidate.setBase("hp", 42);
        REQUIRE_EQ(candidate.view().getBase("hp"), 42.0);
        REQUIRE_EQ(source.getBase("hp"), 10.0);
        REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
        REQUIRE_EQ(eve::cap::listenerCount<eve::IStateMutation>(), mutations);
        std::size_t matches = 0;
        eve::cap::forEach<eve::IStateQuery>([&](eve::IStateQuery* query) {
            auto value = query->attribute("stage-source", "hp");
            if (value) {
                REQUIRE_EQ(value->asDouble(), 10.0);
                ++matches;
            }
        });
        REQUIRE_EQ(matches, 1u);
    }
    REQUIRE_EQ(source.getBase("hp"), 10.0);
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
}
TEST_CASE("attributes.spec.prepared_copy_outlives_source_and_commits_without_replacing_target_identity") {
    std::optional<eve::attributes::PreparedAttributeSet> candidate;
    {
        AttributeSet source("expired-source");
        source.setBase("hp", 7);
        REQUIRE(source
                    .addModifier(eve::attributes::AttributeModifier{"bonus", "hp", "test",
                                                                    eve::attributes::AttributeOperation::Add, 3.0})
                    .ok());
        candidate.emplace(source.prepareCopy());
    }
    AttributeSet target("stage-target");
    target.setBase("hp", 1);
    const auto queries = eve::cap::listenerCount<eve::IStateQuery>();
    auto       moved   = std::move(*candidate);
    target.commitPrepared(std::move(moved));
    REQUIRE(target.subject() == "stage-target");
    REQUIRE_EQ(target.getFinal("hp"), 10.0);
    REQUIRE_EQ(eve::cap::listenerCount<eve::IStateQuery>(), queries);
    std::size_t matches = 0;
    eve::cap::forEach<eve::IStateQuery>([&](eve::IStateQuery* query) {
        REQUIRE(!query->attribute("expired-source", "hp"));
        auto value = query->attribute("stage-target", "hp");
        if (value) {
            REQUIRE_EQ(value->asDouble(), 10.0);
            ++matches;
        }
    });
    REQUIRE_EQ(matches, 1u);
}
}  // namespace

TEST_CASE("attributes.spec.versioned_operation_tracks_actual_execution_and_legacy_replacement") {
    eve::attributes::AttributeOperationRegistry operations;
    int                                         calls = 0;
    REQUIRE(!operations.implementationRevision("saved.op").ok());
    REQUIRE(operations
                .registerVersionedOperation("saved.op", "source:v1",
                                            [&](double a, double b) {
                                                ++calls;
                                                return a + b;
                                            })
                .ok());
    auto revision = operations.implementationRevision("saved.op").expect("actual callback revision");
    REQUIRE_EQ(calls, 0);
    REQUIRE_EQ(operations.apply("saved.op", 10, 2), 12.0);
    REQUIRE_EQ(calls, 1);
    REQUIRE(!operations.registerVersionedOperation("saved.op", "", [](double, double) { return 99; }).ok());
    REQUIRE(!operations.registerVersionedOperation("saved.op", "invalid", {}).ok());
    REQUIRE_EQ(operations.implementationRevision("saved.op").expect("old revision retained"), "source:v1");
    auto copy = operations;
    REQUIRE(
        operations.registerVersionedOperation("saved.op", "source:v2", [](double a, double b) { return a - b; }).ok());
    REQUIRE_EQ(operations.apply("saved.op", 10, 2), 8.0);
    REQUIRE_EQ(copy.apply("saved.op", 10, 2), 12.0);
    REQUIRE_EQ(copy.implementationRevision("saved.op").expect("independent copy"), "source:v1");
    REQUIRE_EQ(revision, "source:v1");
    operations.registerOp("saved.op", [](double a, double b) { return a * b; });
    REQUIRE_EQ(operations.apply("saved.op", 10, 2), 20.0);
    REQUIRE(!operations.implementationRevision("saved.op").ok());
    copy.unregisterOperation("saved.op");
    REQUIRE(!copy.implementationRevision("saved.op").ok());
}

TEST_CASE("attributes.persistence.exactCountersModifiersAndFutureIdentityWithoutPolicyExecution") {
    using namespace eve;
    using namespace eve::attributes;
    std::string futureId, expected;
    auto        payload = [&] {
        AttributeSet source("persistent");
        source.setBase("power", 10);
        source.setBase("emptyValue", 0);
        source.addModifier(AttributeModifier{{}, "power", "origin", AttributeOperation::Add, 2})
            .expect("first generated modifier");
        const auto removed =
            source.addModifier(AttributeModifier{{}, "power", "origin", AttributeOperation::Override, 50}).takeValue();
        source.removeModifier(removed).expect("retain allocation gaps");
        source
            .addModifier(
                AttributeModifier{"custom", "power", "origin", AttributeOperation::Custom, 4, 3, 0, "saved.op"})
            .expect("declarative custom policy");
        source.addModifier(AttributeModifier{"only", "modifierOnly", "origin", AttributeOperation::Add, 5})
            .expect("modifier-only attribute");
        REQUIRE_EQ(source.getFinal("power"), 12.0);
        auto encoded = source.encodeState().takeValue();
        futureId = source.addModifier(AttributeModifier{{}, "power", "future", AttributeOperation::Add, 3}).takeValue();
        expected = source.encodeState().takeValue().toJson().takeValue();
        return encoded;
    }();
    const auto text = payload.toJson().takeValue();
    payload         = Value::fromJson(text).takeValue();
    AttributeSet target("persistent");
    target.setBase("old", 999);
    const auto queries = cap::listenerCount<IStateQuery>(), mutations = cap::listenerCount<IStateMutation>();
    int        calls = 0;
    AttributeOperationRegistry operations;
    operations
        .registerVersionedOperation("saved.op", "source:v1",
                                    [&](double current, double value) {
                                        ++calls;
                                        return current + value;
                                    })
        .expect("explicit implementation owner");
    auto prepared = AttributeSet::prepareState(payload).takeValue();
    REQUIRE_EQ(prepared.view().subject(), target.subject());
    REQUIRE_EQ(cap::listenerCount<IStateQuery>(), queries);
    REQUIRE_EQ(cap::listenerCount<IStateMutation>(), mutations);
    REQUIRE_EQ(target.getBase("old"), 999.0);
    REQUIRE_EQ(calls, 0);
    target.commitPrepared(std::move(prepared));
    REQUIRE(!target.has("old"));
    REQUIRE_EQ(target.getFinal("modifierOnly"), 5.0);
    REQUIRE_EQ(target.getFinal("power", 0, &operations), 16.0);
    REQUIRE_EQ(calls, 1);
    REQUIRE_EQ(target.addModifier(AttributeModifier{{}, "power", "future", AttributeOperation::Add, 3}).takeValue(),
               futureId);
    REQUIRE_EQ(target.encodeState().takeValue().toJson().takeValue(), expected);
}
TEST_CASE("attributes.persistence.rejectsUnknownFieldsInvalidCountersAndDuplicateModifierFacts") {
    using namespace eve;
    using namespace eve::attributes;
    AttributeSet source("strict");
    source.setBase("power", 10);
    source.addModifier(AttributeModifier{{}, "power", "origin", AttributeOperation::Add, 2}).expect("actual modifier");
    const auto encoded = source.encodeState().takeValue();
    const auto before  = encoded.toJson().takeValue();
    auto       bad     = encoded;
    bad.set("extra", true);
    REQUIRE(!AttributeSet::prepareState(bad).ok());
    bad = encoded;
    bad.set("version", 2);
    REQUIRE(!AttributeSet::prepareState(bad).ok());
    for (const auto* counter : {"next_sequence", "next_modifier"}) {
        for (const auto* value : {"0", "01", "-1", "18446744073709551616"}) {
            bad = encoded;
            bad.set(counter, value);
            REQUIRE(!AttributeSet::prepareState(bad).ok());
        }
    }
    bad = encoded;
    bad.set("next_sequence", "1");
    REQUIRE(!AttributeSet::prepareState(bad).ok());
    bad = encoded;
    bad.find("values")->getIf<Value::Array>()->push_back(bad.find("values")->at(0));
    REQUIRE(!AttributeSet::prepareState(bad).ok());
    bad             = encoded;
    auto& modifiers = bad.find("values")->at(0).at(2);
    modifiers.getIf<Value::Array>()->push_back(modifiers.at(0));
    REQUIRE(!AttributeSet::prepareState(bad).ok());
    bad                             = encoded;
    bad.find("values")->at(0).at(1) = Value(std::numeric_limits<double>::quiet_NaN());
    REQUIRE(!AttributeSet::prepareState(bad).ok());
    REQUIRE_EQ(source.encodeState().takeValue().toJson().takeValue(), before);
    REQUIRE(AttributeSet::prepareState(encoded).ok());
}

TEST_CASE("attributes.projection.persistencePreservesInitializationRevisionAndIndependentFacts") {
    using namespace eve;
    using namespace eve::attributes;
    auto encoded = [] {
        AttributeProjection source("projection");
        source.setBase("mana", 4).expect("first projection revision");
        source.setBase("mana", 5).expect("actual projection base");
        source.addModifier(AttributeModifier{{}, "mana", "origin", AttributeOperation::Add, 2})
            .expect("actual modifier");
        return source.capture().takeValue().encodeState().takeValue().toJson().takeValue();
    }();
    const auto listeners = cap::listenerCount<IStateQuery>();
    auto       parsed    = AttributeProjectionCapture::prepareState(Value::fromJson(encoded).takeValue()).takeValue();
    REQUIRE_EQ(cap::listenerCount<IStateQuery>(), listeners);
    REQUIRE(parsed.initialized() == true);
    REQUIRE_EQ(parsed.revision(), Revision(3));
    REQUIRE_EQ(parsed.values()->get().getFinal("mana"), 7.0);
    REQUIRE_EQ(parsed.encodeState().takeValue().toJson().takeValue(), encoded);
    auto empty = [] {
        AttributeProjection source("uninitialized");
        return source.capture().takeValue().encodeState().takeValue();
    }();
    auto uninitialized = AttributeProjectionCapture::prepareState(empty).takeValue();
    REQUIRE(uninitialized.initialized() == false);
    REQUIRE(uninitialized.revision().isZero());
    REQUIRE_EQ(uninitialized.values()->get().attributeCount(), 0);
    const auto original = Value::fromJson(encoded).takeValue();
    auto       bad      = original;
    bad.set("unknown", true);
    REQUIRE(!AttributeProjectionCapture::prepareState(bad).ok());
    bad = original;
    bad.set("version", 2);
    REQUIRE(!AttributeProjectionCapture::prepareState(bad).ok());
    bad = original;
    bad.set("initialized", false);
    REQUIRE(!AttributeProjectionCapture::prepareState(bad).ok());
    for (const auto* revision : {"0", "01", "-1", "18446744073709551616"}) {
        bad = original;
        bad.set("revision", revision);
        REQUIRE(!AttributeProjectionCapture::prepareState(bad).ok());
    }
    auto moved = std::move(parsed);
    REQUIRE(!parsed.encodeState().ok());
    REQUIRE(moved.encodeState().ok());
}
