#include "rpg/AttributeSystem.h"
#include "rpg/RPGActor.h"

#include <algorithm>
#include <atomic>

namespace eve::rpg {

void AttributeOpTable::registerOp(const std::string &name, Fn fn) { ops_[name] = std::move(fn); }

void AttributeOpTable::unregisterOp(const std::string &name) { ops_.erase(name); }

bool AttributeOpTable::has(const std::string &name) const { return ops_.find(name) != ops_.end(); }

double AttributeOpTable::apply(const std::string &name, double current, double value) const {
    auto it = ops_.find(name);
    if (it == ops_.end()) return current;
    return it->second(current, value);
}

double computeAttributeValue(const AttributeValue &attr, const AttributeOpTable *customOps) {
    double addSum = 0.0;
    double mulAddSum = 0.0;
    double mulMulProduct = 1.0;
    std::vector<const AttributeModifier *> sequential;
    sequential.reserve(attr.modifiers.size());

    for (const auto &m : attr.modifiers) {
        if (m.op == "add")
            addSum += m.value;
        else if (m.op == "mulAdd")
            mulAddSum += m.value;
        else if (m.op == "mulMul")
            mulMulProduct *= (1.0 + m.value);
        else
            sequential.push_back(&m);
    }

    double result = attr.base + addSum;
    result *= (1.0 + mulAddSum);
    result *= mulMulProduct;

    // override / clampMin / clampMax / custom ops are order-sensitive: apply by
    // ascending priority (stable, so equal-priority keeps insertion order).
    std::stable_sort(sequential.begin(), sequential.end(),
                      [](const AttributeModifier *a, const AttributeModifier *b) {
                          return a->priority < b->priority;
                      });

    for (const AttributeModifier *m : sequential) {
        if (m->op == "override")
            result = m->value;
        else if (m->op == "clampMin")
            result = std::max(result, m->value);
        else if (m->op == "clampMax")
            result = std::min(result, m->value);
        else if (customOps && customOps->has(m->op))
            result = customOps->apply(m->op, result, m->value);
        // unknown op names are silently ignored (forward-compat: data may
        // reference ops from a newer game build not registered here).
    }
    return result;
}

AttributeOpTable &AttributeSystem::customOps() {
    static AttributeOpTable table;
    return table;
}

std::string AttributeSystem::nextModifierId() {
    static std::atomic<uint64_t> counter{0};
    return "mod#" + std::to_string(++counter);
}

void AttributeSystem::setBase(RPGActor *actor, const std::string &attribute, double value) {
    if (!actor) return;
    auto &av = actor->attributes()->values[attribute];
    av.base = value;
    av.dirty = true;
}

double AttributeSystem::getBase(RPGActor *actor, const std::string &attribute) {
    if (!actor) return 0.0;
    auto &values = actor->attributes()->values;
    auto it = values.find(attribute);
    return it == values.end() ? 0.0 : it->second.base;
}

void AttributeSystem::modifyBase(RPGActor *actor, const std::string &attribute, double delta) {
    if (!actor) return;
    auto &av = actor->attributes()->values[attribute];
    av.base += delta;
    av.dirty = true;
}

bool AttributeSystem::hasAttribute(RPGActor *actor, const std::string &attribute) {
    if (!actor) return false;
    auto &values = actor->attributes()->values;
    return values.find(attribute) != values.end();
}

std::string AttributeSystem::addModifier(RPGActor *actor, const std::string &attribute,
                                          const std::string &source, const std::string &op,
                                          double value, int priority) {
    if (!actor) return {};
    std::string id = nextModifierId();
    auto &av = actor->attributes()->values[attribute];
    AttributeModifier mod;
    mod.id = id;
    mod.source = source;
    mod.op = op;
    mod.value = value;
    mod.priority = priority;
    av.modifiers.push_back(std::move(mod));
    av.dirty = true;
    return id;
}

bool AttributeSystem::removeModifier(RPGActor *actor, const std::string &attribute,
                                      const std::string &modifierId) {
    if (!actor) return false;
    auto &values = actor->attributes()->values;
    auto it = values.find(attribute);
    if (it == values.end()) return false;
    auto &mods = it->second.modifiers;
    auto mit = std::find_if(mods.begin(), mods.end(),
                             [&](const AttributeModifier &m) { return m.id == modifierId; });
    if (mit == mods.end()) return false;
    mods.erase(mit);
    it->second.dirty = true;
    return true;
}

int AttributeSystem::removeModifiersBySource(RPGActor *actor, const std::string &attribute,
                                              const std::string &source) {
    if (!actor) return 0;
    auto &values = actor->attributes()->values;
    auto it = values.find(attribute);
    if (it == values.end()) return 0;
    auto &mods = it->second.modifiers;
    size_t before = mods.size();
    mods.erase(std::remove_if(mods.begin(), mods.end(),
                               [&](const AttributeModifier &m) { return m.source == source; }),
               mods.end());
    int removed = int(before - mods.size());
    if (removed > 0) it->second.dirty = true;
    return removed;
}

int AttributeSystem::removeAllModifiersBySource(RPGActor *actor, const std::string &source) {
    if (!actor) return 0;
    int total = 0;
    for (auto &kv : actor->attributes()->values) {
        auto &mods = kv.second.modifiers;
        size_t before = mods.size();
        mods.erase(std::remove_if(mods.begin(), mods.end(),
                                   [&](const AttributeModifier &m) { return m.source == source; }),
                   mods.end());
        int removed = int(before - mods.size());
        if (removed > 0) {
            kv.second.dirty = true;
            total += removed;
        }
    }
    return total;
}

double AttributeSystem::getFinal(RPGActor *actor, const std::string &attribute) {
    if (!actor) return 0.0;
    auto &av = actor->attributes()->values[attribute];
    if (av.dirty) {
        av.cached = computeAttributeValue(av, &customOps());
        av.dirty = false;
    }
    return av.cached;
}

void AttributeSystem::invalidate(RPGActor *actor, const std::string &attribute) {
    if (!actor) return;
    auto &values = actor->attributes()->values;
    auto it = values.find(attribute);
    if (it != values.end()) it->second.dirty = true;
}

}  // namespace eve::rpg
