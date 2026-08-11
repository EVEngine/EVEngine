#pragma once

#include "common/Resource.h"

#include "medialoader/model/ModelScene.h"

#include <string>

struct aiMesh;
struct aiScene;

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

    medialoader::ModelScene scene;
};

}  // namespace model3d
}  // namespace eve
