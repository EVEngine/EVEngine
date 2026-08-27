#include "procgen/texture/TextureRecipe.h"
#include "procgen/texture/NoiseField.h"
#include "procgen/texture/ColorRamp.h"
#include "procgen/texture/CloudField.h"
#include "procgen/texture/CloudShadow.h"

#include "image/ImageData.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

namespace eve::procgen {
namespace {

float smoothstep(float edge0, float edge1, float x) {
    if (edge0 == edge1) return x < edge0 ? 0.f : 1.f;
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

/** Distance to the nearest lattice-center (value-noise voronoi). Cell size = 1. */
float voronoiDist(const NoiseField &n, float x, float y) {
    float best = 1e18f;
    const int cx0 = int(std::floor(x));
    const int cy0 = int(std::floor(y));
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int cx = cx0 + ox;
            const int cy = cy0 + oy;
            const float px = float(cx) + n.hash01(cx, cy);
            const float py = float(cy) + n.hash01(cx * 7 + 3, cy * 13 + 5);
            const float dx = x - px;
            const float dy = y - py;
            const float d  = dx * dx + dy * dy;
            if (d < best) best = d;
        }
    }
    return std::sqrt(best);
}

std::unique_ptr<image::ImageData> makeFromHeightFn(const Params &params, std::string &error,
                                                   const TextureRecipeDef &def) {
    const auto ctx = TextureGenContext::fromParams(params);
    if (ctx.width > 4096 || ctx.height > 4096) {
        error = "texture size too large (max 4096)";
        return nullptr;
    }
    auto img = std::make_unique<image::ImageData>(ctx.width, ctx.height, "RGBA8");
    std::vector<float> height;
    fillHeightField(ctx, def.height, height);
    paintHeightToImage(*img, height, ctx.width, ctx.height, def.albedo, ctx.colors, ctx.pixelSize);
    return img;
}

/** Build one TextureRecipeDef from a height lambda + ramp + PBR knobs. */
TextureRecipeDef makeDef(std::string id, ColorRamp ramp,
                         std::function<float(float, float, const NoiseField &)> height,
                         PbrParams pbr = {}) {
    TextureRecipeDef def;
    def.id     = std::move(id);
    def.albedo = std::move(ramp);
    def.height = std::move(height);
    def.pbr    = pbr;
    return def;
}

std::vector<TextureRecipeDef> buildDefs() {
    std::vector<TextureRecipeDef> defs;

    // --- tex.soil: loose dark soil ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 58, 38, 22);
        ramp.add(0.35f, 92, 62, 34);
        ramp.add(0.65f, 120, 84, 48);
        ramp.add(1.00f, 148, 110, 68);
        PbrParams pbr;
        pbr.roughnessLow = 0.75f;
        pbr.roughnessHigh = 1.f;
        defs.push_back(makeDef(
            "tex.soil", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                float h = n.fbm(u, v, 4);
                h       = h * 0.75f + 0.25f * n.valueNoise(u * 3.1f + 2.f, v * 3.1f);
                return h;
            },
            pbr));
    }

    // --- tex.stone: cracked stone ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 52, 54, 58);
        ramp.add(0.40f, 88, 90, 96);
        ramp.add(0.70f, 130, 132, 138);
        ramp.add(1.00f, 176, 178, 184);
        PbrParams pbr;
        pbr.roughnessLow = 0.6f;
        pbr.roughnessHigh = 0.9f;
        pbr.aoStrength = 1.4f;
        defs.push_back(makeDef(
            "tex.stone", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                float       h     = n.ridged(u, v, 4);
                const float crack = std::fabs(n.valueNoise(u * 2.7f, v * 2.7f) - 0.5f) * 2.f;
                h                 = h * 0.7f + (1.f - crack) * 0.3f;
                return h;
            },
            pbr));
    }

    // --- tex.rock: warm boulder, larger-scale ridged ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 60, 54, 46);
        ramp.add(0.35f, 96, 88, 76);
        ramp.add(0.65f, 132, 122, 104);
        ramp.add(1.00f, 168, 158, 140);
        PbrParams pbr;
        pbr.roughnessLow = 0.6f;
        pbr.roughnessHigh = 0.95f;
        pbr.aoStrength = 1.3f;
        defs.push_back(makeDef(
            "tex.rock", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                float h = n.ridged(u * 0.8f, v * 0.8f, 4);
                h       = h * 0.75f + 0.25f * n.fbm(u * 2.5f, v * 2.5f, 2);
                return h;
            },
            pbr));
    }

    // --- tex.marble: polished veined marble ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 230, 230, 235);
        ramp.add(0.45f, 200, 200, 210);
        ramp.add(0.55f, 120, 120, 135);
        ramp.add(0.70f, 190, 190, 200);
        ramp.add(1.00f, 245, 245, 248);
        PbrParams pbr;
        pbr.roughnessLow = 0.08f;
        pbr.roughnessHigh = 0.35f;
        defs.push_back(makeDef(
            "tex.marble", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                const float w = n.warp(u, v, 2.5f, std::max(2, 3));
                float vein = std::sin((u + w * 4.f) * 3.14159265f * 2.f) * 0.5f + 0.5f;
                vein       = std::pow(vein, 1.6f);
                return vein * 0.65f + w * 0.35f;
            },
            pbr));
    }

    // --- tex.water: rippling water surface ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 18, 60, 110);
        ramp.add(0.40f, 28, 96, 150);
        ramp.add(0.70f, 48, 140, 180);
        ramp.add(1.00f, 120, 200, 210);
        PbrParams pbr;
        pbr.roughnessLow = 0.05f;
        pbr.roughnessHigh = 0.3f;
        pbr.metallic = 0.05f;
        defs.push_back(makeDef(
            "tex.water", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                float wave = 0.5f + 0.5f * std::sin((u * 2.2f + v * 0.4f) * 6.28318f +
                                                    n.valueNoise(u * 1.3f, v * 1.3f) * 2.f);
                float detail = n.fbm(u * 1.5f, v * 1.5f, 4);
                return wave * 0.55f + detail * 0.45f;
            },
            pbr));
    }

    // --- tex.ripple: concentric ripples from scatter points ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 20, 60, 120);
        ramp.add(0.40f, 40, 120, 190);
        ramp.add(0.70f, 90, 170, 220);
        ramp.add(1.00f, 190, 230, 245);
        PbrParams pbr;
        pbr.roughnessLow = 0.04f;
        pbr.roughnessHigh = 0.5f;
        defs.push_back(makeDef(
            "tex.ripple", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                const float rings = 6.f;
                float d = voronoiDist(n, u, v);
                float ring = 0.5f + 0.5f * std::sin(d * 6.28318f * rings);
                // fade rings near each source centre.
                ring *= smoothstep(0.f, 0.3f, d);
                return ring * 0.7f + n.fbm(u * 2.f, v * 2.f, 3) * 0.3f;
            },
            pbr));
    }

    // --- tex.sky_cloud: sky with clouds ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 70, 130, 200);
        ramp.add(0.45f, 110, 170, 220);
        ramp.add(0.70f, 180, 210, 235);
        ramp.add(1.00f, 245, 248, 252);
        defs.push_back(makeDef(
            "tex.sky_cloud", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                const float sky = std::clamp(1.f - v / std::max(0.01f, 4.f), 0.f, 1.f);
                float clouds    = n.fbm(u * 0.9f + 3.f, v * 0.9f, 4);
                clouds          = smoothstep(0.35f, 0.75f, clouds);
                return sky * 0.45f + clouds * 0.55f;
            }));
    }

    // --- tex.wood: wavy grain running along V ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 150, 106, 58);
        ramp.add(0.30f, 178, 128, 72);
        ramp.add(0.55f, 130, 88, 48);
        ramp.add(0.75f, 96, 62, 36);
        ramp.add(1.00f, 70, 44, 28);
        PbrParams pbr;
        pbr.roughnessLow = 0.35f;
        pbr.roughnessHigh = 0.7f;
        defs.push_back(makeDef(
            "tex.wood", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                const float w    = n.warp(u * 0.8f, v * 0.8f, 2.5f, 3);
                float       grain = 0.5f + 0.5f * std::sin((u * 4.f + w * 6.f) * 6.28318f);
                grain             = std::pow(grain, 2.4f);
                const float detail = n.fbm(u * 3.f, v * 3.f, 3);
                return grain * 0.6f + detail * 0.4f;
            },
            pbr));
    }

    // --- tex.cloth: woven fabric ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 30, 44, 34);
        ramp.add(0.40f, 52, 76, 52);
        ramp.add(0.70f, 74, 104, 68);
        ramp.add(1.00f, 108, 140, 92);
        PbrParams pbr;
        pbr.roughnessLow = 0.8f;
        pbr.roughnessHigh = 1.f;
        pbr.normalStrength = 2.f;
        defs.push_back(makeDef(
            "tex.cloth", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                const float threads = 9.f;
                const float fx      = u * threads;
                const float fy      = v * threads;
                const float dx      = std::fabs(std::fmod(fx, 1.f) - 0.5f);
                const float dy      = std::fabs(std::fmod(fy, 1.f) - 0.5f);
                const float c       = std::min(dx, dy);         // 0 at thread, .5 between
                float       thread  = 1.f - c * 2.f;            // thread cross-section
                const int   ix      = int(std::floor(fx));
                const int   iy      = int(std::floor(fy));
                if ((ix + iy) % 2 == 0) thread = 1.f - thread;  // interlace over/under
                const float noise = n.valueNoise(u * 2.f, v * 2.f);
                return thread * 0.55f + noise * 0.35f + 0.1f;
            },
            pbr));
    }

    // --- tex.ornament: irregular decorative swirl ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 88, 30, 60);
        ramp.add(0.30f, 160, 74, 96);
        ramp.add(0.50f, 224, 176, 120);
        ramp.add(0.70f, 150, 96, 70);
        ramp.add(1.00f, 74, 46, 60);
        PbrParams pbr;
        pbr.roughnessLow = 0.3f;
        pbr.roughnessHigh = 0.6f;
        defs.push_back(makeDef(
            "tex.ornament", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                const float w    = n.warp(u, v, 3.5f, 4);
                float       swirl = 0.5f + 0.5f * (std::sin((u + w * 4.f) * 6.28318f) *
                                          std::cos((v - w * 3.f) * 6.28318f));
                return swirl * 0.6f + n.fbm(u * 2.f, v * 2.f, 4) * 0.4f;
            },
            pbr));
    }

    // --- tex.spot: blobby spots (leopard / dalmatian) ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 220, 200, 168);  // pale base
        ramp.add(0.35f, 196, 172, 138);
        ramp.add(0.50f, 90, 62, 40);  // dark spot
        ramp.add(0.75f, 60, 42, 30);
        ramp.add(1.00f, 150, 120, 92);
        PbrParams pbr;
        pbr.roughnessLow = 0.5f;
        pbr.roughnessHigh = 0.9f;
        defs.push_back(makeDef(
            "tex.spot", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                const float blobs = n.valueNoise(u * 2.2f, v * 2.2f);
                float       spot  = smoothstep(0.5f, 0.6f, blobs);
                spot              = spot * 0.8f + n.fbm(u * 3.f, v * 3.f, 3) * 0.2f;
                return spot;
            },
            pbr));
    }

    // --- tex.zebra: wavy black/white stripes ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 245, 245, 244);
        ramp.add(0.50f, 30, 30, 32);
        ramp.add(1.00f, 245, 245, 244);
        PbrParams pbr;
        pbr.roughnessLow = 0.4f;
        pbr.roughnessHigh = 0.8f;
        defs.push_back(makeDef(
            "tex.zebra", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                const float w     = n.warp(u, v, 1.5f, 3);
                float       stripe = 0.5f + 0.5f * std::sin((u * 5.f + w * 4.f) * 6.28318f);
                stripe             = smoothstep(0.3f, 0.55f, stripe);
                return stripe * 0.9f + n.valueNoise(u * 3.f, v * 3.f) * 0.1f;
            },
            pbr));
    }

    // --- tex.wall: brick wall with recessed mortar ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 78, 46, 38);  // mortar
        ramp.add(0.30f, 130, 74, 54);
        ramp.add(0.60f, 164, 96, 66);
        ramp.add(1.00f, 196, 130, 92);
        PbrParams pbr;
        pbr.roughnessLow = 0.65f;
        pbr.roughnessHigh = 0.95f;
        pbr.aoStrength = 1.6f;
        defs.push_back(makeDef(
            "tex.wall", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                const float rows = 5.f;
                const float cols = 2.f * rows;
                const int   row  = int(std::floor(v * rows));
                const float off  = (row % 2 == 0) ? 0.f : 0.5f;
                const float bx   = u * cols + off;
                const float mh   = std::fabs(std::fmod(v * rows, 1.f) - 0.5f);
                const float mv   = std::fabs(std::fmod(bx, 1.f) - 0.5f);
                float       brick = 1.f - smoothstep(0.30f, 0.42f, std::min(mh, mv));
                const float noise = n.valueNoise(u * 2.f, v * 2.f);
                return brick * 0.75f + noise * 0.25f * brick + (1.f - brick) * 0.15f;
            },
            pbr));
    }

    // --- tex.cement: mottled grey with hairline cracks ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 60, 62, 66);
        ramp.add(0.35f, 96, 100, 106);
        ramp.add(0.70f, 138, 142, 148);
        ramp.add(1.00f, 182, 186, 192);
        PbrParams pbr;
        pbr.roughnessLow = 0.6f;
        pbr.roughnessHigh = 0.85f;
        defs.push_back(makeDef(
            "tex.cement", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                float       mottle = n.fbmPerlin(u, v, 4);
                const float crack  = 1.f - smoothstep(0.55f, 0.72f, n.ridgedPerlin(u * 3.f, v * 3.f, 3));
                return mottle * 0.6f + crack * 0.4f;
            },
            pbr));
    }

    // --- tex.mud: dried cracked mud plates ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 40, 26, 18);  // crack
        ramp.add(0.30f, 84, 56, 34);
        ramp.add(0.60f, 116, 78, 46);
        ramp.add(1.00f, 148, 104, 62);
        PbrParams pbr;
        pbr.roughnessLow = 0.7f;
        pbr.roughnessHigh = 1.f;
        pbr.aoStrength = 1.8f;
        defs.push_back(makeDef(
            "tex.mud", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                float d = voronoiDist(n, u, v);
                float plate = smoothstep(0.04f, 0.14f, d);  // 0 on cracks, 1 inside plates
                return plate * 0.8f + n.fbm(u * 2.5f, v * 2.5f, 3) * 0.2f;
            },
            pbr));
    }

    return defs;
}

