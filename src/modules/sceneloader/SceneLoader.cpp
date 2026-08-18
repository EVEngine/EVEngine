#include "sceneloader/SceneLoader.h"

#include "scene/SceneHost.h"
#include "scene/NodeDesc.h"
#include "scene/TransformSystem.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "graphics/Graphics.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"
#include "data/ByteData.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "animation/AnimImporter.h"
#include "thread/ThreadPool.h"
#include "common/ECS.h"

#include <assimp/scene.h>
#include <assimp/mesh.h>
#include <assimp/material.h>
#include <assimp/matrix4x4.h>
#include <assimp/quaternion.h>
#include <assimp/vector3.h>
#include <assimp/texture.h>
#include <assimp/light.h>
#include <assimp/camera.h>
#include <assimp/GltfMaterial.h>

#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <limits>

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <unordered_set>

namespace eve {
namespace sceneloader {

Module_IMPL(SceneLoader, new SceneLoader());

SceneLoader::~SceneLoader() {
    for (auto &kv : prewarmed_) delete kv.second.md;
    for (auto &d : pending_) delete d.md;
    clearTextures();
}

namespace {

constexpr float kEps = 1e-5f;

std::string normPath(const std::string &p) {
    std::string out;
    out.reserve(p.size());
    for (char c : p) {
        if (c == '\\') out.push_back('/');
        else out.push_back(c);
    }
    return out;
}

graphics::Graphics *currentGraphics() {
    return ModuleManager::getInstance<graphics::Graphics>("Graphics");
}

model3d::ModelLoadOptions toModelOptions(const LoadOptions &o) {
    model3d::ModelLoadOptions m;
    m.triangulate = o.triangulate;
    m.generateNormalsIfMissing = o.generateNormalsIfMissing;
    m.joinIdenticalVertices = o.joinIdenticalVertices;
    m.flipUVs = o.flipUVs;
    m.improveCacheLocality = o.improveCacheLocality;
    return m;
}

bool approx(float a, float b) { return std::fabs(a - b) < kEps; }

// ---- transform helpers ----

void decomposeNode(const aiMatrix4x4 &m, float &x, float &y, float &z, float &yaw, float &pitch,
                   float &roll, float &sx, float &sy, float &sz) {
    aiVector3D pos, scale;
    aiQuaternion rot;
    m.Decompose(scale, rot, pos);
    x = pos.x;
    y = pos.y;
    z = pos.z;
    sx = scale.x;
    sy = scale.y;
    sz = scale.z;
    glm::quat q(rot.w, rot.x, rot.y, rot.z);
    glm::vec3 e = glm::eulerAngles(q);
    yaw = e.y;
    pitch = e.x;
    roll = e.z;
}

std::string uniqueId(const std::string &base, std::unordered_map<std::string, int> &counts) {
    std::string b = base.empty() ? "node" : base;
    int &n = counts[b];
    std::string id = b;
    if (n > 0) id = b + "_" + std::to_string(n);
    ++n;
    return id;
}

// ---- texture helpers (embedded / VFS, cached by path) ----

/** Texture sampling derived from the source material (wrap + glTF mag/min filters). */
struct SamplerSpec {
    bool repeatU = true;
    bool repeatV = true;
    bool mips = true;
    std::string filter = "linear";  // "linear" | "nearest"
    std::string mipmap = "linear";  // "none" | "linear" | "nearest"
};

SamplerSpec samplerFor(const aiMaterial *mat, aiTextureType type, bool wantMips) {
    SamplerSpec s;
    if (!mat) {
        s.mips = wantMips;
        return s;
    }
    int modeU = aiTextureMapMode_Wrap;
    int modeV = aiTextureMapMode_Wrap;
    mat->Get(AI_MATKEY_MAPPINGMODE_U(type, 0), modeU);
    mat->Get(AI_MATKEY_MAPPINGMODE_V(type, 0), modeV);
    s.repeatU = (modeU != aiTextureMapMode_Clamp && modeU != aiTextureMapMode_Mirror);
    s.repeatV = (modeV != aiTextureMapMode_Clamp && modeV != aiTextureMapMode_Mirror);

    // glTF sampler filter values (AI_MATKEY_GLTF_MAPPINGFILTER_*).
    int mag = 0, min = 0;
    mat->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MAG(type, 0), mag);
    mat->Get(AI_MATKEY_GLTF_MAPPINGFILTER_MIN(type, 0), min);
    s.filter = (mag == 9728) ? "nearest" : "linear";  // 0 / 9729(linear) -> linear
    switch (min) {
        case 9728:  // NEAREST
        case 9729:  // LINEAR — no mipmaps requested by the source
            s.mips = false;
            s.mipmap = "none";
            break;
        case 9986:  // NEAREST_MIPMAP_LINEAR
        case 9987:  // LINEAR_MIPMAP_LINEAR
            s.mips = wantMips;
            s.mipmap = "linear";
            break;
        case 9984:  // NEAREST_MIPMAP_NEAREST
        case 9985:  // LINEAR_MIPMAP_NEAREST
            s.mips = wantMips;
            s.mipmap = "nearest";
            break;
        default:  // unspecified -> honor the global toggle
            s.mips = wantMips;
            s.mipmap = wantMips ? "linear" : "none";
            break;
    }
    return s;
}

graphics::Texture *resolveTexture(graphics::Graphics *gfx, const aiScene *scene,
                                  const aiMaterial *mat, aiTextureType type,
                                  SceneLoader::TextureCache &cache, bool wantMips,
                                  const SceneLoader::CpuImageMap *predecoded = nullptr) {
    if (!gfx || !scene || !mat) return nullptr;
    aiString path;
    if (mat->GetTexture(type, 0, &path) != AI_SUCCESS) return nullptr;
    const char *p = path.C_Str();
    if (!p || !p[0]) return nullptr;

    const SamplerSpec s = samplerFor(mat, type, wantMips);
    eve::image::Image::create();

    const std::string keySuffix =
        std::string(s.repeatU ? "|1" : "|0") + (s.repeatV ? "1" : "0") + "|" + s.filter + "|" +
        s.mipmap;

    // Embedded texture ("*0", "*1", ...).
    if (p[0] == '*') {
        int idx = std::atoi(p + 1);
        if (idx < 0 || static_cast<unsigned>(idx) >= scene->mNumTextures) return nullptr;
        const aiTexture *tex = scene->mTextures[idx];
        if (!tex || !tex->pcData) return nullptr;
        const std::string key = "*" + std::to_string(idx) + keySuffix;
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;

        graphics::Texture *t = nullptr;
        if (tex->mHeight == 0) {
            eve::data::ByteData bytes(tex->pcData, static_cast<size_t>(tex->mWidth));
            try {
                eve::image::ImageData *img = eve::image::Image::create()->newImageData(&bytes);
                t = gfx->newTextureWithSampler(img, s.repeatU, s.repeatV, s.mips, 8.f, s.filter,
                                               s.mipmap);
                delete img;
            } catch (...) {
                return nullptr;
            }
        } else {
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
            eve::image::ImageData img(int(w), int(h), "RGBA8", rgba.data(), false);
            t = gfx->newTextureWithSampler(&img, s.repeatU, s.repeatV, s.mips, 8.f, s.filter,
                                           s.mipmap);
        }
        if (t) cache[key] = t;
        return t;
    }

    // External file through the VFS.
    const std::string key = normPath(p) + keySuffix;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    // Off-thread pre-decoded CPU image (async path): upload directly, no disk IO.
    if (predecoded) {
        auto pit = predecoded->find(key);
        if (pit != predecoded->end()) {
            const SceneLoader::CpuImage &ci = pit->second;
            eve::image::ImageData img(ci.w, ci.h, "RGBA8",
                                      const_cast<uint8_t *>(ci.rgba.data()), false);
            graphics::Texture *t = gfx->newTextureWithSampler(&img, s.repeatU, s.repeatV, s.mips,
                                                              8.f, s.filter, s.mipmap);
            if (t) cache[key] = t;
            return t;
        }
    }

    try {
        auto *fs = eve::filesystem::Filesystem::create();
        std::unique_ptr<eve::filesystem::FileData> fd(fs->read(p));
        if (fd && fd->getSize() > 0) {
            eve::image::ImageData *img = eve::image::Image::create()->newImageData(fd.get());
            graphics::Texture *t = gfx->newTextureWithSampler(img, s.repeatU, s.repeatV, s.mips,
                                                              8.f, s.filter, s.mipmap);
            delete img;
            if (t) cache[key] = t;
            return t;
        }
    } catch (...) {
    }
    return nullptr;
}

// Pre-decode external (non-embedded) texture files off the calling thread so the
// async loader can skip disk IO + image decode on the render thread.
void collectCpuImages(const aiScene *scene, const MeshSlotMap &slots, bool wantMips,
                      SceneLoader::CpuImageMap &out) {
    if (!scene) return;
    eve::image::Image::create();
    const aiTextureType kTypes[4] = {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE,
                                     aiTextureType_NORMALS, aiTextureType_HEIGHT};
    for (const auto &kv : slots) {
        for (const MeshSlot &slot : kv.second) {
            if (!slot.scene || slot.materialIndex >= scene->mNumMaterials) continue;
            const aiMaterial *mat = scene->mMaterials[slot.materialIndex];
            if (!mat) continue;
            for (aiTextureType type : kTypes) {
                aiString p;
                if (mat->GetTexture(type, 0, &p) != AI_SUCCESS) continue;
                const char *c = p.C_Str();
                if (!c || !c[0] || c[0] == '*') continue;  // embedded handled on main thread
                const SamplerSpec s = samplerFor(mat, type, wantMips);
                const std::string key = normPath(c) + std::string(s.repeatU ? "|1" : "|0") +
                                        (s.repeatV ? "1" : "0") + "|" + s.filter + "|" + s.mipmap;
                if (out.count(key)) continue;
                try {
                    auto *fs = eve::filesystem::Filesystem::create();
                    std::unique_ptr<eve::filesystem::FileData> fd(fs->read(c));
                    if (!fd || fd->getSize() == 0) continue;
                    eve::image::ImageData *img =
                        eve::image::Image::create()->newImageData(fd.get());
                    if (!img) continue;
                    if (img->getFormat() == "RGBA8") {
                        SceneLoader::CpuImage ci;
                        ci.w = img->getWidth();
                        ci.h = img->getHeight();
                        const size_t n = size_t(ci.w) * size_t(ci.h) * 4;
                        ci.rgba.assign(reinterpret_cast<const uint8_t *>(img->getData()),
                                       reinterpret_cast<const uint8_t *>(img->getData()) + n);
                        out[key] = std::move(ci);
                    }
                    delete img;
                } catch (...) {
                }
            }
        }
    }
}

// ---- renderable creation ----

graphics::Renderable3D *makeRenderable(graphics::Graphics *gfx, const MeshSlot &slot,
                                       SceneLoader::TextureCache &textures, bool mipmaps) {
    if (!gfx || !slot.mesh) return nullptr;
    graphics::Mesh *mesh = gfx->newMeshFromAssimp(*slot.mesh);  // local-space (hierarchy transform)
    if (!mesh) return nullptr;
    auto *r = graphics::Renderable3D::create();
    r->meshRenderer()->visible = true;
    r->setMesh(mesh);

    const aiScene *scene = slot.scene;
    const aiMaterial *mat = nullptr;
    if (scene && scene->mMaterials && slot.materialIndex < scene->mNumMaterials)
        mat = scene->mMaterials[slot.materialIndex];

    aiColor3D base(1.f, 1.f, 1.f);
    if (mat) {
        if (mat->Get(AI_MATKEY_BASE_COLOR, base) != AI_SUCCESS)
            mat->Get(AI_MATKEY_COLOR_DIFFUSE, base);
        float metallic = 0.f;
        float roughness = 0.45f;
        mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
        mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
        r->setMetallic(metallic);
        r->setRoughness(roughness);
    }

    graphics::Texture *albedo =
        resolveTexture(gfx, scene, mat, aiTextureType_BASE_COLOR, textures, mipmaps);
    if (!albedo) albedo = resolveTexture(gfx, scene, mat, aiTextureType_DIFFUSE, textures, mipmaps);
    graphics::Texture *normal =
        resolveTexture(gfx, scene, mat, aiTextureType_NORMALS, textures, mipmaps);
    graphics::Texture *height =
        resolveTexture(gfx, scene, mat, aiTextureType_HEIGHT, textures, mipmaps);

    r->setTint(base.r, base.g, base.b, 1.f);
    if (albedo) r->setTexture(albedo);
    if (normal) r->setNormalTexture(normal);
    if (height) r->setHeightTexture(height);
    return r;
}

void destroyRenderable(graphics::Renderable3D *r) {
    if (r) ecs::DestroyEntity(r);
}

// ---- mesh AABB bounds (picking / culling) ----

glm::mat4 nodeLocalMatrix(const scene::SceneNode &n) {
    glm::mat4 m(1.f);
    m = glm::translate(m, glm::vec3(n.x, n.y, n.z));
    if (n.space == "2d") {
        m = glm::rotate(m, n.roll, glm::vec3(0.f, 0.f, 1.f));
    } else {
        m = glm::rotate(m, n.yaw, glm::vec3(0.f, 1.f, 0.f));
        m = glm::rotate(m, n.pitch, glm::vec3(1.f, 0.f, 0.f));
        m = glm::rotate(m, n.roll, glm::vec3(0.f, 0.f, 1.f));
    }
    m = glm::scale(m, glm::vec3(n.sx, n.sy, n.sz));
    return m;
}

void fillMeshBoundsFromSlot(scene::SceneNode &n, const MeshSlot &slot) {
    const aiMesh *mesh = slot.mesh;
    if (!mesh || !mesh->mVertices || mesh->mNumVertices == 0) return;
    float mn[3] = {std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max()};
    float mx[3] = {std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest()};
    for (unsigned i = 0; i < mesh->mNumVertices; ++i) {
        const aiVector3D &v = mesh->mVertices[i];
        for (int c = 0; c < 3; ++c) {
            const float f = v[c];
            mn[c] = std::min(mn[c], f);
            mx[c] = std::max(mx[c], f);
        }
    }
    n.bminX = mn[0];
    n.bminY = mn[1];
    n.bminZ = mn[2];
    n.bmaxX = mx[0];
    n.bmaxY = mx[1];
    n.bmaxZ = mx[2];
    n.hasBounds = true;
}

/** Post-order: union children bounds into the parent's local space. */
void unionChildBounds(scene::SceneHost::Tree &tree, int nodeIndex) {
    scene::SceneNode &n = tree.nodes[size_t(nodeIndex)];
    for (int c = n.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
        unionChildBounds(tree, c);
    }

    bool any = false;
    float mn[3] = {std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max()};
    float mx[3] = {std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest(),
                   std::numeric_limits<float>::lowest()};
    for (int c = n.firstChild; c >= 0; c = tree.nodes[size_t(c)].nextSibling) {
        scene::SceneNode &ch = tree.nodes[size_t(c)];
        if (!ch.hasBounds) continue;
        any = true;
        const glm::mat4 lm = nodeLocalMatrix(ch);
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 p((i & 1) ? ch.bmaxX : ch.bminX,
                              (i & 2) ? ch.bmaxY : ch.bminY,
                              (i & 4) ? ch.bmaxZ : ch.bminZ);
            const glm::vec4 w = lm * glm::vec4(p, 1.f);
            for (int k = 0; k < 3; ++k) {
                mn[k] = std::min(mn[k], w[k]);
                mx[k] = std::max(mx[k], w[k]);
            }
        }
    }
    if (!any) return;
    if (!n.hasBounds) {
        n.bminX = mn[0];
        n.bminY = mn[1];
        n.bminZ = mn[2];
        n.bmaxX = mx[0];
        n.bmaxY = mx[1];
        n.bmaxZ = mx[2];
        n.hasBounds = true;
    } else {
        n.bminX = std::min(n.bminX, mn[0]);
        n.bminY = std::min(n.bminY, mn[1]);
        n.bminZ = std::min(n.bminZ, mn[2]);
        n.bmaxX = std::max(n.bmaxX, mx[0]);
        n.bmaxY = std::max(n.bmaxY, mx[1]);
        n.bmaxZ = std::max(n.bmaxZ, mx[2]);
    }
}

// ---- NodeDesc build ----

void buildNodeRecursive(const aiScene *scene, const aiNode *node, scene::NodeDesc &out,
                        MeshSlotMap *slots, std::unordered_map<std::string, int> &counts) {
    const std::string rawName = (node && node->mName.length) ? node->mName.C_Str() : "<root>";
    const std::string id = uniqueId(rawName, counts);

    out.id = id;
    out.key = id;
    out.name = rawName;
    out.space = "3d";
    out.visible = true;
    decomposeNode(node->mTransformation, out.x, out.y, out.z, out.yaw, out.pitch, out.roll,
                  out.sx, out.sy, out.sz);

    for (unsigned i = 0; node && i < node->mNumMeshes; ++i) {
        const unsigned mi = node->mMeshes[i];
        if (mi >= scene->mNumMeshes) continue;
        const aiMesh *mesh = scene->mMeshes[mi];
        if (!mesh || mesh->mNumFaces == 0) continue;

        const std::string cid = id + "_mesh" + std::to_string(i);
        scene::NodeDesc child;
        child.id = cid;
        child.key = cid;
        child.name = std::string("mesh") + std::to_string(i);
        child.space = "3d";
        child.visible = true;
        out.children.push_back(std::move(child));

        if (slots) {
            MeshSlot s;
            s.scene = scene;
            s.mesh = mesh;
            s.materialIndex = mesh->mMaterialIndex;
            (*slots)[cid].push_back(std::move(s));
        }
    }

    for (unsigned c = 0; node && c < node->mNumChildren; ++c) {
        scene::NodeDesc child;
        buildNodeRecursive(scene, node->mChildren[c], child, slots, counts);
        out.children.push_back(std::move(child));
    }
}

bool propsChanged(const scene::SceneNode &n, const scene::NodeDesc &d) {
    if (n.visible != d.visible) return true;
    return !(approx(n.x, d.x) && approx(n.y, d.y) && approx(n.z, d.z) && approx(n.yaw, d.yaw) &&
             approx(n.pitch, d.pitch) && approx(n.roll, d.roll) && approx(n.sx, d.sx) &&
             approx(n.sy, d.sy) && approx(n.sz, d.sz));
}

// Walk the new tree in DFS order, emitting Add / Move / Modify entries.
void walkNew(const scene::NodeDesc &d, const std::string &parentId,
             const std::unordered_map<std::string, const scene::SceneNode *> &oldNodes,
             const std::unordered_map<std::string, std::string> &oldParent, SceneDiff &out) {
    auto it = oldNodes.find(d.id);
    if (it == oldNodes.end()) {
        out.entries.push_back({SceneDiffEntry::Action::Add, d.id, parentId});
        ++out.added;
    } else {
        auto pit = oldParent.find(d.id);
        const std::string op = (pit != oldParent.end()) ? pit->second : "";
        if (op != parentId) {
            out.entries.push_back({SceneDiffEntry::Action::Move, d.id, parentId});
            ++out.moved;
        } else if (propsChanged(*it->second, d)) {
            out.entries.push_back({SceneDiffEntry::Action::Modify, d.id, ""});
            ++out.modified;
        }
    }
    for (const auto &child : d.children) walkNew(child, d.id, oldNodes, oldParent, out);
}

// ---- lights / cameras / animation import ----

void importLights(const aiScene *scene, std::vector<graphics::Light3D *> &out) {
    if (!scene) return;
    for (unsigned i = 0; i < scene->mNumLights; ++i) {
        const aiLight *l = scene->mLights[i];
        if (!l) continue;
        std::string type = "point";
        switch (l->mType) {
            case aiLightSource_DIRECTIONAL:
                type = "dir";
                break;
            case aiLightSource_POINT:
            case aiLightSource_SPOT:
            case aiLightSource_AREA:
            default:
                type = "point";
                break;
        }
        graphics::Light3D *light = graphics::Light3D::createLight(type);
        if (type == "dir") {
            light->setDirection(l->mDirection.x, l->mDirection.y, l->mDirection.z);
        } else {
            light->setPosition(l->mPosition.x, l->mPosition.y, l->mPosition.z);
            float radius = 8.f;
            if (l->mAttenuationQuadratic > 1e-6f)
                radius = 1.f / std::sqrt(l->mAttenuationQuadratic);
            light->setRadius(radius);
        }
        light->setColor(l->mColorDiffuse.r, l->mColorDiffuse.g, l->mColorDiffuse.b, 1.f);
        out.push_back(light);
    }
}

void importCameras(const aiScene *scene, std::vector<graphics::Camera3D *> &out) {
    if (!scene) return;
    for (unsigned i = 0; i < scene->mNumCameras; ++i) {
        const aiCamera *c = scene->mCameras[i];
        if (!c) continue;
        graphics::Camera3D *cam = graphics::Camera3D::createCamera();
        cam->setActive(false);
        cam->setEye(c->mPosition.x, c->mPosition.y, c->mPosition.z);
        cam->setTarget(c->mLookAt.x, c->mLookAt.y, c->mLookAt.z);
        cam->setUp(c->mUp.x, c->mUp.y, c->mUp.z);
        cam->setFov(glm::degrees(c->mHorizontalFOV));
        out.push_back(cam);
    }
}

void importAnimations(const aiScene *scene, const LoadOptions &options,
                      animation::AnimSkeleton **skeletonOut,
                      std::vector<animation::AnimClip *> &clips) {
    if (!scene || !options.importAnimations || scene->mNumAnimations == 0) return;
    animation::AnimSkeleton *skeleton = animation::AnimImporter::loadSkeleton(scene);
    if (!skeleton) return;
    *skeletonOut = skeleton;
    for (unsigned i = 0; i < scene->mNumAnimations; ++i) {
        animation::AnimClip *clip = animation::AnimImporter::loadClip(scene, skeleton, int(i));
        if (clip) clips.push_back(clip);
    }
}

}  // namespace

