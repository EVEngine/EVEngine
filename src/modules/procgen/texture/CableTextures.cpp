#include "procgen/texture/CableTextures.h"

#include "image/ImageData.h"
#include "procgen/ParamSchema.h"
#include "procgen/texture/NoiseField.h"
#include "procgen/texture/PbrMaterial.h"
#include "procgen/texture/TextureRecipe.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace eve::procgen {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float smoothstep(float edge0, float edge1, float x) {
    if (edge0 == edge1) return x < edge0 ? 0.f : 1.f;
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

float mixf(float a, float b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return a + (b - a) * t;
}

struct Rgb {
    float r = 0.f, g = 0.f, b = 0.f;
};

Rgb mix(Rgb a, Rgb b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

void writePixel(image::ImageData& img, int x, int y, Rgb c) {
    img.setPixel(x, y, image::ImageData::Colorf{c.r, c.g, c.b, 1.f});
}

struct CableBake {
    std::unique_ptr<image::ImageData> albedo;
    std::vector<float>                height;
    bool                              seamless = true;
    int                               w        = 0;
    int                               h        = 0;
};

struct CablePbrDefaults {
    float roughnessLow   = 0.35f;
    float roughnessHigh  = 0.75f;
    float metallic       = 0.85f;
    float normalStrength = 5.f;
    float aoStrength     = 1.2f;
    float heightStrength = 1.f;
};

void appendPbrParams(RecipeDescriptor& schema, const CablePbrDefaults& d) {
    schema.params.push_back(
        ParamDescriptor::floating("roughnessLow", "Low Roughness", d.roughnessLow, 0.f, 1.f, 0.01f));
    schema.params.push_back(
        ParamDescriptor::floating("roughnessHigh", "High Roughness", d.roughnessHigh, 0.f, 1.f, 0.01f));
    schema.params.push_back(ParamDescriptor::floating("metallic", "Metallic", d.metallic, 0.f, 1.f, 0.01f));
    schema.params.push_back(
        ParamDescriptor::floating("normalStrength", "Normal Strength", d.normalStrength, 0.01f, 32.f, 0.05f));
    schema.params.push_back(
        ParamDescriptor::floating("aoStrength", "AO Strength", d.aoStrength, 0.f, 5.f, 0.05f));
    schema.params.push_back(
        ParamDescriptor::floating("heightStrength", "Height Strength", d.heightStrength, 0.f, 16.f, 0.05f));
}

std::unique_ptr<PbrTextureSet> assembleCablePbr(CableBake bake, const Params& params, const CablePbrDefaults& defaults,
                                                std::string& error) {
    if (!bake.albedo || bake.height.size() != size_t(bake.w) * size_t(bake.h)) {
        error = "cable bake missing albedo or height";
        return {};
    }
    const int   w        = bake.w;
    const int   h        = bake.h;
    const bool  seamless = bake.seamless;
    const float roughnessLow =
        std::clamp(params.getFloat("roughnessLow", defaults.roughnessLow), 0.f, 1.f);
    const float roughnessHigh =
        std::clamp(params.getFloat("roughnessHigh", defaults.roughnessHigh), 0.f, 1.f);
    const float metallic = std::clamp(params.getFloat("metallic", defaults.metallic), 0.f, 1.f);
    const float normalStrength =
        std::max(0.01f, params.getFloat("normalStrength", defaults.normalStrength));
    const float aoStrength = std::clamp(params.getFloat("aoStrength", defaults.aoStrength), 0.f, 5.f);
    const float heightStrength =
        std::max(0.f, params.getFloat("heightStrength", defaults.heightStrength));

    auto set    = std::make_unique<PbrTextureSet>();
    set->albedo = bake.albedo.release();
    set->normal = heightToNormalImage(bake.height, w, h, normalStrength, seamless).release();
    if (!set->normal) {
        error = "cable normal generation failed";
        return {};
    }

    std::vector<float> rough(size_t(w * h));
    std::vector<float> heightMap(size_t(w * h));
    std::vector<float> ao(size_t(w * h));
    const int          r = std::max(1, std::min(w, h) / 64);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i  = size_t(y) * size_t(w) + size_t(x);
            const float  hh = bake.height[i];
            // Ridges (high height) are smoother; grooves are rougher.
            rough[i]     = mixf(roughnessHigh, roughnessLow, hh);
            heightMap[i] = std::clamp(0.5f + (hh - 0.5f) * heightStrength, 0.f, 1.f);

            float sum = 0.f;
            int   cnt = 0;
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    int xx = x + dx, yy = y + dy;
                    if (seamless) {
                        xx = ((xx % w) + w) % w;
                        yy = ((yy % h) + h) % h;
                    } else {
                        if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                    }
                    sum += bake.height[size_t(yy) * size_t(w) + size_t(xx)];
                    ++cnt;
                }
            }
            const float local = cnt > 0 ? sum / float(cnt) : hh;
            ao[i] = std::clamp(1.f - aoStrength * std::max(0.f, local - hh), 0.f, 1.f);
        }
    }
    set->roughness = grayscaleImage(rough, w, h).release();
    set->metallic  = grayscaleImage(std::vector<float>(size_t(w * h), metallic), w, h).release();
    set->height    = grayscaleImage(heightMap, w, h).release();
    set->ao        = grayscaleImage(ao, w, h).release();
    if (!set->roughness || !set->metallic || !set->height || !set->ao) {
        error = "cable PBR map allocation failed";
        return {};
    }
    return set;
}

