#pragma once
/** @file SpawnRegion.h @brief spawn geometry and transient activation input projection; no runtime mutation. */

#include "spawn/SpawnMap.h"

namespace eve::spawn {
/** @brief Decode a transient World2D player input array using the canonical finite point reader.
 * @return Owning positions or shape/coordinate/input-cap failure, without simulation or provider mutation.
 * @ownership Borrows input synchronously; output owns all points and may outlive the input tree.
 * @thread Any thread with immutable input; no callbacks, locks, ECS or service access.
 * @remarks An empty array is a known-empty frame, not an absent capability. Duplicated player positions are valid.
 * This is runtime input projection, not a new persistent schema. The caller supplies its actual input budget.
 * @cost Linear in points with bounded allocation after count admission, per explicit frame preparation.
 * @throws std::bad_alloc Allocation failed without observable simulation mutation.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::vector<Point>> decodePlayerFrame(const Value& input,
                                                                                   std::size_t  maxPlayers);
/** @brief Build the canonical eve.map-spawns/v1 region source fragment from typed candidate geometry.
 * @return Owning Value builder output; validity is admitted only by compileMap, not by this structural encoding.
 * @ownership Copies vertices/points; retains no input references. Allocation failure leaves input untouched.
 * @thread Calling content/authoring thread; reentrant, no callbacks or runtime publication.
 * @cost Linear in vertices/points per candidate build; constant for circles. Batch pointer samples before building.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Value buildRegionSource(const Region& region);
/**
 * @brief Measure shortest distance to a compiled region; interior and boundary have distance zero.
 * @return Owning finite distance or InvalidArgument for an invalid index/point or unrepresentable result.
 * @ownership Borrows map only during this call; retains no data and mutates no state.
 * @thread Any thread with immutable map access; reentrant, no callbacks.
 * @cost Linear in region vertices/points per query; constant for circles.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<double> distanceToRegion(const CompiledMap& map, std::size_t instance,
                                                                      Point point);
/**
 * @brief Produce one bounded candidate from independent uniform draws in [0,1).
 * @return Candidate or normal empty rejection outside a concave polygon; invalid inputs are errors.
 * @remarks Points use the first draw; disks use area-uniform polar sampling; polygons use bounding-box
 * rejection. Every call consumes one caller-owned attempt, including rejection. No internal retry or RNG.
 * Floating geometry is tolerance-bounded; authoritative replay records final positions.
 * @ownership Borrows map only during this call; returned point is owned.
 * @thread Any thread with immutable map access; reentrant, no callbacks or mutable globals.
 * @cost Linear in polygon vertices per attempt, constant for points/circles.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<std::optional<Point>> sampleRegion(const CompiledMap& map,
                                                                                std::size_t instance, double u,
                                                                                double v);
}  // namespace eve::spawn
