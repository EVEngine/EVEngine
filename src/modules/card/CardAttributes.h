#pragma once

/**
 * @file CardAttributes.h
 * @brief Selective canonical attributes for card combat values.
 */

#include "attributes/AttributeProjection.h"

#include <string_view>

namespace eve::card {

class CardData;

/**
 * @brief Adapts CardData's attack/health values to the canonical AttributeSet.
 *
 * The attributes component owns attack and health after first binding. The
 * legacy integer fields are refreshed as a one-way compatibility projection.
 * Card cost remains a resource/payment value, while layout, phase and card
 * membership remain Card-owned state.
 */
class CardAttributeAdapter final {
public:
    static constexpr std::string_view attackAttribute = "attack";
    static constexpr std::string_view healthAttribute = "health";

    /** @brief Bind and seed the card's selected attributes exactly once. */
    [[nodiscard]] static eve::Result<void> ensure(CardData& card);

    /** @brief Read a selected final card attribute and refresh compatibility fields. */
    [[nodiscard]] static eve::Result<double> read(CardData& card, std::string_view attribute);

    /** @brief Set a selected canonical base and refresh the legacy projection. */
    [[nodiscard]] static eve::Result<void> setBase(CardData& card, std::string_view attribute, double value);

    /** @brief Add a modifier using the shared attribute operation ordering. */
    [[nodiscard]] static eve::Result<eve::attributes::ModifierId> addModifier(
        CardData& card, std::string id, std::string_view attribute, std::string source,
        eve::attributes::AttributeOperation operation, double value,
        eve::attributes::ModifierPriority priority = eve::attributes::priority::runtime);

    /** @brief Project canonical values into the legacy integer Stats fields. */
    [[nodiscard]] static eve::Result<void> project(CardData& card);

    /** @brief Capture attack/health with the owning ECS generation and revision. */
    [[nodiscard]] static eve::Result<eve::attributes::AttributeProjectionSnapshot> snapshot(CardData& card);

    /** @brief Restore a card snapshot only when owner and revision still match. */
    [[nodiscard]] static eve::Result<void> restore(CardData&                                           card,
                                                   const eve::attributes::AttributeProjectionSnapshot& snapshot,
                                                   eve::Revision expectedRevision);
};

}  // namespace eve::card