struct CommonOpts {
    int   strands  = 6;
    float twist    = 2.f;
    float gap      = 0.12f;
    float wear     = 0.25f;
    float contrast = 0.55f;
    int   colors   = 8;
    bool  seamless = true;
};

CommonOpts commonFromParams(const Params& params, int defaultStrands, float defaultTwist) {
    CommonOpts o;
    o.strands  = std::clamp(params.getInt("strands", defaultStrands), 2, 16);
    o.twist    = std::clamp(params.getFloat("twist", defaultTwist), 0.25f, 16.f);
    o.gap      = std::clamp(params.getFloat("gap", 0.12f), 0.f, 0.45f);
    o.wear     = std::clamp(params.getFloat("wear", 0.25f), 0.f, 1.f);
    o.contrast = std::clamp(params.getFloat("contrast", 0.55f), 0.f, 1.f);
    o.colors   = std::max(2, params.getInt("colors", 8));
    o.seamless = params.getInt("seamless", 1) != 0;
    return o;
}

/** Helical strand field: U wraps circumference, V runs along length. */
void strandField(float u, float v, const CommonOpts& o, float& height, float& strandId) {
    const float twisted = u * float(o.strands) + v * o.twist;
    const float cell    = twisted - std::floor(twisted);
    const float dist    = std::min(cell, 1.f - cell) * 2.f;  // 0 at edge, 1 at center
    const float ridge   = smoothstep(o.gap, 1.f, dist);
    height              = ridge;
    strandId            = std::floor(twisted);
}

