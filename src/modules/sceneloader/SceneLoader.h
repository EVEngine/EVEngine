#pragma once

#include "common/Module.h"

#include <string>
#include <unordered_map>
#include <vector>

struct aiScene;
struct aiNode;
struct aiMesh;

namespace eve {
namespace graphics {
class Graphics;
class Renderable3D;
}  // namespace graphics
namespace model3d {
class ModelData;
}  // namespace model3d
namespace scene {
class NodeDesc;
class SceneHost;
}  // namespace scene

namespace sceneloader {

/**
 * SceneDiffEntry / SceneDiff
 *
 * The result of diffing a freshly decoded 3D scene against the scene tree that
 * is currently mounted (the "old" SceneHost). This is what makes hot-reload a
 * *fast diff*: instead of reloading and rebuilding every GameObject, the loader
 * reports exactly which GameObjects need to change so only the corresponding
 * objects are updated.
 *
 *  - Add:     object exists in the new scene but not in the mounted tree.
 *  - Remove:  object exists in the mounted tree but not in the new scene.
 *  - Modify:  object exists in both, identity preserved; only props changed.
 *  - Move:    object exists in both but was re-parented.
 */
struct SceneDiffEntry {
    enum class Action { Add, Remove, Modify, Move };

    Action action = Action::Add;
    std::string id;      // GameObject id (scene-object id).
    std::string parent;  // new parent id (Add / Move only).
};

struct SceneDiff {
    std::vector<SceneDiffEntry> entries;
    int added = 0;
    int removed = 0;
    int modified = 0;
    int moved = 0;

    bool empty() const { return entries.empty(); }
};

/**
 * A GPU mesh reference for one Assimp mesh referenced by an aiNode. The loader
 * uploads one Renderable3D (with its own mesh + material) per aiMesh, so every
 * mesh is an independently diffable GameObject and unchanged meshes are never
 * re-uploaded across a hot reload.
 */
struct MeshSlot {
    const aiScene *scene = nullptr;
    const aiMesh *mesh = nullptr;
    unsigned materialIndex = 0;
};
using MeshSlotMap = std::unordered_map<std::string, std::vector<MeshSlot>>;

/**
 * Loads a 3D scene (glTF / OBJ / FBX / GLB ... via Assimp/medialoader) into the
 * ECS scene tree.
 *
 * For every aiNode it creates a scene::SceneHost GameObject (SceneNode) mirroring
 * the source hierarchy, and for every referenced aiMesh it creates a child
 * GameObject that links a graphics::Renderable3D (mesh + diffuse material). The
 * host is itself an ecs::Entity, so the whole model becomes a tree of GameObjects
 * inside the ECS.
 *
 * Hot-reload: reload() re-decodes the file, diffs the new tree against the
 * mounted tree, and applies only the changed GameObjects in place — transform /
 * visibility edits patch the existing node, added objects are appended (mesh
 * uploaded once), removed objects are destroyed, and identical objects are left
 * completely untouched (no re-upload, no rebuild).
 */
class SceneLoader : public Module {
public:
    Module_REG(SceneLoader);

    /** Full load: build the GameObject tree from `path`, mount it, return host. */
    scene::SceneHost *load(const std::string &path, bool linkRenderables = true);

    /**
     * Hot-reload `path`. Re-decodes, diffs, and applies only what changed.
     * Returns false if the file failed to decode (old scene is kept) or nothing
     * changed. When `out` is non-null it is filled with the applied diff.
     */
    bool reload(const std::string &path, SceneDiff *out = nullptr);

    /** Script-friendly reload: returns true when anything was updated. */
    bool reloadChecked(const std::string &path) { return reload(path, nullptr); }

    /** Dry-run diff for `path` against the currently mounted tree (no mutation). */
    SceneDiff diff(const std::string &path);

    /** The mounted host for `path`, or nullptr if not loaded. */
    scene::SceneHost *host(const std::string &path);

    /** Drop the loaded scene for `path`, destroying linked Renderable3D objects. */
    void unload(const std::string &path);

    /** Number of GameObjects (SceneNodes) currently mounted for `path`; 0 if none. */
    int nodeCount(const std::string &path);
    /** True if `path` is currently loaded. */
    bool loaded(const std::string &path);

    // ---- pure tree helpers (no graphics required; unit-testable) ----

    /** Flatten an Assimp scene into a scene::NodeDesc tree (id = object id). */
    static scene::NodeDesc buildNodeDesc(const aiScene *scene, MeshSlotMap *slotsOut = nullptr);

    /** Diff a mounted host tree against a freshly built NodeDesc tree. */
    static SceneDiff diffTree(scene::SceneHost *host, const scene::NodeDesc &newRoot);

    /**
     * Apply a diff to the host arena in place. Creates/destroys Renderable3D for
     * added/removed mesh objects when `gfx` and `slots` are provided; otherwise
     * only the GameObject tree is mutated (useful for logic-only tests).
     */
    static bool applyTreeDiff(scene::SceneHost *host, const scene::NodeDesc &newRoot,
                              const SceneDiff &diff, graphics::Graphics *gfx,
                              const MeshSlotMap *slots);

private:
    struct Loaded {
        std::string path;
        scene::SceneHost *host = nullptr;
        graphics::Graphics *gfx = nullptr;
    };
    std::unordered_map<std::string, Loaded> scenes_;
};

}  // namespace sceneloader
}  // namespace eve
