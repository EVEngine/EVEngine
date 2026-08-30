#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "attributes/AttributeSet.h"

using eve::attributes::AttributeSet;

TEST_CASE("attributes.baseAndFallback") {
    AttributeSet set("general.1");
    CHECK_EQ(set.subject(), "general.1");
    CHECK_EQ(set.getBase("loyalty", 12.0), 12.0);
    set.setBase("loyalty", 48.0);
    set.modifyBase("loyalty", 2.0);
    CHECK(set.has("loyalty"));
    CHECK_EQ(set.getBase("loyalty"), 50.0);
    CHECK_EQ(set.getFinal("loyalty"), 50.0);
}

TEST_CASE("attributes.modifiersUsePriorityThenSequence") {
    AttributeSet set;
    set.setBase("production_speed", 100.0);
    REQUIRE(set.addModifier("flat", "production_speed", "policy", "add", 10.0, 0));
    REQUIRE(set.addModifier("governor", "production_speed", "general.1", "multiply", 1.5, 10));
    CHECK_EQ(set.getFinal("production_speed"), 165.0);

    REQUIRE(set.addModifier("cap", "production_speed", "difficulty", "min", 140.0, 20));
    CHECK_EQ(set.getFinal("production_speed"), 140.0);
    REQUIRE(set.addModifier("floor", "production_speed", "scenario", "max", 150.0, 30));
    CHECK_EQ(set.getFinal("production_speed"), 150.0);
}

TEST_CASE("attributes.overrideAndRemoval") {
    AttributeSet set;
    set.setBase("loyalty", 50.0);
    CHECK(!set.addModifier("", "loyalty", "event", "add", -5.0));
    CHECK(!set.addModifier("bad", "loyalty", "event", "divide", 2.0));
    REQUIRE(set.addModifier("unpaid", "loyalty", "treasury", "add", -10.0));
    REQUIRE(set.addModifier("scripted", "loyalty", "scenario", "override", 5.0, 100));
    CHECK_EQ(set.getFinal("loyalty"), 5.0);
    CHECK(set.removeModifier("scripted").ok());
    CHECK_EQ(set.getFinal("loyalty"), 40.0);
    auto removedTreasury = set.removeBySource("treasury");
    REQUIRE(removedTreasury.ok());
    CHECK_EQ(removedTreasury.value(), 1);
    CHECK_EQ(set.getFinal("loyalty"), 50.0);
}

TEST_CASE("attributes.modifierEnumerationIsStable") {
    AttributeSet set;
    REQUIRE(set.addModifier("a", "x", "one", "add", 1.0));
    REQUIRE(set.addModifier("b", "x", "two", "add", 2.0));
    REQUIRE(set.addModifier("c", "y", "one", "multiply", 3.0));
    REQUIRE_EQ(set.modifierCount(), 3);
    REQUIRE(set.modifierAt(0) != nullptr);
    REQUIRE(set.modifierAt(1) != nullptr);
    REQUIRE(set.modifierAt(2) != nullptr);
    CHECK_EQ(set.modifierAt(0)->id, "a");
    CHECK_EQ(set.modifierAt(1)->id, "b");
    CHECK_EQ(set.modifierAt(2)->id, "c");
    auto removedOne = set.removeBySource("one");
    REQUIRE(removedOne.ok());
    CHECK_EQ(removedOne.value(), 2);
    REQUIRE_EQ(set.modifierCount(), 1);
    CHECK_EQ(set.modifierAt(0)->id, "b");
}
