#pragma once

#include "common/Resource.h"

#include "image/ImageData.h"
#include "medialoader/model/ModelScene.h"

#include <string>

struct aiMesh;
struct aiScene;
struct aiMaterial;

namespace eve {
namespace model3d {

/**
 * CPU-side decoded 3D model (Assimp scene owned via medialoader::ModelScene).
 * Does not upload to GPU — use graphics::Graphics::newMeshFromAssimp on getMesh().
 */
class ModelData : public Resource {
public:
    explicit ModelData(medialoader::ModelScene scene, std::string uri = "");
    ~ModelData() override;

    bool empty() const;
    int getMeshCount() const;
    int getMaterialCount() const;
    int getVertexCount(int meshIndex) const;
    int getFaceCount(int meshIndex) const;
    bool hasNormals(int meshIndex) const;
    bool hasTexCoords(int meshIndex) const;

    // ---- material accessors ----
    /** Assimp material slot referenced by a mesh; -1 when invalid. */
    int getMaterialIndex(int meshIndex) const;
    std::string getMaterialName(int matIndex) const;
    /** Base color: glTF BASE_COLOR, falling back to OBJ/legacy DIFFUSE. */
    float getMaterialBaseColorR(int matIndex) const;
    float getMaterialBaseColorG(int matIndex) const;
    float getMaterialBaseColorB(int matIndex) const;
    float getMaterialBaseColorA(int matIndex) const;
    /** PBR factors; defaults 0 (metallic) / 0.45 (roughness) when absent. */
    float getMaterialMetallicFactor(int matIndex) const;
    float getMaterialRoughnessFactor(int matIndex) const;
    float getMaterialOpacity(int matIndex) const;
    bool getMaterialTwoSided(int matIndex) const;

    /**
     * Texture type names (Squirrel strings): "base_color", "diffuse",
     * "normals", "height", "emissive", "metalness", "roughness",
     * "ambient_occlusion", "lightmap", "opacity", "specular", "shininess".
     */
    int getMaterialTextureSlotCount(int matIndex, const std::string &type) const;
    /** External file path, or "*N" for an embedded texture. Empty when absent. */
    std::string getMaterialTexturePath(int matIndex, const std::string &type, int slot = 0) const;
    /** Scene texture index for embedded "*N" references; -1 for external files. */
    int getMaterialTextureEmbeddedIndex(int matIndex, const std::string &type, int slot = 0) const;

    // ---- embedded textures (glTF / FBX can embed PNG/JPEG blobs) ----
    int getEmbeddedTextureCount() const;
    std::string getEmbeddedTextureName(int idx) const;
    int getEmbeddedTextureWidth(int idx) const;
    /** 0 means a compressed blob (use getEmbeddedTextureImageData to decode). */
    int getEmbeddedTextureHeight(int idx) const;
    /**
     * Decode an embedded texture to RGBA8 ImageData (caller owns the result).
     * Compressed blobs (PNG/JPEG/...) go through the image module; raw BGRA
     * texels (mHeight > 0) are converted in place. Returns nullptr on failure.
     */
    image::ImageData *getEmbeddedTextureImageData(int idx) const;

    /** Assimp morph / blend-shape targets on a mesh (aiAnimMesh). */
    int getMorphTargetCount(int meshIndex) const;
    std::string getMorphTargetName(int meshIndex, int morphIndex) const;

    /** Assimp skeletal skin data (aiBone / vertex weights) on a mesh. */
    bool hasBones(int meshIndex) const;
    int getBoneCount(int meshIndex) const;
    std::string getBoneName(int meshIndex, int boneIndex) const;
    /** Inverse-bind (offset) matrix element, column-major, elementIndex in [0,15]. */
    float getInverseBindMatrixElement(int meshIndex, int boneIndex, int elementIndex) const;
    int getBoneWeightCount(int meshIndex, int boneIndex) const;
    int getBoneWeightVertex(int meshIndex, int boneIndex, int weightIndex) const;
    float getBoneWeightValue(int meshIndex, int boneIndex, int weightIndex) const;

    /** Scene-level animation clips (aiAnimation). */
    int getAnimationCount() const;
    std::string getAnimationName(int animIndex) const;

    const aiScene *getScene() const;
    const aiMesh *getMesh(int meshIndex) const;

private:
    const aiMesh *meshAt(int meshIndex) const;
    const aiMaterial *materialAt(int matIndex) const;

    medialoader::ModelScene scene;
};

}  // namespace model3d
}  // namespace eve
