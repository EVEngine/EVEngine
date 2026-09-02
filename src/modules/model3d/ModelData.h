#pragma once

#include "common/Resource.h"
#include "common/Result.h"

#include "medialoader/model/ModelScene.h"

#include <memory>
#include <string>
#include <string_view>

struct aiMesh;
struct aiScene;
struct aiMaterial;

namespace eve {
namespace image {
class ImageData;
}
namespace model3d {

/** @brief Owning result of mapping a triangle-local surface point to one UV channel. */
struct SurfaceUv {
    float u = 0.f;
    float v = 0.f;
    float barycentricA = 0.f;
    float barycentricB = 0.f;
    float barycentricC = 0.f;
    int triangleIndex = -1;
    int uvChannel = 0;
};

/**
 * @brief CPU-side decoded 3D model (Assimp scene owned via medialoader::ModelScene).
 * Does not upload to GPU — use graphics::Graphics::newMeshFromAssimp on getMesh().
 */
class ModelData : public Resource {
public:
    explicit ModelData(medialoader::ModelScene scene, std::string uri = "");
    ~ModelData() override;

    bool empty() const;
    int getMeshCount() const;
    int getMaterialCount() const;
    /** @throws eve::Exception when meshIndex is out of range. */
    int getVertexCount(int meshIndex) const;
    /** @throws eve::Exception when meshIndex is out of range. */
    int getFaceCount(int meshIndex) const;
    /** @brief Source position component for one vertex (component 0=x, 1=y, 2=z). */
    float getVertexPosition(int meshIndex, int vertexIndex, int component) const;
    /** @brief Source normal component for one vertex (component 0=x, 1=y, 2=z).
     *  @throws eve::Exception when the mesh/vertex/component is invalid or the mesh has no normals.
     */
    float getVertexNormal(int meshIndex, int vertexIndex, int component) const;
    /**
     * @brief Write one unit (or zero) object-space vertex normal.
     * Allocates the normal stream when the mesh has positions but no normals.
     * @param meshIndex Mesh slot in this ModelData.
     * @param vertexIndex Vertex in that mesh.
     * @param x Object-space normal X.
     * @param y Object-space normal Y.
     * @param z Object-space normal Z. A zero vector is stored as (0,0,1).
     * @return Applied on success. Does not mutate on failure.
     * @ownership Borrowed `this`; no new object is created.
     * @thread Affine to this mutable ModelData. Unsafe on a ResourceManager-shared
     *         file cache entry used by other owners — decode from bytes first.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] eve::Result<void> setVertexNormal(int meshIndex, int vertexIndex, float x, float y,
                                                    float z);
    /**
     * @brief Replace every vertex normal on a mesh from a named procedure.
     * @param meshIndex Mesh slot in this ModelData.
     * @param kind `"radial"`: each normal is the unit vector from the mesh AABB
     *        center to that vertex (outward). Vertices coinciding with the center
     *        receive (0,0,1).
     * @return Applied on success. Does not mutate on failure.
     * @ownership Borrowed `this`.
     * @thread Affine to this mutable ModelData; same cache caveat as setVertexNormal.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] eve::Result<void> applyVertexNormals(int meshIndex, std::string_view kind);
    /**
     * @brief Like applyVertexNormals, but `"radial"` uses an explicit origin.
     * @param meshIndex Mesh slot in this ModelData.
     * @param kind Currently only `"radial"`.
     * @param originX Radial origin X in the mesh local space.
     * @param originY Radial origin Y in the mesh local space.
     * @param originZ Radial origin Z in the mesh local space.
     * @return Applied on success. Does not mutate on failure.
     * @ownership Borrowed `this`.
     * @thread Affine to this mutable ModelData; same cache caveat as setVertexNormal.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] eve::Result<void> applyVertexNormalsFrom(int meshIndex, std::string_view kind,
                                                           float originX, float originY, float originZ);
    /**
     * @brief Rasterize current object-space vertex normals into an RGBA8 normal map.
     * @param meshIndex Mesh slot; faces must be triangles with the given UV channel.
     * @param width Map width in pixels; must be positive.
     * @param height Map height in pixels; must be positive.
     * @param uvChannel UV set to unwrap (usually 0). Image Y is (1-v) (top-left).
     * @param space `"tangent"` (default): encode TBN-space normals for PBR sampling.
     *        `"object"`: encode interpolated object-space normals.
     *        Uncovered texels are tangent (0.5,0.5,1) or object (0.5,0.5,1).
     * @return Owning ImageData on success. Does not mutate this ModelData.
     * @ownership Caller owns the ImageData unique_ptr.
     * @thread Affine to this ModelData; the returned image is independent.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] eve::Result<std::unique_ptr<image::ImageData>> bakeNormalMap(
        int meshIndex, int width, int height, int uvChannel = 0, std::string_view space = "tangent") const;
    /** @brief Source vertex index for one triangle corner (corner 0..2). */
    int getFaceVertexIndex(int meshIndex, int triangleIndex, int corner) const;
    /** @throws eve::Exception when meshIndex is out of range. */
    bool hasNormals(int meshIndex) const;
    /** @throws eve::Exception when meshIndex is out of range. */
    bool hasTexCoords(int meshIndex) const;
    /** @brief Number of populated UV channels (0..AI_MAX_NUMBER_OF_TEXTURECOORDS). */
    int getTexCoordChannelCount(int meshIndex) const;
    /** @brief Whether a mesh has the requested UV channel. */
    bool hasTexCoordChannel(int meshIndex, int channel) const;
    /** @brief UV component in channel for vertex (component 0=u, 1=v, 2=w). */
    float getTexCoord(int meshIndex, int channel, int vertexIndex, int component) const;
    /**
     * @brief Map a point on one source triangle to UV coordinates by barycentric interpolation.
     * @param meshIndex Source mesh index.
     * @param triangleIndex Source triangle index, matching a triangle-mesh collider built from this mesh.
     * @param localX Point X in the model mesh's local coordinate space.
     * @param localY Point Y in the model mesh's local coordinate space.
     * @param localZ Point Z in the model mesh's local coordinate space.
     * @param channel UV channel to sample.
     * @return Owning UV/barycentric snapshot, or a structured failure for invalid or degenerate input.
     * @thread Safe for concurrent calls while this ModelData is not hot-reloaded.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] eve::Result<SurfaceUv> mapSurfacePointToUv(
        int meshIndex, int triangleIndex, float localX, float localY, float localZ,
        int channel = 0) const;
    /** @brief Whether imported tangent and bitangent streams exist. */
    bool hasTangents(int meshIndex) const;
    /** @brief Tangent XYZ component for a vertex. */
    float getTangent(int meshIndex, int vertexIndex, int component) const;
    /** @brief Bitangent XYZ component for a vertex. */
    float getBitangent(int meshIndex, int vertexIndex, int component) const;
    /** @brief Number of populated vertex-color channels. */
    int getVertexColorChannelCount(int meshIndex) const;
    /** @brief Whether a mesh has the requested vertex-color channel. */
    bool hasVertexColorChannel(int meshIndex, int channel) const;
    /** @brief RGBA component (0..3) in a vertex-color channel. */
    float getVertexColor(int meshIndex, int channel, int vertexIndex, int component) const;

