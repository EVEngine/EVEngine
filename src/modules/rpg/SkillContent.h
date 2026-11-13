#pragma once
/** @file SkillContent.h
 * @brief RPG module-interface: immutable admitted skill source records, without a live registry or ECS publication.
 * @remarks No capability, event, script or tool endpoints; custom policy/operation contracts require joint content
 * admission.
 */
#include <memory>
#include <span>
#include <string_view>
#include "common/BorrowedRef.h"
#include "common/Export.h"
namespace eve::definitions {
struct Definition;
}
namespace eve::rpg {
struct SkillDefinition;
class SkillConditionBindings;
struct ImplementationRequirement;
struct ImplementationIdentity;
struct EffectDefinition;
/** @brief Owning canonical skill archive used to validate detached member/checkpoint references.
 * @ownership Owns source records and compiled skill/effect facts; shared const instances may serve an entire restore
 * batch.
 * @thread Owner-thread preparation; immutable queries invoke no callbacks or registry lookups.
 */
class EVENGINE_API_PLATFORM SkillContent {
public:
    /** @brief Release immutable facts. */
    ~SkillContent();
    /** @brief Transfer facts. */
    SkillContent(SkillContent&&) noexcept;
    /** @brief Release previous facts and transfer ownership. */
    SkillContent& operator=(SkillContent&&) noexcept;
    SkillContent(const SkillContent&)            = delete;
    SkillContent& operator=(const SkillContent&) = delete;
    /** @brief Admit exact rpg.skill/v1 records using the canonical typed skill codec; duplicate IDs fail.
     * @param records Synchronous borrow, copied completely; no latest-content fallback or registry mutation.
     * @return Owning immutable facts or failure without entities, capabilities or source changes.
     * @remarks This validates skills, not their external effect/condition-policy reference closure.
     * @cost Full record copying and canonical skill compilation per archived skill, amortized across a restore batch.
     * @throws std::bad_alloc Local allocation failed without publication.
     */
    [[nodiscard]] static Result<SkillContent> prepare(std::span<const definitions::Definition> records);
    /** @brief Admit skill records and exact effect facts, requiring every directly granted effect to be present.
     * @ownership Copies/compiles all borrowed input; no live registry access or fallback.
     * @return Owning immutable content or duplicate/invalid/missing-reference failure without publication.
     * @param identities Optional owning-copy source of saved implementation revisions, never current bindings.
     * @remarks Validates supplied identity keys against derived requirements; missing identities remain unresolved.
     * Restore must check implementationClosure before materialization; an empty list does not certify custom code.
     * @cost Full skill/effect admission plus direct reference validation per archived content batch.
     * @throws std::bad_alloc Local allocation failed without publication.
     */
    [[nodiscard]] static Result<SkillContent> prepareWithEffects(
        std::span<const definitions::Definition> records, std::span<const EffectDefinition> effects,
        std::span<const ImplementationIdentity> identities = {});
    /** @brief Borrow compiled skill facts until this content is moved or destroyed; NotFound for absent IDs. */
    [[nodiscard]] ResultRef<const SkillDefinition> definition(std::string_view id) const;
    /** @brief Borrow exact effect fields until move/destruction, or NotFound; no live lookup. */
    [[nodiscard]] ResultRef<const EffectDefinition> effect(std::string_view id) const;
    /** @brief Borrow sorted external implementation requirements until this content is moved or destroyed.
     * @remarks Derived from canonical skill conditions and effects, retaining all source uses and declarations.
     * No provider lookup or execution occurs. This is not proof that implementations exist or match saved code.
     */
    [[nodiscard]] std::span<const ImplementationRequirement> implementationRequirements() const noexcept;
    /** @brief Require exactly one saved nonempty revision per derived external implementation requirement.
     * @return Success or explicit missing/invalid identity failure; never queries live bindings or invokes callbacks.
     * @cost Linear comparison of sorted immutable requirement and identity records per restore candidate.
     */
    [[nodiscard]] Result<void> implementationClosure() const;
    /** @brief Check saved closure and current actual executable registration revisions before materialization.
     * @return Success or absence/unversioned/mismatch/unsupported binding failure, without publication or callbacks.
     * @param conditions Actual actor/call-site policy owner, borrowed only during this call; no callbacks are invoked.
     * @thread Simulation owner thread; bindings must remain stable through synchronous materialization.
     * @cost Linear owner revision queries and owning string copies per creation attempt; no code is executed.
     */
    [[nodiscard]] Result<void> validateImplementationBindings(const SkillConditionBindings* conditions = nullptr) const;
    /** @brief Whether the admitted archive contains this skill; no allocation or external lookup. */
    [[nodiscard]] bool contains(std::string_view id) const noexcept;
    /** @brief Borrow an exact source until this content is moved/destroyed; NotFound for an absent ID. */
    [[nodiscard]] ResultRef<const definitions::Definition> source(std::string_view id) const;
    /** @brief Borrow sorted immutable records until this content is moved or destroyed. */
    [[nodiscard]] std::span<const definitions::Definition> records() const noexcept;

private:
    struct Impl;
    explicit SkillContent(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/** @brief Scoped immutable gameplay content for one synchronous simulation/materialization operation.
 * @ownership Shares the admitted content for this scope; prior nested scopes must outlive inner scopes.
 * @thread Simulation owner thread only; scopes unwind in stack order. No callbacks or locks in scope changes.
 * @remarks SkillRegistry::find/captureDefinition and EffectRegistry::find resolve exclusively here when present;
 * missing names never fall back to a live registry. Mutators and definitionRegistry still address their explicit
 * live catalogue. This scope introduces no mutable registry and never publishes archived definitions globally.
 */
class EVENGINE_API_PLATFORM SkillContentScope {
public:
    /** @brief Activate owning immutable content, or explicitly select the ordinary live lookup context when empty. */
    explicit SkillContentScope(std::shared_ptr<const SkillContent> content);
    /** @brief Restore the previous lookup context before releasing the local content owner. */
    ~SkillContentScope();
    SkillContentScope(const SkillContentScope&)            = delete;
    SkillContentScope& operator=(const SkillContentScope&) = delete;
    /** @brief Borrow the currently pinned content only until the active scope unwinds; no allocation. */
    [[nodiscard]] static OptionalRef<const SkillContent> current() noexcept;

private:
    std::shared_ptr<const SkillContent> owner_;
    OptionalRef<const SkillContent>     previous_;
};
}  // namespace eve::rpg
