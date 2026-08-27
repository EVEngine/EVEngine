#pragma once

#include "common/Time.h"

/**
 * @file WeaponAction.h
 * @brief Weapon Attack to renderer-independent Action adapter.
 */

#include "action/Action.h"
#include "transaction/AtomicResourcePayment.h"
#include "weapon/WeaponTypes.h"

#include <optional>
#include <string>

namespace eve::weapon {

/**
 * @brief Translates a WeaponDefinition/AttackRequest pair to Action values.
 *
 * The adapter never moves or mirrors magazine, spread, recoil, muzzle or
 * projectile fields into the common action definition. Those remain owned by
 * WeaponEntity/WeaponDefinition and are interpreted by an injected Active
 * executor. ActionRuntime remains the only phase/timer owner.
 */
class WeaponActionAdapter {
public:
    /**
     * @brief Convert the weapon trigger resource into a canonical cost.
     * @param weapon Borrowed weapon definition observed during this call.
     * @return Empty for an infinite or resource-free weapon, otherwise a
     *         validated cost using the canonical resource id (`ammo`, `mana`,
     *         `charges` or `stamina`).
     */
    [[nodiscard]] static eve::Result<std::optional<eve::resource::CostSpec>> resourceCost(
        const WeaponDefinition& weapon);

    /**
     * @brief Execute one already-built weapon effect and charge its resource atomically.
     * @param weapon Borrowed definition supplying the trigger cost.
     * @param account Borrowed authoritative account for ammo/mana/charges/stamina.
     * @param effect Borrowed transaction participant for the weapon effect.
     * @param transactionId Non-empty correlation id; empty derives from weapon id.
     * @return A committed transaction receipt or structured lifecycle failure.
     * @remarks ActionRuntime remains the lifecycle owner for the regular action
     *         pipeline; this entry point is the synchronous effect boundary.
     */
    [[nodiscard]] static eve::Result<eve::transaction::TransactionReceipt> fire(
        const WeaponDefinition& weapon, eve::resource::IResourceAccount& account,
        eve::transaction::ITransactionParticipant& effect, std::string transactionId = {});

    /**
     * @brief Execute one weapon attack through the common Action lifecycle.
     * @param weapon Borrowed weapon definition.
     * @param attack Borrowed attack parameters copied into the ActionRequest.
     * @param account Authoritative ammo/mana/charge account.
     * @param effect Domain participant for the actual projectile/hit operation.
     * @param tick Deterministic simulation tick at which the attack is applied.
     * @return The Action-backed transaction receipt. Condition, action,
     *         payment and effect failures leave the account and effect unchanged.
     * @remarks This is the preferred fire entry point. Callers do not need to
     *          call submit and then assemble a separate payment transaction.
     */
    [[nodiscard]] static eve::Result<eve::transaction::TransactionReceipt> fire(
        const WeaponDefinition& weapon, const AttackRequest& attack, eve::resource::IResourceAccount& account,
        eve::transaction::ITransactionParticipant& effect, eve::SimulationTick tick = eve::SimulationTick(1));

    /**
     * @brief Build an action definition for one attack request.
     * @param weapon Borrowed weapon definition observed during this call only.
     * @param request Borrowed attack request; target selection affects the mode.
     * @return Common definition or a structured mapping failure.
     */
    [[nodiscard]] static eve::Result<action::ActionDefinition> makeDefinition(const WeaponDefinition& weapon,
                                                                              const AttackRequest&    request);

    /**
     * @brief Build a request carrying the original weapon attack parameters.
     * @param weapon Borrowed definition used only to select target mode.
     * @param attack Borrowed request copied into owned ActionRequest parameters.
     * @param source Optional source ECS handle; no raw pointer is retained.
     * @param requestedTick Deterministic simulation tick of the request.
     * @return An owning action request or a structured mapping failure.
     */
    [[nodiscard]] static eve::Result<action::ActionRequest> makeRequest(
        const WeaponDefinition& weapon, const AttackRequest& attack,
        std::optional<ecs::EntityHandle> source        = std::nullopt,
        eve::SimulationTick              requestedTick = eve::SimulationTick::zero());
};

}  // namespace eve::weapon
