#include "rts/RTSCombatRestore.h"
#include <algorithm>
#include <map>
#include "rts/RTS.h"
#include "rts/RTSCombatStateInternal.h"
#include "rts/RTSSnapshotInternal.h"
#include "sensing/Sensing.h"
#include "sensing/SensingCapture.h"
namespace eve::rts {
Result<void> combat_state_internal::validate(const RTSCombatSnapshot& state) {
    if (!std::includes(state.mirroredSubjects.begin(), state.mirroredSubjects.end(), state.blockedSubjects.begin(),
                       state.blockedSubjects.end()))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Blocked subjects lack mirror ownership"));
    for (const auto& id : state.mirroredSubjects) {
        const auto parsed = PersistentId::parse(id);
        if (!parsed || parsed->isNil() || parsed->format() != id)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "Combat mirror requires canonical nonnil subject identity"));
    }
    return Result<void>::success();
}
struct PreparedRTSCombatRestore::Impl {
    struct Binding {
        SubjectRef        subject;
        ecs::EntityHandle handle;
    };
    /** @ownership Non-owning observations, never dereferenced without witnesses and a locked Table.
     * @lifetime Valid only while the original physical target/provider remain alive; owner-thread only.
     */
    RTS*                            target;
    LifetimeWitness                 targetLifetime;
    std::weak_ptr<ecs::Table>       world;
    sensing::SensingWorld*          provider;
    LifetimeWitness                 providerLifetime;
    sensing::PreparedSensingRestore sensing;
    CombatFireSystem::State         combat;
    std::vector<Binding>            bindings;
    Impl(RTS& targetValue, std::shared_ptr<ecs::Table> table, sensing::SensingWorld& providerValue,
         sensing::PreparedSensingRestore prepared)
        : target(&targetValue),
          targetLifetime(targetValue.lifetimeWitness()),
          world(table),
          provider(&providerValue),
          providerLifetime(providerValue.lifetimeWitness()),
          sensing(std::move(prepared)) {}
};
PreparedRTSCombatRestore::PreparedRTSCombatRestore(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedRTSCombatRestore::~PreparedRTSCombatRestore()                                                     = default;
PreparedRTSCombatRestore::PreparedRTSCombatRestore(PreparedRTSCombatRestore&&) noexcept                   = default;
PreparedRTSCombatRestore&        PreparedRTSCombatRestore::operator=(PreparedRTSCombatRestore&&) noexcept = default;
Result<PreparedRTSCombatRestore> PreparedRTSCombatRestore::prepare(RTS& target, const RTSCombatSnapshot& combat,
                                                                   const sensing::SensingCapture& sensing) {
    using R = Result<PreparedRTSCombatRestore>;
    if (!target.ownedWorld_)
        return R::failure(Diagnostic::error(DiagnosticCode::Unsupported, "Combat restore requires an owned RTS Table"));
    if (target.ownedWorld_->is_deferred())
        return R::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Combat restore requires an idle Table"));
    if (!target.sensing_ || !target.sensingLifetime_.isAlive())
        return R::failure(Diagnostic::error(DiagnosticCode::Unsupported, "Combat restore requires live sensing"));
    auto valid = combat_state_internal::validate(combat);
    if (!valid) return R::failure(valid.status());
    auto prepared = sensing::PreparedSensingRestore::prepare(sensing);
    if (!prepared) return R::failure(prepared.status());
    auto plan = std::make_unique<Impl>(target, target.ownedWorld_, *target.sensing_, std::move(prepared).value());
    plan->combat.mirroredSubjects = combat.mirroredSubjects;
    plan->combat.blockedSubjects  = combat.blockedSubjects;
    ecs::ScopedTable                     scope(*target.ownedWorld_);
    std::map<std::string, Impl::Binding> bodies;
    for (const auto* handles : {&target.units_, &target.buildings_}) {
        for (const auto handle : *handles) {
            if (handle.table != target.ownedWorld_.get())
                return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Foreign combat root Table"));
            auto subject = snapshot_internal::subjectOf(handle);
            if (!subject.isValid()) continue;
            if (!bodies.emplace(subject.format(), Impl::Binding{subject, handle}).second)
                return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Ambiguous combat body identity"));
        }
    }
    plan->bindings.reserve(combat.mirroredSubjects.size());
    for (const auto& id : combat.mirroredSubjects) {
        auto       body  = bodies.find(id);
        const auto facts = sensing.subjects();
        const auto fact  = std::lower_bound(facts.begin(), facts.end(), id,
                                            [](const auto& value, const auto& key) { return value.id < key; });
        if (body == bodies.end() || fact == facts.end() || fact->id != id)
            return R::failure(Diagnostic::error(DiagnosticCode::Conflict, "Mirror lacks actual body or sensing fact"));
        plan->bindings.push_back(body->second);
    }
    return R::success(PreparedRTSCombatRestore(std::move(plan)));
}
Result<void> RTS::restoreCombat(PreparedRTSCombatRestore&& prepared) {
    auto* plan = prepared.impl_.get();
    if (!plan || plan->target != this || !plan->targetLifetime.isAlive())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Consumed or foreign combat restore"));
    auto world = plan->world.lock();
    if (!world || world != ownedWorld_ || world->is_deferred())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Combat restore world unavailable or busy"));
    if (!plan->providerLifetime.isAlive() || sensing_ != plan->provider || !sensingLifetime_.isAlive())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Combat restore sensing provider changed"));
    ecs::ScopedTable scope(*world);
    for (const auto& binding : plan->bindings)
        if (snapshot_internal::subjectOf(binding.handle) != binding.subject)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "Combat restore body incarnation changed"));
    auto published = sensing_->restore(std::move(plan->sensing));
    if (!published) return published;
    combatState_.mirroredSubjects.swap(plan->combat.mirroredSubjects);
    combatState_.blockedSubjects.swap(plan->combat.blockedSubjects);
    prepared.impl_.reset();
    return Result<void>::success(Status::success(StatusCode::Applied));
}
}  // namespace eve::rts
