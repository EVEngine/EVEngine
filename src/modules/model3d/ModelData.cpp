#include "model3d/ModelData.h"

#include "common/Exception.h"

#include <assimp/mesh.h>
#include <assimp/scene.h>

namespace eve {
namespace model3d {

ModelData::ModelData(medialoader::ModelScene scene, std::string uri)
    : Resource(std::move(uri)), scene(std::move(scene)) {}

ModelData::~ModelData() = default;

bool ModelData::empty() const { return scene.empty(); }

const aiScene *ModelData::getScene() const {
    if (scene.empty())
        return nullptr;
    return scene.get();
}

const aiMesh *ModelData::meshAt(int meshIndex) const {
    const aiScene *sc = getScene();
    if (!sc || meshIndex < 0 || static_cast<unsigned>(meshIndex) >= sc->mNumMeshes)
        return nullptr;
    return sc->mMeshes[meshIndex];
}

const aiMesh *ModelData::getMesh(int meshIndex) const { return meshAt(meshIndex); }

int ModelData::getMeshCount() const {
    const aiScene *sc = getScene();
    return sc ? static_cast<int>(sc->mNumMeshes) : 0;
}

int ModelData::getMaterialCount() const {
    const aiScene *sc = getScene();
    return sc ? static_cast<int>(sc->mNumMaterials) : 0;
}

int ModelData::getVertexCount(int meshIndex) const {
    const aiMesh *m = meshAt(meshIndex);
    if (!m)
        throw eve::Exception("ModelData::getVertexCount: invalid mesh index");
    return static_cast<int>(m->mNumVertices);
}

int ModelData::getFaceCount(int meshIndex) const {
    const aiMesh *m = meshAt(meshIndex);
    if (!m)
        throw eve::Exception("ModelData::getFaceCount: invalid mesh index");
    return static_cast<int>(m->mNumFaces);
}

bool ModelData::hasNormals(int meshIndex) const {
    const aiMesh *m = meshAt(meshIndex);
    if (!m)
        throw eve::Exception("ModelData::hasNormals: invalid mesh index");
    return m->HasNormals();
}

bool ModelData::hasTexCoords(int meshIndex) const {
    const aiMesh *m = meshAt(meshIndex);
    if (!m)
        throw eve::Exception("ModelData::hasTexCoords: invalid mesh index");
    return m->HasTextureCoords(0);
}

}  // namespace model3d
}  // namespace eve
