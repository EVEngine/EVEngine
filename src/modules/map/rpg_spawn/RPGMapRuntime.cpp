// eve-hot-path
// ECS: delegates Views to RPG systems; retires accepted death handles outside Views; canonical map/session writes.
// Phase: status -> skill -> lifecycle retain -> spawn -> explicit consumer acknowledgement.
// All simulation time is injected; RNG remains the one canonical SpawnWorld stream owner.
#include "map/rpg_spawn/RPGMapRuntime.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <tuple>
#include <type_traits>
#include "common/ListenerStaging.h"
#include "common/StateAccess.h"
#include "definitions/Definitions.h"
#include "map/Map.h"
#include "map/rpg_spawn/RPGMapArchive.h"
#include "map/rpg_spawn/RPGMapCheckpoint.h"
#include "map/rpg_spawn/RPGMapSession.h"
#include "map/spawn/MapSpawnLoad.h"
#include "rpg/Effect.h"
#include "rpg/RPGActor.h"
#include "rpg/SimulationEventArchive.h"
#include "rpg/SkillContent.h"
#include "rpg/SkillSystem.h"
#include "rpg/StatusSystem.h"
#include "rpg/spawn/RPGSpawnCapture.h"
#include "rpg/spawn/RPGSpawnJointRestore.h"
#include "rpg/spawn/RPGSpawnRestoreState.h"
#include "rpg/spawn/RPGUnitCatalogue.h"
#include "spawn/SpawnDiagnostics.h"
#include "spawn/SpawnRestoreState.h"
#include "spawn/definitions/SpawnTemplateSource.h"
namespace eve::map_rpg_spawn {
namespace {
using Counters = std::array<std::uint64_t, 5>;
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* message, DiagnosticCode code = DiagnosticCode::PreconditionViolation) {
    throw Invalid{Status::failure(Diagnostic::error(code, message, "map.rpg.runtime"))};
}
struct Guard {
    bool& busy;
    explicit Guard(bool& value) : busy(value) { busy = true; }
    ~Guard() { busy = false; }
};
LogicalId schema() { return *LogicalId::fromParts("map", "rpg-runtime"); }
Counters  sizes(const rpg::detail::SimulationEvents& q) {
    return {q.casts.size(), q.ticks.size(), q.changes.size(), q.vitals.size(), q.levels.size()};
}
Counters ends(const Counters& base, const rpg::detail::SimulationEvents& q) {
    auto result = sizes(q);
    for (std::size_t i = 0; i < 5; ++i) {
        if (result[i] > UINT64_MAX - base[i]) invalid("Event sequence exhausted");
        result[i] += base[i];
    }
    return result;
}
std::uint64_t counter(const Value& value) {
    auto* text = value.getIf<std::string>();
    if (!text) invalid("Expected decimal counter", DiagnosticCode::ParseError);
    std::uint64_t n      = 0;
    const auto    parsed = std::from_chars(text->data(), text->data() + text->size(), n);
    if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size() || std::to_string(n) != *text)
        invalid("Invalid canonical counter", DiagnosticCode::ParseError);
    return n;
}
constexpr std::size_t activationLimit = 1024;
struct ActivationFacts {
    std::optional<std::vector<spawn::Point>> players;
    std::vector<spawn::SavedSpawnerKey>      events;
};
const ActivationFacts& facts(const std::shared_ptr<const ActivationFacts>& input) {
    static const ActivationFacts empty{std::vector<spawn::Point>{}, {}};
    return input ? *input : empty;
}
bool sameFacts(const std::shared_ptr<const ActivationFacts>& a, const std::shared_ptr<const ActivationFacts>& b) {
    const auto& x = facts(a);
    const auto& y = facts(b);
    if (x.events != y.events || x.players.has_value() != y.players.has_value()) return false;
    if (!x.players) return true;
    if (x.players->size() != y.players->size()) return false;
    for (std::size_t i = 0; i < x.players->size(); ++i)
        if ((*x.players)[i].x != (*y.players)[i].x || (*x.players)[i].y != (*y.players)[i].y) return false;
    return true;
}
void validatePlayers(std::span<const spawn::Point> players) {
    if (players.size() > activationLimit) invalid("Player input exceeds work cap", DiagnosticCode::InvalidArgument);
    for (auto point : players)
        if (!std::isfinite(point.x) || !std::isfinite(point.y))
            invalid("Nonfinite player position", DiagnosticCode::InvalidArgument);
}
Value encodePlayers(std::span<const spawn::Point> players) {
    Value::Array rows;
    rows.reserve(players.size());
    for (auto point : players) rows.emplace_back(Value::Array{point.x, point.y});
    return rows;
}
std::vector<spawn::Point> decodePlayers(const Value& value) {
    const auto* rows = value.getIf<Value::Array>();
    if (!rows || rows->size() > activationLimit) invalid("Invalid player array", DiagnosticCode::ParseError);
    auto coordinate = [](const Value& v) {
        if (!v.isInt64() && !v.isDouble()) invalid("Expected coordinate", DiagnosticCode::ParseError);
        const double n = v.isDouble() ? v.asDouble() : static_cast<double>(v.asInt());
        if (!std::isfinite(n) ||
            (v.isInt64() && (n >= 0x1p63 || n < -0x1p63 || static_cast<std::int64_t>(n) != v.asInt())))
            invalid("Invalid exact coordinate", DiagnosticCode::ParseError);
        return n;
    };
    std::vector<spawn::Point> result;
    result.reserve(rows->size());
    for (const auto& row : *rows) {
        const auto* xy = row.getIf<Value::Array>();
        if (!xy || xy->size() != 2) invalid("Invalid point tuple", DiagnosticCode::ParseError);
        result.push_back({coordinate((*xy)[0]), coordinate((*xy)[1])});
    }
    return result;
}
Value encodeActivation(const std::shared_ptr<const ActivationFacts>& input) {
    const auto&  value = facts(input);
    Value::Array events;
    for (const auto& key : value.events) {
        auto encoded = spawn::encodeSavedSpawnerKey(key);
        if (!encoded) throw Invalid{encoded.status()};
        events.push_back(std::move(encoded).takeValue());
    }
    return Value::object(
        {{"players", value.players ? encodePlayers(*value.players) : Value{}}, {"events", std::move(events)}});
}
std::shared_ptr<const ActivationFacts> decodeActivation(const Value& value) {
    const auto* object = value.getIf<Value::Object>();
    if (!object || object->size() != 2 || !object->contains("players") || !object->contains("events"))
        invalid("Unknown activation fields", DiagnosticCode::ParseError);
    auto result = std::make_shared<ActivationFacts>();
    if (!object->at("players").isNull()) result->players = decodePlayers(object->at("players"));
    const auto* events = object->at("events").getIf<Value::Array>();
    if (!events || events->size() > activationLimit)
        invalid("Invalid activation source array", DiagnosticCode::ParseError);
    std::set<std::tuple<PersistentId, std::uint64_t, std::uint32_t, std::uint32_t>> seen;
    for (const auto& row : *events) {
        auto parsed = spawn::prepareSavedSpawnerKey(row);
        if (!parsed) throw Invalid{parsed.status()};
        const auto key = std::move(parsed).takeValue();
        if (!seen.emplace(key.owner, key.epoch, key.slot, key.generation).second)
            invalid("Duplicate activation source", DiagnosticCode::Conflict);
        result->events.push_back(key);
    }
    return result;
}
struct Frame {
    Counters                                         acknowledged{};
    std::uint64_t                                    forwarded = 0, completed = 0;
    std::optional<SimulationStep>                    pending;
    double                                           radius = 0;
    std::shared_ptr<const std::vector<spawn::Point>> players;
    std::shared_ptr<const ActivationFacts>           input;
};
void validate(const Frame& frame, const rpg::detail::SimulationEvents& events) {
    auto end = ends(frame.acknowledged, events);
    if (!std::isfinite(frame.radius) || frame.radius < 0 || frame.forwarded < frame.acknowledged[3] ||
        frame.forwarded > end[3])
        invalid("Invalid frame spatial/lifecycle watermarks", DiagnosticCode::Conflict);
    if (frame.pending && (frame.completed == UINT64_MAX || frame.pending->tick.value() != frame.completed + 1 ||
                          frame.pending->delta.nanoseconds() <= 0))
        invalid("Invalid pending frame", DiagnosticCode::Conflict);
}
Value encodeFrame(const Frame& frame) {
    Value::Array acknowledged;
    for (auto n : frame.acknowledged) acknowledged.emplace_back(std::to_string(n));
    return Value::object(
        {{"acknowledged", std::move(acknowledged)},
         {"vitals_forwarded", std::to_string(frame.forwarded)},
         {"completed", std::to_string(frame.completed)},
         {"phase", frame.pending ? "spawn_pending" : "ready"},
         {"pending_delta_ns", frame.pending ? Value(std::to_string(frame.pending->delta.nanoseconds())) : Value{}},
         {"body_radius", frame.radius},
         {"players", encodePlayers(frame.players ? std::span<const spawn::Point>(*frame.players)
                                                 : std::span<const spawn::Point>{})},
         {"pending_activation", frame.pending ? encodeActivation(frame.input) : Value{}}});
}
Frame decodeFrame(const Value& value, bool legacy) {
    const auto*                               o = value.getIf<Value::Object>();
    constexpr std::array<std::string_view, 6> keys{"acknowledged", "vitals_forwarded", "completed",
                                                   "phase",        "pending_delta_ns", "body_radius"};
    if (!o || o->size() != keys.size() + (legacy ? 0 : 2)) invalid("Unknown frame fields", DiagnosticCode::ParseError);
    for (auto key : keys)
        if (!o->contains(std::string(key))) invalid("Missing frame field", DiagnosticCode::ParseError);
    Frame       frame;
    const auto* a = o->at("acknowledged").getIf<Value::Array>();
    if (!a || a->size() != 5) invalid("Wrong acknowledged queue count", DiagnosticCode::ParseError);
    for (std::size_t i = 0; i < 5; ++i) frame.acknowledged[i] = counter((*a)[i]);
    frame.forwarded   = counter(o->at("vitals_forwarded"));
    frame.completed   = counter(o->at("completed"));
    const auto* phase = o->at("phase").getIf<std::string>();
    if (!phase) invalid("Missing phase", DiagnosticCode::ParseError);
    const auto& delta = o->at("pending_delta_ns");
    if (*phase == "spawn_pending") {
        const auto n = counter(delta);
        if (n == 0 || n > INT64_MAX || frame.completed == UINT64_MAX)
            invalid("Invalid pending delta", DiagnosticCode::ParseError);
        frame.pending = SimulationStep{SimulationTick(frame.completed + 1), Duration(static_cast<std::int64_t>(n))};
    } else if (*phase != "ready" || !delta.isNull())
        invalid("Invalid frame phase", DiagnosticCode::ParseError);
    const auto& radius = o->at("body_radius");
    if (!radius.isDouble() && !radius.isInt64()) invalid("Invalid body radius", DiagnosticCode::ParseError);
    frame.radius = radius.isDouble() ? radius.asDouble() : static_cast<double>(radius.asInt());
    if (radius.isInt64() &&
        (frame.radius >= 0x1p63 || frame.radius < 0 || static_cast<std::int64_t>(frame.radius) != radius.asInt()))
        invalid("Inexact body radius", DiagnosticCode::ParseError);
    if (!std::isfinite(frame.radius) || frame.radius < 0) invalid("Invalid body radius", DiagnosticCode::ParseError);
    if (!legacy) {
        if (!o->contains("players") || !o->contains("pending_activation"))
            invalid("Missing v2 activation fields", DiagnosticCode::ParseError);
        frame.players = std::make_shared<const std::vector<spawn::Point>>(decodePlayers(o->at("players")));
        if (frame.pending)
            frame.input = decodeActivation(o->at("pending_activation"));
        else if (!o->at("pending_activation").isNull())
            invalid("Ready frame cannot retain activation intent", DiagnosticCode::Conflict);
    }
    if (frame.pending && facts(frame.input).players) {
        const auto& expected = *facts(frame.input).players;
        const auto  current =
            frame.players ? std::span<const spawn::Point>(*frame.players) : std::span<const spawn::Point>{};
        if (expected.size() != current.size())
            invalid("Pending player capability disagrees with its input", DiagnosticCode::Conflict);
        for (std::size_t i = 0; i < current.size(); ++i)
            if (expected[i].x != current[i].x || expected[i].y != current[i].y)
                invalid("Pending player capability disagrees with its input", DiagnosticCode::Conflict);
    }
    return frame;
}
Result<void> validateExecutionContext() {
    auto skills = rpg::SkillSystem::validatePersistenceContext();
    if (!skills) return skills;
    return rpg::StatusSystem::validatePersistenceContext();
}
bool sameBoundary(const SnapshotEnvelope& a, const SnapshotEnvelope& b) {
    return a.instanceId == b.instanceId && a.revision == b.revision && a.tick == b.tick;
}
}  // namespace
struct RPGActivation::Impl {
    std::shared_ptr<const ActivationFacts> facts;
    std::vector<spawn::SpawnerHandle>      handles;
    std::weak_ptr<const unsigned char>     owner;
};
RPGActivation::RPGActivation(std::shared_ptr<const Impl> impl) : impl_(std::move(impl)) {}
RPGActivation::~RPGActivation()                                   = default;
RPGActivation::RPGActivation(RPGActivation&&) noexcept            = default;
RPGActivation& RPGActivation::operator=(RPGActivation&&) noexcept = default;
struct RPGEventDelivery::Impl {
    rpg::detail::SimulationEvents      events;
    std::weak_ptr<const unsigned char> owner;
    Counters                           begin{}, end{};
};
RPGEventDelivery::RPGEventDelivery(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RPGEventDelivery::~RPGEventDelivery()                                                         = default;
RPGEventDelivery::RPGEventDelivery(RPGEventDelivery&&) noexcept                               = default;
RPGEventDelivery&                    RPGEventDelivery::operator=(RPGEventDelivery&&) noexcept = default;
const rpg::detail::SimulationEvents& RPGEventDelivery::events() const noexcept {
    if (impl_) return impl_->events;
    static const rpg::detail::SimulationEvents empty;
    return empty;
}
struct RPGMapRuntime::Impl {
    // Reverse destruction: session/domain/content, then Map, then its actual layer Table.
    std::shared_ptr<ecs::Table>                      mapTable = std::make_shared<ecs::Table>();
    map::Map                                         map;
    std::shared_ptr<definitions::DefinitionRegistry> registry;
    std::shared_ptr<rpg_spawn::RPGUnitCatalogue>     units;
    std::unique_ptr<RPGMapSession>                   session;
    rpg::detail::SimulationEvents                    events;
    std::shared_ptr<const rpg::SkillContent>         content;
    Frame                                            frame;
    std::shared_ptr<const RPGActivation::Impl>       activation;
    enum class Phase { Ready, Executing, SpawnPending, Faulted, Closing, Closed };
    Phase                                phase         = Phase::Ready;
    std::shared_ptr<const unsigned char> deliveryOwner = std::make_shared<const unsigned char>(0);
    bool                                 busy          = false;
    Result<void>                         handoff() {
        const auto                     end    = ends(frame.acknowledged, events)[3];
        const auto                     offset = frame.forwarded - frame.acknowledged[3];
        const auto                     pending = std::span<const rpg::VitalsEvent>(events.vitals).subspan(offset);
        const auto                     world = session->world();
        std::vector<ecs::EntityHandle> deaths;
        for (const auto& event : pending)
            if (event.action == "death" && event.resource == "hp" && event.actor.isFromWorld(world->lifetime()) &&
                event.actor.handle().table)
                deaths.push_back(event.actor.handle());
        auto result = session->retainLifecycle(pending);
        if (!result) return result;
        frame.forwarded = end;
        // Runtime policy: accepted canonical HP deaths retire bodies before future placement, outside Views.
        // Reclaim may fail after retirement. Fault instead of replaying partial structural cleanup.
        try {
            for (const auto& handle : deaths)
                if (auto* actor = ecs::try_get(handle)) ecs::DestroyEntity(actor);
        } catch (...) {
            phase = Phase::Faulted;
            throw;
        }
        return Result<void>::success();
    }
};
struct RPGRuntimeCheckpoint::Impl {
    RPGMapCheckpoint            map;
    rpg::SimulationEventArchive events;
    Frame                       frame;
    bool                        sealing = false;
    Impl(RPGMapCheckpoint m, rpg::SimulationEventArchive e, Frame f)
        : map(std::move(m)), events(std::move(e)), frame(f) {}
};
RPGMapRuntime::RPGMapRuntime(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RPGMapRuntime::~RPGMapRuntime() {
    if (impl_) {
        rpg::SkillContentScope content(impl_->content);
        impl_.reset();
    }
}
Result<std::unique_ptr<RPGMapRuntime>> RPGMapRuntime::prepare(
    std::vector<Value> units, std::vector<Value> templates, Value binding, service::IFileSystem& files,
    PersistentId owner, std::uint64_t seed, double radius,
    std::shared_ptr<const rpg::SkillConditionBindings> conditions) {
    using R = Result<std::unique_ptr<RPGMapRuntime>>;
    rpg::SkillContentScope liveContent({});
    auto                   impl = std::make_unique<Impl>();
    ecs::ScopedTable       scope(*impl->mapTable);
    impl->registry = std::make_shared<definitions::DefinitionRegistry>();
    impl->units    = std::make_shared<rpg_spawn::RPGUnitCatalogue>(impl->registry);
    for (const auto& value : units) {
        auto published = impl->units->publish(value);
        if (!published) return R::failure(published.status());
    }
    spawn::SpawnTemplateSource source(*impl->registry, *impl->units);
    for (const auto& value : templates) {
        auto published = source.publish(value);
        if (!published) return R::failure(published.status());
    }
    auto domain    = std::make_shared<ecs::Table>();
    auto candidate = PreparedRPGMapSession::prepare(impl->map, binding, files, source, domain, impl->units, owner, seed,
                                                    R"({"schema":"eve.map.object-contract","version":1,"types":[]})",
                                                    radius, {}, std::move(conditions));
    if (!candidate) return R::failure(candidate.status());
    auto committed = candidate.value().commit(impl->map);
    if (!committed) return R::failure(committed.status());
    impl->session      = std::move(committed).takeValue();
    impl->frame.radius = radius;
    return R::success(std::unique_ptr<RPGMapRuntime>(new RPGMapRuntime(std::move(impl))));
}
Result<RPGActivation> RPGMapRuntime::prepareActivation(std::optional<std::vector<spawn::Point>> players,
                                                       std::vector<std::string>                 events) const {
    using R = Result<RPGActivation>;
    try {
        if (impl_->busy || (impl_->phase != Impl::Phase::Ready && impl_->phase != Impl::Phase::SpawnPending))
            invalid("Runtime unavailable for activation");
        if (players) validatePlayers(*players);
        const auto limit =
            std::min<std::size_t>(activationLimit, impl_->session->spawning().world().activationInputLimit());
        if ((players && players->size() > limit) || events.size() > limit)
            invalid("Activation exceeds this core's work cap", DiagnosticCode::InvalidArgument);
        if (events.size() > activationLimit) invalid("Too many external events", DiagnosticCode::InvalidArgument);
        for (const auto& name : events) {
            if (name.empty() || name.size() > 256)
                invalid("Invalid external event name", DiagnosticCode::InvalidArgument);
            for (unsigned char c : name)
                if (c < 0x20 || c == 0x7f) invalid("Invalid external event name", DiagnosticCode::InvalidArgument);
        }
        auto input     = std::make_shared<ActivationFacts>();
        input->players = std::move(players);
        auto bound     = std::make_shared<RPGActivation::Impl>();
        bound->owner   = impl_->deliveryOwner;
        if (!events.empty()) {
            auto diagnostics = impl_->session->spawning().world().captureDiagnostics();
            if (!diagnostics) return R::failure(diagnostics.status());
            std::set<std::string> seen;
            for (const auto& name : events) {
                if (!seen.insert(name).second) invalid("Duplicate external event", DiagnosticCode::InvalidArgument);
                bool found = false;
                for (const auto& row : diagnostics.value().sources) {
                    const auto* event = std::get_if<spawn::EventActivation>(&row.activation);
                    if (!event || event->event != name) continue;
                    if (input->events.size() == limit)
                        invalid("Resolved events exceed work cap", DiagnosticCode::InvalidArgument);
                    found = true;
                    input->events.push_back(spawn::savedSpawnerKey(row.source));
                    bound->handles.push_back(row.source);
                }
                if (!found) invalid("Unknown external activation event", DiagnosticCode::NotFound);
            }
        }
        bound->facts = std::move(input);
        return R::success(RPGActivation(std::move(bound)));
    } catch (const Invalid& e) {
        return R::failure(e.status);
    }
}
Result<spawn::AdvanceReport> RPGMapRuntime::advance(const SimulationStep& step) { return advanceBound(step, nullptr); }
Result<spawn::AdvanceReport> RPGMapRuntime::advance(const SimulationStep& step, const RPGActivation& activation) {
    return advanceBound(step, &activation);
}
Result<spawn::AdvanceReport> RPGMapRuntime::advanceBound(const SimulationStep& step, const RPGActivation* activation) {
    using R = Result<spawn::AdvanceReport>;
    try {
        if (impl_->busy || (impl_->phase != Impl::Phase::Ready && impl_->phase != Impl::Phase::SpawnPending))
            invalid("Runtime not advanceable");
        if (step.delta.nanoseconds() <= 0 || impl_->frame.completed == UINT64_MAX ||
            step.tick.value() != impl_->frame.completed + 1)
            invalid("Contiguous positive frames required", DiagnosticCode::InvalidArgument);
        if (impl_->frame.pending &&
            (impl_->frame.pending->tick != step.tick || impl_->frame.pending->delta != step.delta))
            invalid("Exact pending frame required");
        if (activation) {
            if (!activation->impl_ || activation->impl_->owner.lock() != impl_->deliveryOwner)
                invalid("Activation belongs to another runtime incarnation", DiagnosticCode::StaleHandle);
            if (impl_->frame.pending && !sameFacts(impl_->frame.input, activation->impl_->facts))
                invalid("Pending frame requires identical activation facts");
        }
        Guard                             guard(impl_->busy);
        rpg::SkillContentScope            contentScope(impl_->content);
        ecs::ScopedTable                  scope(*impl_->session->world());
        rpg::detail::SimulationEventScope eventScope(impl_->events);
        if (impl_->phase == Impl::Phase::Ready) {
            impl_->activation  = activation ? activation->impl_ : nullptr;
            impl_->frame.input = impl_->activation ? impl_->activation->facts : nullptr;
            if (!impl_->activation)
                impl_->frame.players.reset();
            else if (impl_->frame.input->players)
                impl_->frame.players =
                    std::shared_ptr<const std::vector<spawn::Point>>(impl_->frame.input, &*impl_->frame.input->players);
            impl_->frame.pending = step;
            impl_->phase         = Impl::Phase::Executing;
            try {
                auto status = rpg::StatusSystem::update(static_cast<double>(step.delta.nanoseconds()) / 1e9);
                if (!status) {
                    impl_->phase = Impl::Phase::Faulted;
                    return R::failure(status.status());
                }
                rpg::SkillSystem::update(static_cast<float>(static_cast<double>(step.delta.nanoseconds()) / 1e9));
                impl_->phase = Impl::Phase::SpawnPending;
            } catch (...) {
                impl_->phase = Impl::Phase::Faulted;
                throw;
            }
        }
        auto handed = impl_->handoff();
        if (!handed) return R::failure(handed.status());
        auto advanced = [&]() -> R {
            try {
                spawn::ActivationFrame input;
                const auto&            pending = facts(impl_->frame.input);
                if (pending.players) input.players = std::span<const spawn::Point>(*pending.players);
                if (impl_->activation) input.events = impl_->activation->handles;
                auto result = impl_->session->advance(impl_->map, step, {}, input);
                if (!result && impl_->session->spawning().world().lastConsumedTick() == step.tick)
                    impl_->phase = Impl::Phase::Faulted;
                return result;
            } catch (...) {
                if (impl_->session->spawning().world().lastConsumedTick() == step.tick)
                    impl_->phase = Impl::Phase::Faulted;
                throw;
            }
        }();
        if (!advanced) return advanced;
        impl_->frame.completed = step.tick.value();
        impl_->frame.pending.reset();
        impl_->frame.input.reset();
        impl_->activation.reset();
        impl_->phase = Impl::Phase::Ready;
        return advanced;
    } catch (const Invalid& e) {
        return R::failure(e.status);
    }
}
Result<double> RPGMapRuntime::applyDamage(SubjectRef subject, double amount) {
    using R = Result<double>;
    try {
        if (impl_->busy || impl_->phase != Impl::Phase::Ready) invalid("Damage requires ready runtime");
        Guard                             guard(impl_->busy);
        rpg::SkillContentScope            contentScope(impl_->content);
        ecs::ScopedTable                  scope(*impl_->session->world());
        rpg::detail::SimulationEventScope events(impl_->events);
        auto                              applied = impl_->session->applyDamage(subject, amount);
        if (!applied) return applied;
        const auto damage = std::move(applied).takeValue();
        auto       handed = impl_->handoff();
        if (!handed) return R::failure(handed.status());
        return R::success(damage);
    } catch (const Invalid& e) {
        return R::failure(e.status);
    }
}
Result<rpg_spawn::ActorSnapshot> RPGMapRuntime::inspectActor(SubjectRef subject) const {
    if (impl_->busy)
        return Result<rpg_spawn::ActorSnapshot>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Runtime busy"));
    rpg::SkillContentScope contentScope(impl_->content);
    return impl_->session->inspectActor(subject);
}
Result<RPGEventDelivery> RPGMapRuntime::readEvents() const {
    try {
        if (impl_->busy || (impl_->phase != Impl::Phase::Ready && impl_->phase != Impl::Phase::SpawnPending))
            invalid("Runtime unavailable for delivery");
        const auto end    = ends(impl_->frame.acknowledged, impl_->events);
        auto       result = std::make_unique<RPGEventDelivery::Impl>();
        result->end       = end;
        result->begin     = impl_->frame.acknowledged;
        result->owner     = impl_->deliveryOwner;
        result->events    = impl_->events;
        return Result<RPGEventDelivery>::success(RPGEventDelivery(std::move(result)));
    } catch (const Invalid& e) {
        return Result<RPGEventDelivery>::failure(e.status);
    }
}
Result<void> RPGMapRuntime::acknowledge(const RPGEventDelivery& delivery) {
    try {
        if (impl_->busy || (impl_->phase != Impl::Phase::Ready && impl_->phase != Impl::Phase::SpawnPending))
            invalid("Runtime unavailable for acknowledgement");
        if (!delivery.impl_) invalid("Moved event receipt", DiagnosticCode::StaleHandle);
        const auto& receipt = *delivery.impl_;
        const auto  owner   = receipt.owner.lock();
        if (!owner || owner != impl_->deliveryOwner)
            invalid("Foreign or retired event receipt", DiagnosticCode::StaleHandle);
        if (receipt.begin != impl_->frame.acknowledged)
            invalid("Stale or out-of-order event prefix", DiagnosticCode::Conflict);
        if (receipt.end == receipt.begin) invalid("Empty event receipt", DiagnosticCode::InvalidArgument);
        const auto end = ends(impl_->frame.acknowledged, impl_->events);
        for (std::size_t i = 0; i < 5; ++i)
            if (receipt.end[i] < receipt.begin[i] || receipt.end[i] > end[i])
                invalid("Invalid event prefix", DiagnosticCode::Conflict);
        if (receipt.end[3] > impl_->frame.forwarded) invalid("Lifecycle handoff must precede resource acknowledgement");
        auto erase = [](auto& queue, std::uint64_t count) {
            using Event = typename std::remove_reference_t<decltype(queue)>::value_type;
            static_assert(std::is_nothrow_move_assignable_v<Event>);
            queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(count));
        };
        Counters count{};
        for (std::size_t i = 0; i < 5; ++i) count[i] = receipt.end[i] - receipt.begin[i];
        erase(impl_->events.casts, count[0]);
        erase(impl_->events.ticks, count[1]);
        erase(impl_->events.changes, count[2]);
        erase(impl_->events.vitals, count[3]);
        erase(impl_->events.levels, count[4]);
        impl_->frame.acknowledged = receipt.end;
        return Result<void>::success();
    } catch (const Invalid& e) {
        return Result<void>::failure(e.status);
    }
}
Result<Value> RPGMapRuntime::captureState() const {
    using R = Result<Value>;
    if (impl_->busy) return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Runtime busy"));
    if (impl_->phase == Impl::Phase::Closed) {
        auto state = encodeFrame(impl_->frame);
        state.set("phase", "closed");
        state.set("sources", Value::Array{});
        return R::success(std::move(state));
    }
    auto observed = impl_->session->spawning().world().captureDiagnostics();
    if (!observed) return R::failure(observed.status());
    Value::Array sources;
    for (const auto& source : observed.value().sources) {
        Value::Array subjects;
        for (auto id : source.state.subjects) subjects.emplace_back(id.format());
        const char* phase = source.state.phase == spawn::Phase::Dormant     ? "dormant"
                            : source.state.phase == spawn::Phase::Running   ? "running"
                            : source.state.phase == spawn::Phase::Completed ? "completed"
                                                                            : "faulted";
        sources.push_back(Value::object({{"id", source.state.id},
                                         {"alive", static_cast<std::int64_t>(source.state.alive)},
                                         {"phase", phase},
                                         {"total", std::to_string(source.state.total)},
                                         {"wave", static_cast<std::int64_t>(source.state.wave)},
                                         {"remaining_ns", source.state.remaining.nanoseconds()},
                                         {"subjects", std::move(subjects)}}));
    }
    auto state = encodeFrame(impl_->frame);
    state.set("sources", std::move(sources));
    if (impl_->phase == Impl::Phase::Faulted) state.set("phase", "faulted");
    if (impl_->phase == Impl::Phase::Closing) state.set("phase", "closing");
    if (impl_->phase == Impl::Phase::Closed) state.set("phase", "closed");
    return R::success(std::move(state));
}
Result<std::size_t> RPGMapRuntime::draw(graphics::ISolidRectRenderer& renderer, const ActorStyle& style) const {
    if (impl_->busy || impl_->phase != Impl::Phase::Ready)
        return Result<std::size_t>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Runtime unavailable for presentation"));
    Guard guard(impl_->busy);
    return impl_->session->draw(renderer, style);
}
RPGRuntimeCheckpoint::RPGRuntimeCheckpoint(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
RPGRuntimeCheckpoint::~RPGRuntimeCheckpoint()                                                 = default;
RPGRuntimeCheckpoint::RPGRuntimeCheckpoint(RPGRuntimeCheckpoint&&) noexcept                   = default;
RPGRuntimeCheckpoint&        RPGRuntimeCheckpoint::operator=(RPGRuntimeCheckpoint&&) noexcept = default;
Result<RPGRuntimeCheckpoint> RPGMapRuntime::captureCheckpoint(PersistentId epoch, Revision revision) {
    using R = Result<RPGRuntimeCheckpoint>;
    try {
        if (impl_->busy || (impl_->phase != Impl::Phase::Ready && impl_->phase != Impl::Phase::SpawnPending))
            invalid("Runtime not capturable");
        Guard                  guard(impl_->busy);
        rpg::SkillContentScope contentScope(impl_->content);
        auto                   context = validateExecutionContext();
        if (!context) return R::failure(context.status());
        validate(impl_->frame, impl_->events);
        if (impl_->frame.forwarded != ends(impl_->frame.acknowledged, impl_->events)[3])
            invalid("Retain lifecycle before capture");
        auto events = rpg::SimulationEventArchive::capture(impl_->events, epoch, revision,
                                                           SimulationTick(impl_->frame.completed));
        if (!events) return R::failure(events.status());
        auto map = impl_->session->capture(impl_->map, epoch, revision, events.value().referencedSkills(),
                                           events.value().referencedEffects());
        if (!map) return R::failure(map.status());
        std::size_t actorCount = 0;
        {
            auto view = ecs::View<rpg::RPGActor>(*impl_->session->world());
            for (auto it = view.begin(); it != view.end(); ++it) ++actorCount;
        }
        if (actorCount != map.value().domain().actors().size())
            invalid("Actor outside closed spawn participant", DiagnosticCode::Unsupported);
        std::set<PersistentId> subjects;
        for (const auto& actor : map.value().domain().actors()) subjects.insert(actor.subject.persistentId());
        for (auto subject : events.value().linkedSubjects())
            if (!subjects.contains(subject.persistentId()))
                invalid("Event actor outside saved graph", DiagnosticCode::Unsupported);
        std::set<std::string> skills, effects;
        for (const auto& s : map.value().domain().skills()) skills.insert(s.id);
        for (const auto& e : map.value().domain().effects()) effects.insert(e.id);
        for (const auto& id : events.value().referencedSkills())
            if (!skills.contains(id))
                invalid("Historical skill outside saved content closure", DiagnosticCode::Unsupported);
        for (const auto& id : events.value().referencedEffects())
            if (!effects.contains(id))
                invalid("Historical effect outside saved content closure", DiagnosticCode::Unsupported);
        auto state = std::make_shared<RPGRuntimeCheckpoint::Impl>(std::move(map).takeValue(),
                                                                  std::move(events).takeValue(), impl_->frame);
        return R::success(RPGRuntimeCheckpoint(std::move(state)));
    } catch (const Invalid& e) {
        return R::failure(e.status);
    }
}
Result<SnapshotEnvelope> RPGRuntimeCheckpoint::seal(const SnapshotHashProvider& hash) const {
    using R = Result<SnapshotEnvelope>;
    try {
        auto state = impl_;
        if (!state || state->sealing) invalid("Moved or reentrant runtime checkpoint");
        Guard guard(state->sealing);
        auto  map = state->map.sealMap(hash);
        if (!map) return R::failure(map.status());
        auto events = state->events.seal(hash);
        if (!events) return R::failure(events.status());
        if (!sameBoundary(map.value(), events.value()) || map.value().tick.value() != state->frame.completed)
            invalid("Runtime participant boundary mismatch", DiagnosticCode::Conflict);
        auto m = snapshotEnvelopeValue(map.value());
        if (!m) return R::failure(m.status());
        auto e = snapshotEnvelopeValue(events.value());
        if (!e) return R::failure(e.status());
        const auto epoch    = map.value().instanceId;
        const auto revision = map.value().revision;
        return makeSnapshotEnvelope("eve.map-rpg-runtime", schema(), SchemaVersion(2), epoch, revision,
                                    SimulationTick(state->frame.completed),
                                    Value::object({{"map", std::move(m).takeValue()},
                                                   {"events", std::move(e).takeValue()},
                                                   {"frame", encodeFrame(state->frame)}}),
                                    hash);
    } catch (const Invalid& e) {
        return R::failure(e.status);
    }
}
Result<void> RPGMapRuntime::close() {
    if (impl_->busy)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Runtime busy"));
    if (impl_->phase == Impl::Phase::Closed) return Result<void>::success();
    Guard guard(impl_->busy);
    impl_->phase = Impl::Phase::Closing;
    rpg::SkillContentScope            contentScope(impl_->content);
    ecs::ScopedTable                  scope(*impl_->session->world());
    rpg::detail::SimulationEventScope events(impl_->events);
    auto                              closed = impl_->session->close(spawn::DetachPolicy::RemoveUnits);
    if (!closed) return closed;
    impl_->phase = Impl::Phase::Closed;
    impl_->deliveryOwner.reset();
    return Result<void>::success();
}
struct PreparedRPGRuntimeRestore::Impl {
    std::shared_ptr<const RPGMapArchive> map;
    rpg::SimulationEventArchive          events;
    Frame                                frame;
    bool                                 busy = false, consumed = false;
    Impl(std::shared_ptr<const RPGMapArchive> m, rpg::SimulationEventArchive e, Frame f)
        : map(std::move(m)), events(std::move(e)), frame(f) {}
};
PreparedRPGRuntimeRestore::PreparedRPGRuntimeRestore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedRPGRuntimeRestore::~PreparedRPGRuntimeRestore()                                                      = default;
PreparedRPGRuntimeRestore::PreparedRPGRuntimeRestore(PreparedRPGRuntimeRestore&&) noexcept                   = default;
PreparedRPGRuntimeRestore&        PreparedRPGRuntimeRestore::operator=(PreparedRPGRuntimeRestore&&) noexcept = default;
Result<PreparedRPGRuntimeRestore> PreparedRPGRuntimeRestore::prepare(
    SnapshotEnvelope input, PersistentId epoch, const SnapshotHashProvider& hash,
    std::shared_ptr<const rpg::SkillConditionBindings> conditions) {
    using R = Result<PreparedRPGRuntimeRestore>;
    try {
        auto verified = verifySnapshotEnvelope(input, hash);
        if (!verified) return R::failure(verified.status());
        if (input.type != "eve.map-rpg-runtime" || input.schema != schema())
            invalid("Wrong runtime schema", DiagnosticCode::Unsupported);
        if (input.schemaVersion != SchemaVersion(1) && input.schemaVersion != SchemaVersion(2))
            invalid("Unknown runtime version", DiagnosticCode::UnknownVersion);
        if (epoch.isNil() || input.instanceId != epoch || input.revision.isZero())
            invalid("Invalid joint boundary", DiagnosticCode::Conflict);
        const auto* object = input.payload.getIf<Value::Object>();
        if (!object || object->size() != 3 || !object->contains("map") || !object->contains("events") ||
            !object->contains("frame"))
            invalid("Unknown runtime fields", DiagnosticCode::ParseError);
        auto frame = decodeFrame(object->at("frame"), input.schemaVersion == SchemaVersion(1));
        if (frame.completed != input.tick.value()) invalid("Frame tick disagrees", DiagnosticCode::Conflict);
        auto m = parseSnapshotEnvelopeValue(object->at("map"), hash);
        if (!m) return R::failure(m.status());
        auto e = parseSnapshotEnvelopeValue(object->at("events"), hash);
        if (!e) return R::failure(e.status());
        if (!sameBoundary(input, m.value()) || !sameBoundary(input, e.value()))
            invalid("Nested boundary mismatch", DiagnosticCode::Conflict);
        auto map = RPGMapArchive::prepare(m.value(), epoch, hash, std::move(conditions));
        if (!map) return R::failure(map.status());
        auto events = rpg::SimulationEventArchive::parse(e.value(), epoch, hash);
        if (!events) return R::failure(events.status());
        auto content = map.value()->runtimeState().domain().validateEventClosure(
            events.value().linkedSubjects(), events.value().referencedSkills(), events.value().referencedEffects());
        if (!content) return R::failure(content.status());
        auto counts = events.value().queueSizes();
        for (std::size_t i = 0; i < 5; ++i)
            if (counts[i] > UINT64_MAX - frame.acknowledged[i])
                invalid("Event sequence exhausted", DiagnosticCode::Conflict);
        if (frame.forwarded != frame.acknowledged[3] + counts[3])
            invalid("Unhanded resource queue in runtime file", DiagnosticCode::Conflict);
        const auto limit = map.value()->runtimeState().core().activationInputLimit();
        if ((frame.players && frame.players->size() > limit) || facts(frame.input).events.size() > limit ||
            (facts(frame.input).players && facts(frame.input).players->size() > limit))
            invalid("Saved activation exceeds admitted core work cap", DiagnosticCode::Conflict);
        for (const auto& key : facts(frame.input).events) {
            bool found = false;
            for (const auto& binding : map.value()->runtimeState().core().placementBindings())
                if (binding.source == key) {
                    found = std::holds_alternative<spawn::EventActivation>(
                        binding.map->instances()[binding.instance].activation);
                    break;
                }
            if (!found) invalid("Activation source is absent or not event-configured", DiagnosticCode::Conflict);
        }
        auto context = validateExecutionContext();
        if (!context) return R::failure(context.status());
        auto state = std::make_unique<Impl>(std::move(map).takeValue(), std::move(events).takeValue(), frame);
        return R::success(PreparedRPGRuntimeRestore(std::move(state)));
    } catch (const Invalid& e) {
        return R::failure(e.status);
    }
}
Result<std::unique_ptr<RPGMapRuntime>> PreparedRPGRuntimeRestore::commit(std::unique_ptr<RPGMapRuntime>& active) {
    using R = Result<std::unique_ptr<RPGMapRuntime>>;
    try {
        if (!impl_ || impl_->busy || impl_->consumed || (active && active->impl_->busy))
            invalid("Restore consumed or runtime busy");
        auto context = validateExecutionContext();
        if (!context) return R::failure(context.status());
        Guard guard(impl_->busy);
        // Pin old mutation state while preparing; no observer/gameplay callbacks run in this transaction.
        std::optional<Guard> oldGuard;
        if (active) oldGuard.emplace(active->impl_->busy);
        constexpr std::array<std::string_view, 2> families{IStateQuery::capabilityName, IStateMutation::capabilityName};
        auto                                      stage = cap::ListenerStaging::begin(families);
        if (!stage) return R::failure(stage.status());
        auto staging  = std::move(stage).takeValue();
        auto next     = std::make_unique<RPGMapRuntime::Impl>();
        next->content = impl_->map->runtimeState().domain().contentOwner();
        rpg::SkillContentScope contentScope(next->content);
        ecs::ScopedTable       scope(*next->mapTable);
        auto                   map = PreparedRPGMapRestore::prepareAdmitted(
            next->map, impl_->map, impl_->frame.radius,
            impl_->frame.players ? *impl_->frame.players : std::vector<spawn::Point>{});
        if (!map) return R::failure(map.status());
        auto retired = map.value().commit(next->map, next->session);
        if (!retired) return R::failure(retired.status());
        auto events = impl_->events.prepareRestore(*next->session->world());
        if (!events) return R::failure(events.status());
        validate(impl_->frame, events.value());
        const auto end = ends(impl_->frame.acknowledged, events.value());
        if (impl_->frame.forwarded != end[3])
            invalid("Unhanded resource queue in runtime file", DiagnosticCode::Conflict);
        next->events = std::move(events).takeValue();
        next->frame  = impl_->frame;
        next->phase =
            next->frame.pending ? RPGMapRuntime::Impl::Phase::SpawnPending : RPGMapRuntime::Impl::Phase::Ready;
        if (next->frame.pending && next->frame.input) {
            auto bound   = std::make_shared<RPGActivation::Impl>();
            bound->facts = next->frame.input;
            bound->owner = next->deliveryOwner;
            bound->handles.reserve(bound->facts->events.size());
            for (const auto& key : bound->facts->events) {
                auto source = next->session->spawning().world().resolveSavedSource(key);
                if (!source) return R::failure(source.status());
                bound->handles.push_back(source.value().source());
            }
            next->activation = std::move(bound);
        }
        auto replacement = std::unique_ptr<RPGMapRuntime>(new RPGMapRuntime(std::move(next)));
        auto published   = staging->publish();
        if (!published) return R::failure(published.status());
        if (active) {
            PreparedRPGMapRestore::retireOwnedSession(*active->impl_->session);
            active->impl_->phase = RPGMapRuntime::Impl::Phase::Closed;
            active->impl_->deliveryOwner.reset();
        }
        auto previous   = std::move(active);
        active          = std::move(replacement);
        impl_->consumed = true;
        return R::success(std::move(previous));
    } catch (const Invalid& e) {
        return R::failure(e.status);
    }
}
}  // namespace eve::map_rpg_spawn
