// eve-hot-path
// ECS scope: canonical RTS-owned private Table. All mutation is delegated outside Views to RTS/session APIs.
// Phase: admission -> domain step -> retained lifecycle handoff -> presentation. Time is caller-injected.
#include "map/rts_spawn/RTSMapRuntime.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include "action/Action.h"
#include "combat/Damage.h"
#include "combat/DamageCapture.h"
#include "common/ListenerStaging.h"
#include "common/ServiceInterfaces.h"
#include "common/StateAccess.h"
#include "crowd/Crowd.h"
#include "definitions/DefinitionRegistryCapture.h"
#include "definitions/Definitions.h"
#include "map/Map.h"
#include "map/PathfinderState.h"
#include "map/rts_spawn/RTSMapCheckpoint.h"
#include "map/rts_spawn/RTSMapRuntimeInternal.h"
#include "map/rts_spawn/RTSMapSession.h"
#include "map/rts_spawn/RTSRuntimeCheckpoint.h"
#include "map/rts_spawn/RTSRuntimeCheckpointInternal.h"
#include "map/spawn/MapSpawnBinding.h"
#include "map/spawn/MapSpawnLoad.h"
#include "rts/RTS.h"
#include "rts/RTSActionCapture.h"
#include "rts/RTSAttributeCapture.h"
#include "rts/RTSCrowdCapture.h"
#include "rts/RTSNavigationCapture.h"
#include "rts/RTSWeaponCapture.h"
#include "rts/spawn/RTSSpawnFactory.h"
#include "sensing/Sensing.h"
#include "sensing/SensingCapture.h"
#include "spawn/SpawnDiagnostics.h"
#include "spawn/definitions/SpawnTemplateSource.h"
namespace eve::map_rts_spawn {
namespace {
template <class T>
Result<T> reject(std::string message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, std::move(message)));
}
// Borrowed only during the synchronous prepare call; external code executes outside listener capture.
struct BuilderFiles final : service::IFileSystem {
    service::IFileSystem& source;
    cap::ListenerStaging& staging;
    BuilderFiles(service::IFileSystem& files, cap::ListenerStaging& scope) : source(files), staging(scope) {}
    bool readFile(const std::string& path, std::vector<std::uint8_t>& out) override {
        auto resumed = staging.pause().expect("builder owns active listener scope");
        return source.readFile(path, out);
    }
    bool writeFile(const std::string& path, const void* bytes, std::size_t size) override {
        auto resumed = staging.pause().expect("builder owns active listener scope");
        return source.writeFile(path, bytes, size);
    }
    bool fileExists(const std::string& path) override {
        auto resumed = staging.pause().expect("builder owns active listener scope");
        return source.fileExists(path);
    }
};
struct Guard {
    bool& busy;
    explicit Guard(bool& value) : busy(value) { busy = true; }
    ~Guard() { busy = false; }
};
}  // namespace
RTSRuntimeCheckpoint::RTSRuntimeCheckpoint(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RTSRuntimeCheckpoint::~RTSRuntimeCheckpoint()                                          = default;
RTSRuntimeCheckpoint::RTSRuntimeCheckpoint(RTSRuntimeCheckpoint&&) noexcept            = default;
RTSRuntimeCheckpoint& RTSRuntimeCheckpoint::operator=(RTSRuntimeCheckpoint&&) noexcept = default;
PersistentId          RTSRuntimeCheckpoint::saveEpoch() const noexcept {
    return impl_ ? impl_->map.saveEpoch() : PersistentId{};
}
Revision RTSRuntimeCheckpoint::revision() const noexcept { return impl_ ? impl_->map.revision() : Revision::zero(); }
Result<void> RTSRuntimeCheckpoint::validateRestoreContext() const {
    if (!impl_) return reject<void>("Runtime checkpoint was moved");
    if (!impl_->context.isSuccess()) return Result<void>::failure(impl_->context);
    return Result<void>::success();
}
SimulationTick RTSRuntimeCheckpoint::completedTick() const noexcept {
    return impl_ ? impl_->completed : SimulationTick::zero();
}
std::optional<RTSCheckpointPhase> RTSRuntimeCheckpoint::phase() const noexcept {
    if (!impl_) return std::nullopt;
    return impl_->phase;
}
std::optional<SimulationStep> RTSRuntimeCheckpoint::pendingStep() const noexcept {
    return impl_ ? impl_->pending : std::nullopt;
}
std::optional<std::reference_wrapper<const RTSMapCheckpoint>> RTSRuntimeCheckpoint::map() const noexcept {
    if (!impl_) return std::nullopt;
    return std::cref(impl_->map);
}
OptionalRef<const combat::DamageCapture> RTSRuntimeCheckpoint::damage() const noexcept {
    return impl_ ? OptionalRef<const combat::DamageCapture>(std::cref(impl_->damage)) : std::nullopt;
}
OptionalRef<const definitions::DefinitionRegistryCapture> RTSRuntimeCheckpoint::definitions() const noexcept {
    return impl_ ? OptionalRef<const definitions::DefinitionRegistryCapture>(std::cref(impl_->definitions))
                 : std::nullopt;
}
std::optional<std::reference_wrapper<const rts::RTSUnitAttributeCapture>> RTSRuntimeCheckpoint::unitAttributes()
    const noexcept {
    if (!impl_) return std::nullopt;
    return std::cref(impl_->unitAttributes);
}
std::optional<std::reference_wrapper<const rts::RTSWeaponCapture>> RTSRuntimeCheckpoint::weapons() const noexcept {
    if (!impl_) return std::nullopt;
    return std::cref(impl_->weapons);
}
std::optional<std::reference_wrapper<const rts::RTSActionCapture>> RTSRuntimeCheckpoint::actions() const noexcept {
    if (!impl_) return std::nullopt;
    return std::cref(impl_->actions);
}
std::optional<std::reference_wrapper<const rts::RTSFrameState>> RTSRuntimeCheckpoint::events() const noexcept {
    if (!impl_) return std::nullopt;
    return std::cref(impl_->events);
}
std::optional<std::reference_wrapper<const crowd::AgentFrame>> RTSRuntimeCheckpoint::crowdFrame() const noexcept {
    if (!impl_) return std::nullopt;
    return impl_->crowdState.frame();
}
std::optional<std::reference_wrapper<const rts::RTSCrowdCapture>> RTSRuntimeCheckpoint::crowd() const noexcept {
    if (!impl_) return std::nullopt;
    return std::cref(impl_->crowdState);
}
std::optional<std::reference_wrapper<const map::PathfinderState>> RTSRuntimeCheckpoint::navigationProvider()
    const noexcept {
    if (!impl_) return std::nullopt;
    return impl_->navigation.provider();
}
std::optional<std::reference_wrapper<const rts::RTSNavigationCapture>> RTSRuntimeCheckpoint::navigation()
    const noexcept {
    if (!impl_) return std::nullopt;
    return std::cref(impl_->navigation);
}
std::span<const RTSCrowdCheckpointMember> RTSRuntimeCheckpoint::crowdMembers() const noexcept {
    return impl_ ? std::span<const RTSCrowdCheckpointMember>(impl_->members)
                 : std::span<const RTSCrowdCheckpointMember>{};
}
std::optional<std::reference_wrapper<const sensing::SensingCapture>> RTSRuntimeCheckpoint::sensing() const noexcept {
    if (!impl_) return std::nullopt;
    return std::cref(impl_->sensingState);
}
std::optional<std::reference_wrapper<const rts::RTSCombatSnapshot>> RTSRuntimeCheckpoint::combat() const noexcept {
    if (!impl_) return std::nullopt;
    return std::cref(impl_->combatState);
}
RTSMapRuntime::RTSMapRuntime(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RTSMapRuntime::~RTSMapRuntime() = default;
Result<std::unique_ptr<RTSMapRuntime>> RTSMapRuntime::prepare(std::string content, std::vector<Value> templates,
                                                              Value binding, service::IFileSystem& files,
                                                              SubjectRef faction, PersistentId owner,
                                                              std::uint64_t                           seed,
                                                              std::shared_ptr<const rts::CommandSpec> command) {
    using R = Result<std::unique_ptr<RTSMapRuntime>>;
    // Snapshot the command before any filesystem callback can mutate its caller-owned source.
    if (command) command = std::make_shared<const rts::CommandSpec>(*command);
    auto descriptor = map_spawn::MapSpawnBinding::decode(binding);
    if (!descriptor) return R::failure(descriptor.status());
    constexpr std::array<std::string_view, 4> worldListeners = {
        IGameplayControlProvider::capabilityName, IGameplayInstanceCatalog::capabilityName, IStateQuery::capabilityName,
        IStateMutation::capabilityName};
    auto staging = cap::ListenerStaging::begin(worldListeners);
    if (!staging) return R::failure(staging.status());
    auto             staged     = std::move(staging).takeValue();
    auto             impl       = std::make_unique<Impl>();
    const auto       tableOwner = impl->table;
    ecs::ScopedTable scope(*tableOwner);
    BuilderFiles     externalFiles(files, *staged);
    auto             loaded = impl->domain.loadContent(*impl->registry, content);
    if (!loaded) return R::failure(loaded.status());
    auto createdFaction = impl->domain.newFaction(faction);
    if (!createdFaction) return R::failure(createdFaction.status());
    impl->crowd.setClampToField(false);
    impl->domain.setCrowdProvider(&impl->crowd);
    impl->domain.setCombatProviders(&impl->sensing, &impl->damage);
    auto validator = std::make_shared<rts_spawn::RTSSpawnFactory>(impl->domain, faction, impl->registry, command);
    spawn::SpawnTemplateSource source(*impl->registry, *validator);
    for (const auto& value : templates) {
        auto published = source.publish(value);
        if (!published) return R::failure(published.status());
    }
    auto prepared = PreparedRTSMapSession::prepare(
        impl->map, binding, externalFiles, source, impl->domain, faction, impl->registry, owner, seed,
        R"({"schema":"eve.map.object-contract","version":1,"types":[]})", {}, command);
    if (!prepared) return R::failure(prepared.status());
    auto committed = prepared.value().commit(impl->map);
    if (!committed) return R::failure(committed.status());
    impl->session = std::move(committed).takeValue();
    auto* layer   = impl->map.getLayer(static_cast<int>(descriptor.value().navigationLayer()));
    if (!layer) return reject<std::unique_ptr<RTSMapRuntime>>("Published navigation layer is absent");
    auto geometry = map::describeMapGrid(*layer);
    if (!geometry) return R::failure(geometry.status());
    const auto transform = descriptor.value().transform();
    auto       origin    = map_spawn::pixelToWorld(transform, {geometry.value().originX, geometry.value().originY});
    if (!origin) return R::failure(origin.status());
    const double cell  = geometry.value().stepX * transform.scale;
    const double cellY = geometry.value().stepY * transform.scale;
    const double limit = std::numeric_limits<float>::max();
    if (!std::isfinite(cell) || cell <= 0 || cell != cellY || cell > limit || std::abs(origin.value().x) > limit ||
        std::abs(origin.value().y) > limit || static_cast<float>(cell) <= 0)
        return R::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "RTS requires representable square navigation cells"));
    const float cellSize = static_cast<float>(cell);
    const float x = static_cast<float>(origin.value().x), y = static_cast<float>(origin.value().y);
    impl->navigation.bindLayer(layer);
    impl->crowd.resizeField(layer->getMapWidth(), layer->getMapHeight(), cellSize, x - cellSize * .5f,
                            y - cellSize * .5f);
    impl->crowd.setArriveRadius(cellSize);
    impl->crowd.setSeparationWeight(0.f);
    crowd::AvoidanceSettings avoidance;
    avoidance.enabled = true;
    auto configured   = impl->crowd.configureAvoidance(avoidance);
    if (!configured) return R::failure(configured.status());
    impl->domain.setNavigationProvider(&impl->navigation, {cellSize, x, y});
    // IDs are local to this private Crowd owner. Terrain projection is imported by the canonical RTS phase,
    // not copied into a second mutable blocked-cell array here.
    rts::CrowdSurfaceTracking tracking;
    tracking.surface          = crowd::NavigationSurfaceId(1);
    tracking.movement         = crowd::MovementClassId(1);
    tracking.movementRevision = tracking.costModelRevision = 1;
    auto tracked                                           = impl->domain.configureCrowdSurfaceTracking(tracking);
    if (!tracked) return R::failure(tracked.status());
    auto ready     = std::unique_ptr<RTSMapRuntime>(new RTSMapRuntime(std::move(impl)));
    auto published = staged->publish();
    if (!published) return R::failure(published.status());
    return R::success(std::move(ready));
}
Result<spawn::AdvanceReport> RTSMapRuntime::advance(const SimulationStep& step) {
    using R = Result<spawn::AdvanceReport>;
    if (impl_->busy || (impl_->phase != Impl::Phase::Ready && impl_->phase != Impl::Phase::SpawnPending))
        return reject<spawn::AdvanceReport>("RTS runtime is busy, faulted or closing");
    if (step.delta.nanoseconds() <= 0 || impl_->completedTick == std::numeric_limits<std::uint64_t>::max() ||
        step.tick.value() != impl_->completedTick + 1)
        return R::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "RTS requires contiguous positive simulation frames"));
    if (impl_->pending && (impl_->pending->tick != step.tick || impl_->pending->delta != step.delta))
        return reject<spawn::AdvanceReport>("Retry requires the exact pending domain frame");
    Guard guard(impl_->busy);
    auto  dirty = impl_->session->navigationNeedsRefresh(impl_->map);
    if (!dirty) return R::failure(dirty.status());
    if (dirty.value()) return reject<spawn::AdvanceReport>("Private map navigation changed unexpectedly");
    ecs::ScopedTable scope(*impl_->table);
    if (impl_->phase == Impl::Phase::Ready) {
        impl_->pending = step;
        impl_->phase   = Impl::Phase::DomainExecuting;
        try {
            auto result = impl_->domain.step(step, impl_->executor);
            if (!result) {
                impl_->phase = Impl::Phase::Faulted;
                return R::failure(result.status());
            }
        } catch (...) {
            impl_->phase = Impl::Phase::Faulted;
            throw;
        }
        impl_->phase = Impl::Phase::SpawnPending;
    }
    // Cache remains valid until the next domain step. Repeated facts are canonically deduplicated.
    auto result = [&]() -> R {
        try {
            auto advanced = impl_->session->advance(impl_->map, step, impl_->domain.frameDamageEvents(), {});
            if (!advanced && impl_->session->spawning().world().lastConsumedTick() == step.tick)
                impl_->phase = Impl::Phase::Faulted;
            return advanced;
        } catch (...) {
            if (impl_->session->spawning().world().lastConsumedTick() == step.tick) impl_->phase = Impl::Phase::Faulted;
            throw;
        }
    }();
    if (!result) return result;
    impl_->completedTick = step.tick.value();
    impl_->pending.reset();
    impl_->phase = Impl::Phase::Ready;
    return result;
}
Result<Value> RTSMapRuntime::captureState() const {
    if (impl_->busy) return reject<Value>("RTS runtime is busy");
    Guard            guard(impl_->busy);
    ecs::ScopedTable scope(*impl_->table);
    auto             state       = impl_->domain.inspectState();
    auto             diagnostics = impl_->session->spawning().world().captureDiagnostics();
    if (!diagnostics) return Result<Value>::failure(diagnostics.status());
    Value::Array sources;
    for (const auto& row : diagnostics.value().sources) {
        if (row.state.total > std::uint64_t(std::numeric_limits<std::int64_t>::max()))
            return Result<Value>::failure(
                Diagnostic::error(DiagnosticCode::Unsupported, "Source count exceeds script integer range"));
        sources.push_back(Value::object({{"id", row.state.id},
                                         {"alive", std::int64_t(row.state.alive)},
                                         {"total", std::int64_t(row.state.total)},
                                         {"wave", std::int64_t(row.state.wave)},
                                         {"completed", row.state.phase == spawn::Phase::Completed}}));
    }
    state.set("sources", Value(std::move(sources)));
    if (impl_->completedTick > std::uint64_t(std::numeric_limits<std::int64_t>::max()))
        return Result<Value>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "Tick exceeds script integer range"));
    state.set("tick", Value(std::int64_t(impl_->completedTick)));
    const char* phase = impl_->phase == Impl::Phase::Ready          ? "ready"
                        : impl_->phase == Impl::Phase::SpawnPending ? "spawn_pending"
                        : impl_->phase == Impl::Phase::Faulted      ? "faulted"
                        : impl_->phase == Impl::Phase::Closed       ? "closed"
                                                                    : "closing";
    state.set("phase", Value(phase));
    state.set("crowd_agents", Value(std::int64_t(impl_->crowd.getAgentCount())));
    state.set("movement_surface", Value(std::int64_t(impl_->crowd.getFieldStatus().surface.surface.value())));
    return Result<Value>::success(std::move(state));
}
Result<RTSRuntimeCheckpoint> RTSMapRuntime::captureCheckpoint(PersistentId epoch, Revision revision) const {
    using R = Result<RTSRuntimeCheckpoint>;
    if (impl_->busy || (impl_->phase != Impl::Phase::Ready && impl_->phase != Impl::Phase::SpawnPending))
        return reject<RTSRuntimeCheckpoint>("RTS runtime cannot freeze while busy, faulted or closing");
    if (epoch.isNil() || revision == Revision::zero() || impl_->table->is_deferred())
        return reject<RTSRuntimeCheckpoint>("Checkpoint needs nonnil identity/revision and an idle actual world");
    Guard            guard(impl_->busy);
    ecs::ScopedTable scope(*impl_->table);
    const auto       completed = SimulationTick(impl_->completedTick);
    const bool       pending   = impl_->phase == Impl::Phase::SpawnPending;
    if (pending != impl_->pending.has_value())
        return reject<RTSRuntimeCheckpoint>("Runtime phase and pending frame disagree");
    const auto domainTick = pending ? impl_->pending->tick : completed;
    auto       context =
        impl_->domain.validateCheckpointContext(impl_->crowd, impl_->navigation, impl_->sensing, impl_->damage,
                                                *impl_->registry, rts::RTSCheckpointAttachment::Attached);
    auto contextStatus = context.status();

    auto damage = impl_->domain.captureDamage(impl_->damage);
    if (!damage) return R::failure(damage.status());
    auto definitions = impl_->domain.captureDefinitions(*impl_->registry);
    if (!definitions) return R::failure(definitions.status());
    auto crowdState = impl_->domain.captureCrowd(epoch);
    if (!crowdState) return R::failure(crowdState.status());
    if (crowdState.value().frame()->get().tick != domainTick)
        return reject<RTSRuntimeCheckpoint>("Crowd clock disagrees with runtime frame responsibility");
    auto navigation = impl_->domain.captureNavigation(epoch, impl_->navigation);
    if (!navigation) return R::failure(navigation.status());
    auto map = impl_->session->capture(impl_->map, epoch, revision);
    if (!map) return R::failure(map.status());
    if (map.value().coreTick() != completed)
        return reject<RTSRuntimeCheckpoint>("Spawn consumed tick disagrees with runtime completed frame");
    auto attributes = impl_->domain.captureUnitAttributes(epoch);
    if (!attributes) return R::failure(attributes.status());
    auto weapons = impl_->domain.captureWeapons(epoch);
    if (!weapons) return R::failure(weapons.status());
    auto actions = impl_->executor.capture(epoch, impl_->table, {});
    if (!actions) return R::failure(actions.status());
    auto events = impl_->domain.captureFrameState();
    if (!events) return R::failure(events.status());
    auto sensed = impl_->sensing.capture();
    if (!sensed) return R::failure(sensed.status());
    auto combat = impl_->domain.captureCombatState();
    if (!combat) return R::failure(combat.status());
    const auto subjects = sensed.value().subjects();
    for (const auto& id : combat.value().mirroredSubjects) {
        const auto found = std::lower_bound(subjects.begin(), subjects.end(), id,
                                            [](const auto& subject, const auto& key) { return subject.id < key; });
        if (found == subjects.end() || found->id != id)
            return reject<RTSRuntimeCheckpoint>("Combat mirror ownership has no captured sensing fact");
    }
    for (const auto& id : combat.value().blockedSubjects)
        if (!combat.value().mirroredSubjects.contains(id))
            return reject<RTSRuntimeCheckpoint>("Blocked combat source is not mirrored");
    auto result = std::make_unique<RTSRuntimeCheckpoint::Impl>(
        std::move(contextStatus), std::move(damage).takeValue(), std::move(definitions).takeValue(),
        std::move(map).takeValue(), std::move(crowdState).takeValue(), std::move(actions).takeValue(),
        std::move(events).takeValue(), std::move(sensed).takeValue(), std::move(combat).takeValue(),
        std::move(weapons).takeValue(), std::move(attributes).takeValue(), std::move(navigation).takeValue(), completed,
        impl_->pending, pending ? RTSCheckpointPhase::SpawnPending : RTSCheckpointPhase::Ready);
    auto joined = rebuildRuntimeMembers(result->map.roots()->get(), result->crowdState, result->members);
    if (!joined) return R::failure(joined.status());
    return R::success(RTSRuntimeCheckpoint(std::move(result)));
}
Result<void> RTSMapRuntime::close() {
    if (impl_->busy) return reject<void>("RTS runtime is busy");
    if (impl_->phase == Impl::Phase::Closed) return Result<void>::success();
    Guard guard(impl_->busy);
    impl_->phase = Impl::Phase::Closing;
    ecs::ScopedTable scope(*impl_->table);
    auto             closed = impl_->session->close(spawn::DetachPolicy::RemoveUnits);
    if (!closed) return closed;
    impl_->domain.setNavigationProvider(nullptr);
    impl_->domain.setCombatProviders(nullptr, nullptr);
    impl_->domain.setCrowdProvider(nullptr);
    impl_->pending.reset();
    impl_->phase = Impl::Phase::Closed;
    return Result<void>::success();
}
}  // namespace eve::map_rts_spawn
