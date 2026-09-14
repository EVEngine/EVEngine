#include "model3d/ModelRenderer.h"

#include "model3d/ModelData.h"

#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "graphics/IResourceFactory.h"
#include "graphics/Material.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "image/Image.h"

#include <assimp/GltfMaterial.h>
#include <assimp/material.h>
#include <assimp/matrix4x4.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "common/Exception.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::model3d {
namespace {

using eve::graphics::IResourceFactory;
using eve::graphics::Mesh;
using eve::graphics::Material;
using eve::graphics::Renderable3D;
using eve::graphics::Texture;

Texture *textureFromImageData(IResourceFactory *gfx, image::ImageData *img) {
    if (!img) return nullptr;
    try {
        return gfx->newTexture(img);
    } catch (...) {
        return nullptr;
    }
}

Texture *loadEmbeddedTexture(IResourceFactory *gfx, ModelData *model, int idx) {
    image::ImageData *img = model->getEmbeddedTextureImageData(idx);
    Texture *tex = textureFromImageData(gfx, img);
    delete img;
    return tex;
}

std::string basenameOf(const std::string &path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

Texture *loadExternalTexture(IResourceFactory *gfx, const std::string &path) {
    auto *fs = filesystem::Filesystem::create();
    std::unique_ptr<filesystem::FileData> fd;
    try {
        fd.reset(fs->read(path));
    } catch (...) {
    }
    if (!fd || fd->getSize() == 0) {
        const std::string base = basenameOf(path);
        try {
            fd.reset(fs->read(base));
        } catch (...) {
        }
        if (!fd || fd->getSize() == 0) {
            try {
                fd.reset(fs->read("Textures/" + base));
            } catch (...) {
            }
        }
    }
    if (!fd || fd->getSize() == 0) return nullptr;
    try {
        image::ImageData *img = image::Image::create()->newImageData(fd.get());
        Texture *tex = textureFromImageData(gfx, img);
        delete img;
        return tex;
    } catch (...) {
        return nullptr;
    }
}

Texture* loadModelTexture(IResourceFactory* gfx, ModelData* model, const std::string& path) {
    // Imported texture paths are relative to the source model, not the VFS root.
    // Normalize parent segments (OBJ/MTL commonly uses ../textures/foo.png).
    std::string uri = model->getUri();
    if (uri.rfind("file://", 0) == 0) uri.erase(0, 7);
    // ResourceManager cache keys are normalized VFS paths without a scheme.
    if (!uri.empty() && uri.find("://") == std::string::npos) {
        uri                  = uri.substr(0, uri.find('?'));
        std::string relative = path;
        std::replace(relative.begin(), relative.end(), '\\', '/');
        const auto resolved = (std::filesystem::path(uri).parent_path() / relative).lexically_normal();
        if (auto* texture = loadExternalTexture(gfx, resolved.generic_string())) return texture;
    }
    return loadExternalTexture(gfx, path);
}

Texture* loadTextureSlot(IResourceFactory* gfx, ModelData* model, int matIndex, const std::string& type) {
    if (model->getMaterialTextureSlotCount(matIndex, type) <= 0) return nullptr;
    const int embedded = model->getMaterialTextureEmbeddedIndex(matIndex, type, 0);
    if (embedded >= 0) return loadEmbeddedTexture(gfx, model, embedded);
    const std::string path = model->getMaterialTexturePath(matIndex, type, 0);
    if (path.empty()) return nullptr;
    return loadModelTexture(gfx, model, path);
}

struct TextureLook {
    Texture*    albedo = nullptr;
    Texture*    normal = nullptr;
    Texture *height = nullptr;
    float tr = 1.f, tg = 1.f, tb = 1.f, ta = 1.f;
    float metallic = 0.f;
    float roughness = 0.45f;
    std::string alphaMode = "OPAQUE";
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
    bool                 extendedPbr = false;
    graphics::PbrSurface pbr;
};

void extendedLook(IResourceFactory* gfx, ModelData* model, int index, TextureLook& look,
                  const ModelRenderOptions& options) {
    const auto* scene = model->getScene();
    if (!scene || index < 0 || unsigned(index) >= scene->mNumMaterials) return;
    const auto* material = scene->mMaterials[index];
    aiString    alpha;
    if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, alpha) != AI_SUCCESS) return;
    look.extendedPbr = true;
    look.ta          = model->getMaterialBaseColorA(index);
    auto& p          = look.pbr;
    auto  scalar     = [&](const char* key, unsigned type, unsigned slot, float fallback) {
        float value = fallback;
        material->Get(key, type, slot, value);
        if (!std::isfinite(value)) throw eve::Exception("nonfinite imported material factor %s", key);
        return value;
    };
    auto unit = [&](const char* key, unsigned type, unsigned slot, float fallback) {
        float value = scalar(key, type, slot, fallback);
        if (value < 0 || value > 1)
            std::fprintf(stderr, "[model3d] warning %s=%g clamped to [0,1]\n", key, double(value));
        return std::clamp(value, 0.f, 1.f);
    };
    p.specularFactor = unit(AI_MATKEY_SPECULAR_FACTOR, 1);
    aiColor3D color(1, 1, 1);
    material->Get(AI_MATKEY_COLOR_SPECULAR, color);
    p.specularColor = {color.r, color.g, color.b};
    color           = {0, 0, 0};
    material->Get(AI_MATKEY_COLOR_EMISSIVE, color);
    p.emissive             = {color.r, color.g, color.b};
    p.emissiveStrength     = scalar(AI_MATKEY_EMISSIVE_INTENSITY, 1);
    p.ior                  = scalar(AI_MATKEY_REFRACTI, 1.5f);
    p.clearcoatFactor      = unit(AI_MATKEY_CLEARCOAT_FACTOR, 0);
    p.clearcoatRoughness   = unit(AI_MATKEY_CLEARCOAT_ROUGHNESS_FACTOR, 0);
    p.anisotropyStrength   = unit(AI_MATKEY_ANISOTROPY_FACTOR, 0);
    p.normalScale          = scalar(AI_MATKEY_GLTF_TEXTURE_SCALE(aiTextureType_NORMALS, 0), 1);
    p.clearcoatNormalScale = scalar(AI_MATKEY_GLTF_TEXTURE_SCALE(aiTextureType_CLEARCOAT, 2), 1);
    p.occlusionStrength    = unit("$tex.file.strength", aiTextureType_LIGHTMAP, 0, 1);
    int shading            = 0;
    material->Get(AI_MATKEY_SHADING_MODEL, shading);
    p.unlit                                          = shading == aiShadingMode_Unlit;
    const std::pair<aiTextureType, unsigned> roles[] = {
        {aiTextureType_BASE_COLOR, 0}, {aiTextureType_UNKNOWN, 0},  {aiTextureType_NORMALS, 0},
        {aiTextureType_LIGHTMAP, 0},   {aiTextureType_EMISSIVE, 0}, {aiTextureType_SPECULAR, 0},
        {aiTextureType_SPECULAR, 1},   {aiTextureType_NONE, 0},     {aiTextureType_CLEARCOAT, 0},
        {aiTextureType_CLEARCOAT, 1},  {aiTextureType_CLEARCOAT, 2}};
    for (size_t i = 0; i < 11; i++) {
        if ((i == 0 && !options.importAlbedo) || ((i == 2 || i == 10) && !options.importNormalMaps)) continue;
        const auto [type, slot] = roles[i];
        if (type == aiTextureType_NONE) continue;
        aiString         path;
        unsigned         uv      = 0;
        aiTextureMapMode wrap[2] = {aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};
        if (material->GetTexture(type, slot, &path, nullptr, &uv, nullptr, nullptr, wrap) != AI_SUCCESS) continue;
        auto& binding = p.textures[i];
        if (i == 0 && look.albedo)
            binding.texture = look.albedo;
        else if (i == 2 && look.normal)
            binding.texture = look.normal;
        else if (path.length > 1 && path.C_Str()[0] == '*')
            binding.texture = loadEmbeddedTexture(gfx, model, std::stoi(path.C_Str() + 1));
        else
            binding.texture = loadModelTexture(gfx, model, path.C_Str());
        if (!binding.texture) {
            // Model3D preview preserves the legacy factor-only material when an
            // external resource is absent. Canonical asset admission remains strict.
            std::fprintf(stderr, "[model3d] warning: missing material texture %s (model %s); using material factors\n",
                         path.C_Str(), model->getUri().c_str());
            continue;
        }
        binding.texcoord = uv;
        auto wrapEnum    = [](aiTextureMapMode mode) {
            return mode == aiTextureMapMode_Clamp ? 33071u : mode == aiTextureMapMode_Mirror ? 33648u : 10497u;
        };
        binding.wrapS = wrapEnum(wrap[0]);
        binding.wrapT = wrapEnum(wrap[1]);
        int filter    = 9987;
        material->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MIN(type, slot), filter);
        binding.minFilter = filter;
        filter            = 9729;
        material->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MAG(type, slot), filter);
        binding.magFilter = filter;
        aiUVTransform transform;
        if (material->Get(AI_MATKEY_UVTRANSFORM(type, slot), transform) == AI_SUCCESS) {
            // First undo the optional postprocess, then Assimp's glTF origin/center conversion.
            if (model->hasFlippedUvs()) {
                transform.mRotation      = -transform.mRotation;
                transform.mTranslation.y = -transform.mTranslation.y;
            }
            const float r = -transform.mRotation, c = std::cos(r), sn = std::sin(r);
            const float sx = transform.mScaling.x, sy = transform.mScaling.y;
            binding.rotation = r;
            binding.scale    = {sx, sy};
            binding.offset   = {transform.mTranslation.x - .5f * sx * (-c + sn + 1),
                                1 - sy - transform.mTranslation.y + .5f * sy * (sn + c - 1)};
        }
    }
    look.albedo = p.textures[0].texture;
    look.normal = p.textures[2].texture;
}

