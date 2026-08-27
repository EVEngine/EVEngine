#pragma once

/**
 * @file VehicleAttributes.h
 * @brief Selective canonical health and armor adapter for vehicles.
 */

#include "attributes/AttributeProjection.h"

#include <string>
#include <string_view>

namespace eve::vehicle {

class VehicleEntity;

/**
 * @brief Adapts VehicleEntity health and armor to the common attribute core.
 *
 * `VehicleEntity::Health` remains a one-way compatibility projection for
 * health/max_health. Armor-zone geometry and multipliers remain vehicle
 * policy data; position, speed and physics bodies are deliberately excluded.
 */
class VehicleAttributeAdapter final {
public:
    static constexpr std::string_view healthAttribute    = "health";
    static constexpr std::string_view maxHealthAttribute = "max_health";
    static constexpr std::string_view armorAttribute     = "armor";

    /** @brief Bind and seed selected combat attributes exactly once. */
    [[nodiscard]] static eve::Result<void> ensure(VehicleEntity& vehicle);

    /** @brief Read a selected final combat attribute and refresh health projection. */
    [[nodiscard]] static eve::Result<double> read(VehicleEntity& vehicle, std::string_view attribute);

    /** @brief Set a selected canonical base and refresh the compatibility health fields. */
    [[nodiscard]] static eve::Result<void> setBase(VehicleEntity& vehicle, std::string_view attribute, double value);

    /** @brief Add a modifier using the shared operation/priority/sequence contract. */
    [[nodiscard]] static eve::Result<eve::attributes::ModifierId> addModifier(
        VehicleEntity& vehicle, std::string id, std::string_view attribute, std::string source,
        eve::attributes::AttributeOperation operation, double value,
        eve::attributes::ModifierPriority priority = eve::attributes::priority::runtime);

    /** @brief Project canonical health values into VehicleEntity::Health. */
    [[nodiscard]] static eve::Result<void> project(VehicleEntity& vehicle);

    /** @brief Capture health/max_health/armor with owner generation and revision. */
    [[nodiscard]] static eve::Result<eve::attributes::AttributeProjectionSnapshot> snapshot(VehicleEntity& vehicle);

    /** @brief Restore a vehicle attribute snapshot after owner/revision checks. */
    [[nodiscard]] static eve::Result<void> restore(VehicleEntity&                                      vehicle,
                                                   const eve::attributes::AttributeProjectionSnapshot& snapshot,
                                                   eve::Revision expectedRevision);
};

}  // namespace eve::vehicle
