#pragma once
/** @file RTSWeaponCapture.h
 * @brief rts module-interface: detached complete Unit/Building weapon closure at one checkpoint epoch.
 * @remarks Persistent data face rts:body-weapons v1; root topology, content and restore authority remain separate.
 * No systems, events, services or script/tool bindings are introduced.
 */
#include <memory>
#include <optional>
#include <span>
#include "common/Export.h"
#include "common/Snapshot.h"
#include "common/SubjectRef.h"
namespace eve::weapon {
class WeaponCapture;
}
namespace eve::rts {
class RTS;
/** @brief Native body family; identity alone cannot authorize changing a Unit into a Building. */
enum class RTSWeaponBodyKind { Unit, Building };
/** @brief Stable body relationship, including explicit weapon absence.
 * @ownership Shares detached immutable weapon facts, never a source entity, Table or Link.
 * @remarks Aliased body links share the same capture owner; distinct weapons cannot share an instance identity.
 */
struct RTSWeaponMember {
    SubjectRef                                   body;
    std::shared_ptr<const weapon::WeaponCapture> weapon;
    /** @brief Actual source body family, checked during native reconstruction. */
    RTSWeaponBodyKind kind = RTSWeaponBodyKind::Unit;
};
/** @brief All owned Unit/Building weapon relationships, independent of spawn provenance or content registry.
 * @ownership Owns immutable native facts; no runtime handles, leases or source world survive capture.
 * @thread Capture on the owner thread outside Views/callbacks. Detached access does not advance simulation.
 */
class EVENGINE_API_DOMAINS RTSWeaponCapture final {
public:
    /** @brief Release detached facts only. */
    ~RTSWeaponCapture();
    /** @brief Transfer facts. */
    RTSWeaponCapture(RTSWeaponCapture&&) noexcept;
    /** @brief Replace facts by move. */
    RTSWeaponCapture& operator=(RTSWeaponCapture&&) noexcept;
    RTSWeaponCapture(const RTSWeaponCapture&)            = delete;
    RTSWeaponCapture& operator=(const RTSWeaponCapture&) = delete;
    /** @brief Checkpoint epoch, or nil after move. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Borrow body-identity-ordered rows until move/destruction; empty after move. */
    [[nodiscard]] std::span<const RTSWeaponMember> members() const noexcept;
    /** @brief Seal complete body-to-weapon aliases and full weapon/pool facts at an injected save boundary.
     * @param revision Nonzero enclosing save revision, distinct from individual attribute revisions.
     * @param tick Enclosing checkpoint tick; caller joins phase/time with other participants.
     * @param hash Synchronous borrowed digest provider; never retained.
     * @return Owning rts:body-weapons v1 envelope or moved/invalid/conflicting graph facts.
     * @ownership Copies all facts before invoking hash; callback may retire this capture or its source world.
     * @thread Caller synchronizes detached facts; no world, policies, simulation or entity creation is accessed.
     * @cost Copies each unique weapon definition/attributes once and indexes body/pool aliases per save attempt.
     * @throws std::bad_alloc Detached capture and live worlds remain unchanged.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(Revision revision, SimulationTick tick,
                                                const SnapshotHashProvider& hash) const;
    /** @brief Verify and admit strict rts:body-weapons v1 without creating bodies, weapons or pools.
     * @param snapshot Owning input copied before digest callbacks; no borrowed wire data survives.
     * @param epoch Expected non-nil enclosing checkpoint identity.
     * @param hash Synchronous digest verification provider; never retained.
     * @return Detached graph with shared capture owners for aliases, or invalid boundary/fields/references.
     * @remarks Unknown fields/versions, duplicate/unreferenced weapons and conflicting shared-pool facts reject.
     * Initial v1 has no implicit migration. Native restoration still checks full target topology, body kind,
     * physical owners/View and referenced domain targets; parsing grants no external implementation authority.
     * @thread No source/target world, callbacks beyond hash, capability registration or simulation is accessed.
     * @cost Parses complete unique weapon facts and indexes graph references once per checkpoint load.
     * @throws std::bad_alloc Input caller and live worlds remain unchanged.
     */
    [[nodiscard]] static Result<RTSWeaponCapture> parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                        const SnapshotHashProvider& hash);

private:
    friend class RTS;
    struct Impl;
    explicit RTSWeaponCapture(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::rts
