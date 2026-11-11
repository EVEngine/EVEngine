/** @file RTSRootState.cpp
 * @brief rts module-interface: rts.roots v2 portable data face for native v3 roots.
 * @remarks No ECS, services, policy evaluation or event replay. Other game participants remain external.
 */
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "rts/RTSOrderState.h"
#include "rts/RTSRootStateCodecInternal.h"
namespace eve::rts::root_state_codec {
namespace {
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message) {
    throw Invalid{Status::failure(Diagnostic::error(DiagnosticCode::ParseError, message, "rts.roots"))};
}
template <class T>
T take(Result<T> result) {
    if (!result) throw Invalid{result.status()};
    return std::move(result).takeValue();
}
const Value::Array& array(const Value& value) {
    const auto* result = value.getIf<Value::Array>();
    if (!result || result->size() > 1048576) invalid("Invalid RTS root array");
    return *result;
}
const std::string& text(const Value& value) {
    const auto* result = value.getIf<std::string>();
    if (!result) invalid("Expected RTS root text");
    return *result;
}
template <class T>
T integerText(const Value& value) {
    const auto& s = text(value);
    T           n{};
    const auto  p = std::from_chars(s.data(), s.data() + s.size(), n);
    if (p.ec != std::errc{} || p.ptr != s.data() + s.size() || std::to_string(n) != s)
        invalid("Invalid RTS root integer");
    return n;
}
template <class T>
struct Vector : std::false_type {};
template <class T, class A>
struct Vector<std::vector<T, A>> : std::true_type {};
template <class T>
struct Optional : std::false_type {};
template <class T>
struct Optional<std::optional<T>> : std::true_type {};
template <class T>
struct Map : std::false_type {};
template <class T, class C, class A>
struct Map<std::map<std::string, T, C, A>> : std::true_type {};
static_assert(int(CombatStance::Passive) == 0 && int(CombatStance::Defensive) == 1 &&
              int(CombatStance::Aggressive) == 2);
static_assert(int(AbilityTarget::Self) == 0 && int(AbilityTarget::Ally) == 1 && int(AbilityTarget::Enemy) == 2 &&
              int(AbilityTarget::Point) == 3);
static_assert(int(RTSEffectKind::Morale) == 0 && int(RTSEffectKind::Suppression) == 1 &&
              int(RTSEffectKind::ProductionLock) == 2);
static_assert(int(VictoryRule::Annihilation) == 0 && int(VictoryRule::DestroyHeadquarters) == 1 &&
              int(VictoryRule::ResourceTarget) == 2);
static_assert(int(MatchPhase::Setup) == 0 && int(MatchPhase::Running) == 1 && int(MatchPhase::Finished) == 2);
template <class T>
constexpr int enumMaximum() {
    if constexpr (std::is_same_v<T, CombatStance> || std::is_same_v<T, RTSEffectKind> ||
                  std::is_same_v<T, MatchPhase> || std::is_same_v<T, effects::StackCountPolicy> ||
                  std::is_same_v<T, effects::DurationPolicy> || std::is_same_v<T, effects::OverflowPolicy>)
        return 2;
    else if constexpr (std::is_same_v<T, AbilityTarget> || std::is_same_v<T, effects::StackMode> ||
                       std::is_same_v<T, effects::MagnitudePolicy>)
        return 3;
    else if constexpr (std::is_same_v<T, MissionObjectiveStatus>)
        return 2;
    else if constexpr (std::is_same_v<T, MissionObjectiveKind>)
        return 6;
    else if constexpr (std::is_same_v<T, MissionTriggerEventKind>)
        return 9;
    else if constexpr (std::is_same_v<T, MissionVariableCompare>)
        return 5;
    else if constexpr (std::is_same_v<T, MissionActionKind>)
        return 10;
    else if constexpr (std::is_same_v<T, VictoryRule>)
        return 4;
    else if constexpr (std::is_same_v<T, OrderKind>)
        return 17;
    else if constexpr (std::is_same_v<T, attributes::AttributeOperation>)
        return 6;
    else
        static_assert(!sizeof(T), "Undeclared RTS root wire enum");
}
Value production(const std::string& json) {
    production::WorkQueue queue;
    auto                  restored = queue.restore(json);
    if (!restored) throw Invalid{restored.status()};
    return take(Value::fromJson(take(queue.snapshot())));
}
template <class T>
Value encode(const T& v) {
    if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, bool> || std::is_same_v<T, int>)
        return Value(v);
    else if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(v)) invalid("Nonfinite RTS root number");
        return Value(double(v));
    } else if constexpr (std::is_integral_v<T>)
        return std::to_string(v);
    else if constexpr (std::is_enum_v<T>) {
        if (int(v) < 0 || int(v) > enumMaximum<T>()) invalid("Unknown RTS root enum");
        return int(v);
    } else if constexpr (std::is_same_v<T, SubjectRef> || std::is_same_v<T, LogicalId>)
        return v.format();
    else if constexpr (std::is_same_v<T, Duration>)
        return std::to_string(v.nanoseconds());
    else if constexpr (std::is_same_v<T, Value>) {
        take(v.toJson());
        return v;
    } else if constexpr (std::is_same_v<T, Revision> || std::is_same_v<T, SimulationTick>)
        return std::to_string(v.value());
    else if constexpr (std::is_same_v<T, ecs::EntityHandle>) {
        if (v.table || v.id || v.generation || v.type != typeid(void)) invalid("Native handle in portable RTS root");
        return Value{};
    } else if constexpr (std::is_same_v<T, PlacementLink>)
        return v.key();
    else if constexpr (std::is_same_v<T, Value::Object>) {
        Value result(v);
        auto  json = result.toJson();
        if (!json) throw Invalid{json.status()};
        return result;
    } else if constexpr (std::is_same_v<T, RTSEffectSnapshot> || std::is_same_v<T, RTSProjectileSystemSnapshot>)
        return take(v.encodeState());
    else if constexpr (Vector<T>::value) {
        if (v.size() > 1048576) invalid("Too many RTS root values");
        if constexpr (std::is_same_v<typename T::value_type, ecs::EntityHandle>)
            if (!v.empty()) invalid("Native handle vector in RTS root");
        Value::Array a;
        for (const auto& item : v) a.push_back(encode(item));
        return a;
    } else if constexpr (Optional<T>::value)
        return v ? encode(*v) : Value{};
    else if constexpr (Map<T>::value) {
        if (v.size() > 1048576) invalid("Too many RTS root map values");
        Value::Object o;
        for (const auto& [k, x] : v) o.emplace(k, encode(x));
        return o;
    } else {
        if constexpr (std::is_same_v<T, Building::Placement>)
            if (v.link.isBound()) invalid("External placement provider binding is not a root fact");
        if constexpr (std::is_same_v<T, Unit::Navigation>)
            if (v.formationSpeedFactor != 1 || v.formationTarget || v.combatSlotTarget || v.combatSlotWaiting ||
                v.trafficRecoveryTarget)
                invalid("Derived navigation facts must be captured through native snapshot");
        Value::Array a;
        std::apply([&](const auto&... x) { (a.push_back(encode(x)), ...); }, fields(v));
        if constexpr (std::is_same_v<T, RTSUnitSnapshot> || std::is_same_v<T, RTSBuildingSnapshot>)
            a.push_back(take(RTSOrderState{v.orders, v.orderTargets}.encodeState()));
        if constexpr (std::is_same_v<T, RTSBuildingSnapshot>) a.push_back(production(v.productionJson));
        return a;
    }
}
template <class T>
void decode(const Value& value, T& out) {
    if constexpr (std::is_same_v<T, std::string>)
        out = text(value);
    else if constexpr (std::is_same_v<T, bool>) {
        const auto* b = value.getIf<bool>();
        if (!b) invalid("Expected RTS root boolean");
        out = *b;
    } else if constexpr (std::is_same_v<T, int>) {
        if (!value.isInt64() || value.asInt() < std::numeric_limits<int>::min() ||
            value.asInt() > std::numeric_limits<int>::max())
            invalid("RTS root integer out of range");
        out = int(value.asInt());
    } else if constexpr (std::is_floating_point_v<T>) {
        if (!value.isDouble() && !value.isInt64()) invalid("Expected RTS root number");
        const auto n = value.isDouble() ? value.asDouble() : double(value.asInt());
        if (!std::isfinite(n) || std::abs(n) > std::numeric_limits<T>::max()) invalid("RTS root number out of range");
        out = T(n);
    } else if constexpr (std::is_integral_v<T>)
        out = integerText<T>(value);
    else if constexpr (std::is_enum_v<T>) {
        if (!value.isInt64() || value.asInt() < 0 || value.asInt() > enumMaximum<T>()) invalid("Unknown RTS root enum");
        out = T(value.asInt());
    } else if constexpr (std::is_same_v<T, SubjectRef>) {
        const auto& s  = text(value);
        auto        id = PersistentId::parse(s);
        if (!id || id->format() != s) invalid("Invalid RTS root subject");
        out = SubjectRef::fromPersistentId(*id);
    } else if constexpr (std::is_same_v<T, LogicalId>) {
        const auto& s = text(value);
        if (s.empty()) {
            out = {};
            return;
        }
        auto id = LogicalId::parse(s);
        if (!id || id->format() != s) invalid("Invalid RTS root definition");
        out = *id;
    } else if constexpr (std::is_same_v<T, Duration>)
        out = Duration::fromNanoseconds(integerText<std::int64_t>(value));
    else if constexpr (std::is_same_v<T, Value>) {
        take(value.toJson());
        out = value;
    } else if constexpr (std::is_same_v<T, Revision>)
        out = Revision(integerText<std::uint64_t>(value));
    else if constexpr (std::is_same_v<T, SimulationTick>)
        out = SimulationTick(integerText<std::uint64_t>(value));
    else if constexpr (std::is_same_v<T, ecs::EntityHandle>) {
        if (!value.isNull()) invalid("Native handle in RTS file");
        out = {};
    } else if constexpr (std::is_same_v<T, PlacementLink>)
        out = PlacementLink::fromKeyForRestore(text(value));
    else if constexpr (std::is_same_v<T, Value::Object>) {
        const auto* object = value.getIf<Value::Object>();
        if (!object) invalid("Expected RTS definition object");
        auto json = value.toJson();
        if (!json) throw Invalid{json.status()};
        out = *object;
    } else if constexpr (std::is_same_v<T, RTSEffectSnapshot>)
        out = take(RTSEffectSnapshot::prepareState(value));
    else if constexpr (std::is_same_v<T, RTSProjectileSystemSnapshot>)
        out = take(RTSProjectileSystemSnapshot::prepareState(value));
    else if constexpr (Vector<T>::value) {
        const auto& a = array(value);
        if constexpr (std::is_same_v<typename T::value_type, ecs::EntityHandle>)
            if (!a.empty()) invalid("Native handle vector in RTS file");
        for (const auto& item : a) {
            typename T::value_type x;
            decode(item, x);
            out.push_back(std::move(x));
        }
    } else if constexpr (Optional<T>::value) {
        if (!value.isNull()) {
            out.emplace();
            decode(value, *out);
        }
    } else if constexpr (Map<T>::value) {
        const auto* object = value.getIf<Value::Object>();
        if (!object || object->size() > 1048576) invalid("Invalid RTS root map");
        for (const auto& [k, v] : *object) {
            typename T::mapped_type x;
            decode(v, x);
            out.emplace(k, std::move(x));
        }
    } else {
        auto                  f     = fields(out);
        constexpr auto        count = std::tuple_size_v<decltype(f)>;
        constexpr std::size_t extra =
            std::is_same_v<T, RTSUnitSnapshot> ? 1 : (std::is_same_v<T, RTSBuildingSnapshot> ? 2 : 0);
        const auto& a = array(value);
        if (a.size() != count + extra) invalid("Unknown or missing RTS root tuple fields");
        std::size_t i = 0;
        std::apply([&](auto&... x) { (decode(a[i++], x), ...); }, f);
        if constexpr (std::is_same_v<T, RTSUnitSnapshot> || std::is_same_v<T, RTSBuildingSnapshot>) {
            auto orders      = take(RTSOrderState::prepareState(a[i++]));
            out.orders       = std::move(orders.orders);
            out.orderTargets = std::move(orders.targets);
        }
        if constexpr (std::is_same_v<T, RTSBuildingSnapshot>)
            out.productionJson = take(production(take(a[i].toJson())).toJson());
    }
}
// This is the CPU relationship admission boundary. Native materialization still validates real providers/owners.
void validate(const RTSStateSnapshot& state) {
    if (state.version != 3) invalid("Portable root protocol requires native snapshot v3");
    enum Kind { UnitKind, BuildingKind, NodeKind, PlayerKind, FactionKind, MatchKind };
    std::unordered_map<SubjectRef, Kind> roots;
    auto                                 add = [&](const auto& values, Kind kind) {
        for (const auto& v : values)
            if (!v.subject.isValid() || !roots.emplace(v.subject, kind).second)
                invalid("Invalid or duplicated RTS root identity");
    };
    add(state.units, UnitKind);
    add(state.buildings, BuildingKind);
    add(state.resourceNodes, NodeKind);
    add(state.players, PlayerKind);
    add(state.factions, FactionKind);
    add(state.matches, MatchKind);
    auto ref = [&](SubjectRef id, Kind kind, bool optional = true) {
        if (!id.isValid() && optional) return;
        auto found = roots.find(id);
        if (found == roots.end() || found->second != kind) invalid("RTS root reference has absent or wrong family");
    };
    auto actor = [&](SubjectRef id) {
        if (!id.isValid()) return;
        auto found = roots.find(id);
        if (found == roots.end() || (found->second != UnitKind && found->second != BuildingKind))
            invalid("Absent RTS actor target");
    };
    auto refs = [&](const auto& ids, Kind kind) {
        std::unordered_set<SubjectRef> seen;
        for (auto id : ids) {
            ref(id, kind, false);
            if (!seen.insert(id).second) invalid("Duplicated RTS membership");
        }
    };
    auto targets = [&](const auto& values) {
        for (const auto& [id, target] : values) {
            (void)id;
            if (!target.isValid() || !roots.contains(target)) invalid("Absent RTS command target");
        }
    };
    auto tags = [](const auto& values) {
        if (!std::is_sorted(values.begin(), values.end()) ||
            std::adjacent_find(values.begin(), values.end()) != values.end() ||
            std::any_of(values.begin(), values.end(), [](const auto& s) { return s.empty(); }))
            invalid("Noncanonical RTS tags");
    };
    for (const auto& u : state.units) {
        tags(u.tags);
        ref(u.faction, FactionKind);
        ref(u.worker.resourceNode, NodeKind);
        ref(u.worker.dropoff, BuildingKind);
        actor(u.combatTarget);
        actor(u.commandSource);
        actor(u.commandUplink);
        actor(u.abilityTarget);
        actor(u.container);
        refs(u.occupants, UnitKind);
        ref(u.supplyTarget, UnitKind);
        ref(u.convoyLeader, UnitKind);
        ref(u.fireSupportRequester, UnitKind);
        actor(u.observedFireSpotter);
        actor(u.escortTarget);
        targets(u.orderTargets);
        // Intel/escort-intercept observations may refer to retired enemies and are intentionally not live links.
        if (u.navigation.waypointIndex > u.navigation.waypoints.size() || u.motion.speed < 0 ||
            u.motion.arrivalRadius < 0)
            invalid("Invalid RTS motion or navigation cursor");
        auto valid = u.durability.state.validate();
        if (!valid) throw Invalid{valid.status()};
        if (u.durability.state.subject != u.subject) invalid("RTS durability owner differs");
        std::set<std::string> bases, modifiers;
        for (const auto& b : u.attributes.bases)
            if (b.attribute.empty() || !bases.insert(b.attribute).second) invalid("Invalid selected attribute base");
        for (const auto& m : u.attributes.modifiers)
            if (m.id.empty() || m.attribute.empty() || !bases.contains(m.attribute) || !modifiers.insert(m.id).second ||
                (m.operation == attributes::AttributeOperation::Custom && m.policyId.empty()))
                invalid("Invalid selected attribute modifier");
        if (!u.abilities.channel && u.abilityTarget.isValid()) invalid("Ability target lacks a channel");
        if (u.abilities.channel) {
            auto valid = u.abilities.channel->spec.id.size();
            if (!valid) invalid("Active ability lacks definition");
        }
    }
    for (const auto& b : state.buildings) {
        tags(b.tags);
        ref(b.faction, FactionKind);
        refs(b.builders, UnitKind);
        ref(b.capturingFaction, FactionKind);
        if (b.rallyCommandTarget.isValid() && !roots.contains(b.rallyCommandTarget))
            invalid("Absent rally command target");
        ref(b.rallyTransport, UnitKind);
        refs(b.reinforcements, UnitKind);
        actor(b.combatTarget);
        ref(b.airDefenseNetworkRoot, BuildingKind);
        refs(b.occupants, UnitKind);
        targets(b.orderTargets);
        auto valid = b.integrity.state.validate();
        if (!valid) throw Invalid{valid.status()};
        if (b.integrity.state.subject != b.subject) invalid("RTS integrity owner differs");
        auto command = b.rally.command.validate();
        if (!command) throw Invalid{command.status()};
    }
    for (const auto& n : state.resourceNodes) {
        refs(n.workers, UnitKind);
        if (n.stock.remaining < 0 || n.stock.maximum < 0 || n.position.radius < 0)
            invalid("Invalid resource node quantities");
    }
    for (const auto& p : state.players) {
        refs(p.units, UnitKind);
        refs(p.buildings, BuildingKind);
    }
    for (const auto& f : state.factions) {
        refs(f.units, UnitKind);
        refs(f.buildings, BuildingKind);
    }
    for (const auto& m : state.matches) {
        std::unordered_set<SubjectRef> members;
        for (const auto& p : m.participants) {
            ref(p.faction, FactionKind, false);
            if (!members.insert(p.faction).second) invalid("Repeated match participant");
        }
        std::uint64_t previous = 0;
        for (const auto& e : m.events.values) {
            if (!e.sequence || e.sequence <= previous || e.sequence > m.state.updateSequence || e.kind.empty())
                invalid("Invalid match event sequence");
            previous = e.sequence;
        }
    }
    for (const auto& p : state.projectiles.payloads) {
        ref(p.faction, FactionKind, false);
        actor(p.target);
        actor(p.observer);
        actor(p.trajectoryTarget);
    }
    std::unordered_set<SubjectRef> grouped;
    for (const auto& g : state.movementGroups) {
        if (g.members.size() < 2 || !std::isfinite(g.leadDistance) || g.leadDistance <= 0)
            invalid("Invalid movement group");
        for (const auto& member : g.members) {
            ref(member.subject, UnitKind, false);
            if (!grouped.insert(member.subject).second) invalid("Repeated movement group member");
            const auto it = std::find_if(state.units.begin(), state.units.end(),
                                         [&](const auto& u) { return u.subject == member.subject; });
            if (!it->durability.alive || it->container.isValid()) invalid("Unavailable movement group member");
            OrderComponent validator;
            auto           restored = validator.restoreState(it->orders);
            if (!restored) throw Invalid{restored.status()};
            auto current = take(validator.current());
            if (current.id != member.orderId ||
                (current.kind != OrderKind::Move && current.kind != OrderKind::AttackMove))
                invalid("Movement group lacks active move order");
        }
    }
}
}  // namespace
}  // namespace eve::rts::root_state_codec
namespace eve::rts {
Result<Value> RTSStateSnapshot::encodeState() const {
    using namespace root_state_codec;
    try {
        validate(*this);
        return Result<Value>::success(Value::object({{"schema", "rts.roots"},
                                                     {"version", 2},
                                                     {"snapshotVersion", 3},
                                                     {"units", encode(units)},
                                                     {"buildings", encode(buildings)},
                                                     {"resourceNodes", encode(resourceNodes)},
                                                     {"players", encode(players)},
                                                     {"factions", encode(factions)},
                                                     {"matches", encode(matches)},
                                                     {"movementGroups", encode(movementGroups)},
                                                     {"projectiles", encode(projectiles)}}));
    } catch (const Invalid& e) {
        return Result<Value>::failure(e.status);
    }
}
Result<RTSStateSnapshot> RTSStateSnapshot::prepareState(const Value& value) {
    using namespace root_state_codec;
    try {
        const auto* o = value.getIf<Value::Object>();
        if (!o || o->size() != 11) invalid("Unknown or missing RTS root fields");
        for (const auto* key : {"schema", "version", "snapshotVersion", "units", "buildings", "resourceNodes",
                                "players", "factions", "matches", "movementGroups", "projectiles"})
            if (!o->contains(key)) invalid("Missing RTS root field");
        if (text(o->at("schema")) != "rts.roots" || !o->at("version").isInt64() ||
            (o->at("version").asInt() != 1 && o->at("version").asInt() != 2) || !o->at("snapshotVersion").isInt64() ||
            o->at("snapshotVersion").asInt() != 3)
            invalid("Unsupported RTS root schema or version");
        RTSStateSnapshot state;
        decode(o->at("units"), state.units);
        decode(o->at("buildings"), state.buildings);
        decode(o->at("resourceNodes"), state.resourceNodes);
        decode(o->at("players"), state.players);
        decode(o->at("factions"), state.factions);
        if (o->at("version").asInt() == 1) {
            // Migrate only the historical three-field Rules/State and five-field Match rows.
            // Copy the candidate first; failures cannot alter the caller's archive.
            Value migrated = o->at("matches");
            auto* matches  = migrated.getIf<Value::Array>();
            if (!matches) invalid("Invalid legacy match rows");
            for (auto& match : *matches) {
                auto* row = match.getIf<Value::Array>();
                if (!row || row->size() != 5) invalid("Invalid legacy match row");
                for (std::size_t index : {std::size_t(1), std::size_t(3)}) {
                    auto* fields = (*row)[index].getIf<Value::Array>();
                    if (!fields || fields->size() != 3) invalid("Invalid legacy match clock");
                    fields->emplace_back("0");
                    fields->emplace_back(false);
                }
                row->push_back(encode(Match::Scores{}));
                row->push_back(encode(Match::Objectives{}));
                row->push_back(encode(Match::Campaign{}));
            }
            decode(migrated, state.matches);
        } else {
            decode(o->at("matches"), state.matches);
        }
        decode(o->at("movementGroups"), state.movementGroups);
        decode(o->at("projectiles"), state.projectiles);
        validate(state);
        return Result<RTSStateSnapshot>::success(std::move(state));
    } catch (const Invalid& e) {
        return Result<RTSStateSnapshot>::failure(e.status);
    }
}
}  // namespace eve::rts