// ---- public: pure helpers ----

scene::NodeDesc SceneLoader::buildNodeDesc(const aiScene *scene, MeshSlotMap *slotsOut) {
    scene::NodeDesc root;
    if (!scene || !scene->mRootNode) return root;
    std::unordered_map<std::string, int> counts;
    buildNodeRecursive(scene, scene->mRootNode, root, slotsOut, counts);
    return root;
}

SceneDiff SceneLoader::diffTree(scene::SceneHost *host, const scene::NodeDesc &newRoot) {
    SceneDiff out;
    std::unordered_map<std::string, const scene::SceneNode *> oldNodes;
    std::unordered_map<std::string, std::string> oldParent;
    if (host) {
        auto t = host->tree();
        for (size_t i = 0; i < t->nodes.size(); ++i) {
            const scene::SceneNode &n = t->nodes[i];
            oldNodes[n.id] = &n;
            oldParent[n.id] = (n.parent >= 0) ? t->nodes[size_t(n.parent)].id : "";
        }
    }

    // Collect the set of ids present in the new tree.
    std::unordered_set<std::string> newIds;
    std::function<void(const scene::NodeDesc &)> collect = [&](const scene::NodeDesc &d) {
        newIds.insert(d.id);
        for (const auto &c : d.children) collect(c);
    };
    collect(newRoot);

    // Removed: present in old, absent in new.
    for (const auto &kv : oldNodes) {
        if (!newIds.count(kv.first)) {
            out.entries.push_back({SceneDiffEntry::Action::Remove, kv.first, ""});
            ++out.removed;
        }
    }

    // Add / Move / Modify: walk the new tree (DFS so parents precede children).
    walkNew(newRoot, "", oldNodes, oldParent, out);
    return out;
}

