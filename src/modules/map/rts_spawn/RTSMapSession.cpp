// eve-hot-path
// ECS: delegates actual RTS factory/occupancy operations outside Views; no local System or structural mutation.
// Phase: caller's domain step/event dispatch -> retained lifecycle handoff -> canonical map spawn advance.
#include "map/rts_spawn/RTSMapSession.h"
#include <cmath>
#include <map>
#include <set>
#include "common/LifetimeWitness.h"
#include "map/Map.h"
#include "map/MapGridState.h"
#include "map/rts_spawn/RTSMapCheckpoint.h"
#include "map/rts_spawn/RTSMapCheckpointInternal.h"
#include "map/rts_spawn/RTSMapRestoreState.h"
#include "map/spawn/MapSpawnLoad.h"
#include "rts/RTS.h"
#include "rts/spawn/RTSSpawnCapture.h"
#include "rts/spawn/RTSSpawnFactory.h"
#include "rts/spawn/RTSSpawnOccupancy.h"
#include "spawn/SpawnCheckpoint.h"
#include "spawn/SpawnRestoreState.h"
namespace eve::map_rts_spawn {
namespace {
template <class T>
Result<T> unavailable() {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::PreconditionViolation, "RTS map session is busy, closing or consumed"));
}
struct Guard {
    bool& busy;
    explicit Guard(bool& value) : busy(value) { busy = true; }
    ~Guard() { busy = false; }
};
}  // namespace
struct RTSMapSession::Impl {
    std::shared_ptr<rts_spawn::RTSSpawnFactory>        factory;
    std::shared_ptr<rts_spawn::RTSSpawnOccupancy>      occupancy;
    std::unique_ptr<map_spawn::MapSpawnSession>        session;
    std::vector<std::vector<rts::RTSFrameDamageEvent>> inputs;
    std::vector<std::vector<spawn::LifecycleInput>>    pending;
    rts::RTS*                                          domain =
        nullptr;  // @lifetime Actual facade borrowed behind domainLifetime; never dereference after retirement.
    LifetimeWitness domainLifetime;
    bool            busy = false, accepting = true;
};
RTSMapCheckpoint::RTSMapCheckpoint(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RTSMapCheckpoint::~RTSMapCheckpoint()                                      = default;
RTSMapCheckpoint::RTSMapCheckpoint(RTSMapCheckpoint&&) noexcept            = default;
RTSMapCheckpoint& RTSMapCheckpoint::operator=(RTSMapCheckpoint&&) noexcept = default;
PersistentId      RTSMapCheckpoint::saveEpoch() const noexcept { return impl_ ? impl_->epoch : PersistentId{}; }
Revision          RTSMapCheckpoint::revision() const noexcept { return impl_ ? impl_->revision : Revision::zero(); }
SimulationTick    RTSMapCheckpoint::coreTick() const noexcept { return impl_ ? impl_->tick : SimulationTick::zero(); }
std::optional<std::reference_wrapper<const spawn::SpawnCheckpoint>> RTSMapCheckpoint::core() const noexcept {
    if (!impl_) return std::nullopt;
    return std::cref(impl_->core);
}
std::optional<std::reference_wrapper<const rts_spawn::RTSSpawnCapture>> RTSMapCheckpoint::provenance() const noexcept {
    if (!impl_) return std::nullopt;
    return std::cref(impl_->provenance);
}
std::optional<std::reference_wrapper<const rts::RTSStateSnapshot>> RTSMapCheckpoint::roots() const noexcept {
    if (!impl_) return std::nullopt;
    return std::cref(impl_->roots);
}
std::span<const rts::RTSFrameDamageEvent> RTSMapCheckpoint::pendingDamage() const noexcept {
    return impl_ ? std::span<const rts::RTSFrameDamageEvent>(impl_->inputs)
                 : std::span<const rts::RTSFrameDamageEvent>{};
}
std::span<const spawn::LifecycleInput> RTSMapCheckpoint::pendingLifecycle() const noexcept {
    return impl_ ? std::span<const spawn::LifecycleInput>(impl_->pending) : std::span<const spawn::LifecycleInput>{};
}
std::shared_ptr<const map_spawn::MapSpawnAssets> RTSMapCheckpoint::assets() const noexcept {
    return impl_ ? impl_->assets : nullptr;
}
std::shared_ptr<const map::MapGridState> RTSMapCheckpoint::gridState() const noexcept {
    return impl_ ? impl_->grid : nullptr;
}
struct PreparedRTSMapSession::Impl {
    map_spawn::PreparedMapSpawn             candidate;
    std::unique_ptr<RTSMapSession>          session;
    LifetimeWitness                         domain;
    std::shared_ptr<const RTSMapCheckpoint> restoration;
};
RTSMapSession::RTSMapSession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RTSMapSession::~RTSMapSession() = default;
const map_spawn::MapSpawnSession& RTSMapSession::spawning() const noexcept { return *impl_->session; }
Result<spawn::AdvanceReport>      RTSMapSession::advance(map::Map& target, const SimulationStep& step,
                                                         std::span<const rts::RTSFrameDamageEvent> events,
                                                         spawn::ActivationFrame                    activation) {
    if (impl_->busy || !impl_->accepting) return unavailable<spawn::AdvanceReport>();
    Guard guard(impl_->busy);
    // Publish an owning input batch before the bridge can retire any source index.
    impl_->inputs.emplace_back(events.begin(), events.end());
    std::vector<rts::RTSFrameDamageEvent> input;
    for (const auto& batch : impl_->inputs) input.insert(input.end(), batch.begin(), batch.end());
    impl_->pending.emplace_back();
    auto lifecycle = [&]() -> Result<std::vector<spawn::LifecycleInput>> {
        try {
            return impl_->factory->collectLifecycle(input);
        } catch (...) {
            impl_->pending.pop_back();
            throw;
        }
    }();
    if (!lifecycle) {
        impl_->pending.pop_back();
        return Result<spawn::AdvanceReport>::failure(lifecycle.status());
    }
    impl_->pending.back() = std::move(lifecycle).takeValue();
    impl_->inputs.clear();
    if (impl_->pending.back().empty()) impl_->pending.pop_back();
    std::vector<spawn::LifecycleInput> frame;
    for (const auto& batch : impl_->pending) frame.insert(frame.end(), batch.begin(), batch.end());
    auto result = impl_->session->advance(target, step, frame, activation);
    if (result) impl_->pending.clear();
    return result;
}
Result<RTSMapCheckpoint> RTSMapSession::capture(map::Map& target, PersistentId saveEpoch, Revision revision) {
    using R       = Result<RTSMapCheckpoint>;
    auto conflict = [](const char* message) {
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, message, "map.rts.spawn.capture"));
    };
    if (impl_->busy || !impl_->accepting || !impl_->domainLifetime.isAlive()) return unavailable<RTSMapCheckpoint>();
    Guard guard(impl_->busy);
    if (saveEpoch.isNil() || revision.isZero())
        return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Save epoch and revision required"));
    auto ready = impl_->factory->validateDomain();
    if (!ready) return R::failure(ready.status());
    auto navigation = impl_->session->navigationNeedsRefresh(target);
    if (!navigation) return R::failure(navigation.status());
    if (navigation.value()) return conflict("Refresh navigation before freezing map state");
    auto grid = map::MapGridState::capture(target);
    if (!grid) return R::failure(grid.status());
    auto core = impl_->session->world().capture(saveEpoch, revision);
    if (!core) return R::failure(core.status());
    auto provenance = impl_->factory->capture(saveEpoch, core.value().unitReferences());
    if (!provenance) return R::failure(provenance.status());
    auto roots = impl_->domain->snapshotState();
    if (!roots) return R::failure(roots.status());
    auto frozen = std::make_unique<RTSMapCheckpoint::Impl>(
        saveEpoch, revision, impl_->session->world().lastConsumedTick(), std::move(core).takeValue(),
        std::move(provenance).takeValue(), std::move(roots).takeValue());
    frozen->grid   = std::move(grid).takeValue();
    frozen->assets = impl_->session->assets();
    for (const auto& batch : impl_->inputs) frozen->inputs.insert(frozen->inputs.end(), batch.begin(), batch.end());
    for (const auto& batch : impl_->pending) frozen->pending.insert(frozen->pending.end(), batch.begin(), batch.end());
    auto boundary = impl_->session->world().restoreBindings();
    if (!boundary) return R::failure(boundary.status());
    for (const auto& member : frozen->core.membership())
        if (member.source.incarnation != frozen->provenance.sourceIncarnation())
            return conflict("Core and provenance process incarnations disagree");
    std::vector<spawn::SavedLifecycleInput> pending;
    pending.reserve(frozen->pending.size());
    for (const auto& input : frozen->pending) {
        if (input.source.incarnation != frozen->provenance.sourceIncarnation())
            return conflict("Pending lifecycle has foreign process incarnation");
        pending.push_back(spawn::savedLifecycleInput(input));
    }
    auto relations = frozen->provenance.validateRelations(
        boundary.value().membership, boundary.value().lifecycleSequence, frozen->roots.units, pending, frozen->inputs);
    if (!relations) return R::failure(relations.status());
    return R::success(RTSMapCheckpoint(std::move(frozen)));
}
Result<bool> RTSMapSession::navigationNeedsRefresh(map::Map& target) const {
    if (impl_->busy || !impl_->accepting) return unavailable<bool>();
    return impl_->session->navigationNeedsRefresh(target);
}
Result<std::unique_ptr<map_spawn::PreparedMapNavigation>> RTSMapSession::prepareNavigationRefresh(map::Map& target) {
    if (impl_->busy || !impl_->accepting) return unavailable<std::unique_ptr<map_spawn::PreparedMapNavigation>>();
    Guard guard(impl_->busy);
    return impl_->session->prepareNavigationRefresh(target);
}
Result<void> RTSMapSession::close(spawn::DetachPolicy policy) {
    if (policy != spawn::DetachPolicy::KeepUnits && policy != spawn::DetachPolicy::RemoveUnits)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid RTS map detach policy"));
    if (impl_->busy) return unavailable<void>();
    Guard guard(impl_->busy);
    impl_->accepting = false;
    auto result      = impl_->session->close(policy);
    if (result) {
        impl_->inputs.clear();
        impl_->pending.clear();
    }
    return result;
}
PreparedRTSMapSession::PreparedRTSMapSession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedRTSMapSession::~PreparedRTSMapSession()                                                  = default;
PreparedRTSMapSession::PreparedRTSMapSession(PreparedRTSMapSession&&) noexcept                   = default;
PreparedRTSMapSession&        PreparedRTSMapSession::operator=(PreparedRTSMapSession&&) noexcept = default;
Result<PreparedRTSMapSession> PreparedRTSMapSession::prepare(
    map::Map& target, const Value& binding, service::IFileSystem& files, const spawn::ITemplateResolver& templates,
    rts::RTS& domain, SubjectRef faction, std::shared_ptr<definitions::DefinitionRegistry> registry, PersistentId owner,
    std::uint64_t seed, std::string_view objectContract, std::vector<spawn::Point> players,
    std::shared_ptr<const rts::CommandSpec> initialCommand) {
    using R    = Result<PreparedRTSMapSession>;
    auto world = domain.ownedWorld();
    if (!world || world->is_deferred() || !registry || !faction.isValid()) return unavailable<PreparedRTSMapSession>();
    const auto lifetime = domain.lifetimeWitness();
    {
        ecs::ScopedTable scope(*world);
        if (!domain.findFaction(faction))
            return R::failure(Diagnostic::error(DiagnosticCode::NotFound, "RTS map faction is not owned by domain"));
    }
    auto state            = std::make_unique<RTSMapSession::Impl>();
    state->domain         = &domain;
    state->domainLifetime = lifetime;
    state->factory =
        std::make_shared<rts_spawn::RTSSpawnFactory>(domain, faction, std::move(registry), std::move(initialCommand));
    state->occupancy = std::make_shared<rts_spawn::RTSSpawnOccupancy>(domain);
    auto ready       = state->factory->validateDomain();
    if (!ready) return R::failure(ready.status());
    auto candidate =
        map_spawn::PreparedMapSpawn::prepare(target, binding, files, templates, *state->factory, owner, seed,
                                             objectContract, {state->factory, state->occupancy, std::move(players)});
    if (!candidate) return R::failure(candidate.status());
    if (!lifetime.isAlive()) return unavailable<PreparedRTSMapSession>();
    auto current = state->factory->validateDomain();
    if (!current) return R::failure(current.status());
    auto session = std::unique_ptr<RTSMapSession>(new RTSMapSession(std::move(state)));
    return R::success(PreparedRTSMapSession(
        std::make_unique<Impl>(Impl{std::move(candidate).takeValue(), std::move(session), lifetime, {}})));
}
struct RTSMapRestoreState::Impl {
    std::shared_ptr<const RTSMapCheckpoint> saved;
    spawn::SpawnRestoreState                core;
    Impl(std::shared_ptr<const RTSMapCheckpoint> source, spawn::SpawnRestoreState value)
        : saved(std::move(source)), core(std::move(value)) {}
};
RTSMapRestoreState::RTSMapRestoreState(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RTSMapRestoreState::~RTSMapRestoreState()                                                            = default;
RTSMapRestoreState::RTSMapRestoreState(RTSMapRestoreState&&) noexcept                                = default;
RTSMapRestoreState&                     RTSMapRestoreState::operator=(RTSMapRestoreState&&) noexcept = default;
std::shared_ptr<const RTSMapCheckpoint> RTSMapRestoreState::checkpoint() const noexcept {
    return impl_ ? impl_->saved : nullptr;
}
OptionalRef<const spawn::SpawnRestoreState> RTSMapRestoreState::core() const noexcept {
    return impl_ ? OptionalRef<const spawn::SpawnRestoreState>(std::cref(impl_->core)) : std::nullopt;
}
Result<RTSMapRestoreState> RTSMapRestoreState::prepare(std::shared_ptr<const RTSMapCheckpoint> saved,
                                                       const SnapshotHashProvider&             hashProvider) {
    using R           = Result<RTSMapRestoreState>;
    const auto reject = [](DiagnosticCode code, const char* message) {
        return R::failure(Diagnostic::error(code, message, "map.rts.spawn.admit"));
    };
    if (!saved || !saved->core() || !saved->provenance() || !saved->roots() || !saved->assets() ||
        !saved->gridState() || saved->saveEpoch().isNil() || saved->revision().isZero())
        return reject(DiagnosticCode::InvalidArgument, "Complete native map checkpoint required");
    const auto& provenance = saved->provenance()->get();
    if (provenance.saveEpoch() != saved->saveEpoch())
        return reject(DiagnosticCode::Conflict, "Core and provenance save epochs differ");
    std::vector<spawn::SavedLifecycleInput> pending;
    pending.reserve(saved->pendingLifecycle().size());
    for (const auto& member : saved->core()->get().membership())
        if (member.source.incarnation != provenance.sourceIncarnation())
            return reject(DiagnosticCode::Conflict, "Captured core has a foreign source incarnation");
    for (const auto& input : saved->pendingLifecycle()) {
        if (input.source.incarnation != provenance.sourceIncarnation())
            return reject(DiagnosticCode::Conflict, "Retained lifecycle has a foreign source incarnation");
        pending.push_back(spawn::savedLifecycleInput(input));
    }

    struct FrozenUnits final : spawn::IUnitDefinitionResolver {
        const rts_spawn::RTSSpawnCapture& source;
        explicit FrozenUnits(const rts_spawn::RTSSpawnCapture& value) : source(value) {}
        Result<void> validate(const DefinitionRef& reference) const override {
            auto result = source.resolveFuture(reference);
            if (!result) return Result<void>::failure(result.status());
            return Result<void>::success();
        }
    } units(provenance);
    auto sealed = saved->core()->get().seal(hashProvider);
    if (!sealed) return R::failure(sealed.status());
    auto admitted = spawn::SpawnRestoreState::decode(sealed.value(), saved->saveEpoch(), hashProvider, units);
    if (!admitted) return R::failure(admitted.status());
    if (admitted.value().revision() != saved->revision() || admitted.value().tick() != saved->coreTick())
        return reject(DiagnosticCode::Conflict, "Core revision or clock differs from the joined checkpoint");
    auto relations =
        provenance.validateRelations(admitted.value().membership(), admitted.value().acceptedLifecycleSequence(),
                                     saved->roots()->get().units, pending, saved->pendingDamage());
    if (!relations) return R::failure(relations.status());
    return R::success(RTSMapRestoreState(std::make_unique<Impl>(std::move(saved), std::move(admitted).takeValue())));
}
Result<PreparedRTSMapSession> PreparedRTSMapSession::prepareRestored(map::Map&                                target,
                                                                     const RTSMapRestoreState&                admitted,
                                                                     rts::RTS&                                domain,
                                                                     std::optional<std::vector<spawn::Point>> players) {
    using R           = Result<PreparedRTSMapSession>;
    const auto reject = [](DiagnosticCode code, const char* message) {
        return R::failure(Diagnostic::error(code, message, "map.rts.spawn.restore"));
    };
    auto saved = admitted.checkpoint();
    if (!saved || !admitted.core())
        return reject(DiagnosticCode::PreconditionViolation, "Native map CPU admission was moved");
    const auto lifetime = domain.lifetimeWitness();
    auto       world    = domain.ownedWorld();
    if (!world || world->is_deferred())
        return reject(DiagnosticCode::PreconditionViolation, "Restore requires an idle actual owning RTS Table");
    auto provenance = std::shared_ptr<const rts_spawn::RTSSpawnCapture>(saved, &saved->provenance()->get());
    std::vector<spawn::SavedLifecycleInput> pending;
    pending.reserve(saved->pendingLifecycle().size());
    for (const auto& input : saved->pendingLifecycle()) pending.push_back(spawn::savedLifecycleInput(input));
    auto factory = rts_spawn::RTSSpawnFactory::prepareRestored(domain, provenance);
    if (!factory) return R::failure(factory.status());
    auto roots = domain.snapshotState();
    if (!roots) return R::failure(roots.status());
    auto relations = provenance->validateRelations(admitted.core()->get().membership(),
                                                   admitted.core()->get().acceptedLifecycleSequence(),
                                                   roots.value().units, pending, saved->pendingDamage());
    if (!relations) return R::failure(relations.status());
    auto state            = std::make_unique<RTSMapSession::Impl>();
    state->domain         = &domain;
    state->domainLifetime = lifetime;
    state->factory        = std::move(factory).takeValue();
    state->occupancy      = std::make_shared<rts_spawn::RTSSpawnOccupancy>(domain);
    if (!saved->pendingDamage().empty())
        state->inputs.emplace_back(saved->pendingDamage().begin(), saved->pendingDamage().end());
    auto candidate = map_spawn::PreparedMapSpawn::prepareRestored(
        target, saved->assets(), *saved->gridState(), admitted.core()->get(),
        {state->factory, state->occupancy, std::move(players)});
    if (!candidate) return R::failure(candidate.status());
    auto rebound = state->factory->restoreSources(candidate.value().world()->get(), pending, saved->pendingDamage());
    if (!rebound) return R::failure(rebound.status());
    if (!rebound.value().empty()) state->pending.push_back(std::move(rebound).takeValue());
    auto session = std::unique_ptr<RTSMapSession>(new RTSMapSession(std::move(state)));
    return R::success(PreparedRTSMapSession(std::make_unique<Impl>(
        Impl{std::move(candidate).takeValue(), std::move(session), lifetime, std::move(saved)})));
}
Result<std::unique_ptr<RTSMapSession>> PreparedRTSMapSession::commit(map::Map& target) {
    using R = Result<std::unique_ptr<RTSMapSession>>;
    if (!impl_ || !impl_->session || !impl_->domain.isAlive()) return unavailable<std::unique_ptr<RTSMapSession>>();
    auto ready = impl_->session->impl_->factory->validateDomain();
    if (!ready) return R::failure(ready.status());
    if (impl_->restoration) {
        auto& state = *impl_->session->impl_;
        auto  roots = state.domain->snapshotState();
        if (!roots) return R::failure(roots.status());
        auto observed =
            state.factory->capture(impl_->restoration->saveEpoch(), impl_->restoration->core()->get().unitReferences());
        if (!observed) return R::failure(observed.status());
        auto binding = impl_->candidate.world()->get().restoreBindings();
        if (!binding) return R::failure(binding.status());
        std::vector<spawn::SavedLifecycleInput> pending;
        std::vector<rts::RTSFrameDamageEvent>   inputs;
        for (const auto& batch : state.pending)
            for (const auto& input : batch) pending.push_back(spawn::savedLifecycleInput(input));
        for (const auto& batch : state.inputs) inputs.insert(inputs.end(), batch.begin(), batch.end());
        auto relations = observed.value().validateRelations(
            binding.value().membership, binding.value().lifecycleSequence, roots.value().units, pending, inputs);
        if (!relations) return R::failure(relations.status());
    }
    auto committed = impl_->candidate.commit(target);
    if (!committed) return R::failure(committed.status());
    impl_->session->impl_->session = std::move(committed).takeValue();
    return R::success(std::move(impl_->session));
}
}  // namespace eve::map_rts_spawn
