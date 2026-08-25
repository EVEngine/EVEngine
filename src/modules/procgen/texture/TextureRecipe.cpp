#include "procgen/texture/TextureRecipe.h"

#include "image/ImageData.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace eve::procgen {

TextureRecipeRegistry &TextureRecipeRegistry::instance() {
    static TextureRecipeRegistry reg;
    return reg;
}

void TextureRecipeRegistry::registerRecipe(const std::string &id, TextureRecipeFn fn) {
    RecipeDescriptor descriptor;
    descriptor.id = id;
    descriptor.displayName = id;
    registerRecipe(std::move(descriptor), std::move(fn));
}

void TextureRecipeRegistry::registerRecipe(RecipeDescriptor descriptor, TextureRecipeFn fn) {
    const std::string id = descriptor.id;
    recipes_[id] = Entry{std::move(fn), std::move(descriptor)};
}

bool TextureRecipeRegistry::has(const std::string &id) const {
    return recipes_.find(id) != recipes_.end();
}

image::ImageData *TextureRecipeRegistry::generate(const std::string &id, const Params &params,
                                                  std::string &error) const {
    auto it = recipes_.find(id);
    if (it == recipes_.end()) {
        error = "unknown texture recipe: " + id;
        return nullptr;
    }
    return it->second.fn(params, error);
}

const RecipeDescriptor *TextureRecipeRegistry::descriptor(const std::string &id) const {
    const auto it = recipes_.find(id);
    return it == recipes_.end() ? nullptr : &it->second.descriptor;
}

bool TextureRecipeRegistry::applyDefaults(const std::string &id, Params &params) const {
    const RecipeDescriptor *schema = descriptor(id);
    if (!schema) return false;
    schema->applyDefaults(params);
    return true;
}

std::vector<std::string> TextureRecipeRegistry::list() const {
    std::vector<std::string> ids;
    ids.reserve(recipes_.size());
    for (const auto &kv : recipes_) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}

TextureGenContext TextureGenContext::fromParams(const Params &params) {
    TextureGenContext ctx;
    ctx.width     = std::max(1, params.getWidth());
    ctx.height    = std::max(1, params.getHeight());
    ctx.seed      = params.getSeed();
    ctx.scale     = std::max(0.05f, params.getFloat("scale", 4.f));
    ctx.octaves   = std::max(1, params.getInt("octaves", 4));
    ctx.colors    = std::max(2, params.getInt("colors", 6));
    ctx.pixelSize = std::max(1, params.getInt("pixelSize", 1));
    ctx.seamless  = params.getInt("seamless", 1) != 0;
    ctx.warpAmp   = params.getFloat("warp", 1.f);
    return ctx;
}

PbrParams PbrParams::fromParams(const Params &params) {
    PbrParams p;
    p.roughnessLow   = std::clamp(params.getFloat("roughnessLow", p.roughnessLow), 0.f, 1.f);
    p.roughnessHigh  = std::clamp(params.getFloat("roughnessHigh", p.roughnessHigh), 0.f, 1.f);
    p.metallic       = std::clamp(params.getFloat("metallic", p.metallic), 0.f, 1.f);
    p.normalStrength = std::max(0.01f, params.getFloat("normalStrength", p.normalStrength));
    p.aoStrength     = std::clamp(params.getFloat("aoStrength", p.aoStrength), 0.f, 5.f);
    p.heightStrength = std::max(0.f, params.getFloat("heightStrength", p.heightStrength));
    return p;
}

