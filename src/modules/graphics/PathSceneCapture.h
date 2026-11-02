#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "graphics/PathTracing.h"

namespace ecs {
class Table;
}
namespace eve::graphics {
class Graphics;
class IRayTracing;

/** @brief Prepare an unculled path scene from the supplied ECS world.
 * @param world Borrowed table; visible Renderable3D descendants and enabled Light3D descendants are read.
 * @param graphics Borrowed device; must remain alive until provider detachment.
 * @param provider Borrowed path provider; receives a copied snapshot only after collection succeeds.
 * @return Prepared identity, or a structured error preserving the previous provider scene.
 * @ownership No entity, component, mesh, material or source texture pointer survives the synchronous call.
 * @thread Render thread and table owner thread; no callbacks, mutations or reentrancy.
 * @cost Traverses and snapshots world renderables; the provider compares values and reuses
 * unchanged GPU scenes, updates TLAS for instance-only changes, or uploads/builds changed geometry.
 * Changed native texture contents are copied on the GPU; unchanged identities reuse the scene snapshot.
 * @details Uses highest-detail meshes without camera/frustum/distance culling.
 * Equal mesh and material values share one geometry/BLAS, with independent instance transforms.
 * Unsupported material, custom instance and light features fail explicitly rather than disappear.
 * Point strength is interpreted as radiant intensity; raster radius cutoffs are not applied.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<PathSceneToken> preparePathWorld(ecs::Table& world, Graphics& graphics,
                                                                            IRayTracing& provider);
}  // namespace eve::graphics
