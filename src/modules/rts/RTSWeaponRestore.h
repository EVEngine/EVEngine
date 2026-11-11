#pragma once
/** @file RTSWeaponRestore.h
 * @brief rts module-interface: native body weapon graph materialization and explicit restored pool ownership.
 * @remarks No persistent schema, systems, time advancement, scripts or tools. Caller coordinates other participants.
 */
#include <memory>
#include <span>
#include "common/Export.h"
#include "common/Result.h"
#include "common/SubjectRef.h"
namespace eve::weapon {
struct AmmoPoolRestoreReceipt;
}
namespace eve::rts {
class RTS;
class RTSWeaponCapture;
/** @brief Own only the restored referenced pool rows; RTS independently owns restored guns and body links.
 * @ownership Weakly observes the destination Table, never keeps RTS or a world alive. Game composition retains this
 * owner and closes it on unload. Closing/destroying it first makes gun pool links stale; RTS destruction first
 * retires guns but leaves pools alive while this owner and the actual Table remain. Replaced generations are ignored.
 * @thread Owner thread; destruction/move replacement must occur outside Views of its destination Table.
 */
class EVENGINE_API_DOMAINS RestoredRTSWeaponPools final {
public:
    /** @brief Release remaining owned rows as local noexcept resource teardown, without gameplay callbacks. */
    ~RestoredRTSWeaponPools();
    /** @brief Transfer pool responsibility. */
    RestoredRTSWeaponPools(RestoredRTSWeaponPools&&) noexcept;
    /** @brief Retire the previous owned rows and transfer pool responsibility. */
    RestoredRTSWeaponPools& operator=(RestoredRTSWeaponPools&&) noexcept;
    RestoredRTSWeaponPools(const RestoredRTSWeaponPools&)            = delete;
    RestoredRTSWeaponPools& operator=(const RestoredRTSWeaponPools&) = delete;
    /** @brief Joint epoch, or nil after move/close. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Borrow source archive correspondences and new weak links until move/close/destruction. */
    [[nodiscard]] std::span<const weapon::AmmoPoolRestoreReceipt> pools() const noexcept;
    /** @brief Explicitly release owned pool rows; busy destination rejects without losing responsibility.
     * @return Applied/NoOp or View rejection; already dead worlds/rows require no further cleanup.
     * @thread Owner thread outside destination Views; no policy evaluation, events or callbacks.
     * @cost Linear pool retirement once per owner scope; no successful allocation.
     */
    [[nodiscard]] Result<void> close();

private:
    friend class RTS;
    struct Impl;
    explicit RestoredRTSWeaponPools(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/** @brief CPU candidates for a complete pristine body weapon scope, bound to one physical RTS and topology.
 * @ownership Owns immutable native captures and target observations; retains no source/target world or facade.
 * @thread Owner thread outside Views/callbacks. Destruction releases CPU data only.
 */
class EVENGINE_API_DOMAINS PreparedRTSWeaponRestore final {
public:
    /** @brief Discard unpublished CPU facts. */
    ~PreparedRTSWeaponRestore();
    /** @brief Transfer candidates. */
    PreparedRTSWeaponRestore(PreparedRTSWeaponRestore&&) noexcept;
    /** @brief Replace unpublished candidates. */
    PreparedRTSWeaponRestore& operator=(PreparedRTSWeaponRestore&&) noexcept;
    PreparedRTSWeaponRestore(const PreparedRTSWeaponRestore&)            = delete;
    PreparedRTSWeaponRestore& operator=(const PreparedRTSWeaponRestore&) = delete;
    /** @brief Admit all saved Unit/Building weapon relations against existing unarmed target roots.
     * @return Candidate or moved source, borrowed/busy world, body kind/topology, claimed weapon scope or target error.
     * @remarks Intended for isolated native root reconstruction. Does not replace live guns or create ECS rows.
     * Captured absence and body aliases are exact; guns and referenced pools are materialized only by restoreWeapons.
     * @ownership Source borrowed synchronously; returned facts survive source capture/world destruction.
     * @cost Copies/indexes graph relations and binds request targets once per explicit restore attempt.
     * @throws std::bad_alloc No source or target facts/links/capabilities changed.
     */
    [[nodiscard]] static Result<PreparedRTSWeaponRestore> prepare(RTS& target, const RTSWeaponCapture& captured);

private:
    friend class RTS;
    struct Impl;
    explicit PreparedRTSWeaponRestore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::rts
