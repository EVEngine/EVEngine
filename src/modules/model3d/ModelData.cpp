#include "model3d/ModelData.h"

#include "common/Exception.h"

#include <assimp/anim.h>
#include <assimp/matrix4x4.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>

#include <string>

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

int ModelData::getMorphTargetCount(int meshIndex) const {
    const aiMesh *m = meshAt(meshIndex);
    if (!m) return 0;
    return static_cast<int>(m->mNumAnimMeshes);
}

std::string ModelData::getMorphTargetName(int meshIndex, int morphIndex) const {
    const aiMesh *m = meshAt(meshIndex);
    if (!m || morphIndex < 0 || static_cast<unsigned>(morphIndex) >= m->mNumAnimMeshes)
        return {};
    const aiAnimMesh *am = m->mAnimMeshes[morphIndex];
    if (!am) return {};
    if (am->mName.length)
        return am->mName.C_Str();
    return "morph" + std::to_string(morphIndex);
}

bool ModelData::hasBones(int meshIndex) const {
    const aiMesh *m = meshAt(meshIndex);
    if (!m)
        throw eve::Exception("ModelData::hasBones: invalid mesh index");
    return m->HasBones() && m->mNumBones > 0;
}

int ModelData::getBoneCount(int meshIndex) const {
    const aiMesh *m = meshAt(meshIndex);
    if (!m)
        throw eve::Exception("ModelData::getBoneCount: invalid mesh index");
    return static_cast<int>(m->mNumBones);
}

std::string ModelData::getBoneName(int meshIndex, int boneIndex) const {
    const aiMesh *m = meshAt(meshIndex);
    if (!m || boneIndex < 0 || static_cast<unsigned>(boneIndex) >= m->mNumBones)
        return {};
    const aiBone *b = m->mBones[boneIndex];
    if (!b) return {};
    return b->mName.C_Str();
}

float ModelData::getInverseBindMatrixElement(int meshIndex, int boneIndex,
                                             int elementIndex) const {
    const aiMesh *m = meshAt(meshIndex);
    if (!m)
        throw eve::Exception("ModelData::getInverseBindMatrixElement: invalid mesh index");
    if (boneIndex < 0 || static_cast<unsigned>(boneIndex) >= m->mNumBones)
        throw eve::Exception("ModelData::getInverseBindMatrixElement: invalid bone index");
    if (elementIndex < 0 || elementIndex > 15)
        throw eve::Exception("ModelData::getInverseBindMatrixElement: elementIndex must be 0..15");
    const aiBone *b = m->mBones[boneIndex];
    if (!b)
        throw eve::Exception("ModelData::getInverseBindMatrixElement: null bone");
    // Assimp row-major → column-major element.
    const aiMatrix4x4 &mat = b->mOffsetMatrix;
    const float rows[4][4] = {
        {mat.a1, mat.a2, mat.a3, mat.a4},
        {mat.b1, mat.b2, mat.b3, mat.b4},
        {mat.c1, mat.c2, mat.c3, mat.c4},
        {mat.d1, mat.d2, mat.d3, mat.d4},
    };
    const int col = elementIndex / 4;
    const int row = elementIndex % 4;
    return rows[row][col];
}

int ModelData::getBoneWeightCount(int meshIndex, int boneIndex) const {
    const aiMesh *m = meshAt(meshIndex);
    if (!m || boneIndex < 0 || static_cast<unsigned>(boneIndex) >= m->mNumBones)
        return 0;
    const aiBone *b = m->mBones[boneIndex];
    return b ? static_cast<int>(b->mNumWeights) : 0;
}

int ModelData::getBoneWeightVertex(int meshIndex, int boneIndex, int weightIndex) const {
    const aiMesh *m = meshAt(meshIndex);
    if (!m || boneIndex < 0 || static_cast<unsigned>(boneIndex) >= m->mNumBones)
        return -1;
    const aiBone *b = m->mBones[boneIndex];
    if (!b || weightIndex < 0 || static_cast<unsigned>(weightIndex) >= b->mNumWeights)
        return -1;
    return static_cast<int>(b->mWeights[weightIndex].mVertexId);
}

float ModelData::getBoneWeightValue(int meshIndex, int boneIndex, int weightIndex) const {
    const aiMesh *m = meshAt(meshIndex);
    if (!m || boneIndex < 0 || static_cast<unsigned>(boneIndex) >= m->mNumBones)
        return 0.f;
    const aiBone *b = m->mBones[boneIndex];
    if (!b || weightIndex < 0 || static_cast<unsigned>(weightIndex) >= b->mNumWeights)
        return 0.f;
    return b->mWeights[weightIndex].mWeight;
}

int ModelData::getAnimationCount() const {
    const aiScene *sc = getScene();
    return sc ? static_cast<int>(sc->mNumAnimations) : 0;
}

std::string ModelData::getAnimationName(int animIndex) const {
    const aiScene *sc = getScene();
    if (!sc || animIndex < 0 || static_cast<unsigned>(animIndex) >= sc->mNumAnimations)
        return {};
    const aiAnimation *a = sc->mAnimations[animIndex];
    if (!a) return {};
    if (a->mName.length)
        return a->mName.C_Str();
    return "anim" + std::to_string(animIndex);
}

}  // namespace model3d
}  // namespace eve
