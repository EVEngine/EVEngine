#pragma once

#include <vector>

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
 * Build a Renderable3D for one Assimp mesh. The mesh index is looked up in the
 * scene graph to bake its node world transform (when bakeWorldTransform is on);
 * if several nodes reference the mesh, the first is used.
 * Returns nullptr for invalid/empty meshes. The entity is registered in the
 * current ECS; the caller keeps it alive by owning a reference in script state.
 */
graphics::Renderable3D *buildRenderable(graphics::IResourceFactory &gfx, ModelData *model, int meshIndex,
                                        const ModelRenderOptions &options = {});

/** Build one Renderable3D per mesh referenced by the scene graph. */
std::vector<graphics::Renderable3D *> buildRenderables(graphics::IResourceFactory &gfx, ModelData *model,
                                                       const ModelRenderOptions &options = {});

}  // namespace eve::model3d