    // ---- material accessors ----
    /** @brief Assimp material slot referenced by a mesh; -1 when invalid. */
    int getMaterialIndex(int meshIndex) const;
    std::string getMaterialName(int matIndex) const;
    /** @brief Base color: glTF BASE_COLOR, falling back to OBJ/legacy DIFFUSE. */
    float getMaterialBaseColorR(int matIndex) const;
    float getMaterialBaseColorG(int matIndex) const;
    float getMaterialBaseColorB(int matIndex) const;
    float getMaterialBaseColorA(int matIndex) const;
    /** @brief PBR factors; defaults 0 (metallic) / 0.45 (roughness) when absent. */
    float getMaterialMetallicFactor(int matIndex) const;
    float getMaterialRoughnessFactor(int matIndex) const;
    float getMaterialOpacity(int matIndex) const;
    bool getMaterialTwoSided(int matIndex) const;
    /** @brief glTF alpha mode normalized to "OPAQUE", "MASK", or "BLEND". */
    std::string getMaterialAlphaMode(int matIndex) const;
    float getMaterialAlphaCutoff(int matIndex) const;

    /**
     * @brief Texture type names (Squirrel strings): "base_color", "diffuse",
     * "normals", "height", "emissive", "metalness", "roughness",
     * "ambient_occlusion", "lightmap", "opacity", "specular", "shininess".
     */
    int getMaterialTextureSlotCount(int matIndex, const std::string &type) const;
    /** @brief External file path, or "*N" for an embedded texture. Empty when absent. */
    std::string getMaterialTexturePath(int matIndex, const std::string &type, int slot = 0) const;
    /** @brief Scene texture index for embedded "*N" references; -1 for external files. */
    int getMaterialTextureEmbeddedIndex(int matIndex, const std::string &type, int slot = 0) const;

