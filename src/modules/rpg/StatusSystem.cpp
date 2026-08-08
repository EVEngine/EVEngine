#include "rpg/StatusSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/Effect.h"
#include "rpg/AttributeSystem.h"

#include <algorithm>
#include <limits>

namespace eve::rpg {

namespace {

std::vector<StatusTickEvent> &tickQueue() {
    static std::vector<StatusTickEvent> q;
    return q;
}

void unapplyInstance(RPGActor *actor, StatusInstance &inst) {
    for (auto &kv : inst.appliedModifiers)
        AttributeSystem::removeModifier(actor, kv.first, kv.second);
    inst.appliedModifiers.clear();
}

/** (Re)writes the attribute modifiers for a non-periodic instance, scaled by stacks. */
void applyModifiersForInstance(RPGActor *actor, const EffectDefinition &def, StatusInstance &inst) {
    unapplyInstance(actor, inst);
    const std::string source = "effect:" + def.id + "#" + std::to_string(inst.instanceId);
    for (const auto &spec : def.modifiers) {
        const double value = spec.value * double(inst.stacks);
        std::string modId =
            AttributeSystem::addModifier(actor, spec.attribute, source, spec.op, value, spec.priority);
        inst.appliedModifiers.emplace_back(spec.attribute, modId);
    }
}

}  // namespace

int StatusSystem::apply(RPGActor *actor, const std::string &effectId, const std::string &source) {
    if (!actor) return -1;
    const EffectDefinition *def = EffectRegistry::find(effectId);
    if (!def) return -1;

    if (def->durationPolicy == "instant") {
        // One-shot: nudge base values directly; nothing to track/undo later.
        for (const auto &spec : def->modifiers) AttributeSystem::modifyBase(actor, spec.attribute, spec.value);
        return 0;
    }

    auto &active = actor->statuses()->active;
    auto it = std::find_if(active.begin(), active.end(),
                            [&](const StatusInstance &s) { return s.effectId == effectId; });

    if (it != active.end()) {
        const std::string &sp = def->stackPolicy;
        if (sp == "refresh") {
            if (def->durationPolicy == "duration") it->remaining = def->duration;
            return it->instanceId;
        }
        if (sp == "extend") {
            if (def->durationPolicy == "duration" && it->remaining >= 0.f) it->remaining += def->duration;
            return it->instanceId;
        }
        if (sp == "stack") {
            const int cap = def->maxStacks > 0 ? def->maxStacks : std::numeric_limits<int>::max();
            if (it->stacks < cap) it->stacks += 1;
            if (def->durationPolicy == "duration") it->remaining = def->duration;
            if (def->period <= 0.f) applyModifiersForInstance(actor, *def, *it);
            return it->instanceId;
        }
        // "none" (or unknown policy) rejects duplicate application.
        return -1;
    }

    StatusInstance inst;
    inst.instanceId = actor->statuses()->nextInstanceId++;
    inst.effectId = effectId;
    inst.source = source;
    inst.stacks = 1;
    inst.remaining = (def->durationPolicy == "duration") ? def->duration : -1.f;
    inst.periodAccum = 0.f;

    if (def->period <= 0.f) applyModifiersForInstance(actor, *def, inst);

    const int id = inst.instanceId;
    active.push_back(std::move(inst));
    return id;
}

bool StatusSystem::remove(RPGActor *actor, int instanceId) {
    if (!actor) return false;
    auto &active = actor->statuses()->active;
    auto it = std::find_if(active.begin(), active.end(),
                            [&](const StatusInstance &s) { return s.instanceId == instanceId; });
    if (it == active.end()) return false;
    unapplyInstance(actor, *it);
    active.erase(it);
    return true;
}

int StatusSystem::removeByEffect(RPGActor *actor, const std::string &effectId) {
    if (!actor) return 0;
    auto &active = actor->statuses()->active;
    int count = 0;
    for (auto it = active.begin(); it != active.end();) {
        if (it->effectId == effectId) {
            unapplyInstance(actor, *it);
            it = active.erase(it);
            ++count;
        } else {
            ++it;
        }
    }
    return count;
}

int StatusSystem::removeBySource(RPGActor *actor, const std::string &source) {
    if (!actor) return 0;
    auto &active = actor->statuses()->active;
    int count = 0;
    for (auto it = active.begin(); it != active.end();) {
        if (it->source == source) {
            unapplyInstance(actor, *it);
            it = active.erase(it);
            ++count;
        } else {
            ++it;
        }
    }
    return count;
}

int StatusSystem::removeByTag(RPGActor *actor, const std::string &tag) {
    if (!actor) return 0;
    auto &active = actor->statuses()->active;
    int count = 0;
    for (auto it = active.begin(); it != active.end();) {
        const EffectDefinition *def = EffectRegistry::find(it->effectId);
        if (def && def->hasTag(tag)) {
            unapplyInstance(actor, *it);
            it = active.erase(it);
            ++count;
        } else {
            ++it;
        }
    }
    return count;
}

bool StatusSystem::hasEffect(RPGActor *actor, const std::string &effectId) {
    if (!actor) return false;
    auto &active = actor->statuses()->active;
    return std::find_if(active.begin(), active.end(), [&](const StatusInstance &s) {
               return s.effectId == effectId;
           }) != active.end();
}

int StatusSystem::getActiveCount(RPGActor *actor) {
    if (!actor) return 0;
    return int(actor->statuses()->active.size());
}

std::string StatusSystem::getActiveEffectId(RPGActor *actor, int index) {
    if (!actor) return {};
    auto &active = actor->statuses()->active;
    if (index < 0 || size_t(index) >= active.size()) return {};
    return active[size_t(index)].effectId;
}

int StatusSystem::getActiveStacks(RPGActor *actor, int index) {
    if (!actor) return 0;
    auto &active = actor->statuses()->active;
    if (index < 0 || size_t(index) >= active.size()) return 0;
    return active[size_t(index)].stacks;
}

float StatusSystem::getActiveRemaining(RPGActor *actor, int index) {
    if (!actor) return 0.f;
    auto &active = actor->statuses()->active;
    if (index < 0 || size_t(index) >= active.size()) return 0.f;
    return active[size_t(index)].remaining;
}

int StatusSystem::getActiveInstanceId(RPGActor *actor, int index) {
    if (!actor) return 0;
    auto &active = actor->statuses()->active;
    if (index < 0 || size_t(index) >= active.size()) return 0;
    return active[size_t(index)].instanceId;
}

void StatusSystem::update(float dt) {
    for (RPGActor *actor : RPGActor::liveActors()) {
        auto &active = actor->statuses()->active;
        for (auto it = active.begin(); it != active.end();) {
            const EffectDefinition *def = EffectRegistry::find(it->effectId);
            bool expired = false;

            if (it->remaining >= 0.f) {
                it->remaining -= dt;
                if (it->remaining <= 0.f) expired = true;
            }

            if (def && def->period > 0.f) {
                it->periodAccum += dt;
                while (it->periodAccum >= def->period) {
                    it->periodAccum -= def->period;
                    StatusTickEvent evt;
                    evt.actor = actor;
                    evt.instanceId = it->instanceId;
                    evt.effectId = it->effectId;
                    evt.source = it->source;
                    evt.stacks = it->stacks;
                    tickQueue().push_back(std::move(evt));
                }
            }

            if (expired) {
                unapplyInstance(actor, *it);
                it = active.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void StatusSystem::pollTicks(std::vector<StatusTickEvent> &out) {
    auto &q = tickQueue();
    for (auto &e : q) out.push_back(std::move(e));
    q.clear();
}

}  // namespace eve::rpg
