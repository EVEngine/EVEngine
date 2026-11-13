#pragma once
/** @file RPGUnitCatalogue.h
 * @brief rpg_spawn module-interface: strict live and archived unit recipe admission.
 * @remarks Consumes canonical Definition records and admitted RPG skill content; returns owning CPU data.
 * No capability registration, ECS entity/Link creation, events, script bindings or tool endpoints are added.
 * Archived input retains the existing eve.rpg.unit/v1 format; runtime generation metadata is provenance only.
 */

#include "common/Export.h"
#include "common/definitions/DefinitionRuntime.h"

#include "spawn/SpawnDefinition.h"

#include <memory>

namespace eve::definitions {
class DefinitionRegistry;
struct Definition;
}  // namespace eve::definitions
namespace eve::rpg {
struct EncounterMemberDefinition;
class SkillContent;
}  // namespace eve::rpg
namespace eve::rpg_spawn {
/** @brief Owning immutable member recipe pinned to the canonical definition generation. */
struct UnitRecipe {
    std::shared_ptr<const rpg::EncounterMemberDefinition> member;
    definition::DefinitionHandle                          definition;
    /** @brief Canonical immutable source retained for pinned-content save/restore. */
    std::string canonicalJson;
};
/**
 * @brief Strict RPG unit codec over the existing shared Definitions registry.
 * @ownership Registry is genuinely shared with the content host; no second mutable recipe catalogue.
 * @thread Content/simulation owner thread; no script or user callbacks.
 */
class EVENGINE_API_PLATFORM RPGUnitCatalogue final : public spawn::IUnitDefinitionResolver {
public:
    /** @brief Bind the canonical content registry; a null registry makes operations fail explicitly. */
    explicit RPGUnitCatalogue(std::shared_ptr<definitions::DefinitionRegistry> registry);
    /**
     * @brief Validate eve.rpg.unit/v1 and atomically publish its canonical source.
     * @return Success or failure preserving the previous definition.
     * @cost Linear in source fields plus skill lookup, once per asset publication.
     */
    [[nodiscard]] Result<void> publish(const Value& source);
    /**
     * @brief Resolve a generation-qualified owning unit recipe without creating an actor.
     * @return Owning recipe or structured schema/reference failure.
     * @cost Linear in recipe data, amortized once per prepared spawn batch member.
     */
    [[nodiscard]] Result<UnitRecipe> resolve(const DefinitionRef& reference) const;
    /** @brief Compile an exact archived eve.rpg.unit/v1 record without accessing or publishing a unit registry.
     * @param source Borrowed immutable archive record; type/key/version/generation and JSON must agree.
     * @return Owning recipe with archived provenance, not proof that its handle is current in any live registry.
     * @ownership Retains copied canonical source and compiled member only; no registry or record references.
     * @thread Content owner thread; the joint save's RPG skill content must already be admitted and stable.
     * No callbacks or entity/capability publication. Missing skills fail rather than substituting another skill.
     * @cost Full strict parse, canonical encoding and recipe allocation per archived revision; share the result.
     */
    [[nodiscard]] static Result<UnitRecipe> prepareCaptured(const definitions::Definition& source);
    /** @brief Admit an exact unit revision against archived skill facts, without accessing live registries.
     * @ownership Borrows source/content synchronously and returns owning immutable recipe facts.
     * @thread Content owner thread, no callbacks or publication; other metadata/unknown-field rules match
     * prepareCaptured.
     * @return Canonical recipe or schema/reference failure, with no current-content fallback.
     * @cost Full unit parse and canonical member validation per archived revision; share admitted results.
     */
    [[nodiscard]] static Result<UnitRecipe> prepareCaptured(const definitions::Definition& source,
                                                            const rpg::SkillContent&       skills);
    /** @brief Validate the referenced unit without retaining a registry borrow.
     * @cost Linear recipe parsing and skill lookup, paid at content/map admission. */
    [[nodiscard]] Result<void> validate(const DefinitionRef& reference) const override;

private:
    [[nodiscard]] static Result<UnitRecipe>          prepareWithContent(const definitions::Definition& source,
                                                                        const rpg::SkillContent*       skills);
    std::shared_ptr<definitions::DefinitionRegistry> registry_;
};
}  // namespace eve::rpg_spawn