CableBake bakeSteelCable(const Params& params, std::string& error) {
    const int w = std::clamp(params.getWidth() > 0 ? params.getWidth() : 256, 8, 4096);
    const int h = std::clamp(params.getHeight() > 0 ? params.getHeight() : 256, 8, 4096);
    CommonOpts o = commonFromParams(params, 6, 3.f);
    const float polish = std::clamp(params.getFloat("polish", 0.45f), 0.f, 1.f);
    NoiseField noise;
    noise.seed    = uint32_t(params.getSeed());
    noise.periodX = o.seamless ? 8 : 0;
    noise.periodY = o.seamless ? 8 : 0;

    CableBake bake;
    bake.w        = w;
    bake.h        = h;
    bake.seamless = o.seamless;
    bake.albedo   = std::make_unique<image::ImageData>(w, h, "RGBA8");
    bake.height.assign(size_t(w * h), 0.f);
    if (!bake.albedo) {
        error = "steel cable albedo allocation failed";
        return {};
    }

    const Rgb bright{0.72f, 0.74f, 0.76f};
    const Rgb mid{0.48f, 0.50f, 0.52f};
    const Rgb dark{0.28f, 0.30f, 0.32f};

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float u = (float(x) + 0.5f) / float(w);
            const float v = (float(y) + 0.5f) / float(h);
            float       hh = 0.f, sid = 0.f;
            strandField(u, v, o, hh, sid);
            const float n =
                noise.fbmPerlin(u * 6.f + sid * 0.17f, v * 4.f, 3, 2.f, 0.5f);
            const float scratch = noise.ridgedPerlin(u * 18.f, v * 2.f + sid, 2, 2.2f, 0.55f);
            float       shade   = mixf(0.35f, 1.f, hh);
            shade               = mixf(shade, shade * (0.85f + 0.3f * n), o.contrast);
            shade               = mixf(shade, shade * (1.f - 0.35f * scratch), o.wear);
            shade               = mixf(shade, mixf(shade, 1.f, polish * 0.25f), polish);
            Rgb col             = mix(dark, mix(mid, bright, shade), shade);
            if (o.colors < 64) {
                const float bands = float(o.colors);
                col.r             = std::floor(col.r * bands) / bands;
                col.g             = std::floor(col.g * bands) / bands;
                col.b             = std::floor(col.b * bands) / bands;
            }
            writePixel(*bake.albedo, x, y, col);
            bake.height[size_t(y) * size_t(w) + size_t(x)] =
                std::clamp(hh * 0.85f + n * 0.08f - scratch * 0.05f * o.wear, 0.f, 1.f);
        }
    }
    return bake;
}

CableBake bakeIronChain(const Params& params, std::string& error) {
    const int w = std::clamp(params.getWidth() > 0 ? params.getWidth() : 256, 8, 4096);
    const int h = std::clamp(params.getHeight() > 0 ? params.getHeight() : 256, 8, 4096);
    CommonOpts o = commonFromParams(params, 4, 1.5f);
    const float rust = std::clamp(params.getFloat("rust", 0.35f), 0.f, 1.f);
    NoiseField noise;
    noise.seed    = uint32_t(params.getSeed());
    noise.periodX = o.seamless ? 8 : 0;
    noise.periodY = o.seamless ? 8 : 0;

    CableBake bake;
    bake.w        = w;
    bake.h        = h;
    bake.seamless = o.seamless;
    bake.albedo   = std::make_unique<image::ImageData>(w, h, "RGBA8");
    bake.height.assign(size_t(w * h), 0.f);
    if (!bake.albedo) {
        error = "iron chain albedo allocation failed";
        return {};
    }

    const Rgb steel{0.42f, 0.44f, 0.46f};
    const Rgb dark{0.18f, 0.17f, 0.16f};
    const Rgb rustA{0.55f, 0.28f, 0.12f};
    const Rgb rustB{0.38f, 0.18f, 0.08f};

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float u = (float(x) + 0.5f) / float(w);
            const float v = (float(y) + 0.5f) / float(h);
            // Brushed metal with circumferential bands + sparse rivet pits.
            const float band = 0.5f + 0.5f * std::sin(u * 2.f * kPi * float(o.strands) + v * o.twist * kPi);
            const float n    = noise.fbmPerlin(u * 5.f, v * 5.f, 4, 2.f, 0.5f);
            const float pit  = smoothstep(0.72f, 0.92f, noise.ridgedPerlin(u * 14.f, v * 14.f, 2, 2.f, 0.5f));
            const float rustMask =
                smoothstep(0.55f, 0.9f, noise.fbm(u * 3.f, v * 3.f, 3, 2.f, 0.55f)) * rust;
            float hh = mixf(0.35f, 0.85f, band) * (1.f - pit * 0.55f);
            hh       = mixf(hh, hh * 0.7f, rustMask);
            Rgb col  = mix(dark, steel, mixf(0.4f, 1.f, band) * (0.75f + 0.25f * n));
            col      = mix(col, mix(rustB, rustA, n), rustMask);
            col      = mix(col, dark, pit * 0.65f);
            col.r    = mixf(col.r, col.r * (1.f - 0.2f * o.wear), o.wear);
            col.g    = mixf(col.g, col.g * (1.f - 0.2f * o.wear), o.wear);
            col.b    = mixf(col.b, col.b * (1.f - 0.15f * o.wear), o.wear);
            if (o.colors < 64) {
                const float bands = float(o.colors);
                col.r             = std::floor(col.r * bands) / bands;
                col.g             = std::floor(col.g * bands) / bands;
                col.b             = std::floor(col.b * bands) / bands;
            }
            writePixel(*bake.albedo, x, y, col);
            bake.height[size_t(y) * size_t(w) + size_t(x)] = std::clamp(hh, 0.f, 1.f);
        }
    }
    return bake;
}

