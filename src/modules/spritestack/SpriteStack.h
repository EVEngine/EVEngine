#pragma once

#include "common/Module.h"

#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics {
class Camera2D;
class Graphics;
class Texture;
}
namespace eve::image { class ImageData; }
namespace eve::model3d { class ModelData; }

namespace eve::spritestack {

/** @brief CPU triangle-mesh input used to bake horizontal RGBA sprite slices. */
struct SliceInput {
    const float *posXYZ = nullptr;
    const float *nrmXYZ = nullptr;
    const float *rgb = nullptr;
    int vertexCount = 0;
    const uint32_t *indices = nullptr;
    int indexCount = 0;
};

/** @brief Options for baking a mesh into equal-sized RGBA sprite layers. */
struct SliceOptions {
    int layerCount = 16;
    int imageW = 128;
    int imageH = 128;
    std::string axis = "y";
    float thickness = 0.f;
    float padding = 0.04f;
    bool shade = true;
    float tintR = 1.f, tintG = 1.f, tintB = 1.f;
};

std::vector<image::ImageData *> sliceMeshToLayers(const SliceInput &, const SliceOptions &);
std::vector<image::ImageData *> sliceModelToLayers(model3d::ModelData *, const SliceOptions &);
std::vector<image::ImageData *> slicePrimitiveToLayers(const std::string &, const SliceOptions &);

/**
 * @brief Pure-2D sprite stack made from horizontal RGBA cross-sections.
 *
 * Layers are drawn bottom-to-top through the normal 2D textured-quad path. Each
 * higher layer is offset upward by `thickness` 2D units. Rotation is applied in
 * the 2D plane; no Camera3D, depth buffer, G-buffer, mesh, or 3D pass is used.
 */
class SpriteStack2D {
public:
    SpriteStack2D() = default;
    SpriteStack2D(const SpriteStack2D &) = delete;
    SpriteStack2D &operator=(const SpriteStack2D &) = delete;

    void setLayerCount(int count);
    int getLayerCount() const;
    void setLayerTexture(graphics::Texture *texture, int index);
    graphics::Texture *getLayerTexture(int index) const;
    void setLayerImage(graphics::Graphics *gfx, image::ImageData *img, int index);
    void setLayerFile(graphics::Graphics *gfx, const std::string &path, int index);
    void setLayersFromAtlas(graphics::Graphics *gfx, graphics::Texture *atlas, int layerCount);
    /** @brief Upward 2D offset between adjacent layers. */
    void setThickness(float thickness);
    float getThickness() const;
    /** @brief Display size of every layer in 2D units. */
    void setSize(float width, float height);
    float getWidth() const;
    float getHeight() const;
    void setPosition(float x, float y);
    /** @brief Rotate every slice around its common 2D center, in degrees. */
    void setRotation(float degrees);
    void setTint(float r, float g, float b, float a = 1.f);
    void setVisible(bool visible);
    bool getVisible() const;
    /** @brief Draw a cheap 2D contact shadow behind the stack. */
    void setShadowEnabled(bool enabled);
    bool getShadowEnabled() const;
    void setShadowOpacity(float opacity);
    void setShadowOffset(float x, float y);
    /** @brief Draw enlarged dark copies behind the layers. */
    void setOutline(float width, float r = 0.f, float g = 0.f, float b = 0.f);
    float getOutlineWidth() const;
    void setOutlineColor(float r, float g, float b);
    uint64_t getVersion() const { return version_; }
    /** @brief Queue the stack into Graphics' normal 2D renderer. */
    void render(graphics::Graphics *gfx) const;
    void renderWithCamera(graphics::Graphics *gfx, graphics::Camera2D *camera) const;

private:
    friend class SpriteStackBatch;
    struct Layer {
        graphics::Texture *texture = nullptr;
        float u0 = 0.f, v0 = 0.f, u1 = 1.f, v1 = 1.f;
    };
    void draw(graphics::Graphics *gfx, graphics::Camera2D *camera) const;
    void bumpVersion() { ++version_; }
    std::vector<Layer> layers_;
    int layerCount_ = 0;
    float thickness_ = 1.f, width_ = 64.f, height_ = 64.f;
    float x_ = 0.f, y_ = 0.f, rotation_ = 0.f;
    float tintR_ = 1.f, tintG_ = 1.f, tintB_ = 1.f, tintA_ = 1.f;
    bool visible_ = true, shadowEnabled_ = false;
    float shadowOpacity_ = 0.3f, shadowOffsetX_ = 5.f, shadowOffsetY_ = 4.f;
    float outlineWidth_ = 0.f, outlineR_ = 0.f, outlineG_ = 0.f, outlineB_ = 0.f;
    uint64_t version_ = 1;
};

/** @brief Collection of 2D stacks; Graphics performs texture batching. */
class SpriteStackBatch {
public:
    void add(SpriteStack2D *stack);
    void remove(SpriteStack2D *stack);
    void clear();
    int getStackCount() const;
    void render(graphics::Graphics *gfx) const;
    void renderWithCamera(graphics::Graphics *gfx, graphics::Camera2D *camera) const;
private:
    std::vector<SpriteStack2D *> stacks_;
};

/** @brief SpriteStack module: horizontal slice baking plus pure-2D rendering. */
class SpriteStack : public Module {
public:
    Module_REG(SpriteStack);
    SpriteStack() = default;
    ~SpriteStack() override = default;
    SpriteStack2D *newStack(graphics::Graphics *gfx);
    SpriteStackBatch *newBatch(graphics::Graphics *gfx);
    std::vector<image::ImageData *> slicePrimitive(const std::string &kind, int layerCount,
                                                   int imageW, int imageH,
                                                   const std::string &axis, float thickness);
    std::vector<image::ImageData *> sliceModel(model3d::ModelData *model, int layerCount,
                                               int imageW, int imageH, const std::string &axis,
                                               float thickness);
};

}  // namespace eve::spritestack
