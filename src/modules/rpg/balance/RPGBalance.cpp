#include "rpg/balance/RPGBalance.h"

#include "agent/Balance.h"
#include "rpg/AttributeSystem.h"
#include "rpg/Battle.h"
#include "rpg/BattleSystem.h"
#include "rpg/Class.h"
#include "rpg/ClassSystem.h"
#include "rpg/Effect.h"
#include "rpg/EquipmentSystem.h"
#include "rpg/RPGActor.h"
#include "rpg/SimulationEvents.h"
#include "rpg/Skill.h"
#include "rpg/SkillSystem.h"
#include "rpg/StatusSystem.h"
#include "rpg/VitalsSystem.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace eve::rpg::balance {
namespace {
struct Invalid : std::runtime_error {
    using std::runtime_error::runtime_error;
};
using Object = Value::Object;
const Object& object(const Value& v, std::initializer_list<std::string_view> fields) {
    const auto* o = v.getIf<Object>();
    if (!o) throw Invalid("Expected object");
    for (const auto& [key, value] : *o)
        if (std::find(fields.begin(), fields.end(), key) == fields.end()) throw Invalid("Unknown suite field: " + key);
    return *o;
}
const Value& required(const Object& o, const char* key) {
    auto i = o.find(key);
    if (i == o.end()) throw Invalid(std::string("Missing field: ") + key);
    return i->second;
}
std::string text(const Value& v) {
    const auto* s = v.getIf<std::string>();
    if (!s || s->empty() || s->size() > 128) throw Invalid("Expected nonempty string up to 128 bytes");
    return *s;
}
double number(const Value& v) {
    double n;
    if (const auto* x = v.getIf<double>())
        n = *x;
    else if (const auto* x = v.getIf<std::int64_t>())
        n = double(*x);
    else
        throw Invalid("Expected number");
    if (!std::isfinite(n) || std::abs(n) > 1e6) throw Invalid("Nonfinite or oversized number");
    return n;
}
double optionalNumber(const Object& o, const char* key, double fallback) {
    auto i = o.find(key);
    return i == o.end() ? fallback : number(i->second);
}
const Value::Array& array(const Value& v, std::size_t limit) {
    const auto* a = v.getIf<Value::Array>();
    if (!a || a->empty() || a->size() > limit) throw Invalid("Invalid or oversized array");
    return *a;
}
std::vector<std::string> strings(const Value& v, std::size_t limit) {
    std::vector<std::string> result;
    std::set<std::string>    seen;
    for (const auto& x : array(v, limit)) {
        auto s = text(x);
        if (!seen.insert(s).second) throw Invalid("Duplicate string: " + s);
        result.push_back(std::move(s));
    }
    return result;
}
std::vector<std::uint64_t> readSeeds(const Value& v) {
    std::vector<std::uint64_t> result;
    std::set<std::uint64_t>    seen;
    for (const auto& item : array(v, 16)) {
        std::uint64_t seed = 0;
        if (const auto* n = item.getIf<std::int64_t>()) {
            if (*n < 0) throw Invalid("Negative seed");
            seed = std::uint64_t(*n);
        } else {
            auto s      = text(item);
            auto parsed = std::from_chars(s.data(), s.data() + s.size(), seed);
            if (parsed.ec != std::errc{} || parsed.ptr != s.data() + s.size()) throw Invalid("Invalid seed");
        }
        if (!seen.insert(seed).second) throw Invalid("Duplicate seed");
        result.push_back(seed);
    }
    return result;
}
Diagnostic diagnostic(std::string message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), {}, {}, "rpg.balance");
}
struct Gear {
    std::string                slot;
    std::vector<EquipmentStat> stats;
};
struct Build {
    std::string                   id, classId;
    std::map<std::string, double> attributes;
    std::vector<std::string>      skills;
    std::vector<Gear>             gear;
};
struct Strategy {
    std::string              id;
    std::vector<std::string> priority;
    double                   healBelow = 0.5, reserveMana = 0;
};
struct Scenario {
    std::string id;
    double      healthScale = 1, manaScale = 1, manaRegen = 0;
};
struct Binding {
    std::string parameter, build, attribute, slot;
};
struct Edge {
    std::size_t            left = 0, right = 0;
    agent::balance::Target target;
};
struct Config {
    Value                 source;
    std::vector<Build>    builds;
    std::vector<Strategy> strategies;
    std::vector<Scenario> scenarios;
    std::vector<Binding>  bindings;
    std::vector<Edge>     edges;
    std::set<std::string> parameters;
};
std::size_t buildIndex(const Config& c, const std::string& id) {
    auto it = std::find_if(c.builds.begin(), c.builds.end(), [&](const Build& b) { return b.id == id; });
    if (it == c.builds.end()) throw Invalid("Unknown build: " + id);
    return std::size_t(it - c.builds.begin());
}
void validateSkills(const Config& c) {
    for (const auto& b : c.builds) {
        if (!ClassRegistry::find(b.classId)) throw Invalid("Unknown class: " + b.classId);
        for (const auto& id : b.skills) {
            const auto* skill = SkillRegistry::find(id);
            if (!skill) throw Invalid("Unknown skill: " + id);
            if (skill->castTime != 0) throw Invalid("Turn-based balance requires instant skills: " + id);
            if (skill->targetType != "self" && skill->targetType != "enemySingle" && skill->targetType != "single" &&
                skill->targetType != "allySingle")
                throw Invalid("Unsupported duel target type: " + id);
            for (const auto& effect : skill->grantedEffects)
                if (!EffectRegistry::find(effect)) throw Invalid("Unknown granted effect: " + effect);
            const auto* damage = BattleSystem::findSkillDamage(id);
            if (!damage && skill->grantedEffects.empty()) throw Invalid("Skill has no damage or effects: " + id);
        }
    }
}
Config decode(const Value& value) {
    const auto& root =
        object(value, {"schema", "version", "builds", "strategies", "scenarios", "bindings", "matchups"});
    if (text(required(root, "schema")) != "evengine.rpg.balance.suite" || number(required(root, "version")) != 1)
        throw Invalid("Unsupported RPG balance schema/version");
    Config c;
    c.source = value;
    std::set<std::string> ids;
    for (const auto& v : array(required(root, "builds"), 8)) {
        const auto& o = object(v, {"id", "classId", "attributes", "skills", "equipment"});
        Build       b;
        b.id = text(required(o, "id"));
        if (b.id.find('>') != std::string::npos || !ids.insert(b.id).second)
            throw Invalid("Invalid/duplicate build id");
        b.classId         = text(required(o, "classId"));
        const auto* attrs = required(o, "attributes").getIf<Object>();
        if (!attrs || attrs->size() > 32) throw Invalid("Invalid attributes");
        for (const auto& [name, n] : *attrs) {
            if (name.empty() || name.size() > 128) throw Invalid("Invalid attribute name");
            b.attributes.emplace(name, number(n));
        }
        if (!b.attributes.contains("hp") || b.attributes.at("hp") <= 0 || !b.attributes.contains("mana") ||
            b.attributes.at("mana") < 0)
            throw Invalid("Build requires positive hp and nonnegative mana");
        b.skills = strings(required(o, "skills"), 16);
        if (auto it = o.find("equipment"); it != o.end()) {
            std::set<std::string> slots;
            for (const auto& item : array(it->second, 8)) {
                const auto& g = object(item, {"slot", "stats"});
                Gear        gear;
                gear.slot = text(required(g, "slot"));
                if (!slots.insert(gear.slot).second) throw Invalid("Duplicate equipment slot");
                for (const auto& itemStat : array(required(g, "stats"), 16)) {
                    const auto&   s = object(itemStat, {"attribute", "op", "value"});
                    EquipmentStat stat{text(required(s, "attribute")), text(required(s, "op")),
                                       number(required(s, "value")), 0};
                    auto          parsed = attributes::parseAttributeOperation(stat.op, stat.value);
                    if (!parsed) throw Invalid("Unknown equipment operation: " + stat.op);
                    gear.stats.push_back(std::move(stat));
                }
                b.gear.push_back(std::move(gear));
            }
        }
        c.builds.push_back(std::move(b));
    }
    if (c.builds.size() < 2) throw Invalid("At least two builds required");
    ids.clear();
    for (const auto& v : array(required(root, "strategies"), 4)) {
        const auto& o = object(v, {"id", "priority", "healBelow", "reserveMana"});
        Strategy s{text(required(o, "id")), strings(required(o, "priority"), 16), optionalNumber(o, "healBelow", 0.5),
                   optionalNumber(o, "reserveMana", 0)};
        if (!ids.insert(s.id).second || s.healBelow < 0 || s.healBelow > 1 || s.reserveMana < 0 || s.reserveMana > 1)
            throw Invalid("Invalid strategy");
        c.strategies.push_back(std::move(s));
    }
    ids.clear();
    for (const auto& v : array(required(root, "scenarios"), 4)) {
        const auto& o = object(v, {"id", "healthScale", "manaScale", "manaRegen"});
        Scenario    s{text(required(o, "id")), optionalNumber(o, "healthScale", 1), optionalNumber(o, "manaScale", 1),
                      optionalNumber(o, "manaRegen", 0)};
        if (!ids.insert(s.id).second || s.healthScale <= 0 || s.healthScale > 10 || s.manaScale < 0 ||
            s.manaScale > 10 || s.manaRegen < 0 || s.manaRegen > 1000)
            throw Invalid("Invalid scenario");
        c.scenarios.push_back(std::move(s));
    }
    for (const auto& v : array(required(root, "bindings"), 64)) {
        const auto& o = object(v, {"parameter", "build", "attribute", "slot"});
        Binding     b{text(required(o, "parameter")), text(required(o, "build")), text(required(o, "attribute")), {}};
        auto&       build = c.builds[buildIndex(c, b.build)];
        if (auto it = o.find("slot"); it != o.end()) b.slot = text(it->second);
        if (b.slot.empty()) {
            if (!build.attributes.contains(b.attribute) || b.attribute == "hp" || b.attribute == "mana")
                throw Invalid("Binding must name a declared non-resource attribute");
        } else {
            auto gear =
                std::find_if(build.gear.begin(), build.gear.end(), [&](const Gear& g) { return g.slot == b.slot; });
            if (gear == build.gear.end() ||
                std::count_if(gear->stats.begin(), gear->stats.end(),
                              [&](const EquipmentStat& s) { return s.attribute == b.attribute; }) != 1)
                throw Invalid("Gear binding must name exactly one stat in its slot");
        }
        for (const auto& old : c.bindings)
            if (old.build == b.build && old.attribute == b.attribute && old.slot == b.slot)
                throw Invalid("Two parameters cannot own the same attribute binding");
        c.parameters.insert(b.parameter);
        c.bindings.push_back(std::move(b));
    }
    ids.clear();
    for (const auto& v : array(required(root, "matchups"), 64)) {
        const auto& o    = object(v, {"left", "right", "min", "max", "weight"});
        const auto  left = text(required(o, "left")), right = text(required(o, "right"));
        const auto  name = left + ">" + right;
        Edge edge{buildIndex(c, left),
                  buildIndex(c, right),
                  {name, number(required(o, "min")), number(required(o, "max")), 1, optionalNumber(o, "weight", 1)}};
        if (edge.left == edge.right || !ids.insert(name).second || edge.target.minimum < 0 || edge.target.maximum > 1 ||
            edge.target.minimum > edge.target.maximum || edge.target.weight <= 0)
            throw Invalid("Invalid directed matchup target");
        for (const auto& old : c.edges)
            if (old.left == edge.right && old.right == edge.left &&
                (old.target.minimum > 1 - edge.target.minimum || old.target.maximum < 1 - edge.target.maximum))
                throw Invalid("Contradictory reverse matchup targets");
        c.edges.push_back(std::move(edge));
    }
    const auto cells =
        c.builds.size() * (c.builds.size() - 1) / 2 * c.strategies.size() * c.strategies.size() * c.scenarios.size();
    if (cells > 128) throw Invalid("Suite exceeds 128 strategy/scenario matchup cells");
    validateSkills(c);
    return c;
}
std::string choose(RPGActor& actor, const Build& build, const Strategy& strategy, double capacity) {
    const double ratio = VitalsSystem::getCurrent(&actor, "hp") / VitalsSystem::getMax(&actor, "hp");
    for (const auto& tag : strategy.priority)
        for (const auto& id : build.skills) {
            const auto* skill = SkillRegistry::find(id);
            if (!skill || !skill->hasTag(tag) || !SkillSystem::canCast(&actor, id)) continue;
            const auto* damage  = BattleSystem::findSkillDamage(id);
            const bool  healing = damage && damage->damageType == "hpHeal";
            if (healing && ratio >= strategy.healBelow) continue;
            if (!healing && skill->cost && AttributeSystem::getFinal(&actor, "mana") < capacity * strategy.reserveMana)
                continue;
            return id;
        }
    return {};  // An explicitly modeled basic attack when no prioritized skill is legal.
}
struct Bout {
    int                        winner = -1, rounds = 0;
    Value::Array               trace;
    std::map<std::string, int> skills;
    double                     mana = 0;
};
Result<Bout> fight(const Build& left, const Build& right, const Strategy& ls, const Strategy& rs,
                   const Scenario& scenario, std::uint64_t seed, bool reverse, double dt, int limit, bool evidence) {
    ecs::Table                   world;
    ecs::ScopedTable             selected(world);
    detail::SimulationEvents     events;
    detail::SimulationEventScope eventScope(events);
    // Components are all materialized before rounds; no ECS System or structural mutation while iterating.
    auto*      a       = RPGActor::createActor();
    auto*      b       = RPGActor::createActor();
    const auto prepare = [&](RPGActor& actor, const Build& build) -> Result<void> {
        for (const auto& [name, value] : build.attributes) AttributeSystem::setBase(&actor, name, value);
        if (!ClassSystem::setClass(&actor, build.classId))
            return Result<void>::failure(diagnostic("Class assignment failed"));
        for (const auto& gear : build.gear) {
            auto result = EquipmentSystem::projectSlot(actor, gear.slot, gear.stats);
            if (!result) return Result<void>::failure(result.status());
        }
        AttributeSystem::setBase(&actor, "hp", build.attributes.at("hp") * scenario.healthScale);
        AttributeSystem::setBase(&actor, "mana", build.attributes.at("mana") * scenario.manaScale);
        if (AttributeSystem::getFinal(&actor, "hp") <= 0 || AttributeSystem::getFinal(&actor, "mana") < 0)
            return Result<void>::failure(diagnostic("Projected build has invalid resource capacity"));
        for (const auto& id : build.skills) SkillSystem::learn(&actor, id);
        VitalsSystem::revive(&actor, "hp");
        return Result<void>::success();
    };
    auto prepared = prepare(*a, left);
    if (!prepared) return Result<Bout>::failure(prepared.status());
    prepared = prepare(*b, right);
    if (!prepared) return Result<Bout>::failure(prepared.status());
    const double capacityA = AttributeSystem::getFinal(a, "mana"), capacityB = AttributeSystem::getFinal(b, "mana");
    Battle       battle;
    battle.addActor(reverse ? b : a, reverse ? 1 : 0);
    battle.addActor(reverse ? a : b, reverse ? 0 : 1);
    auto seeded = battle.configureSeed(seed);
    if (!seeded) return Result<Bout>::failure(seeded.status());
    Bout result;
    for (int round = 0; round < limit && !battle.isFinished(); ++round) {
        const auto queue = [&](RPGActor& actor, RPGActor& other, const Build& build, const Strategy& policy,
                               double capacity) -> Result<void> {
            auto        id  = choose(actor, build, policy, capacity);
            const auto* def = id.empty() ? nullptr : SkillRegistry::find(id);
            auto* target    = def && (def->targetType == "self" || def->targetType == "allySingle") ? &actor : &other;
            return battle.setActionChecked(&actor, id, target);
        };
        auto queued = reverse ? queue(*b, *a, right, rs, capacityB) : queue(*a, *b, left, ls, capacityA);
        if (!queued) return Result<Bout>::failure(queued.status());
        queued = reverse ? queue(*a, *b, left, ls, capacityA) : queue(*b, *a, right, rs, capacityB);
        if (!queued) return Result<Bout>::failure(queued.status());
        const double beforeMana = AttributeSystem::getFinal(a, "mana") + AttributeSystem::getFinal(b, "mana");
        battle.startRound();
        while (battle.executeNextAction()) {
        }
        result.mana += beforeMana - AttributeSystem::getFinal(a, "mana") - AttributeSystem::getFinal(b, "mana");
        battle.pollEvents();
        for (int i = 0; i < battle.getEventCount(); ++i) {
            const auto e = battle.getEvent(i);
            if (e.action == "settlementFailed")
                return Result<Bout>::failure(diagnostic("Canonical battle settlement failed"));
            if (e.action == "actionStart") ++result.skills[e.skillId.empty() ? "basic" : e.skillId];
            if (evidence && result.trace.size() < 24 && e.action != "roundStart")
                result.trace.push_back(Value::object({{"round", round + 1},
                                                      {"action", e.action},
                                                      {"skill", e.skillId},
                                                      {"source", e.caster == a   ? left.id
                                                                 : e.caster == b ? right.id
                                                                                 : ""},
                                                      {"target", e.target == a   ? left.id
                                                                 : e.target == b ? right.id
                                                                                 : ""},
                                                      {"amount", e.amount}}));
        }
        SkillSystem::update(float(dt));
        auto advanced = StatusSystem::update(dt);
        if (!advanced) return Result<Bout>::failure(advanced.status());
        std::vector<StatusTickEvent> ticks;
        StatusSystem::pollTicks(ticks);
        for (const auto& tick : ticks) {
            auto settled = battle.settleStatusTick(tick, {}, SimulationTick(std::uint64_t(round + 1)));
            if (!settled) return Result<Bout>::failure(settled.status());
            for (const auto& item : settled.value())
                if (!item.result) return Result<Bout>::failure(diagnostic("Periodic settlement failed"));
        }
        battle.pollEvents();
        for (int i = 0; i < battle.getEventCount(); ++i) {
            const auto e = battle.getEvent(i);
            if (e.action == "settlementFailed")
                return Result<Bout>::failure(diagnostic("Canonical periodic settlement failed"));
            if (evidence && result.trace.size() < 24)
                result.trace.push_back(Value::object({{"round", round + 1},
                                                      {"action", e.action},
                                                      {"skill", e.skillId},
                                                      {"source", e.caster == a   ? left.id
                                                                 : e.caster == b ? right.id
                                                                                 : ""},
                                                      {"target", e.target == a   ? left.id
                                                                 : e.target == b ? right.id
                                                                                 : ""},
                                                      {"amount", e.amount}}));
        }
        if (scenario.manaRegen > 0) {
            const auto regenerate = [&](RPGActor* actor, double capacity) {
                const double base  = AttributeSystem::getBase(actor, "mana");
                const double final = AttributeSystem::getFinal(actor, "mana");
                AttributeSystem::setBase(actor, "mana",
                                         base + std::min(dt * scenario.manaRegen, std::max(0.0, capacity - final)));
            };
            regenerate(a, capacityA);
            regenerate(b, capacityB);
        }
        events.casts.clear();
        events.vitals.clear();
        events.changes.clear();
        result.rounds = round + 1;
        if (VitalsSystem::isDead(a, "hp") || VitalsSystem::isDead(b, "hp")) break;
    }
    const bool deadA = VitalsSystem::isDead(a, "hp"), deadB = VitalsSystem::isDead(b, "hp");
    result.winner = deadA == deadB ? -1 : deadA ? 1 : 0;
    return Result<Bout>::success(std::move(result));
}
Value interval(double score, int count) {
    // Wilson interval for bounded match scores (draws contribute half); descriptive, not independent trials proof.
    const double z2     = 3.841458820694124;
    const double center = (score + z2 / (2 * count)) / (1 + z2 / count);
    const double radius =
        std::sqrt(score * (1 - score) / count + z2 / (4 * count * count)) * std::sqrt(z2) / (1 + z2 / count);
    return Value::array({Value(std::max(0.0, center - radius)), Value(std::min(1.0, center + radius))});
}
}  // namespace

