#include "rpg/Combat.h"
#include "rpg/RPGActor.h"

#include <algorithm>
#include <random>

namespace eve::rpg {

namespace {
std::vector<CombatEvent>& events() {
    static std::vector<CombatEvent> value;
    return value;
}
std::mt19937& rng() {
    static std::mt19937 value(0x455645u);
    return value;
}
}  // namespace

CombatRules& CombatSystem::rules() {
    static CombatRules value;
    return value;
}
void CombatSystem::setRandomSeed(uint32_t seed) { rng().seed(seed); }

double CombatSystem::dealDamage(RPGActor* source, RPGActor* target, double power, const std::string& damageType) {
    if (!target || power <= 0.0) return 0.0;
    auto&  r      = rules();
    double amount = power + (source ? source->getFinalAttribute(r.attackAttribute) : 0.0);
    double armor  = std::max(0.0, target->getFinalAttribute(r.armorAttribute));
    amount *= r.armorConstant / (r.armorConstant + armor);
    std::string resistAttribute = "resist." + (damageType.empty() ? "physical" : damageType);
    double      resistance      = std::clamp(target->getFinalAttribute(resistAttribute), -1.0, r.maxResistance);
    amount *= 1.0 - resistance;

    bool critical = false;
    if (source) {
        double chance = std::clamp(source->getFinalAttribute(r.critChanceAttribute), 0.0, 1.0);
        critical      = std::generate_canonical<double, 24>(rng()) < chance;
        if (critical) amount *= std::max(1.0, source->getFinalAttribute(r.critMultiplierAttribute));
    }
    amount        = std::max(r.minDamage, amount);
    double before = target->getFinalAttribute(r.healthAttribute);
    target->modifyBaseAttribute(r.healthAttribute, -amount);
    double after = std::max(0.0, target->getFinalAttribute(r.healthAttribute));
    if (after == 0.0 && target->getBaseAttribute(r.healthAttribute) < 0.0)
        target->setBaseAttribute(r.healthAttribute, 0.0);
    events().push_back(
        {source, target, "damage", damageType, amount, before, after, critical, before > 0.0 && after <= 0.0});
    return amount;
}

double CombatSystem::heal(RPGActor* source, RPGActor* target, double amount) {
    if (!target || amount <= 0.0) return 0.0;
    auto&  r      = rules();
    double before = target->getFinalAttribute(r.healthAttribute);
    target->modifyBaseAttribute(r.healthAttribute, amount);
    double after = target->getFinalAttribute(r.healthAttribute);
    events().push_back({source, target, "heal", {}, after - before, before, after, false, false});
    return after - before;
}

void CombatSystem::pollEvents(std::vector<CombatEvent>& out) {
    out = std::move(events());
    events().clear();
}

}  // namespace eve::rpg
