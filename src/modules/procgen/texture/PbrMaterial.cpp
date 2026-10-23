#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/CableTextures.h"
#include "procgen/texture/ColorRamp.h"
#include "procgen/texture/EverydayMaterials.h"
#include "procgen/texture/FloorTextures.h"
#include "procgen/texture/NoiseField.h"

#include "image/ImageData.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::procgen {

PbrTextureSet::~PbrTextureSet() { destroy(); }

void PbrTextureSet::destroy() {
    delete albedo;
    delete normal;
    delete roughness;
    delete metallic;
    delete height;
    delete ao;
    albedo = normal = roughness = metallic = height = ao = nullptr;
}

PbrRecipeRegistry &PbrRecipeRegistry::instance() {
    static PbrRecipeRegistry reg;
    return reg;
}

void PbrRecipeRegistry::registerPbrRecipe(const std::string &id, PbrRecipeFn fn) {
    RecipeDescriptor descriptor;
    descriptor.id = id;
    descriptor.displayName = id;
    registerPbrRecipe(std::move(descriptor), std::move(fn));
}

void PbrRecipeRegistry::registerPbrRecipe(RecipeDescriptor descriptor, PbrRecipeFn fn) {
    const std::string id = descriptor.id;
    recipes_[id] = Entry{std::move(fn), std::move(descriptor)};
}

bool PbrRecipeRegistry::has(const std::string &id) const {
    return recipes_.find(id) != recipes_.end();
}

std::unique_ptr<PbrTextureSet> PbrRecipeRegistry::generate(const std::string &id, const Params &params,
                                                           std::string &error) const {
    auto it = recipes_.find(id);
    if (it == recipes_.end()) {
        error = "unknown pbr recipe: " + id;
        return {};
    }
    return it->second.fn(params, error);
}

const RecipeDescriptor *PbrRecipeRegistry::descriptor(const std::string &id) const {
    const auto it = recipes_.find(id);
    return it == recipes_.end() ? nullptr : &it->second.descriptor;
}

bool PbrRecipeRegistry::applyDefaults(const std::string &id, Params &params) const {
    const RecipeDescriptor *schema = descriptor(id);
    if (!schema) return false;
    schema->applyDefaults(params);
    return true;
}

std::vector<std::string> PbrRecipeRegistry::list() const {
    std::vector<std::string> ids;
    ids.reserve(recipes_.size());
    for (const auto &kv : recipes_) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}

void PbrRecipeRegistry::registerPbrBuiltins() {
    if (builtinsRegistered_) return;
    for (const TextureRecipeDef &def : builtinTextureDefs()) {
        const std::string id = "pbr." + def.id.substr(def.id.find('.') + 1);
        RecipeDescriptor descriptor = makeTextureRecipeDescriptor(def);
        descriptor.id = id;
        descriptor.category = "Material";
        descriptor.params.push_back(ParamDescriptor::floating("roughnessLow", "Low Roughness", def.pbr.roughnessLow, 0.f, 1.f, 0.01f));
        descriptor.params.push_back(ParamDescriptor::floating("roughnessHigh", "High Roughness", def.pbr.roughnessHigh, 0.f, 1.f, 0.01f));
        descriptor.params.push_back(ParamDescriptor::floating("metallic", "Metallic", def.pbr.metallic, 0.f, 1.f, 0.01f));
        descriptor.params.push_back(ParamDescriptor::floating("normalStrength", "Normal Strength", def.pbr.normalStrength, 0.01f, 32.f, 0.05f));
        descriptor.params.push_back(ParamDescriptor::floating("aoStrength", "AO Strength", def.pbr.aoStrength, 0.f, 5.f, 0.05f));
        descriptor.params.push_back(ParamDescriptor::floating("heightStrength", "Height Strength", def.pbr.heightStrength, 0.f, 16.f, 0.05f));
        registerPbrRecipe(std::move(descriptor), [def](const Params &params, std::string &error) {
            return generatePbrSet(def, params, error);
        });
    }
    registerFloorPbrRecipes(*this);
    registerCablePbrRecipes(*this);
    // Existing registry/script entry points project the canonical Result factory.
    const char *names[] = {"wood",  "metal",    "rubber",    "cloth",  "plastic", "steel", "glass",
                           "paper", "aluminum", "cast_iron", "cement", "paint",   "marble"};
    for (size_t i = 0; i < 13; ++i) {
        auto schema =
            RecipeDescriptor::grid(std::string("pbr.everyday.") + names[i], names[i], "Everyday Material", 1, 1);
        for (auto &parameter : schema.params) {
            if (parameter.key == "width" || parameter.key == "height") parameter.maximum = 2048;
        }
        schema.params.push_back(ParamDescriptor::floating("scale", "Pattern Repeats", 4.f, 1.f, 32.f, 1.f));
        schema.params.push_back(ParamDescriptor::floating("detail", "Surface Detail", .5f, 0.f, 1.f, .01f));
        schema.params.push_back(ParamDescriptor::floating("finish", "Polish", .5f, 0.f, 1.f, .01f));
        schema.params.push_back(ParamDescriptor::floating("variation", "Color Variation", .4f, 0.f, 1.f, .01f));
        registerPbrRecipe(std::move(schema),
                          [family = static_cast<EverydayMaterial>(i)](const Params &p, std::string &error) {
                              auto result = EverydayMaterials::generate(family, p);
                              if (!result.ok()) {
                                  error = "Everyday material generation failed: unsupported family or size";
                                  return std::unique_ptr<PbrTextureSet>{};
                              }
                              error.clear();
                              return std::move(result).takeValue();
                          });
    }
    builtinsRegistered_ = true;
}