bool SceneLoader::applyTreeDiff(scene::SceneHost *host, const scene::NodeDesc &newRoot,
                                const SceneDiff &diff, graphics::Graphics *gfx,
                                const MeshSlotMap *slots) {
    if (!host || diff.empty()) return false;

    // Snapshot the mounted tree by id so kept GameObjects can keep their linked
    // Renderable3D (no re-upload / no object rebuild) across the update.
    std::unordered_map<std::string, const scene::SceneNode *> oldNodes;
    {
        auto t = host->tree();
        for (const auto &n : t->nodes) oldNodes[n.id] = &n;
    }

    // Collect the ids present in the new tree.
    std::unordered_set<std::string> newIds;
    std::function<void(const scene::NodeDesc &)> collect = [&](const scene::NodeDesc &d) {
        newIds.insert(d.id);
        for (const auto &c : d.children) collect(c);
    };
    collect(newRoot);

    // Destroy Renderable3D of removed GameObjects.
    for (const auto &kv : oldNodes) {
        if (!newIds.count(kv.first)) {
            if (const auto *l = host->findLink(kv.second, scene::LinkKind::Renderable3D)) {
                destroyRenderable(static_cast<graphics::Renderable3D *>(l->target));
            }
        }
    }

    // Rebuild the arena from the new tree (DFS). Kept nodes copy their link from
    // the old node (identity preserved); added mesh nodes get a fresh Renderable3D.
    std::vector<scene::SceneNode> nodes;
    nodes.reserve(newIds.size());
    std::function<int(const scene::NodeDesc &, int)> build = [&](const scene::NodeDesc &d,
                                                                 int parentIndex) -> int {
        const int idx = int(nodes.size());
        scene::SceneNode n;
        n.id = d.id;
        n.key = d.key.empty() ? d.id : d.key;
        n.name = d.name.empty() ? d.id : d.name;
        n.space = d.space.empty() ? "3d" : d.space;
        n.visible = d.visible;
        n.x = d.x;
        n.y = d.y;
        n.z = d.z;
        n.yaw = d.yaw;
        n.pitch = d.pitch;
        n.roll = d.roll;
        n.sx = d.sx;
        n.sy = d.sy;
        n.sz = d.sz;
        n.localDirty = true;
        n.world = glm::mat4(1.f);
        n.firstChild = -1;
        n.nextSibling = -1;
        n.parent = parentIndex;

        auto oldIt = oldNodes.find(d.id);
        if (oldIt != oldNodes.end()) {
            n.links = oldIt->second->links;
            n.objectId = oldIt->second->objectId;
            n.bminX = oldIt->second->bminX;
            n.bminY = oldIt->second->bminY;
            n.bminZ = oldIt->second->bminZ;
            n.bmaxX = oldIt->second->bmaxX;
            n.bmaxY = oldIt->second->bmaxY;
            n.bmaxZ = oldIt->second->bmaxZ;
            n.hasBounds = oldIt->second->hasBounds;
        }

        nodes.push_back(std::move(n));

        int prevChild = -1;
        int firstChild = -1;
        for (const auto &c : d.children) {
            const int ci = build(c, idx);
            if (firstChild < 0) firstChild = ci;
            if (prevChild >= 0) nodes[size_t(prevChild)].nextSibling = ci;
            prevChild = ci;
        }
        nodes[size_t(idx)].firstChild = firstChild;
        return idx;
    };
    const int root = build(newRoot, -1);

    // Fresh Renderable3D for newly added mesh GameObjects (only changed ones).
    if (gfx && slots) {
        TextureCache unused;
        for (auto &n : nodes) {
            if (!n.links.empty()) continue;
            auto sit = slots->find(n.id);
            if (sit == slots->end() || sit->second.empty()) continue;
            graphics::Renderable3D *r = nullptr;
            try {
                r = makeRenderable(gfx, sit->second[0], unused, true);
            } catch (...) {
                r = nullptr;
            }
            if (r) {
                n.links.push_back(scene::SceneLink{scene::LinkKind::Renderable3D, r, 0});
            }
        }
    }

    host->tree()->nodes = std::move(nodes);
    host->tree()->root = root;
    host->invalidateIndex();
    host->markTransformDirty();
    if (slots) fillSceneBounds(host, *slots);
    return true;
}

