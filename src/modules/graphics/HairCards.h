#pragma once

#include <cstdint>
#include <vector>

namespace eve::graphics {

class Graphics;
class Material;
class Mesh;
class Renderable3D;
class Shader;
class Texture;

/**
 * @brief Procedural hair/fur card meshes + material/LOD helpers (no strand simulation).
 *
 * Cards are oriented quads suitable for the built-in hair transparent pass.
 * Strand flow is along +V (card height); the hair shader derives tangents from
 * that convention unless `strandDir` is overridden.
 */
namespace hair {

/** @brief CPU-side interleaved mesh arrays for upload via Graphics::newMeshFromArrays. */
struct CardMeshData {
    std::vector<float> positions;  ///< xyz per vertex
    std::vector<float> normals;    ///< xyz per vertex
    std::vector<float> uvs;        ///< uv per vertex
    std::vector<uint32_t> indices;
};

/** @brief One root sample expanded into a card. */
struct CardRoot {
    float x = 0.f, y = 0.f, z = 0.f;
    float dirX = 0.f, dirY = 1.f, dirZ = 0.f;
    float width = 0.08f;
    float length = 0.35f;
};

/**
 * @brief Build a single upright card in local space (origin at root, height along +Y, face +Z).
 * @param width Card width.
 * @param height Card length along the strand axis.
 */
CardMeshData buildCard(float width = 0.12f, float height = 0.45f);

/**
 * @brief Build one card per polyline segment, facing a camera-facing side approximated by world up.
 * @param pointsXYZ Interleaved xyz, length >= 2.
 * @param pointCount Number of points.
 * @param width Card width.
 */
CardMeshData buildCardsAlongPolyline(const float *pointsXYZ, int pointCount, float width = 0.08f);

/**
 * @brief Expand root samples into independent cards.
 */
CardMeshData buildCardsFromRoots(const CardRoot *roots, int rootCount);

/** @brief Append `src` into `dst` (vertex/index rebase). */
void appendCardMesh(CardMeshData &dst, const CardMeshData &src);

/**
 * @brief Upload card arrays as a Graphics-owned Mesh.
 * @return Mesh pointer owned by Graphics, or null on failure.
 */
Mesh *uploadCardMesh(Graphics *gfx, const CardMeshData &data);

/**
 * @brief Convenience: single card mesh.
 * @ownership Graphics owns the returned Mesh.
 */
Mesh *newCardMesh(Graphics *gfx, float width = 0.12f, float height = 0.45f);

/**
 * @brief Apply transparent hair-card surface defaults (double-sided, no depth write/shadows).
 * Does not create or assign a shader.
 */
void applyCardDefaults(Material &mat);

/**
 * @brief Create a Material configured for hair cards and optionally bind the hair shader + albedo.
 * @ownership Caller owns the Material*; Graphics owns shader/texture resources.
 */
Material *makeCardMaterial(Graphics *gfx, Texture *albedo = nullptr, Shader *hairShader = nullptr);

/**
 * @brief Wire near/far geometric LOD on a renderable (index 0 = cards, index 1 = proxy).
 * @param switchDistance Camera distance that selects the far mesh.
 */
void configureCardLod(Renderable3D *renderable, Mesh *nearCards, Mesh *farProxy,
                      float switchDistance);

}  // namespace hair

}  // namespace eve::graphics
