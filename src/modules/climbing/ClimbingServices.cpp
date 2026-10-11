#include "climbing/ClimbingServices.h"

#include "climbing/ClimbingEventValue.h"

#include "common/Capability.h"
#include "common/CrashLog.h"
#include "common/ScriptProvider.h"
#include "common/Value.h"
#include "physics/World3D.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace eve::climbing {
namespace {

bool commandMatches(ClimbingCommandRequirement requirement, ClimbingCommand command) {
    if (requirement == ClimbingCommandRequirement::Any) return true;
    return (requirement == ClimbingCommandRequirement::Jump && command == ClimbingCommand::Jump) ||
           (requirement == ClimbingCommandRequirement::Climb && command == ClimbingCommand::Climb) ||
           (requirement == ClimbingCommandRequirement::Drop && command == ClimbingCommand::Drop) ||
           (requirement == ClimbingCommandRequirement::Sprint && command == ClimbingCommand::Sprint) ||
           (requirement == ClimbingCommandRequirement::Crouch && command == ClimbingCommand::Crouch);
}

}  // namespace

eve::Result<ClimbingServiceStart> ClimbingServiceSelectionSystem::tryStart(
    ClimbingRuntime& runtime, physics::World3D& world, const ClimbingPose& pose, ClimbingIntent& intent,
    ClimbingCommand command, eve::SimulationTick tick, eve::SimulationTick lastGroundedTick,
    ClimbingServiceSubject subject) {
    if (subject.isZero())
        return eve::Result<ClimbingServiceStart>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                                 "service subject must be non-zero",
                                                                                 "subject", {}, "climbing.services"));
    if (!ClimbingInputSystem::peek(intent, command, tick))
        return eve::Result<ClimbingServiceStart>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "climbing.input.no_match", "intent.commands", {}, "climbing.services"));

    ClimbingPose effectivePose = pose;
    if (!effectivePose.grounded && command != ClimbingCommand::Drop &&
        ClimbingInputSystem::coyoteWindowState(tick, lastGroundedTick, runtime.profile_.coyoteTicks) ==
            ClimbingCoyoteState::Eligible)
        effectivePose.grounded = true;
    auto prepared = runtime.prepareBegin(world, effectivePose, tick);
    if (!prepared) return eve::Result<ClimbingServiceStart>::failure(prepared.status());
    if (!commandMatches(prepared.value().action.requiredCommand, command))
        return eve::Result<ClimbingServiceStart>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "selected action does not accept the buffered command",
            "action.requiredCommand", {}, "climbing.services"));

    if (!prepared.value().action.requiredConditionTags.empty()) {
        IClimbingConditionAuthority* conditions = eve::cap::query<IClimbingConditionAuthority>();
        if (!conditions)
            return eve::Result<ClimbingServiceStart>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Unsupported, "required climbing condition provider is absent", "conditions", {},
                "climbing.services"));
        auto decision = conditions->evaluate(subject, prepared.value().action.requiredConditionTags, tick);
        if (!decision) return eve::Result<ClimbingServiceStart>::failure(decision.status());
        if (decision.value() != ClimbingConditionDecision::Allowed)
            return eve::Result<ClimbingServiceStart>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::PreconditionViolation, "climbing action conditions were denied", "conditions", {},
                "climbing.services"));
        prepared.value().conditionsSatisfied = true;
    }

    IClimbingStaminaAuthority* stamina = nullptr;
    ClimbingStaminaReservation reservation = ClimbingStaminaReservation::zero();
    ClimbingOptionalServiceState staminaState = ClimbingOptionalServiceState::Disabled;
    if (prepared.value().action.staminaCost > 0.f &&
        runtime.profile_.staminaPolicy != ClimbingStaminaPolicy::Disabled) {
        stamina = eve::cap::query<IClimbingStaminaAuthority>();
        if (!stamina && runtime.profile_.staminaPolicy == ClimbingStaminaPolicy::RequireProvider)
            return eve::Result<ClimbingServiceStart>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, "required climbing stamina provider is absent",
                                       "stamina", {}, "climbing.services"));
        if (!stamina) {
            staminaState = ClimbingOptionalServiceState::ProviderAbsent;
        } else {
            if (!runtime.profile_.staminaAdapter.empty() &&
                stamina->adapterId() != runtime.profile_.staminaAdapter)
                return eve::Result<ClimbingServiceStart>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Unsupported, "configured climbing stamina adapter is not available",
                    "staminaAdapter", {}, "climbing.services"));
            auto reserved = stamina->prepare(subject, prepared.value().action.staminaCost, tick);
            if (!reserved) return eve::Result<ClimbingServiceStart>::failure(reserved.status());
            reservation = reserved.value();
            if (reservation.isZero())
                return eve::Result<ClimbingServiceStart>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvariantViolation, "stamina provider returned a zero reservation", "stamina",
                    {}, "climbing.services"));
            staminaState = ClimbingOptionalServiceState::Applied;
        }
    }

    const ClimbingIntent originalIntent = intent;
    auto consumed = ClimbingInputSystem::consume(intent, command, tick, prepared.value().executionId);
    if (!consumed || !consumed.value()) {
        intent = originalIntent;
        if (stamina) stamina->cancelPrepared(reservation);
        if (!consumed) return eve::Result<ClimbingServiceStart>::failure(consumed.status());
        return eve::Result<ClimbingServiceStart>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "eligible climbing input changed before commit",
                                   "intent.commands", {}, "climbing.services"));
    }
    auto committed = runtime.commitBegin(std::move(prepared).takeValue());
    if (!committed) {
        intent = originalIntent;
        if (stamina) stamina->cancelPrepared(reservation);
        return eve::Result<ClimbingServiceStart>::failure(committed.status());
    }
    if (stamina) stamina->commitPrepared(reservation, committed.value().executionId);
    return eve::Result<ClimbingServiceStart>::success({std::move(committed).takeValue(), staminaState},
                                                       eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ClimbingOptionalServiceState> dispatchClimbingEvents(
    ClimbingServiceSubject subject, std::span<const ClimbingEvent> events) {
    if (subject.isZero())
        return eve::Result<ClimbingOptionalServiceState>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "service subject must be non-zero", "subject",
                                   {}, "climbing.services"));
    IClimbingEventSink* sink = eve::cap::query<IClimbingEventSink>();
    if (!sink)
        return eve::Result<ClimbingOptionalServiceState>::success(
            ClimbingOptionalServiceState::ProviderAbsent, eve::Status::success(eve::StatusCode::NoOp));
    auto published = sink->publish(subject, events);
    if (!published) return eve::Result<ClimbingOptionalServiceState>::failure(published.status());
    return eve::Result<ClimbingOptionalServiceState>::success(ClimbingOptionalServiceState::Applied,
                                                               eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<ClimbingOptionalServiceState> applyClimbingPose(ClimbingServiceSubject subject,
                                                            const ClimbingAdvance& advance) {
    if (subject.isZero())
        return eve::Result<ClimbingOptionalServiceState>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "service subject must be non-zero", "subject",
                                   {}, "climbing.services"));
    IClimbingPoseAdapter* adapter = eve::cap::query<IClimbingPoseAdapter>();
    if (!adapter)
        return eve::Result<ClimbingOptionalServiceState>::success(
            ClimbingOptionalServiceState::ProviderAbsent, eve::Status::success(eve::StatusCode::NoOp));
    auto applied = adapter->apply(subject, advance);
    if (!applied) return eve::Result<ClimbingOptionalServiceState>::failure(applied.status());
    return eve::Result<ClimbingOptionalServiceState>::success(ClimbingOptionalServiceState::Applied,
                                                              eve::Status::success(eve::StatusCode::Applied));
}

