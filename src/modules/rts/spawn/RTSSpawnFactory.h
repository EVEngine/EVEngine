#pragma once
/** @file RTSSpawnFactory.h
 * @brief rts_spawn module-interface: native RTS factory and lifecycle projection.
 * @remarks Inputs are the owning RTS domain, immutable content revisions and one faction.
 * Output implements ISpawnFactory; ECS roots and commands remain RTS-owned. Lifecycle facts
 * are translated from already-dispatched damage facts and actual roots. No new scripts/tools or persistent format.
 */
#include "spawn/SpawnWorld.h"
namespace eve::definitions {
class DefinitionRegistry;
}
namespace eve::rts {
class RTS;
struct CommandSpec;
struct RTSFrameDamageEvent;
}  // namespace eve::rts
namespace eve::rts_spawn {
class RTSSpawnCapture;
/** @brief Owning observation of the authoritative RTS unit, without borrowed ECS pointers. */
struct UnitSnapshot {
    SubjectRef                          subject;
    std::optional<spawn::SpawnerHandle> origin;
    spawn::Point                        position;
    double                              health = 0;
};
/** @brief Real RTS/Crowd factory with bounded latest-operation receipts.
 * @ownership Weakly observes RTS/Table; retains content and owning recipe revisions, never unit pointers.
 * @thread Simulation owner thread outside Views. @reentrancy No nested factory operations or callbacks.
 * @lifetime RTS and its provider lifecycle contracts must remain valid for calls. Tokens weakly observe the factory.
 */
class EVENGINE_API_ORCHESTRATION RTSSpawnFactory final : public spawn::ISpawnFactory,
                                                         public spawn::IUnitDefinitionResolver {
public:
    /** @brief Bind one domain/faction and content authority; copy the optional canonical initial command. */
    RTSSpawnFactory(rts::RTS& domain, SubjectRef faction, std::shared_ptr<definitions::DefinitionRegistry> registry,
                    std::shared_ptr<const rts::CommandSpec> initialCommand = {});
    /** @brief Prepare a pristine native restore factory using only exact saved future recipes and initial command.
     * @return Unpublished factory or moved capture, missing faction/command target, borrowed/busy world/provider error.
     * @ownership Shares immutable capture; weakly observes actual target RTS/Table. No live content registry retained.
     * @thread Owner thread outside Views/callbacks; creates no roots, agents, source leases or origin indexes.
     * @remarks Target native roots and providers must already be staged. validate can admit core templates before
     * restoreSources; gameplay/capture operations reject until source binding succeeds. Not joint world publication.
     * @cost Copies command and prepares factory ownership once per isolated restore attempt.
     * @throws std::bad_alloc No source, target or running session was changed.
     */
    [[nodiscard]] static Result<std::unique_ptr<RTSSpawnFactory>> prepareRestored(
        rts::RTS& domain, std::shared_ptr<const RTSSpawnCapture> captured);
    /** @brief Bind existing staged roots to actual restored core sources, receipts and retired operation floors.
     * @return Owning remapped translated lifecycle inputs, or failure preserving factory/core/domain for retry.
     * @ownership Requires a pristine restore factory and the actual core execution factory (including decorators).
     * Inputs borrowed synchronously; result owns events, source leases weakly observe the new core only.
     * @remarks Reuses canonical source/body/handoff relation admission; never infers deaths, recreates members,
     * advances time or changes native roots. Raw damage remains the coordinator's retained input responsibility.
     * Bodies/weapons/Crowd/content/clocks and persistent integrity still need whole-session coordination.
     * @thread Owner thread outside Views/callbacks; rejects reentry. No gameplay callbacks or locks.
     * @cost Copies/indexes all provenance, native roots, core bindings and receipts once per restore attempt;
     * successful final index publication allocates nothing.
     * @throws std::bad_alloc Local staging failed without publication; the same factory can retry.
     */
    [[nodiscard]] Result<std::vector<spawn::LifecycleInput>> restoreSources(
        const spawn::SpawnWorld& core, std::span<const spawn::SavedLifecycleInput> pending,
        std::span<const rts::RTSFrameDamageEvent> damage);
    /** @brief Release derived indexes; units remain owned by RTS. */
    ~RTSSpawnFactory() override;
    RTSSpawnFactory(const RTSSpawnFactory&)            = delete;
    RTSSpawnFactory& operator=(const RTSSpawnFactory&) = delete;
    /** @brief Validate actual domain/faction, live Crowd availability and the copied initial command.
     * @return Missing/stale/unsupported service or command error without mutation or callbacks.
     * @thread Owner thread outside Views; later batch prepare/commit still revalidate their live inputs.
     * @cost Faction/command admission per explicit composition check; no unit or navigation snapshot is captured.
     */
    [[nodiscard]] Result<void> validateDomain() const;
    /** @brief Freeze provenance, exact recipes, live weapon facts, latest receipts and lifecycle sequence.
     * @param saveEpoch Nonnil joint save identity, not a source epoch or world incarnation.
     * @param futureUnits Core checkpoint's unit references; resolved once at this same quiescent boundary.
     * @return Detached participant or stale owner/source/target, missing recipe or invalid reference failure.
     * @ownership Input span is borrowed synchronously; no RTS/Table/registry/lease survives in the result.
     * @remarks Joins live WeaponCapture; native unit/Crowd/action state and retained events still need the coordinator.
     * Does not consume lifecycle events. Expired source leases
     * reject: reconcile or explicitly release those origins before saving. Missing bodies remain explicit Absent
     * facts for coordinator validation, never implicit deaths. No source/index or receipt mutation on failure.
     * @thread Domain owner thread outside Views and concurrent content changes; rejects factory reentry.
     * @cost Linear provenance/receipt and live weapon/attribute copies plus distinct future recipe compilation per save
     * boundary.
     * @throws std::bad_alloc Local allocation failed without factory or domain mutation.
     */
    [[nodiscard]] Result<RTSSpawnCapture> capture(PersistentId                   saveEpoch,
                                                  std::span<const DefinitionRef> futureUnits) const;
    /** @brief Validate canonical RTS unit content without creating roots.
     * @cost Canonical recipe parsing per content/map admission; no domain mutation. */
    [[nodiscard]] Result<void> validate(const DefinitionRef& reference) const override;
    /** @brief Validate canonical identities, pin recipes and prepare the native atomic batch.
     * @return Hidden token or structured failure. Identical latest receipts replay; old ordinals reject.
     * @cost Bounded identity checks, definition parsing and native preparation per batch, amortized across its units.
     */
    [[nodiscard]] Result<spawn::PrepareOutcome> prepare(const spawn::BatchRequest&    request,
                                                        std::span<const spawn::Point> positions) override;
    /** @brief Atomically destroy source-owned units or release origin; repeated removal is idempotent.
     * @cost Index validation and one canonical RTS reference sweep per removal batch.
     */
    [[nodiscard]] Result<spawn::RemovalReceipt> remove(const spawn::RemovalRequest& request) override;
    /** @brief Copy live domain state; no raw pointers escape. */
    [[nodiscard]] Result<UnitSnapshot> inspect(SubjectRef subject) const;
    /** @brief Translate shared damage facts and actual death/stale roots without polling domain queues.
     * @cost Linear tracked-unit scan and bounded damage indexing per simulation step.
     */
    [[nodiscard]] Result<std::vector<spawn::LifecycleInput>> collectLifecycle(
        std::span<const rts::RTSFrameDamageEvent> events);

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}  // namespace eve::rts_spawn
