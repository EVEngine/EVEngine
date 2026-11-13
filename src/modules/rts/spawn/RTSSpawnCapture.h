#pragma once
/** @file RTSSpawnCapture.h
 * @brief rts_spawn module-interface: immutable provenance/content/receipt facts for joint saving.
 * @remarks Data face rts:spawn-state v1; no ECS roots, event consumption, scripts or tools. Live weapon facts are
 * joined per present member. Native RTS/Crowd/action state remain separate participants; this capture alone is not a
 * domain or game checkpoint.
 */
#include "common/Snapshot.h"
#include "spawn/SpawnWorld.h"
namespace eve::weapon {
class WeaponCapture;
}
namespace eve::rts {
class PreparedUnitArchetype;
struct CommandSpec;
struct RTSUnitSnapshot;
struct RTSFrameDamageEvent;
}  // namespace eve::rts
namespace eve::rts_spawn {
class RTSSpawnFactory;
/** @brief Whether the exact admitted unit incarnation still exists at the freeze boundary. */
enum class CapturedUnitPresence { Present, Absent };
/** @brief Immutable creation recipe and observed source relationship; contains no entity handle or source lease.
 * @remarks Absent is not an inferred death. The joint coordinator must account for retained lifecycle inputs.
 * Runtime source incarnation is for in-process joining only and must not be encoded as a persistent identity.
 */
struct CapturedSpawnUnit {
    SubjectRef                                        subject;
    std::optional<spawn::SpawnerHandle>               origin;
    CapturedUnitPresence                              presence = CapturedUnitPresence::Absent;
    std::shared_ptr<const rts::PreparedUnitArchetype> recipe;
    /** @brief Actual live weapon state when present, detached from the source world. */
    std::shared_ptr<const weapon::WeaponCapture> weapon;
};
/** @brief One exact future recipe, independent of later registry replacement or destruction. */
struct CapturedFutureUnit {
    DefinitionRef                                     reference;
    std::shared_ptr<const rts::PreparedUnitArchetype> recipe;
};
/** @brief Latest bounded receipt for one live source, with owning payload and no borrowed lifetime token.
 * @remarks Includes removed members: a retained creation receipt must never recreate its deleted units.
 */
struct CapturedSpawnReceipt {
    spawn::SpawnerHandle            source;
    PersistentId                    operation;
    spawn::OperationOrdinal         ordinal;
    std::vector<spawn::UnitRequest> units;
    std::vector<spawn::Point>       positions;
};
/** @brief Move-only freeze of one actual factory's provenance, source watermarks and content closure.
 * @ownership Owns copies/immutable recipe owners, never the RTS/Table/registry, raw ECS handles or source leases.
 * @thread Capture on the simulation owner thread outside Views; independent reads after source retirement.
 * @remarks A coordinator must join these facts with core, retained lifecycle, complete native domain and backend
 * snapshots before save publication. No implicit lifecycle reconciliation, latest-revision substitution or restore.
 */
class EVENGINE_API_ORCHESTRATION RTSSpawnCapture final {
public:
    /** @brief Release detached facts only. */
    ~RTSSpawnCapture();
    /** @brief Transfer detached facts. */
    RTSSpawnCapture(RTSSpawnCapture&&) noexcept;
    /** @brief Replace detached facts by move. */
    RTSSpawnCapture& operator=(RTSSpawnCapture&&) noexcept;
    RTSSpawnCapture(const RTSSpawnCapture&)            = delete;
    RTSSpawnCapture& operator=(const RTSSpawnCapture&) = delete;
    /** @brief Joint save identity, or nil after move. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Domain faction's persistent identity, or empty after move. */
    [[nodiscard]] SubjectRef faction() const noexcept;
    /** @brief Bound source owner, or nil before the first publication/after move. */
    [[nodiscard]] PersistentId sourceOwner() const noexcept;
    /** @brief Bound source epoch, or zero before the first publication/after move. */
    [[nodiscard]] std::uint64_t sourceEpoch() const noexcept;
    /** @brief Observed runtime source incarnation for joint validation only; never serialize this value. */
    [[nodiscard]] spawn::WorldIncarnation sourceIncarnation() const noexcept;
    /** @brief Borrow subject-ordered unit facts until move/destruction; no entity pointers escape. */
    [[nodiscard]] std::span<const CapturedSpawnUnit> units() const noexcept;
    /** @brief Borrow reference-ordered deduplicated future recipes until move/destruction. */
    [[nodiscard]] std::span<const CapturedFutureUnit> futureUnits() const noexcept;
    /** @brief Resolve an exact saved future recipe without accessing a world or current registry.
     * @param reference Logical unit reference from the jointly saved core.
     * @return Shared immutable admitted recipe or missing/moved capture failure; never substitutes latest content.
     * @ownership Returned recipe owns its facts independently of this capture and the source game.
     * @thread Owner/content thread, read-only; no callbacks, provider lookup or time advancement.
     * @cost Linear saved future-reference lookup per explicit admission/batch, independent of world size.
     */
    [[nodiscard]] Result<std::shared_ptr<const rts::PreparedUnitArchetype>> resolveFuture(
        const DefinitionRef& reference) const;
    /** @brief Borrow source-slot-ordered latest live-source receipts until move/destruction. */
    [[nodiscard]] std::span<const CapturedSpawnReceipt> receipts() const noexcept;
    /** @brief Validate durable core/source/native-body relations and both retained lifecycle handoff stages.
     * @return Success or moved/duplicate/missing/source/sequence/body contradiction, without repairing facts.
     * @ownership Inputs borrowed synchronously; no root pointer or live authority retained. No callbacks.
     * @remarks Runtime incarnation equality is checked separately by a live coordinator before durable projection.
     * Native-only admission: persistent callers must integrity-cover all participants together.
     * @thread Owner thread outside Views and mutations.
     * @cost Indexes all joined members, roots and handoff facts once per save/restore boundary.
     * @throws std::bad_alloc No input or live state was changed.
     */
    [[nodiscard]] Result<void> validateRelations(std::span<const spawn::SavedSpawnMember>    membership,
                                                 std::uint64_t                               acceptedSequence,
                                                 std::span<const rts::RTSUnitSnapshot>       roots,
                                                 std::span<const spawn::SavedLifecycleInput> pending,
                                                 std::span<const rts::RTSFrameDamageEvent>   damage) const;
    /** @brief Last translated lifecycle sequence; capture does not advance it. */
    [[nodiscard]] std::uint64_t lifecycleSequence() const noexcept;
    /** @brief Borrow copied initial command with its ECS target cleared, or null when none/moved-from.
     * @ownership Borrow expires at move/destruction; copy to retain. Target identity is initialCommandTarget().
     * @lifetime The command never borrows the original factory, entity or registry.
     */
    [[nodiscard]] const rts::CommandSpec* initialCommand() const noexcept;
    /** @brief Stable identity of the initial command's RTS target, or empty for a point-only/no command. */
    [[nodiscard]] SubjectRef initialCommandTarget() const noexcept;
    /** @brief Seal complete provenance, exact current/future recipes, command and bounded latest receipts.
     * @param revision Nonzero enclosing save revision.
     * @param tick Enclosing save tick; the game coordinator joins all participant phases/time.
     * @param hash Synchronous borrowed digest provider; never retained.
     * @return Owning rts:spawn-state v1 envelope or moved/inconsistent capture failure.
     * @ownership Copies all facts before hash; the callback may retire this capture and its source world.
     * @remarks Runtime incarnation, leases, ECS handles and registry references are excluded. Sealing grants no
     * source authority; core/native roots and retained handoffs remain separate participants.
     * @thread Detached owner/content thread; no policies, services or simulation advancement.
     * @cost Copies complete recipes/weapons and receipt payloads per save boundary, independent of live world size.
     * @throws std::bad_alloc Capture and live worlds remain unchanged except explicit caller callback effects.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(Revision revision, SimulationTick tick,
                                                const SnapshotHashProvider& hash) const;
    /** @brief Verify strict rts:spawn-state v1 into detached facts without live source authority.
     * @param snapshot Owning input established before digest callbacks.
     * @param epoch Expected non-nil enclosing save identity.
     * @param hash Synchronous borrowed digest verification provider; never retained.
     * @return Capture or invalid boundary, version/fields, recipe/reference/command/receipt data.
     * @remarks Unknown fields/versions reject; initial v1 has no implicit migration. Parsed incarnation fields
     * are zero. Actual core leases and fresh incarnations are issued only during native restoreSources after
     * validateRelations, complete root/context admission and canonical receipt identity checks.
     * @thread Pure CPU content admission; no entities, capability registration or callbacks beyond hash.
     * @cost Parses complete nested content and validates all saved records once per source participant load.
     * @throws std::bad_alloc Caller input and live worlds remain unchanged except explicit callback effects.
     */
    [[nodiscard]] static Result<RTSSpawnCapture> parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                       const SnapshotHashProvider& hash);

private:
    friend class RTSSpawnFactory;
    struct Impl;
    explicit RTSSpawnCapture(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::rts_spawn