void SceneLoader::fillSceneBounds(scene::SceneHost *host, const MeshSlotMap &slots) {
    if (!host) return;
    auto t = host->tree();
    for (auto &n : t->nodes) {
        auto it = slots.find(n.id);
        if (it == slots.end() || it->second.empty()) continue;
        fillMeshBoundsFromSlot(n, it->second[0]);
    }
    if (t->root >= 0) unionChildBounds(*t, t->root);
}

// ---- public: file / lifecycle ----
// ---- private: linking + async decode ----

void SceneLoader::linkMeshNodes(scene::SceneHost *host, const MeshSlotMap &slots,
                                graphics::Graphics *gfx, const LoadOptions &options,
                                TextureCache &textures, MeshCache &shared,
                                const CpuImageMap *predecoded) {
    if (!gfx) return;
    for (const auto &kv : slots) {
        scene::SceneNode *n = host->findById(kv.first);
        if (!n || host->findLink(n, scene::LinkKind::Renderable3D) || kv.second.empty()) continue;
        const MeshSlot &slot = kv.second[0];
        graphics::Mesh *mesh = nullptr;
        auto it = shared.find(slot.mesh);
        if (options.sharedMeshes && it != shared.end()) {
            mesh = it->second;
        } else {
            try {
                mesh = gfx->newMeshFromAssimp(*slot.mesh);
            } catch (...) {
                mesh = nullptr;
            }
            if (mesh && options.sharedMeshes) shared[slot.mesh] = mesh;
        }
        if (!mesh) continue;
        auto *r = graphics::Renderable3D::create();
        r->meshRenderer()->visible = true;
        r->setMesh(mesh);

        const aiScene *scene = slot.scene;
        const aiMaterial *mat = nullptr;
        if (scene && scene->mMaterials && slot.materialIndex < scene->mNumMaterials)
            mat = scene->mMaterials[slot.materialIndex];
        aiColor3D base(1.f, 1.f, 1.f);
        if (mat) {
            if (mat->Get(AI_MATKEY_BASE_COLOR, base) != AI_SUCCESS)
                mat->Get(AI_MATKEY_COLOR_DIFFUSE, base);
            float metallic = 0.f;
            float roughness = 0.45f;
            mat->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
            mat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
            r->setMetallic(metallic);
            r->setRoughness(roughness);
        }
        graphics::Texture *albedo = resolveTexture(gfx, scene, mat, aiTextureType_BASE_COLOR,
                                                   textures, options.mipmaps, predecoded);
        if (!albedo)
            albedo = resolveTexture(gfx, scene, mat, aiTextureType_DIFFUSE, textures,
                                    options.mipmaps, predecoded);
        graphics::Texture *normal = resolveTexture(gfx, scene, mat, aiTextureType_NORMALS,
                                                   textures, options.mipmaps, predecoded);
        graphics::Texture *height = resolveTexture(gfx, scene, mat, aiTextureType_HEIGHT,
                                                   textures, options.mipmaps, predecoded);
        r->setTint(base.r, base.g, base.b, 1.f);
        if (albedo) r->setTexture(albedo);
        if (normal) r->setNormalTexture(normal);
        if (height) r->setHeightTexture(height);
        n->links.push_back(scene::SceneLink{scene::LinkKind::Renderable3D, r, 0});
    }
}

