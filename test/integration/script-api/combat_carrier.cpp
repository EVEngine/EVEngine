#include <cmath>
#include <map>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string_view>
#include "common/Diagnostic.h"
#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "weapon/CombatCarrier.h"
#include "weapon/Weapon.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// combat_carrier.cpp
namespace {

[[maybe_unused]] eve::LogicalId id(std::string_view value) {
    auto parsed = eve::LogicalId::parse(value);
    REQUIRE(parsed.has_value());
    return std::move(*parsed);
}

[[maybe_unused]] eve::Duration seconds(double value) {
    auto duration = eve::Duration::fromSeconds(value);
    REQUIRE(duration.ok());
    return duration.value();
}

class TargetProvider final : public eve::weapon::IProjectileTargetProvider {
public:
    [[nodiscard]] eve::Result<eve::weapon::ProjectilePoint> position(ecs::EntityHandle target) const override {
        auto found = positions.find(target.id);
        if (reject || found == positions.end())
            return eve::Result<eve::weapon::ProjectilePoint>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "target unavailable"));
        return eve::Result<eve::weapon::ProjectilePoint>::success(found->second);
    }

    mutable bool                                          reject = false;
    std::map<std::uint32_t, eve::weapon::ProjectilePoint> positions;
};

class HitProbe final : public eve::weapon::ICarrierHitProbe {
public:
    [[nodiscard]] eve::Result<std::vector<Contact>> query(eve::weapon::CarrierHandle, const eve::weapon::CarrierMotion&,
                                                          const eve::weapon::CarrierMotion& current) const override {
        ++queries;
        if (reject)
            return eve::Result<std::vector<Contact>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "hit probe failed"));
        std::vector<Contact> contacts;
        for (const auto& planned : plannedHits) {
            if (std::fabs(current.position.x - planned.atX) < 1e-9) {
                Contact contact;
                contact.target = planned.target;
                contact.point  = current.position;
                contact.normal = planned.normal;
                contacts.push_back(contact);
            }
        }
        return eve::Result<std::vector<Contact>>::success(std::move(contacts));
    }

    struct PlannedHit {
        double                        atX = 0.0;
        ecs::EntityHandle             target{};
        eve::weapon::ProjectileVector normal{-1.0, 0.0, 0.0};
    };

    mutable int             queries = 0;
    mutable bool            reject  = false;
    std::vector<PlannedHit> plannedHits;
};

[[maybe_unused]] eve::weapon::CarrierRecipe linearShell(std::string_view recipeId, double damage) {
    eve::weapon::CarrierRecipe recipe;
    recipe.id       = id(recipeId);
    recipe.lifetime = seconds(2.0);
    recipe.speed    = 10.0;
    recipe.motionOps.push_back({eve::weapon::CarrierMotionOpKind::IntegrateLinear});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnExpire});
    recipe.triggers.push_back({eve::weapon::CarrierTriggerKind::OnHit});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::EmitHit, eve::weapon::CarrierTriggerKind::OnHit, damage});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnHit});
    recipe.impacts.push_back({eve::weapon::CarrierImpactKind::Release, eve::weapon::CarrierTriggerKind::OnExpire});
    return recipe;
}

}  // namespace


class BodyDefenseProvider final : public eve::weapon::ICarrierBodyDefenseProvider {
public:
    [[nodiscard]] eve::Result<std::vector<eve::weapon::CarrierBodyDefenseSample>> sample(
        eve::weapon::CarrierHandle, ecs::EntityHandle, const eve::weapon::CarrierMotion&) const override {
        return eve::Result<std::vector<eve::weapon::CarrierBodyDefenseSample>>::success(bodies);
    }

    std::vector<eve::weapon::CarrierBodyDefenseSample> bodies;
};


