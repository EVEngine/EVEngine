#pragma once

/**
 * @file SettlementAdapter.h
 * @brief RPG AttributeSet policy adapter for the common settlement pipeline.
 *
 * This adapter is deliberately independent from StatusSystem.  Statuses may
 * produce a SettlementRequest for periodic damage or healing, but health,
 * resistances, armor, shield and source attributes remain in AttributeSet.
 */

#include "rpg/RPGActor.h"
#include "rpg/StatusTypes.h"
#include "settlement/Settlement.h"

#include <optional>
#include <string>
#include <string_view>

#include "common/Export.h"

namespace eve::rpg {

/**
 * @brief Project one configured RPG status tick into the canonical settlement request.
 * @param tick Owning lifecycle event produced by StatusSystem.
 * @param source Stable source identity supplied by the owning game; it may be nil when unknown.
 * @param target Stable target identity resolving to tick.actor.
 * @param simulationTick Deterministic scheduler tick for replay and event ordering.
 * @return Request decoded from the effect definition's strict `settlement.tick` JSON object.
 * @remarks The object requires `kind`, `resource`, and non-negative `magnitude`; optional `tags`
 * and `context` are copied. Magnitude is multiplied by the captured status stack count.
 * @thread Call on the RPG registry thread while the referenced definition remains registered.
 * @reentrancy Does not invoke callbacks or mutate StatusSystem, the actor, or the registry.
 * @cost Linear in JSON and tag/context size; call once per emitted tick, outside Settlement's hot stages.
 */
[[nodiscard]] EVENGINE_API_PLATFORM eve::Result<settlement::SettlementRequest> makeStatusTickSettlementRequest(
    const StatusTickEvent& tick, SubjectRef source, SubjectRef target, SimulationTick simulationTick);

/**
 * @brief Settles damage/healing against one RPG actor's AttributeSet.
 *
 * The target actor and optional source actor are borrowed for the synchronous
 * call.  `targetRef` and `sourceRef` are caller-owned stable identities; they
 * are checked against the request and are never resolved by the pipeline.
 */
class EVENGINE_API_PLATFORM RPGSettlementAdapter final : public settlement::ISettlementPolicy {
public:
    /** @brief Attribute names used by the RPG policy. */
    struct Config {
        /** @brief Vitals resource holding authoritative current health. */
        std::string healthAttribute = "hp";
        /** @brief Final attribute supplying the resource maximum. */
        std::string maxHealthAttribute          = "hp";
        std::string shieldAttribute             = "shield";
        std::string armorAttribute              = "armor";
        std::string resistancePrefix            = "resistance.";
        std::string criticalChanceAttribute     = "critical_chance";
        std::string criticalMultiplierAttribute = "critical_multiplier";
        std::string sourceMultiplierAttribute   = "damage_multiplier";
        /** @brief Emit death/kill transitions when this resource crosses zero. */
        bool emitLifeTransitions = true;
    };

    /** @brief Bind a target actor with default RPG attribute names. */
    RPGSettlementAdapter(RPGActor& target, SubjectRef targetRef);
    RPGSettlementAdapter(RPGActor& target, SubjectRef targetRef, Config config);

    /** @brief Bind target and optional source actors with explicit identities. */
    RPGSettlementAdapter(RPGActor& target, SubjectRef targetRef, RPGActor* source, SubjectRef sourceRef);
    RPGSettlementAdapter(RPGActor& target, SubjectRef targetRef, RPGActor* source, SubjectRef sourceRef, Config config);

    RPGSettlementAdapter(const RPGSettlementAdapter&)            = delete;
    RPGSettlementAdapter& operator=(const RPGSettlementAdapter&) = delete;

    /** @brief Stable identity checked for the target side of the request. */
    [[nodiscard]] const SubjectRef& targetRef() const noexcept { return targetRef_; }

    /** @copydoc settlement::ISettlementPolicy::validate */
    [[nodiscard]] eve::Result<void> validate(settlement::SettlementContext& context) override;
    /** @copydoc settlement::ISettlementPolicy::sourceModifiers */
    [[nodiscard]] eve::Result<void> sourceModifiers(settlement::SettlementContext& context) override;
    /** @copydoc settlement::ISettlementPolicy::targetMitigation */
    [[nodiscard]] eve::Result<void> targetMitigation(settlement::SettlementContext& context) override;
    /** @copydoc settlement::ISettlementPolicy::armorShield */
    [[nodiscard]] eve::Result<void> armorShield(settlement::SettlementContext& context) override;
    /** @copydoc settlement::ISettlementPolicy::clamp */
    [[nodiscard]] eve::Result<void> clamp(settlement::SettlementContext& context) override;
    /** @copydoc settlement::ISettlementPolicy::prepareApply */
    [[nodiscard]] eve::Result<settlement::PreparedApply> prepareApply(
        const settlement::SettlementContext& context) override;
    /** @copydoc settlement::ISettlementPolicy::prepareTrigger */
    [[nodiscard]] eve::Result<std::vector<settlement::SettlementRequest>> prepareTrigger(
        const settlement::SettlementContext& context, const settlement::SettlementResult& result) override;

private:
    [[nodiscard]] bool isDamage(const settlement::SettlementContext& context) const noexcept;
    [[nodiscard]] bool isHealing(const settlement::SettlementContext& context) const noexcept;
    /**
     * @brief Finds one settlement context value without transferring ownership.
     * @return Borrowed nullable value owned by the supplied context.
     * @ownership SettlementContext owns the value; this helper never releases it.
     * @lifetime Valid only until the context is changed or destroyed; copy it before retaining.
     * @thread Call on the settlement pipeline's owning simulation thread.
     * @reentrancy Does not invoke callbacks and is invalid across re-entrant context mutation.
     */
    [[nodiscard]] static const eve::Value*          contextValue(const settlement::SettlementContext& context,
                                                                 std::string_view                     key) noexcept;
    [[nodiscard]] static std::optional<double>      numberValue(const eve::Value* value) noexcept;
    [[nodiscard]] static std::optional<bool>        boolValue(const eve::Value* value) noexcept;
    [[nodiscard]] static std::optional<std::string> stringValue(const eve::Value* value);
    [[nodiscard]] eve::Result<double>               readBase(std::string_view name) const;
    [[nodiscard]] eve::Result<double>               readFinal(std::string_view name, double fallback = 0.0) const;
    [[nodiscard]] eve::Result<double>               readSourceFinal(std::string_view name, double fallback = 0.0) const;

    RPGActor&  target_;
    SubjectRef targetRef_;
    RPGActor*  source_ = nullptr;
    SubjectRef sourceRef_;
    Config     config_;
};

}  // namespace eve::rpg
