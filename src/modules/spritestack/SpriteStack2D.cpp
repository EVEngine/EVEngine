#include "spritestack/SpriteStack.h"

#include "common/Exception.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "image/ImageData.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::spritestack {
namespace {
float clamp01(float value) { return std::max(0.f, std::min(1.f, value)); }
}

void SpriteStack2D::setLayerCount(int count) {
    if (count < 0) throw eve::Exception("SpriteStack2D.setLayerCount: count must be >= 0");
    layerCount_ = count;
    layers_.assign(size_t(count), Layer{});
    bumpVersion();
}

int SpriteStack2D::getLayerCount() const { return layerCount_; }

void SpriteStack2D::setLayerTexture(graphics::Texture *texture, int index) {
    if (index < 0 || index >= layerCount_)
        throw eve::Exception("SpriteStack2D.setLayerTexture: index %d out of range [0,%d)", index,
                             layerCount_);
    layers_[size_t(index)] = Layer{texture, 0.f, 0.f, 1.f, 1.f};
    bumpVersion();
}

graphics::Texture *SpriteStack2D::getLayerTexture(int index) const {
    if (index < 0 || index >= layerCount_) return nullptr;
    return layers_[size_t(index)].texture;
}

void SpriteStack2D::setLayerImage(graphics::Graphics *gfx, image::ImageData *img, int index) {
    if (!gfx) throw eve::Exception("SpriteStack2D.setLayerImage: null graphics");
    if (!img) throw eve::Exception("SpriteStack2D.setLayerImage: null ImageData");
    setLayerTexture(gfx->newTextureFromImageData(img, false, false), index);
}

void SpriteStack2D::setLayerFile(graphics::Graphics *gfx, const std::string &path, int index) {
    if (!gfx) throw eve::Exception("SpriteStack2D.setLayerFile: null graphics");
    setLayerTexture(gfx->newTextureFromFile(path), index);
}

void SpriteStack2D::setLayersFromAtlas(graphics::Graphics *gfx, graphics::Texture *atlas,
                                       int layerCount) {
    if (!gfx) throw eve::Exception("SpriteStack2D.setLayersFromAtlas: null graphics");
    if (!atlas) throw eve::Exception("SpriteStack2D.setLayersFromAtlas: null atlas");
    if (layerCount <= 0) throw eve::Exception("SpriteStack2D.setLayersFromAtlas: count must be > 0");
    setLayerCount(layerCount);
    for (int i = 0; i < layerCount; ++i) {
        const float u0 = float(i) / float(layerCount);
        const float u1 = float(i + 1) / float(layerCount);
        layers_[size_t(i)] = Layer{atlas, u0, 0.f, u1, 1.f};
    }
    bumpVersion();
}

void SpriteStack2D::setThickness(float thickness) {
    if (thickness <= 0.f) throw eve::Exception("SpriteStack2D.setThickness: must be > 0");
    thickness_ = thickness;
    bumpVersion();
}
float SpriteStack2D::getThickness() const { return thickness_; }

void SpriteStack2D::setSize(float width, float height) {
    if (width <= 0.f || height <= 0.f) throw eve::Exception("SpriteStack2D.setSize: size must be > 0");
    width_ = width;
    height_ = height;
    bumpVersion();
}
float SpriteStack2D::getWidth() const { return width_; }
float SpriteStack2D::getHeight() const { return height_; }

void SpriteStack2D::setPosition(float x, float y) {
    x_ = x;
    y_ = y;
    bumpVersion();
}
void SpriteStack2D::setRotation(float degrees) {
    rotation_ = degrees;
    bumpVersion();
}
void SpriteStack2D::setTint(float r, float g, float b, float a) {
    tintR_ = clamp01(r);
    tintG_ = clamp01(g);
    tintB_ = clamp01(b);
    tintA_ = clamp01(a);
    bumpVersion();
}
void SpriteStack2D::setVisible(bool visible) { visible_ = visible; }
bool SpriteStack2D::getVisible() const { return visible_; }

void SpriteStack2D::setShadowEnabled(bool enabled) { shadowEnabled_ = enabled; }
bool SpriteStack2D::getShadowEnabled() const { return shadowEnabled_; }
void SpriteStack2D::setShadowOpacity(float opacity) { shadowOpacity_ = clamp01(opacity); }
void SpriteStack2D::setShadowOffset(float x, float y) {
    shadowOffsetX_ = x;
    shadowOffsetY_ = y;
}
void SpriteStack2D::setOutline(float width, float r, float g, float b) {
    outlineWidth_ = std::max(0.f, width);
    setOutlineColor(r, g, b);
}
float SpriteStack2D::getOutlineWidth() const { return outlineWidth_; }
void SpriteStack2D::setOutlineColor(float r, float g, float b) {
    outlineR_ = clamp01(r);
    outlineG_ = clamp01(g);
    outlineB_ = clamp01(b);
}

