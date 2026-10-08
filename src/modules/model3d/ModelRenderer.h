#pragma once
#include "common/Export.h"


#include <vector>
#include "common/Result.h"

namespace eve::graphics {
class IResourceFactory;
class Renderable3D;
}

namespace eve::model3d {

class ModelData;

/**
 * Options for assembling Renderable3D entities from a decoded ModelData.
 * Mirrors what SceneLoader does for scene graphs, but self-contained: one
 * Renderable3D per Assimp mesh with material tint / PBR factors / albedo,
 * normal and height textures applied.
 */
/** @brief ModelRenderOptions public API. */
struct ModelRenderOptions {
    bool importAlbedo = true;
    bool importNormalMaps = true;
    bool importHeightMaps = true;
    bool mipmaps = true;
    /**
     * Bake Assimp node world transforms into vertex positions (like the test
     * harness). When false, meshes are uploaded in local space and transforms
     * must be applied by the caller (scene graph).
     */
    bool bakeWorldTransform = true;
};

/**
 * Build a Renderable3D for one Assimp mesh.
 * Missing external preview textures report a warning and retain material factors;
 * this does not relax canonical asset import or archive dependency validation. The mesh index is looked up in the
 * scene graph to bake its node world transform (when bakeWorldTransform is on);
 * if several nodes reference the mesh, the first is used.
 * Returns nullptr for invalid/empty meshes. The entity is registered in the
 * current ECS; the caller keeps it alive by owning a reference in script state.
 */
/** @brief Builds renderable. */
EVENGINE_API_WORLD graphics::Renderable3D *buildRenderable(graphics::IResourceFactory &gfx, ModelData *model,
                                                           int meshIndex, const ModelRenderOptions &options = {});

/** Build one Renderable3D per mesh referenced by the scene graph. */
/** @brief Builds renderables. */
EVENGINE_API_WORLD std::vector<graphics::Renderable3D *> buildRenderables(graphics::IResourceFactory &gfx,
                                                                          ModelData                  *model,
                                                                          const ModelRenderOptions   &options = {});

/**
 * @brief Generate the PBR vegetation-motion rest stream for one imported mesh.
 * @param model Borrowed decoded model used to recover the authored vertex positions and node transform.
 * @param meshIndex Mesh index used by buildRenderable with the default baked-world option.
 * @param renderable Borrowed matching renderable whose graphics-owned Mesh receives the stream.
 * @return Success after atomic stream replacement, or InvalidArgument without changing the mesh.
 * @ownership Retains no pointers and invokes no callbacks. Model, renderable, and mesh remain caller/factory-owned.
 * @thread Render-thread affine because the destination Mesh is mutable render state.
 * @cost O(vertex count) CPU work and nine floats of persistent mesh storage per vertex; call once per prototype.
 */
[[nodiscard]] Result<void> prepareFoliageDeformation(ModelData &model, int meshIndex,
                                                     graphics::Renderable3D &renderable);

}  // namespace eve::model3d