CableBake bakeHempRope(const Params& params, std::string& error) {
    const int w = std::clamp(params.getWidth() > 0 ? params.getWidth() : 256, 8, 4096);
    const int h = std::clamp(params.getHeight() > 0 ? params.getHeight() : 256, 8, 4096);
    CommonOpts o = commonFromParams(params, 3, 2.5f);
    const float fiber = std::clamp(params.getFloat("fiber", 0.65f), 0.f, 1.f);
    NoiseField noise;
    noise.seed    = uint32_t(params.getSeed());
    noise.periodX = o.seamless ? 8 : 0;
    noise.periodY = o.seamless ? 8 : 0;

    CableBake bake;
    bake.w        = w;
    bake.h        = h;
    bake.seamless = o.seamless;
    bake.albedo   = std::make_unique<image::ImageData>(w, h, "RGBA8");
    bake.height.assign(size_t(w * h), 0.f);
    if (!bake.albedo) {
        error = "hemp rope albedo allocation failed";
        return {};
    }

    const Rgb light{0.72f, 0.58f, 0.34f};
    const Rgb mid{0.52f, 0.40f, 0.22f};
    const Rgb dark{0.32f, 0.24f, 0.12f};

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float u = (float(x) + 0.5f) / float(w);
            const float v = (float(y) + 0.5f) / float(h);
            float       hh = 0.f, sid = 0.f;
            strandField(u, v, o, hh, sid);
            const float n =
                noise.fbmPerlin(u * 10.f + sid, v * 18.f, 4, 2.1f, 0.5f);
            const float fluff = noise.valueNoise(u * 32.f, v * 40.f);
            float       shade = mixf(0.25f, 1.f, hh);
            shade             = mixf(shade, shade * (0.7f + 0.5f * n), fiber);
            shade             = mixf(shade, shade * (0.85f + 0.3f * fluff), o.wear * 0.5f + 0.2f);
            Rgb col           = mix(dark, mix(mid, light, shade), shade);
            // Occasional darker fiber flecks.
            if (fluff > 0.78f) col = mix(col, dark, 0.35f * fiber);
            if (o.colors < 64) {
                const float bands = float(o.colors);
                col.r             = std::floor(col.r * bands) / bands;
                col.g             = std::floor(col.g * bands) / bands;
                col.b             = std::floor(col.b * bands) / bands;
            }
            writePixel(*bake.albedo, x, y, col);
            bake.height[size_t(y) * size_t(w) + size_t(x)] =
                std::clamp(hh * 0.8f + n * 0.15f * fiber + fluff * 0.05f, 0.f, 1.f);
        }
    }
    return bake;
}