/** Build a CloudField::Params from texture params (world-scale / wind / coverage…). */
CloudField::Params cloudFieldFromParams(const Params &params) {
    CloudField::Params p;
    p.seed       = params.getSeed();
    p.worldScale = params.getFloat("worldScale", 96.f);
    p.coverage   = params.getFloat("cloudCoverage", 0.55f);
    p.softness   = params.getFloat("cloudSoftness", 0.12f);
    p.detail     = params.getFloat("cloudDetail", 0.5f);
    p.windSpeed  = params.getFloat("windSpeed", 4.f);
    p.windAngle  = params.getFloat("windAngle", 0.f);
    p.octaves    = params.getInt("octaves", 4);
    return p;
}

/** tex.cloud: standalone billowy cloud cover (white puffs on translucent sky). */
std::unique_ptr<image::ImageData> genCloud(const Params &params, std::string &error) {
    const auto ctx = TextureGenContext::fromParams(params);
    if (ctx.width > 4096 || ctx.height > 4096) {
        error = "texture size too large (max 4096)";
        return nullptr;
    }
    auto img = std::make_unique<image::ImageData>(ctx.width, ctx.height, "RGBA8");
    ColorRamp ramp;
    ramp.add(0.00f, 90, 140, 205);
    ramp.add(0.30f, 168, 205, 238);
    ramp.add(0.55f, 238, 248, 252);
    ramp.add(1.00f, 255, 255, 255);

    CloudField field(cloudFieldFromParams(params));
    const float time   = params.getFloat("time", 0.f);
    const float extent = params.getFloat("extent", field.params().worldScale);
    std::vector<float> height(size_t(ctx.width * ctx.height));
    field.sample(height.data(), ctx.width, ctx.height, time, 0.f, 0.f, extent);
    paintHeightToImage(*img, height, ctx.width, ctx.height, ramp, ctx.colors, ctx.pixelSize);
    return img;
}