TextureLook materialLook(IResourceFactory *gfx, ModelData *model, int matIndex, const ModelRenderOptions &options,
                         std::unordered_map<int, TextureLook> &cache) {
    auto it = cache.find(matIndex);
    if (it != cache.end()) return it->second;

    TextureLook look;
    if (matIndex >= 0) {
        look.tr = model->getMaterialBaseColorR(matIndex);
        look.tg = model->getMaterialBaseColorG(matIndex);
        look.tb = model->getMaterialBaseColorB(matIndex);
        look.ta = model->getMaterialBaseColorA(matIndex);
        look.metallic = model->getMaterialMetallicFactor(matIndex);
        look.roughness = model->getMaterialRoughnessFactor(matIndex);
        look.ta *= model->getMaterialOpacity(matIndex);
        look.alphaMode = model->getMaterialAlphaMode(matIndex);
        look.alphaCutoff = model->getMaterialAlphaCutoff(matIndex);
        look.doubleSided = model->getMaterialTwoSided(matIndex);
        if (options.importAlbedo) {
            look.albedo = loadTextureSlot(gfx, model, matIndex, "base_color");
            if (!look.albedo) look.albedo = loadTextureSlot(gfx, model, matIndex, "diffuse");
        }
        if (options.importNormalMaps)
            look.normal = loadTextureSlot(gfx, model, matIndex, "normals");
        if (options.importHeightMaps)
            look.height = loadTextureSlot(gfx, model, matIndex, "height");
    }
    extendedLook(gfx, model, matIndex, look, options);
    cache.emplace(matIndex, look);
    return look;
}

