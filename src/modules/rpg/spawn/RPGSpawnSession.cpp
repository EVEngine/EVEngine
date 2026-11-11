// ECS scope: delegates all actor creation/rollback to RPGSpawnFactory outside Views.
// Phase: bundle admission -> isolated source binding -> commit materialization -> owner exchange.
#include "rpg/spawn/RPGSpawnSession.h"
#include "common/ECS.h"
#include "rpg/spawn/RPGSpawnCapture.h"
#include "rpg/spawn/RPGSpawnFactory.h"
#include "rpg/spawn/RPGSpawnJointRestore.h"
namespace eve::rpg_spawn {
namespace {
template <class T>
Result<T> sessionFailure(std::string message) {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::PreconditionViolation, std::move(message), "rpg.spawn.session"));
}
struct SessionBusy {
    bool& flag;
    explicit SessionBusy(bool& value) : flag(value) { flag = true; }
    ~SessionBusy() { flag = false; }
};
}  // namespace
struct RPGSpawnSession::Impl {
    std::shared_ptr<ecs::Table>                     world;
    std::shared_ptr<RPGSpawnFactory>                factory;
    std::shared_ptr<spawn::SpawnProviders>          providers;
    std::unique_ptr<spawn::SpawnWorld>              core;
    std::vector<spawn::SpawnerHandle>               sources;
    std::vector<std::vector<spawn::LifecycleInput>> pending;
    bool                                            busy      = false;
    bool                                            accepting = true;
};
RPGSpawnSession::RPGSpawnSession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RPGSpawnSession::~RPGSpawnSession() = default;
std::shared_ptr<ecs::Table>           RPGSpawnSession::world() const noexcept { return impl_->world; }
const spawn::SpawnWorld&              RPGSpawnSession::spawning() const noexcept { return *impl_->core; }
std::span<const spawn::SpawnerHandle> RPGSpawnSession::sources() const noexcept { return impl_->sources; }
Result<ActorSnapshot> RPGSpawnSession::inspect(SubjectRef subject) const { return impl_->factory->inspect(subject); }
Result<spawn::AdvanceReport> RPGSpawnSession::advance(const SimulationStep&             step,
                                                      std::span<const rpg::VitalsEvent> events,
                                                      spawn::ActivationFrame            activation) {
    if (impl_->busy || !impl_->accepting) return sessionFailure<spawn::AdvanceReport>("Session is busy or retired");
    SessionBusy guard(impl_->busy);
    impl_->pending.emplace_back();
    auto lifecycle = impl_->factory->collectLifecycle(events);
    if (!lifecycle) {
        impl_->pending.pop_back();
        return Result<spawn::AdvanceReport>::failure(lifecycle.status());
    }
    impl_->pending.back() = std::move(lifecycle).takeValue();
    if (impl_->pending.back().empty()) impl_->pending.pop_back();
    std::vector<spawn::LifecycleInput> frame;
    for (const auto& batch : impl_->pending) frame.insert(frame.end(), batch.begin(), batch.end());
    auto result = impl_->core->advance(step, frame, activation);
    if (result) impl_->pending.clear();
    return result;
}
Result<void> RPGSpawnSession::enqueue(spawn::ControlCommand command) {
    if (!impl_->accepting) return sessionFailure<void>("Session is retired");
    return impl_->core->enqueue(std::move(command));
}
Result<Subscription> RPGSpawnSession::subscribe(Observer<spawn::SpawnEvent>::Callback callback) {
    if (!impl_->accepting) return sessionFailure<Subscription>("Session is retired");
    return impl_->core->subscribe(std::move(callback));
}
Result<SnapshotEnvelope> RPGSpawnSession::capture(PersistentId epoch, Revision revision,
                                                  const SnapshotHashProvider& hash) {
    using R = Result<SnapshotEnvelope>;
    if (impl_->busy || !impl_->accepting) return sessionFailure<SnapshotEnvelope>("Session is busy or retired");
    SessionBusy guard(impl_->busy);
    auto        core = impl_->core->capture(epoch, revision);
    if (!core) return R::failure(core.status());
    auto domain = impl_->factory->capture(epoch, core.value().unitReferences());
    if (!domain) return R::failure(domain.status());
    std::vector<spawn::SavedLifecycleInput> pending;
    for (const auto& batch : impl_->pending)
        for (const auto& input : batch) pending.push_back(spawn::savedLifecycleInput(input));
    auto coreEnvelope = core.value().seal(hash);
    if (!coreEnvelope) return R::failure(coreEnvelope.status());
    auto domainEnvelope = domain.value().seal(coreEnvelope.value().revision, coreEnvelope.value().tick, hash);
    if (!domainEnvelope) return R::failure(domainEnvelope.status());
    return RPGSpawnJointRestore::sealBundle(coreEnvelope.value(), domainEnvelope.value(), epoch, pending, hash);
}
PreparedRPGSpawnSession::PreparedRPGSpawnSession(std::unique_ptr<RPGSpawnSession>      session,
                                                 std::unique_ptr<RPGSpawnJointRestore> admitted)
    : session_(std::move(session)), admitted_(std::move(admitted)) {}
