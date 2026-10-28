#include "rpg/Trait.h"
#include <algorithm>
#include "common/Json.h"
#include "rpg/AttributeSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/TraitSystem.h"

// Trait.cpp
namespace eve::rpg {

using eve::json::Value;

bool TraitDefinition::hasTag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::string TraitDefinition::getExtra(const std::string &key, const std::string &fallback) const {
    auto it = extra.find(key);
    return it == extra.end() ? fallback : it->second;
}

namespace {
std::unordered_map<std::string, TraitDefinition> &traitTable() {
    static std::unordered_map<std::string, TraitDefinition> t;
    return t;
}
}  // namespace

void TraitRegistry::registerTrait(const TraitDefinition &def) {
    if (def.id.empty()) return;
    traitTable()[def.id] = def;
}

const TraitDefinition *TraitRegistry::find(const std::string &id) {
    auto &t  = traitTable();
    auto  it = t.find(id);
    return it == t.end() ? nullptr : &it->second;
}

bool TraitRegistry::remove(const std::string &id) { return traitTable().erase(id) > 0; }

void TraitRegistry::clear() { traitTable().clear(); }

int TraitRegistry::count() { return int(traitTable().size()); }

int TraitRegistry::loadFromJson(const std::string &json, std::string *error) {
    std::string               err;
    const eve::json::Document doc = eve::json::Document::parse(json, &err);
    if (!doc.valid()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return 0;
    }
    const Value root     = doc.root();
    int         n        = 0;
    const auto  parseOne = [&](Value o) {
        if (!o.isObject()) return;
        TraitDefinition def;
        def.id = o.getString("id");
        if (def.id.empty()) return;
        def.tags           = o.getStringArray("tags");
        def.extra          = o.getStringMap("extra");
        const Value traits = o.get("traits");
        for (size_t i = 0; i < traits.size(); ++i) {
            const Value t = traits.at(i);
            TraitSpec   spec;
            spec.kind   = t.getString("kind");
            spec.target = t.getString("target");
            spec.value  = t.getDouble("value", 0.0);
            if (spec.kind.empty()) continue;
            def.traits.push_back(std::move(spec));
        }
        traitTable()[def.id] = std::move(def);
        ++n;
    };
    if (root.isArray()) {
        for (size_t i = 0; i < root.size(); ++i) parseOne(root.at(i));
    } else if (root.isObject()) {
        parseOne(root);
    }
    return n;
}

}  // namespace eve::rpg