Renderable3D *makeRenderable(IResourceFactory *gfx, ModelData *model, int meshIndex, const aiMatrix4x4 &world,
                             const ModelRenderOptions &options, std::unordered_map<int, TextureLook> &cache) {
    const aiScene *scene = model->getScene();
    if (!scene || meshIndex < 0 || static_cast<unsigned>(meshIndex) >= scene->mNumMeshes)
        return nullptr;
    const aiMesh *ai = scene->mMeshes[meshIndex];
    if (!ai || ai->mNumVertices == 0 || ai->mNumFaces == 0) return nullptr;

    // Skinned vertex positions and inverse-bind matrices share the model's
    // bind-pose space. Baking the owning node transform into only the vertices
    // makes the skin palette apply that transform a second time, separating
    // modular body parts (notably KayKit heads, armour and limbs). Static
    // meshes still use the established baked scene transform.
    const bool bakeWorld = options.bakeWorldTransform && !ai->HasBones();
    Mesh *mesh = bakeWorld ? gfx->newMeshFromAssimp(*ai, world) : gfx->newMeshFromAssimp(*ai);
    if (!mesh) return nullptr;

    const int matIndex = model->getMaterialIndex(meshIndex);
    const TextureLook look = materialLook(gfx, model, matIndex, options, cache);

    Renderable3D *ent = Renderable3D::create();
    ent->meshRenderer()->visible = true;
    ent->setMesh(mesh);
    // Preserve the established Renderable3D inspection API. The Material below
    // is authoritative for drawing, while these mirrored fields keep imported
    // models compatible with callers that query meshRenderer()/getTexture().
    if (look.albedo) ent->setTexture(look.albedo);
    if (look.normal) ent->setNormalTexture(look.normal);
    if (look.height) ent->setHeightTexture(look.height);
    ent->setTint(look.tr, look.tg, look.tb, look.ta);
    ent->setMetallic(look.metallic);
    ent->setRoughness(look.roughness);
    // Keep imported surface semantics in one Material instead of losing glTF
    // alphaMode/alphaCutoff on the legacy renderer fields.
    Material *material = new Material();
    material->setAlbedoTexture(look.albedo);
    material->setNormalTexture(look.normal);
    material->setHeightTexture(look.height);
    material->setTint(look.tr, look.tg, look.tb, look.ta);
    material->setMetallic(look.metallic);
    material->setRoughness(look.roughness);
    material->setDoubleSided(look.doubleSided);
    material->setAlphaCutoff(look.alphaCutoff);
    if (look.alphaMode == "MASK")
        material->setSurfaceMode("masked");
    else if (look.alphaMode == "BLEND")
        material->setSurfaceMode("transparent");
    else
        material->setSurfaceMode("opaque");
    if (look.extendedPbr) {
        auto configured = material->setPbrSurface(look.pbr);
        if (!configured) throw eve::Exception("%s", configured.error()->message().c_str());
        for (unsigned channel = 0; channel < AI_MAX_NUMBER_OF_TEXTURECOORDS; channel++) {
            if (!ai->mTextureCoords[channel]) continue;
            std::vector<float> values;
            values.reserve(size_t(ai->mNumVertices) * 2);
            for (unsigned v = 0; v < ai->mNumVertices; v++) {
                values.push_back(ai->mTextureCoords[channel][v].x);
                values.push_back(model->hasFlippedUvs() ? ai->mTextureCoords[channel][v].y
                                                        : 1 - ai->mTextureCoords[channel][v].y);
            }
            auto attached = mesh->setTexcoordSet(channel, values);
            if (!attached) throw eve::Exception("%s", attached.error()->message().c_str());
        }
    }
    ent->setMaterial(material);
    return ent;
}