PreparedRPGSpawnSession::~PreparedRPGSpawnSession()                                                    = default;
PreparedRPGSpawnSession::PreparedRPGSpawnSession(PreparedRPGSpawnSession&&) noexcept                   = default;
PreparedRPGSpawnSession&        PreparedRPGSpawnSession::operator=(PreparedRPGSpawnSession&&) noexcept = default;
Result<PreparedRPGSpawnSession> PreparedRPGSpawnSession::prepare(
    const SnapshotEnvelope& input, PersistentId epoch, const SnapshotHashProvider& hash,
    std::shared_ptr<spawn::ISpawnPlacement> placement, std::shared_ptr<const rpg::SkillConditionBindings> conditions) {
    using R = Result<PreparedRPGSpawnSession>;
    if (!placement) return sessionFailure<PreparedRPGSpawnSession>("Actual placement owner is required");
    auto bundle = input;
    auto result = RPGSpawnJointRestore::prepareBundle(bundle, epoch, hash, std::move(conditions));
    if (!result) return R::failure(result.status());
    auto admitted = std::make_unique<RPGSpawnJointRestore>(std::move(result).takeValue());
    auto impl     = std::make_unique<RPGSpawnSession::Impl>();
    impl->world   = std::make_shared<ecs::Table>();
    auto factory  = RPGSpawnFactory::prepareRestored(impl->world, admitted->domainOwner());
    if (!factory) return R::failure(factory.status());
    impl->factory = std::move(factory).takeValue();
    impl->providers =
        std::make_shared<spawn::SpawnProviders>(spawn::SpawnProviders{std::move(placement), impl->factory});
    auto core = admitted->bindCore(impl->providers);
    if (!core) return R::failure(core.status());
    impl->core    = std::make_unique<spawn::SpawnWorld>(std::move(core).takeValue());
    auto bindings = impl->core->restoreBindings();
    if (!bindings) return R::failure(bindings.status());
    impl->sources.reserve(bindings.value().sources.size());
    for (const auto& source : bindings.value().sources) impl->sources.push_back(source.lifetime.source());
    std::vector<spawn::LifecycleInput> pending;
    pending.reserve(admitted->pendingLifecycle().size());
    for (const auto& input : admitted->pendingLifecycle()) {
        auto source = impl->core->resolveSavedSource(input.source);
        if (!source) return R::failure(source.status());
        pending.push_back({input.subject, input.sequence, input.reason, source.value().source()});
    }
    if (!pending.empty()) impl->pending.push_back(std::move(pending));
    auto session = std::unique_ptr<RPGSpawnSession>(new RPGSpawnSession(std::move(impl)));
    return R::success(PreparedRPGSpawnSession(std::move(session), std::move(admitted)));
}
Result<std::unique_ptr<RPGSpawnSession>> PreparedRPGSpawnSession::commit(std::unique_ptr<RPGSpawnSession>& target) {
    using R = Result<std::unique_ptr<RPGSpawnSession>>;
    if (!session_ || !admitted_ || committing_ || (target && target->impl_->busy))
        return sessionFailure<std::unique_ptr<RPGSpawnSession>>("Candidate consumed or session operation in progress");
    SessionBusy candidateGuard(committing_);
    // @lifetime Synchronous guard observes the still-owned target object; pointer exchange never moves that object.
    struct OldBusy {
        bool* flag;
        ~OldBusy() {
            if (flag) *flag = false;
        }
    } oldGuard{target ? &target->impl_->busy : nullptr};
    if (oldGuard.flag) *oldGuard.flag = true;
    if (!materialized_) {
        auto created = session_->impl_->factory->materializeSavedActors(*session_->impl_->core, *admitted_);
        if (!created) return R::failure(created.status());
        materialized_ = true;
    }
    if (target) {
        auto retired = target->impl_->core->retireSources();
        if (!retired) return R::failure(retired.status());
        target->impl_->accepting = false;
    }
    auto previous = std::move(target);
    target        = std::move(session_);
    admitted_.reset();
    return R::success(std::move(previous));
}
}  // namespace eve::rpg_spawn
