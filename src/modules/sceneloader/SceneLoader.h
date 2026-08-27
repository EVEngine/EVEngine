#pragma once

#include "common/Module.h"
#include "common/Result.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct aiScene;
struct aiNode;
struct aiMesh;

namespace eve {
namespace animation {
class AnimSkeleton;
class AnimClip;
}  // namespace animation
namespace graphics {
class Graphics;
class Renderable3D;
class Light3D;
class Camera3D;
class Texture;
class Mesh;
}  // namespace graphics
namespace model3d {
class ModelData;
}  // namespace model3d
namespace scene {
struct NodeDesc;
class SceneHost;
}  // namespace scene
namespace thread {
class ThreadPool;
}

namespace sceneloader {

/**
 * @brief SceneDiffEntry / SceneDiff
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
 * @brief A GPU mesh reference for one Assimp mesh referenced by an aiNode. The loader
 * uploads one Renderable3D (with its own mesh + material) per aiMesh, so every
 * mesh is an independently diffable GameObject and unchanged meshes are never
 * re-uploaded across a hot reload. With LoadOptions::sharedMeshes the same
 * aiMesh referenced by several nodes uploads a single GPU buffer.
 */
struct MeshSlot {
    const aiScene *scene = nullptr;
    const aiMesh *mesh = nullptr;
    unsigned materialIndex = 0;
};
using MeshSlotMap = std::unordered_map<std::string, std::vector<MeshSlot>>;

/**
 * @brief Options controlling how a 3D scene file is decoded and mounted.
 *
 * The first five toggles map to medialoader's Assimp post-processing steps
 * (triangulate, generate-normals-if-missing, join-identical-vertices, flip-UVs,
 * improve-cache-locality). All default to on; disabling one changes the decoded
 * vertex layout (e.g. joinIdenticalVertices=false keeps per-face vertices for
 * hard-edged flat shading). Tangents are not required by the default PBR
 * pipeline (it derives its tangent frame from screen-space derivatives).
 */
struct LoadOptions {
    bool triangulate = true;
    bool generateNormalsIfMissing = true;
    bool joinIdenticalVertices = true;
    bool flipUVs = true;
    bool improveCacheLocality = true;
    /** @brief Reuse one GPU mesh when several nodes reference the same aiMesh. */
    bool sharedMeshes = true;
    /** @brief Generate mipmaps + anisotropic filtering for imported textures. */
    bool mipmaps = true;
    /** @brief Import aiLight entries as graphics::Light3D entities. */
    bool importLights = true;
    /** @brief Import aiCamera entries as inactive graphics::Camera3D entities. */
    bool importCameras = false;
    /** @brief Import aiAnimation clips (skeleton + clips exposed via accessors). */
    bool importAnimations = true;
};

/**
 * @brief Loads a 3D scene (glTF / OBJ / FBX / GLB ... via Assimp/medialoader) into the
 * ECS scene tree.
 *
 * For every aiNode it creates a scene::SceneHost GameObject (SceneNode) mirroring
 * the source hierarchy, and for every referenced aiMesh it creates a child
 * GameObject that links a graphics::Renderable3D (mesh + material). Materials
 * import the PBR base-color / metallic / roughness / normal / height factors
 * and textures. Textures are cached by path and shared across meshes; meshes
 * referenced by several nodes share one GPU buffer (LoadOptions::sharedMeshes).
 * aiLight / aiCamera / aiAnimation entries are imported as graphics::Light3D /
 * graphics::Camera3D entities and animation::AnimSkeleton + AnimClip objects
 * when the corresponding LoadOptions flags are enabled.
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

    ~SceneLoader() override;

    /** @brief Full load: build the GameObject tree from `path`, mount it, return host. */
    scene::SceneHost *load(const std::string &path, bool linkRenderables = true,
                           const LoadOptions &options = {});
    scene::SceneHost *load(const std::string &path, const LoadOptions &options);
    /** @brief Load with a built-in preset: quality, balanced, mobile, or raw. */
    scene::SceneHost *loadPreset(const std::string &path, const std::string &preset);

