#include "rpg/ActionEncounter.h"
#include <cmath>
#include "common/Capability.h"
#include "rpg/ActionDamageSink.h"
#include "rpg/ActionKillReward.h"
#include "rpg/RPGActor.h"

namespace eve::rpg {
struct ActionEncounter::Impl {
    RPGDamageTarget                       player, enemy;
    std::optional<RPGActionDamageReceipt> pending;
    int                                   lastTick = -1;
    RPGActionDamageSink                   sink{[this](ecs::EntityHandle handle) -> std::optional<RPGDamageTarget> {
        auto same = [&](const RPGDamageTarget& value) {
            return handle.table == value.actor.table && handle.type == value.actor.type &&
                   handle.id == value.actor.id && handle.generation == value.actor.generation;
        };
        if (same(player)) return player;
        if (same(enemy)) return enemy;
        return std::nullopt;
    }};
};
ActionEncounter::ActionEncounter() : impl_(std::make_unique<Impl>()) {}
ActionEncounter::~ActionEncounter() = default;
void         ActionEncounter::close() noexcept { impl_.reset(); }
Result<void> ActionEncounter::bind(RPGActor& player, RPGActor& enemy, std::string_view playerId,
                                   std::string_view enemyId) {
    const auto source = PersistentId::parse(playerId);
    const auto target = PersistentId::parse(enemyId);
    if (!source || !target || !SubjectRef::fromPersistentId(*source).isValid() ||
        !SubjectRef::fromPersistentId(*target).isValid() || *source == *target || &player == &enemy)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Distinct actors and life UUIDs required"));
    if (!impl_) impl_ = std::make_unique<Impl>();
    if (impl_->pending)
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Claim pending reward before rebinding"));
    impl_->player   = {ecs::handle_of(&player), SubjectRef::fromPersistentId(*source)};
    impl_->enemy    = {ecs::handle_of(&enemy), SubjectRef::fromPersistentId(*target)};
    impl_->lastTick = -1;
    return Result<void>::success();
}
Result<double> ActionEncounter::hit(bool playerAttacks, double amount, int tick) {
    if (!impl_)
        return Result<double>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "Encounter is closed"));
    if (tick < 0 || tick < impl_->lastTick || !std::isfinite(amount) || amount <= 0.0)
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Positive finite damage and monotonic tick required"));
    if (impl_->pending)
        return Result<double>::failure(Diagnostic::error(DiagnosticCode::Conflict, "Reward delivery is pending"));
    const auto source   = playerAttacks ? impl_->player : impl_->enemy;
    const auto target   = playerAttacks ? impl_->enemy : impl_->player;
    auto*      attacker = dynamic_cast<RPGActor*>(ecs::try_get(source.actor));
    auto*      defender = dynamic_cast<RPGActor*>(ecs::try_get(target.actor));
    if (!attacker || !defender)
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::StaleHandle, "Encounter participant was destroyed"));
    if (attacker->getCurrent("hp") <= 0 || defender->getCurrent("hp") <= 0)
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation, "Both participants must be alive"));
    // The registry chooses its first accepting provider. Reject ambiguous ownership
    // before any provider can commit damage or enqueue a receipt for another encounter.
    std::size_t accepting  = 0;
    bool        ownsTarget = false;
    cap::forEach<action::IActionDamageSink>([&](action::IActionDamageSink* sink) {
        if (!sink->supports(target.actor)) return;
        ++accepting;
        ownsTarget = ownsTarget || sink == &impl_->sink;
    });
    if (accepting != 1 || !ownsTarget)
        return Result<double>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "Encounter target must have exactly one owning damage provider"));
    auto registry = action::ActionNotifyRegistry::withBuiltins();
    if (!registry) return Result<double>::failure(registry.status());
    action::ActionTimelineEvent event;
    event.type    = *LogicalId::parse("combat:damage");
    event.payload = {{"damageType", "Damage.Physical"}, {"amount", amount}};
    action::ActionNotifyContext context;
    context.source  = source.actor;
    context.targets = {target.actor};
    context.tick    = SimulationTick(static_cast<unsigned long long>(tick));
    auto dispatched = registry.value().dispatch(event, context);
    if (!dispatched) return Result<double>::failure(dispatched.status());
    auto receipts = impl_->sink.takeReceipts();
    if (receipts.size() != 1)
        return Result<double>::failure(
            Diagnostic::error(DiagnosticCode::InvariantViolation, "Encounter requires sole damage provider ownership"));
    impl_->lastTick = tick;
    if (playerAttacks && receipts.front().killed) impl_->pending = receipts.front();
    return Result<double>::success(receipts.front().applied);
}
Result<void> ActionEncounter::claim(GameState& state, Tracker& tracker, std::string_view encounterId) {
    if (!impl_ || !impl_->pending) return Result<void>::success(Status::success(StatusCode::NoOp));
    auto result = ActionKillReward::settle(impl_->player, state, tracker, *impl_->pending, encounterId);
    if (!result) return Result<void>::failure(result.status());
    impl_->pending.reset();
    return Result<void>::success(result.status());
}
bool ActionEncounter::hasPendingReward() const { return impl_ && impl_->pending.has_value(); }
}  // namespace eve::rpg
