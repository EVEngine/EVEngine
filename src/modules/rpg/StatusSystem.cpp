#include "rpg/StatusSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/Effect.h"
#include "rpg/AttributeSystem.h"

#include <algorithm>
#include <limits>
#include <unordered_map>

namespace eve::rpg {

namespace {

std::vector<StatusTickEvent> &tickQueue() {
    static std::vector<StatusTickEvent> q;
    return q;
}

std::vector<StatusChangeEvent> &changeQueue() {
    static std::vector<StatusChangeEvent> q;
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

bool isBuiltinStackPolicy(const std::string &sp) {
    return sp == "none" || sp == "refresh" || sp == "extend" || sp == "stack";
}

}  // namespace

std::unordered_map<std::string, StatusSystem::ApplyCondition> &StatusSystem::applyConditions() {
    static std::unordered_map<std::string, ApplyCondition> t;
    return t;
}

std::unordered_map<std::string, StatusSystem::StackPolicyFn> &StatusSystem::stackPolicies() {
    static std::unordered_map<std::string, StackPolicyFn> t;
    return t;
}

std::unordered_map<std::string, StatusSystem::LifecycleHook> &StatusSystem::lifecycleHooks() {
    static std::unordered_map<std::string, LifecycleHook> t;
    return t;
}

void StatusSystem::registerApplyCondition(const std::string &name, ApplyCondition fn) {
    if (!name.empty()) applyConditions()[name] = std::move(fn);
}

void StatusSystem::unregisterApplyCondition(const std::string &name) {
    applyConditions().erase(name);
}

bool StatusSystem::hasApplyCondition(const std::string &name) {
    return applyConditions().count(name) > 0;
}

void StatusSystem::clearApplyConditions() { applyConditions().clear(); }

void StatusSystem::registerStackPolicy(const std::string &name, StackPolicyFn fn) {
    if (!name.empty()) stackPolicies()[name] = std::move(fn);
}

void StatusSystem::unregisterStackPolicy(const std::string &name) { stackPolicies().erase(name); }

bool StatusSystem::hasStackPolicy(const std::string &name) {
    return stackPolicies().count(name) > 0;
}

void StatusSystem::clearStackPolicies() { stackPolicies().clear(); }

void StatusSystem::registerLifecycleHook(const std::string &name, LifecycleHook fn) {
    if (!name.empty()) lifecycleHooks()[name] = std::move(fn);
}

void StatusSystem::unregisterLifecycleHook(const std::string &name) {
    lifecycleHooks().erase(name);
}

bool StatusSystem::hasLifecycleHook(const std::string &name) {
    return lifecycleHooks().count(name) > 0;
}

void StatusSystem::clearLifecycleHooks() { lifecycleHooks().clear(); }

void StatusSystem::emitChange(StatusChangeEvent ev) {
    changeQueue().push_back(ev);
    for (auto &kv : lifecycleHooks()) {
        if (kv.second) kv.second(ev);
    }
}

StatusInstance *StatusSystem::findByInstanceId(RPGActor *actor, int instanceId) {
    if (!actor) return nullptr;
    auto &active = actor->statuses()->active;
    auto it = std::find_if(active.begin(), active.end(),
                            [&](const StatusInstance &s) { return s.instanceId == instanceId; });
    return it == active.end() ? nullptr : &(*it);
}

int StatusSystem::apply(RPGActor *actor, const std::string &effectId, const std::string &source) {
    if (!actor) return -1;
    const EffectDefinition *def = EffectRegistry::find(effectId);
    if (!def) return -1;

    for (auto &kv : applyConditions()) {
        if (!kv.second) continue;
        std::string reason;
        if (!kv.second(actor, *def, source, reason)) {
            StatusChangeEvent reject;
            reject.actor = actor;
            reject.effectId = effectId;
            reject.source = source;
            reject.action = "reject";
            reject.reason = reason.empty() ? kv.first : reason;
            emitChange(std::move(reject));
            return -1;
        }
    }

    if (def->durationPolicy == "instant") {
        // One-shot: nudge base values directly; nothing to track/undo later.
        for (const auto &spec : def->modifiers) AttributeSystem::modifyBase(actor, spec.attribute, spec.value);
        StatusChangeEvent applied;
        applied.actor = actor;
        applied.effectId = effectId;
        applied.source = source;
        applied.action = "apply";
        applied.stacks = 0;
        emitChange(std::move(applied));
        return 0;
    }

    auto &active = actor->statuses()->active;
    auto it = std::find_if(active.begin(), active.end(),
                            [&](const StatusInstance &s) { return s.effectId == effectId; });

    if (it != active.end()) {
        const std::string &sp = def->stackPolicy;

        auto customIt = stackPolicies().find(sp);
        if (customIt != stackPolicies().end() && customIt->second) {
            const int result = customIt->second(actor, *it, *def, source);
            if (result > 0) {
                // Refresh modifiers if the instance is still present and non-periodic.
                auto *live = findByInstanceId(actor, result);
                if (live && def->period <= 0.f) applyModifiersForInstance(actor, *def, *live);
                StatusChangeEvent stacked;
                stacked.actor = actor;
                stacked.instanceId = result;
                stacked.effectId = effectId;
                stacked.source = source;
                stacked.action = "stack";
                stacked.stacks = live ? live->stacks : it->stacks;
                emitChange(std::move(stacked));
            } else if (result < 0) {
                StatusChangeEvent reject;
                reject.actor = actor;
                reject.instanceId = it->instanceId;
                reject.effectId = effectId;
                reject.source = source;
                reject.action = "reject";
                reject.stacks = it->stacks;
                reject.reason = "stackPolicy:" + sp;
                emitChange(std::move(reject));
            }
            return result;
        }

        if (sp == "refresh") {
            if (def->durationPolicy == "duration") it->remaining = def->duration;
            StatusChangeEvent refreshed;
            refreshed.actor = actor;
            refreshed.instanceId = it->instanceId;
            refreshed.effectId = effectId;
            refreshed.source = source;
            refreshed.action = "refresh";
            refreshed.stacks = it->stacks;
            emitChange(std::move(refreshed));
            return it->instanceId;
        }
        if (sp == "extend") {
            if (def->durationPolicy == "duration" && it->remaining >= 0.f) it->remaining += def->duration;
            StatusChangeEvent extended;
            extended.actor = actor;
            extended.instanceId = it->instanceId;
            extended.effectId = effectId;
            extended.source = source;
            extended.action = "extend";
            extended.stacks = it->stacks;
            emitChange(std::move(extended));
            return it->instanceId;
        }
        if (sp == "stack") {
            const int cap = def->maxStacks > 0 ? def->maxStacks : std::numeric_limits<int>::max();
            if (it->stacks < cap) it->stacks += 1;
            if (def->durationPolicy == "duration") it->remaining = def->duration;
            if (def->period <= 0.f) applyModifiersForInstance(actor, *def, *it);
            StatusChangeEvent stacked;
            stacked.actor = actor;
            stacked.instanceId = it->instanceId;
            stacked.effectId = effectId;
            stacked.source = source;
            stacked.action = "stack";
            stacked.stacks = it->stacks;
            emitChange(std::move(stacked));
            return it->instanceId;
        }
        // "none" (or unknown unregistered policy) rejects duplicate application.
        StatusChangeEvent reject;
        reject.actor = actor;
        reject.instanceId = it->instanceId;
        reject.effectId = effectId;
        reject.source = source;
        reject.action = "reject";
        reject.stacks = it->stacks;
        reject.reason = isBuiltinStackPolicy(sp) ? "stackPolicy:none" : ("unknownStackPolicy:" + sp);
        emitChange(std::move(reject));
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
    const int stacks = inst.stacks;
    active.push_back(std::move(inst));

    StatusChangeEvent applied;
    applied.actor = actor;
    applied.instanceId = id;
    applied.effectId = effectId;
    applied.source = source;
    applied.action = "apply";
    applied.stacks = stacks;
    emitChange(std::move(applied));
    return id;
}

bool StatusSystem::remove(RPGActor *actor, int instanceId) {
    if (!actor) return false;
    auto &active = actor->statuses()->active;
    auto it = std::find_if(active.begin(), active.end(),
                            [&](const StatusInstance &s) { return s.instanceId == instanceId; });
    if (it == active.end()) return false;

    StatusChangeEvent removed;
    removed.actor = actor;
    removed.instanceId = it->instanceId;
    removed.effectId = it->effectId;
    removed.source = it->source;
    removed.action = "remove";
    removed.stacks = it->stacks;

    unapplyInstance(actor, *it);
    active.erase(it);
    emitChange(std::move(removed));
    return true;
}

int StatusSystem::removeByEffect(RPGActor *actor, const std::string &effectId) {
    if (!actor) return 0;
    auto &active = actor->statuses()->active;
    int count = 0;
    for (auto it = active.begin(); it != active.end();) {
        if (it->effectId == effectId) {
            StatusChangeEvent removed;
            removed.actor = actor;
            removed.instanceId = it->instanceId;
            removed.effectId = it->effectId;
            removed.source = it->source;
            removed.action = "remove";
            removed.stacks = it->stacks;
            unapplyInstance(actor, *it);
            it = active.erase(it);
            emitChange(std::move(removed));
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
            StatusChangeEvent removed;
            removed.actor = actor;
            removed.instanceId = it->instanceId;
            removed.effectId = it->effectId;
            removed.source = it->source;
            removed.action = "remove";
            removed.stacks = it->stacks;
            unapplyInstance(actor, *it);
            it = active.erase(it);
            emitChange(std::move(removed));
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
            StatusChangeEvent removed;
            removed.actor = actor;
            removed.instanceId = it->instanceId;
            removed.effectId = it->effectId;
            removed.source = it->source;
            removed.action = "remove";
            removed.stacks = it->stacks;
            unapplyInstance(actor, *it);
            it = active.erase(it);
            emitChange(std::move(removed));
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

bool StatusSystem::hasTag(RPGActor *actor, const std::string &tag) {
    if (!actor) return false;
    for (const auto &inst : actor->statuses()->active) {
        const EffectDefinition *def = EffectRegistry::find(inst.effectId);
        if (def && def->hasTag(tag)) return true;
    }
    return false;
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

std::string StatusSystem::getActiveSource(RPGActor *actor, int index) {
    if (!actor) return {};
    auto &active = actor->statuses()->active;
    if (index < 0 || size_t(index) >= active.size()) return {};
    return active[size_t(index)].source;
}

std::string StatusSystem::getProp(RPGActor *actor, int instanceId, const std::string &key,
                                   const std::string &fallback) {
    StatusInstance *inst = findByInstanceId(actor, instanceId);
    if (!inst) return fallback;
    auto it = inst->props.find(key);
    return it == inst->props.end() ? fallback : it->second;
}

bool StatusSystem::setProp(RPGActor *actor, int instanceId, const std::string &key,
                            const std::string &value) {
    StatusInstance *inst = findByInstanceId(actor, instanceId);
    if (!inst) return false;
    inst->props[key] = value;
    return true;
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
                StatusChangeEvent expiredEv;
                expiredEv.actor = actor;
                expiredEv.instanceId = it->instanceId;
                expiredEv.effectId = it->effectId;
                expiredEv.source = it->source;
                expiredEv.action = "expire";
                expiredEv.stacks = it->stacks;
                unapplyInstance(actor, *it);
                it = active.erase(it);
                emitChange(std::move(expiredEv));
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

void StatusSystem::pollChanges(std::vector<StatusChangeEvent> &out) {
    auto &q = changeQueue();
    for (auto &e : q) out.push_back(std::move(e));
    q.clear();
}

}  // namespace eve::rpg
