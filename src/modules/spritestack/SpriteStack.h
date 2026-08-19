#pragma once

#include "common/Module.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace eve::graphics {
class Camera3D;
class Graphics;
class Mesh;
class Shader;
class Texture;
}  // namespace eve::graphics

namespace eve::image {
class ImageData;
}  // namespace eve::image

namespace eve::model3d {
class ModelData;
}  // namespace eve::model3d

namespace eve::spritestack {

/**
 * CPU inputs for slicing a triangle mesh into sprite-stack layers.
 *
 * Positions are required (vertexCount * 3). Normals / per-vertex colors are
 * optional; colors (0..1 RGB) are averaged per triangle, shaded by the face
 * normal against the slice view direction, and multiplied by the tint.
 */
struct SliceInput {
    const float *posXYZ = nullptr;
    const float *nrmXYZ = nullptr;
    const float *rgb = nullptr;
    int vertexCount = 0;
    const uint32_t *indices = nullptr;
    int indexCount = 0;
};

/**
 * Slicer options. The mesh is cut into `layerCount` thin slabs along `axis`
 * ("x" | "y" | "z"); each slab is orthographically projected into an RGBA
 * image. Slab thickness defaults to AABB extent / layerCount when 0.
 */
struct SliceOptions {
    int layerCount = 16;
    int imageW = 128;
    int imageH = 128;
    std::string axis = "z";  // "x" | "y" | "z"
    float thickness = 0.f;   // world units per layer; 0 = auto from AABB
    float padding = 0.04f;   // margin around the model in the image plane
    bool shade = true;       // N*view shading per triangle
    float tintR = 1.f;
    float tintG = 1.f;
    float tintB = 1.f;
};

/**
 * Slice a triangle mesh into `layerCount` RGBA8 layer images (caller owns the
 * returned ImageData). Classic sprite stacking: vertical "bread slices" along
 * the chosen axis, later rendered as camera-facing billboards.
 */
std::vector<image::ImageData *> sliceMeshToLayers(const SliceInput &input,
                                                  const SliceOptions &options);

/** Slice every mesh of a decoded model (assimp scene) with the same options. */
std::vector<image::ImageData *> sliceModelToLayers(model3d::ModelData *model,
                                                   const SliceOptions &options);

/**
 * Build a procedural CPU mesh ("box" | "cylinder" | "sphere" | "cone") and
 * slice it — a self-contained asset-free path for tests / demos.
 */
std::vector<image::ImageData *> slicePrimitiveToLayers(const std::string &kind,
                                                       const SliceOptions &options);

/**
 * A renderable pseudo-3D sprite stack: a column of layer textures drawn as
 * alpha-blended slices inside the 3D forward pass (call render() after
 * gfx.render3D() / RenderSystem3D::render and before present).
 *
 * Modes:
 *   "vertical"   (default) — bread-slice billboards that always face the
 *                camera; yaw rotates the stack's depth layout around Y.
 *   "horizontal" — slices are horizontal quads (top-down layers); a 3/4
 *                camera sees true volume with yaw parallax.
 *
 * Textures are owned by Graphics; this object only holds pointers. Create via
 * eve.SpriteStack().newStack(gfx); the script VM owns the object.
 */
class SpriteStack3D {
public:
    SpriteStack3D() = default;
    ~SpriteStack3D();

    SpriteStack3D(const SpriteStack3D &) = delete;
    SpriteStack3D &operator=(const SpriteStack3D &) = delete;

    /** One slice instance with its baked world transform and layer UV rect. */
    struct SliceDraw {
        glm::mat4 model{1.f};
        glm::vec4 uv{0.f, 0.f, 1.f, 1.f};
        float distSq = 0.f;
        graphics::Texture *texture = nullptr;
    };

    void setLayerCount(int count);
    int getLayerCount() const;

    /** Null texture clears the slot (that layer is skipped). */
    void setLayerTexture(graphics::Texture *texture, int index);
    graphics::Texture *getLayerTexture(int index) const;

