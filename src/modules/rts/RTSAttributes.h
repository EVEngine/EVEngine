#pragma once

/**
 * @file RTSAttributes.h
 * @brief Selective combat-stat adapter for RTS units.
 */

#include "attributes/AttributeProjection.h"

#include <string>
#include <string_view>

namespace eve::rts {

class Unit;

/**
 * @brief Defines the small set of RTS unit stats eligible for attributes.
 *
 * Attack, health, max_health and armor are optional combat facts. Unit
 * position, speed, arrival radius, formation coordinates and order state stay
 * in their dedicated components and are never copied into AttributeSet.
 */
class RTSUnitAttributeAdapter final {
public:
    static constexpr std::string_view attackAttribute    = "attack";
    static constexpr std::string_view healthAttribute    = "health";
    static constexpr std::string_view maxHealthAttribute = "max_health";
    static constexpr std::string_view armorAttribute     = "armor";

    /** @brief Bind and seed default optional combat stats exactly once. */
    [[nodiscard]] static eve::Result<void> ensure(Unit& unit);

    /** @brief Read one selected combat stat. */
    [[nodiscard]] static eve::Result<double> read(Unit& unit, std::string_view attribute);

    /** @brief Set one selected canonical base stat. */
    [[nodiscard]] static eve::Result<void> setBase(Unit& unit, std::string_view attribute, double value);

    /** @brief Add a modifier using the shared operation/priority/sequence contract. */
    [[nodiscard]] static eve::Result<eve::attributes::ModifierId> addModifier(
        Unit& unit, std::string id, std::string_view attribute, std::string source,
        eve::attributes::AttributeOperation operation, double value,
        eve::attributes::ModifierPriority priority = eve::attributes::priority::runtime);

    /** @brief Capture selected unit combat stats with owner generation and revision. */
    [[nodiscard]] static eve::Result<eve::attributes::AttributeProjectionSnapshot> snapshot(Unit& unit);

    /** @brief Restore selected unit combat stats after owner/revision checks. */
    [[nodiscard]] static eve::Result<void> restore(Unit&                                               unit,
                                                   const eve::attributes::AttributeProjectionSnapshot& snapshot,
                                                   eve::Revision expectedRevision);
};

}  // namespace eve::rts
