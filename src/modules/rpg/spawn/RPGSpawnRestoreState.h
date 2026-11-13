#pragma once
/** @file RPGSpawnRestoreState.h
 * @brief rpg_spawn module-interface: canonical detached member/checkpoint admission before joint restoration.
 * @remarks No ECS entities, source leases, capabilities, callbacks, events, script or tool publication.
 */
#include <memory>
#include <span>
#include "common/Export.h"
#include "common/Identity.h"
#include "common/ResourceRef.h"
#include "common/Result.h"
#include "common/Revision.h"
#include "common/SubjectRef.h"
#include "common/Time.h"
namespace eve::spawn {
struct SavedSpawnMember;
struct SavedLifecycleInput;
}  // namespace eve::spawn
namespace eve::rpg {
struct ImplementationRequirement;
class SkillConditionBindings;
class PreparedMemberActor;
class SkillContent;
}  // namespace eve::rpg
namespace eve::rpg_spawn {
class RPGSpawnArchive;
struct UnitRecipe;
class RPGSpawnJointRestore;
/** @brief Owning admitted actor facts and pinned unit/skill content, not a running domain world.
 * @ownership Independent of archive, Table and live registries; cancellation releases only local CPU data.
 * @thread Simulation/content owner thread; preparation never invokes callbacks or mutates global content.
 */
class EVENGINE_API_PLATFORM RPGSpawnRestoreState {
public:
    /** @brief Cancel unpublished local facts. */
    ~RPGSpawnRestoreState();
    /** @brief Transfer facts. */
    RPGSpawnRestoreState(RPGSpawnRestoreState&&) noexcept;
    /** @brief Cancel old facts and transfer ownership. */
    RPGSpawnRestoreState& operator=(RPGSpawnRestoreState&&) noexcept;
    RPGSpawnRestoreState(const RPGSpawnRestoreState&)            = delete;
    RPGSpawnRestoreState& operator=(const RPGSpawnRestoreState&) = delete;
    /** @brief Admit every exact unit recipe and actor checkpoint against archived skill/effect facts.
     * @param archive Synchronous structural-archive borrow; records and metadata are copied/compiled into owning state.
     * @param conditions Optional actual immutable policy owner shared into actor candidates; no functions executed.
     * Pure preparation does not certify its identity; validateImplementationBindings verifies before materialization.
     * @return Owning candidates or failure without registry, entity or capability publication.
     * @remarks Uses canonical unit/member/actor codecs, including versioned actor state, required layout/trait and
     * active-cast restrictions and explicit v1/v2 migration. No current-content substitution. The coordinator must
     * still admit external custom effect implementation/policy contracts and validate core/lifecycle/source relations
     * before any joint materialization/publication.
     * @cost Full skill/unit compilation and per-actor canonical checkpoint preparation, once per restore attempt.
     * @throws std::bad_alloc Local allocation failed without changing live state.
     */
    [[nodiscard]] static Result<RPGSpawnRestoreState> prepare(
        const RPGSpawnArchive& archive, std::shared_ptr<const rpg::SkillConditionBindings> conditions = {});
    /** @brief Borrow all external implementation requirements until this candidate is moved or destroyed.
     * @remarks They remain unresolved facts, not code identity/availability proofs. The coordinator must verify
     * actual bindings before publishing; preparation never consults name-only live callback registries.
     */
    [[nodiscard]] std::span<const rpg::ImplementationRequirement> implementationRequirements() const noexcept;
    /** @brief Verify archived implementation revisions against actual execution owners before materialization.
     * @return Explicit absence, unversioned, mismatch or unsupported binding failure; candidate and live state
     * unchanged.
     * @thread Simulation owner thread; bindings must remain stable through the ensuing synchronous transaction.
     * @cost Linear owner revision queries with owning string copies per restore attempt, without executing callbacks.
     */
    [[nodiscard]] Result<void> validateImplementationBindings() const;
    /** @brief Require linked actors and historical content names in this exact archived closure.
     * @return Missing content fails without consulting current registries or executing code.
     * @ownership Spans are borrowed synchronously; no data is retained or modified.
     * @thread Content owner thread; no callbacks/reentry.
     * @cost Actor indexing plus event-name/reference lookup per joint participant admission. */
    [[nodiscard]] Result<void> validateEventClosure(std::span<const SubjectRef>  subjects,
                                                    std::span<const std::string> skills,
                                                    std::span<const std::string> effects) const;
    /** @brief Share this exact admitted immutable execution content for scoped runtime lookup.
     * @ownership Returned owner survives this state; no registry publication or code execution. */
    [[nodiscard]] std::shared_ptr<const rpg::SkillContent> contentOwner() const noexcept;
    /** @brief Shared save identity, or nil after move. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Verified archive revision; zero after move. */
    [[nodiscard]] Revision revision() const noexcept;
    /** @brief Verified archive simulation boundary; zero after move. */
    [[nodiscard]] SimulationTick tick() const noexcept;
    /** @brief Number of admitted actors; no entity is created. */
    [[nodiscard]] std::size_t actorCount() const noexcept;
    /** @brief Resolve the exact immutable recipe bound for future creation; never infer from existing actors.
     * @return Shared canonical recipe or NotFound/StaleHandle, without live lookup or callbacks.
     * @ownership Returned owner survives candidate move/destruction; reference is borrowed only during the query.
     */
    [[nodiscard]] Result<std::shared_ptr<const UnitRecipe>> futureUnit(const DefinitionRef& reference) const;
    /** @brief Prepare fresh member defaults from the explicit future binding and exact archived skill content.
     * @return Independent owning candidate or NotFound/StaleHandle/schema failure; this state remains reusable.
     * @ownership Candidate shares immutable archived content and actual condition executors, surviving this state's
     * destruction. Reference is borrowed only during this call; no registry, entity or capability is changed.
     * @thread Simulation owner thread, no callbacks. Creation separately verifies actual execution/content bindings.
     * @cost Full member attribute/skill/vital preparation per future unit; share the archive across candidates.
     * @throws std::bad_alloc Local preparation failed without changing this state or the live world.
     */
    [[nodiscard]] Result<rpg::PreparedMemberActor> prepareFutureUnit(const DefinitionRef& reference) const;
    /** @brief Number of explicit future bindings; zero after move. */
    [[nodiscard]] std::size_t futureUnitCount() const noexcept;
    /** @brief Number of independently admitted unit revisions. */
    [[nodiscard]] std::size_t unitRevisionCount() const noexcept;

private:
    friend class RPGSpawnJointRestore;
    friend class RPGSpawnFactory;
    [[nodiscard]] std::shared_ptr<const rpg::SkillConditionBindings> conditionOwner() const noexcept;
    [[nodiscard]] Result<void> validateRelations(std::span<const spawn::SavedSpawnMember>    membership,
                                                 std::uint64_t                               acceptedSequence,
                                                 std::span<const spawn::SavedLifecycleInput> pending) const;
    struct Impl;
    explicit RPGSpawnRestoreState(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::rpg_spawn