/** tex.cloud_shadow: projected cloud coverage cast on the ground (white = dense shadow). */
std::unique_ptr<image::ImageData> genCloudShadow(const Params &params, std::string &error) {
    const auto ctx = TextureGenContext::fromParams(params);
    if (ctx.width > 4096 || ctx.height > 4096) {
        error = "texture size too large (max 4096)";
        return nullptr;
    }
    auto img = std::make_unique<image::ImageData>(ctx.width, ctx.height, "RGBA8");
    ColorRamp ramp;
    ramp.add(0.00f, 18, 20, 26);   // clear (lit)
    ramp.add(0.30f, 60, 62, 70);
    ramp.add(0.60f, 120, 122, 130);
    ramp.add(1.00f, 200, 202, 208);  // dense cloud (strong shadow)

    CloudField::Params fp = cloudFieldFromParams(params);
    CloudShadow::Params sp;
    sp.field          = CloudField(fp);
    sp.sunDirX        = params.getFloat("sunDirX", 0.f);
    sp.sunDirY        = params.getFloat("sunDirY", 1.f);
    sp.sunDirZ        = params.getFloat("sunDirZ", 0.f);
    sp.cloudAltitude  = params.getFloat("cloudAltitude", 60.f);
    sp.strength       = params.getFloat("cloudShadowStrength", 0.8f);
    CloudShadow shadow(sp);

    const float time   = params.getFloat("time", 0.f);
    const float extent = params.getFloat("extent", fp.worldScale);
    std::vector<float> height(size_t(ctx.width * ctx.height));
    shadow.sampleCoverage(height.data(), ctx.width, ctx.height, time, 0.f, 0.f, extent);
    paintHeightToImage(*img, height, ctx.width, ctx.height, ramp, ctx.colors, ctx.pixelSize);
    return img;
}

}  // namespace

