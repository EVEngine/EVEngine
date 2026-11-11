#include "rpg/SimulationEventArchive.h"
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include "rpg/RPGActor.h"

namespace eve::rpg {
namespace {
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message, DiagnosticCode code = DiagnosticCode::ParseError) {
    throw Invalid{Status::failure(Diagnostic::error(code, message, "rpg.simulation-events"))};
}
LogicalId           schema() { return *LogicalId::fromParts("rpg", "simulation-events"); }
const Value::Array& array(const Value& value) {
    auto* p = value.getIf<Value::Array>();
    if (!p) invalid("Expected queue array");
    return *p;
}
const Value::Array& row(const Value& value, std::size_t size) {
    const auto& a = array(value);
    if (a.size() != size) invalid("Unknown or missing tuple fields");
    return a;
}
const std::string& text(const Value& value, bool required = false) {
    auto* p = value.getIf<std::string>();
    if (!p || (required && p->empty())) invalid("Expected event text");
    return *p;
}
int integer(const Value& value, int minimum) {
    if (!value.isInt64() || value.asInt() < minimum || value.asInt() > std::numeric_limits<int>::max())
        invalid("Event integer out of range");
    return static_cast<int>(value.asInt());
}
double number(const Value& value) {
    if (!value.isInt64() && !value.isDouble()) invalid("Expected event number");
    const double n = value.isDouble() ? value.asDouble() : static_cast<double>(value.asInt());
    if (!std::isfinite(n) || n < 0) invalid("Invalid event number");
    if (value.isInt64() && (n >= 0x1p63 || static_cast<std::int64_t>(n) != value.asInt()))
        invalid("Event integer cannot be represented exactly");
    return n;
}
const std::string& action(const Value& value, std::initializer_list<std::string_view> allowed) {
    const auto& s = text(value);
    for (auto a : allowed)
        if (s == a) return s;
    invalid("Unknown event action");
}
struct Requirements {
    std::map<std::string, SubjectRef> subjects;
    std::set<std::string>             skills, effects;
};
struct SavedRef {
    std::string kind;
    SubjectRef  subject;
};
SavedRef reference(const Value& value, bool optional, Requirements& requirements) {
    const auto& a = row(value, 2);
    SavedRef    ref{text(a[0]), {}};
    if (ref.kind == "none") {
        if (!optional || !a[1].isNull()) invalid("Absent event actor is not permitted");
        return ref;
    }
    if (ref.kind != "linked" && ref.kind != "retired") invalid("Unknown actor reference kind");
    const auto& id     = text(a[1]);
    auto        parsed = PersistentId::parse(id);
    if (!parsed || parsed->isNil() || parsed->format() != id) invalid("Invalid canonical actor subject");
    ref.subject = SubjectRef::fromPersistentId(*parsed);
    if (ref.kind == "linked") requirements.subjects.emplace(id, ref.subject);
    return ref;
}
template <bool Build, class Resolve>
detail::SimulationEvents decode(const Value& payload, Requirements& requirements, Resolve&& resolve) {
    auto* object = payload.getIf<Value::Object>();
    if (!object || object->size() != 5) invalid("Unknown or missing event queues");
    auto queue = [&](const char* name) -> const Value::Array& {
        auto it = object->find(name);
        if (it == object->end()) invalid("Missing event queue");
        return array(it->second);
    };
    auto ref = [&](const Value& value, bool optional = false) {
        auto saved = reference(value, optional, requirements);
        if constexpr (Build)
            return resolve(saved);
        else
            return ActorEventRef{};
    };
    detail::SimulationEvents events;
    for (const auto& value : queue("casts")) {
        const auto&    a = row(value, 3);
        SkillCastEvent event{ref(a[0]), ref(a[1], true), text(a[2], true)};
        requirements.skills.insert(event.skillId);
        if constexpr (Build) events.casts.push_back(std::move(event));
    }
    for (const auto& value : queue("ticks")) {
        const auto&     a = row(value, 5);
        StatusTickEvent event{ref(a[0]), integer(a[1], 1), text(a[2], true), text(a[3]), integer(a[4], 1)};
        requirements.effects.insert(event.effectId);
        if constexpr (Build) events.ticks.push_back(std::move(event));
    }
    for (const auto& value : queue("changes")) {
        const auto&       a = row(value, 7);
        StatusChangeEvent event{ref(a[0]),
                                integer(a[1], 0),
                                text(a[2], true),
                                text(a[3]),
                                action(a[4], {"apply", "refresh", "extend", "stack", "remove", "expire", "reject"}),
                                integer(a[5], 0),
                                text(a[6])};
        requirements.effects.insert(event.effectId);
        if constexpr (Build) events.changes.push_back(std::move(event));
    }
    for (const auto& value : queue("vitals")) {
        const auto& a = row(value, 7);
        VitalsEvent event{ref(a[0]),    text(a[1], true), action(a[2], {"damage", "heal", "death", "revive"}),
                          number(a[3]), text(a[4]),       number(a[5]),
                          number(a[6])};
        if constexpr (Build) events.vitals.push_back(std::move(event));
    }
    for (const auto& value : queue("levels")) {
        const auto&  a = row(value, 3);
        LevelUpEvent event{ref(a[0]), integer(a[1], 1), integer(a[2], 1)};
        if (static_cast<std::int64_t>(event.previousLevel) + 1 != event.newLevel) invalid("Nonconsecutive level event");
        if constexpr (Build) events.levels.push_back(std::move(event));
    }
    return events;
}
}  // namespace
struct SimulationEventArchive::Impl {
    PersistentId             epoch;
    Revision                 revision;
    SimulationTick           tick;
    Value                    payload;
    std::vector<SubjectRef>  subjects;
    std::vector<std::string> skills, effects;
    bool                     sealing = false;
    void                     validate() {
        Requirements requirements;
        decode<false>(payload, requirements, [](const SavedRef&) { return ActorEventRef{}; });
        for (const auto& [id, subject] : requirements.subjects) subjects.push_back(subject);
        skills.assign(requirements.skills.begin(), requirements.skills.end());
        effects.assign(requirements.effects.begin(), requirements.effects.end());
    }
};
SimulationEventArchive::SimulationEventArchive(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
SimulationEventArchive::~SimulationEventArchive()                                                   = default;
SimulationEventArchive::SimulationEventArchive(SimulationEventArchive&&) noexcept                   = default;
SimulationEventArchive&        SimulationEventArchive::operator=(SimulationEventArchive&&) noexcept = default;
Result<SimulationEventArchive> SimulationEventArchive::capture(const detail::SimulationEvents& events,
                                                               PersistentId epoch, Revision revision,
                                                               SimulationTick tick) {
    try {
        if (epoch.isNil()) invalid("Joint save epoch must be non-nil", DiagnosticCode::InvalidArgument);
        ecs::TableLifetime                       affiliation;
        bool                                     hasAffiliation = false;
        std::map<std::string, ecs::EntityHandle> live;
        auto                                     ref = [&](const ActorEventRef& actor) -> Value {
            const auto presence = actor.presence();
            if (presence == ActorEventPresence::None) return Value::Array{"none", Value{}};
            if (!actor.world_.sameOwner(actor.world_)) invalid("Missing world affiliation");
            if (hasAffiliation && !affiliation.sameOwner(actor.world_))
                invalid("Mixed event worlds", DiagnosticCode::Conflict);
            affiliation        = actor.world_;
            hasAffiliation     = true;
            const auto subject = actor.capturedSubject();
            if (!subject) invalid("Anonymous historical event actor", DiagnosticCode::Unsupported);
            if (presence == ActorEventPresence::Live) {
                auto resolved = actor.resolve();
                if (!resolved || actorSubject(resolved->get()) != subject)
                    invalid("Event identity changed", DiagnosticCode::Conflict);
                auto [it, inserted] = live.emplace(subject->format(), actor.handle());
                const auto h   = actor.handle();
                const auto old = it->second;
                if (!inserted &&
                    (old.table != h.table || old.type != h.type || old.id != h.id || old.generation != h.generation))
                    invalid("Duplicate live event identity", DiagnosticCode::Conflict);
            }
            return Value::Array{presence == ActorEventPresence::Live ? "linked" : "retired", subject->format()};
        };
        Value::Array casts, ticks, changes, vitals, levels;
        for (const auto& e : events.casts) casts.emplace_back(Value::Array{ref(e.caster), ref(e.target), e.skillId});
        for (const auto& e : events.ticks)
            ticks.emplace_back(Value::Array{ref(e.actor), e.instanceId, e.effectId, e.source, e.stacks});
        for (const auto& e : events.changes)
            changes.emplace_back(
                Value::Array{ref(e.actor), e.instanceId, e.effectId, e.source, e.action, e.stacks, e.reason});
        for (const auto& e : events.vitals)
            vitals.emplace_back(Value::Array{ref(e.actor), e.resource, e.action, e.amount, e.source, e.current, e.max});
        for (const auto& e : events.levels)
            levels.emplace_back(Value::Array{ref(e.actor), e.previousLevel, e.newLevel});
        auto state      = std::make_shared<Impl>();
        state->epoch    = epoch;
        state->revision = revision;
        state->tick     = tick;
        state->payload  = Value::Object{{"casts", std::move(casts)},
                                        {"ticks", std::move(ticks)},
                                        {"changes", std::move(changes)},
                                        {"vitals", std::move(vitals)},
                                        {"levels", std::move(levels)}};
        state->validate();
        return Result<SimulationEventArchive>::success(SimulationEventArchive(std::move(state)));
    } catch (const Invalid& e) {
        return Result<SimulationEventArchive>::failure(e.status);
    }
}
Result<SimulationEventArchive> SimulationEventArchive::parse(const SnapshotEnvelope& input, PersistentId epoch,
                                                             const SnapshotHashProvider& hash) {
    try {
        auto snapshot = input;
        if (epoch.isNil() || snapshot.instanceId != epoch)
            invalid("Joint save epoch mismatch", DiagnosticCode::Conflict);
        if (snapshot.type != "rpg.simulation-events" || snapshot.schema != schema()) invalid("Wrong event schema");
        if (snapshot.schemaVersion != SchemaVersion(1))
            invalid("Unknown event version", DiagnosticCode::UnknownVersion);
        auto verified = verifySnapshotEnvelope(snapshot, hash);
        if (!verified) return Result<SimulationEventArchive>::failure(verified.status());
        auto state      = std::make_shared<Impl>();
        state->epoch    = epoch;
        state->revision = snapshot.revision;
        state->tick     = snapshot.tick;
        state->payload  = std::move(snapshot.payload);
        state->validate();
        return Result<SimulationEventArchive>::success(SimulationEventArchive(std::move(state)));
    } catch (const Invalid& e) {
        return Result<SimulationEventArchive>::failure(e.status);
    }
}
Result<SnapshotEnvelope> SimulationEventArchive::seal(const SnapshotHashProvider& hash) const {
    auto state = impl_;
    if (!state || state->sealing)
        return Result<SnapshotEnvelope>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation, "Moved or reentrant event archive", "rpg.simulation-events"));
    state->sealing = true;
    struct Guard {
        std::shared_ptr<Impl> state;
        ~Guard() { state->sealing = false; }
    } guard{state};
    return makeSnapshotEnvelope("rpg.simulation-events", schema(), SchemaVersion(1), state->epoch, state->revision,
                                state->tick, state->payload, hash);
}
Result<detail::SimulationEvents> SimulationEventArchive::prepareRestore(ecs::Table& destination) const {
    try {
        auto state = impl_;
        if (!state || state->sealing || destination.is_deferred())
            invalid("Moved, reentrant or deferred event restore", DiagnosticCode::PreconditionViolation);
        std::map<std::string, ecs::EntityHandle> actors;
        if (!state->subjects.empty() && destination.getManager<RPGActor>()) {
            auto view = ecs::View<RPGActor>(destination);
            for (auto it = view.begin(); it != view.end(); ++it) {
                const auto subject = actorSubject(*it.entity());
                if (subject && !actors.emplace(subject->format(), ecs::handle_of(it.entity())).second)
                    invalid("Duplicate destination actor identity", DiagnosticCode::Conflict);
            }
        }
        auto resolve = [&](const SavedRef& saved) {
            ActorEventRef result;
            if (saved.kind == "none") return result;
            if (saved.kind == "retired") {
                result.specified_ = true;
                result.subject_   = saved.subject;
                result.world_     = destination.lifetime();
                return result;
            }
            auto it = actors.find(saved.subject.format());
            if (it == actors.end()) invalid("Missing destination event actor", DiagnosticCode::NotFound);
            auto* actor = dynamic_cast<RPGActor*>(ecs::try_get(it->second));
            if (!actor || actorSubject(*actor) != std::optional<SubjectRef>{saved.subject})
                invalid("Stale destination event actor", DiagnosticCode::StaleHandle);
            return ActorEventRef(*actor);
        };
        Requirements requirements;
        return Result<detail::SimulationEvents>::success(decode<true>(state->payload, requirements, resolve));
    } catch (const Invalid& e) {
        return Result<detail::SimulationEvents>::failure(e.status);
    }
}
std::array<std::size_t, 5> SimulationEventArchive::queueSizes() const noexcept {
    if (!impl_) return {};
    std::array<std::size_t, 5> result{};
    // No temporary std::string construction in this noexcept query.
    for (const auto& [key, value] : *impl_->payload.getIf<Value::Object>()) {
        const auto size = value.getIf<Value::Array>()->size();
        if (key == "casts")
            result[0] = size;
        else if (key == "ticks")
            result[1] = size;
        else if (key == "changes")
            result[2] = size;
        else if (key == "vitals")
            result[3] = size;
        else if (key == "levels")
            result[4] = size;
    }
    return result;
}
PersistentId   SimulationEventArchive::saveEpoch() const noexcept { return impl_ ? impl_->epoch : PersistentId{}; }
Revision       SimulationEventArchive::revision() const noexcept { return impl_ ? impl_->revision : Revision{}; }
SimulationTick SimulationEventArchive::tick() const noexcept { return impl_ ? impl_->tick : SimulationTick{}; }
std::span<const SubjectRef> SimulationEventArchive::linkedSubjects() const noexcept {
    return impl_ ? std::span<const SubjectRef>(impl_->subjects) : std::span<const SubjectRef>{};
}
std::span<const std::string> SimulationEventArchive::referencedSkills() const noexcept {
    return impl_ ? std::span<const std::string>(impl_->skills) : std::span<const std::string>{};
}
std::span<const std::string> SimulationEventArchive::referencedEffects() const noexcept {
    return impl_ ? std::span<const std::string>(impl_->effects) : std::span<const std::string>{};
}
}  // namespace eve::rpg
