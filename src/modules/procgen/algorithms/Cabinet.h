#pragma once

#include "common/Export.h"
#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

namespace eve::procgen {
class MeshRecipeRegistry;

/**
 * @brief Build a deterministic, Y-up cabinet with its base at Y=0 and front toward +Z.
 * @param params Borrowed synchronous input; see cabinet.md for dimensions and layout rules.
 * @return Owning mesh or InvalidArgument; no shared state or partial output is published.
 * Named panel/trim/hardware/interior groups carry `material.<group>` role metadata;
 * no texture resources are loaded. Doors use vertical grain UVs; drawer fronts use horizontal grain UVs.
 * @thread Safe for concurrent calls with independent inputs. No callbacks or resources.
 * @cost Linear in compartments, levels, doors and drawers; build once and reuse the mesh.
 */
[[nodiscard]] EVENGINE_API_DOMAINS eve::Result<MeshBuild> generateCabinet(const Params& params);

/** @brief Register mesh.cabinet and its editor schema during single-threaded initialization.
 * @param registry Borrowed registry; retains its own descriptor and callback.
 */
void registerCabinetRecipe(MeshRecipeRegistry& registry);
}  // namespace eve::procgen
