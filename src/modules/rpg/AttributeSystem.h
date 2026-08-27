#pragma once

/**
 * @file AttributeSystem.h
 * @brief RPG's thin compatibility facade over the attributes module.
 *
 * Attribute data is owned by RPGActor::Attributes, whose concrete value is
 * attributes::AttributeSet. All arithmetic and modifier lifecycle operations
 * are delegated to that set; this type owns no parallel attribute state.
 */

#include "common/Result.h"
#include "rpg/AttributeTypes.h"

#include <string>

namespace eve::rpg {

class RPGActor;

/**
 * @brief RPG-facing attribute operations backed by the canonical AttributeSet.
 *
 * The string overloads are retained for existing scripts and effects. New C++
 * code should use the Result overloads and AttributeOperation enum.
 */
class AttributeSystem {
public:
    /** @brief Process-wide custom policy registry used only for Custom modifiers. */
    static AttributeOpTable& customOps();

    /** @brief Set a base value; invalid actor or attribute is a compatibility no-op. */
    static void setBase(RPGActor* actor, const std::string& attribute, double value);
    /** @brief Read a base value, returning zero for a missing actor/attribute. */
    static double getBase(RPGActor* actor, const std::string& attribute);
    /** @brief Add a delta to a base value. */
    static void modifyBase(RPGActor* actor, const std::string& attribute, double delta);
    /** @brief Query whether an attribute exists on the actor. */
    static bool hasAttribute(RPGActor* actor, const std::string& attribute);

    /**
     * @brief Add one canonical modifier to an actor.
     * @return The generated or supplied modifier id, or structured rejection.
     */
    [[nodiscard]] static eve::Result<ModifierId> addModifier(RPGActor* actor,
                                                               AttributeModifier modifier);

    /**
     * @brief Compatibility string facade for existing RPG/effects callers.
     * @return Empty string when the request is rejected.
     */
    static std::string addModifier(RPGActor* actor, const std::string& attribute,
                                   const std::string& source, const std::string& operation,
                                   double value, int priority = 0);

    /** @brief Remove by canonical modifier id with a structured result. */
    [[nodiscard]] static eve::Result<void> removeModifier(RPGActor* actor,
                                                           const ModifierId& modifierId);
    /** @brief Compatibility boolean removal facade. */
    static bool removeModifier(RPGActor* actor, const std::string& attribute,
                               const std::string& modifierId);
    /** @brief Remove source modifiers with a structured count result. */
    [[nodiscard]] static eve::Result<int> removeModifiersBySource(
        RPGActor* actor, const std::string& attribute, const std::string& source);
    /** @brief Compatibility all-attribute source removal facade. */
    static int removeAllModifiersBySource(RPGActor* actor, const std::string& source);

    /** @brief Compute the final value through the canonical attributes engine. */
    static double getFinal(RPGActor* actor, const std::string& attribute);
    /** @brief Mark a canonical attribute value dirty for its next built-in read. */
    static void invalidate(RPGActor* actor, const std::string& attribute);
};

}  // namespace eve::rpg
