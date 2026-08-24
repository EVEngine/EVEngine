#include "model3d/ModelData.h"

#include "common/Exception.h"
#include "data/ByteData.h"
#include "image/Image.h"

#include <assimp/anim.h>
#include <assimp/material.h>
#include <assimp/matrix4x4.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <assimp/texture.h>

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace eve {
namespace model3d {

namespace {

aiTextureType textureTypeFromName(const std::string &type) {
    if (type == "base_color") return aiTextureType_BASE_COLOR;
    if (type == "diffuse") return aiTextureType_DIFFUSE;
    if (type == "normals") return aiTextureType_NORMALS;
    if (type == "height") return aiTextureType_HEIGHT;
    if (type == "emissive") return aiTextureType_EMISSIVE;
    if (type == "metalness") return aiTextureType_METALNESS;
    if (type == "roughness") return aiTextureType_DIFFUSE_ROUGHNESS;
    // Assimp maps glTF occlusionTexture to LIGHTMAP.
    if (type == "ambient_occlusion" || type == "lightmap") return aiTextureType_LIGHTMAP;
    if (type == "opacity") return aiTextureType_OPACITY;
    if (type == "specular") return aiTextureType_SPECULAR;
    if (type == "shininess") return aiTextureType_SHININESS;
    return aiTextureType_NONE;
}

aiColor4D materialBaseColor(const aiMaterial *mat) {
    aiColor4D c(1.f, 1.f, 1.f, 1.f);
    if (!mat) return c;
    if (mat->Get(AI_MATKEY_BASE_COLOR, c) != AI_SUCCESS) {
        aiColor3D diffuse(1.f, 1.f, 1.f);
        if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS)
            c = aiColor4D(diffuse.r, diffuse.g, diffuse.b, 1.f);
    }
    return c;
}

}  // namespace

ModelData::ModelData(medialoader::ModelScene scene, std::string uri)
    : Resource(std::move(uri)), scene(std::move(scene)) {}

ModelData::~ModelData() = default;

void ModelData::adopt(eve::Resource &replacement) {
    auto &other = static_cast<ModelData &>(replacement);
    std::swap(scene, other.scene);
}

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

const aiMaterial *ModelData::materialAt(int matIndex) const {
    const aiScene *sc = getScene();
    if (!sc || matIndex < 0 || static_cast<unsigned>(matIndex) >= sc->mNumMaterials)
        return nullptr;
    return sc->mMaterials[matIndex];
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

int ModelData::getMaterialIndex(int meshIndex) const {
    const aiMesh *m = meshAt(meshIndex);
    return m ? static_cast<int>(m->mMaterialIndex) : -1;
}

std::string ModelData::getMaterialName(int matIndex) const {
    const aiMaterial *mat = materialAt(matIndex);
    if (!mat) return {};
    aiString name;
    if (mat->Get(AI_MATKEY_NAME, name) != AI_SUCCESS || name.length == 0)
        return "material" + std::to_string(matIndex);
    return name.C_Str();
}

float ModelData::getMaterialBaseColorR(int matIndex) const {
    return materialBaseColor(materialAt(matIndex)).r;
}

float ModelData::getMaterialBaseColorG(int matIndex) const {
    return materialBaseColor(materialAt(matIndex)).g;
}

float ModelData::getMaterialBaseColorB(int matIndex) const {
    return materialBaseColor(materialAt(matIndex)).b;
}

float ModelData::getMaterialBaseColorA(int matIndex) const {
    return materialBaseColor(materialAt(matIndex)).a;
}

float ModelData::getMaterialMetallicFactor(int matIndex) const {
    const aiMaterial *mat = materialAt(matIndex);
    if (!mat) return 0.f;
    float metallic = 0.f;
    if (mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) != AI_SUCCESS)
        return 0.f;
    return metallic;
}

float ModelData::getMaterialRoughnessFactor(int matIndex) const {
    const aiMaterial *mat = materialAt(matIndex);
    if (!mat) return 0.45f;
    float roughness = 0.45f;
    if (mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) != AI_SUCCESS)
        return 0.45f;
    return roughness;
}

float ModelData::getMaterialOpacity(int matIndex) const {
    const aiMaterial *mat = materialAt(matIndex);
    if (!mat) return 1.f;
    float opacity = 1.f;
    if (mat->Get(AI_MATKEY_OPACITY, opacity) != AI_SUCCESS)
        return 1.f;
    return opacity;
}

bool ModelData::getMaterialTwoSided(int matIndex) const {
    const aiMaterial *mat = materialAt(matIndex);
    if (!mat) return false;
    int twoSided = 0;
    if (mat->Get(AI_MATKEY_TWOSIDED, twoSided) != AI_SUCCESS)
        return false;
    return twoSided != 0;
}

