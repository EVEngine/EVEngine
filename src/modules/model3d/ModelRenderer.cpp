#include "model3d/ModelRenderer.h"

#include "model3d/ModelData.h"

#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "graphics/IResourceFactory.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "image/Image.h"

#include <assimp/matrix4x4.h>
#include <assimp/mesh.h>
#include <assimp/scene.h>

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

Texture *loadTextureSlot(IResourceFactory *gfx, ModelData *model, int matIndex, const std::string &type) {
    if (model->getMaterialTextureSlotCount(matIndex, type) <= 0) return nullptr;
    const int embedded = model->getMaterialTextureEmbeddedIndex(matIndex, type, 0);
    if (embedded >= 0) return loadEmbeddedTexture(gfx, model, embedded);
    const std::string path = model->getMaterialTexturePath(matIndex, type, 0);
    if (path.empty()) return nullptr;
    return loadExternalTexture(gfx, path);
}

struct TextureLook {
    Texture *albedo = nullptr;
    Texture *normal = nullptr;
    Texture *height = nullptr;
    float tr = 1.f, tg = 1.f, tb = 1.f, ta = 1.f;
    float metallic = 0.f;
    float roughness = 0.45f;
    std::string alphaMode = "OPAQUE";
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
};

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