    // ---- embedded textures (glTF / FBX can embed PNG/JPEG blobs) ----
    int getEmbeddedTextureCount() const;
    std::string getEmbeddedTextureName(int idx) const;
    int getEmbeddedTextureWidth(int idx) const;
    /** @brief 0 means a compressed blob (use getEmbeddedTextureImageData to decode). */
    int getEmbeddedTextureHeight(int idx) const;
    /**
     * Decode an embedded texture to RGBA8 ImageData (caller owns the result).
     * Compressed blobs (PNG/JPEG/...) go through the image module; raw BGRA
     * texels (mHeight > 0) are converted in place. Returns nullptr on failure.
     */
    image::ImageData *getEmbeddedTextureImageData(int idx) const;

    /** @brief Assimp morph / blend-shape targets on a mesh (aiAnimMesh). */
    int getMorphTargetCount(int meshIndex) const;
    std::string getMorphTargetName(int meshIndex, int morphIndex) const;

    /** @brief Assimp skeletal skin data (aiBone / vertex weights) on a mesh. */
    bool hasBones(int meshIndex) const;
    int getBoneCount(int meshIndex) const;
    std::string getBoneName(int meshIndex, int boneIndex) const;
    /** @brief Inverse-bind (offset) matrix element, column-major, elementIndex in [0,15]. */
    float getInverseBindMatrixElement(int meshIndex, int boneIndex, int elementIndex) const;
    int getBoneWeightCount(int meshIndex, int boneIndex) const;
    int getBoneWeightVertex(int meshIndex, int boneIndex, int weightIndex) const;
    float getBoneWeightValue(int meshIndex, int boneIndex, int weightIndex) const;

    /** @brief Scene-level animation clips (aiAnimation). */
    int getAnimationCount() const;
    std::string getAnimationName(int animIndex) const;

    const aiScene *getScene() const;
    /** @brief Raw Assimp mesh at meshIndex; returns nullptr when out of range. */
    const aiMesh *getMesh(int meshIndex) const;

    /** @brief Replace this scene with `replacement`'s (cache reload). */
    void adopt(eve::Resource &replacement) override;

private:
    /** @brief Borrowed Assimp mesh; nullptr when out of range. */
    const aiMesh *meshAt(int meshIndex) const;
    /**
     * @brief Mutable borrowed Assimp mesh for in-place vertex-stream edits.
     * @ownership Borrowed observer into `scene`; nullptr when out of range.
     * @lifetime Valid until adopt() or this ModelData is destroyed.
     * @thread Affine to this ModelData.
     */
    aiMesh *meshAtMutable(int meshIndex);
    const aiMaterial *materialAt(int matIndex) const;

    medialoader::ModelScene scene;
};

}  // namespace model3d
}  // namespace eve