struct RPGBalanceSuite::Impl {
    std::optional<Config> config;
    bool                  active = false;
};
RPGBalanceSuite::RPGBalanceSuite() : impl_(std::make_unique<Impl>()) {}
RPGBalanceSuite::~RPGBalanceSuite() = default;

Result<void> RPGBalanceSuite::configure(const Value& input) {
    if (impl_->active)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, "Tournament is active"));
    try {
        auto c        = decode(input);
        impl_->config = std::move(c);
        return Result<void>::success();
    } catch (const Invalid& error) {
        return Result<void>::failure(diagnostic(error.what()));
    }
}
Result<Value> RPGBalanceSuite::targets() const {
    if (!impl_->config) return Result<Value>::failure(diagnostic("Configure suite first"));
    Value::Array targets;
    for (const auto& edge : impl_->config->edges)
        targets.push_back(Value::object({{"name", edge.target.name},
                                         {"min", edge.target.minimum},
                                         {"max", edge.target.maximum},
                                         {"scale", 0.25},
                                         {"weight", edge.target.weight}}));
    return Result<Value>::success(Value(std::move(targets)));
}
Result<Value> RPGBalanceSuite::measure(const Value& parameters, const Value& seedsValue, double dt, int rounds) {
    if (impl_->active) return Result<Value>::failure(Diagnostic::error(DiagnosticCode::Conflict, "Tournament reentry"));
    if (!impl_->config) return Result<Value>::failure(diagnostic("Configure suite first"));
    struct Guard {
        bool& active;
        explicit Guard(bool& a) : active(a) { active = true; }
        ~Guard() { active = false; }
    } guard(impl_->active);
    try {
        const auto& c = *impl_->config;
        validateSkills(c);
        const auto seeds = readSeeds(seedsValue);
        if (!std::isfinite(dt) || dt <= 0 || dt > 60 || rounds < 1 || rounds > 128)
            throw Invalid("Invalid simulation time budget");
        const auto* params = parameters.getIf<Object>();
        if (!params || params->size() != c.parameters.size())
            throw Invalid("Candidate keys must match bindings exactly");
        for (const auto& name : c.parameters) (void)number(required(*params, name.c_str()));
        auto builds = c.builds;
        for (const auto& binding : c.bindings) {
            auto&      b     = builds[buildIndex(c, binding.build)];
            const auto value = number(params->at(binding.parameter));
            if (binding.slot.empty())
                b.attributes[binding.attribute] = value;
            else
                for (auto& g : b.gear)
                    if (g.slot == binding.slot)
                        for (auto& s : g.stats)
                            if (s.attribute == binding.attribute) s.value = value;
        }
        Value::Array                     cells, matrix;
        std::vector<std::vector<double>> scores(builds.size(), std::vector<double>(builds.size(), 0.5));
        int                              matches = 0, draws = 0;
        for (std::size_t l = 0; l < builds.size(); ++l)
            for (std::size_t r = l + 1; r < builds.size(); ++r) {
                double totalScore = 0;
                int    totalCount = 0;
                for (const auto& scenario : c.scenarios)
                    for (const auto& ls : c.strategies)
                        for (const auto& rs : c.strategies) {
                            int                        wins = 0, losses = 0, ties = 0, turns = 0;
                            double                     spent = 0;
                            std::map<std::string, int> usage;
                            Value::Array               trace;
                            for (auto seed : seeds)
                                for (bool reverse : {false, true}) {
                                    auto bout = fight(builds[l], builds[r], ls, rs, scenario, seed, reverse, dt, rounds,
                                                      trace.empty());
                                    if (!bout) return Result<Value>::failure(bout.status());
                                    const auto& b = bout.value();
                                    if (b.winner == 0)
                                        ++wins;
                                    else if (b.winner == 1)
                                        ++losses;
                                    else
                                        ++ties;
                                    turns += b.rounds;
                                    spent += b.mana;
                                    for (const auto& [id, count] : b.skills) usage[id] += count;
                                    if (trace.empty()) trace = b.trace;
                                }
                            const int    count = wins + losses + ties;
                            const double score = (wins + 0.5 * ties) / count;
                            totalScore += wins + 0.5 * ties;
                            totalCount += count;
                            matches += count;
                            draws += ties;
                            Object skills;
                            for (const auto& [name, count] : usage) skills.emplace(name, Value(count));
                            cells.push_back(Value::object({{"left", builds[l].id},
                                                           {"right", builds[r].id},
                                                           {"leftStrategy", ls.id},
                                                           {"rightStrategy", rs.id},
                                                           {"scenario", scenario.id},
                                                           {"wins", wins},
                                                           {"losses", losses},
                                                           {"draws", ties},
                                                           {"score", score},
                                                           {"scoreInterval", interval(score, count)},
                                                           {"meanRounds", double(turns) / count},
                                                           {"meanManaSpent", spent / count},
                                                           {"skills", std::move(skills)},
                                                           {"trace", std::move(trace)},
                                                           {"traceSeed", std::to_string(seeds.front())},
                                                           {"traceReverse", false},
                                                           {"traceEventLimit", 24}}));
                        }
                scores[l][r] = totalScore / totalCount;
                scores[r][l] = 1 - scores[l][r];
            }
        Object metrics;
        for (const auto& edge : c.edges) metrics.emplace(edge.target.name, Value(scores[edge.left][edge.right]));
        Value::Array names;
        for (const auto& build : builds) names.emplace_back(build.id);
        for (const auto& row : scores) {
            Value::Array values;
            for (auto score : row) values.emplace_back(score);
            matrix.emplace_back(std::move(values));
        }
        return Result<Value>::success(
            Value::object({{"schema", "evengine.rpg.balance.report"},
                           {"version", 1},
                           {"suite", c.source},
                           {"parameters", parameters},
                           {"seeds", seedsValue},
                           {"dt", dt},
                           {"rounds", rounds},
                           {"builds", std::move(names)},
                           {"matrix", std::move(matrix)},
                           {"metrics", std::move(metrics)},
                           {"cells", std::move(cells)},
                           {"matches", matches},
                           {"draws", draws},
                           {"intervalNote",
                            "Wilson approximation on paired bounded scores; samples share seeds and are correlated"}}));
    } catch (const Invalid& error) {
        return Result<Value>::failure(diagnostic(error.what()));
    } catch (const std::exception& error) {
        return Result<Value>::failure(
            Diagnostic::error(DiagnosticCode::CallbackFailure, error.what(), {}, {}, "rpg.balance"));
    }
}
}  // namespace eve::rpg::balance
