#include "map/Map.h"
#include "map/MapGridState.h"
#include "map/PreparedMapLoad.h"
#include "map/rpg_spawn/RPGMapArchive.h"
#include "map/spawn/MapSpawnAssets.h"
#include "spawn/SpawnRestoreState.h"
// ECS scope: delegated RPG factory lifecycle/creation; no local View or structural mutation.
// Phase: lifecycle handoff -> coordinated spawn advance -> presentation; injected simulation time/RNG only.
#include <algorithm>
#include <cmath>
#include <map>
#include "common/Assert.h"
#include "map/rpg_spawn/MapRPGPresentation.h"
#include "map/rpg_spawn/RPGMapCheckpoint.h"
#include "map/rpg_spawn/RPGMapSession.h"
#include "map/spawn/MapSpawnLoad.h"
#include "rpg/spawn/RPGSpawnCapture.h"
#include "rpg/spawn/RPGSpawnFactory.h"
#include "rpg/spawn/RPGSpawnJointRestore.h"
#include "rpg/spawn/RPGSpawnOccupancy.h"
#include "rpg/spawn/RPGUnitCatalogue.h"
namespace eve::map_rpg_spawn {
struct RPGMapSession::Impl {
    std::shared_ptr<ecs::Table>                     world;
    std::shared_ptr<rpg_spawn::RPGSpawnFactory>     factory;
    std::unique_ptr<map_spawn::MapSpawnSession>     session;
    std::vector<std::vector<spawn::LifecycleInput>> pending;
    bool                                            accepting = true;
    bool                                            busy      = false;
    Result<void>                                    retain(std::span<const rpg::VitalsEvent> events) {
        // Reserve the receiving slot before the domain bridge retires any identity index.
        pending.emplace_back();
        auto lifecycle = factory->collectLifecycle(events);
        if (!lifecycle) {
            pending.pop_back();
            return Result<void>::failure(lifecycle.status());
        }
        pending.back() = std::move(lifecycle).value();
        if (pending.back().empty()) pending.pop_back();
        return Result<void>::success();
    }
};
struct RPGMapCheckpoint::Impl {
    spawn::SpawnCheckpoint                           core;
    rpg_spawn::RPGSpawnCapture                       domain;
    std::vector<spawn::LifecycleInput>               pending;
    std::shared_ptr<const map_spawn::MapSpawnAssets> assets;
    std::shared_ptr<const map::MapGridState>         grid;
    bool                                             sealing = false;
    [[nodiscard]] Result<SnapshotEnvelope>           sealRuntime(const SnapshotHashProvider& hash) const;
    Impl(spawn::SpawnCheckpoint coreValue, rpg_spawn::RPGSpawnCapture domainValue)
        : core(std::move(coreValue)), domain(std::move(domainValue)) {}
};
RPGMapCheckpoint::RPGMapCheckpoint(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RPGMapCheckpoint::~RPGMapCheckpoint()                                                  = default;
RPGMapCheckpoint::RPGMapCheckpoint(RPGMapCheckpoint&&) noexcept                        = default;
RPGMapCheckpoint&             RPGMapCheckpoint::operator=(RPGMapCheckpoint&&) noexcept = default;
const spawn::SpawnCheckpoint& RPGMapCheckpoint::core() const noexcept {
    if (!impl_) EV_ASSERT(false, "Map checkpoint consumed");
    return impl_->core;
}
const rpg_spawn::RPGSpawnCapture& RPGMapCheckpoint::domain() const noexcept {
    if (!impl_) EV_ASSERT(false, "Map checkpoint consumed");
    return impl_->domain;
}
std::span<const spawn::LifecycleInput> RPGMapCheckpoint::pendingLifecycle() const noexcept {
    return impl_ ? std::span<const spawn::LifecycleInput>(impl_->pending) : std::span<const spawn::LifecycleInput>{};
}
std::shared_ptr<const map_spawn::MapSpawnAssets> RPGMapCheckpoint::assets() const noexcept {
    return impl_ ? impl_->assets : nullptr;
}
std::shared_ptr<const map::MapGridState> RPGMapCheckpoint::gridState() const noexcept {
    return impl_ ? impl_->grid : nullptr;
}
Result<SnapshotEnvelope> RPGMapCheckpoint::seal(const SnapshotHashProvider& hash) const {
    using R = Result<SnapshotEnvelope>;
    if (!impl_ || impl_->sealing)
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Checkpoint consumed or sealing"));
    impl_->sealing = true;
    struct Reset {
        bool& value;
        ~Reset() { value = false; }
    } reset{impl_->sealing};
    return impl_->sealRuntime(hash);
}
Result<SnapshotEnvelope> RPGMapCheckpoint::Impl::sealRuntime(const SnapshotHashProvider& hash) const {
    using R = Result<SnapshotEnvelope>;
    std::vector<spawn::SavedLifecycleInput> pending;
    pending.reserve(this->pending.size());
    for (const auto& input : this->pending) pending.push_back(spawn::savedLifecycleInput(input));
    auto core = this->core.seal(hash);
    if (!core) return R::failure(core.status());
    auto domain = this->domain.seal(core.value().revision, core.value().tick, hash);
    if (!domain) return R::failure(domain.status());
    return rpg_spawn::RPGSpawnJointRestore::sealBundle(core.value(), domain.value(), this->domain.saveEpoch(), pending,
                                                       hash);
}
Result<SnapshotEnvelope> RPGMapCheckpoint::sealMap(const SnapshotHashProvider& hash) const {
    using R = Result<SnapshotEnvelope>;
    if (!impl_ || impl_->sealing || !impl_->assets || !impl_->grid)
        return R::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Map checkpoint unavailable or sealing"));
    impl_->sealing = true;
    struct Reset {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{impl_->sealing};
    auto runtime = impl_->sealRuntime(hash);
    if (!runtime) return R::failure(runtime.status());
    const auto& boundary = runtime.value();
    auto        assets   = impl_->assets->seal(boundary.instanceId, boundary.revision, boundary.tick, hash);
    if (!assets) return R::failure(assets.status());
    auto grid = impl_->grid->seal(boundary.instanceId, boundary.revision, boundary.tick, hash);
    if (!grid) return R::failure(grid.status());
    return RPGMapArchive::sealParticipants(std::move(runtime).takeValue(), std::move(assets).takeValue(),
                                           std::move(grid).takeValue(), hash);
}
struct PreparedRPGMapSession::Impl {
    map_spawn::PreparedMapSpawn    candidate;
    std::unique_ptr<RPGMapSession> session;
};
namespace {
struct BusyGuard {
    bool& value;
    explicit BusyGuard(bool& flag) : value(flag) { value = true; }
    ~BusyGuard() { value = false; }
};
}  // namespace
RPGMapSession::RPGMapSession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RPGMapSession::~RPGMapSession() = default;
std::shared_ptr<ecs::Table>       RPGMapSession::world() const noexcept { return impl_->world; }
const map_spawn::MapSpawnSession& RPGMapSession::spawning() const noexcept { return *impl_->session; }
Result<rpg_spawn::ActorSnapshot>  RPGMapSession::inspectActor(SubjectRef subject) const {
    if (impl_->busy || !impl_->accepting)
        return Result<rpg_spawn::ActorSnapshot>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "RPG map session is busy or closing"));
    return impl_->factory->inspect(subject);
}
Result<double> RPGMapSession::applyDamage(SubjectRef subject, double amount) {
    if (impl_->busy || !impl_->accepting)
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "RPG map session is busy or closing"));
    BusyGuard guard(impl_->busy);
    return impl_->factory->applyDamage(subject, amount);
}
Result<void> RPGMapSession::resetPlacementTrace(map_spawn::PlacementTraceMode mode) {
    if (impl_->busy || !impl_->accepting)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "RPG map session is busy or closing"));
    return impl_->session->resetPlacementTrace(mode);
}
Result<spawn::AdvanceReport> RPGMapSession::advance(map::Map& target, const SimulationStep& step,
                                                    std::span<const rpg::VitalsEvent> events,
                                                    spawn::ActivationFrame            activation) {
    if (impl_->busy || !impl_->accepting)
        return Result<spawn::AdvanceReport>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "RPG map session is busy"));
    BusyGuard guard(impl_->busy);
    auto      retained = impl_->retain(events);
    if (!retained) return Result<spawn::AdvanceReport>::failure(retained.status());
    std::vector<spawn::LifecycleInput> frame;
    for (const auto& batch : impl_->pending) frame.insert(frame.end(), batch.begin(), batch.end());
    auto result = impl_->session->advance(target, step, frame, activation);
    if (result) impl_->pending.clear();
    return result;
}
Result<void> RPGMapSession::retainLifecycle(std::span<const rpg::VitalsEvent> events) {
    if (impl_->busy || !impl_->accepting)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Session busy or closing"));
    BusyGuard guard(impl_->busy);
    return impl_->retain(events);
}
Result<void> RPGMapSession::enqueue(spawn::ControlCommand command) {
    if (!impl_->accepting)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "RPG map session is closing"));
    return impl_->session->enqueue(std::move(command));
}
Result<Subscription> RPGMapSession::subscribe(Observer<spawn::SpawnEvent>::Callback callback) {
    if (!impl_->accepting)
        return Result<Subscription>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Session retired"));
    return impl_->session->subscribe(std::move(callback));
}
Result<bool> RPGMapSession::navigationNeedsRefresh(map::Map& target) const {
    if (impl_->busy || !impl_->accepting)
        return Result<bool>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "RPG map session is not accepting frames"));
    return impl_->session->navigationNeedsRefresh(target);
}
Result<std::unique_ptr<map_spawn::PreparedMapNavigation>> RPGMapSession::prepareNavigationRefresh(map::Map& target) {
    if (impl_->busy || !impl_->accepting)
        return Result<std::unique_ptr<map_spawn::PreparedMapNavigation>>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "RPG map session is not accepting frames"));
    return impl_->session->prepareNavigationRefresh(target);
}
Result<std::size_t> RPGMapSession::draw(graphics::ISolidRectRenderer& renderer, const ActorStyle& style) const {
    if (impl_->busy || !impl_->accepting || !impl_->pending.empty())
        return Result<std::size_t>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Accept pending RPG frame before drawing"));
    BusyGuard guard(impl_->busy);
    return drawActors(*impl_->session, *impl_->factory, renderer, style);
}
Result<void> RPGMapSession::close(spawn::DetachPolicy policy) {
    if (impl_->busy)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "RPG map session is busy"));
    BusyGuard guard(impl_->busy);
    auto      result = impl_->session->close(policy);
    if (result) impl_->pending.clear();
    return result;
}
Result<RPGMapCheckpoint> RPGMapSession::capture(map::Map& target, PersistentId saveEpoch, Revision revision,
                                                std::span<const std::string> eventSkills,
                                                std::span<const std::string> eventEffects) {
    using R   = Result<RPGMapCheckpoint>;
    auto fail = [](std::string message) {
        return R::failure(Diagnostic::error(DiagnosticCode::Conflict, std::move(message), "map.rpg.spawn.capture"));
    };
    if (impl_->busy || !impl_->accepting)
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Session is not idle"));
    BusyGuard guard(impl_->busy);
    auto      navigation = impl_->session->navigationNeedsRefresh(target);
    if (!navigation) return R::failure(navigation.status());
    if (navigation.value()) return fail("Refresh navigation before capturing the map participant");
    auto grid = map::MapGridState::capture(target);
    if (!grid) return R::failure(grid.status());
    auto core = impl_->session->world().capture(saveEpoch, revision);
    if (!core) return R::failure(core.status());
    auto domain = impl_->factory->capture(saveEpoch, core.value().unitReferences(), eventSkills, eventEffects);
    if (!domain) return R::failure(domain.status());
    auto frozen = std::make_unique<RPGMapCheckpoint::Impl>(std::move(core).takeValue(), std::move(domain).takeValue());
    frozen->assets = impl_->session->assets();
    frozen->grid   = std::move(grid).takeValue();
    for (const auto& batch : impl_->pending) frozen->pending.insert(frozen->pending.end(), batch.begin(), batch.end());
    const auto                          membership = frozen->core.membership();
    const auto                          actors     = frozen->domain.actors();
    std::map<PersistentId, std::size_t> tracked, materialized, retired;
    for (std::size_t i = 0; i < membership.size(); ++i) tracked.emplace(membership[i].subject.persistentId(), i);
    for (std::size_t i = 0; i < actors.size(); ++i) materialized.emplace(actors[i].subject.persistentId(), i);
    std::uint64_t previous = 0;
    for (std::size_t i = 0; i < frozen->pending.size(); ++i) {
        const auto& input = frozen->pending[i];
        const auto  found = tracked.find(input.subject.persistentId());
        if (input.sequence <= previous || found == tracked.end() || membership[found->second].source != input.source ||
            !retired.emplace(input.subject.persistentId(), i).second)
            return fail("Retained lifecycle facts do not match captured core membership");
        previous = input.sequence;
    }
    for (const auto& entry : membership) {
        const auto found   = materialized.find(entry.subject.persistentId());
        const bool pending = retired.contains(entry.subject.persistentId());
        if (found == materialized.end()) {
            if (!pending) return fail("Tracked subject is absent without a retained removal");
            continue;
        }
        const auto& actor = actors[found->second];
        if (pending) {
            if (actor.origin) return fail("Retired actor still has a live origin projection");
        } else {
            if (actor.origin != entry.source) return fail("Tracked subject and domain origin disagree");
            auto state = Value::fromJson(actor.checkpointJson);
            if (!state) return R::failure(state.status());
            const auto* vitals = state.value().find("vitals");
            const auto* hp     = vitals ? vitals->find("hp") : nullptr;
            if (!hp || !hp->isNumeric() || (hp->isInt64() ? double(hp->asInt()) : hp->asDouble()) <= 0)
                return fail("Hand off domain death facts before capturing a source-owned actor");
        }
    }
    for (const auto& actor : actors) {
        if (!actor.origin) continue;
        const auto found = tracked.find(actor.subject.persistentId());
        if (found == tracked.end() || membership[found->second].source != actor.origin)
            return fail("Domain origin is absent from this core participant");
    }
    return R::success(RPGMapCheckpoint(std::move(frozen)));
}
PreparedRPGMapSession::PreparedRPGMapSession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedRPGMapSession::~PreparedRPGMapSession()                                                  = default;
PreparedRPGMapSession::PreparedRPGMapSession(PreparedRPGMapSession&&) noexcept                   = default;
PreparedRPGMapSession&        PreparedRPGMapSession::operator=(PreparedRPGMapSession&&) noexcept = default;
Result<PreparedRPGMapSession> PreparedRPGMapSession::prepare(
    map::Map& target, const Value& binding, service::IFileSystem& files, const spawn::ITemplateResolver& templates,
    std::shared_ptr<ecs::Table> world, std::shared_ptr<rpg_spawn::RPGUnitCatalogue> units, PersistentId owner,
    std::uint64_t seed, std::string_view objectContract, double bodyRadius, std::vector<spawn::Point> players,
    std::shared_ptr<const rpg::SkillConditionBindings> conditions) {
    if (!world || !units || !std::isfinite(bodyRadius) || bodyRadius < 0)
        return Result<PreparedRPGMapSession>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid RPG map domain services"));
    auto runtime     = std::make_unique<RPGMapSession::Impl>();
    runtime->world   = world;
    runtime->factory = std::make_shared<rpg_spawn::RPGSpawnFactory>(world, units, std::move(conditions));
    auto occupancy   = std::make_shared<rpg_spawn::RPGSpawnOccupancy>(world, bodyRadius);
    auto candidate =
        map_spawn::PreparedMapSpawn::prepare(target, binding, files, templates, *units, owner, seed, objectContract,
                                             {runtime->factory, occupancy, std::move(players)});
    if (!candidate) return Result<PreparedRPGMapSession>::failure(candidate.status());
    // Allocate the final session before map publication, so the post-publication transfer cannot allocate.
    auto session = std::unique_ptr<RPGMapSession>(new RPGMapSession(std::move(runtime)));
    return Result<PreparedRPGMapSession>::success(
        PreparedRPGMapSession(std::make_unique<Impl>(Impl{std::move(candidate).value(), std::move(session)})));
}
Result<std::unique_ptr<RPGMapSession>> PreparedRPGMapSession::commit(map::Map& target) {
    if (!impl_ || !impl_->session)
        return Result<std::unique_ptr<RPGMapSession>>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "RPG map candidate consumed"));
    auto committed = impl_->candidate.commit(target);
    if (!committed) return Result<std::unique_ptr<RPGMapSession>>::failure(committed.status());
    impl_->session->impl_->session = std::move(committed).value();
    return Result<std::unique_ptr<RPGMapSession>>::success(std::move(impl_->session));
}
struct PreparedRPGMapRestore::Impl {
    map::MapLease                            owner;
    std::shared_ptr<const RPGMapArchive>     archive;
    double                                   radius = 0;
    std::optional<std::vector<spawn::Point>> players;
    bool                                     committing = false;
    bool                                     consumed   = false;
};
PreparedRPGMapRestore::PreparedRPGMapRestore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedRPGMapRestore::~PreparedRPGMapRestore()                                                  = default;
PreparedRPGMapRestore::PreparedRPGMapRestore(PreparedRPGMapRestore&&) noexcept                   = default;
PreparedRPGMapRestore&        PreparedRPGMapRestore::operator=(PreparedRPGMapRestore&&) noexcept = default;
Result<PreparedRPGMapRestore> PreparedRPGMapRestore::prepare(
    map::Map& target, const SnapshotEnvelope& envelope, PersistentId epoch, const SnapshotHashProvider& hash,
    double radius, std::optional<std::vector<spawn::Point>> players,
    std::shared_ptr<const rpg::SkillConditionBindings> conditions) {
    using R = Result<PreparedRPGMapRestore>;
    if (!std::isfinite(radius) || radius < 0 ||
        (players && (players->size() > 4096 || std::any_of(players->begin(), players->end(), [](auto p) {
                         return !std::isfinite(p.x) || !std::isfinite(p.y);
                     }))))
        return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Invalid restore spatial inputs"));
    const auto owner   = target.observeLifetime();
    auto       archive = RPGMapArchive::prepare(envelope, epoch, hash, std::move(conditions));
    if (!archive) return R::failure(archive.status());
    if (!owner.isCurrent())
        return R::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Target Map revoked during admission"));
    return prepareAdmitted(target, std::move(archive).takeValue(), radius, std::move(players));
}
Result<PreparedRPGMapRestore> PreparedRPGMapRestore::prepareAdmitted(map::Map&                                target,
                                                                     std::shared_ptr<const RPGMapArchive>     archive,
                                                                     double                                   radius,
                                                                     std::optional<std::vector<spawn::Point>> players) {
    auto impl     = std::make_unique<Impl>();
    impl->owner   = target.observeLifetime();
    impl->archive = std::move(archive);
    impl->radius  = radius;
    impl->players = std::move(players);
    return Result<PreparedRPGMapRestore>::success(PreparedRPGMapRestore(std::move(impl)));
}
void PreparedRPGMapRestore::retireOwnedSession(RPGMapSession& session) noexcept {
    session.impl_->session->retireForReplacement();
    session.impl_->accepting = false;
}
Result<std::unique_ptr<RPGMapSession>> PreparedRPGMapRestore::commit(map::Map&                       target,
                                                                     std::unique_ptr<RPGMapSession>& active) {
    using R = Result<std::unique_ptr<RPGMapSession>>;
    if (!impl_ || impl_->consumed || impl_->committing || (active && active->impl_->busy))
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Restore consumed or session busy"));
    if (!impl_->owner.isCurrent() || !impl_->owner.hasSameOwner(target.observeLifetime()))
        return R::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "Restored Map owner mismatch or revoked"));
    if (ecs::current()->is_deferred() || (active && !active->impl_->session->replacementReady(impl_->owner)))
        return R::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Replacement requires idle owners outside Views"));
    // @lifetime Existing layer is borrowed only for this metadata check; its owning Table must remain current/alive.
    auto* existingLayer = target.getLayer(0);
    if (existingLayer && ecs::handle_of(existingLayer).table != ecs::current())
        return R::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                            "Map layer Table must be current during replacement"));
    BusyGuard candidateGuard(impl_->committing);
    // @lifetime Synchronous guard observes the still-owned old object; exchanging unique owners preserves its address.
    struct OldBusy {
        bool* flag;
        bool* accepting;
        bool  previousAccepting;
        bool  retired = false;
        ~OldBusy() {
            if (flag) *flag = false;
            if (accepting && !retired) *accepting = previousAccepting;
        }
    } oldGuard{active ? &active->impl_->busy : nullptr, active ? &active->impl_->accepting : nullptr,
               active ? active->impl_->accepting : false};
    if (oldGuard.flag) {
        *oldGuard.flag      = true;
        *oldGuard.accepting = false;
    }
    const auto& admitted = impl_->archive->runtimeState();
    auto        runtime  = std::make_unique<RPGMapSession::Impl>();
    runtime->world       = std::make_shared<ecs::Table>();
    auto factory         = rpg_spawn::RPGSpawnFactory::prepareRestored(runtime->world, admitted.domainOwner());
    if (!factory) return R::failure(factory.status());
    runtime->factory = std::move(factory).takeValue();
    auto occupancy   = std::make_shared<rpg_spawn::RPGSpawnOccupancy>(runtime->world, impl_->radius);
    auto candidate =
        map_spawn::PreparedMapSpawn::prepareRestored(target, impl_->archive->assets(), *impl_->archive->gridState(),
                                                     admitted.core(), {runtime->factory, occupancy, impl_->players});
    if (!candidate) return R::failure(candidate.status());
    std::vector<spawn::LifecycleInput> pending;
    pending.reserve(admitted.pendingLifecycle().size());
    for (const auto& input : admitted.pendingLifecycle()) {
        auto source = candidate.value().world()->get().resolveSavedSource(input.source);
        if (!source) return R::failure(source.status());
        pending.push_back({input.subject, input.sequence, input.reason, source.value().source()});
    }
    if (!pending.empty()) runtime->pending.push_back(std::move(pending));
    auto replacement  = std::unique_ptr<RPGMapSession>(new RPGMapSession(std::move(runtime)));
    auto materialized = replacement->impl_->factory->materializeSavedActors(candidate.value().world()->get(), admitted);
    if (!materialized) return R::failure(materialized.status());
    auto published = candidate.value().commit(target);
    if (!published) return R::failure(published.status());
    replacement->impl_->session = std::move(published).takeValue();
    // No fallible work or callbacks remain. The outer busy guard proves the old private core is idle.
    if (active) {
        retireOwnedSession(*active);
        oldGuard.retired = true;
    }
    auto previous   = std::move(active);
    active          = std::move(replacement);
    impl_->consumed = true;
    return R::success(std::move(previous));
}
}  // namespace eve::map_rpg_spawn
