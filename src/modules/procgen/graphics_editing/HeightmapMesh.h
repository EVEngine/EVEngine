#pragma once

#include "editing/EditingResult.h"

namespace eve::graphics {
class Graphics;
class Mesh;
}
namespace eve::procgen {
class Heightmap;
}

namespace eve::procgen_graphics_editing {

/**
 * @brief Build a heightmap preview mesh through the linked Graphics module.
 * @param heightmap Borrowed source heightmap used only for this call.
 * @param cellSize Horizontal spacing; zero is rejected for smooth normals.
 * @param heightScale Vertical scale.
 * @param smoothNormals Whether to share vertices and derive gradient normals.
 * @return Applied with a newly allocated mesh owned by the caller, or a structured failure.
 * @thread Render-thread only.
 */
[[nodiscard]] editing::Result<graphics::Mesh*> createHeightmapMesh(
    procgen::Heightmap* heightmap, float cellSize, float heightScale, bool smoothNormals);
/**
 * @brief Update preview vertices without replacing the mesh identity.
 * @param mesh Borrowed mesh mutated during this call.
 * @param graphics Borrowed graphics backend owning mesh.
 * @param heightmap Borrowed source heightmap.
 * @param cellSize Horizontal spacing; zero is rejected for smooth normals.
 * @param heightScale Vertical scale.
 * @param smoothNormals Whether to derive shared gradient normals.
 * @return Applied, or a structured validation/backend failure.
 * @thread Render-thread only.
 */
[[nodiscard]] editing::Result<void> updateHeightmapMesh(
    graphics::Mesh* mesh, graphics::Graphics* graphics, procgen::Heightmap* heightmap,
    float cellSize, float heightScale, bool smoothNormals);

}  // namespace eve::procgen_graphics_editing
