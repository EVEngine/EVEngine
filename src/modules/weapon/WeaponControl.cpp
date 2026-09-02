#include "weapon/WeaponControl.h"

#include "common/Capability.h"
#include "common/ResourceAccount.h"
#include "transaction/Transaction.h"
#include "weapon/WeaponAction.h"

#include <algorithm>
#include <limits>

namespace eve::weapon {
namespace {

template <typename T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

LogicalId id(std::string_view text) { return LogicalId::parse(text).value(); }

Result<const Value::Object*> object(const Value& value) {
    const auto* result = value.getIf<Value::Object>();
    if (!result)
        return failure<const Value::Object*>(DiagnosticCode::InvalidArgument,
                                             "weapon parameters must be an object", "parameters");
    return Result<const Value::Object*>::success(result);
}

Result<double> number(const Value::Object& value, std::string_view name, double fallback) {
    const auto found = value.find(std::string(name));
    if (found == value.end()) return Result<double>::success(fallback);
    if (!found->second.isNumeric())
        return failure<double>(DiagnosticCode::InvalidArgument,
                               "weapon parameter must be numeric", "parameters." + std::string(name));
    return Result<double>::success(found->second.isInt64()
                                       ? static_cast<double>(found->second.asInt())
                                       : found->second.asDouble());
}

Result<int> integer(const Value::Object& value, std::string_view name, int fallback) {
    const auto found = value.find(std::string(name));
    if (found == value.end()) return Result<int>::success(fallback);
    if (!found->second.isInt64() || found->second.asInt() < std::numeric_limits<int>::min() ||
        found->second.asInt() > std::numeric_limits<int>::max())
        return failure<int>(DiagnosticCode::InvalidArgument,
                            "weapon parameter must be an integer", "parameters." + std::string(name));
    return Result<int>::success(static_cast<int>(found->second.asInt()));
}

}  // namespace

WeaponControl::WeaponControl(SubjectRef instance, SubjectRef wielder, WeaponDefinition definition,
                             resource::IResourceAccount& account,
                             transaction::ITransactionParticipant& effect)
    : instance_(instance),
      wielder_(wielder),
      definition_(std::move(definition)),
      account_(&account),
      effect_(&effect) {
    cap::addListener<IGameplayControlProvider>(this);
}

WeaponControl::~WeaponControl() { cap::removeListener<IGameplayControlProvider>(this); }

std::string_view WeaponControl::gameplayDomain() const noexcept { return "weapon"; }

bool WeaponControl::controls(const GameplaySession& session) const {
    return session.access != GameplayAccess::PlayerEquivalent ||
           std::find(session.controlledSubjects.begin(), session.controlledSubjects.end(), wielder_) !=
               session.controlledSubjects.end();
}

Result<GameplayObservation> WeaponControl::observeGameplay(const GameplaySession& session,
                                                            SubjectRef instance) const {
    if (instance != instance_ || !instance_.isValid())
        return failure<GameplayObservation>(DiagnosticCode::NotFound,
                                            "weapon gameplay instance was not found", "instance");
    if (!controls(session))
        return failure<GameplayObservation>(DiagnosticCode::PreconditionViolation,
                                            "session does not control the weapon wielder", "instance");
    if (!account_ || !effect_ || definition_.id.empty())
        return failure<GameplayObservation>(DiagnosticCode::StaleHandle,
                                            "weapon gameplay participants are invalid", "instance");
    auto cost = WeaponActionAdapter::resourceCost(definition_);
    if (!cost) return Result<GameplayObservation>::failure(cost.status());
    bool affordable = true;
    if (cost.value()) {
        auto answer = account_->canAfford(*cost.value());
        if (!answer) return Result<GameplayObservation>::failure(answer.status());
        affordable = answer.value().isAffordable();
    }
    GameplayObservation observation;
    observation.domain = id("gameplay:weapon");
    observation.instance = instance_;
    observation.tick = tick_;
    observation.revision = revision_;
    observation.state = Value(Value::Object{{"affordable", Value(affordable)},
                                             {"damage", Value(static_cast<double>(definition_.damage))},
                                             {"range", Value(static_cast<double>(definition_.range))},
                                             {"weaponId", Value(definition_.id)}});
    return Result<GameplayObservation>::success(std::move(observation));
}

Result<std::vector<GameplayActionDescriptor>> WeaponControl::availableGameplayActions(
    const GameplaySession& session, SubjectRef instance, SubjectRef subject) const {
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<std::vector<GameplayActionDescriptor>>::failure(observed.status());
    std::move(observed).takeValue();
    if (subject != wielder_)
        return failure<std::vector<GameplayActionDescriptor>>(
            DiagnosticCode::PreconditionViolation, "weapon action subject must be its wielder", "subject");
    const Value numberType(Value::Object{{"type", Value("number")}});
    const Value integerType(Value::Object{{"type", Value("integer")}});
    return Result<std::vector<GameplayActionDescriptor>>::success({
        {id("weapon:fire"),
         Value(Value::Object{{"pitch", numberType}, {"shooterId", integerType},
                             {"targetX", numberType}, {"targetY", numberType},
                             {"targetZ", numberType}, {"yaw", numberType}})},
    });
}

Result<GameplayCommandReceipt> WeaponControl::submitGameplay(const GameplaySession& session,
                                                              SubjectRef instance,
                                                              const GameplayCommand& command) {
    if (instance != instance_)
        return failure<GameplayCommandReceipt>(DiagnosticCode::NotFound,
                                               "weapon gameplay instance was not found", "instance");
    if (!controls(session) || command.subject != wielder_)
        return failure<GameplayCommandReceipt>(DiagnosticCode::PreconditionViolation,
                                               "session does not control the weapon wielder", "command.subject");
    if (command.id.empty())
        return failure<GameplayCommandReceipt>(DiagnosticCode::InvalidArgument,
                                               "command id must not be empty", "command.id");
    if (command.action != id("weapon:fire"))
        return failure<GameplayCommandReceipt>(DiagnosticCode::Unsupported,
                                               "unsupported weapon action", "command.action");
    if (command.observedTick != tick_ || command.expectedRevision != revision_)
        return failure<GameplayCommandReceipt>(DiagnosticCode::Conflict,
                                               "weapon command was based on a stale observation",
                                               "command.expectedRevision");
    auto values = object(command.parameters);
    if (!values) return Result<GameplayCommandReceipt>::failure(values.status());
    auto x = number(*values.value(), "targetX", 0.0);
    auto y = number(*values.value(), "targetY", 0.0);
    auto z = number(*values.value(), "targetZ", 0.0);
    auto yaw = number(*values.value(), "yaw", 0.0);
    auto pitch = number(*values.value(), "pitch", 0.0);
    auto shooter = integer(*values.value(), "shooterId", 0);
    if (!x) return Result<GameplayCommandReceipt>::failure(x.status());
    if (!y) return Result<GameplayCommandReceipt>::failure(y.status());
    if (!z) return Result<GameplayCommandReceipt>::failure(z.status());
    if (!yaw) return Result<GameplayCommandReceipt>::failure(yaw.status());
    if (!pitch) return Result<GameplayCommandReceipt>::failure(pitch.status());
    if (!shooter) return Result<GameplayCommandReceipt>::failure(shooter.status());
    AttackRequest attack;
    attack.targetX = static_cast<float>(x.value());
    attack.targetY = static_cast<float>(y.value());
    attack.targetZ = static_cast<float>(z.value());
    attack.hasTarget = values.value()->contains("targetX") || values.value()->contains("targetY") ||
                       values.value()->contains("targetZ");
    attack.yaw = static_cast<float>(yaw.value());
    attack.pitch = static_cast<float>(pitch.value());
    attack.shooterId = shooter.value();
    auto fired = WeaponActionAdapter::fire(definition_, attack, *account_, *effect_, tick_);
    if (!fired) return Result<GameplayCommandReceipt>::failure(fired.status());
    const auto transaction = std::move(fired).takeValue();
    ++revision_;
    GameplayEvent event;
    event.sequence = nextEventSequence_++;
    event.tick = tick_;
    event.type = "weapon.fired";
    event.subject = wielder_;
    event.causationCommandId = command.id;
    event.correlationId = command.id;
    event.payload = Value(Value::Object{{"transactionId", Value(transaction.transactionId)},
                                        {"weaponId", Value(definition_.id)}});
    events_.push_back(std::move(event));
    GameplayCommandReceipt receipt;
    receipt.commandId = command.id;
    receipt.executionId = transaction.transactionId;
    receipt.acceptedTick = tick_;
    receipt.resultingRevision = revision_;
    receipt.details = Value(Value::Object{{"weaponId", Value(definition_.id)}});
    return Result<GameplayCommandReceipt>::success(std::move(receipt),
                                                   Status::success(StatusCode::Applied));
}

Result<GameplayObservation> WeaponControl::advanceGameplay(const GameplaySession& session,
                                                            SubjectRef instance,
                                                            const SimulationStep& step) {
    if (step.tick <= tick_)
        return failure<GameplayObservation>(DiagnosticCode::Conflict,
                                            "weapon simulation tick must increase", "step.tick");
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<GameplayObservation>::failure(observed.status());
    std::move(observed).takeValue();
    tick_ = step.tick;
    return observeGameplay(session, instance);
}

Result<std::vector<GameplayEvent>> WeaponControl::gameplayEvents(const GameplaySession& session,
                                                                  SubjectRef instance,
                                                                  std::uint64_t afterSequence) const {
    auto observed = observeGameplay(session, instance);
    if (!observed) return Result<std::vector<GameplayEvent>>::failure(observed.status());
    std::move(observed).takeValue();
    std::vector<GameplayEvent> result;
    for (const auto& event : events_)
        if (event.sequence > afterSequence) result.push_back(event);
    const bool empty = result.empty();
    return Result<std::vector<GameplayEvent>>::success(
        std::move(result), Status::success(empty ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace eve::weapon
