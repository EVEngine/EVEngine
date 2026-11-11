#pragma once

/** @file RTSArchetype.h
 * @brief rts module-interface: Definitions admission into owning recipes and typed Unit/Weapon composition.
 * @remarks Data face rts.unit-archetype v1 stores exact owning sources; no capability registration, new ECS model,
 * events or script/tool bindings. Recipe admission grants no target-world or external implementation authority.
 */

#include "common/Export.h"
#include "common/Identity.h"
#include "common/Result.h"
#include "common/Value.h"

#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace eve::definitions {
class DefinitionRegistry;
struct Definition;
}  // namespace eve::definitions
namespace eve::weapon {
class WeaponEntity;
}

namespace eve::rts {
class Unit;
class Building;

/** @brief Factory that creates one canonical weapon instance from a registry definition. */
using ArchetypeWeaponFactory =
    std::function<Result<weapon::WeaponEntity*>(std::string_view definitionId, PersistentId instanceId)>;

/** @brief Prepare/publish a weapon from the recipe-owned immutable source, never the registry's latest record.
 * @ownership Source is borrowed only during the callback. ECS/domain owns the returned entity;
 * its borrow must remain valid through application. Factory retains rollback responsibility until the owning
 * transaction commits/cancels. No callback or entity pointer is retained by the recipe.
 * @thread Owner thread outside Views; must not reenter or destroy the target unit. */
using CapturedArchetypeWeaponFactory =
    std::function<Result<weapon::WeaponEntity*>(const definitions::Definition&, PersistentId)>;
/**
 * @brief Immutable unit fields plus exact owning unit/weapon source revisions; preparation creates no ECS rows.
 * @ownership Owns shared const recipe storage; never retains the registry or entity pointers.
 * @thread Owner-thread preparation/application; independent copies may be retained across asset replacement.
 */
class EVENGINE_API_DOMAINS PreparedUnitArchetype {
public:
    /** @brief Release one owning recipe revision. */
    ~PreparedUnitArchetype();
    /** @brief Share an immutable revision without reparsing content. */
    PreparedUnitArchetype(const PreparedUnitArchetype&) noexcept;
    /** @brief Replace the owning immutable revision. */
    PreparedUnitArchetype& operator=(const PreparedUnitArchetype&) noexcept;
    /** @brief Transfer the owning revision; the source becomes empty. */
    PreparedUnitArchetype(PreparedUnitArchetype&&) noexcept;
    /** @brief Transfer the revision; the source becomes empty. */
    PreparedUnitArchetype& operator=(PreparedUnitArchetype&&) noexcept;
    /** @brief Parse canonical unit fields and capture exact unit/weapon source revisions without entities.
     * @param registry Borrowed only for this synchronous call; not retained by the result.
     * @param definition Unit logical identity, in the unit namespace.
     * @return Owning immutable recipe or validation/reference failure.
     * @cost Linear definition parsing and owning source copies, amortized per admitted recipe revision. */
    [[nodiscard]] static Result<PreparedUnitArchetype> prepare(definitions::DefinitionRegistry& registry,
                                                               LogicalId                        definition);
    /** @brief Encode exact unit/weapon source revisions as owning rts.unit-archetype v1 values.
     * @ownership Result owns source JSON and metadata; no registry, entity or callback is retained.
     * @thread Read-only detached access; caller synchronizes this handle against replacement/destruction.
     * @return Complete value or moved recipe failure; no world is accessed.
     * @remarks Epoch, save revision/tick and graph membership belong to enclosing participants.
     * @cost Copies the two source records once per saved recipe, independent of live world size.
     * @throws std::bad_alloc Recipe and all live state remain unchanged.
     */
    [[nodiscard]] Result<Value> encodeState() const;
    /** @brief Admit exact saved sources through the same canonical kernel as registry preparation.
     * @param state Synchronous borrowed rts.unit-archetype v1 value; result owns all admitted facts.
     * @return Recipe or invalid schema/fields/version/generation, unit data or weapon reference/content.
     * @remarks Strict outer fields and source tuples; initial v1 has no implicit migration. Nested source JSON
     * follows the existing unit/weapon v1 readers' unknown-field policy and is retained verbatim.
     * Does not consult a registry, allocate entities, invoke factories or authorize named external logic.
     * @thread Pure owning content preparation; no capabilities, simulation time/RNG or callbacks.
     * @cost Parses and validates exact unit/weapon source revisions once per loaded recipe.
     * @throws std::bad_alloc Input and all live worlds remain unchanged.
     */
    [[nodiscard]] static Result<PreparedUnitArchetype> prepareState(const Value& state);
    /** @brief Borrow the captured unit source; empty for a moved-from recipe.
     * @ownership Borrowed until this handle is reassigned/destroyed; copy the record to retain it. */
    [[nodiscard]] std::optional<std::reference_wrapper<const definitions::Definition>> source() const noexcept;
    /** @brief Borrow the exact captured weapon source, or empty when the recipe has no weapon/is moved-from.
     * @ownership Borrowed until this handle is reassigned/destroyed; never a registry view. */
    [[nodiscard]] std::optional<std::reference_wrapper<const definitions::Definition>> weaponSource() const noexcept;
    /** @brief Materialize this recipe into a matching unpublished Unit inside its owning transaction.
     * @param unit Borrowed only during this call; owning transaction must roll it back on failure/exception.
     * @param weaponFactory Synchronous callback using the captured source; must not reenter/destroy this unit.
     * @return Structured admission/application failure or Applied. Exceptions propagate to the owning transaction.
     * @remarks No registry lookups; callbacks are not retained and no locks/Views are held here.
     * @cost Component initialization and weapon publication per new unit; parsing belongs to prepare. */
    [[nodiscard]] Result<void> apply(Unit& unit, const CapturedArchetypeWeaponFactory& weaponFactory) const;

private:
    /** @brief Canonical shared source admission for registry and archive callers.
     * @ownership Sources are synchronous borrows copied into the result, never retained as pointers.
     * @lifetime Input records outlive this call only; no world or callback is used.
     */
    [[nodiscard]] static Result<PreparedUnitArchetype> prepareSources(const definitions::Definition& source,
                                                                      const definitions::Definition* weapon);
    struct Impl;
    explicit PreparedUnitArchetype(std::shared_ptr<const Impl> impl);
    std::shared_ptr<const Impl> impl_;
};

/**
 * @brief Canonical building materialization; units use PreparedUnitArchetype.
 *
 * DefinitionRegistry remains the archetype authority. This adapter copies only
 * per-instance runtime state and delegates weapon creation to the canonical
 * weapon runtime supplied by the caller.
 */
class RTSArchetypeMaterializer {
public:
    /** @brief Apply the Building root's logical definition atomically. */
    [[nodiscard]] static Result<void> apply(definitions::DefinitionRegistry& registry, Building& building,
                                             const ArchetypeWeaponFactory& weaponFactory = {});
};

}  // namespace eve::rts
