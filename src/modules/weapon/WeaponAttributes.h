#pragma once

/**
 * @file WeaponAttributes.h
 * @brief Selective canonical mana/stamina adapter for weapons.
 */

#include "attributes/AttributeProjection.h"

#include <string>
#include <string_view>

namespace eve::weapon {

class WeaponEntity;

/**
 * @brief Adapts only Mana/Stamina trigger resources to AttributeProjection.
 *
 * Ammo, charges, reload state, cooldown, spread, recoil and attack phase
 * remain weapon-owned state. The legacy Resource fields are refreshed only as
 * a one-way projection after canonical reads/writes.
 */
class WeaponAttributeAdapter final {
public:
    static constexpr std::string_view manaAttribute       = "mana";
    static constexpr std::string_view maxManaAttribute    = "max_mana";
    static constexpr std::string_view staminaAttribute    = "stamina";
    static constexpr std::string_view maxStaminaAttribute = "max_stamina";

    /** @brief Bind and seed the selected resource attributes exactly once. */
    [[nodiscard]] static eve::Result<void> ensure(WeaponEntity& weapon);

    /** @brief Read a selected final mana/stamina attribute. */
    [[nodiscard]] static eve::Result<double> read(WeaponEntity& weapon, std::string_view attribute);

    /** @brief Set a selected canonical base and refresh Resource compatibility fields. */
    [[nodiscard]] static eve::Result<void> setBase(WeaponEntity& weapon, std::string_view attribute, double value);

    /** @brief Consume the current weapon's canonical Mana/Stamina trigger cost. */
    [[nodiscard]] static eve::Result<void> consumeTriggerResource(WeaponEntity& weapon);

    /** @brief Add a modifier using the shared operation/priority/sequence contract. */
    [[nodiscard]] static eve::Result<eve::attributes::ModifierId> addModifier(
        WeaponEntity& weapon, std::string id, std::string_view attribute, std::string source,
        eve::attributes::AttributeOperation operation, double value,
        eve::attributes::ModifierPriority priority = eve::attributes::priority::runtime);

    /** @brief Project canonical Mana/Stamina values into the legacy Resource component. */
    [[nodiscard]] static eve::Result<void> project(WeaponEntity& weapon);

    /** @brief Capture selected resource attributes with owner generation and revision. */
    [[nodiscard]] static eve::Result<eve::attributes::AttributeProjectionSnapshot> snapshot(WeaponEntity& weapon);

    /** @brief Restore selected resource attributes after owner/revision checks. */
    [[nodiscard]] static eve::Result<void> restore(WeaponEntity&                                       weapon,
                                                   const eve::attributes::AttributeProjectionSnapshot& snapshot,
                                                   eve::Revision expectedRevision);
};

}  // namespace eve::weapon