bool SceneLoader::decode(const std::string &path, const LoadOptions &options, DecodedScene *out) {
    auto *mod3d = ModuleManager::getInstance<model3d::Model3D>("Model3D");
    if (!mod3d) mod3d = model3d::Model3D::create();
    model3d::ModelData *md = nullptr;
    try {
        md = mod3d->newModelDataFromFile(path, toModelOptions(options));
    } catch (...) {
        return false;
    }
    if (!md) return false;

    out->path = normPath(path);
    out->md = md;
    out->options = options;
    out->root = buildNodeDesc(md->getScene(), &out->slots);
    collectCpuImages(md->getScene(), out->slots, options.mipmaps, out->cpuImages);
    return true;
}

scene::SceneHost *SceneLoader::mount(DecodedScene &d) {
    scene::SceneHost *host = scene::SceneHost::createHost(d.path);
    host->setTree(std::move(d.root));

    graphics::Graphics *gfx = currentGraphics();
    if (gfx) {
        MeshCache shared;
        linkMeshNodes(host, d.slots, gfx, d.options, textures_, shared, &d.cpuImages);
        if (d.options.importLights) importLights(d.md->getScene(), d.lights);
        if (d.options.importCameras) importCameras(d.md->getScene(), d.cameras);
        importAnimations(d.md->getScene(), d.options, &d.skeleton, d.clips);
    }
    fillSceneBounds(host, d.slots);
    scene::TransformSystem::updateHost(host);
    scenes_[d.path] = Loaded{d.path, host, gfx, d.options, std::move(d.lights),
                             std::move(d.cameras), d.skeleton, std::move(d.clips)};
    return host;
}

