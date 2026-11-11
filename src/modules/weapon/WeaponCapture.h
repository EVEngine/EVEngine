#pragma once
/** @file WeaponCapture.h
 * @brief weapon module-interface: detached live weapon freeze for joint checkpoints.
 * ECS read-only; no systems, events, services or script/tool bindings.
 * Detached value formats: weapon.ammo-pool v1 and weapon.capture v1; no target restore authority.
 */
#include <functional>
#include <optional>
#include <span>
#include "common/SubjectRef.h"
#include "common/Value.h"
#include "weapon/WeaponDefinitionRuntime.h"
namespace eve::attributes {
class AttributeProjectionCapture;
}
namespace eve::weapon {
/** @brief Caller-owned correspondence from an actual target to its authoritative domain identity.
 * @remarks The joint coordinator supplies and validates domain membership; weapon validates world/generation.
 */
struct WeaponCaptureTarget {
    ecs::EntityHandle entity;
    SubjectRef        subject;
};
/** @brief Opaque, weakly observed weapon-pool graph used only during one checkpoint freeze.
 * @ownership Owns archive references and weak AmmoPoolLinks, never a Table or entity. Not persistable.
 * @thread Owner thread outside Views/callbacks; pool links reject retirement and physical replacement.
 */
class EVENGINE_API_WORLD WeaponPoolBindings final {
public:
    /** @brief Release weak observations. */
    ~WeaponPoolBindings();
    /** @brief Transfer observations. */
    WeaponPoolBindings(WeaponPoolBindings&&) noexcept;
    /** @brief Replace observations. */
    WeaponPoolBindings& operator=(WeaponPoolBindings&&) noexcept;
    WeaponPoolBindings(const WeaponPoolBindings&)            = delete;
    WeaponPoolBindings& operator=(const WeaponPoolBindings&) = delete;
    /** @brief Capture all bound weapon-pool relationships in the actual source Table without changing it.
     * @ownership World is borrowed for this synchronous call only; result holds weak links.
     * @return Graph or nil epoch, busy world, stale link or missing/duplicate stable weapon instance identity.
     * @remarks Each pool reference derives from epoch and its lowest stable member weapon instance id. Names
     * do not identify pools. Unreferenced pools and other inventory relationships are outside this graph.
     * Keep the freeze boundary stable between this preparation and individual weapon captures.
     * @cost Scans weapon roots and groups actual pools once per joint checkpoint, amortized over its weapons.
     * @throws std::bad_alloc Source remains unchanged.
     */
    [[nodiscard]] static Result<WeaponPoolBindings> capture(ecs::Table& world, PersistentId saveEpoch);
    /** @brief Checkpoint scope, or nil after move. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Number of actual referenced pools, or zero after move. */
    [[nodiscard]] std::size_t size() const noexcept;

private:
    friend class WeaponCapture;
    struct Impl;
    explicit WeaponPoolBindings(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/** @brief Detached selected attributes; no ECS owner or evaluation callback is retained. */
struct CapturedWeaponAttributes {
    bool                                           initialized = false;
    Revision                                       revision    = Revision::zero();
    std::vector<attributes::AttributeSnapshotBase> bases;
    std::vector<attributes::AttributeModifier>     modifiers;
};
/** @brief Detached shared-pool facts. Identity.id is a descriptive scoped name, not the relationship identity.
 * @remarks archiveId joins aliases within saveEpoch; equal names do not imply equal pools.
 */
struct CapturedAmmoPool {
    AmmoPoolEntity::Identity identity;
    AmmoPoolEntity::State    state;
    /** @brief Checkpoint-local graph reference, independent of the descriptive scoped name. */
    PersistentId archiveId;
    /** @brief Encode complete detached pool facts as weapon.ammo-pool v1.
     * @ownership Returns owning values; retains no entity, lifetime record or callback.
     * @thread Caller synchronizes this value; no simulation or provider access.
     * @return Canonical state or nil archive identity; descriptive names need not be unique or nonempty.
     * @remarks All signed int32 count/max facts are retained, including negative unlimited maxima.
     * Epoch and graph membership belong to the enclosing weapon participant. No restore authority is granted.
     * @cost Copies descriptive strings and allocates a small value tree once per archived pool.
     * @throws std::bad_alloc This value and live worlds remain unchanged.
     */
    [[nodiscard]] EVENGINE_API_WORLD Result<Value> encodeState() const;
    /** @brief Admit weapon.ammo-pool v1 without creating a pool or binding a weapon.
     * @param state Synchronous borrowed value; result owns every fact independently afterwards.
     * @return Detached facts or unknown version/field, malformed canonical UUID or signed integer overflow.
     * @remarks Strict fields; initial v1 has no implicit migration. Native restore still checks the actual
     * destination and rebuilds lifetime authority. This does not authorize an epoch or graph relationship.
     * @thread No source or target world is accessed; caller synchronizes the borrowed input.
     * @cost Copies descriptive strings once per pool admission, amortized per checkpoint load.
     * @throws std::bad_alloc Input and live worlds remain unchanged.
     */
    [[nodiscard]] static EVENGINE_API_WORLD Result<CapturedAmmoPool> prepareState(const Value& state);
};
/** @brief Complete native weapon component facts without runtime pointers or borrowed definition ownership.
 * @remarks request.targetHandle is always empty; target retains the stable correspondence instead.
 * Absent attributes means the component was missing; uninitialized projections retain their seed flag.
 * Resource remains authoritative until canonical initialization. Restoration still requires native target admission.
 */
struct CapturedWeapon {
    WeaponEntity::Identity                  identity;
    WeaponEntity::DefinitionBinding         binding;
    WeaponDefinition                        definition;
    WeaponRuntimeState                      state;
    AttackRequest                           request;
    SubjectRef                              target;
    std::optional<AttackStageSpec>          stages;
    WeaponEntity::Aim                       aim;
    std::optional<CapturedWeaponAttributes> attributes;
    std::optional<CapturedAmmoPool>         pool;
};
/** @brief Immutable detached live-entity freeze, independent of source Table/registry/pools after capture.
 * @thread Capture on the owner thread outside Views; no callbacks, time/RNG advancement or component creation.
 * @ownership Owns all copied facts; never retains entity handles, witnesses or source owners.
 */
class EVENGINE_API_WORLD WeaponCapture final {
public:
    /** @brief Freeze existing components and validate every retained target correspondence.
     * @param weapon Synchronous live borrow; no source reference survives this call.
     * @param saveEpoch Non-nil identity used by the joint checkpoint coordinator.
     * @param targets Authoritative domain identities for any staged request target; unused entries are ignored.
     * @param pools Prepared actual weapon-pool graph for this epoch; no source graph is retained by the result.
     * @return Detached facts or missing/stale/ambiguous identity failure; source state is unchanged.
     * @cost Copies definition strings, selected modifiers and pool facts per weapon, amortized per save boundary.
     * @throws std::bad_alloc Candidate allocation failed; source state remains unchanged.
     */
    [[nodiscard]] static Result<WeaponCapture> capture(WeaponEntity& weapon, PersistentId saveEpoch,
                                                       std::span<const WeaponCaptureTarget> targets,
                                                       const WeaponPoolBindings&            pools);
    /** @brief Destroy detached facts. */
    ~WeaponCapture();
    /** @brief Transfer detached facts. */
    WeaponCapture(WeaponCapture&&) noexcept;
    /** @brief Replace detached facts. */
    WeaponCapture& operator=(WeaponCapture&&) noexcept;
    WeaponCapture(const WeaponCapture&)            = delete;
    WeaponCapture& operator=(const WeaponCapture&) = delete;
    /** @brief Joint identity, or nil after move. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Borrow immutable facts until move/destruction; empty after move. */
    [[nodiscard]] std::optional<std::reference_wrapper<const CapturedWeapon>> facts() const noexcept;
    /** @brief Encode complete weapon.capture v1 values, including epoch, exact definition and native attributes.
     * @ownership Returns owning values; no source entity, callbacks or temporary pointers survive.
     * @thread Detached value access; caller synchronizes against move/destruction.
     * @return Complete facts or moved capture, invalid scalar/identity/enum or nonpersistable native state.
     * @remarks Strict tuples and root fields; target correspondence and pool archive references are descriptive.
     * Does not authorize target topology, implementation revisions or graph publication.
     * @cost Copies complete definition/attributes and validates the value tree once per weapon save boundary.
     * @throws std::bad_alloc Capture and live worlds remain unchanged.
     */
    [[nodiscard]] Result<Value> encodeState() const;
    /** @brief Admit strict weapon.capture v1 into detached native facts without touching a world.
     * @param state Synchronous borrowed input; result independently owns all parsed facts.
     * @return Capture or unknown version/field/enum, malformed identity, invalid projection or nonfinite scalar.
     * @remarks Initial v1 has no implicit migration. Request handles are rebuilt only by native restore;
     * selected attribute inspection is derived from the full projection, never independently imported.
     * @thread No providers, policies, simulation time or callbacks are invoked.
     * @cost Copies definition, request, pool and attributes once per parsed weapon; no ECS materialization.
     * @throws std::bad_alloc Input and live worlds remain unchanged.
     */
    [[nodiscard]] static Result<WeaponCapture> prepareState(const Value& state);
    /** @brief Borrow full native projection facts, including internal counters, until move/destruction.
     * @remarks Empty when the source Attributes component was absent or this capture was moved. Selected public
     * metadata remains a derived inspection projection; exact restoration uses these canonical native facts.
     */
    [[nodiscard]] std::optional<std::reference_wrapper<const attributes::AttributeProjectionCapture>> attributes()
        const noexcept;

private:
    struct Impl;
    explicit WeaponCapture(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::weapon