std::unique_ptr<image::ImageData> steelRecipe(const Params& params, std::string& error) {
    return bakeSteelCable(params, error).albedo;
}
std::unique_ptr<image::ImageData> ironRecipe(const Params& params, std::string& error) {
    return bakeIronChain(params, error).albedo;
}
std::unique_ptr<image::ImageData> hempRecipe(const Params& params, std::string& error) {
    return bakeHempRope(params, error).albedo;
}

void appendCommonParams(RecipeDescriptor& schema, int strands, float twist) {
    schema.params.push_back(ParamDescriptor::integer("strands", "Strand Count", strands, 2, 16));
    schema.params.push_back(ParamDescriptor::floating("twist", "Twist Rate", twist, 0.25f, 16.f, 0.05f));
    schema.params.push_back(ParamDescriptor::floating("gap", "Groove Width", 0.12f, 0.f, 0.45f, 0.01f));
    schema.params.push_back(ParamDescriptor::floating("wear", "Wear", 0.25f, 0.f, 1.f, 0.01f));
    schema.params.push_back(ParamDescriptor::floating("contrast", "Contrast", 0.55f, 0.f, 1.f, 0.01f));
    schema.params.push_back(ParamDescriptor::integer("colors", "Color Bands", 8, 2, 64));
    schema.params.push_back(ParamDescriptor::boolean("seamless", "Seamless", true));
}

RecipeDescriptor steelDescriptor() {
    RecipeDescriptor schema =
        RecipeDescriptor::grid("tex.cable.steel", "Steel Cable", "Cable", 8, 8, 4096, 4096);
    appendCommonParams(schema, 6, 3.f);
    schema.params.push_back(ParamDescriptor::floating("polish", "Polish", 0.45f, 0.f, 1.f, 0.01f));
    return schema;
}

RecipeDescriptor ironDescriptor() {
    RecipeDescriptor schema =
        RecipeDescriptor::grid("tex.chain.iron", "Iron Chain Metal", "Cable", 8, 8, 4096, 4096);
    appendCommonParams(schema, 4, 1.5f);
    schema.params.push_back(ParamDescriptor::floating("rust", "Rust", 0.35f, 0.f, 1.f, 0.01f));
    return schema;
}

RecipeDescriptor hempDescriptor() {
    RecipeDescriptor schema =
        RecipeDescriptor::grid("tex.rope.hemp", "Hemp Rope", "Cable", 8, 8, 4096, 4096);
    appendCommonParams(schema, 3, 2.5f);
    schema.params.push_back(ParamDescriptor::floating("fiber", "Fiber Detail", 0.65f, 0.f, 1.f, 0.01f));
    return schema;
}

CablePbrDefaults steelPbrDefaults() { return {0.42f, 0.22f, 0.92f, 6.5f, 1.15f, 1.1f}; }
CablePbrDefaults ironPbrDefaults() { return {0.55f, 0.30f, 0.78f, 5.5f, 1.35f, 1.05f}; }
CablePbrDefaults hempPbrDefaults() { return {0.82f, 0.55f, 0.02f, 4.5f, 1.2f, 1.2f}; }

}  // namespace

eve::Result<std::unique_ptr<image::ImageData>> generateSteelCableTexture(const Params& params) {
    std::string error;
    auto        img = steelRecipe(params, error);
    if (!img) {
        return eve::Result<std::unique_ptr<image::ImageData>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                   error.empty() ? "steel cable texture generation failed" : error, "recipe"));
    }
    return eve::Result<std::unique_ptr<image::ImageData>>::success(std::move(img));
}

eve::Result<std::unique_ptr<image::ImageData>> generateIronChainTexture(const Params& params) {
    std::string error;
    auto        img = ironRecipe(params, error);
    if (!img) {
        return eve::Result<std::unique_ptr<image::ImageData>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                   error.empty() ? "iron chain texture generation failed" : error, "recipe"));
    }
    return eve::Result<std::unique_ptr<image::ImageData>>::success(std::move(img));
}

