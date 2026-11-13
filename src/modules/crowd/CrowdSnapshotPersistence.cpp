/** @file CrowdSnapshotPersistence.cpp
 * @brief crowd module-interface: crowd.snapshot v1 complete backend CPU data, no ECS/events/script/tool endpoints.
 * @remarks Owner nonces/pins/schedulers are never persisted. Cold explicit admission, no simulation advancement.
 */
#include <array>
#include <charconv>
#include <map>
#include <set>
#include <tuple>
#include <type_traits>
#include "common/Exception.h"
#include "crowd/CrowdInternal.h"
namespace eve::crowd {
namespace {
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message, DiagnosticCode code = DiagnosticCode::ParseError) {
    throw Invalid{Status::failure(Diagnostic::error(code, message, "crowd.snapshot"))};
}
template <class T>
T take(Result<T> value) {
    if (!value) throw Invalid{value.status()};
    return std::move(value).takeValue();
}
const Value::Array& array(const Value& v) {
    auto* a = v.getIf<Value::Array>();
    if (!a) invalid("Expected snapshot array");
    return *a;
}
const Value::Array& tuple(const Value& v, std::size_t n) {
    const auto& a = array(v);
    if (a.size() != n) invalid("Unknown or missing snapshot tuple fields");
    return a;
}
const std::string& text(const Value& v) {
    auto* s = v.getIf<std::string>();
    if (!s) invalid("Expected snapshot text");
    return *s;
}
std::uint64_t counter(const Value& v) {
    const auto&   s = text(v);
    std::uint64_t n = 0;
    auto          p = std::from_chars(s.data(), s.data() + s.size(), n);
    if (p.ec != std::errc{} || p.ptr != s.data() + s.size() || std::to_string(n) != s)
        invalid("Invalid snapshot counter");
    return n;
}
std::int64_t integer(const Value& v, std::int64_t min, std::int64_t max) {
    if (!v.isInt64() || v.asInt() < min || v.asInt() > max) invalid("Invalid snapshot integer");
    return v.asInt();
}
float number(const Value& v) {
    if (!v.isDouble() && !v.isInt64()) invalid("Expected snapshot number");
    double n = v.isDouble() ? v.asDouble() : double(v.asInt());
    if (!std::isfinite(n) || std::abs(n) > std::numeric_limits<float>::max()) invalid("Invalid snapshot float");
    return float(n);
}
bool boolean(const Value& v) {
    auto* b = v.getIf<bool>();
    if (!b) invalid("Expected snapshot boolean");
    return *b;
}
void checkStamp(const NavigationSurfaceStamp& s) {
    const bool empty = s.surface.isZero() && s.movement.isZero() && s.surfaceRevision == 0 && s.movementRevision == 0 &&
                       s.costModelRevision == 0 && s.clearanceRadius == 0.f;
    const bool full = !s.surface.isZero() && !s.movement.isZero() && s.surfaceRevision != 0 &&
                      s.movementRevision != 0 && s.costModelRevision != 0 && std::isfinite(s.clearanceRadius) &&
                      s.clearanceRadius >= 0;
    if (!empty && !full) invalid("Incomplete surface stamp");
}
Value stamp(const NavigationSurfaceStamp& s) {
    checkStamp(s);
    return Value::Array{std::to_string(s.surface.value()),   std::to_string(s.movement.value()),
                        std::to_string(s.surfaceRevision),   std::to_string(s.movementRevision),
                        std::to_string(s.costModelRevision), s.clearanceRadius};
}
NavigationSurfaceStamp stamp(const Value& v) {
    const auto&            a = tuple(v, 6);
    NavigationSurfaceStamp s{NavigationSurfaceId(counter(a[0])),
                             MovementClassId(counter(a[1])),
                             counter(a[2]),
                             counter(a[3]),
                             counter(a[4]),
                             number(a[5])};
    checkStamp(s);
    return s;
}
template <class S>
auto settings(S& s) {
    return std::tie(s.defaultSpeed, s.defaultRadius, s.defaultTurnRate, s.arriveRadius, s.sepRadius, s.perceiveRadius,
                    s.sepWeight, s.alignWeight, s.cohesionWeight, s.wanderWeight, s.goalWeight, s.resolveOverlaps,
                    s.clampToField, s.maxAgents, s.simTime, s.avoidance.enabled, s.avoidance.horizon,
                    s.avoidance.margin, s.avoidance.maxNeighbors, s.nextAnonymousTieKey);
}
template <class S>
auto agents(S& s) {
    return std::tie(s.xs, s.ys, s.headings, s.vxs, s.vys, s.speeds, s.radii, s.maxSpeeds, s.maxAccels, s.turnRates,
                    s.actions, s.datas, s.avoidancePriorities, s.hasTargets, s.interactions, s.movements, s.flows,
                    s.targetXs, s.targetYs, s.wanderPhases, s.stableIds, s.tieKeys);
}
template <class Tuple, class Emit>
Value encodeTuple(Tuple&& t, Emit&& emit) {
    Value::Array a;
    std::apply([&](const auto&... x) { (a.push_back(emit(x)), ...); }, t);
    return a;
}
template <class Tuple, class Read>
void decodeTuple(const Value& v, Tuple&& t, Read&& read) {
    const auto& a = tuple(v, std::tuple_size_v<std::remove_reference_t<Tuple>>);
    std::size_t i = 0;
    std::apply([&](auto&... x) { (read(a[i++], x), ...); }, t);
}
Value atom(float v) {
    if (!std::isfinite(v)) invalid("Nonfinite snapshot value");
    return v;
}
Value atom(bool v) { return v; }
Value atom(int v) { return v; }
Value atom(std::uint8_t v) { return int(v); }
Value atom(std::uint32_t v) { return std::int64_t(v); }
Value atom(std::uint64_t v) { return std::to_string(v); }
Value atom(const std::string& v) { return v; }
Value atom(InteractionPolicy p) {
    return Value::Array{p.pushability,   p.holdPosition, p.avoidanceLayer,
                        p.avoidanceMask, p.contactLayer, p.contactMask};
}
Value atom(MovementBinding b) { return Value::Array{std::to_string(b.movement.value()), std::to_string(b.revision)}; }
Value atom(CrowdTieKey key) { return Value::Array{key.anonymous, std::to_string(key.value)}; }
void  atom(const Value& v, float& x) { x = number(v); }
void  atom(const Value& v, bool& x) { x = boolean(v); }
void  atom(const Value& v, int& x) { x = int(integer(v, INT32_MIN, INT32_MAX)); }
void  atom(const Value& v, std::uint8_t& x) { x = std::uint8_t(integer(v, 0, 1)); }
void  atom(const Value& v, std::uint32_t& x) { x = std::uint32_t(integer(v, 0, UINT32_MAX)); }
void  atom(const Value& v, std::uint64_t& x) { x = counter(v); }
void  atom(const Value& v, std::string& x) { x = text(v); }
void  atom(const Value& v, InteractionPolicy& p) {
    const auto& a = tuple(v, 6);
    p             = {number(a[0]),
                      boolean(a[1]),
                      int(integer(a[2], 0, INT32_MAX)),
                      int(integer(a[3], 0, INT32_MAX)),
                      int(integer(a[4], 0, INT32_MAX)),
                      int(integer(a[5], 0, INT32_MAX))};
    if (!validInteraction(p)) invalid("Invalid interaction");
}
void atom(const Value& v, MovementBinding& b) {
    const auto& a = tuple(v, 2);
    b             = {MovementClassId(counter(a[0])), counter(a[1])};
    if (b.movement.isZero() != (b.revision == 0)) invalid("Invalid movement binding");
}
void atom(const Value& v, CrowdTieKey& key) {
    const auto& a = tuple(v, 2);
    key           = {boolean(a[0]), counter(a[1])};
}
// Lists and tuples keep one typed field ledger for both directions.
template <class S>
void validateSettings(const S& s) {
    for (float x :
         {s.defaultSpeed, s.defaultRadius, s.defaultTurnRate, s.arriveRadius, s.sepRadius, s.perceiveRadius, s.simTime})
        if (!std::isfinite(x) || x < 0) invalid("Invalid backend configuration");
    if (s.maxAgents < 0 || s.nextAnonymousTieKey == 0 || s.avoidance.horizon <= 0 || s.avoidance.horizon > 10 ||
        s.avoidance.margin < 0 || s.avoidance.maxNeighbors < 1 || s.avoidance.maxNeighbors > 128)
        invalid("Invalid backend settings");
}
}  // namespace
Result<Value> Crowd::encodeArchivePlan(const FlowBuildPlan& plan, std::uint64_t owner) {
    Value field;
    if (plan.field_) {
        auto encoded = plan.field_->encodeState();
        if (!encoded) return Result<Value>::failure(encoded.status());
        field = std::move(encoded).takeValue();
    }
    return Result<Value>::success(
        Value::Array{int(plan.state_), plan.owner_ == owner, std::to_string(plan.revision_), std::move(field)});
}
Result<Crowd::FlowBuildPlan> Crowd::prepareArchivePlan(const Value& value, std::uint64_t owner) {
    using R = Result<FlowBuildPlan>;
    try {
        const auto&   a = tuple(value, 4);
        FlowBuildPlan plan;
        plan.state_    = FlowBuildState(integer(a[0], 0, 5));
        plan.owner_    = boolean(a[1]) ? owner : 0;
        plan.revision_ = counter(a[2]);
        if (!a[3].isNull()) plan.field_ = std::make_unique<CrowdField>(take(CrowdField::prepareState(a[3])));
        const bool work = plan.state_ == FlowBuildState::Pending || plan.state_ == FlowBuildState::Ready ||
                          plan.state_ == FlowBuildState::Unreachable;
        if (work != bool(plan.field_)) invalid("Plan phase/storage disagree");
        if (plan.field_) {
            auto&                 f = *plan.field_;
            std::vector<FlowGoal> goals;
            for (std::size_t i = 0; i < f.goals_.size(); i += 2) goals.push_back({f.goals_[i], f.goals_[i + 1]});
            if (take(canonicalFlowCoordinates(goals, f)) != f.goals_) invalid("Noncanonical plan goals");
            if (plan.revision_ == 0 || plan.revision_ != f.inputRevision() || f.flowCursor_ < 0 ||
                f.flowCursor_ > f.width_ * f.height_ || (plan.state_ == FlowBuildState::Pending && f.isBuilt()) ||
                (plan.state_ != FlowBuildState::Pending && (!f.isBuilt() || classifyCompletedFlow(f) != plan.state_)))
                invalid("Invalid plan phase/cursor/input");
        }
        return R::success(std::move(plan));
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
Result<Value> Crowd::encodeArchiveLease(const FlowLease& lease, std::uint64_t owner, Value field) {
    return Result<Value>::success(Value::Array{std::move(field), bool(lease.field_) && lease.owner_ == owner,
                                               std::to_string(lease.input_), std::to_string(lease.generation_),
                                               std::int64_t(lease.algorithm_)});
}
Result<FlowLease> Crowd::prepareArchiveLease(const Value& value, std::uint64_t owner,
                                             std::shared_ptr<const CrowdField> field) {
    using R = Result<FlowLease>;
    try {
        const auto& a = tuple(value, 5);
        FlowLease   lease;
        lease.field_      = std::move(field);
        lease.owner_      = boolean(a[1]) ? owner : 0;
        lease.input_      = counter(a[2]);
        lease.generation_ = counter(a[3]);
        lease.algorithm_  = std::uint32_t(integer(a[4], 0, UINT32_MAX));
        if (lease.field_ && (!lease.field_->isBuilt() || lease.generation_ == 0))
            invalid("Lease lacks built field/generation");
        return R::success(std::move(lease));
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
Result<Value>           Crowd::Snapshot::encodeState() const { return Crowd::encodeSnapshotState(*this); }
Result<Crowd::Snapshot> Crowd::Snapshot::prepareState(const Value& value) { return Crowd::prepareSnapshotState(value); }
Result<FlowRequestRef>  Crowd::Snapshot::requestReference(std::uint32_t index, std::uint32_t generation) const {
    if (!state_ || !state_->requests || index >= state_->requests->entries.size())
        return Result<FlowRequestRef>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Missing saved request"));
    const auto& entry = state_->requests->entries[index];
    if (!entry.occupied || entry.generation != generation)
        return Result<FlowRequestRef>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Retired saved request"));
    return Result<FlowRequestRef>::success(
        {RuntimeHandle<CrowdFlowRequestTag>(index, generation), state_->state.owner});
}
Result<Value> Crowd::encodeSnapshotState(const Snapshot& snapshot) {
    using R = Result<Value>;
    if (!snapshot.state_)
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Empty Crowd snapshot"));
    try {
        const auto& s = snapshot.state_->state;
        if (s.budgetActive) invalid("Active Crowd cannot be serialized");
        validateSettings(s);
        if (s.xs.size() > 1000000 || s.registry.size() > 1000000) invalid("Oversized agent snapshot");
        std::vector<std::shared_ptr<const CrowdField>>     nodes;
        std::unordered_map<const CrowdField*, std::size_t> nodeIndices;
        auto node = [&](const std::shared_ptr<const CrowdField>& field) -> Value {
            if (!field) return {};
            auto [found, inserted] = nodeIndices.emplace(field.get(), nodes.size());
            if (inserted) {
                nodes.push_back(field);
                if (nodes.size() > 100000) invalid("Too many field graph nodes");
            }
            return std::to_string(found->second);
        };
        Value::Array cache;
        for (const auto& e : s.flowCache)
            cache.push_back(Value::Array{node(e.field), stamp(e.surface), std::to_string(e.input),
                                         std::to_string(e.generation), std::to_string(e.bytes),
                                         std::to_string(e.access.value()), std::int64_t(e.algorithm)});
        auto lease = [&](const FlowLease& l) -> Value { return take(encodeArchiveLease(l, s.owner, node(l.field_))); };
        auto emit  = [&](const auto& v) -> Value {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, FlowLease>)
                return lease(v);
            else
                return atom(v);
        };
        Value::Array rows;
        const auto   columns = agents(s);
        for (std::size_t i = 0; i < s.xs.size(); ++i)
            rows.push_back(encodeTuple(std::apply(
                                           [&](const auto&... v) {
                                               if (((v.size() != s.xs.size()) || ...))
                                                   invalid("Misaligned agent columns");
                                               return std::tie(v[i]...);
                                           },
                                           columns),
                                       emit));
        Value requests;
        if (snapshot.state_->requests) {
            if (snapshot.state_->requests->entries.size() > 64) invalid("Oversized request registry");
            Value::Array entries;
            for (const auto& e : snapshot.state_->requests->entries) {
                entries.push_back(Value::Array{std::int64_t(e.generation), e.occupied,
                                               take(encodeArchivePlan(e.plan, s.owner)),
                                               e.lease ? lease(*e.lease) : Value{}});
            }
            requests = std::move(entries);
        }
        Value::Array fields;
        for (const auto& field : nodes) fields.push_back(take(field->encodeState()));
        Value::Array registry, dense, free;
        for (const auto& e : s.registry) registry.push_back(Value::Array{std::int64_t(e.generation), e.denseIndex});
        for (auto x : s.denseToRegistry) dense.push_back(std::int64_t(x));
        for (auto x : s.freeRegistry) free.push_back(std::int64_t(x));
        Value layout = Value::Array{std::int64_t(sizeof(CrowdField)), std::int64_t(sizeof(Impl::FlowCacheEntry)),
                                    std::int64_t(sizeof(float)), std::int64_t(sizeof(int)),
                                    std::int64_t(sizeof(std::pair<float, int>))};
        return R::success(Value::object(
            {{"schema", "crowd.snapshot"},
             {"version", 1},
             {"field", take(s.field.encodeState())},
             {"surface", stamp(s.surfaceStamp)},
             {"settings", encodeTuple(settings(s), emit)},
             {"clock", Value::Array{std::to_string(s.tick.value()), s.elapsed.nanoseconds(),
                                    s.clock ? Value(int(*s.clock)) : Value{}}},
             {"registry", Value::Array{std::move(registry), std::move(dense), std::move(free)}},
             {"agents", std::move(rows)},
             {"cache", Value::Array{std::to_string(s.flowCacheBudget), std::to_string(s.nextFlowGeneration),
                                    std::move(layout), std::move(cache)}},
             {"nodes", std::move(fields)},
             {"requests", std::move(requests)}}));
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
Result<Crowd::Snapshot> Crowd::prepareSnapshotState(const Value& value) {
    using R = Result<Snapshot>;
    try {
        const auto* root = value.getIf<Value::Object>();
        if (!root || root->size() != 11) invalid("Unknown or missing snapshot fields");
        for (const char* key : {"schema", "version", "field", "surface", "settings", "clock", "registry", "agents",
                                "cache", "nodes", "requests"})
            if (!root->contains(key)) invalid("Missing snapshot field");
        if (text(root->at("schema")) != "crowd.snapshot") invalid("Wrong snapshot schema");
        if (integer(root->at("version"), 0, INT32_MAX) != 1)
            invalid("Unknown snapshot version", DiagnosticCode::UnknownVersion);
        const auto&                      cache  = tuple(root->at("cache"), 4);
        const auto&                      layout = tuple(cache[2], 5);
        const std::array<std::size_t, 5> expected{sizeof(CrowdField), sizeof(Impl::FlowCacheEntry), sizeof(float),
                                                  sizeof(int), sizeof(std::pair<float, int>)};
        for (std::size_t i = 0; i < 5; ++i)
            if (integer(layout[i], 0, INT32_MAX) != std::int64_t(expected[i]))
                invalid("Cache byte accounting ABI differs", DiagnosticCode::Unsupported);
        std::shared_ptr<SnapshotData> data;
        try {
            data = std::make_shared<SnapshotData>();
        } catch (const eve::Exception& error) {
            return R::failure(
                Diagnostic::error(DiagnosticCode::PreconditionViolation, error.what(), "crowd.snapshot.scope"));
        }
        auto& s        = data->state;
        s.field        = take(CrowdField::prepareState(root->at("field")));
        s.surfaceStamp = stamp(root->at("surface"));
        if (s.field.valid() && s.field.inputRevision() == 0) invalid("Configured field lost input revision");
        decodeTuple(root->at("settings"), settings(s), [](const Value& v, auto& x) { atom(v, x); });
        validateSettings(s);
        const auto& clock = tuple(root->at("clock"), 3);
        s.tick            = SimulationTick(counter(clock[0]));
        s.elapsed         = Duration(integer(clock[1], 0, INT64_MAX));
        if (!clock[2].isNull()) s.clock = ClockAuthority(integer(clock[2], 0, 1));
        if (!s.clock && (!s.tick.isZero() || !s.elapsed.isZero())) invalid("Unbound clock carries time");
        s.flowCacheBudget    = counter(cache[0]);
        s.nextFlowGeneration = counter(cache[1]);
        if (s.flowCacheBudget > 8ULL * 1024 * 1024 * 1024 || s.nextFlowGeneration == 0)
            invalid("Invalid cache watermarks");
        std::vector<std::shared_ptr<const CrowdField>> nodes;
        const auto&                                    encodedNodes = array(root->at("nodes"));
        if (encodedNodes.size() > 100000) invalid("Too many field graph nodes");
        for (const auto& node : encodedNodes)
            nodes.push_back(std::make_shared<const CrowdField>(take(CrowdField::prepareState(node))));
        std::vector<bool> used(nodes.size());
        std::size_t       nextNode = 0;
        auto              node     = [&](const Value& v) -> std::shared_ptr<const CrowdField> {
            if (v.isNull()) return {};
            auto id = counter(v);
            if (id >= nodes.size()) invalid("Unknown field graph node");
            if (!used[std::size_t(id)]) {
                if (id != nextNode++) invalid("Noncanonical field graph ordering");
                used[std::size_t(id)] = true;
            }

            return nodes[std::size_t(id)];
        };
        auto lease = [&](const Value& v) -> FlowLease {
            const auto& a = tuple(v, 5);
            return take(prepareArchiveLease(v, s.owner, node(a[0])));
        };
        auto validateGoals = [&](const CrowdField& field) {
            std::vector<FlowGoal> goals;
            for (std::size_t i = 0; i < field.goals_.size(); i += 2)
                goals.push_back({field.goals_[i], field.goals_[i + 1]});
            if (take(canonicalFlowCoordinates(goals, field)) != field.goals_)
                invalid("Noncanonical request/cache goals");
        };
        std::uint64_t bytes = 0, prior = 0;
        if (array(cache[3]).size() > 10000) invalid("Too many cache entries");
        for (const auto& value : array(cache[3])) {
            const auto&          a = tuple(value, 7);
            Impl::FlowCacheEntry e;
            e.field      = node(a[0]);
            e.surface    = stamp(a[1]);
            e.input      = counter(a[2]);
            e.generation = counter(a[3]);
            e.bytes      = counter(a[4]);
            e.access     = SimulationTick(counter(a[5]));
            e.algorithm  = std::uint32_t(integer(a[6], 0, UINT32_MAX));
            if (!e.field || !e.field->isBuilt() || e.generation <= prior || e.generation >= s.nextFlowGeneration ||
                e.input == 0 || e.input != e.field->inputRevision() || e.algorithm != flowBuildAlgorithmVersion ||
                e.bytes != Impl::retainedFlowBytes(*e.field) || e.bytes > s.flowCacheBudget - bytes)
                invalid("Invalid cache entry identity/accounting");
            validateGoals(*e.field);
            prior = e.generation;
            bytes += e.bytes;
            s.flowCache.push_back(std::move(e));
        }
        std::vector<std::size_t> keys;
        keys.reserve(s.flowCache.size());
        for (std::size_t i = 0; i < s.flowCache.size(); ++i) keys.push_back(i);
        std::sort(keys.begin(), keys.end(),
                  [&](auto a, auto b) { return Impl::isFlowKeyBefore(s.flowCache[a], s.flowCache[b]); });
        for (std::size_t i = 1; i < keys.size(); ++i)
            if (!Impl::isFlowKeyBefore(s.flowCache[keys[i - 1]], s.flowCache[keys[i]]))
                invalid("Duplicate complete flow cache key");
        const auto& rows = array(root->at("agents"));
        if (rows.size() > 1000000) invalid("Agent snapshot exceeds supported count");
        auto read = [&](const Value& v, auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, FlowLease>)
                x = lease(v);
            else
                atom(v, x);
        };
        for (const auto& row : rows) {
            auto columns = agents(s);
            std::apply([](auto&... v) { (v.emplace_back(), ...); }, columns);
            decodeTuple(row, std::apply([](auto&... v) { return std::tie(v.back()...); }, columns), read);
        }
        if (s.nextAnonymousTieKey == 0) invalid("Zero tie allocation watermark");
        std::set<std::uint64_t> anonymous;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (s.speeds[i] < 0 || s.radii[i] < 0 || s.maxSpeeds[i] < 0 || s.maxAccels[i] < 0 || s.turnRates[i] < 0 ||
                s.actions[i] < 0 || s.actions[i] > 3)
                invalid("Invalid agent facts");
            const auto& key = s.tieKeys[i];
            if (s.stableIds[i].empty()) {
                if (!key.anonymous || key.value == 0 || key.value >= s.nextAnonymousTieKey ||
                    !anonymous.insert(key.value).second)
                    invalid("Invalid anonymous tie");
            } else if (key.anonymous || key.value != namedTieValue(s.stableIds[i]) ||
                       !s.namedAgents.emplace(s.stableIds[i], int(i)).second ||
                       !s.namedTieKeys.emplace(key.value, int(i)).second)
                invalid("Duplicate or inconsistent named agent");
        }
        const auto& registry = tuple(root->at("registry"), 3);
        if (array(registry[0]).size() > 1000000) invalid("Oversized agent registry");
        for (const auto& row : array(registry[0])) {
            const auto& a = tuple(row, 2);
            s.registry.push_back({std::uint32_t(integer(a[0], 1, UINT32_MAX)), int(integer(a[1], -1, INT32_MAX))});
        }
        std::set<std::uint32_t> occupied, free;
        for (const auto& v : array(registry[1])) {
            auto index = std::uint32_t(integer(v, 0, UINT32_MAX));
            if (index >= s.registry.size() || s.registry[index].denseIndex != int(s.denseToRegistry.size()) ||
                !occupied.insert(index).second)
                invalid("Invalid dense/registry correspondence");
            s.denseToRegistry.push_back(index);
        }
        if (s.denseToRegistry.size() != rows.size()) invalid("Agent registry length differs");
        for (const auto& v : array(registry[2])) {
            auto index = std::uint32_t(integer(v, 0, UINT32_MAX));
            if (index >= s.registry.size() || s.registry[index].denseIndex != -1 ||
                s.registry[index].generation == UINT32_MAX || !free.insert(index).second)
                invalid("Invalid free registry entry");
            s.freeRegistry.push_back(index);
        }
        for (std::size_t i = 0; i < s.registry.size(); ++i)
            if (!occupied.contains(std::uint32_t(i)) && !free.contains(std::uint32_t(i)) &&
                (s.registry[i].denseIndex != -1 || s.registry[i].generation != UINT32_MAX))
                invalid("Missing registry correspondence");
        if (!root->at("requests").isNull()) {
            data->requests = std::make_unique<NavigationRequests>();
            if (array(root->at("requests")).size() > 64) invalid("Oversized request registry");
            for (const auto& v : array(root->at("requests"))) {
                const auto&               a = tuple(v, 4);
                NavigationRequests::Entry e;
                e.generation = std::uint32_t(integer(a[0], 1, UINT32_MAX));
                e.occupied   = boolean(a[1]);
                e.plan       = take(prepareArchivePlan(a[2], s.owner));
                if (!a[3].isNull()) e.lease = lease(a[3]);
                const bool work = e.plan.state_ == FlowBuildState::Pending || e.plan.state_ == FlowBuildState::Ready ||
                                  e.plan.state_ == FlowBuildState::Unreachable;
                if (work != bool(e.plan.field_) ||
                    (!e.occupied && (e.plan.field_ || e.lease || e.plan.state_ != FlowBuildState::Cancelled ||
                                     e.plan.owner_ != 0 || e.plan.revision_ != 0)) ||
                    (e.occupied && (e.plan.owner_ != s.owner || e.plan.revision_ == 0)))
                    invalid("Request phase/storage disagree");
                data->requests->entries.push_back(std::move(e));
            }
        }
        auto validateLease = [&](const FlowLease& l) {
            if (!l.field_ || l.owner_ != s.owner) return;
            auto found =
                std::lower_bound(s.flowCache.begin(), s.flowCache.end(), l.generation_,
                                 [](const auto& e, std::uint64_t generation) { return e.generation < generation; });
            if (found == s.flowCache.end() || found->generation != l.generation_ || !Impl::matchesFlowLease(*found, l))
                invalid("Current lease is not closed over captured cache");
        };
        for (const auto& l : s.flows) validateLease(l);
        if (data->requests)
            for (const auto& e : data->requests->entries)
                if (e.lease) validateLease(*e.lease);
        if (std::find(used.begin(), used.end(), false) != used.end()) invalid("Unreferenced field graph node");
        s.detachFlowPins(data->requests.get());
        Snapshot snapshot;
        snapshot.state_ = std::move(data);
        return R::success(std::move(snapshot));
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
Result<void> Crowd::NavigationCapture::validateGoalSets(std::span<const std::vector<FlowGoal>> sets) const {
    return Crowd::validateNavigationGoals(*this, sets);
}
Result<void> Crowd::validateNavigationGoals(const NavigationCapture&               capture,
                                            std::span<const std::vector<FlowGoal>> sets) {
    const auto* data_ = capture.data_.get();
    if (!data_ || data_->work.size() != sets.size())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, "External goal/work count mismatch"));
    for (std::size_t i = 0; i < sets.size(); ++i) {
        const auto& row = data_->work[i];
        for (const CrowdField* field :
             std::array<const CrowdField*, 2>{row.plan.field_.get(), row.lease.field_.get()}) {
            if (!field) continue;
            auto canonical = canonicalFlowCoordinates(sets[i], *field);
            if (!canonical) return Result<void>::failure(canonical.status());
            if (canonical.value() != field->goals_)
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::Conflict, "External goal/field mismatch"));
        }
    }
    return Result<void>::success();
}
Result<SnapshotEnvelope> Crowd::NavigationCapture::seal(PersistentId epoch, Revision revision, SimulationTick tick,
                                                        const SnapshotHashProvider& hash) const {
    return Crowd::sealNavigationState(*this, epoch, revision, tick, hash);
}
Result<Crowd::NavigationCapture> Crowd::Snapshot::parseNavigation(SnapshotEnvelope envelope, PersistentId epoch,
                                                                  const SnapshotHashProvider& hash) const {
    return Crowd::parseNavigationState(*this, std::move(envelope), epoch, hash);
}
Result<SnapshotEnvelope> Crowd::sealNavigationState(const NavigationCapture& capture, PersistentId epoch,
                                                    Revision revision, SimulationTick tick,
                                                    const SnapshotHashProvider& hash) {
    using R = Result<SnapshotEnvelope>;
    if (!capture.data_ || !capture.data_->source || epoch.isNil() || revision.isZero() || !hash)
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Invalid navigation save boundary"));
    try {
        const auto source = capture.data_->source;
        if (capture.data_->work.size() > 100000) invalid("Too many external navigation rows");
        Snapshot backend;
        backend.state_                                                    = source;
        auto                                                 backendValue = take(backend.encodeState());
        auto                                                 backendJson  = take(backendValue.toJson());
        std::map<std::uint64_t, const Impl::FlowCacheEntry*> cache;
        for (const auto& entry : source->state.flowCache) cache.emplace(entry.generation, &entry);
        Value::Array work;
        work.reserve(capture.data_->work.size());
        for (const auto& row : capture.data_->work) {
            Value field;
            if (row.lease.field_) {
                const auto found  = cache.find(row.lease.generation_);
                const bool cached = found != cache.end() && Impl::matchesFlowLease(*found->second, row.lease);
                if (cached)
                    field = Value::Array{0};
                else
                    field = Value::Array{1, take(row.lease.field_->encodeState())};
            }
            work.push_back(Value::Array{take(encodeArchivePlan(row.plan, source->state.owner)),
                                        take(encodeArchiveLease(row.lease, source->state.owner, std::move(field)))});
        }
        // All source facts are owned locally before an unknown digest callback can retire the caller.
        auto digest = take(hash(backendJson));
        if (digest.isNil()) invalid("Nil backend digest", DiagnosticCode::InvalidArgument);
        return makeSnapshotEnvelope("eve.crowd-navigation", *LogicalId::fromParts("crowd", "navigation"),
                                    SchemaVersion(1), epoch, revision, tick,
                                    Value::object({{"backendHash", digest.format()}, {"work", std::move(work)}}), hash);
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
Result<Crowd::NavigationCapture> Crowd::parseNavigationState(const Snapshot& backend, SnapshotEnvelope envelope,
                                                             PersistentId epoch, const SnapshotHashProvider& hash) {
    using R           = Result<NavigationCapture>;
    const auto source = backend.state_;
    if (!source || epoch.isNil() || !hash)
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Missing navigation backend/hash"));
    // Capture canonical backend bytes before callbacks, which may destroy the Snapshot wrapper.
    Snapshot detached;
    detached.state_ = source;
    auto encoded    = detached.encodeState();
    if (!encoded) return R::failure(encoded.status());
    auto json = encoded.value().toJson();
    if (!json) return R::failure(json.status());
    auto verified = verifySnapshotEnvelope(envelope, hash);
    if (!verified) return R::failure(verified.status());
    if (envelope.schemaVersion != SchemaVersion(1))
        return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unknown external navigation version"));
    if (envelope.type != "eve.crowd-navigation" || envelope.schema != *LogicalId::fromParts("crowd", "navigation") ||
        envelope.instanceId != epoch || envelope.revision.isZero())
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "External navigation boundary mismatch"));
    try {
        const auto* root = envelope.payload.getIf<Value::Object>();
        if (!root || root->size() != 2 || !root->contains("backendHash") || !root->contains("work"))
            invalid("Unknown or missing navigation fields");
        const auto& spelling = text(root->at("backendHash"));
        auto        expected = ContentId::parse(spelling);
        if (!expected || expected->isNil() || expected->format() != spelling) invalid("Invalid backend digest");
        auto actual = take(hash(json.value()));
        if (actual != *expected) invalid("Navigation backend digest mismatch", DiagnosticCode::Conflict);
        const auto& rows = array(root->at("work"));
        if (rows.size() > 100000) invalid("Too many external navigation rows");
        auto data    = std::make_unique<NavigationData>();
        data->source = source;
        data->work.reserve(rows.size());
        std::map<std::uint64_t, const Impl::FlowCacheEntry*> cache;
        for (const auto& entry : source->state.flowCache) cache.emplace(entry.generation, &entry);
        for (const auto& row : rows) {
            const auto&    a = tuple(row, 2);
            NavigationWork work;
            work.plan          = take(prepareArchivePlan(a[0], source->state.owner));
            work.lease         = take(prepareArchiveLease(a[1], source->state.owner, {}));
            const auto& role   = tuple(a[1], 5)[0];
            bool        cached = false;
            if (!role.isNull()) {
                const auto& ref = array(role);
                if (ref.empty()) invalid("Missing external field role");
                const auto mode = integer(ref[0], 0, 1);
                if (mode == 0) {
                    if (ref.size() != 1) invalid("Invalid cache reference");
                    const auto found = cache.find(work.lease.generation_);
                    if (found != cache.end() && found->second->input == work.lease.input_ &&
                        found->second->algorithm == work.lease.algorithm_) {
                        work.lease.field_ = found->second->field;
                        cached            = true;
                    }
                    if (!cached) invalid("Missing external cache field", DiagnosticCode::Conflict);
                } else {
                    if (ref.size() != 2) invalid("Invalid inline field");
                    work.lease.field_ = std::make_shared<CrowdField>(take(CrowdField::prepareState(ref[1])));
                }
            }
            if (!work.lease.field_ && work.lease.owner_ != 0) invalid("Empty lease has owner permission");
            work.lease = take(prepareArchiveLease(a[1], source->state.owner, std::move(work.lease.field_)));
            if (work.lease.field_ && work.lease.owner_ == source->state.owner &&
                work.lease.input_ == source->state.field.inputRevision() &&
                work.lease.algorithm_ == flowBuildAlgorithmVersion && !cached)
                invalid("Current external lease is outside backend cache", DiagnosticCode::Conflict);
            data->work.push_back(std::move(work));
        }
        return R::success(NavigationCapture(std::move(data)));
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}

}  // namespace eve::crowd
