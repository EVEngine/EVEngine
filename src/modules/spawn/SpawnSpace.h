#pragma once
#include "common/SubjectRef.h"
#include "spawn/SpawnMap.h"

namespace eve::spawn {
/** @brief Owning domain collision projection; geometry is read from its domain owner each query. */
struct OccupiedDisk {
    SubjectRef subject;
    Point      center;
    double     radius = 0;
};
/**
 * @brief Injected read-only occupancy capture, with no secondary mutable position registry.
 * @thread Simulation owner thread; captures are synchronous, pure, non-reentrant and invoke no gameplay callbacks.
 */
class ISpawnOccupancy {
public:
    /** @brief Release the source. */
    virtual ~ISpawnOccupancy() = default;
    /**
     * @brief Capture owning current domain footprints; missing world/projection is an explicit error.
     * @return Owning snapshot, valid independently of subsequent movement or domain destruction.
     * @cost Linear in domain subjects, amortized per planned/finally validated batch.
     */
    [[nodiscard]] virtual Result<std::vector<OccupiedDisk>> capture() const = 0;
};
}  // namespace eve::spawn
