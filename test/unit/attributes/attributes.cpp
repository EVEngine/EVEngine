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

#include "attributes/AttributeSet.h"
#include "common/Capability.h"
#include "common/StateAccess.h"

#include <cmath>
#include <limits>
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
}  // namespace