    /** Upload RGBA8 ImageData layers (convenience around setLayerTexture). */
    void setLayerImage(graphics::Graphics *gfx, image::ImageData *img, int index);
    /** Upload each path via Graphics::newTextureFromFile (reloads in place). */
    void setLayerFile(graphics::Graphics *gfx, const std::string &path, int index);
    /**
     * Split one horizontal atlas strip into `layerCount` layers (layer i =
     * columns [i/count..(i+1)/count)). The strip stays one GPU texture; slices
     * sample their cell through per-layer UV rects.
     */
    void setLayersFromAtlas(graphics::Graphics *gfx, graphics::Texture *atlas,
                            int layerCount);

    /** World-space spacing between consecutive slices. */
    void setThickness(float thickness);
    float getThickness() const;

    /** Quad size for every slice (world units). Default 1 x 1. */
    void setSize(float width, float height);
    float getWidth() const;
    float getHeight() const;

    void setPosition(float x, float y, float z);
    void setYaw(float radians);
    void setTint(float r, float g, float b, float a = 1.f);
    void setAlphaCutoff(float cutoff);
    void setVisible(bool visible);
    bool getVisible() const;
    void setMode(const std::string &mode);  // "vertical" | "horizontal"
    std::string getMode() const;

    /** Pseudo-3D projected contact shadow (soft dark silhouette on the ground). */
    void setShadowEnabled(bool enabled);
    bool getShadowEnabled() const;
    void setShadowOpacity(float opacity);
    void setShadowLight(float dx, float dy, float dz);
    void setShadowPlaneY(float y);

    /** Stylized rim outline: an expanded dark silhouette behind every slice. */
    void setOutline(float width, float r = 0.f, float g = 0.f, float b = 0.f);
    float getOutlineWidth() const;
    void setOutlineColor(float r, float g, float b);

    /**
     * Contribute this stack to the G-buffer (via RenderSystem3D's extra-drawer
     * hook) so post-processing that reads depth/normal (AO, outline) recognizes
     * the slice silhouettes instead of empty space.
     */
    void setGbufferEnabled(bool enabled);
    bool getGbufferEnabled() const;

    /**
     * Cast real CSM shadows: the stack's slices are drawn into the cascaded
     * shadow map through the alpha-cutout shadow pipeline, so the silhouette
     * (not a solid quad) lands on receiving geometry. Requires a directional
     * light with castShadow, or the legacy directional light fallback.
     */
    void setCastShadow(bool cast);
    bool getCastShadow() const;

    /** Monotonic revision counter; SpriteStackBatch uses it to detect changes. */
    uint64_t getVersion() const { return version_; }

    /**
     * Draw all slices into the currently open 3D scene pass (Vulkan). Uses the
     * active Camera3D when `camera` is null. Slices are sorted back-to-front
     * and depth-tested against the scene (depth writes stay off).
     */
    void render(graphics::Graphics *gfx, graphics::Camera3D *camera = nullptr) const;

private:
    void ensureResources(graphics::Graphics *gfx) const;

    void bumpVersion() { ++version_; }

    friend class SpriteStackBatch;

    static void collectSlices(const SpriteStack3D &stack, const glm::vec3 &eye,
                              std::vector<SliceDraw> &out);
    static SliceDraw makeShadowDraw(const SpriteStack3D &stack, const SliceDraw &s,
                                    const glm::vec3 &eye);
    static SliceDraw makeOutlineDraw(const SpriteStack3D &stack, const SliceDraw &s,
                                     const glm::vec3 &eye);
    static void drawGbufferStacks(graphics::Graphics &gfx, const glm::mat4 &viewProj,
                                  float eyeX, float eyeY, float eyeZ, float nearZ, float farZ);
    static void registerGbufferDrawer();
    static std::vector<SpriteStack3D *> &gbufferStacks();
    static void drawShadowCasterStacks(graphics::Graphics &gfx, const glm::mat4 &lightVP,
                                       float eyeX, float eyeY, float eyeZ);
    static void registerShadowDrawer();
    static std::vector<SpriteStack3D *> &shadowCasterStacks();