std::string ModelData::getMaterialAlphaMode(int matIndex) const {
    const aiMaterial *mat = materialAt(matIndex);
    if (!mat) return "OPAQUE";
    aiString mode;
    if (mat->Get("$mat.gltf.alphaMode", 0, 0, mode) == AI_SUCCESS) {
        const std::string value = mode.C_Str();
        if (value == "MASK" || value == "BLEND") return value;
    }
    // Legacy formats generally expose opacity but no explicit alpha mode.
    return getMaterialOpacity(matIndex) < 0.999f ? "BLEND" : "OPAQUE";
}

float ModelData::getMaterialAlphaCutoff(int matIndex) const {
    const aiMaterial *mat = materialAt(matIndex);
    if (!mat) return 0.5f;
    float cutoff = 0.5f;
    if (mat->Get("$mat.gltf.alphaCutoff", 0, 0, cutoff) != AI_SUCCESS) return 0.5f;
    return cutoff < 0.f ? 0.f : (cutoff > 1.f ? 1.f : cutoff);
}

int ModelData::getMaterialTextureSlotCount(int matIndex, const std::string &type) const {
    const aiMaterial *mat = materialAt(matIndex);
    if (!mat) return 0;
    const aiTextureType texType = textureTypeFromName(type);
    if (texType == aiTextureType_NONE) return 0;
    return static_cast<int>(mat->GetTextureCount(texType));
}

std::string ModelData::getMaterialTexturePath(int matIndex, const std::string &type,
                                              int slot) const {
    const aiMaterial *mat = materialAt(matIndex);
    if (!mat || slot < 0) return {};
    const aiTextureType texType = textureTypeFromName(type);
    if (texType == aiTextureType_NONE) return {};
    aiString path;
    if (mat->GetTexture(texType, static_cast<unsigned>(slot), &path) != AI_SUCCESS)
        return {};
    return path.C_Str();
}

int ModelData::getMaterialTextureEmbeddedIndex(int matIndex, const std::string &type,
                                               int slot) const {
    const std::string path = getMaterialTexturePath(matIndex, type, slot);
    if (path.empty() || path[0] != '*') return -1;
    return std::atoi(path.c_str() + 1);
}

int ModelData::getEmbeddedTextureCount() const {
    const aiScene *sc = getScene();
    return sc ? static_cast<int>(sc->mNumTextures) : 0;
}

std::string ModelData::getEmbeddedTextureName(int idx) const {
    const aiScene *sc = getScene();
    if (!sc || idx < 0 || static_cast<unsigned>(idx) >= sc->mNumTextures) return {};
    const aiTexture *tex = sc->mTextures[idx];
    if (!tex) return {};
    if (tex->mFilename.length) return tex->mFilename.C_Str();
    return "texture" + std::to_string(idx);
}

int ModelData::getEmbeddedTextureWidth(int idx) const {
    const aiScene *sc = getScene();
    if (!sc || idx < 0 || static_cast<unsigned>(idx) >= sc->mNumTextures) return 0;
    const aiTexture *tex = sc->mTextures[idx];
    return tex ? static_cast<int>(tex->mWidth) : 0;
}

int ModelData::getEmbeddedTextureHeight(int idx) const {
    const aiScene *sc = getScene();
    if (!sc || idx < 0 || static_cast<unsigned>(idx) >= sc->mNumTextures) return 0;
    const aiTexture *tex = sc->mTextures[idx];
    return tex ? static_cast<int>(tex->mHeight) : 0;
}

image::ImageData *ModelData::getEmbeddedTextureImageData(int idx) const {
    const aiScene *sc = getScene();
    if (!sc || idx < 0 || static_cast<unsigned>(idx) >= sc->mNumTextures) return nullptr;
    const aiTexture *tex = sc->mTextures[idx];
    if (!tex || !tex->pcData) return nullptr;
    try {
        if (tex->mHeight == 0) {
            // Compressed blob (PNG/JPEG/...) — decode through the image module.
            eve::data::ByteData bytes(tex->pcData, static_cast<size_t>(tex->mWidth));
            return eve::image::Image::create()->newImageData(&bytes);
        }
        // Uncompressed BGRA8888 texels — convert to RGBA8.
        const unsigned w = tex->mWidth;
        const unsigned h = tex->mHeight;
        std::vector<uint8_t> rgba(size_t(w) * size_t(h) * 4);
        const aiTexel *src = tex->pcData;
        for (unsigned i = 0; i < w * h; ++i) {
            rgba[i * 4 + 0] = src[i].r;
            rgba[i * 4 + 1] = src[i].g;
            rgba[i * 4 + 2] = src[i].b;
            rgba[i * 4 + 3] = src[i].a;
        }
        return new eve::image::ImageData(int(w), int(h), "RGBA8", rgba.data(), false);
    } catch (...) {
        return nullptr;
    }
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
