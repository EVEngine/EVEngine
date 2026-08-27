#include "weapon/WeaponAction.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace eve::weapon {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

eve::Result<eve::LogicalId> weaponActionId(const WeaponDefinition& weapon) {
    if (weapon.id.empty())
        return failure<eve::LogicalId>(eve::DiagnosticCode::InvalidArgument,
                                      "weapon definition id must not be empty", "weapon.id");
    const auto id = eve::LogicalId::fromParts("weapon", "attack." + weapon.id);
    if (!id)
        return failure<eve::LogicalId>(eve::DiagnosticCode::InvalidArgument,
                                      "weapon id cannot form a valid action logical id", "weapon.id");
    return eve::Result<eve::LogicalId>::success(*id);
}

eve::Result<eve::Duration> seconds(float value, const char* path) {
    auto duration = eve::Duration::fromSeconds(static_cast<double>(value));
    if (!duration) return eve::Result<eve::Duration>::failure(duration.status());
    if (duration.value().nanoseconds() < 0)
        return failure<eve::Duration>(eve::DiagnosticCode::InvalidArgument,
                                      "weapon action duration must be non-negative", path);
    return duration;
}

const char* resourceName(ResourceKind kind) {
    switch (kind) {
    case ResourceKind::Ammo: return "ammo";
    case ResourceKind::Mana: return "mana";
    case ResourceKind::Charges: return "charges";
    case ResourceKind::Stamina: return "stamina";
    case ResourceKind::None: return "";
    }
    return "";
}

eve::Result<std::optional<eve::resource::CostSpec>> weaponCost(const WeaponDefinition& weapon) {
    if (weapon.resource.infinite || weapon.resource.kind == ResourceKind::None)
        return eve::Result<std::optional<eve::resource::CostSpec>>::success(std::nullopt);

    double amount = static_cast<double>(weapon.resource.cost);
    // Ranged WeaponSystem consumes one magazine unit per trigger even when
    // the legacy template leaves resource.cost at its zero default.
    if (weapon.resource.kind == ResourceKind::Ammo && amount <= 0.0) amount = 1.0;
    if (!std::isfinite(amount) || amount <= 0.0 || std::floor(amount) != amount ||
        amount >= std::ldexp(1.0, 63))
        return failure<std::optional<eve::resource::CostSpec>>(
            eve::DiagnosticCode::InvalidArgument,
            "weapon resource cost must be a finite positive integer", "resource.cost");

    auto cost = eve::resource::CostSpec::single(resourceName(weapon.resource.kind),
                                                static_cast<std::int64_t>(amount));
    if (!cost) return eve::Result<std::optional<eve::resource::CostSpec>>::failure(cost.status());
    return eve::Result<std::optional<eve::resource::CostSpec>>::success(
        std::move(cost).takeValue());
}

void put(eve::Value::Object& object, const char* key, float value) {
    object.emplace(key, eve::Value(static_cast<double>(value)));
}

void put(eve::Value::Object& object, const char* key, int value) {
    object.emplace(key, eve::Value(value));
}

}  // namespace

eve::Result<std::optional<eve::resource::CostSpec>> WeaponActionAdapter::resourceCost(
    const WeaponDefinition& weapon) {
    return weaponCost(weapon);
}

namespace {

eve::Result<eve::transaction::TransactionReceipt> executeActionFire(
    const WeaponDefinition& weapon, const AttackRequest& attack,
    eve::resource::IResourceAccount& account,
    eve::transaction::ITransactionParticipant& effect, eve::SimulationTick tick,
    std::string transactionId) {
    auto definition = WeaponActionAdapter::makeDefinition(weapon, attack);
    if (!definition)
        return eve::Result<eve::transaction::TransactionReceipt>::failure(definition.status());
    auto request = WeaponActionAdapter::makeRequest(weapon, attack, std::nullopt, tick);
    if (!request)
        return eve::Result<eve::transaction::TransactionReceipt>::failure(request.status());

    auto actionDefinition = std::move(definition).takeValue();
    auto actionRequest = std::move(request).takeValue();
    if (transactionId.empty()) transactionId = "weapon.fire." + weapon.id;
    actionRequest.transactionId = std::move(transactionId);

    action::ActionServices services;
    services.transactionEffect = &effect;
    services.transactionAccount = &account;
    action::ActionRuntime runtime(services);
    auto submitted = runtime.submit(std::move(actionDefinition), std::move(actionRequest));
    if (!submitted)
        return eve::Result<eve::transaction::TransactionReceipt>::failure(submitted.status());
    const auto executionId = std::move(submitted).takeValue();
    const auto* submittedExecution = runtime.find(executionId);
    if (submittedExecution == nullptr)
        return eve::Result<eve::transaction::TransactionReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation,
            "weapon Action execution disappeared after submit", "weapon.fire"));

    auto total = submittedExecution->definition().timing.windup.tryAdd(
        submittedExecution->definition().timing.active);
    if (!total)
        return eve::Result<eve::transaction::TransactionReceipt>::failure(total.status());
    total = std::move(total).takeValue().tryAdd(
        submittedExecution->definition().timing.recover);
    if (!total)
        return eve::Result<eve::transaction::TransactionReceipt>::failure(total.status());

    auto advanced = runtime.advance(executionId, tick, std::move(total).takeValue());
    if (!advanced)
        return eve::Result<eve::transaction::TransactionReceipt>::failure(advanced.status());
    const auto* execution = runtime.find(executionId);
    if (execution == nullptr || execution->phase() != action::ActionPhase::Completed ||
        execution->transactionReceipt() == nullptr)
        return eve::Result<eve::transaction::TransactionReceipt>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation,
            "weapon Action execution completed without a transaction receipt", "weapon.fire"));
    return eve::Result<eve::transaction::TransactionReceipt>::success(
        *execution->transactionReceipt(), advanced.status());
}

}  // namespace