void SpriteStack2D::draw(graphics::Graphics *gfx, graphics::Camera2D *camera) const {
    if (!visible_ || layerCount_ <= 0) return;
    float cx = x_;
    float baseY = y_;
    float scale = 1.f;
    if (camera) {
        const float viewW = float(gfx->getWidth());
        const float viewH = float(gfx->getHeight());
        cx = camera->worldToScreenX(x_, y_, viewW, viewH);
        baseY = camera->worldToScreenY(x_, y_, viewW, viewH);
        scale = camera->getZoom();
    }
    const auto queueLayer = [&](const Layer &layer, float x, float y, float w, float h,
                                const graphics::Color &tint) {
        gfx->drawTexturedRectShaderUVRotated(layer.texture, nullptr, x, y, w, h, rotation_,
                                             layer.u0, layer.v0, layer.u1, layer.v1, tint);
    };
    if (shadowEnabled_) {
        const graphics::Color shadow(0.f, 0.f, 0.f, shadowOpacity_ * tintA_);
        for (int i = 0; i < layerCount_; ++i) {
            const Layer &layer = layers_[size_t(i)];
            if (!layer.texture) continue;
            queueLayer(layer, cx + shadowOffsetX_ * scale,
                       baseY + shadowOffsetY_ * scale - float(i) * thickness_ * scale,
                       width_ * scale, height_ * scale, shadow);
        }
    }
    if (outlineWidth_ > 0.f) {
        const graphics::Color outline(outlineR_, outlineG_, outlineB_, tintA_);
        for (int i = 0; i < layerCount_; ++i) {
            const Layer &layer = layers_[size_t(i)];
            if (!layer.texture) continue;
            queueLayer(layer, cx, baseY - float(i) * thickness_ * scale,
                       width_ * scale + outlineWidth_ * 2.f,
                       height_ * scale + outlineWidth_ * 2.f, outline);
        }
    }
    const graphics::Color tint(tintR_, tintG_, tintB_, tintA_);
    for (int i = 0; i < layerCount_; ++i) {
        const Layer &layer = layers_[size_t(i)];
        if (!layer.texture) continue;
        queueLayer(layer, cx, baseY - float(i) * thickness_ * scale,
                   width_ * scale, height_ * scale, tint);
    }
}

void SpriteStack2D::render(graphics::Graphics *gfx) const {
    if (!gfx) throw eve::Exception("SpriteStack2D.render: null graphics");
    draw(gfx, nullptr);
}
void SpriteStack2D::renderWithCamera(graphics::Graphics *gfx, graphics::Camera2D *camera) const {
    if (!gfx) throw eve::Exception("SpriteStack2D.renderWithCamera: null graphics");
    draw(gfx, camera);
}

void SpriteStackBatch::add(SpriteStack2D *stack) {
    if (!stack) throw eve::Exception("SpriteStackBatch.add: null stack");
    if (std::find(stacks_.begin(), stacks_.end(), stack) == stacks_.end()) stacks_.push_back(stack);
}
void SpriteStackBatch::remove(SpriteStack2D *stack) {
    stacks_.erase(std::remove(stacks_.begin(), stacks_.end(), stack), stacks_.end());
}
void SpriteStackBatch::clear() { stacks_.clear(); }
int SpriteStackBatch::getStackCount() const { return int(stacks_.size()); }
void SpriteStackBatch::render(graphics::Graphics *gfx) const {
    if (!gfx) throw eve::Exception("SpriteStackBatch.render: null graphics");
    renderWithCamera(gfx, nullptr);
}
void SpriteStackBatch::renderWithCamera(graphics::Graphics *gfx, graphics::Camera2D *camera) const {
    if (!gfx) throw eve::Exception("SpriteStackBatch.renderWithCamera: null graphics");
    for (SpriteStack2D *stack : stacks_) if (stack) stack->draw(gfx, camera);
}

Module_IMPL(SpriteStack, new SpriteStack());

SpriteStack2D *SpriteStack::newStack(graphics::Graphics *gfx) {
    if (!gfx) throw eve::Exception("SpriteStack.newStack: null graphics");
    return new SpriteStack2D();
}
SpriteStackBatch *SpriteStack::newBatch(graphics::Graphics *gfx) {
    if (!gfx) throw eve::Exception("SpriteStack.newBatch: null graphics");
    return new SpriteStackBatch();
}