    /**
     * @brief Hot-reload `path`. Re-decodes, diffs, and applies only what changed.
     * @return A checked result whose value is true when anything changed. A
     *         successful false value is a decoded no-op; decode failures are
     *         reported through the structured status and leave the old scene intact.
     * @param out Optional borrowed output written with the applied diff.
     * @param options Import options used for the fresh decode.
     */
    [[nodiscard("scene reload outcome must be checked")]] eve::Result<bool> reload(const std::string &path,
                                                                                   SceneDiff         *out     = nullptr,
                                                                                   const LoadOptions &options = {});

    /** @brief Dry-run diff for `path` against the currently mounted tree (no mutation). */
    SceneDiff diff(const std::string &path);

    /** @brief The mounted host for `path`, or nullptr if not loaded. */
    scene::SceneHost *host(const std::string &path);

    /** @brief Drop the loaded scene for `path`, destroying linked Renderable3D objects. */
    void unload(const std::string &path);

    /** @brief Number of GameObjects (SceneNodes) currently mounted for `path`; 0 if none. */
    int nodeCount(const std::string &path);
    /** @brief True if `path` is currently loaded. */
    bool loaded(const std::string &path);

    // ---- async loading (decode off-thread, mount on the main thread) ----

    /**
     * @brief Decode `path` on a worker thread, then apply it on the main thread the
     * next time pollAsync() is called (GPU upload + ECS mount happen there).
     * `done` fires on the main thread during pollAsync() with the mounted host.
     * Returns false when a load for `path` is already in flight.
     */
    bool loadAsync(const std::string &path, const LoadOptions &options = {},
                   std::function<void(scene::SceneHost *)> done = nullptr);

    /** @brief Script-friendly async load using the default import preset. */
    bool loadAsyncDefault(const std::string &path) { return loadAsync(path); }
    bool loadAsyncPreset(const std::string &path, const std::string &preset);

    /**
     * @brief Mount every decoded-but-not-applied scene. Must be called on the main /
     * render thread. Returns the number of scenes applied this call.
     */
    int pollAsync();

    /** @brief Decode and retain a scene without creating ECS/GPU objects. */
    bool prewarmAsync(const std::string &path, const LoadOptions &options = {});
    /** @brief Script-friendly CPU prewarm using the default import preset. */
    bool prewarm(const std::string &path) { return prewarmAsync(path); }
    /** @brief True when a decoded scene is ready for load() to consume. */
    bool prewarmed(const std::string &path) const;
    /** @brief Drop a decoded prewarm result and release its CPU scene. */
    void clearPrewarm(const std::string &path);

    /** @brief Number of async loads still waiting to be mounted. */
    int pendingAsyncCount() const;

    /** @brief Number of non-fatal import warnings retained for path. */
    int warningCount(const std::string &path) const;
    /** @brief Non-fatal import warning at index, or an empty string. */
    std::string warning(const std::string &path, int index) const;
    /** @brief Select imported name-convention LOD (_LOD0, _LOD1, ...). */
    bool setLod(const std::string &path, int level);
    /** @brief Number of imported SOCKET_ nodes. */
    int socketCount(const std::string &path) const;
    /** @brief Name of an imported SOCKET_ node, or an empty string. */
    std::string socketName(const std::string &path, int index) const;
    /** @brief Number of imported UCX_/UBX_/USP_/UCP_ collision nodes. */
    int collisionCount(const std::string &path) const;
    /** @brief Name of an imported collision node, or an empty string. */
    std::string collisionName(const std::string &path, int index) const;

    // ---- imported scene extras ----

    /** @brief Number of imported Light3D entities for `path`; 0 if none / disabled. */
    int lightCount(const std::string &path);
    graphics::Light3D *light(const std::string &path, int index);

    /** @brief Number of imported Camera3D entities for `path`; 0 if none / disabled. */
    int cameraCount(const std::string &path);
    graphics::Camera3D *camera(const std::string &path, int index);

    /** @brief Number of imported animation clips for `path`; 0 if none / disabled. */
    int animationCount(const std::string &path);
    /** @brief Imported skeleton for `path` (nullptr when the scene has no animations). */
    animation::AnimSkeleton *skeleton(const std::string &path);
    animation::AnimClip *clip(const std::string &path, int index);

    // ---- pure tree helpers (no graphics required; unit-testable) ----