// ---------------------------------------------------------------------------
// 脚本提供方接缝：IClimbingEventSink
// ---------------------------------------------------------------------------

namespace {

/**
 * @brief 脚本提供方实现的 `IClimbingEventSink`。
 *
 * 每次派发编成**一张表**（`subject` + `events` 数组）交给脚本的 `publishClimbingEvents`：这就是
 * 决定③要的"对象参数"。事件记录用的是 `climbingEventValue`（绑定与快照编解码的同一份映射），
 * 所以脚本在这里看到的字段与它在 `eve.ClimbingRuntime` 上看到的完全一致。
 */
class ScriptClimbingEventSink final : public IClimbingEventSink {
public:
    explicit ScriptClimbingEventSink(eve::ScriptProviderHandle handle) : handle_(std::move(handle)) {}

    /** @brief 本适配器所服务的那一代脚本提供方。 */
    [[nodiscard]] const eve::ScriptProviderHandle& handle() const noexcept { return handle_; }

    eve::Result<void> publish(ClimbingServiceSubject subject, std::span<const ClimbingEvent> events) override {
        const eve::ScriptArg args[] = {
            eve::ScriptArg::fromTable({{"subject", eve::Value::integer(static_cast<std::int64_t>(subject.value()))},
                                       {"events", climbingEventArrayValue(events)}})};
        return eve::ScriptProviderRegistry::instance().call(handle_, "publishClimbingEvents", args);
    }

private:
    eve::ScriptProviderHandle handle_;
};

/** @brief 进程级持有脚本适配器（理由同武器投射物接缝：能力表是进程级的）。 */
struct ScriptClimbingEventProviders {
    std::vector<std::unique_ptr<ScriptClimbingEventSink>> live;
    std::vector<std::unique_ptr<ScriptClimbingEventSink>> retired;
};

ScriptClimbingEventProviders& scriptClimbingEventProviders() {
    static ScriptClimbingEventProviders state;
    return state;
}

}  // namespace

