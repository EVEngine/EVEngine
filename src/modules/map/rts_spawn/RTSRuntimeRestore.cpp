#include "map/rts_spawn/RTSRuntimeRestore.h"
#include <array>
#include <limits>
#include <unordered_set>
#include "action/ActionCapture.h"
#include "combat/DamageCapture.h"
#include "common/ListenerStaging.h"
#include "common/StateAccess.h"
#include "definitions/DefinitionRegistryCapture.h"
#include "map/PathfinderState.h"
#include "map/rts_spawn/RTSMapCheckpoint.h"
#include "map/rts_spawn/RTSMapRestoreState.h"
#include "map/rts_spawn/RTSMapRuntimeInternal.h"
#include "map/rts_spawn/RTSRuntimeCheckpoint.h"
#include "map/rts_spawn/RTSRuntimeCheckpointInternal.h"
#include "map/spawn/MapSpawnAssets.h"
#include "map/spawn/MapSpawnBinding.h"
#include "rts/RTSActionCapture.h"
#include "rts/RTSAttributeCapture.h"
#include "rts/RTSCombatRestore.h"
#include "rts/RTSCrowdCapture.h"
#include "rts/RTSFrameRestore.h"
#include "rts/RTSNavigationCapture.h"
#include "rts/RTSWeaponCapture.h"
namespace eve::map_rts_spawn {
namespace {
template <class T>
Result<T> reject(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "map.rts.runtime.restore"));
}
struct Busy {
    bool& value;
    explicit Busy(bool& state) : value(state) { value = true; }
    ~Busy() { value = false; }
};
Result<std::vector<action::ActionEntityBinding>> actionBindings(rts::RTS&                    domain,
                                                                const rts::RTSActionCapture& captured) {
    using R = Result<std::vector<action::ActionEntityBinding>>;
    if (!captured.actions())
        return reject<std::vector<action::ActionEntityBinding>>(DiagnosticCode::PreconditionViolation,
                                                                "Native actions were moved");
    std::unordered_set<SubjectRef> seen;
    std::vector<SubjectRef>        wanted;
    const auto                     remember = [&](SubjectRef subject) {
        if (seen.insert(subject).second) wanted.push_back(subject);
    };
    for (const auto& row : captured.actions()->get().executions()) {
        if (row.source && row.source->presence == action::ActionEntityPresence::Present) remember(row.source->subject);
        for (const auto& reference : row.targets)
            if (reference.presence == action::ActionEntityPresence::Present) remember(reference.subject);
    }
    std::vector<action::ActionEntityBinding> result;
    result.reserve(wanted.size());
    for (auto subject : wanted) {
        auto handle = domain.resolveOwnedSubject(subject);
        if (!handle) return R::failure(handle.status());
        result.push_back({handle.value(), subject});
    }
    return R::success(std::move(result));
}
}  // namespace
struct PreparedRTSRuntimeRestore::Impl {
    std::shared_ptr<const RTSRuntimeCheckpoint> saved;
    RTSMapRestoreState                          map;
    bool                                        busy = false, consumed = false;
    Impl(std::shared_ptr<const RTSRuntimeCheckpoint> checkpoint, RTSMapRestoreState admitted)
        : saved(std::move(checkpoint)), map(std::move(admitted)) {}
};
PreparedRTSRuntimeRestore::PreparedRTSRuntimeRestore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedRTSRuntimeRestore::~PreparedRTSRuntimeRestore()                                               = default;
PreparedRTSRuntimeRestore::PreparedRTSRuntimeRestore(PreparedRTSRuntimeRestore&&) noexcept            = default;
PreparedRTSRuntimeRestore& PreparedRTSRuntimeRestore::operator=(PreparedRTSRuntimeRestore&&) noexcept = default;
Result<void>               validateRTSRuntimeBoundary(const RTSRuntimeCheckpoint& saved) {
    using R = Result<void>;
    if (!saved.phase() || !saved.map() || !saved.definitions() || !saved.damage() || !saved.crowd() ||
        !saved.crowdFrame() || !saved.navigation() || !saved.navigationProvider() || !saved.weapons() ||
        !saved.unitAttributes() || !saved.actions() || !saved.actions()->get().actions() || !saved.events() ||
        !saved.sensing() || !saved.combat() || saved.saveEpoch().isNil() || saved.revision().isZero())
        return reject<void>(DiagnosticCode::InvalidArgument, "Complete native game checkpoint required");
    auto context = saved.validateRestoreContext();
    if (!context) return R::failure(context.status());
    const auto epoch     = saved.saveEpoch();
    const auto completed = saved.completedTick();
    const bool pending   = *saved.phase() == RTSCheckpointPhase::SpawnPending;
    const auto frame     = saved.pendingStep();
    if (pending != frame.has_value() ||
        (frame && (frame->delta.nanoseconds() <= 0 || completed.value() == std::numeric_limits<std::uint64_t>::max() ||
                   frame->tick.value() != completed.value() + 1)))
        return reject<void>(DiagnosticCode::Conflict, "Native phase and pending frame responsibility differ");
    const auto domainTick = frame ? frame->tick : completed;
    if (saved.map()->get().saveEpoch() != epoch || saved.map()->get().revision() != saved.revision() ||
        saved.map()->get().coreTick() != completed || saved.crowd()->get().saveEpoch() != epoch ||
        saved.crowdFrame()->get().tick != domainTick || saved.navigation()->get().saveEpoch() != epoch ||
        saved.weapons()->get().saveEpoch() != epoch || saved.unitAttributes()->get().saveEpoch() != epoch ||
        saved.actions()->get().actions()->get().saveEpoch() != epoch ||
        saved.actions()->get().actions()->get().activePipeline() != action::ActionActivePipeline::LegacyPorts)
        return reject<void>(DiagnosticCode::Conflict,
                                          "Native participants disagree on epoch, clocks or action context");
    return R::success();
}
Result<PreparedRTSRuntimeRestore> PreparedRTSRuntimeRestore::prepare(std::shared_ptr<const RTSRuntimeCheckpoint> saved,
                                                                     const SnapshotHashProvider& hashProvider) {
    using R = Result<PreparedRTSRuntimeRestore>;
    if (!saved)
        return reject<PreparedRTSRuntimeRestore>(DiagnosticCode::InvalidArgument, "Runtime checkpoint required");
    auto boundary = validateRTSRuntimeBoundary(*saved);
    if (!boundary) return R::failure(boundary.status());
    auto mapOwner = std::shared_ptr<const RTSMapCheckpoint>(saved, &saved->map()->get());
    auto admitted = RTSMapRestoreState::prepare(std::move(mapOwner), hashProvider);
    if (!admitted) return R::failure(admitted.status());
    return R::success(
        PreparedRTSRuntimeRestore(std::make_unique<Impl>(std::move(saved), std::move(admitted).takeValue())));
}
Result<void> PreparedRTSRuntimeRestore::commit(std::unique_ptr<RTSMapRuntime>& owner) {
    if (!impl_ || impl_->busy || impl_->consumed)
        return reject<void>(DiagnosticCode::PreconditionViolation, "Native game restore token is busy or consumed");
    Busy tokenBusy(impl_->busy);
    // Declared before the old busy guard, so retirement happens only after its borrowed flag is released.
    std::unique_ptr<RTSMapRuntime> retired;
    std::optional<Busy>            oldBusy;
    if (owner) {
        auto& old = *owner->impl_;
        if (old.busy || old.table->is_deferred())
            return reject<void>(DiagnosticCode::PreconditionViolation, "Old native game is busy or inside a View");
        auto context = old.domain.validateCheckpointContext(
            old.crowd, old.navigation, old.sensing, old.damage, *old.registry,
            old.phase == RTSMapRuntime::Impl::Phase::Closed ? rts::RTSCheckpointAttachment::Detached
                                                            : rts::RTSCheckpointAttachment::Attached);
        if (!context) return Result<void>::failure(context.status());
        oldBusy.emplace(old.busy);
    }
    constexpr std::array<std::string_view, 4> families{IGameplayControlProvider::capabilityName,
                                                       IGameplayInstanceCatalog::capabilityName,
                                                       IStateQuery::capabilityName, IStateMutation::capabilityName};
    auto                                      stage = cap::ListenerStaging::begin(families);
    if (!stage) return Result<void>::failure(stage.status());
    auto             staging    = std::move(stage).takeValue();
    auto             candidate  = std::make_unique<RTSMapRuntime::Impl>();
    const auto       tableOwner = candidate->table;
    ecs::ScopedTable scope(*tableOwner);
    const auto&      saved    = *impl_->saved;
    auto             registry = definitions::DefinitionRegistry::prepareRestored(saved.definitions()->get());
    if (!registry) return Result<void>::failure(registry.status());
    candidate->registry = std::shared_ptr<definitions::DefinitionRegistry>(std::move(registry).takeValue());
    auto damage         = combat::DamageRuntime::prepareRestored(saved.damage()->get());
    if (!damage) return Result<void>::failure(damage.status());
    candidate->damage = std::move(*damage.value());
    candidate->domain.setCrowdProvider(&candidate->crowd);
    candidate->domain.setCombatProviders(&candidate->sensing, &candidate->damage);
    // Rebuild unarmed native roots first. Attaching content earlier would rematerialize default guns.
    auto roots = candidate->domain.rebuildState(saved.map()->get().roots()->get());
    if (!roots) return roots;
    candidate->domain.setDefinitionRegistry(candidate->registry.get());
    auto attributes = rts::PreparedRTSUnitAttributeRestore::prepare(candidate->domain, saved.unitAttributes()->get());
    if (!attributes) return Result<void>::failure(attributes.status());
    auto restoredAttributes = candidate->domain.restoreUnitAttributes(std::move(attributes).takeValue());
    if (!restoredAttributes) return restoredAttributes;
    auto weapons = rts::PreparedRTSWeaponRestore::prepare(candidate->domain, saved.weapons()->get());
    if (!weapons) return Result<void>::failure(weapons.status());
    auto pools = candidate->domain.restoreWeapons(std::move(weapons).takeValue());
    if (!pools) return Result<void>::failure(pools.status());
    candidate->restoredPools.emplace(std::move(pools).takeValue());
    auto crowd = rts::PreparedRTSCrowdRestore::prepare(candidate->domain, saved.crowd()->get());
    if (!crowd) return Result<void>::failure(crowd.status());
    auto restoredCrowd = candidate->domain.restoreCrowd(std::move(crowd).takeValue());
    if (!restoredCrowd) return restoredCrowd;
    auto mapCandidate = PreparedRTSMapSession::prepareRestored(candidate->map, impl_->map, candidate->domain,
                                                               std::vector<spawn::Point>{});
    if (!mapCandidate) return Result<void>::failure(mapCandidate.status());
    auto session = mapCandidate.value().commit(candidate->map);
    if (!session) return Result<void>::failure(session.status());
    candidate->session = std::move(session).takeValue();
    auto binding       = map_spawn::MapSpawnBinding::decode(saved.map()->get().assets()->binding());
    if (!binding) return Result<void>::failure(binding.status());
    auto* layer = candidate->map.getLayer(static_cast<int>(binding.value().navigationLayer()));
    if (!layer) return reject<void>(DiagnosticCode::Conflict, "Restored navigation layer is absent");
    auto navigation = map::Pathfinder::prepareRestored(saved.navigationProvider()->get(), std::ref(*layer));
    if (!navigation) return Result<void>::failure(navigation.status());
    candidate->navigation   = std::move(*navigation.value());
    auto restoredNavigation = candidate->domain.restoreNavigation(saved.navigation()->get(), candidate->navigation);
    if (!restoredNavigation) return restoredNavigation;
    auto bindings = actionBindings(candidate->domain, saved.actions()->get());
    if (!bindings) return Result<void>::failure(bindings.status());
    auto actions = rts::PreparedRTSActionRestore::prepare(saved.actions()->get(), candidate->table, bindings.value());
    if (!actions) return Result<void>::failure(actions.status());
    auto restoredActions = candidate->executor.restore(std::move(actions).takeValue());
    if (!restoredActions) return restoredActions;
    auto combat =
        rts::PreparedRTSCombatRestore::prepare(candidate->domain, saved.combat()->get(), saved.sensing()->get());
    if (!combat) return Result<void>::failure(combat.status());
    auto restoredCombat = candidate->domain.restoreCombat(std::move(combat).takeValue());
    if (!restoredCombat) return restoredCombat;
    auto frames = rts::PreparedRTSFrameRestore::prepare(saved.events()->get());
    if (!frames) return Result<void>::failure(frames.status());
    auto restoredFrames = candidate->domain.restoreFrameState(std::move(frames).takeValue());
    if (!restoredFrames) return restoredFrames;
    candidate->completedTick = saved.completedTick().value();
    candidate->pending       = saved.pendingStep();
    candidate->phase         = *saved.phase() == RTSCheckpointPhase::Ready ? RTSMapRuntime::Impl::Phase::Ready
                                                                           : RTSMapRuntime::Impl::Phase::SpawnPending;
    auto context             = candidate->domain.validateCheckpointContext(
        candidate->crowd, candidate->navigation, candidate->sensing, candidate->damage, *candidate->registry,
        rts::RTSCheckpointAttachment::Attached);
    if (!context) return context;
    auto ready     = std::unique_ptr<RTSMapRuntime>(new RTSMapRuntime(std::move(candidate)));
    auto published = staging->publish();
    if (!published) return published;
    // No callbacks or fallible work after listener publication; registration/dispatch share owner-thread affinity.
    retired         = std::move(owner);
    owner           = std::move(ready);
    impl_->consumed = true;
    return Result<void>::success(Status::success(StatusCode::Applied));
}
}  // namespace eve::map_rts_spawn