eve::Result<std::unique_ptr<image::ImageData>> generateHempRopeTexture(const Params& params) {
    std::string error;
    auto        img = hempRecipe(params, error);
    if (!img) {
        return eve::Result<std::unique_ptr<image::ImageData>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                   error.empty() ? "hemp rope texture generation failed" : error, "recipe"));
    }
    return eve::Result<std::unique_ptr<image::ImageData>>::success(std::move(img));
}

eve::Result<std::unique_ptr<PbrTextureSet>> generateSteelCablePbr(const Params& params) {
    std::string error;
    CableBake   bake = bakeSteelCable(params, error);
    auto        set  = assembleCablePbr(std::move(bake), params, steelPbrDefaults(), error);
    if (!set) {
        return eve::Result<std::unique_ptr<PbrTextureSet>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                   error.empty() ? "steel cable PBR generation failed" : error, "recipe"));
    }
    return eve::Result<std::unique_ptr<PbrTextureSet>>::success(std::move(set));
}

eve::Result<std::unique_ptr<PbrTextureSet>> generateIronChainPbr(const Params& params) {
    std::string error;
    CableBake   bake = bakeIronChain(params, error);
    auto        set  = assembleCablePbr(std::move(bake), params, ironPbrDefaults(), error);
    if (!set) {
        return eve::Result<std::unique_ptr<PbrTextureSet>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                   error.empty() ? "iron chain PBR generation failed" : error, "recipe"));
    }
    return eve::Result<std::unique_ptr<PbrTextureSet>>::success(std::move(set));
}

eve::Result<std::unique_ptr<PbrTextureSet>> generateHempRopePbr(const Params& params) {
    std::string error;
    CableBake   bake = bakeHempRope(params, error);
    auto        set  = assembleCablePbr(std::move(bake), params, hempPbrDefaults(), error);
    if (!set) {
        return eve::Result<std::unique_ptr<PbrTextureSet>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                   error.empty() ? "hemp rope PBR generation failed" : error, "recipe"));
    }
    return eve::Result<std::unique_ptr<PbrTextureSet>>::success(std::move(set));
}

void registerCableTextureRecipes(TextureRecipeRegistry& registry) {
    registry.registerRecipe(steelDescriptor(), steelRecipe);
    registry.registerRecipe(ironDescriptor(), ironRecipe);
    registry.registerRecipe(hempDescriptor(), hempRecipe);
}

void registerCablePbrRecipes(PbrRecipeRegistry& registry) {
    RecipeDescriptor steel = steelDescriptor();
    steel.id               = "pbr.cable.steel";
    steel.displayName      = "Steel Cable PBR";
    steel.category         = "Material";
    appendPbrParams(steel, steelPbrDefaults());
    registry.registerPbrRecipe(std::move(steel), [](const Params& params, std::string& error) {
        CableBake bake = bakeSteelCable(params, error);
        return assembleCablePbr(std::move(bake), params, steelPbrDefaults(), error);
    });

    RecipeDescriptor iron = ironDescriptor();
    iron.id               = "pbr.chain.iron";
    iron.displayName      = "Iron Chain PBR";
    iron.category         = "Material";
    appendPbrParams(iron, ironPbrDefaults());
    registry.registerPbrRecipe(std::move(iron), [](const Params& params, std::string& error) {
        CableBake bake = bakeIronChain(params, error);
        return assembleCablePbr(std::move(bake), params, ironPbrDefaults(), error);
    });

    RecipeDescriptor hemp = hempDescriptor();
    hemp.id               = "pbr.rope.hemp";
    hemp.displayName      = "Hemp Rope PBR";
    hemp.category         = "Material";
    appendPbrParams(hemp, hempPbrDefaults());
    registry.registerPbrRecipe(std::move(hemp), [](const Params& params, std::string& error) {
        CableBake bake = bakeHempRope(params, error);
        return assembleCablePbr(std::move(bake), params, hempPbrDefaults(), error);
    });
}

}  // namespace eve::procgen
