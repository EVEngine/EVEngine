#include "weapon/CarrierRecipeCodec.h"
#include "weapon/SpellFragmentCompiler.h"

#include "common/Diagnostic.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

TEST_CASE("carrierRecipeCodec.decodesPresetAndRoundTripsFullForm") {
    auto parsed = eve::Value::fromJson(R"({
        "id":"spell:bolt",
        "lifetime":2.0,
        "speed":12.0,
        "damage":10.0,
        "element":"fire",
        "homing":90.0,
        "pierce":2
    })");
    REQUIRE(parsed.ok());
    auto recipe = eve::weapon::decodeCarrierRecipe(parsed.value());
    REQUIRE(recipe.ok());
    CHECK_EQ(recipe.value().id.format(), std::string("spell:bolt"));
    CHECK(recipe.value().speed == 12.0);
    CHECK(recipe.value().motionOps.size() >= 2);

    auto encoded = eve::weapon::encodeCarrierRecipe(recipe.value());
    REQUIRE(encoded.ok());
    auto again = eve::weapon::decodeCarrierRecipe(encoded.value());
    REQUIRE(again.ok());
    CHECK_EQ(again.value().id.format(), recipe.value().id.format());
    CHECK_EQ(again.value().motionOps.size(), recipe.value().motionOps.size());
    CHECK_EQ(again.value().impacts.size(), recipe.value().impacts.size());
}

TEST_CASE("carrierRecipeCodec.rejectsMalformedOptionalNumbers") {
    auto badGravity = eve::Value::fromJson(R"({
        "id":"spell:bad","lifetime":1.0,"speed":5.0,"gravity":"heavy"
    })");
    REQUIRE(badGravity.ok());
    CHECK(!eve::weapon::decodeCarrierRecipe(badGravity.value()).ok());

    auto badMotion = eve::Value::fromJson(R"({
        "id":"spell:bad2","lifetime":1.0,"speed":5.0,
        "motion":[{"kind":"homing","maxTurnRateDegrees":"fast"},"linear"],
        "triggers":["onExpire"],
        "impacts":[{"kind":"release","on":"onExpire"}]
    })");
    REQUIRE(badMotion.ok());
    CHECK(!eve::weapon::decodeCarrierRecipe(badMotion.value()).ok());
}

TEST_CASE("spellFragmentCompiler.appliesModifiersBeforeProjectile") {
    auto parsed = eve::Value::fromJson(R"([
        {"kind":"fan","count":3,"spread":30},
        {"kind":"homing","turnRate":120},
        {"kind":"pierce","count":2},
        {"kind":"damage","add":5},
        {"kind":"projectile","id":"spell:bolt","speed":10,"damage":8,"lifetime":2,"element":"arcane"}
    ])");
    REQUIRE(parsed.ok());
    auto plan = eve::weapon::compileSpellFragments(parsed.value());
    REQUIRE(plan.ok());
    CHECK_EQ(plan.value().recipe.id.format(), std::string("spell:bolt"));
    CHECK(plan.value().recipe.speed == 10.0);
    bool foundDamage = false;
    for (const auto& impact : plan.value().recipe.impacts) {
        if (impact.kind == eve::weapon::CarrierImpactKind::EmitHit) {
            CHECK(std::fabs(impact.damage - 13.0) < 1e-9);
            CHECK_EQ(impact.element, std::string("arcane"));
            foundDamage = true;
        }
    }
    CHECK(foundDamage);
    REQUIRE(plan.value().volley.has_value());
    CHECK_EQ(plan.value().volley->count, 3);
    CHECK(plan.value().volley->pattern == eve::weapon::CarrierVolleyPattern::Fan);
}

TEST_CASE("spellFragmentCompiler.rejectsModifierAfterProjectile") {
    auto parsed = eve::Value::fromJson(R"([
        {"kind":"projectile","id":"spell:bolt","speed":10,"damage":1,"lifetime":1},
        {"kind":"homing","turnRate":90}
    ])");
    REQUIRE(parsed.ok());
    auto plan = eve::weapon::compileSpellFragments(parsed.value());
    CHECK(!plan.ok());
}

TEST_CASE("spellFragmentCompiler.preservesProjectileLocalHomingPreset") {
    auto parsed = eve::Value::fromJson(R"([
        {"kind":"projectile","id":"spell:seeker","speed":10,"damage":1,"lifetime":2,"homing":45}
    ])");
    REQUIRE(parsed.ok());
    auto plan = eve::weapon::compileSpellFragments(parsed.value());
    REQUIRE(plan.ok());
    bool foundHoming = false;
    for (const auto& op : plan.value().recipe.motionOps) {
        if (op.kind == eve::weapon::CarrierMotionOpKind::SteerHoming) {
            CHECK(std::fabs(op.maxTurnRateDegrees - 45.0) < 1e-9);
            foundHoming = true;
        }
    }
    CHECK(foundHoming);
}

TEST_CASE("spellFragmentCompiler.appliesDamageOpsLeftToRight") {
    auto addThenMul = eve::Value::fromJson(R"([
        {"kind":"damage","add":10},
        {"kind":"damage","multiply":2},
        {"kind":"projectile","id":"spell:a","speed":1,"damage":5,"lifetime":1}
    ])");
    REQUIRE(addThenMul.ok());
    auto planA = eve::weapon::compileSpellFragments(addThenMul.value());
    REQUIRE(planA.ok());
    // ((5+10)*2) = 30
    double damageA = 0.0;
    for (const auto& impact : planA.value().recipe.impacts) {
        if (impact.kind == eve::weapon::CarrierImpactKind::EmitHit) damageA = impact.damage;
    }
    CHECK(std::fabs(damageA - 30.0) < 1e-9);

    auto mulThenAdd = eve::Value::fromJson(R"([
        {"kind":"damage","multiply":2},
        {"kind":"damage","add":10},
        {"kind":"projectile","id":"spell:b","speed":1,"damage":5,"lifetime":1}
    ])");
    REQUIRE(mulThenAdd.ok());
    auto planB = eve::weapon::compileSpellFragments(mulThenAdd.value());
    REQUIRE(planB.ok());
    // ((5*2)+10) = 20
    double damageB = 0.0;
    for (const auto& impact : planB.value().recipe.impacts) {
        if (impact.kind == eve::weapon::CarrierImpactKind::EmitHit) damageB = impact.damage;
    }
    CHECK(std::fabs(damageB - 20.0) < 1e-9);
}

TEST_CASE("spellFragmentCompiler.rejectsMalformedProjectileNumbers") {
    auto parsed = eve::Value::fromJson(R"([
        {"kind":"projectile","id":"spell:bad","speed":"fast","damage":1,"lifetime":1}
    ])");
    REQUIRE(parsed.ok());
    auto plan = eve::weapon::compileSpellFragments(parsed.value());
    CHECK(!plan.ok());
}

TEST_CASE("spellFragmentCompiler.rejectsFullFormProjectileFragments") {
    auto parsed = eve::Value::fromJson(R"([
        {"kind":"damage","add":1},
        {"kind":"projectile","id":"spell:full","speed":1,"lifetime":1,
         "motion":["linear"],"triggers":["onExpire"],
         "impacts":[{"kind":"release","on":"onExpire"}]}
    ])");
    REQUIRE(parsed.ok());
    auto plan = eve::weapon::compileSpellFragments(parsed.value());
    CHECK(!plan.ok());
}