    struct Layer {
        graphics::Texture *texture = nullptr;
        glm::vec4 uv{0.f, 0.f, 1.f, 1.f};  // u0, v0, u1, v1
    };
    std::vector<Layer> layers_;
    int layerCount_ = 0;
    float thickness_ = 0.1f;
    float width_ = 1.f;
    float height_ = 1.f;
    float x_ = 0.f, y_ = 0.f, z_ = 0.f;
    float yaw_ = 0.f;
    float tintR_ = 1.f, tintG_ = 1.f, tintB_ = 1.f, tintA_ = 1.f;
    float alphaCutoff_ = 0.05f;
    bool visible_ = true;
    std::string mode_ = "vertical";
    bool shadowEnabled_ = false;
    float shadowOpacity_ = 0.32f;
    float shadowLightX_ = 0.35f, shadowLightY_ = -1.f, shadowLightZ_ = 0.25f;
    float shadowPlaneY_ = 0.f;
    float outlineWidth_ = 0.f;
    float outlineR_ = 0.f, outlineG_ = 0.f, outlineB_ = 0.f;
    bool gbufferEnabled_ = false;
    bool castShadow_ = false;
    uint64_t version_ = 1;

    // Lazy GPU resources (owned by Graphics).
    mutable graphics::Shader *shader_ = nullptr;
    mutable graphics::Mesh *quad_ = nullptr;
};

/**
 * Multi-stack batching: draws every visible slice of the registered stacks as
 * ONE draw call per (texture, tint) group (all slice transforms and UV rects
 * are baked into a shared mesh, uploaded in place via Graphics::updateMeshVertices).
 *
 * The combined mesh is rebuilt only when a registered stack changes (revision
 * counter); per-frame animated stacks should keep using SpriteStack3D::render.
 * Shadows / outlines are per-stack features and are not included in batches.
 */
class SpriteStackBatch {
public:
    SpriteStackBatch() = default;
    ~SpriteStackBatch() = default;

    SpriteStackBatch(const SpriteStackBatch &) = delete;
    SpriteStackBatch &operator=(const SpriteStackBatch &) = delete;

    void add(SpriteStack3D *stack);
    void remove(SpriteStack3D *stack);
    void clear();
    int getStackCount() const;

    void render(graphics::Graphics *gfx, graphics::Camera3D *camera = nullptr);

private:
    struct GroupKey {
        graphics::Texture *texture = nullptr;
        uint32_t tint = 0;  // packed RGBA
        bool operator==(const GroupKey &o) const {
            return texture == o.texture && tint == o.tint;
        }
    };
    struct GroupKeyHash {
        size_t operator()(const GroupKey &k) const {
            return std::hash<void *>()(k.texture) ^ (size_t(k.tint) * 0x9e3779b9u);
        }
    };

    struct Group {
        graphics::Mesh *mesh = nullptr;
        uint64_t stamp = 0;
        int vertexCapacity = 0;
        int indexCapacity = 0;
    };

    void ensureShader(graphics::Graphics *gfx);

    std::vector<SpriteStack3D *> stacks_;
    std::unordered_map<GroupKey, Group, GroupKeyHash> groups_;
    bool forceRebuild_ = true;  // add/remove/clear invalidate cached group meshes
    mutable graphics::Shader *shader_ = nullptr;
};

/**
 * SpriteStack module: CPU slicing + SpriteStack3D factory.
 *
 * Script: `spritestack <- eve.SpriteStack();`
 */
class SpriteStack : public Module {
public:
    Module_REG(SpriteStack);
    SpriteStack() = default;
    ~SpriteStack() override = default;

    /** New empty stack (caller/VM owns; set layers before rendering). */
    SpriteStack3D *newStack(graphics::Graphics *gfx);
    /** New empty batch (caller/VM owns). */
    SpriteStackBatch *newBatch(graphics::Graphics *gfx);

    std::vector<image::ImageData *> slicePrimitive(const std::string &kind, int layerCount,
                                                   int imageW, int imageH,
                                                   const std::string &axis, float thickness);
    std::vector<image::ImageData *> sliceModel(model3d::ModelData *model, int layerCount,
                                               int imageW, int imageH, const std::string &axis,
                                               float thickness);
};

}  // namespace eve::spritestack
