#pragma once

/**
 * @file SettlementAdapter.h
 * @brief Vehicle health/armor policy adapter for the common settlement pipeline.
 *
 * Vehicle owns its health component and armor-zone definition.  This adapter
 * only stages a candidate write to that existing state; settlement itself
 * never stores a second health value.
 */

#include "settlement/Settlement.h"
#include "vehicle/VehicleTypes.h"

#include <optional>
#include <string>
#include <string_view>

namespace eve::vehicle {

/**
 * @brief Settles damage/healing against one VehicleEntity.
 *
 * The entity is borrowed for one synchronous pipeline call.  Zone, armor
 * penetration and deterministic incidence are read from request context:
 * `armor_zone` (string), `penetration` (0..1) and `incidence` (0..1).
 */
class VehicleSettlementAdapter final : public settlement::ISettlementPolicy {
public:
    /** @brief Bind an existing vehicle health component to a stable subject id. */
    VehicleSettlementAdapter(VehicleEntity& vehicle, SubjectRef targetRef);

    VehicleSettlementAdapter(const VehicleSettlementAdapter&)            = delete;
    VehicleSettlementAdapter& operator=(const VehicleSettlementAdapter&) = delete;

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

private:
    /**
     * @brief Finds a context value without transferring ownership.
     * @return Borrowed nullable value owned by the supplied context.
     * @ownership SettlementContext owns the value; this helper never releases it.
     * @lifetime Valid only until the context is changed or destroyed; callers must copy it for later use.
     * @thread Call on the settlement pipeline's owning simulation thread.
     * @reentrancy Does not invoke callbacks and must not be used across a re-entrant context mutation.
     */
    [[nodiscard]] static const eve::Value*          contextValue(const settlement::SettlementContext& context,
                                                                 std::string_view                     key) noexcept;
    [[nodiscard]] static std::optional<double>      numberValue(const eve::Value* value) noexcept;
    [[nodiscard]] static std::optional<bool>        boolValue(const eve::Value* value) noexcept;
    [[nodiscard]] static std::optional<std::string> stringValue(const eve::Value* value);
    [[nodiscard]] bool isDamage(const settlement::SettlementContext& context) const noexcept;
    [[nodiscard]] bool isHealing(const settlement::SettlementContext& context) const noexcept;

    VehicleEntity& vehicle_;
    SubjectRef     targetRef_;
};

}  // namespace eve::vehicle