void SceneLoader::clearTextures() {
    for (auto &kv : textures_) delete kv.second;
    textures_.clear();
}

// ---- public: file / lifecycle ----

scene::SceneHost *SceneLoader::load(const std::string &path, bool linkRenderables,
                                    const LoadOptions &options) {
    const std::string key = normPath(path);
    DecodedScene d;
    auto warm = prewarmed_.find(key);
    if (warm != prewarmed_.end()) {
        d = std::move(warm->second);
        prewarmed_.erase(warm);
    } else if (!decode(path, options, &d)) {
        return nullptr;
    }
    if (!linkRenderables) {
        scene::SceneHost *host = scene::SceneHost::createHost(d.path);
        host->setTree(std::move(d.root));
        scene::TransformSystem::updateHost(host);
        scenes_[d.path] = Loaded{d.path, host, nullptr, options, {}, {}, nullptr, {}};
        delete d.md;
        return host;
    }
    scene::SceneHost *host = mount(d);
    delete d.md;
    return host;
}

scene::SceneHost *SceneLoader::load(const std::string &path, const LoadOptions &options) {
    return load(path, true, options);
}

bool SceneLoader::reload(const std::string &path, SceneDiff *out, const LoadOptions &options) {
    const std::string key = normPath(path);
    auto it = scenes_.find(key);
    if (it == scenes_.end()) {
        load(path, options);
        if (out) *out = SceneDiff{};
        return true;
    }
    Loaded &ld = it->second;

    DecodedScene d;
    if (!decode(path, options, &d)) return false;

    SceneDiff diff = diffTree(ld.host, d.root);
    if (out) *out = diff;
    if (!diff.empty()) {
        applyTreeDiff(ld.host, d.root, diff, nullptr, nullptr);
        MeshCache shared;
        linkMeshNodes(ld.host, d.slots, ld.gfx, options, textures_, shared, &d.cpuImages);
        scene::TransformSystem::updateHost(ld.host);
    }
    delete d.md;
    return !diff.empty();
}