void walkNodes(const aiNode *node, const aiMatrix4x4 &parent, IResourceFactory *gfx, ModelData *model,
               const ModelRenderOptions &options, std::vector<Renderable3D *> &out,
               std::unordered_map<int, TextureLook> &cache) {
    if (!node) return;
    const aiMatrix4x4 world = parent * node->mTransformation;
    for (unsigned i = 0; i < node->mNumMeshes; ++i) {
        Renderable3D *ent =
            makeRenderable(gfx, model, static_cast<int>(node->mMeshes[i]), world, options, cache);
        if (ent) out.push_back(ent);
    }
    for (unsigned c = 0; c < node->mNumChildren; ++c)
        walkNodes(node->mChildren[c], world, gfx, model, options, out, cache);
}

bool findMeshTransform(const aiNode *node, const aiMatrix4x4 &parent, unsigned meshIndex,
                       aiMatrix4x4 &out) {
    if (!node) return false;
    const aiMatrix4x4 world = parent * node->mTransformation;
    for (unsigned i = 0; i < node->mNumMeshes; ++i) {
        if (node->mMeshes[i] == meshIndex) {
            out = world;
            return true;
        }
    }
    for (unsigned c = 0; c < node->mNumChildren; ++c)
        if (findMeshTransform(node->mChildren[c], world, meshIndex, out)) return true;
    return false;
}

}  // namespace

Renderable3D *buildRenderable(IResourceFactory &gfx, ModelData *model, int meshIndex,
                              const ModelRenderOptions &options) {
    if (!model) return nullptr;
    const aiScene *scene = model->getScene();
    if (!scene || !scene->mRootNode) return nullptr;

    aiMatrix4x4 world;
    if (!findMeshTransform(scene->mRootNode, aiMatrix4x4(), static_cast<unsigned>(meshIndex),
                           world)) {
        world = aiMatrix4x4();
    }
    std::unordered_map<int, TextureLook> cache;
    return makeRenderable(&gfx, model, meshIndex, world, options, cache);
}

std::vector<Renderable3D *> buildRenderables(IResourceFactory &gfx, ModelData *model,
                                             const ModelRenderOptions &options) {
    std::vector<Renderable3D *> out;
    if (!model) return out;
    const aiScene *scene = model->getScene();
    if (!scene || !scene->mRootNode) return out;
    std::unordered_map<int, TextureLook> cache;
    walkNodes(scene->mRootNode, aiMatrix4x4(), &gfx, model, options, out, cache);
    return out;
}

}  // namespace eve::model3d