void declareScriptClimbingEventSeam() {
    ScriptClimbingEventProviders& owned = scriptClimbingEventProviders();
    eve::ScriptProviderRegistry::instance().declareCapability(
        IClimbingEventSink::capabilityName,
        [&owned](const eve::ScriptProviderHandle& handle) -> eve::Result<void> {
            // 单实现槽位：`provide` 会顶掉当前实现，顶掉必须可见（否则原生 sink 会"悄悄失效"，
            // 而症状只是事件不再被派发）。
            if (eve::cap::query<IClimbingEventSink>() != nullptr) {
                eve::recordLogEvent("warn", "script climbing event provider '" + handle.name +
                                                "' replaced the provider that owned IClimbingEventSink");
            }
            auto                     adapter = std::make_unique<ScriptClimbingEventSink>(handle);
            ScriptClimbingEventSink* raw     = adapter.get();
            eve::cap::provide<IClimbingEventSink>(raw);
            owned.live.push_back(std::move(adapter));
            return eve::Result<void>::success();
        },
        [&owned](const eve::ScriptProviderHandle& handle) -> eve::Result<void> {
            const auto match = std::find_if(owned.live.begin(), owned.live.end(),
                                            [&handle](const std::unique_ptr<ScriptClimbingEventSink>& adapter) {
                                                return adapter->handle() == handle;
                                            });
            if (match == owned.live.end()) {
                // 幂等：后置条件是"这一代没有适配器"，模块退出时可能已经撤过了。
                return eve::Result<void>::success();
            }
            eve::cap::revoke<IClimbingEventSink>(match->get());
            owned.retired.push_back(std::move(*match));
            owned.live.erase(match);
            return eve::Result<void>::success();
        });
}

void withdrawScriptClimbingEventSeam() {
    ScriptClimbingEventProviders& owned = scriptClimbingEventProviders();
    for (auto& adapter : owned.live) {
        eve::cap::revoke<IClimbingEventSink>(adapter.get());
        owned.retired.push_back(std::move(adapter));
    }
    owned.live.clear();
    eve::ScriptProviderRegistry::instance()
        .removeCapability(IClimbingEventSink::capabilityName)
        .ignore("module teardown may already have dropped this seam");
}

Climbing::Climbing() { declareScriptClimbingEventSeam(); }

Climbing::~Climbing() { withdrawScriptClimbingEventSeam(); }

}  // namespace eve::climbing