const std::vector<TextureRecipeDef> &builtinTextureDefs() {
    static const std::vector<TextureRecipeDef> defs = buildDefs();
    return defs;
}

void TextureRecipeRegistry::registerBuiltins() {
    if (builtinsRegistered_) return;
    auto cloudDescriptor = [](std::string id, std::string name, bool shadow) {
        RecipeDescriptor schema = RecipeDescriptor::grid(std::move(id), std::move(name), "Atmosphere", 1, 1);
        schema.params.push_back(ParamDescriptor::floating("worldScale", "World Scale", 96.f, 1.f, 4096.f, 1.f));
        schema.params.push_back(ParamDescriptor::floating("cloudCoverage", "Coverage", 0.55f, 0.f, 1.f, 0.01f));
        schema.params.push_back(ParamDescriptor::floating("cloudSoftness", "Softness", 0.12f, 0.001f, 1.f, 0.01f));
        schema.params.push_back(ParamDescriptor::floating("cloudDetail", "Detail", 0.5f, 0.f, 2.f, 0.01f));
        schema.params.push_back(ParamDescriptor::floating("windSpeed", "Wind Speed", 4.f, 0.f, 256.f, 0.1f));
        schema.params.push_back(ParamDescriptor::floating("windAngle", "Wind Angle", 0.f, -6.2832f, 6.2832f, 0.01f));
        schema.params.push_back(ParamDescriptor::integer("octaves", "Octaves", 4, 1, 12));
        schema.params.push_back(ParamDescriptor::floating("time", "Time", 0.f, 0.f, 86400.f, 0.1f));
        schema.params.push_back(ParamDescriptor::floating("extent", "Extent", 96.f, 1.f, 4096.f, 1.f));
        if (shadow) {
            schema.params.push_back(ParamDescriptor::floating("cloudAltitude", "Cloud Altitude", 60.f, 0.f, 10000.f, 1.f));
            schema.params.push_back(ParamDescriptor::floating("cloudShadowStrength", "Shadow Strength", 0.8f, 0.f, 1.f, 0.01f));
        }
        return schema;
    };
    registerRecipe(cloudDescriptor("tex.cloud", "Cloud", false), genCloud);
    registerRecipe(cloudDescriptor("tex.cloud_shadow", "Cloud Shadow", true), genCloudShadow);
    for (const TextureRecipeDef &def : builtinTextureDefs()) {
        registerRecipe(makeTextureRecipeDescriptor(def), [def](const Params &params, std::string &error) {
            return makeFromHeightFn(params, error, def);
        });
    }
    builtinsRegistered_ = true;
}

}  // namespace eve::procgen