// combat_carrier_script.cpp
TEST_CASE("combatCarrierScript.castAtTargetReportsHitEvents") {
    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::weapon::Weapon::expose(eve);
    vm.run(vm.compileSource(R"(
        weapon <- eve.Weapon();
        created <- weapon.newCarrierRuntime(16);
        pool <- created.value;
        pool.setTargetPosition(7, 1, 5.0, 0.0, 0.0);
        pool.addHitTarget(7, 1, 5.0, 0.0, 0.0, 0.6);
        casted <- pool.castAtTarget([
            { kind = "homing", turnRate = 90 },
            { kind = "pierce", count = 1 },
            { kind = "projectile", id = "spell:bolt", speed = 10, damage = 12, lifetime = 2, element = "fire" }
        ], 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 7, 1);
        missingTarget <- pool.cast([
            { kind = "homing", turnRate = 90 },
            { kind = "projectile", id = "spell:bolt", speed = 10, damage = 1, lifetime = 2 }
        ], 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
        frame <- pool.update(0.5);
        eventCount <- frame.value.eventCount;
        firstImpact <- frame.value.events[0].impact;
        firstTrigger <- frame.value.events[0].trigger;
        preset <- pool.registerRecipe({
            id = "spell:shell",
            lifetime = 1.5,
            speed = 8.0,
            damage = 4.0,
            motion = "linear"
        });
        spawned <- pool.spawn("spell:shell", 0.0, 0.0, 0.0, 0.0, 1.0, 0.0);
        // Preset OnHit recipes must advance even with an empty hit list.
        emptyHitFrame <- pool.update(0.1);
        invalid <- pool.cast([
            { kind = "projectile", id = "spell:a", speed = 1, damage = 1, lifetime = 1 },
            { kind = "homing", turnRate = 10 }
        ], 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
        oversized <- pool.addHitTarget(4294967296, 1, 0.0, 0.0, 0.0, 0.5);
        avoidBody <- pool.registerRecipe({
            id = "spell:avoid",
            lifetime = 1.0,
            speed = 5.0,
            motion = [ { kind = "avoidBody", avoidLookAhead = 2.0, maxTurnRateDegrees = 90.0 }, "linear" ],
            triggers = [ "onExpire" ],
            impacts = [ { kind = "release", on = "onExpire" } ]
        });
    )"));
    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK(vm.find("casted").toTable().get<bool>("ok"));
    CHECK(!vm.find("missingTarget").toTable().get<bool>("ok"));
    CHECK(vm.find("frame").toTable().get<bool>("ok"));
    CHECK(vm.find("eventCount").toInt() >= 1);
    CHECK_EQ(vm.find("firstImpact").toString(), std::string("emitHit"));
    CHECK_EQ(vm.find("firstTrigger").toString(), std::string("onHit"));
    CHECK(vm.find("preset").toTable().get<bool>("ok"));
    CHECK(vm.find("spawned").toTable().get<bool>("ok"));
    CHECK(vm.find("emptyHitFrame").toTable().get<bool>("ok"));
    CHECK(!vm.find("invalid").toTable().get<bool>("ok"));
    CHECK(!vm.find("oversized").toTable().get<bool>("ok"));
    CHECK(!vm.find("avoidBody").toTable().get<bool>("ok"));
}

TEST_CASE("combatCarrierScript.castUniquifiesRecipeIds") {
    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::weapon::Weapon::expose(eve);
    vm.run(vm.compileSource(R"(
        weapon <- eve.Weapon();
        pool <- weapon.newCarrierRuntime(16).value;
        first <- pool.cast([
            { kind = "damage", add = 1 },
            { kind = "projectile", id = "spell:bolt", speed = 10, damage = 5, lifetime = 2 }
        ], 0.0, 0.0, 0.0, 1.0, 0.0, 0.0);
        second <- pool.cast([
            { kind = "damage", multiply = 3 },
            { kind = "projectile", id = "spell:bolt", speed = 10, damage = 5, lifetime = 2 }
        ], 0.0, 1.0, 0.0, 1.0, 0.0, 0.0);
        pool.addHitTarget(1, 1, 5.0, 0.0, 0.0, 0.6);
        pool.addHitTarget(2, 1, 5.0, 1.0, 0.0, 0.6);
        frame <- pool.update(0.5);
        firstRecipe <- first.value.recipeId;
        secondRecipe <- second.value.recipeId;
        eventCount <- frame.value.eventCount;
        dmgA <- frame.value.events[0].damage;
        dmgB <- frame.value.events[1].damage;
    )"));
    CHECK(vm.find("first").toTable().get<bool>("ok"));
    CHECK(vm.find("second").toTable().get<bool>("ok"));
    CHECK(vm.find("frame").toTable().get<bool>("ok"));
    CHECK(vm.find("firstRecipe").toString() != vm.find("secondRecipe").toString());
    CHECK(vm.find("eventCount").toInt() >= 2);
    const double dmgA = vm.find("dmgA").toFloat();
    const double dmgB = vm.find("dmgB").toFloat();
    // First cast: 5+1=6; second: 5*3=15. Both must survive independently.
    const bool sawSix     = std::fabs(dmgA - 6.0) < 1e-6 || std::fabs(dmgB - 6.0) < 1e-6;
    const bool sawFifteen = std::fabs(dmgA - 15.0) < 1e-6 || std::fabs(dmgB - 15.0) < 1e-6;
    CHECK(sawSix);
    CHECK(sawFifteen);
}
