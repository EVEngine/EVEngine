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

    const aiScene *getScene() const;
    const aiMesh *getMesh(int meshIndex) const;

private:
    const aiMesh *meshAt(int meshIndex) const;

    medialoader::ModelScene scene;
};

}  // namespace model3d
}  // namespace eve