SceneDiff SceneLoader::diff(const std::string &path) {
    const std::string key = normPath(path);
    auto *mod3d = ModuleManager::getInstance<model3d::Model3D>("Model3D");
    if (!mod3d) mod3d = model3d::Model3D::create();
    model3d::ModelData *md = nullptr;
    try {
        md = mod3d->newModelDataFromFile(path);
    } catch (...) {
        return SceneDiff{};
    }
    if (!md) return SceneDiff{};
    MeshSlotMap slots;
    scene::NodeDesc newRoot = buildNodeDesc(md->getScene(), &slots);
    SceneDiff d;
    auto it = scenes_.find(key);
    if (it != scenes_.end() && it->second.host) {
        d = diffTree(it->second.host, newRoot);
    } else {
        // Nothing mounted: every node in the new tree is an add.
        std::function<void(const scene::NodeDesc &, const std::string &)> walk =
            [&](const scene::NodeDesc &n, const std::string &parent) {
                d.entries.push_back({SceneDiffEntry::Action::Add, n.id, parent});
                ++d.added;
                for (const auto &c : n.children) walk(c, n.id);
            };
        walk(newRoot, "");
    }
    delete md;
    return d;
}

scene::SceneHost *SceneLoader::host(const std::string &path) {
    auto it = scenes_.find(normPath(path));
    return (it != scenes_.end()) ? it->second.host : nullptr;
}

int SceneLoader::nodeCount(const std::string &path) {
    scene::SceneHost *h = host(path);
    return h ? h->getNodeCount() : 0;
}

bool SceneLoader::loaded(const std::string &path) { return scenes_.count(normPath(path)) > 0; }

void SceneLoader::unload(const std::string &path) {
    auto it = scenes_.find(normPath(path));
    if (it == scenes_.end()) return;
    if (it->second.host) {
        auto t = it->second.host->tree();
        for (auto &n : t->nodes) {
            if (const auto *l = it->second.host->findLink(&n, scene::LinkKind::Renderable3D)) {
                destroyRenderable(static_cast<graphics::Renderable3D *>(l->target));
            }
            n.links.clear();
        }
        ecs::DestroyEntity(it->second.host);
    }
    for (graphics::Light3D *l : it->second.lights) ecs::DestroyEntity(l);
    for (graphics::Camera3D *c : it->second.cameras) ecs::DestroyEntity(c);
    delete it->second.skeleton;
    for (animation::AnimClip *clip : it->second.clips) delete clip;
    scenes_.erase(it);
}