std::vector<image::ImageData *> SpriteStack::slicePrimitive(const std::string &kind, int layerCount,
                                                             int imageW, int imageH,
                                                             const std::string &axis,
                                                             float thickness) {
    SliceOptions options;
    options.layerCount = layerCount;
    options.imageW = imageW;
    options.imageH = imageH;
    options.axis = axis;
    options.thickness = thickness;
    return slicePrimitiveToLayers(kind, options);
}

std::vector<image::ImageData *> SpriteStack::sliceModel(model3d::ModelData *model, int layerCount,
                                                         int imageW, int imageH,
                                                         const std::string &axis,
                                                         float thickness) {
    SliceOptions options;
    options.layerCount = layerCount;
    options.imageW = imageW;
    options.imageH = imageH;
    options.axis = axis;
    options.thickness = thickness;
    return sliceModelToLayers(model, options);
}

void SpriteStack::expose(ssq::Table &table) {
    auto cls = table.addClass(name, SpriteStack::create, false);
    expose(cls);
    auto stack = table.addClass<SpriteStack2D>(
        "SpriteStack2D", std::function<SpriteStack2D *()>([]() { return nullptr; }), true);
    stack.addFunc("setLayerCount", &SpriteStack2D::setLayerCount);
    stack.addFunc("getLayerCount", &SpriteStack2D::getLayerCount);
    stack.addFunc("setLayerTexture", &SpriteStack2D::setLayerTexture);
    stack.addFunc("getLayerTexture", &SpriteStack2D::getLayerTexture);
    stack.addFunc("setLayerImage", &SpriteStack2D::setLayerImage);
    stack.addFunc("setLayerFile", &SpriteStack2D::setLayerFile);
    stack.addFunc("setLayersFromAtlas", &SpriteStack2D::setLayersFromAtlas);
    stack.addFunc("setThickness", &SpriteStack2D::setThickness);
    stack.addFunc("getThickness", &SpriteStack2D::getThickness);
    stack.addFunc("setSize", &SpriteStack2D::setSize);
    stack.addFunc("getWidth", &SpriteStack2D::getWidth);
    stack.addFunc("getHeight", &SpriteStack2D::getHeight);
    stack.addFunc("setPosition", &SpriteStack2D::setPosition);
    stack.addFunc("setRotation", &SpriteStack2D::setRotation);
    stack.addFunc("setTint", &SpriteStack2D::setTint);
    stack.addFunc("setVisible", &SpriteStack2D::setVisible);
    stack.addFunc("getVisible", &SpriteStack2D::getVisible);
    stack.addFunc("setShadowEnabled", &SpriteStack2D::setShadowEnabled);
    stack.addFunc("getShadowEnabled", &SpriteStack2D::getShadowEnabled);
    stack.addFunc("setShadowOpacity", &SpriteStack2D::setShadowOpacity);
    stack.addFunc("setShadowOffset", &SpriteStack2D::setShadowOffset);
    stack.addFunc("setOutline", &SpriteStack2D::setOutline);
    stack.addFunc("getOutlineWidth", &SpriteStack2D::getOutlineWidth);
    stack.addFunc("setOutlineColor", &SpriteStack2D::setOutlineColor);
    stack.addFunc("render", &SpriteStack2D::render);
    stack.addFunc("renderWithCamera", &SpriteStack2D::renderWithCamera);

    auto batch = table.addClass<SpriteStackBatch>(
        "SpriteStackBatch", std::function<SpriteStackBatch *()>([]() { return nullptr; }), true);
    batch.addFunc("add", &SpriteStackBatch::add);
    batch.addFunc("remove", &SpriteStackBatch::remove);
    batch.addFunc("clear", &SpriteStackBatch::clear);
    batch.addFunc("getStackCount", &SpriteStackBatch::getStackCount);
    batch.addFunc("render", &SpriteStackBatch::render);
    batch.addFunc("renderWithCamera", &SpriteStackBatch::renderWithCamera);
}

void SpriteStack::expose(ssq::Class &cls) {
    cls.addFunc("getName", &SpriteStack::getName);
    cls.addFunc("newStack", &SpriteStack::newStack);
    cls.addFunc("newBatch", &SpriteStack::newBatch);
    cls.addFunc("slicePrimitive", &SpriteStack::slicePrimitive);
    cls.addFunc("sliceModel", &SpriteStack::sliceModel);
}

}  // namespace eve::spritestack
