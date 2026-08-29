#include "rpg/VitalsSystem.h"

#include "rpg/AttributeSystem.h"
#include "rpg/RPGActor.h"

#include <algorithm>

namespace eve::rpg {

namespace {
std::vector<VitalsEvent> &vitalsEvents() {
    static std::vector<VitalsEvent> events;
    return events;
}

void emit(RPGActor *actor, std::string resource, std::string action, double amount,
          std::string source, double current, double max) {
    VitalsEvent ev;
    ev.actor = actor;
    ev.resource = std::move(resource);
    ev.action = std::move(action);
    ev.amount = amount;
    ev.source = std::move(source);
    ev.current = current;
    ev.max = max;
    vitalsEvents().push_back(std::move(ev));
}
}  // namespace

double VitalsSystem::getCurrent(RPGActor *actor, const std::string &resource) {
    if (!actor) return 0.0;
    auto it = actor->vitals()->current.find(resource);
    return it == actor->vitals()->current.end() ? 0.0 : it->second;
}

double VitalsSystem::getMax(RPGActor *actor, const std::string &resource) {
    return actor ? AttributeSystem::getFinal(actor, resource) : 0.0;
}

void VitalsSystem::setCurrent(RPGActor *actor, const std::string &resource, double value) {
    if (!actor) return;
    const double max = getMax(actor, resource);
    actor->vitals()->current[resource] = std::clamp(value, 0.0, max);
}

double VitalsSystem::takeDamage(RPGActor *actor, const std::string &resource, double amount,
                                const std::string &source) {
    if (!actor || amount <= 0.0) return 0.0;
    auto &current = actor->vitals()->current;
    const double max = getMax(actor, resource);
    const double cur = getCurrent(actor, resource);
    if (cur <= 0.0) return 0.0;  // 已死，不再受伤
    const double applied = std::min(amount, cur);
    current[resource] = cur - applied;
    emit(actor, resource, "damage", applied, source, cur - applied, max);
    if (cur - applied <= 0.0) {
        emit(actor, resource, "death", applied, source, 0.0, max);
    }
    return applied;
}

double VitalsSystem::heal(RPGActor *actor, const std::string &resource, double amount) {
    if (!actor || amount <= 0.0) return 0.0;
    auto &current = actor->vitals()->current;
    const double max = getMax(actor, resource);
    const double cur = getCurrent(actor, resource);
    const double applied = std::min(amount, max - cur);
    if (applied <= 0.0) return 0.0;
    current[resource] = cur + applied;
    emit(actor, resource, "heal", applied, "", cur + applied, max);
    return applied;
}

void VitalsSystem::revive(RPGActor *actor, const std::string &resource, double amount) {
    if (!actor) return;
    auto &current = actor->vitals()->current;
    const double max = getMax(actor, resource);
    const double value = amount >= 0.0 ? std::min(amount, max) : max;
    current[resource] = value;
    emit(actor, resource, "revive", value, "", value, max);
}

bool VitalsSystem::isDead(RPGActor *actor, const std::string &resource) {
    const double max = getMax(actor, resource);
    return max > 0.0 && getCurrent(actor, resource) <= 0.0;
}

void VitalsSystem::pollEvents(std::vector<VitalsEvent> &out) {
    out.clear();
    out = std::move(vitalsEvents());
    vitalsEvents().clear();
}

}  // namespace eve::rpg