    /** @brief Caches shared across the loader: textures by source key, GPU meshes per aiMesh. */
    using TextureCache = std::unordered_map<std::string, graphics::Texture *>;
    using MeshCache = std::unordered_map<const aiMesh *, graphics::Mesh *>;
    /** @brief CPU-decoded texture (RGBA8) keyed by the same source+suffix key as TextureCache. */
    struct CpuImage {
        int w = 0;
        int h = 0;
        std::vector<uint8_t> rgba;
    };
    using CpuImageMap = std::unordered_map<std::string, CpuImage>;

    /** @brief Flatten an Assimp scene into a scene::NodeDesc tree (id = object id). */
    static scene::NodeDesc buildNodeDesc(const aiScene *scene, MeshSlotMap *slotsOut = nullptr);

    /** @brief Diff a mounted host tree against a freshly built NodeDesc tree. */
    static SceneDiff diffTree(scene::SceneHost *host, const scene::NodeDesc &newRoot);

    /**
     * @brief Apply a diff to the host arena in place. Creates/destroys Renderable3D for
     * added/removed mesh objects when `gfx` and `slots` are provided; otherwise
     * only the GameObject tree is mutated (useful for logic-only tests).
     */
    static bool applyTreeDiff(scene::SceneHost *host, const scene::NodeDesc &newRoot,
                              const SceneDiff &diff, graphics::Graphics *gfx,
                              const MeshSlotMap *slots);

    /**
     * @brief Fill node bounds from Assimp mesh AABBs (mesh GameObjects get the exact
     * local-space AABB; ancestors get the union of their children in local
     * space), so picking / frustum culling work on loaded models.
     */
    static void fillSceneBounds(scene::SceneHost *host, const MeshSlotMap &slots);

private:
    struct Loaded {
        std::string path;
        scene::SceneHost *host = nullptr;
        graphics::Graphics *gfx = nullptr;
        LoadOptions options;
        std::vector<graphics::Light3D *> lights;
        std::vector<graphics::Camera3D *> cameras;
        animation::AnimSkeleton *skeleton = nullptr;
        std::vector<animation::AnimClip *> clips;
    };

    struct DecodedScene {
        DecodedScene();
        ~DecodedScene();
        DecodedScene(DecodedScene &&) noexcept;
        DecodedScene &operator=(DecodedScene &&) noexcept;
        DecodedScene(const DecodedScene &) = delete;
        DecodedScene &operator=(const DecodedScene &) = delete;

        std::string path;
        model3d::ModelData *md = nullptr;
        std::unique_ptr<scene::NodeDesc> root;
        MeshSlotMap slots;
        LoadOptions options;
        CpuImageMap cpuImages;  // external textures pre-decoded off-thread
        std::vector<graphics::Light3D *> lights;
        std::vector<graphics::Camera3D *> cameras;
        animation::AnimSkeleton *skeleton = nullptr;
        std::vector<animation::AnimClip *> clips;
        bool prewarmOnly = false;
        std::function<void(scene::SceneHost *)> done;
    };

    bool decode(const std::string &path, const LoadOptions &options, DecodedScene *out);
    scene::SceneHost *mount(DecodedScene &d);
    /** @brief Attach shared Renderable3D to every mesh node that has none yet. */
    void linkMeshNodes(scene::SceneHost *host, const MeshSlotMap &slots,
                       graphics::Graphics *gfx, const LoadOptions &options,
                       TextureCache &textures, MeshCache &shared,
                       const CpuImageMap *predecoded = nullptr);
    /** @brief Release textures created by this loader (shared across scenes). */
    void clearTextures();
    /** @brief Reads an internal decode diagnostic while constructing a structured Result. */
    std::string decodeErrorFor(const std::string &path) const;

    std::unordered_map<std::string, Loaded> scenes_;
    std::unordered_map<std::string, DecodedScene> prewarmed_;
    TextureCache textures_;
    std::shared_ptr<thread::ThreadPool> pool_;
    std::vector<DecodedScene> pending_;
    std::unordered_set<std::string> inFlight_;
    // Internal async/decode diagnostics; public operations return structured Results.
    std::unordered_map<std::string, std::string> lastErrors_;
    std::unordered_map<std::string, std::vector<std::string>> warnings_;
    mutable std::mutex pendingMu_;
};

}  // namespace sceneloader
}  // namespace eve