// ---- async loading ----

bool SceneLoader::loadAsync(const std::string &path, const LoadOptions &options,
                            std::function<void(scene::SceneHost *)> done) {
    const std::string key = normPath(path);
    {
        std::lock_guard<std::mutex> lock(pendingMu_);
        for (const auto &p : pending_)
            if (p.path == key) return false;
    }
    if (!pool_) pool_ = std::make_shared<thread::ThreadPool>(2);

    auto self = this;
    pool_->submit([self, key, options, done]() {
        DecodedScene d;
        if (self->decode(key, options, &d)) {
            std::lock_guard<std::mutex> lock(self->pendingMu_);
            d.done = done;
            self->pending_.push_back(std::move(d));
        }
    });
    return true;
}

int SceneLoader::pollAsync() {
    std::vector<DecodedScene> ready;
    {
        std::lock_guard<std::mutex> lock(pendingMu_);
        ready.swap(pending_);
    }
    for (auto &d : ready) {
        if (!d.md) continue;
        if (d.prewarmOnly) {
            prewarmed_[d.path] = std::move(d);
            continue;
        }
        scene::SceneHost *h = mount(d);
        delete d.md;
        if (d.done) d.done(h);
    }
    return static_cast<int>(ready.size());
}

bool SceneLoader::prewarmAsync(const std::string &path, const LoadOptions &options) {
    const std::string key = normPath(path);
    {
        std::lock_guard<std::mutex> lock(pendingMu_);
        if (prewarmed_.count(key)) return false;
        for (const auto &p : pending_)
            if (p.path == key) return false;
    }
    if (!pool_) pool_ = std::make_shared<thread::ThreadPool>(2);

    auto self = this;
    pool_->submit([self, key, options]() {
        DecodedScene d;
        if (!self->decode(key, options, &d)) return;
        d.prewarmOnly = true;
        std::lock_guard<std::mutex> lock(self->pendingMu_);
        self->pending_.push_back(std::move(d));
    });
    return true;
}

bool SceneLoader::prewarmed(const std::string &path) const {
    return prewarmed_.count(normPath(path)) > 0;
}

void SceneLoader::clearPrewarm(const std::string &path) {
    auto it = prewarmed_.find(normPath(path));
    if (it == prewarmed_.end()) return;
    delete it->second.md;
    prewarmed_.erase(it);
}

int SceneLoader::pendingAsyncCount() const {
    std::lock_guard<std::mutex> lock(pendingMu_);
    return static_cast<int>(pending_.size());
}

// ---- imported scene extras ----

int SceneLoader::lightCount(const std::string &path) {
    auto it = scenes_.find(normPath(path));
    return (it != scenes_.end()) ? static_cast<int>(it->second.lights.size()) : 0;
}

graphics::Light3D *SceneLoader::light(const std::string &path, int index) {
    auto it = scenes_.find(normPath(path));
    if (it == scenes_.end() || index < 0 ||
        static_cast<size_t>(index) >= it->second.lights.size())
        return nullptr;
    return it->second.lights[size_t(index)];
}

int SceneLoader::cameraCount(const std::string &path) {
    auto it = scenes_.find(normPath(path));
    return (it != scenes_.end()) ? static_cast<int>(it->second.cameras.size()) : 0;
}

graphics::Camera3D *SceneLoader::camera(const std::string &path, int index) {
    auto it = scenes_.find(normPath(path));
    if (it == scenes_.end() || index < 0 ||
        static_cast<size_t>(index) >= it->second.cameras.size())
        return nullptr;
    return it->second.cameras[size_t(index)];
}

int SceneLoader::animationCount(const std::string &path) {
    auto it = scenes_.find(normPath(path));
    return (it != scenes_.end()) ? static_cast<int>(it->second.clips.size()) : 0;
}

animation::AnimSkeleton *SceneLoader::skeleton(const std::string &path) {
    auto it = scenes_.find(normPath(path));
    return (it != scenes_.end()) ? it->second.skeleton : nullptr;
}

animation::AnimClip *SceneLoader::clip(const std::string &path, int index) {
    auto it = scenes_.find(normPath(path));
    if (it == scenes_.end() || index < 0 ||
        static_cast<size_t>(index) >= it->second.clips.size())
        return nullptr;
    return it->second.clips[size_t(index)];
}

void SceneLoader::expose(ssq::Table &table) {
    auto cls = table.addClass(name, SceneLoader::create, false);
    expose(cls);
}

void SceneLoader::expose(ssq::Class &cls) {
    cls.addFunc("getName", &SceneLoader::getName);
    cls.addFunc("reloadChecked", &SceneLoader::reloadChecked);
    cls.addFunc("nodeCount", &SceneLoader::nodeCount);
    cls.addFunc("loaded", &SceneLoader::loaded);
    cls.addFunc("unload", &SceneLoader::unload);
    cls.addFunc("pollAsync", &SceneLoader::pollAsync);
    cls.addFunc("pendingAsyncCount", &SceneLoader::pendingAsyncCount);
    cls.addFunc("prewarmed", &SceneLoader::prewarmed);
    cls.addFunc("clearPrewarm", &SceneLoader::clearPrewarm);
    cls.addFunc("lightCount", &SceneLoader::lightCount);
    cls.addFunc("cameraCount", &SceneLoader::cameraCount);
    cls.addFunc("animationCount", &SceneLoader::animationCount);
}

}  // namespace sceneloader
}  // namespace eve
