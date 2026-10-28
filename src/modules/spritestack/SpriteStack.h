#pragma once
#include "common/Export.h"


#include "common/Module.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ssq {
class Class;
}

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

/** @brief Slice mesh to layers. */
std::vector<image::ImageData *> sliceMeshToLayers(const SliceInput &, const SliceOptions &);
/** @brief Assimp-backed bake; compiled only when the model3d module is linked. */
EVENGINE_API_WORLD std::vector<image::ImageData *> sliceModelToLayers(model3d::ModelData *, const SliceOptions &);
/** @brief Slice primitive to layers. */
EVENGINE_API_WORLD std::vector<image::ImageData *> slicePrimitiveToLayers(const std::string &, const SliceOptions &);

/**
 * @brief Pure-2D sprite stack made from horizontal RGBA cross-sections.
 *
 * Layers are drawn bottom-to-top through the normal 2D textured-quad path. Each
 * higher layer is offset upward by `thickness` 2D units. Rotation is applied in
 * the 2D plane; no Camera3D, depth buffer, G-buffer, mesh, or 3D pass is used.
 */
class EVENGINE_API_WORLD SpriteStack2D {
public:
    /** @brief Sprite stack 2 d. */
    SpriteStack2D() = default;
    SpriteStack2D(const SpriteStack2D &) = delete;
    SpriteStack2D &operator=(const SpriteStack2D &) = delete;

    /** @brief Sets the layer count. */
    void setLayerCount(int count);
    /** @brief Returns the layer count. */
    int getLayerCount() const;
    /** @brief Sets the layer texture. */
    void setLayerTexture(graphics::Texture *texture, int index);
    /** @brief Returns the layer texture. */
    graphics::Texture *getLayerTexture(int index) const;
    /** @brief Sets the layer image. */
    void setLayerImage(graphics::Graphics *gfx, image::ImageData *img, int index);
    /** @brief Sets the layer file. */
    void setLayerFile(graphics::Graphics *gfx, const std::string &path, int index);
    /** @brief Sets the layers from atlas. */
    void setLayersFromAtlas(graphics::Graphics *gfx, graphics::Texture *atlas, int layerCount);
    /** @brief Upward 2D offset between adjacent layers. */
    void setThickness(float thickness);
    /** @brief Returns the thickness. */
    float getThickness() const;
    /** @brief Display size of every layer in 2D units. */
    void setSize(float width, float height);
    /** @brief Returns the width. */
    float getWidth() const;
    /** @brief Returns the height. */
    float getHeight() const;
    /** @brief Sets the position. */
    void setPosition(float x, float y);
    /** @brief Rotate every slice around its common 2D center, in degrees. */
    void setRotation(float degrees);
    /** @brief Sets the tint. */
    void setTint(float r, float g, float b, float a = 1.f);
    /** @brief Sets the visible. */
    void setVisible(bool visible);
    /** @brief Returns the visible. */
    bool getVisible() const;
    /** @brief Draw a cheap 2D contact shadow behind the stack. */
    void setShadowEnabled(bool enabled);
    /** @brief Returns the shadow enabled. */
    bool getShadowEnabled() const;
    /** @brief Sets the shadow opacity. */
    void setShadowOpacity(float opacity);
    /** @brief Sets the shadow offset. */
    void setShadowOffset(float x, float y);
    /** @brief Draw enlarged dark copies behind the layers. */
    void setOutline(float width, float r = 0.f, float g = 0.f, float b = 0.f);
    /** @brief Returns the outline width. */
    float getOutlineWidth() const;
    /** @brief Sets the outline color. */
    void setOutlineColor(float r, float g, float b);
    /** @brief Returns the version. */
    uint64_t getVersion() const { return version_; }
    /** @brief Queue the stack into Graphics' normal 2D renderer. */
    void render(graphics::Graphics *gfx) const;
    /** @brief Renders with camera. */
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
class EVENGINE_API_WORLD SpriteStackBatch {
public:
    /** @brief Adds add. */
    void add(SpriteStack2D *stack);
    /** @brief Removes remove. */
    void remove(SpriteStack2D *stack);
    /** @brief Clears clear. */
    void clear();
    /** @brief Returns the stack count. */
    int getStackCount() const;
    /** @brief Renders render. */
    void render(graphics::Graphics *gfx) const;
    /** @brief Renders with camera. */
    void renderWithCamera(graphics::Graphics *gfx, graphics::Camera2D *camera) const;
private:
    std::vector<SpriteStack2D *> stacks_;
};

/** @brief SpriteStack module: horizontal slice baking plus pure-2D rendering. */
class EVENGINE_API_WORLD SpriteStack : public Module {
public:
    Module_REG(SpriteStack);
    /** @brief Sprite stack. */
    SpriteStack() = default;
    /** @brief Sprite stack. */
    ~SpriteStack() override = default;
    /** @brief Creates a stack. @ownership Caller deletes unless documented otherwise. */
    SpriteStack2D *newStack(graphics::Graphics *gfx);
    /** @brief Creates a batch. @ownership Caller deletes unless documented otherwise. */
    SpriteStackBatch *newBatch(graphics::Graphics *gfx);
    /** @brief Slice primitive. */
    std::vector<image::ImageData *> slicePrimitive(const std::string &kind, int layerCount,
                                                   int imageW, int imageH,
                                                   const std::string &axis, float thickness);
    /** @brief Slice model. */
    std::vector<image::ImageData *> sliceModel(model3d::ModelData *model, int layerCount,
                                               int imageW, int imageH, const std::string &axis,
                                               float thickness);
};

/** @brief Register sliceModel when SpriteStackModel.cpp is linked (model3d on). */
void exposeSpriteStackModelBindings(ssq::Class &cls);

}  // namespace eve::spritestack
