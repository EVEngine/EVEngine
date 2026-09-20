#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

namespace eve::procgen {

/**
 * @brief Build a small accent flower (stem + radial petals + centre).
 *
 * Registered as the `mesh.flower` recipe for grassland / bush scatter.
 * Generation is deterministic from `params` seed and has no file/resource
 * dependency.
 *
 * @param params Generation parameters borrowed for this call only.
 * @param out Caller-owned mesh cleared and filled on success.
 * @return Success, or a structured Failed diagnostic when the mesh is empty.
 * @thread Safe; the function mutates no shared state beyond `out`.
 * @reentrancy Does not invoke external callbacks.
 */
[[nodiscard]] EVENGINE_API_DOMAINS eve::Result<void> generateFlowerMesh(const Params &params, MeshBuild &out);

}  // namespace eve::procgen
