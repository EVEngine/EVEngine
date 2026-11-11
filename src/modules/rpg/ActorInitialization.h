#pragma once

#include <memory>
#include <string_view>
#include "common/ECS.h"
#include "common/Export.h"
#include "common/Result.h"
#include "rpg/ActorExecutionOrder.h"
#include "rpg/SkillTypes.h"

namespace eve::rpg {
class RPGActor;
class SkillContent;
class SkillConditionBindings;
struct EncounterMemberDefinition;
/**
 * @brief Materialize every RPG base component on a fresh actor before publication.
 * @ownership Actor is borrowed only for this synchronous call; no pointers are retained.
 * @thread ECS owner thread; no callbacks, events or registry mutations outside this actor.
 * @throws Propagates allocation failure; caller must dispose the unpublished candidate.
 */
EVENGINE_API_PLATFORM void initializeActorComponents(RPGActor& actor);
/** @brief Validate the canonical member recipe and its skill reference without mutation or callbacks. */
[[nodiscard]] EVENGINE_API_PLATFORM Result<void> validateMemberDefinition(const EncounterMemberDefinition& member);
/** @brief Validate canonical member fields and references against admitted immutable skill content.
 * @ownership Synchronous borrows only; no registry lookup, callbacks or mutation.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<void> validateCapturedMemberDefinition(
    const EncounterMemberDefinition& member, const SkillContent& skills);
/**
 * @brief Initialize an unpublished fresh actor using the canonical encounter/unit recipe.
 * @return Validation outcome; allocation exceptions leave the candidate for caller rollback.
 * @cost Component and attribute/skill allocation proportional to recipe size, once per created actor.
 * @ownership Synchronous borrow; no actor or recipe references retained.
 * @thread ECS owner thread; no user callbacks or gameplay events.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<void> initializeMemberActor(RPGActor&                        actor,
                                                                       const EncounterMemberDefinition& member);
/** @brief RPG module-interface: owning CPU member state prepared without an ECS object or global capabilities.
 * @ownership Owns canonical attributes, skills, progression and vitals. No actor, table or recipe pointer retained.
 * @thread RPG owner thread; skill content stays stable until creation. No scripts, events or tool endpoints.
 * @remarks Checkpoints reuse the canonical versioned actor codec; class/trait identity must match a plain member.
 */
class EVENGINE_API_PLATFORM PreparedMemberActor {
public:
    /** @brief Cancel unpublished data locally. */
    ~PreparedMemberActor();
    /** @brief Transfer unpublished state. */
    PreparedMemberActor(PreparedMemberActor&&) noexcept;
    /** @brief Cancel old state and transfer ownership. */
    PreparedMemberActor& operator=(PreparedMemberActor&&) noexcept;
    PreparedMemberActor(const PreparedMemberActor&)            = delete;
    PreparedMemberActor& operator=(const PreparedMemberActor&) = delete;
    /** @brief Validate and prepare canonical member defaults without creating an actor.
     * @param conditions Optional actual condition executors. If supplied, all policy uses must match their contracts;
     * omission preserves legacy unbound member creation. Spawn factory admission separately requires needed bindings.
     * @return Owning state or recipe/skill failure, with no entity/capability publication.
     * @cost Attribute, skill and vital allocation proportional to member data, once per candidate.
     * @throws std::bad_alloc Owning allocation failed without entity/capability publication.
     */
    [[nodiscard]] static Result<PreparedMemberActor> prepare(
        const EncounterMemberDefinition& member, std::shared_ptr<const SkillConditionBindings> conditions = {});
    /** @brief Prepare member facts using exact admitted archive skills, without accessing live content.
     * @param conditions Owning immutable actual policy callbacks for restored actors, shared through clone/creation.
     * @return Owning candidate or failure without publication; a missing content owner fails explicitly.
     * @ownership Shares immutable skill facts for this candidate and its clones; member is borrowed only here.
     * @remarks Checkpoint validation also uses this content. Creation requires the live skill projection to match
     * these exact skill records, admitted direct effect facts and saved implementation revisions; mismatch fails before
     * actor creation instead of adopting current content.
     * @cost Attribute/skill preparation per member; immutable content ownership is shared across the restore batch.
     */
    [[nodiscard]] static Result<PreparedMemberActor> prepareCaptured(
        const EncounterMemberDefinition& member, std::shared_ptr<const SkillContent> skills,
        std::shared_ptr<const SkillConditionBindings> conditions = {});
    /** @brief Prepare an independent attempt from admitted facts, without consulting unit content or creating entities.
     * @return Owning candidate or StaleHandle if this object was consumed. This object is unchanged.
     * @cost Deep attribute, skill and vital copy per attempt; retain the source to retry a larger transaction.
     * @throws std::bad_alloc Owning copy failed without publication or source mutation.
     */
    [[nodiscard]] Result<PreparedMemberActor> clone() const;
    /** @brief Validate a canonical checkpoint against this member's layout and replace only candidate data.
     * @param legacyOrder Explicit coordinator default for v1-v4 imports, which did not store phase order.
     * It never replaces a v5 ordering fact; full coordinators must require a present unique key.
     * @remarks Legacy v1/v2 imports require a clean effect prototype; v3/v4 replace complete active effects; v4 admits
     * detached cast intent.
     * @return Success or failure preserving this candidate for retry; no live owner is touched.
     * @cost Full actor checkpoint parsing and owning validation per restore attempt.
     * @throws std::bad_alloc Owning allocation failed; this candidate remains unchanged.
     */
    [[nodiscard]] Result<void> restoreCheckpoint(std::string_view                 json,
                                                 std::optional<ActorExecutionKey> legacyOrder = std::nullopt);
    /** @brief Read the admitted execution ordering value, without consulting any world.
     * @return Owning key or NotFound/StaleHandle for an unkeyed/consumed candidate; no callbacks or mutation.
     */
    [[nodiscard]] Result<ActorExecutionKey> executionKey() const;
    /** @brief Read the detached active linked target identity, or null for inactive/self/retired casts.
     * @return Owning optional identity or StaleHandle after consumption; no world lookup or callbacks.
     */
    [[nodiscard]] Result<std::optional<SubjectRef>> restoredCastTarget() const;
    /** @brief Publish admitted member facts to an already allocated, clean RPG actor in a candidate world.
     * @param actor Synchronous borrow with all canonical components, no active effects/cast and plain member
     * class/traits.
     * @param target Coordinator-admitted target incarnation; required exactly for saved linked casts.
     * @return Success consumes this candidate. Failure preserves actor and candidate; no gameplay events.
     * @ownership Caller owns actor lifetime and rollback of its entire unpublished batch. No borrowed data retained.
     * @thread Simulation owner thread, outside Views; no callbacks or locks. Validate all roots before owner swap.
     * @cost Validate exact content per known skill and effect, then move prepared facts once per candidate actor.
     * @throws std::bad_alloc Validation failed before publication; both inputs remain unchanged.
     */
    [[nodiscard]] Result<void> publishTo(RPGActor& actor, std::optional<CastTargetBinding> target = std::nullopt);
    /** @brief Read one admitted vital without entity creation, global lookup or callbacks.
     * @return Value, or StaleHandle/NotFound for consumed state or an absent resource.
     * @ownership Resource name is borrowed only for this synchronous owner-thread query.
     * @throws std::bad_alloc Lookup key or diagnostic allocation failed without state changes.
     */
    [[nodiscard]] Result<double> currentVital(std::string_view resource) const;
    /** @brief Verify exact admitted content and actual execution bindings without creating an actor.
     * @return Success or missing/mismatched content/code failure, retaining this candidate for retry.
     * @ownership No borrowed pointer escapes; no registry mutation or callback execution.
     * @thread Simulation/content owner thread; bindings must remain stable through synchronous creation/capture.
     * @cost Canonical source and effect encoding per known skill and active effect, before each ECS publication.
     * @throws std::bad_alloc Validation allocation failed without mutation.
     */
    [[nodiscard]] Result<void> validateCreationBindings() const;
    /** @brief Create one actor in the current ECS table and consume this state only on success.
     * @return Table-owned generation handle, or failure with no new actor and the candidate retained for retry.
     * @ownership The returned handle does not extend table lifetime. Call outside Views on the owner thread.
     * @reentrancy No callbacks or gameplay events; failed initialization destroys the whole unpublished prefix.
     * @cost Exact skill source validation for archived candidates plus ECS root/component allocation per actor;
     * prepared actor facts commit without allocation.
     * @throws std::bad_alloc Source validation allocation failed before entity creation.
     */
    [[nodiscard]] Result<ecs::EntityHandle> create();

private:
    friend Result<void> initializeMemberActor(RPGActor&, const EncounterMemberDefinition&);
    struct Impl;
    explicit PreparedMemberActor(std::unique_ptr<Impl> impl);
    [[nodiscard]] static Result<PreparedMemberActor> prepareWithContent(const EncounterMemberDefinition&    member,
                                                                        std::shared_ptr<const SkillContent> skills);
    void                                             applyTo(RPGActor& actor) noexcept;
    std::unique_ptr<Impl>                            impl_;
};

/**
 * @brief Create one fully initialized actor in the current domain table with complete failure cleanup.
 * @return Generation-qualified identity, or validation/allocation failure without a visible candidate.
 * @cost Actor/component/attribute allocation proportional to recipe size, once per domain creation.
 * @ownership Table owns the actor; the returned handle never extends table lifetime.
 * @thread Current ECS owner thread; no callbacks. Keep the table alive when resolving the handle.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<ecs::EntityHandle> createInitializedActor(
    const EncounterMemberDefinition& member);
}  // namespace eve::rpg