eve::Result<eve::transaction::TransactionReceipt> WeaponActionAdapter::fire(
    const WeaponDefinition& weapon, eve::resource::IResourceAccount& account,
    eve::transaction::ITransactionParticipant& effect, std::string transactionId) {
    return executeActionFire(weapon, AttackRequest{}, account, effect, eve::SimulationTick(1),
                             std::move(transactionId));
}

eve::Result<eve::transaction::TransactionReceipt> WeaponActionAdapter::fire(
    const WeaponDefinition& weapon, const AttackRequest& attack,
    eve::resource::IResourceAccount& account,
    eve::transaction::ITransactionParticipant& effect, eve::SimulationTick tick) {
    return executeActionFire(weapon, attack, account, effect, tick, {});
}

eve::Result<action::ActionDefinition> WeaponActionAdapter::makeDefinition(
    const WeaponDefinition& weapon, const AttackRequest& request) {
    auto id = weaponActionId(weapon);
    if (!id) return eve::Result<action::ActionDefinition>::failure(id.status());
    auto windup = seconds(weapon.stages.windupTime, "stages.windupTime");
    if (!windup) return eve::Result<action::ActionDefinition>::failure(windup.status());
    auto active = seconds(weapon.stages.activeTime, "stages.activeTime");
    if (!active) return eve::Result<action::ActionDefinition>::failure(active.status());
    auto recover = seconds(weapon.stages.recoverTime, "stages.recoverTime");
    if (!recover) return eve::Result<action::ActionDefinition>::failure(recover.status());
    auto cost = resourceCost(weapon);
    if (!cost) return eve::Result<action::ActionDefinition>::failure(cost.status());

    action::ActionDefinition definition;
    definition.id = std::move(id).takeValue();
    definition.timing.windup = std::move(windup).takeValue();
    definition.timing.active = std::move(active).takeValue();
    definition.timing.recover = std::move(recover).takeValue();
    definition.targetingMode = request.hasTarget ? action::TargetingMode::Explicit
                                                  : action::TargetingMode::None;
    definition.cost = std::move(cost).takeValue();
    // Weapon logic is an Active operation even if its effect ids are resolved
    // by the weapon logic/event sink rather than the effects registry.
    definition.activeExecutionRequired = true;
    definition.metadata.emplace("weaponId", eve::Value(weapon.id));
    definition.metadata.emplace("logic", eve::Value(weapon.logic));
    definition.metadata.emplace("kind", eve::Value(static_cast<int>(weapon.kind)));

    auto valid = definition.validate();
    if (!valid) return eve::Result<action::ActionDefinition>::failure(valid.status());
    return eve::Result<action::ActionDefinition>::success(std::move(definition));
}

eve::Result<action::ActionRequest> WeaponActionAdapter::makeRequest(
    const WeaponDefinition& weapon, const AttackRequest& attack,
    std::optional<ecs::EntityHandle> source, eve::SimulationTick requestedTick) {
    auto definition = makeDefinition(weapon, attack);
    if (!definition) return eve::Result<action::ActionRequest>::failure(definition.status());
    auto checkedDefinition = std::move(definition).takeValue();

    action::ActionRequest request;
    request.actionId = checkedDefinition.id;
    request.source = std::move(source);
    request.requestedTick = requestedTick;
    if (attack.hasTarget) request.targetEntities.push_back(attack.targetHandle);

    // The canonical request owns deterministic attack parameters. Weapon
    // fields remain in WeaponDefinition/WeaponEntity; this is only a
    // provider-neutral transport envelope for the Active executor.
    put(request.parameters, "targetX", attack.targetX);
    put(request.parameters, "targetY", attack.targetY);
    put(request.parameters, "targetZ", attack.targetZ);
    request.parameters.emplace("hasTarget", eve::Value(attack.hasTarget));
    put(request.parameters, "muzzleX", attack.muzzleX);
    put(request.parameters, "muzzleY", attack.muzzleY);
    put(request.parameters, "muzzleZ", attack.muzzleZ);
    put(request.parameters, "yaw", attack.yaw);
    put(request.parameters, "pitch", attack.pitch);
    put(request.parameters, "shooterId", attack.shooterId);
    put(request.parameters, "arcAngle", attack.arcAngle);
    put(request.parameters, "aoeRadius", attack.aoeRadius);
    put(request.parameters, "spread", attack.spread);
    put(request.parameters, "pelletIndex", attack.pelletIndex);
    put(request.parameters, "pelletCount", attack.pelletCount);

    auto valid = request.validate(checkedDefinition);
    if (!valid) return eve::Result<action::ActionRequest>::failure(valid.status());
    return eve::Result<action::ActionRequest>::success(std::move(request));
}

}  // namespace eve::weapon