RecipeDescriptor makeTextureRecipeDescriptor(const TextureRecipeDef &definition) {
    std::string displayName = definition.id;
    const size_t dot = displayName.find('.');
    if (dot != std::string::npos) displayName.erase(0, dot + 1);
    for (char &ch : displayName) {
        if (ch == '_') ch = ' ';
    }
    if (!displayName.empty())
        displayName[0] = char(std::toupper(static_cast<unsigned char>(displayName[0])));

    RecipeDescriptor descriptor = RecipeDescriptor::grid(definition.id, displayName, "Texture", 1, 1);
    descriptor.params.push_back(ParamDescriptor::floating("scale", "Scale", 4.f, 0.05f, 256.f, 0.05f));
    descriptor.params.push_back(ParamDescriptor::integer("octaves", "Octaves", 4, 1, 16));
    descriptor.params.push_back(ParamDescriptor::integer("colors", "Color Bands", 6, 2, 64));
    descriptor.params.push_back(ParamDescriptor::integer("pixelSize", "Pixel Size", 1, 1, 64));
    descriptor.params.push_back(ParamDescriptor::boolean("seamless", "Seamless", true));
    descriptor.params.push_back(ParamDescriptor::floating("warp", "Warp", 1.f, 0.f, 32.f, 0.05f));
    return descriptor;
}

void fillHeightField(const TextureGenContext &ctx,
                     const std::function<float(float, float, const NoiseField &)> &fn,
                     std::vector<float> &height) {
    height.resize(size_t(ctx.width) * size_t(ctx.height));
    NoiseField n;
    n.seed = ctx.seed;
    if (ctx.seamless) {
        n.periodX = std::max(1, int(std::lround(ctx.scale)));
        n.periodY = std::max(1, int(std::lround(ctx.scale * float(ctx.height) /
                                                float(std::max(1, ctx.width)))));
    }
    for (int y = 0; y < ctx.height; ++y) {
        for (int x = 0; x < ctx.width; ++x) {
            const float u = float(x) / float(ctx.width) * ctx.scale;
            const float v = float(y) / float(ctx.height) * ctx.scale;
            height[size_t(y * ctx.width + x)] = std::clamp(fn(u, v, n), 0.f, 1.f);
        }
    }
}

void paintHeightToImage(image::ImageData &img, const std::vector<float> &height, int w, int h,
                        const ColorRamp &ramp, int bands, int pixelSize) {
    auto *pixels = static_cast<uint8_t *>(img.getData());
    if (!pixels || int(height.size()) < w * h) return;
    pixelSize = std::max(1, pixelSize);
    for (int y = 0; y < h; ++y) {
        const int by = (y / pixelSize) * pixelSize;
        for (int x = 0; x < w; ++x) {
            const int   bx = (x / pixelSize) * pixelSize;
            const float t  = height[size_t(by * w + bx)];
            const Rgba8 c  = ramp.sampleBanded(t, bands);
            const size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4u;
            pixels[i + 0]  = c.r;
            pixels[i + 1]  = c.g;
            pixels[i + 2]  = c.b;
            pixels[i + 3]  = c.a;
        }
    }
}

image::ImageData *heightToNormalImage(const std::vector<float> &height, int w, int h,
                                      float strength, bool seamless) {
    auto *img    = new image::ImageData(w, h, "RGBA8");
    auto *pixels = static_cast<uint8_t *>(img->getData());
    auto  sample = [&](int x, int y) -> float {
        if (seamless) {
            x = (x % w + w) % w;
            y = (y % h + h) % h;
        } else {
            x = std::clamp(x, 0, w - 1);
            y = std::clamp(y, 0, h - 1);
        }
        return height[size_t(y * w + x)];
    };
    strength = std::max(0.01f, strength);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float dx = (sample(x + 1, y) - sample(x - 1, y)) * strength;
            const float dy = (sample(x, y + 1) - sample(x, y - 1)) * strength;
            float       nx = -dx, ny = -dy, nz = 1.f;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-6f) {
                nx /= len;
                ny /= len;
                nz /= len;
            }
            const size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4u;
            pixels[i + 0]  = uint8_t(std::lround((nx * 0.5f + 0.5f) * 255.f));
            pixels[i + 1]  = uint8_t(std::lround((ny * 0.5f + 0.5f) * 255.f));
            pixels[i + 2]  = uint8_t(std::lround((nz * 0.5f + 0.5f) * 255.f));
            pixels[i + 3]  = 255;
        }
    }
    return img;
}

}  // namespace eve::procgen
