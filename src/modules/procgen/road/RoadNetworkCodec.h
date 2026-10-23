#pragma once
#include "common/Value.h"
#include "procgen/Params.h"
#include "procgen/road/RoadNetwork.h"
namespace eve::procgen::road {
/** @brief Capture validated, owning author data using the editor-compatible roadNetwork schema v1.
 * @ownership The returned value owns all records; no borrowed buffers or derived meshes.
 * @thread Caller synchronizes concurrent edits. No callbacks.
 * @cost Linear in author records and prefab source geometry; reuse the snapshot across consumers.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<eve::Value> captureRoadNetwork(const RoadNetwork& network);
/** @brief Decode schema v1 privately; unknown fields are ignored and unsupported versions rejected.
 * @return An independent validated network; failures never mutate an observable author owner.
 * @thread Caller thread; no callbacks.
 * @cost Linear in author data plus topology and seam validation; decode once per import.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<RoadNetwork> restoreRoadNetwork(const eve::Value& snapshot);
/** @brief Store canonical validated author data in the standard recipe's networkSnapshot parameter.
 * @ownership Params takes an owning canonical JSON copy, published only on success.
 * @thread Caller synchronizes Params and the network; no callbacks.
 * @cost Linear in the author graph/source meshes; prepare once and reuse for mesh/artifact/LOD builds.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<void> setRoadRecipeNetwork(Params& params, const RoadNetwork& network);
/** @brief Resolve the recipe's author snapshot, or its procedural scene when no snapshot was supplied.
 * @return An owning validated network. Invalid supplied snapshots fail instead of selecting a scene.
 * @thread Caller thread; no callbacks.
 * @cost Linear in author data and seam validation, or procedural scene construction; reused by recipe projections.
 */
[[nodiscard]] EVENGINE_API_DOMAINS Result<RoadNetwork> resolveRoadRecipeNetwork(const Params& params);
}  // namespace eve::procgen::road
