#include "sceneloader/SceneLoader.h"

#include "scene/SceneHost.h"
#include "scene/NodeDesc.h"
#include "scene/TransformSystem.h"
#include "model3d/Model3D.h"
#include "model3d/ModelData.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "filesystem/Filesystem.h"
#include "filesystem/FileData.h"
#include "data/ByteData.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "common/ECS.h"

#include <assimp/scene.h>
#include <assimp/mesh.h>
#include <assimp/material.h>
#include <assimp/matrix4x4.h>
#include <assimp/quaternion.h>
#include <assimp/vector3.h>
#include <assimp/texture.h>

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

// ---- texture helper (embedded / VFS) ----

graphics::Texture *resolveDiffuseTexture(graphics::Graphics *gfx, const aiScene *scene,
                                         const aiMaterial *mat) {
    if (!gfx || !scene || !mat) return nullptr;
    aiString path;
    if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &path) != AI_SUCCESS) return nullptr;
    const char *p = path.C_Str();
    if (!p || !p[0]) return nullptr;

    eve::image::Image::create();

    // Embedded texture ("*0", "*1", ...).
    if (p[0] == '*') {
        int idx = std::atoi(p + 1);
        if (idx < 0 || static_cast<unsigned>(idx) >= scene->mNumTextures) return nullptr;
        const aiTexture *tex = scene->mTextures[idx];
        if (!tex || !tex->pcData) return nullptr;
        if (tex->mHeight == 0) {
            eve::data::ByteData bytes(tex->pcData, static_cast<size_t>(tex->mWidth));
            try {
                eve::image::ImageData *img = eve::image::Image::create()->newImageData(&bytes);
                graphics::Texture *t = gfx->newTexture(img);
                delete img;
                return t;
            } catch (...) {
                return nullptr;
            }
        }
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
        return gfx->newTexture(int(w), int(h), rgba.data());
    }

    // External file through the VFS.
    try {
        auto *fs = eve::filesystem::Filesystem::create();
        std::unique_ptr<eve::filesystem::FileData> fd(fs->read(p));
        if (fd && fd->getSize() > 0) {
            eve::image::ImageData *img = eve::image::Image::create()->newImageData(fd.get());
            graphics::Texture *t = gfx->newTexture(img);
            delete img;
            return t;
        }
    } catch (...) {
    }
    return nullptr;
}

// ---- renderable creation ----

graphics::Renderable3D *makeRenderable(graphics::Graphics *gfx, const MeshSlot &slot) {
    if (!gfx || !slot.mesh) return nullptr;
    graphics::Mesh *mesh = gfx->newMeshFromAssimp(*slot.mesh);  // local-space (hierarchy transform)
    if (!mesh) return nullptr;
    auto *r = graphics::Renderable3D::create();
    r->meshRenderer()->visible = true;
    r->setMesh(mesh);

    aiColor3D kd(1.f, 1.f, 1.f);
    if (slot.scene && slot.scene->mMaterials && slot.materialIndex < slot.scene->mNumMaterials) {
        const aiMaterial *mat = slot.scene->mMaterials[slot.materialIndex];
        mat->Get(AI_MATKEY_COLOR_DIFFUSE, kd);
        graphics::Texture *tex = resolveDiffuseTexture(gfx, slot.scene, mat);
        if (tex) r->setTexture(tex);
    }
    r->setTint(kd.r, kd.g, kd.b, 1.f);
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

void linkAllMeshNodes(scene::SceneHost *host, graphics::Graphics *gfx, const MeshSlotMap &slots) {
    for (const auto &kv : slots) {
        scene::SceneNode *n = host->findById(kv.first);
        if (!n) continue;
        graphics::Renderable3D *r = makeRenderable(gfx, kv.second[0]);
        if (!r) continue;
        n->links.push_back(scene::SceneLink{scene::LinkKind::Renderable3D, r, 0});
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
        for (auto &n : nodes) {
            if (!n.links.empty()) continue;
            auto sit = slots->find(n.id);
            if (sit == slots->end() || sit->second.empty()) continue;
            graphics::Renderable3D *r = nullptr;
            try {
                r = makeRenderable(gfx, sit->second[0]);
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

graphics::Graphics *currentGraphics() {
    return ModuleManager::getInstance<graphics::Graphics>("Graphics");
}

scene::SceneHost *SceneLoader::load(const std::string &path, bool linkRenderables) {
    const std::string key = normPath(path);
    auto *mod3d = ModuleManager::getInstance<model3d::Model3D>("Model3D");
    if (!mod3d) mod3d = model3d::Model3D::create();
    model3d::ModelData *md = mod3d->newModelDataFromFile(path);

    MeshSlotMap slots;
    scene::NodeDesc root = buildNodeDesc(md->getScene(), &slots);

    scene::SceneHost *host = scene::SceneHost::createHost(key);
    host->setTree(std::move(root));

    graphics::Graphics *gfx = currentGraphics();
    if (linkRenderables && gfx && !slots.empty()) linkAllMeshNodes(host, gfx, slots);
    if (!slots.empty()) fillSceneBounds(host, slots);

    scene::TransformSystem::updateHost(host);
    scenes_[key] = Loaded{key, host, gfx};
    delete md;
    return host;
}

bool SceneLoader::reload(const std::string &path, SceneDiff *out) {
    const std::string key = normPath(path);
    auto it = scenes_.find(key);
    if (it == scenes_.end()) {
        load(path);
        if (out) *out = SceneDiff{};
        return true;
    }
    Loaded &ld = it->second;

    auto *mod3d = ModuleManager::getInstance<model3d::Model3D>("Model3D");
    if (!mod3d) mod3d = model3d::Model3D::create();
    model3d::ModelData *md = nullptr;
    try {
        md = mod3d->newModelDataFromFile(path);
    } catch (...) {
        // File decode failed: keep the old scene.
        return false;
    }
    if (!md) return false;

    MeshSlotMap slots;
    scene::NodeDesc newRoot = buildNodeDesc(md->getScene(), &slots);
    SceneDiff d = diffTree(ld.host, newRoot);
    if (out) *out = d;
    if (!d.empty()) {
        applyTreeDiff(ld.host, newRoot, d, ld.gfx, &slots);
        scene::TransformSystem::updateHost(ld.host);
    }
    delete md;
    return !d.empty();
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
    scenes_.erase(it);
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
}

}  // namespace sceneloader
}  // namespace eve
