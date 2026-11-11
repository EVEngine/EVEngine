#pragma once
/** @file RTSSpawnOccupancy.h
 * @brief rts_spawn module-interface: actual World2D unit footprints for injected spawn placement.
 * @remarks Implements existing occupancy capability; no ECS components, events, scripts, tools or save schema.
 */
#include <memory>
#include "common/Export.h"
#include "spawn/SpawnSpace.h"
namespace eve::rts {
class RTS;
}
namespace eve::rts_spawn {
/** @brief Capture physical RTS unit disks directly from the actual shared domain Table.
 * @ownership Weakly observes the actual Table and RTS lifetime; retains no unit, component or facade pointer.
 * @thread Domain owner thread outside Views, non-reentrant; no callbacks or locks.
 * @remarks Includes all Unit roots in this Table, including other factions/facades and bodies awaiting cleanup.
 * Contained units do not have independent world footprints, matching canonical RTS navigation/Crowd projection.
 * Motion owns position; Unit::Crowd owns the admitted radius. No mutable spatial registry or implicit movement.
 */
class EVENGINE_API_ORCHESTRATION RTSSpawnOccupancy final : public spawn::ISpawnOccupancy {
public:
    /** @brief Observe domain lifetime and its actual Table without extending either lifetime. */
    explicit RTSSpawnOccupancy(rts::RTS& domain);
    /** @brief Release local observations only, never units or Crowd agents. */
    ~RTSSpawnOccupancy() override;
    RTSSpawnOccupancy(const RTSSpawnOccupancy&)            = delete;
    RTSSpawnOccupancy& operator=(const RTSSpawnOccupancy&) = delete;
    /** @brief Copy finite World2D disks from existing components, without materializing missing state.
     * @return Owning positions/radii or expired owner, deferred Table, missing component or invalid geometry error.
     * @ownership Capture remains valid after movement, removal or owner destruction; no pointers escape.
     * @cost Linear in actual Table units, with owning result storage grown per explicit capture; no persistent position
     * cache.
     * @throws std::bad_alloc Result allocation failed without domain or provider mutation.
     */
    [[nodiscard]] Result<std::vector<spawn::OccupiedDisk>> capture() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::rts_spawn