std::unique_ptr<image::ImageData> grayscaleImage(const std::vector<float> &values, int w, int h) {
    auto  img    = std::make_unique<image::ImageData>(w, h, "RGBA8");
    auto *pixels = static_cast<uint8_t *>(img->getData());
    for (int i = 0; i < w * h && i < int(values.size()); ++i) {
        const uint8_t v = uint8_t(std::lround(std::clamp(values[size_t(i)], 0.f, 1.f) * 255.f));
        const size_t  o = size_t(i) * 4u;
        pixels[o + 0] = pixels[o + 1] = pixels[o + 2] = v;
        pixels[o + 3]                                  = 255;
    }
    return img;
}

std::unique_ptr<PbrTextureSet> generatePbrSet(const TextureRecipeDef &def, const Params &params, std::string &error) {
    const auto ctx = TextureGenContext::fromParams(params);
    if (ctx.width > 4096 || ctx.height > 4096) {
        error = "texture size too large (max 4096)";
        return {};
    }

    // Recipe defaults, then per-call overrides.
    PbrParams pbr = def.pbr;
    pbr.roughnessLow   = std::clamp(params.getFloat("roughnessLow", pbr.roughnessLow), 0.f, 1.f);
    pbr.roughnessHigh  = std::clamp(params.getFloat("roughnessHigh", pbr.roughnessHigh), 0.f, 1.f);
    pbr.metallic       = std::clamp(params.getFloat("metallic", pbr.metallic), 0.f, 1.f);
    pbr.normalStrength = std::max(0.01f, params.getFloat("normalStrength", pbr.normalStrength));
    pbr.aoStrength     = std::clamp(params.getFloat("aoStrength", pbr.aoStrength), 0.f, 5.f);
    pbr.heightStrength = std::max(0.f, params.getFloat("heightStrength", pbr.heightStrength));

    std::vector<float> height;
    fillHeightField(ctx, def.height, height);
    const int w = ctx.width;
    const int h = ctx.height;

    auto set    = std::make_unique<PbrTextureSet>();
    set->albedo = new image::ImageData(w, h, "RGBA8");
    std::vector<float> pigment;
    if (def.colorField) fillHeightField(ctx, def.colorField, pigment);
    paintHeightToImage(*set->albedo, def.colorField ? pigment : height, w, h, def.albedo, ctx.colors, ctx.pixelSize);
    set->normal = heightToNormalImage(height, w, h, pbr.normalStrength, ctx.seamless).release();

    std::vector<float> microfacet;
    if (def.roughnessField) fillHeightField(ctx, def.roughnessField, microfacet);
    std::vector<float> rough(size_t(w * h));
    std::vector<float> heightMap(size_t(w * h));
    std::vector<float> ao(size_t(w * h));
    const int r = std::max(1, std::min(w, h) / 64);  // cavity sampling radius
    // Separable sliding box mean: O(w*h), including wrapped or clipped boundaries.
    // This is the same cavity footprint as the original nested neighbourhood scan.
    std::vector<float> horizontal(height.size()), neighbourhood(height.size());
    auto               wrap = [](int coordinate, int extent) { return (coordinate % extent + extent) % extent; };
    for (int y = 0; y < h; ++y) {
        double sum    = 0.0;
        int    count  = 0;
        auto   update = [&](int x, int sign) {
            if (!ctx.seamless && (x < 0 || x >= w)) return;
            sum += sign * double(height[size_t(y) * size_t(w) + size_t(wrap(x, w))]);
            count += sign;
        };
        for (int x = -r; x <= r; ++x) update(x, 1);
        for (int x = 0; x < w; ++x) {
            horizontal[size_t(y) * size_t(w) + size_t(x)] = float(sum / count);
            update(x - r, -1);
            update(x + r + 1, 1);
        }
    }
    for (int x = 0; x < w; ++x) {
        double sum    = 0.0;
        int    count  = 0;
        auto   update = [&](int y, int sign) {
            if (!ctx.seamless && (y < 0 || y >= h)) return;
            sum += sign * double(horizontal[size_t(wrap(y, h)) * size_t(w) + size_t(x)]);
            count += sign;
        };
        for (int y = -r; y <= r; ++y) update(y, 1);
        for (int y = 0; y < h; ++y) {
            neighbourhood[size_t(y) * size_t(w) + size_t(x)] = float(sum / count);
            update(y - r, -1);
            update(y + r + 1, 1);
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            const float  hh = height[i];
            rough[size_t(i)] =
                pbr.roughnessLow + (pbr.roughnessHigh - pbr.roughnessLow) * (def.roughnessField ? microfacet[i] : hh);
            heightMap[i] = std::clamp(0.5f + (hh - 0.5f) * pbr.heightStrength, 0.f, 1.f);

            const float local = neighbourhood[i];
            ao[i] = std::clamp(1.f - pbr.aoStrength * std::max(0.f, local - hh), 0.f, 1.f);
        }
    }
    set->roughness = grayscaleImage(rough, w, h).release();
    set->metallic  = grayscaleImage(std::vector<float>(size_t(w * h), pbr.metallic), w, h).release();
    set->height    = grayscaleImage(heightMap, w, h).release();
    set->ao        = grayscaleImage(ao, w, h).release();
    return set;
}

}  // namespace eve::procgen