// TraitSystem.cpp
namespace eve::rpg {

namespace {

const TraitDefinition *findDef(const std::string &id) { return TraitRegistry::find(id); }

bool matches(const TraitSpec &spec, const std::string &target) { return spec.target.empty() || spec.target == target; }

}  // namespace

int TraitSystem::apply(RPGActor *actor, const std::string &traitId, const std::string &source) {
    if (!actor) return 0;
    const TraitDefinition *def = findDef(traitId);
    if (!def) return 0;
    auto                   &ts = *actor->traits();
    RPGActor::TraitInstance inst;
    inst.instanceId = ts.nextInstanceId++;
    inst.traitId    = traitId;
    inst.source     = source;
    // paramRate → 属性修改器（mulAdd 倍率），source="trait:<instanceId>"
    for (const auto &spec : def->traits) {
        if (spec.kind != "paramRate" || spec.target.empty()) continue;
        AttributeSystem::addModifier(actor, spec.target, "trait:" + std::to_string(inst.instanceId), "mulAdd",
                                     spec.value - 1.0, 0);
    }
    ts.active.push_back(std::move(inst));
    return inst.instanceId;
}

bool TraitSystem::remove(RPGActor *actor, int instanceId) {
    if (!actor) return false;
    auto &ts = *actor->traits();
    for (auto it = ts.active.begin(); it != ts.active.end(); ++it) {
        if (it->instanceId != instanceId) continue;
        AttributeSystem::removeAllModifiersBySource(actor, "trait:" + std::to_string(instanceId));
        ts.active.erase(it);
        return true;
    }
    return false;
}

int TraitSystem::removeBySource(RPGActor *actor, const std::string &source) {
    if (!actor) return 0;
    auto &ts = *actor->traits();
    int   n  = 0;
    for (auto it = ts.active.begin(); it != ts.active.end();) {
        if (it->source == source) {
            AttributeSystem::removeAllModifiersBySource(actor, "trait:" + std::to_string(it->instanceId));
            it = ts.active.erase(it);
            ++n;
        } else {
            ++it;
        }
    }
    return n;
}

int TraitSystem::removeByTrait(RPGActor *actor, const std::string &traitId) {
    if (!actor) return 0;
    auto &ts = *actor->traits();
    int   n  = 0;
    for (auto it = ts.active.begin(); it != ts.active.end();) {
        if (it->traitId == traitId) {
            AttributeSystem::removeAllModifiersBySource(actor, "trait:" + std::to_string(it->instanceId));
            it = ts.active.erase(it);
            ++n;
        } else {
            ++it;
        }
    }
    return n;
}

void TraitSystem::removeAll(RPGActor *actor) {
    if (!actor) return;
    auto &ts = *actor->traits();
    for (const auto &inst : ts.active) {
        AttributeSystem::removeAllModifiersBySource(actor, "trait:" + std::to_string(inst.instanceId));
    }
    ts.active.clear();
}

bool TraitSystem::hasTrait(RPGActor *actor, const std::string &traitId) {
    if (!actor) return false;
    for (const auto &inst : actor->traits()->active) {
        if (inst.traitId == traitId) return true;
    }
    return false;
}

int TraitSystem::getCount(RPGActor *actor) { return actor ? int(actor->traits()->active.size()) : 0; }

int TraitSystem::getInstanceIdAt(RPGActor *actor, int index) {
    if (!actor || index < 0 || size_t(index) >= actor->traits()->active.size()) return 0;
    return actor->traits()->active[size_t(index)].instanceId;
}

std::string TraitSystem::getTraitIdAt(RPGActor *actor, int index) {
    if (!actor || index < 0 || size_t(index) >= actor->traits()->active.size()) return {};
    return actor->traits()->active[size_t(index)].traitId;
}

std::string TraitSystem::getSourceAt(RPGActor *actor, int index) {
    if (!actor || index < 0 || size_t(index) >= actor->traits()->active.size()) return {};
    return actor->traits()->active[size_t(index)].source;
}

double TraitSystem::getParamRate(RPGActor *actor, const std::string &param) {
    if (!actor) return 1.0;
    double product = 1.0;
    for (const auto &inst : actor->traits()->active) {
        const TraitDefinition *def = findDef(inst.traitId);
        if (!def) continue;
        for (const auto &spec : def->traits) {
            if (spec.kind == "paramRate" && spec.target == param) product *= spec.value;
        }
    }
    return product;
}

double TraitSystem::getElementRate(RPGActor *actor, const std::string &element) {
    if (!actor) return 1.0;
    double product = 1.0;
    for (const auto &inst : actor->traits()->active) {
        const TraitDefinition *def = findDef(inst.traitId);
        if (!def) continue;
        for (const auto &spec : def->traits) {
            if (spec.kind == "elementRate" && matches(spec, element)) product *= spec.value;
        }
    }
    return product;
}

double TraitSystem::getStateRate(RPGActor *actor, const std::string &stateId) {
    if (!actor) return 1.0;
    double product = 1.0;
    for (const auto &inst : actor->traits()->active) {
        const TraitDefinition *def = findDef(inst.traitId);
        if (!def) continue;
        for (const auto &spec : def->traits) {
            if (spec.kind == "stateRate" && matches(spec, stateId)) product *= spec.value;
        }
    }
    return product;
}

bool TraitSystem::isStateResist(RPGActor *actor, const std::string &stateId) {
    if (!actor) return false;
    for (const auto &inst : actor->traits()->active) {
        const TraitDefinition *def = findDef(inst.traitId);
        if (!def) continue;
        for (const auto &spec : def->traits) {
            if (spec.kind == "stateResist" && matches(spec, stateId)) return true;
        }
    }
    return false;
}

double TraitSystem::getExParam(RPGActor *actor, const std::string &exParam) {
    if (!actor) return 0.0;
    double sum = 0.0;
    for (const auto &inst : actor->traits()->active) {
        const TraitDefinition *def = findDef(inst.traitId);
        if (!def) continue;
        for (const auto &spec : def->traits) {
            if (spec.kind == "exParam" && spec.target == exParam) sum += spec.value;
        }
    }
    return sum;
}

std::vector<std::string> TraitSystem::getAttackElements(RPGActor *actor) {
    std::vector<std::string> out;
    if (!actor) return out;
    for (const auto &inst : actor->traits()->active) {
        const TraitDefinition *def = findDef(inst.traitId);
        if (!def) continue;
        for (const auto &spec : def->traits) {
            if (spec.kind == "attackElement" && !spec.target.empty()) {
                if (std::find(out.begin(), out.end(), spec.target) == out.end()) out.push_back(spec.target);
            }
        }
    }
    return out;
}

std::vector<std::string> TraitSystem::getAttackStates(RPGActor *actor) {
    std::vector<std::string> out;
    if (!actor) return out;
    for (const auto &inst : actor->traits()->active) {
        const TraitDefinition *def = findDef(inst.traitId);
        if (!def) continue;
        for (const auto &spec : def->traits) {
            if (spec.kind == "attackState" && !spec.target.empty()) {
                if (std::find(out.begin(), out.end(), spec.target) == out.end()) out.push_back(spec.target);
            }
        }
    }
    return out;
}

double TraitSystem::getAttackSpeed(RPGActor *actor) {
    if (!actor) return 0.0;
    double sum = 0.0;
    for (const auto &inst : actor->traits()->active) {
        const TraitDefinition *def = findDef(inst.traitId);
        if (!def) continue;
        for (const auto &spec : def->traits) {
            if (spec.kind == "attackSpeed") sum += spec.value;
        }
    }
    return sum;
}

int TraitSystem::getAttackTimesAdd(RPGActor *actor) {
    if (!actor) return 0;
    int sum = 0;
    for (const auto &inst : actor->traits()->active) {
        const TraitDefinition *def = findDef(inst.traitId);
        if (!def) continue;
        for (const auto &spec : def->traits) {
            if (spec.kind == "attackTimes") sum += static_cast<int>(spec.value);
        }
    }
    return sum;
}

}  // namespace eve::rpg
