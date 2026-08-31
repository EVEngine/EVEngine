#pragma once

/** @file RTSArchetype.h @brief Canonical definition-to-RTS-component materialization. */

#include "definitions/Definitions.h"
#include "rts/RTSTypes.h"

#include <functional>

namespace eve::weapon {
class WeaponEntity;
}

namespace eve::rts {

/** @brief Factory that creates one canonical weapon instance from a registry definition. */
using ArchetypeWeaponFactory =
    std::function<Result<weapon::WeaponEntity*>(std::string_view definitionId, PersistentId instanceId)>;

/**
 * @brief Materializes canonical unit/building definitions into RTS composition components.
 *
 * DefinitionRegistry remains the archetype authority. This adapter copies only
 * per-instance runtime state and delegates weapon creation to the canonical
 * weapon runtime supplied by the caller.
 */
class RTSArchetypeMaterializer {
public:
    /** @brief Apply the Unit root's logical definition atomically. */
    [[nodiscard]] static Result<void> apply(definitions::DefinitionRegistry& registry, Unit& unit,
                                             const ArchetypeWeaponFactory& weaponFactory = {});
    /** @brief Apply the Building root's logical definition atomically. */
    [[nodiscard]] static Result<void> apply(definitions::DefinitionRegistry& registry, Building& building,
                                             const ArchetypeWeaponFactory& weaponFactory = {});
};

}  // namespace eve::rts
