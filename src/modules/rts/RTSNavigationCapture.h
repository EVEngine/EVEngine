#pragma once
/** @file RTSNavigationCapture.h
 * @brief rts module-interface: detached native map provider and surface-publication tracking facts.
 * @remarks Persistent data face eve.rts-navigation / rts:navigation v1; external callback implementations are not
 * captured.
 */
#include <memory>
#include "common/BorrowedRef.h"
#include "common/Export.h"
#include "common/Identity.h"
#include "common/Snapshot.h"
namespace eve::map {
class PathfinderState;
}
namespace eve::rts {
class RTS;
/** @brief Immutable map-provider facts and desired/last-published RTS surface tracking state.
 * @ownership Contains no live provider, layer, Table or callback; source owners may die after capture.
 * @thread Synchronous simulation-owner capture; detached reads do not advance time or invoke services.
 */
class EVENGINE_API_DOMAINS RTSNavigationCapture final {
public:
    /** @brief Release detached CPU facts. */
    ~RTSNavigationCapture();
    /** @brief Transfer detached facts. */
    RTSNavigationCapture(RTSNavigationCapture&&) noexcept;
    /** @brief Replace detached facts. */
    RTSNavigationCapture& operator=(RTSNavigationCapture&&) noexcept;
    RTSNavigationCapture(const RTSNavigationCapture&)            = delete;
    RTSNavigationCapture& operator=(const RTSNavigationCapture&) = delete;
    /** @brief Joint epoch, or nil after move. */
    [[nodiscard]] PersistentId saveEpoch() const noexcept;
    /** @brief Borrow native provider facts until move/destruction; absent after move. */
    [[nodiscard]] OptionalRef<const map::PathfinderState> provider() const noexcept;

    /** @brief Seal exact provider and publication tracking facts as eve.rts-navigation / rts:navigation v1.
     * @param revision Nonzero joint-save revision.
     * @param tick Caller-coordinated simulation boundary, not a wall clock.
     * @param hash Synchronous digest provider; never retained.
     * @return Owning envelope with complete map.pathfinder-state and nullable tracker, or invalid facts.
     * @remarks Copies all facts before invoking hash; does not synchronize or publish a navigation surface.
     * @thread Immutable CPU encoding; caller synchronizes moves. No locks or gameplay callbacks.
     * @cost Copies full provider grids/caches and tracking facts once per explicit joint save.
     * @throws std::bad_alloc Source and live worlds remain unchanged.
     */
    [[nodiscard]] Result<SnapshotEnvelope> seal(Revision revision, SimulationTick tick,
                                                const SnapshotHashProvider& hash) const;
    /** @brief Admit strict v1 provider/tracker data without creating or claiming a provider.
     * @param snapshot Owned envelope; epoch must match its nonnil instance identity.
     * @param hash Synchronous verifier; never retained.
     * @return Detached state or unknown version/fields, malformed values, boundary or invariant failure.
     * @remarks Initial v1 has no implicit migration. Map/Crowd state and clocks still require joint admission;
     * RTS::restoreNavigation requires the exact provider prepared from this admitted provider state.
     * @thread Pure CPU admission; no simulation, projection, registration or surface publication.
     * @cost Verifies and copies full provider grids/caches per explicit load.
     * @throws std::bad_alloc Input and live worlds remain unchanged.
     */
    [[nodiscard]] static Result<RTSNavigationCapture> parse(SnapshotEnvelope snapshot, PersistentId epoch,
                                                            const SnapshotHashProvider& hash);

private:
    friend class RTS;
    struct Impl;
    explicit RTSNavigationCapture(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::rts
