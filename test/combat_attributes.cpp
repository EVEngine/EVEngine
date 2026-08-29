#include "combat/CombatAttributes.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <limits>

namespace {

eve::combat::CombatAttributeDefinition health() { return {"Attribute.Vital.Health", 75.0, 0.0, 100.0, 5.0}; }

}  // namespace

TEST_CASE("combatAttributes.registersTagAddressedBoundedValues") {
    eve::combat::CombatAttributeRuntime runtime("fighter");
    REQUIRE(runtime.registerAttribute(health()).ok());
    CHECK(!runtime.registerAttribute(health()).ok());
    auto value = runtime.value("Attribute.Vital.Health");
    REQUIRE(value.ok());
    CHECK_EQ(value.value(), 75.0);
    CHECK(!runtime.value("Attribute.Vital.Mana").ok());
}

TEST_CASE("combatAttributes.emitsGainLossAndDepletionEvents") {
    eve::combat::CombatAttributeRuntime runtime;
    REQUIRE(runtime.registerAttribute(health()).ok());
    auto loss = runtime.modify("Attribute.Vital.Health", -25.0);
    REQUIRE(loss.ok());
    REQUIRE_EQ(loss.value().size(), 1u);
    CHECK(static_cast<int>(loss.value()[0].kind) ==
          static_cast<int>(eve::combat::CombatAttributeEventKind::Loss));
    auto depleted = runtime.modify("Attribute.Vital.Health", -1000.0);
    REQUIRE(depleted.ok());
    REQUIRE_EQ(depleted.value().size(), 2u);
    CHECK(static_cast<int>(depleted.value()[1].kind) ==
          static_cast<int>(eve::combat::CombatAttributeEventKind::Depleted));
    auto gain = runtime.setValue("Attribute.Vital.Health", 10.0);
    REQUIRE(gain.ok());
    REQUIRE_EQ(gain.value().size(), 1u);
    CHECK(static_cast<int>(gain.value()[0].kind) ==
          static_cast<int>(eve::combat::CombatAttributeEventKind::Gain));
}

TEST_CASE("combatAttributes.regenerationUsesInjectedDurationAndClamps") {
    eve::combat::CombatAttributeRuntime runtime;
    REQUIRE(runtime.registerAttribute(health()).ok());
    auto advanced = runtime.advance(eve::Duration::fromNanoseconds(1000000000));
    REQUIRE(advanced.ok());
    REQUIRE_EQ(advanced.value().events.size(), 1u);
    CHECK_EQ(runtime.value("Attribute.Vital.Health").value(), 80.0);
    REQUIRE(runtime.advance(eve::Duration::fromNanoseconds(10000000000)).ok());
    CHECK_EQ(runtime.value("Attribute.Vital.Health").value(), 100.0);
    auto noOp = runtime.advance(eve::Duration::zero());
    REQUIRE(noOp.ok());
    CHECK(noOp.value().events.empty());
}

TEST_CASE("combatAttributes.rejectsInvalidInputWithoutMutation") {
    eve::combat::CombatAttributeRuntime runtime;
    auto invalid = health();
    invalid.regenerationPerSecond = -1.0;
    CHECK(!runtime.registerAttribute(invalid).ok());
    REQUIRE(runtime.registerAttribute(health()).ok());
    CHECK(!runtime.modify("Attribute.Vital.Health", std::numeric_limits<double>::infinity()).ok());
    CHECK_EQ(runtime.value("Attribute.Vital.Health").value(), 75.0);
    CHECK(!runtime.advance(eve::Duration::fromNanoseconds(-1)).ok());
    CHECK_EQ(runtime.value("Attribute.Vital.Health").value(), 75.0);
}
