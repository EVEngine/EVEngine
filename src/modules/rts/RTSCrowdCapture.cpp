#include "rts/RTSCrowdCapture.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include "crowd/Crowd.h"
#include "rts/RTS.h"
#include "rts/RTSCrowdFlowNavigationInternal.h"
#include "rts/RTSSnapshotInternal.h"
namespace eve::rts {
namespace {
template <class T>
Result<T> reject(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message));
}
bool same(ecs::EntityHandle a, ecs::EntityHandle b) noexcept {
    return a.table == b.table && a.type == b.type && a.id == b.id && a.generation == b.generation;
}
bool pristine(const Unit::Crowd& value) noexcept {
    return !value.link.isBound() && !value.runtimeAgent.isValid() && value.runtimeOwner == 0 &&
           value.runtimeBindingKey.empty();
}
ResultRef<Unit::Crowd> destination(ecs::EntityHandle handle, const RTSCrowdMember& saved) {
    auto* unit = dynamic_cast<Unit*>(ecs::try_get(handle));
    if (!unit || snapshot_internal::existingSubject(*unit) != saved.subject)
        return reject<std::reference_wrapper<Unit::Crowd>>(DiagnosticCode::StaleHandle,
                                                           "Crowd Unit incarnation changed");
    const auto* motion      = detail::existingComponent<Unit::Motion>(*unit);
    const auto* containment = detail::existingComponent<Unit::Containment>(*unit);
    auto*       settings    = detail::existingComponent<Unit::Crowd>(*unit);
    if (!motion || !containment || !settings)
        return reject<std::reference_wrapper<Unit::Crowd>>(DiagnosticCode::NotFound, "Crowd Unit component missing");
    if (containment->container.isBound() && containment->container.handle().table != handle.table)
        return reject<std::reference_wrapper<Unit::Crowd>>(DiagnosticCode::Conflict, "Foreign Crowd container");
    auto container =
        containment->container.isBound() ? snapshot_internal::subjectOf(containment->container.handle()) : SubjectRef{};
    if ((containment->container.isBound() && !container.isValid()) || container != saved.container ||
        motion->x != saved.x || motion->y != saved.y || !pristine(*settings))
        return reject<std::reference_wrapper<Unit::Crowd>>(DiagnosticCode::Conflict,
                                                           "Crowd Unit claimed or pose/containment differs");
    return ResultRef<Unit::Crowd>::success(std::ref(*settings));
}
}  // namespace
namespace {
// Native-only qualifier for closed RTS root relationships; not a persistent type encoding.
struct FrozenFlowSubject {
    SubjectRef      subject;
    std::type_index type;
};
struct FrozenFlowNavigation {
    CrowdFlowNavigationSettings settings;
    bool                        enabled = false;
    struct Job {
        std::uint64_t                sequence;
        std::vector<crowd::FlowGoal> goals;
        SimulationTick               used;
    };
    struct Group {
        std::optional<FrozenFlowSubject>                    unit;
        std::string                                         order;
        std::shared_ptr<const std::vector<crowd::FlowGoal>> goals;
        bool                                                reform;
        std::uint64_t                                       owner, input;
    };
    struct Notice {
        std::optional<FrozenFlowSubject> unit, target;
        OrderRecord                      order;
    };
    std::vector<Job>                               jobs;
    std::vector<Group>                             groups;
    std::vector<Notice>                            notices;
    std::optional<crowd::Crowd::NavigationCapture> work;
    std::optional<SimulationTick>                  frame, pumped;
    std::uint64_t                  owner = 0, input = 0, nextSequence = 1, workAfter = 0, roundThrough = 0;
    CrowdFlowNavigationObservation stats;
};
}  // namespace
struct RTSCrowdCapture::Impl {
    PersistentId                          epoch;
    crowd::Crowd::Snapshot                snapshot;
    crowd::AgentFrame                     frame;
    std::vector<RTSCrowdMember>           members;
    std::uint64_t                         backendOwner      = 0;
    bool                                  clearFlowBindings = false;
    std::unique_ptr<FrozenFlowNavigation> navigation;
};
RTSCrowdCapture::RTSCrowdCapture(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RTSCrowdCapture::~RTSCrowdCapture()                                     = default;
RTSCrowdCapture::RTSCrowdCapture(RTSCrowdCapture&&) noexcept            = default;
RTSCrowdCapture& RTSCrowdCapture::operator=(RTSCrowdCapture&&) noexcept = default;
PersistentId     RTSCrowdCapture::saveEpoch() const noexcept { return impl_ ? impl_->epoch : PersistentId{}; }
std::span<const RTSCrowdMember> RTSCrowdCapture::members() const noexcept {
    return impl_ ? std::span<const RTSCrowdMember>(impl_->members) : std::span<const RTSCrowdMember>{};
}
std::optional<std::reference_wrapper<const crowd::AgentFrame>> RTSCrowdCapture::frame() const noexcept {
    return impl_ ? std::optional{std::cref(impl_->frame)} : std::nullopt;
}
namespace {
namespace crowd_archive {
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message) {
    throw Invalid{Status::failure(Diagnostic::error(DiagnosticCode::ParseError, message, "rts.crowd"))};
}
template <class T>
T take(Result<T> value) {
    if (!value) throw Invalid{value.status()};
    return std::move(value).takeValue();
}
const Value::Array& array(const Value& v, std::size_t limit = 1000000) {
    const auto* a = v.getIf<Value::Array>();
    if (!a || a->size() > limit) invalid("Invalid Crowd archive array");
    return *a;
}
const Value::Array& tuple(const Value& v, std::size_t size) {
    const auto& a = array(v);
    if (a.size() != size) invalid("Unknown or missing Crowd archive tuple fields");
    return a;
}
const std::string& text(const Value& v) {
    const auto* s = v.getIf<std::string>();
    if (!s) invalid("Expected Crowd archive text");
    return *s;
}
std::uint64_t counter(const Value& v) {
    const auto&   s      = text(v);
    std::uint64_t result = 0;
    const auto    p      = std::from_chars(s.data(), s.data() + s.size(), result);
    if (p.ec != std::errc{} || p.ptr != s.data() + s.size() || std::to_string(result) != s)
        invalid("Invalid Crowd counter");
    return result;
}
std::size_t size(const Value& v) {
    const auto result = counter(v);
    if (result > std::numeric_limits<std::size_t>::max()) invalid("Crowd size exceeds host range");
    return std::size_t(result);
}
bool boolean(const Value& v) {
    const auto* b = v.getIf<bool>();
    if (!b) invalid("Expected Crowd boolean");
    return *b;
}
int integer(const Value& v, int min, int max) {
    if (!v.isInt64() || v.asInt() < min || v.asInt() > max) invalid("Invalid Crowd integer");
    return int(v.asInt());
}
float number(const Value& v) {
    if (!v.isDouble() && !v.isInt64()) invalid("Expected Crowd number");
    const auto d = v.isDouble() ? v.asDouble() : double(v.asInt());
    if (!std::isfinite(d) || std::abs(d) > std::numeric_limits<float>::max()) invalid("Invalid Crowd float");
    return float(d);
}
SubjectRef subject(const Value& v, bool nil = false) {
    const auto& spelling = text(v);
    auto        id       = PersistentId::parse(spelling);
    if (!id || id->format() != spelling || (!nil && id->isNil())) invalid("Invalid Crowd subject");
    return SubjectRef::fromPersistentId(*id);
}
const std::array<std::type_index, 6> rootTypes{typeid(Unit),   typeid(Building), typeid(ResourceNode),
                                               typeid(Player), typeid(Faction),  typeid(Match)};
Value                                root(const std::optional<FrozenFlowSubject>& s) {
    if (!s) return {};
    const auto found = std::find(rootTypes.begin(), rootTypes.end(), s->type);
    if (found == rootTypes.end() || !s->subject.isValid()) invalid("Unsupported navigation root family");
    return Value::Array{s->subject.format(), int(found - rootTypes.begin())};
}
std::optional<FrozenFlowSubject> root(const Value& v) {
    if (v.isNull()) return {};
    const auto& a = tuple(v, 2);
    return FrozenFlowSubject{subject(a[0]), rootTypes[integer(a[1], 0, 5)]};
}
Value tick(const std::optional<SimulationTick>& value) {
    return value ? Value(std::to_string(value->value())) : Value{};
}
std::optional<SimulationTick> tick(const Value& v) {
    return v.isNull() ? std::nullopt : std::optional{SimulationTick(counter(v))};
}
Value goals(const std::vector<crowd::FlowGoal>& values) {
    Value::Array a;
    a.reserve(values.size());
    for (const auto& g : values) a.push_back(Value::Array{g.x, g.y});
    return a;
}
std::vector<crowd::FlowGoal> goals(const Value& v) {
    const auto& a = array(v, 1024);
    if (a.empty()) invalid("Empty shared goals");
    std::vector<crowd::FlowGoal> result;
    result.reserve(a.size());
    for (const auto& row : a) {
        const auto&     g = tuple(row, 2);
        crowd::FlowGoal goal{integer(g[0], 0, INT_MAX), integer(g[1], 0, INT_MAX)};
        if (!result.empty() && !(result.back() < goal)) invalid("Noncanonical shared goals");
        result.push_back(goal);
    }
    return result;
}
// One field ledger for the owning notification order; target relationships are encoded separately.
template <class T>
auto orderFields(T& order) {
    return std::tie(order.id, order.kind, order.target.x, order.target.y, order.definitionId, order.secondaryTarget.x,
                    order.secondaryTarget.y, order.radius, order.append, order.formationSlot);
}
Value scalar(const std::string& v) { return v; }
Value scalar(float v) { return v; }
Value scalar(int v) { return v; }
Value scalar(bool v) { return v; }
Value scalar(OrderKind v) { return int(v); }
void  read(const Value& v, std::string& x) { x = text(v); }
void  read(const Value& v, float& x) { x = number(v); }
void  read(const Value& v, int& x) { x = integer(v, INT_MIN, INT_MAX); }
void  read(const Value& v, bool& x) { x = boolean(v); }
void  read(const Value& v, OrderKind& x) { x = OrderKind(integer(v, 0, int(OrderKind::SupplyRelay))); }
Value order(const OrderRecord& value) {
    Value::Array a;
    std::apply([&](const auto&... v) { (a.push_back(scalar(v)), ...); }, orderFields(value));
    return a;
}
OrderRecord order(const Value& v) {
    OrderRecord result;
    const auto  fields = orderFields(result);
    const auto& a      = tuple(v, std::tuple_size_v<decltype(fields)>);
    std::size_t i      = 0;
    std::apply([&](auto&... x) { (read(a[i++], x), ...); }, fields);
    CommandSpec command;
    command.kind            = result.kind;
    command.target          = result.target;
    command.secondaryTarget = result.secondaryTarget;
    command.radius          = result.radius;
    auto valid              = command.validate();
    if (!valid) throw Invalid{valid.status()};
    if (result.id.empty() || result.formationSlot < -1) invalid("Invalid notice order");
    return result;
}
Value navigation(const FrozenFlowNavigation& n, std::uint64_t owner) {
    Value::Array                                               jobs, nodes, groups, notices;
    std::map<const std::vector<crowd::FlowGoal>*, std::size_t> indices;
    for (const auto& j : n.jobs)
        jobs.push_back(Value::Array{std::to_string(j.sequence), goals(j.goals), std::to_string(j.used.value())});
    for (const auto& g : n.groups) {
        if (!g.goals) invalid("Missing group goals");
        auto [it, inserted] = indices.emplace(g.goals.get(), indices.size());
        if (inserted) nodes.push_back(goals(*g.goals));
        groups.push_back(Value::Array{root(g.unit), g.order, std::to_string(it->second), g.reform, g.owner == owner,
                                      std::to_string(g.input)});
    }
    for (const auto& notice : n.notices)
        notices.push_back(Value::Array{root(notice.unit), root(notice.target), order(notice.order)});
    const auto& s = n.stats;
    return Value::Array{
        Value::Array{n.settings.maxWorkPerTick, n.settings.maxStartsPerTick},
        n.enabled,
        tick(n.frame),
        tick(n.pumped),
        n.owner == owner,
        std::to_string(n.input),
        std::to_string(n.nextSequence),
        std::to_string(n.workAfter),
        std::to_string(n.roundThrough),
        Value::Array{std::to_string(s.pendingGoals), std::to_string(s.readyGoals), s.lastWork, s.lastStarts,
                     std::to_string(s.prepared), std::to_string(s.cacheHits), s.cachePressure},
        std::move(jobs),
        std::move(nodes),
        std::move(groups),
        std::move(notices),
        Value{}};
}
std::unique_ptr<FrozenFlowNavigation> navigation(const Value& v, std::uint64_t owner) {
    const auto& a        = tuple(v, 15);
    auto        n        = std::make_unique<FrozenFlowNavigation>();
    const auto& settings = tuple(a[0], 2);
    n->settings          = {integer(settings[0], 1, 1000000), integer(settings[1], 1, 16)};
    n->enabled           = boolean(a[1]);
    n->frame             = tick(a[2]);
    n->pumped            = tick(a[3]);
    n->owner             = boolean(a[4]) ? owner : 0;
    n->input             = counter(a[5]);
    n->nextSequence      = counter(a[6]);
    n->workAfter         = counter(a[7]);
    n->roundThrough      = counter(a[8]);
    if (n->nextSequence == 0 || n->workAfter >= n->nextSequence || n->roundThrough >= n->nextSequence)
        invalid("Invalid work sequence watermarks");
    const auto& s = tuple(a[9], 7);
    n->stats      = {size(s[0]),    size(s[1]),   integer(s[2], 0, 1000000), integer(s[3], 0, 16), counter(s[4]),
                     counter(s[5]), boolean(s[6])};
    std::set<std::uint64_t> sequences;
    for (const auto& row : array(a[10], 10000)) {
        const auto&               j = tuple(row, 3);
        FrozenFlowNavigation::Job job{counter(j[0]), goals(j[1]), SimulationTick(counter(j[2]))};
        if (job.sequence == 0 || job.sequence >= n->nextSequence || !sequences.insert(job.sequence).second ||
            (!n->jobs.empty() && !(n->jobs.back().goals < job.goals)))
            invalid("Invalid navigation job ordering");
        n->jobs.push_back(std::move(job));
    }
    std::vector<std::shared_ptr<const std::vector<crowd::FlowGoal>>> nodes;
    for (const auto& row : array(a[11], 65536))
        nodes.push_back(std::make_shared<const std::vector<crowd::FlowGoal>>(goals(row)));
    std::vector<bool>      used(nodes.size());
    std::size_t            next = 0;
    std::set<PersistentId> units;
    for (const auto& row : array(a[12], 65536)) {
        const auto& g   = tuple(row, 6);
        const auto  ref = counter(g[2]);
        if (ref >= nodes.size() || (!used[ref] && ref != next)) invalid("Invalid group goal graph reference");
        if (!used[ref]) {
            used[ref] = true;
            ++next;
        }
        auto unit = root(g[0]);
        if (unit && !units.emplace(unit->subject.persistentId()).second) invalid("Duplicate group member");
        auto id = text(g[1]);
        if (id.empty()) invalid("Empty group order");
        n->groups.push_back({std::move(unit), id, nodes[ref], boolean(g[3]), boolean(g[4]) ? owner : 0, counter(g[5])});
    }
    if (next != nodes.size()) invalid("Orphan shared goal node");
    for (const auto& row : array(a[13])) {
        const auto& r = tuple(row, 3);
        n->notices.push_back({root(r[0]), root(r[1]), order(r[2])});
    }
    if (!n->enabled && (!n->jobs.empty() || !n->groups.empty() || !n->notices.empty()))
        invalid("Disabled navigation retained jobs/groups");
    return n;
}
void validateWork(const FrozenFlowNavigation& n, const crowd::Crowd::Snapshot& backend, std::uint64_t owner) {
    if (!n.work) invalid("Missing external navigation work");
    std::vector<std::vector<crowd::FlowGoal>> goals;
    goals.reserve(n.jobs.size());
    for (const auto& job : n.jobs) {
        if (n.owner == owner) {
            auto valid = backend.validateGoalSet(job.goals, n.input);
            if (!valid) throw Invalid{valid.status()};
        }
        goals.push_back(job.goals);
    }
    std::map<const std::vector<crowd::FlowGoal>*, std::set<std::uint64_t>> checked;
    for (const auto& group : n.groups) {
        if (group.owner == owner && checked[group.goals.get()].insert(group.input).second) {
            auto valid = backend.validateGoalSet(*group.goals, group.input);
            if (!valid) throw Invalid{valid.status()};
        }
    }
    auto valid = n.work->validateGoalSets(goals);
    if (!valid) throw Invalid{valid.status()};
}
Value scalar(SubjectRef v) { return v.format(); }
Value scalar(const std::optional<std::size_t>& v) { return v ? Value(std::to_string(*v)) : Value{}; }
void  read(const Value& v, SubjectRef& x) { x = subject(v, true); }
void  read(const Value& v, std::optional<std::size_t>& x) { x = v.isNull() ? std::nullopt : std::optional{size(v)}; }
template <class T>
auto memberFields(T& m) {
    return std::tie(m.subject, m.container, m.key, m.bindingKey, m.radius, m.heading, m.x, m.y, m.frameIndex);
}
Value member(const RTSCrowdMember& m) {
    Value::Array a;
    std::apply([&](const auto&... v) { (a.push_back(scalar(v)), ...); }, memberFields(m));
    return a;
}
RTSCrowdMember member(const Value& v) {
    RTSCrowdMember m;
    const auto     fields = memberFields(m);
    const auto&    a      = tuple(v, std::tuple_size_v<decltype(fields)>);
    std::size_t    i      = 0;
    std::apply([&](auto&... x) { (read(a[i++], x), ...); }, fields);
    return m;
}
void members(const std::vector<RTSCrowdMember>& members, const crowd::Crowd::Snapshot& snapshot,
             const crowd::AgentFrame& frame) {
    if (members.size() > 1000000) invalid("Too many members");
    std::set<std::size_t> claimed;
    SubjectRef            previous;
    for (const auto& m : members) {
        if (!std::isfinite(m.radius) || m.radius < 0 || !std::isfinite(m.heading) || !std::isfinite(m.x) ||
            !std::isfinite(m.y))
            invalid("Invalid member geometry");
        if (!m.subject.isValid() || (previous.isValid() && !(previous.persistentId() < m.subject.persistentId())))
            invalid("Invalid member ordering");
        previous = m.subject;
        if (m.frameIndex) {
            if (*m.frameIndex >= frame.agents.size() || !claimed.insert(*m.frameIndex).second)
                invalid("Invalid/duplicate member agent row");
            const auto  named = take(snapshot.resolveNamedAgent(m.bindingKey));
            const auto& agent = frame.agents[*m.frameIndex];
            if (named != agent.reference ||
                (!m.container.isValid() && !m.key.empty() && (m.x != agent.state.x || m.y != agent.state.y)))
                invalid("Member binding key/pose differs");
        } else if (!m.bindingKey.empty())
            invalid("Unbound member retained binding key");
    }
    if (claimed.size() != frame.agents.size()) invalid("Backend has unowned agents");
}
}  // namespace crowd_archive
}  // namespace
Result<SnapshotEnvelope> RTSCrowdCapture::seal(Revision revision, SimulationTick tick,
                                               const SnapshotHashProvider& hash) const {
    using R = Result<SnapshotEnvelope>;
    if (!impl_ || impl_->epoch.isNil() || revision.isZero())
        return reject<SnapshotEnvelope>(DiagnosticCode::PreconditionViolation, "Empty Crowd capture or save boundary");
    try {
        const auto epoch = impl_->epoch;
        auto       frame = crowd_archive::take(impl_->snapshot.frame());
        crowd_archive::members(impl_->members, impl_->snapshot, frame);
        auto         backend = crowd_archive::take(impl_->snapshot.encodeState());
        Value::Array members;
        for (const auto& m : impl_->members) members.push_back(crowd_archive::member(m));
        Value nav;
        if (impl_->navigation) nav = crowd_archive::navigation(*impl_->navigation, impl_->backendOwner);
        Value payload = Value::object({{"backend", std::move(backend)},
                                       {"members", std::move(members)},
                                       {"clearFlowBindings", impl_->clearFlowBindings},
                                       {"navigation", std::move(nav)}});
        if (impl_->navigation) {
            auto validated =
                crowd_archive::navigation(payload.getIf<Value::Object>()->at("navigation"), impl_->backendOwner);
            if (!impl_->navigation->work || impl_->navigation->work->size() != validated->jobs.size())
                crowd_archive::invalid("Navigation work count differs");
            crowd_archive::validateWork(*impl_->navigation, impl_->snapshot, impl_->backendOwner);
            // Last source access: nested seal copies its complete facts before invoking unknown digest callbacks.
            auto  work    = crowd_archive::take(impl_->navigation->work->seal(epoch, revision, tick, hash));
            auto  encoded = crowd_archive::take(snapshotEnvelopeValue(work));
            auto& a       = *payload.getIf<Value::Object>()->at("navigation").getIf<Value::Array>();
            a[14]         = std::move(encoded);
        }
        return makeSnapshotEnvelope("eve.rts-crowd", *LogicalId::fromParts("rts", "crowd"), SchemaVersion(1), epoch,
                                    revision, tick, std::move(payload), hash);
    } catch (const crowd_archive::Invalid& error) {
        return R::failure(error.status);
    }
}
Result<RTSCrowdCapture> RTSCrowdCapture::parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                               const SnapshotHashProvider& hash) {
    using R       = Result<RTSCrowdCapture>;
    auto verified = verifySnapshotEnvelope(snapshot, hash);
    if (!verified) return R::failure(verified.status());
    if (snapshot.schemaVersion != SchemaVersion(1))
        return reject<RTSCrowdCapture>(DiagnosticCode::UnknownVersion, "Unknown RTS Crowd version");
    if (epoch.isNil() || snapshot.instanceId != epoch || snapshot.revision.isZero() ||
        snapshot.type != "eve.rts-crowd" || snapshot.schema != *LogicalId::fromParts("rts", "crowd"))
        return reject<RTSCrowdCapture>(DiagnosticCode::Conflict, "RTS Crowd boundary mismatch");
    try {
        const auto* root = snapshot.payload.getIf<Value::Object>();
        if (!root || root->size() != 4 || !root->contains("backend") || !root->contains("members") ||
            !root->contains("clearFlowBindings") || !root->contains("navigation"))
            crowd_archive::invalid("Unknown or missing RTS Crowd fields");
        auto data               = std::make_unique<Impl>();
        data->epoch             = epoch;
        data->snapshot          = crowd_archive::take(crowd::Crowd::Snapshot::prepareState(root->at("backend")));
        data->backendOwner      = data->snapshot.referenceScope();
        data->frame             = crowd_archive::take(data->snapshot.frame());
        data->clearFlowBindings = crowd_archive::boolean(root->at("clearFlowBindings"));
        for (const auto& row : crowd_archive::array(root->at("members")))
            data->members.push_back(crowd_archive::member(row));
        crowd_archive::members(data->members, data->snapshot, data->frame);
        if (!root->at("navigation").isNull()) {
            data->navigation = crowd_archive::navigation(root->at("navigation"), data->backendOwner);
            const auto& a    = crowd_archive::tuple(root->at("navigation"), 15);
            auto        work = crowd_archive::take(parseSnapshotEnvelopeValue(a[14], hash));
            if (work.revision != snapshot.revision || work.tick != snapshot.tick)
                crowd_archive::invalid("Navigation save boundary differs");
            data->navigation->work.emplace(
                crowd_archive::take(data->snapshot.parseNavigation(std::move(work), epoch, hash)));
            crowd_archive::validateWork(*data->navigation, data->snapshot, data->backendOwner);
        }
        return R::success(RTSCrowdCapture(std::move(data)));
    } catch (const crowd_archive::Invalid& error) {
        return R::failure(error.status);
    }
}
Result<RTSCrowdCapture> RTS::captureCrowd(PersistentId epoch) const {
    using R = Result<RTSCrowdCapture>;
    if (epoch.isNil() || !ownedWorld_ || ownedWorld_->is_deferred() || !hasLiveCrowdProvider())
        return reject<RTSCrowdCapture>(DiagnosticCode::PreconditionViolation,
                                       "Crowd capture needs epoch, idle owned Table and live provider");
    ecs::ScopedTable scope(*ownedWorld_);
    auto             snapshot = crowd_->captureSnapshot();
    if (!snapshot) return R::failure(snapshot.status());
    auto result      = std::make_unique<RTSCrowdCapture::Impl>();
    result->epoch    = epoch;
    result->snapshot = std::move(snapshot).takeValue();
    auto copied      = crowd_->copyAgentFrame(result->frame);
    if (!copied) return R::failure(copied.status());
    using Key = std::pair<std::uint64_t, crowd::AgentHandle>;
    std::map<Key, std::size_t> indices;
    for (std::size_t i = 0; i < result->frame.agents.size(); ++i) {
        const auto ref = result->frame.agents[i].reference;
        if (!indices.emplace(Key{ref.owner, ref.handle}, i).second)
            return reject<RTSCrowdCapture>(DiagnosticCode::Conflict, "Duplicate Crowd agent");
    }
    std::set<PersistentId> subjects;
    for (auto handle : units_) {
        auto* unit = handle.table == ownedWorld_.get() ? dynamic_cast<Unit*>(ecs::try_get(handle)) : nullptr;
        if (!unit) return reject<RTSCrowdCapture>(DiagnosticCode::StaleHandle, "Owned Crowd Unit retired");
        RTSCrowdMember member;
        member.subject          = snapshot_internal::existingSubject(*unit);
        const auto* settings    = detail::existingComponent<Unit::Crowd>(*unit);
        const auto* motion      = detail::existingComponent<Unit::Motion>(*unit);
        const auto* containment = detail::existingComponent<Unit::Containment>(*unit);
        if (!member.subject.isValid() || !subjects.insert(member.subject.persistentId()).second || !settings ||
            !motion || !containment)
            return reject<RTSCrowdCapture>(DiagnosticCode::Conflict, "Missing or duplicate Crowd Unit facts");
        if (containment->container.isBound()) {
            if (containment->container.handle().table != ownedWorld_.get())
                return reject<RTSCrowdCapture>(DiagnosticCode::Conflict, "Foreign Crowd container");
            member.container = snapshot_internal::subjectOf(containment->container.handle());
            if (!member.container.isValid())
                return reject<RTSCrowdCapture>(DiagnosticCode::StaleHandle, "Crowd container retired");
        }
        member.key     = settings->link.key();
        member.radius  = settings->radius;
        member.heading = settings->heading;
        member.x       = motion->x;
        member.y       = motion->y;
        if (!std::isfinite(member.radius) || member.radius < 0.f || !std::isfinite(member.heading) ||
            !std::isfinite(member.x) || !std::isfinite(member.y))
            return reject<RTSCrowdCapture>(DiagnosticCode::InvalidArgument, "Invalid Crowd Unit geometry");
        crowd::AgentRef ref{settings->runtimeAgent, settings->runtimeOwner};
        if (crowd_->isAgentAlive(ref)) {
            member.bindingKey = settings->runtimeBindingKey;
        } else if (settings->link.isBound() && !member.container.isValid()) {
            auto resolved = crowd_->resolveNamedAgent(member.key);
            if (resolved) {
                ref               = resolved.value();
                member.bindingKey = member.key;
            }
        }
        if (crowd_->isAgentAlive(ref)) {
            auto found = indices.find({ref.owner, ref.handle});
            if (found == indices.end())
                return reject<RTSCrowdCapture>(DiagnosticCode::Conflict, "Agent claimed by multiple Units");
            member.frameIndex = found->second;
            const auto named  = crowd_->resolveNamedAgent(member.bindingKey);
            if (!named || named.value().owner != ref.owner || named.value().handle != ref.handle)
                return reject<RTSCrowdCapture>(DiagnosticCode::Conflict,
                                               "Crowd binding cache has no canonical named identity");
            const auto& agent = result->frame.agents[found->second].state;
            if (!member.container.isValid() && !member.key.empty() && (agent.x != member.x || agent.y != member.y))
                return reject<RTSCrowdCapture>(DiagnosticCode::Conflict, "Crowd Unit pose differs");
            indices.erase(found);
        }
        result->members.push_back(std::move(member));
    }
    if (!indices.empty()) return reject<RTSCrowdCapture>(DiagnosticCode::Conflict, "Crowd scope has unowned agents");
    std::sort(result->members.begin(), result->members.end(),
              [](const auto& a, const auto& b) { return a.subject.persistentId() < b.subject.persistentId(); });
    result->backendOwner      = crowd_->getFieldStatus().owner;
    result->clearFlowBindings = clearCrowdFlowBindings_;
    if (crowdFlowNavigation_) {
        const auto& nav      = *crowdFlowNavigation_;
        auto        frozen   = std::make_unique<FrozenFlowNavigation>();
        frozen->settings     = nav.settings_;
        frozen->enabled      = nav.enabled_;
        frozen->frame        = nav.frame_;
        frozen->pumped       = nav.pumped_;
        frozen->owner        = nav.owner_;
        frozen->input        = nav.input_;
        frozen->nextSequence = nav.nextSequence_;
        frozen->workAfter    = nav.workAfter_;
        frozen->roundThrough = nav.roundThrough_;
        frozen->stats        = nav.stats_;
        std::vector<crowd::Crowd::NavigationWorkView> work;
        work.reserve(nav.jobs_.size());
        frozen->jobs.reserve(nav.jobs_.size());
        for (const auto& job : nav.jobs_) {
            frozen->jobs.push_back({job.sequence, job.goals, job.used});
            work.push_back({std::cref(job.plan), std::cref(job.lease)});
        }
        auto captured = result->snapshot.captureNavigation(work);
        if (!captured) return R::failure(captured.status());
        frozen->work.emplace(std::move(captured).takeValue());
        // Absent targets retain a non-resolving relationship and never claim a future replacement.
        auto subject = [&](ecs::EntityHandle handle) -> Result<std::optional<FrozenFlowSubject>> {
            if (!handle.table) return Result<std::optional<FrozenFlowSubject>>::success(std::nullopt);
            if (handle.table != ownedWorld_.get())
                return reject<std::optional<FrozenFlowSubject>>(DiagnosticCode::Conflict,
                                                                "Foreign shared-flow relationship");
            if (!ecs::try_get(handle)) return Result<std::optional<FrozenFlowSubject>>::success(std::nullopt);
            const auto id    = snapshot_internal::subjectOf(handle);
            auto       owned = resolveOwnedSubject(id);
            if (!owned) return Result<std::optional<FrozenFlowSubject>>::failure(owned.status());
            if (!same(owned.value(), handle))
                return reject<std::optional<FrozenFlowSubject>>(DiagnosticCode::Conflict,
                                                                "Shared-flow relationship is not owned");
            return Result<std::optional<FrozenFlowSubject>>::success(FrozenFlowSubject{id, handle.type});
        };
        for (const auto& group : nav.groups_) {
            auto unit = subject(group.member.unit);
            if (!unit) return R::failure(unit.status());
            frozen->groups.push_back({std::move(unit).takeValue(), group.member.orderId, group.goals, group.reform,
                                      group.owner, group.input});
        }
        for (const auto& notice : nav.notices_) {
            auto unit = subject(notice.unit);
            if (!unit) return R::failure(unit.status());
            auto target = subject(notice.order.targetEntity);
            if (!target) return R::failure(target.status());
            if (unit.value() && notice.order.targetEntity.table != nullptr && !target.value())
                return reject<RTSCrowdCapture>(DiagnosticCode::NotFound,
                                               "Live shared-flow notice has a retired order target");
            auto order         = notice.order;
            order.targetEntity = {};
            frozen->notices.push_back({std::move(unit).takeValue(), std::move(target).takeValue(), std::move(order)});
        }
        result->navigation = std::move(frozen);
    }
    return R::success(RTSCrowdCapture(std::move(result)));
}
struct PreparedRTSCrowdRestore::Impl {
    /** @ownership Observed target facade, guarded by physical witness and weak Table.
     * @lifetime Not dereferenced during destruction; publication requires the original live target. */
    RTS*                      target;
    LifetimeWitness           lifetime;
    std::weak_ptr<ecs::Table> world;
    /** @ownership Observed backend, guarded by physical provider witness before any use.
     * @lifetime The game owns this backend; preparation does not extend its lifetime. */
    crowd::Crowd*                  provider;
    LifetimeWitness                providerLifetime;
    crowd::Crowd::RestorePlan      backend;
    std::vector<ecs::EntityHandle> topology;
    struct Member {
        ecs::EntityHandle target;
        RTSCrowdMember    saved;
        Unit::Crowd       candidate;
    };
    std::vector<Member>                                   members;
    std::unique_ptr<CrowdFlowNavigation>                  navigation;
    bool                                                  clearFlowBindings = false;
    std::vector<std::pair<ecs::EntityHandle, SubjectRef>> navigationTargets;
};
PreparedRTSCrowdRestore::PreparedRTSCrowdRestore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedRTSCrowdRestore::~PreparedRTSCrowdRestore()                                                  = default;
PreparedRTSCrowdRestore::PreparedRTSCrowdRestore(PreparedRTSCrowdRestore&&) noexcept                 = default;
PreparedRTSCrowdRestore&      PreparedRTSCrowdRestore::operator=(PreparedRTSCrowdRestore&&) noexcept = default;
Result<crowd::FlowRequestRef> PreparedRTSCrowdRestore::remapFlowRequest(crowd::FlowRequestRef source) const {
    if (!impl_) return reject<crowd::FlowRequestRef>(DiagnosticCode::PreconditionViolation, "Crowd candidate consumed");
    return impl_->backend.remapFlowRequest(source);
}
Result<PreparedRTSCrowdRestore> PreparedRTSCrowdRestore::prepare(RTS& target, const RTSCrowdCapture& captured) {
    using R    = Result<PreparedRTSCrowdRestore>;
    auto world = target.ownedWorld_;
    if (!captured.impl_ || !world || world->is_deferred() || !target.hasLiveCrowdProvider())
        return reject<PreparedRTSCrowdRestore>(DiagnosticCode::PreconditionViolation,
                                               "Crowd restore needs facts, idle world and live provider");
    if (target.crowdFlowNavigation_ || target.clearCrowdFlowBindings_ || target.crowd_->getAgentCount() != 0 ||
        target.units_.size() != captured.members().size())
        return reject<PreparedRTSCrowdRestore>(DiagnosticCode::Conflict,
                                               "Crowd restore requires empty backend and exact Unit topology");
    ecs::ScopedTable                          scope(*world);
    std::map<PersistentId, ecs::EntityHandle> targets;
    for (auto handle : target.units_) {
        auto subject = snapshot_internal::subjectOf(handle);
        if (handle.table != world.get() || !subject.isValid() ||
            !targets.emplace(subject.persistentId(), handle).second)
            return reject<PreparedRTSCrowdRestore>(DiagnosticCode::Conflict, "Invalid target Crowd topology");
    }
    auto backend = target.crowd_->prepareRestore(captured.impl_->snapshot);
    if (!backend) return R::failure(backend.status());
    auto plan              = std::make_unique<Impl>();
    plan->target           = &target;
    plan->lifetime         = target.lifetimeWitness();
    plan->world            = world;
    plan->provider         = target.crowd_;
    plan->providerLifetime = target.crowdLifetime_;
    plan->backend          = std::move(backend).takeValue();
    plan->topology         = target.units_;
    plan->members.reserve(captured.members().size());
    for (const auto& saved : captured.members()) {
        const auto found = targets.find(saved.subject.persistentId());
        if (found == targets.end())
            return reject<PreparedRTSCrowdRestore>(DiagnosticCode::Conflict, "Target Crowd Unit missing");
        auto checked = destination(found->second, saved);
        if (!checked) return R::failure(checked.status());
        Unit::Crowd candidate;
        candidate.link    = CrowdLink::fromKeyForRestore(saved.key);
        candidate.radius  = saved.radius;
        candidate.heading = saved.heading;
        if (saved.frameIndex) {
            auto agent = plan->backend.inspectAgent(*saved.frameIndex);
            if (!agent) return R::failure(agent.status());
            candidate.runtimeAgent      = agent.value().reference.handle;
            candidate.runtimeOwner      = agent.value().reference.owner;
            candidate.runtimeBindingKey = saved.bindingKey;
        }
        plan->members.push_back({found->second, saved, std::move(candidate)});
    }
    plan->clearFlowBindings = captured.impl_->clearFlowBindings;
    if (captured.impl_->navigation) {
        const auto& frozen = *captured.impl_->navigation;
        auto        native = plan->backend.prepareNavigation(*frozen.work);
        if (!native) return R::failure(native.status());
        if (native.value().size() != frozen.jobs.size())
            return reject<PreparedRTSCrowdRestore>(DiagnosticCode::Conflict, "Shared-flow native job scope differs");
        auto       nav   = std::make_unique<CrowdFlowNavigation>(frozen.settings);
        const auto owner = [&](std::uint64_t old) {
            return old == captured.impl_->backendOwner ? plan->backend.owner() : old;
        };
        nav->enabled_      = frozen.enabled;
        nav->owner_        = owner(frozen.owner);
        nav->input_        = frozen.input;
        nav->frame_        = frozen.frame;
        nav->pumped_       = frozen.pumped;
        nav->nextSequence_ = frozen.nextSequence;
        nav->workAfter_    = frozen.workAfter;
        nav->roundThrough_ = frozen.roundThrough;
        nav->stats_        = frozen.stats;
        nav->jobs_.reserve(frozen.jobs.size());
        for (std::size_t i = 0; i < frozen.jobs.size(); ++i) {
            const auto& job = frozen.jobs[i];
            nav->jobs_.push_back({job.sequence, job.goals, job.used, std::move(native.value()[i].plan),
                                  std::move(native.value()[i].lease)});
        }
        auto resolve = [&](const std::optional<FrozenFlowSubject>& id) -> Result<ecs::EntityHandle> {
            if (!id) return Result<ecs::EntityHandle>::success({});
            auto handle = target.resolveOwnedSubject(id->subject);
            if (!handle) return handle;
            if (handle.value().type != id->type)
                return reject<ecs::EntityHandle>(DiagnosticCode::Conflict, "Shared-flow target root family changed");
            plan->navigationTargets.emplace_back(handle.value(), id->subject);
            return handle;
        };
        for (const auto& group : frozen.groups) {
            auto handle = resolve(group.unit);
            if (!handle) return R::failure(handle.status());
            nav->groups_.push_back(
                {{handle.value(), group.order}, group.goals, group.reform, owner(group.owner), group.input});
        }
        std::sort(nav->groups_.begin(), nav->groups_.end(), [](const auto& a, const auto& b) {
            return CrowdFlowNavigation::handleLess(a.member.unit, b.member.unit);
        });
        for (const auto& notice : frozen.notices) {
            auto unit = resolve(notice.unit);
            if (!unit) return R::failure(unit.status());
            auto targetHandle = resolve(notice.target);
            if (!targetHandle) return R::failure(targetHandle.status());
            auto order         = notice.order;
            order.targetEntity = targetHandle.value();
            nav->notices_.push_back({unit.value(), std::move(order)});
        }
        plan->navigation = std::move(nav);
    }
    return R::success(PreparedRTSCrowdRestore(std::move(plan)));
}
Result<void> RTS::restoreCrowd(PreparedRTSCrowdRestore&& prepared) {
    auto* plan = prepared.impl_.get();
    if (!plan || plan->target != this || !plan->lifetime.isAlive())
        return reject<void>(DiagnosticCode::PreconditionViolation, "Crowd token consumed or foreign");
    auto world = plan->world.lock();
    if (!world || world != ownedWorld_ || world->is_deferred() || !hasLiveCrowdProvider() || plan->provider != crowd_ ||
        !plan->providerLifetime.isAlive())
        return reject<void>(DiagnosticCode::PreconditionViolation, "Crowd target or provider retired, changed or busy");
    if (crowdFlowNavigation_ || clearCrowdFlowBindings_ || crowd_->getAgentCount() != 0 ||
        units_.size() != plan->topology.size())
        return reject<void>(DiagnosticCode::Conflict, "Crowd target claimed after preparation");
    ecs::ScopedTable scope(*world);
    for (std::size_t i = 0; i < units_.size(); ++i)
        if (!same(units_[i], plan->topology[i]))
            return reject<void>(DiagnosticCode::StaleHandle, "Crowd topology replaced");
    for (const auto& member : plan->members) {
        auto checked = destination(member.target, member.saved);
        if (!checked) return Result<void>::failure(checked.status());
    }
    for (const auto& [handle, subject] : plan->navigationTargets)
        if (snapshot_internal::subjectOf(handle) != subject)
            return reject<void>(DiagnosticCode::StaleHandle, "Shared-flow target generation changed");
    auto committed = crowd_->commitRestore(std::move(plan->backend));
    if (!committed) return committed;
    static_assert(std::is_nothrow_swappable_v<Unit::Crowd>);
    for (auto& member : plan->members) {
        auto* unit     = static_cast<Unit*>(ecs::try_get(member.target));
        auto* settings = detail::existingComponent<Unit::Crowd>(*unit);
        using std::swap;
        swap(*settings, member.candidate);
    }
    crowdFlowNavigation_.swap(plan->navigation);
    clearCrowdFlowBindings_ = plan->clearFlowBindings;
    prepared.impl_.reset();
    return Result<void>::success(Status::success(StatusCode::Applied));
}
}  // namespace eve::rts
