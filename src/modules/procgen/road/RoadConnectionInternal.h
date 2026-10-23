#pragma once
#include "procgen/road/RoadNetwork.h"
namespace eve::procgen {
class MeshBuild;
}
namespace eve::procgen::road {
struct RoadBakeOptions;
}

namespace eve::procgen::road::detail {
/** @brief Validate explicit one-to-one routes, compatible sections and live incident endpoint ownership. */
[[nodiscard]] Result<void> validateFacilityRoutes(const RoadNetwork& network, const RoadNode& node);
/** @brief Validate the owned asset, local frames, indexed topology and exact mesh boundary seams. */
[[nodiscard]] Result<void> validateConnectionAsset(const RoadConnection& connection);
/** @brief Validate node-local edge bindings against the sole authoritative graph. */
[[nodiscard]] Result<void> validateConnectionBindings(const RoadNetwork& network, const RoadNode& node);
/** @brief Validate actual baked spline sockets against every full custom asset seam. */
[[nodiscard]] Result<void> validateConnectionSeams(const RoadNetwork& network, const RoadNode& node);
/** @brief Return deterministic arc-distance to the bound custom socket (zero when absent). */
[[nodiscard]] float connectionSocketDistance(const RoadNode& node, const RoadEdge& edge, float length);
/** @brief Append the validated custom connection mesh with its node transform. */
[[nodiscard]] Result<void> bakeCustomConnection(MeshBuild& mesh, const RoadNetwork& network, const RoadNode& node,
                                                const RoadBakeOptions& options);
}  // namespace eve::procgen::road::detail
