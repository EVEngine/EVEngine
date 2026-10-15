#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "attributes/AttributeSet.h"
#include "rpg/AttributeSystem.h"
#include "rpg/RPGActor.h"

#include <type_traits>

using eve::attributes::AttributeModifier;
using eve::attributes::AttributeOperation;
using eve::attributes::AttributeSet;
using eve::rpg::RPGActor;

TEST_CASE("attributes.canonicalOperationOrderAndMetadata") {
    AttributeSet set("actor:one");
    set.setBase("power", 100.0);

    auto add = set.addModifier(AttributeModifier{"", "power", "gear", AttributeOperation::Add, 10.0, 0});
    auto additive =
        set.addModifier(AttributeModifier{"", "power", "talent", AttributeOperation::AdditivePercent, 0.2, 0});
    auto multiplicative =
        set.addModifier(AttributeModifier{"", "power", "aura", AttributeOperation::MultiplicativePercent, 0.1, 0});
    auto overrideValue =
        set.addModifier(AttributeModifier{"", "power", "scenario", AttributeOperation::Override, 50.0, 100});
    auto clamp = set.addModifier(AttributeModifier{"", "power", "rules", AttributeOperation::ClampMin, 60.0, 200});

    REQUIRE(add.ok());
    REQUIRE(additive.ok());
    REQUIRE(multiplicative.ok());
    REQUIRE(overrideValue.ok());
    REQUIRE(clamp.ok());
    CHECK_EQ(set.getFinal("power"), 60.0);
    CHECK_EQ(set.modifierCount(), 5);
    REQUIRE(set.modifierAt(0) != nullptr);
    REQUIRE(set.modifierAt(4) != nullptr);
    CHECK_EQ(set.modifierAt(0)->sequence, 1u);
    CHECK_EQ(set.modifierAt(4)->sequence, 5u);
    CHECK_EQ(set.modifierAt(0)->attribute, "power");
    CHECK_EQ(set.modifierAt(0)->source, "gear");
}

TEST_CASE("attributes.generatedIdsAreSetLocalAndCopyDeterministic") {
    AttributeSet first("actor:first");
    AttributeSet second("actor:second");

    auto firstId  = first.addModifier(AttributeModifier{"", "power", "source", AttributeOperation::Add, 1.0});
    auto secondId = second.addModifier(AttributeModifier{"", "power", "source", AttributeOperation::Add, 1.0});
    REQUIRE(firstId.ok());
    REQUIRE(secondId.ok());
    CHECK_EQ(firstId.value(), "attribute:modifier:actor:first:1");
    CHECK_EQ(secondId.value(), "attribute:modifier:actor:second:1");
    CHECK_EQ(first.modifierAt(0)->sequence, 1u);
    CHECK_EQ(second.modifierAt(0)->sequence, 1u);

    AttributeSet restored = first;
    auto restoredNext = restored.addModifier(AttributeModifier{"", "power", "restore", AttributeOperation::Add, 2.0});
    auto firstNext    = first.addModifier(AttributeModifier{"", "power", "next", AttributeOperation::Add, 2.0});
    REQUIRE(restoredNext.ok());
    REQUIRE(firstNext.ok());
    CHECK_EQ(restoredNext.value(), "attribute:modifier:actor:first:2");
    CHECK_EQ(firstNext.value(), "attribute:modifier:actor:first:2");
    CHECK_EQ(restored.modifierAt(1)->sequence, 2u);
    CHECK_EQ(first.modifierAt(1)->sequence, 2u);
}

TEST_CASE("attributes.rpgFacadeUsesTheSameAttributeSetTruth") {
    static_assert(std::is_same_v<decltype(RPGActor::Attributes{}.values), AttributeSet>);

    AttributeSet direct("actor:parity");
    direct.setBase("damage", 100.0);
    auto directModifier =
        direct.addModifier(AttributeModifier{"direct", "damage", "shared", AttributeOperation::AdditivePercent, 0.2});
    REQUIRE(directModifier.ok());

    RPGActor* actor = RPGActor::createActor();
    actor->attributes()->values.setBase("damage", 100.0);
    auto actorModifier = actor->addAttributeModifier(
        AttributeModifier{"direct", "damage", "shared", AttributeOperation::AdditivePercent, 0.2});
    REQUIRE(actorModifier.ok());
    CHECK_EQ(direct.getFinal("damage"), actor->getFinalAttribute("damage"));

    auto bad = actor->addAttributeModifier(AttributeModifier{"bad", "", "source", AttributeOperation::Add, 1.0});
    CHECK(!bad.ok());
    CHECK_EQ(bad.code(), eve::StatusCode::Rejected);

    CHECK(actor->removeAttributeModifier(actorModifier.value()).ok());
    CHECK_EQ(actor->getFinalAttribute("damage"), 100.0);
    actor->release();
}

TEST_CASE("attributes.customPolicyParityUsesCanonicalOperationRegistry") {
    auto& policies = eve::rpg::AttributeSystem::customOps();
    policies.registerOperation("square", [](double, double value) { return value * value; });

    AttributeSet direct("actor:custom");
    direct.setBase("power", 1.0);
    auto directModifier = direct.addModifier(
        AttributeModifier{"square", "power", "policy", AttributeOperation::Custom, 4.0, 0, 0, "square"});
    REQUIRE(directModifier.ok());

    RPGActor* actor = RPGActor::createActor();
    actor->attributes()->values.setBase("power", 1.0);
    auto actorModifier = actor->addAttributeModifier(
        AttributeModifier{"square", "power", "policy", AttributeOperation::Custom, 4.0, 0, 0, "square"});
    REQUIRE(actorModifier.ok());
    CHECK_EQ(direct.getFinal("power", 0.0, &policies), actor->getFinalAttribute("power"));

    actor->release();
    policies.unregisterOperation("square");
}
