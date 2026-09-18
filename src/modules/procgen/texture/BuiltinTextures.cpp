#include "procgen/texture/TextureRecipe.h"
#include "procgen/texture/NoiseField.h"
#include "procgen/texture/PrototypeTextures.h"
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
    auto               img = std::make_unique<image::ImageData>(ctx.width, ctx.height, "RGBA8");
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

    // --- tex.rock: warm boulder with deep crevices (darker, more contrast) ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 28, 24, 20);
        ramp.add(0.28f, 58, 52, 44);
        ramp.add(0.55f, 92, 84, 70);
        ramp.add(0.80f, 122, 112, 96);
        ramp.add(1.00f, 148, 138, 120);
        PbrParams pbr;
        pbr.roughnessLow = 0.62f;
        pbr.roughnessHigh = 0.98f;
        pbr.aoStrength = 1.7f;
        pbr.normalStrength = 3.0f;
        defs.push_back(makeDef(
            "tex.rock", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                float h = n.ridged(u * 0.9f, v * 0.9f, 5);
                const float crack =
                    std::pow(std::fabs(n.valueNoise(u * 2.2f, v * 2.2f) - 0.5f) * 2.f, 1.6f);
                h = h * 0.62f + 0.22f * n.fbm(u * 2.8f, v * 2.8f, 3) + (1.f - crack) * 0.16f;
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

    // --- tex.bark: vertical fissured trunk bark (TreeMesh atlas left half) ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 28, 18, 12);
        ramp.add(0.22f, 58, 38, 24);
        ramp.add(0.48f, 98, 68, 42);
        ramp.add(0.72f, 138, 102, 64);
        ramp.add(1.00f, 72, 48, 30);
        PbrParams pbr;
        pbr.roughnessLow = 0.58f;
        pbr.roughnessHigh = 0.98f;
        pbr.aoStrength = 1.85f;
        pbr.normalStrength = 3.6f;
        defs.push_back(makeDef(
            "tex.bark", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                const float warp = n.warp(u, v, 2.1f, 3);
                const float grain =
                    0.5f + 0.5f * std::sin((u * 9.f + warp * 6.f) * 6.28318f);
                const float ridge = n.ridged(u * 1.6f, v * 0.28f, 5);
                const float crack =
                    std::pow(std::fabs(n.valueNoise(u * 1.4f, v * 4.2f) - 0.5f) * 2.f, 1.55f);
                const float flake = n.fbm(u * 3.2f + 1.7f, v * 0.9f, 3);
                return grain * 0.28f + ridge * 0.38f + (1.f - crack) * 0.22f + flake * 0.12f;
            },
            pbr));
    }

    // --- tex.moss: cool green-gray rock weathering with vivid moss patches ---
    {
        ColorRamp ramp;
        ramp.add(0.00f, 36, 40, 32);
        ramp.add(0.30f, 62, 72, 48);
        ramp.add(0.52f, 78, 108, 52);
        ramp.add(0.72f, 96, 132, 58);
        ramp.add(1.00f, 148, 156, 138);
        PbrParams pbr;
        pbr.roughnessLow = 0.72f;
        pbr.roughnessHigh = 1.f;
        pbr.aoStrength = 1.7f;
        pbr.normalStrength = 3.1f;
        defs.push_back(makeDef(
            "tex.moss", std::move(ramp),
            [](float u, float v, const NoiseField &n) {
                float rock = n.ridged(u * 0.95f, v * 0.95f, 5);
                float moss = smoothstep(0.32f, 0.78f, n.fbm(u * 1.8f + 2.f, v * 1.8f, 4));
                const float crack =
                    std::pow(std::fabs(n.valueNoise(u * 2.4f, v * 2.4f) - 0.5f) * 2.f, 1.5f);
                return rock * (1.f - moss * 0.55f) + moss * 0.62f + (1.f - crack) * 0.08f;
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
    auto      img = std::make_unique<image::ImageData>(ctx.width, ctx.height, "RGBA8");
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
    auto      img = std::make_unique<image::ImageData>(ctx.width, ctx.height, "RGBA8");
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

/**
 * @brief Discrete ovate leaf stamps on a transparent leaf-card atlas.
 *
 * Each stamp is an egg-shaped leaf with clear empty margins so
 * SurfaceMode::Masked cuts the card to foliage silhouettes rather than a
 * solid green rectangle.
 */
struct LeafStamp {
    float cx = 0.5f, cy = 0.5f;
    float halfW = 0.12f, halfH = 0.18f;
    float ang = 0.f;
};

void buildOvateLeafStamps(const NoiseField &noise, int count, std::vector<LeafStamp> &stamps) {
    stamps.clear();
    count = std::clamp(count, 1, 24);
    // Rough grid so leaves do not pile into an opaque slab.
    const int cols = std::max(1, int(std::ceil(std::sqrt(float(count)))));
    const int rows = std::max(1, int(std::ceil(float(count) / float(cols))));
    int placed = 0;
    for (int row = 0; row < rows && placed < count; ++row) {
        for (int col = 0; col < cols && placed < count; ++col) {
            const float cellW = 1.f / float(cols);
            const float cellH = 1.f / float(rows);
            const float jx = (noise.hash01(col * 3 + 1, row * 5 + 2) - 0.5f) * cellW * 0.45f;
            const float jy = (noise.hash01(col * 7 + 3, row * 11 + 4) - 0.5f) * cellH * 0.45f;
            LeafStamp s;
            s.cx = (float(col) + 0.5f) * cellW + jx;
            s.cy = (float(row) + 0.5f) * cellH + jy;
            s.halfW = cellW * (0.28f + 0.10f * noise.hash01(col + 9, row + 2));
            s.halfH = cellH * (0.38f + 0.12f * noise.hash01(col + 4, row + 8));
            s.ang = (noise.hash01(col * 13 + 1, row * 17 + 3) - 0.5f) * 1.8f;
            stamps.push_back(s);
            ++placed;
        }
    }
}

void sampleOvateLeafCard(const NoiseField &noise, float u, float v, const std::vector<LeafStamp> &stamps,
                         float &cover, float &shade) {
    cover = 0.f;
    shade = 0.f;
    for (const LeafStamp &s : stamps) {
        float dx = u - s.cx;
        float dy = v - s.cy;
        const float ca = std::cos(s.ang), sa = std::sin(s.ang);
        const float rx = dx * ca - dy * sa;
        const float ry = dx * sa + dy * ca;
        // Ovate / lanceolate: pointed tip along +Y, wider shoulders below centre.
        const float halfW = s.halfW * (1.f - 0.42f * std::max(0.f, ry / std::max(1e-4f, s.halfH)));
        const float d = (rx * rx) / std::max(1e-5f, halfW * halfW) +
                        (ry * ry) / std::max(1e-5f, s.halfH * s.halfH);
        const float mask = 1.f - smoothstep(0.78f, 1.02f, d);
        if (mask <= cover) continue;
        cover = mask;
        const float vein = std::pow(
            std::fabs(std::sin(ry * 11.f / std::max(1e-4f, s.halfH) +
                               noise.valueNoise(s.cx * 4.f, s.cy * 4.f) * 2.f)),
            3.5f);
        const float mott = noise.fbm(s.cx * 3.f + u * 2.f, s.cy * 3.f + v * 2.f, 3);
        shade = std::clamp(0.30f + mott * 0.40f + (1.f - vein) * 0.30f, 0.f, 1.f);
    }
}

/**
 * @brief Dense leafy fill for bush/canopy blobs (mostly opaque).
 */
void sampleFoliageFill(const NoiseField &noise, float u, float v, float scale, float &cover, float &shade) {
    cover = 0.f;
    shade = 0.f;
    const float su = u * scale;
    const float sv = v * scale;
    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            const int cx = int(std::floor(su * 2.2f)) + ox;
            const int cy = int(std::floor(sv * 2.2f)) + oy;
            const float px = float(cx) + noise.hash01(cx, cy);
            const float py = float(cy) + noise.hash01(cx * 7 + 3, cy * 13 + 5);
            float dx = su * 2.2f - px;
            float dy = sv * 2.2f - py;
            const float ang = (noise.hash01(cx * 3 + 1, cy * 5 + 2) - 0.5f) * 1.4f;
            const float ca = std::cos(ang), sa = std::sin(ang);
            const float rx = dx * ca - dy * sa;
            const float ry = dx * sa + dy * ca;
            const float halfW = 0.48f * (1.f - 0.4f * std::max(0.f, ry));
            const float d = (rx * rx) / std::max(1e-4f, halfW * halfW) + (ry * ry) / 0.58f;
            const float mask = 1.f - smoothstep(0.55f, 1.05f, d);
            if (mask <= cover) continue;
            cover = mask;
            const float vein = std::pow(
                std::fabs(std::sin(ry * 8.f + noise.valueNoise(px * 0.35f, py * 0.35f) * 2.5f)), 3.f);
            const float mott = noise.fbm(px * 0.28f + u * 1.6f, py * 0.28f + v * 1.6f, 3);
            shade = std::clamp(0.30f + mott * 0.42f + (1.f - vein) * 0.28f, 0.f, 1.f);
        }
    }
    // Keep blob fill nearly opaque so ellipsoid lobes do not go hollow.
    cover = std::clamp(cover * 0.55f + 0.45f, 0.f, 1.f);
}

/**
 * tex.tree_atlas — split atlas matching mesh.tree UV layout:
 *   u in [0, 0.45] bark on the left, u in [0.55, 1] foliage on the right.
 */
std::unique_ptr<image::ImageData> genTreeAtlas(const Params &params, std::string &error) {
    const auto ctx = TextureGenContext::fromParams(params);
    if (ctx.width > 4096 || ctx.height > 4096) {
        error = "texture size too large (max 4096)";
        return nullptr;
    }
    auto img = std::make_unique<image::ImageData>(ctx.width, ctx.height, "RGBA8");
    NoiseField noise;
    noise.seed = ctx.seed;
    if (ctx.seamless) {
        noise.periodX = std::max(1, int(ctx.scale));
        noise.periodY = noise.periodX;
    }

    ColorRamp barkRamp;
    barkRamp.add(0.00f, 28, 18, 12);
    barkRamp.add(0.22f, 58, 38, 24);
    barkRamp.add(0.48f, 102, 72, 44);
    barkRamp.add(0.72f, 148, 110, 70);
    barkRamp.add(1.00f, 68, 46, 28);

    ColorRamp leafRamp;
    leafRamp.add(0.00f, 14, 36, 10);
    leafRamp.add(0.28f, 32, 78, 22);
    leafRamp.add(0.52f, 52, 118, 34);
    leafRamp.add(0.78f, 88, 148, 46);
    leafRamp.add(1.00f, 24, 54, 16);

    std::vector<LeafStamp> leafStamps;
    buildOvateLeafStamps(noise, 10, leafStamps);

    const float invW = 1.f / float(std::max(1, ctx.width - 1));
    const float invH = 1.f / float(std::max(1, ctx.height - 1));
    for (int y = 0; y < ctx.height; ++y) {
        for (int x = 0; x < ctx.width; ++x) {
            const float u = float(x) * invW;
            const float v = float(y) * invH;
            Rgba8 color;
            if (u < 0.48f) {
                const float bu = u / 0.48f;
                const float warp = noise.warp(bu * ctx.scale, v * ctx.scale, 2.1f, 3);
                const float grain =
                    0.5f + 0.5f * std::sin((bu * 9.f + warp * 6.f) * 6.28318f);
                const float ridge = noise.ridged(bu * 1.6f * ctx.scale, v * 0.28f * ctx.scale, 5);
                const float crack = std::pow(
                    std::fabs(noise.valueNoise(bu * 1.4f * ctx.scale, v * 4.2f * ctx.scale) - 0.5f) *
                        2.f,
                    1.55f);
                const float flake =
                    noise.fbm(bu * 3.2f * ctx.scale + 1.7f, v * 0.9f * ctx.scale, 3);
                const float h = std::clamp(
                    grain * 0.28f + ridge * 0.38f + (1.f - crack) * 0.22f + flake * 0.12f, 0.f, 1.f);
                color = barkRamp.sampleBanded(h, ctx.colors);
            } else if (u > 0.52f) {
                const float fu = (u - 0.52f) / 0.48f;
                float cover = 0.f, shade = 0.f;
                // Transparent card with scattered ovate leaves for masked leaf cards.
                sampleOvateLeafCard(noise, fu, v, leafStamps, cover, shade);
                color = leafRamp.sampleBanded(shade, ctx.colors);
                color.a = static_cast<uint8_t>(std::clamp(cover, 0.f, 1.f) * 255.f);
            } else {
                // Narrow blend strip between bark and foliage regions.
                color = {72, 86, 48, 255};
            }
            img->setPixel(x, y,
                          image::ImageData::Colorf{color.r / 255.f, color.g / 255.f, color.b / 255.f,
                                                   color.a / 255.f});
        }
    }
    return img;
}

/**
 * tex.foliage — dual atlas for bush meshes:
 *   u in [0, 0.48] opaque leafy fill for ellipsoid blobs,
 *   u in [0.52, 1] transparent leaf-card with scattered ovate leaves for addLeafCard.
 */
std::unique_ptr<image::ImageData> genFoliage(const Params &params, std::string &error) {
    const auto ctx = TextureGenContext::fromParams(params);
    if (ctx.width > 4096 || ctx.height > 4096) {
        error = "texture size too large (max 4096)";
        return nullptr;
    }
    auto img = std::make_unique<image::ImageData>(ctx.width, ctx.height, "RGBA8");
    NoiseField noise;
    noise.seed = ctx.seed;
    if (ctx.seamless) {
        noise.periodX = std::max(1, int(ctx.scale));
        noise.periodY = noise.periodX;
    }

    // Cooler forest green for bushes — less lime/yellow than the tree-atlas canopy.
    ColorRamp leafRamp;
    leafRamp.add(0.00f, 12, 32, 16);
    leafRamp.add(0.28f, 28, 68, 34);
    leafRamp.add(0.52f, 42, 96, 48);
    leafRamp.add(0.78f, 62, 122, 58);
    leafRamp.add(1.00f, 22, 48, 28);

    std::vector<LeafStamp> leafStamps;
    buildOvateLeafStamps(noise, 8, leafStamps);

    const float invW = 1.f / float(std::max(1, ctx.width - 1));
    const float invH = 1.f / float(std::max(1, ctx.height - 1));
    for (int y = 0; y < ctx.height; ++y) {
        for (int x = 0; x < ctx.width; ++x) {
            const float u = float(x) * invW;
            const float v = float(y) * invH;
            float cover = 0.f, shade = 0.f;
            Rgba8 color;
            if (u < 0.48f) {
                const float fu = u / 0.48f;
                sampleFoliageFill(noise, fu, v, ctx.scale, cover, shade);
                color = leafRamp.sampleBanded(shade, ctx.colors);
                color.a = 255;
            } else if (u > 0.52f) {
                const float fu = (u - 0.52f) / 0.48f;
                sampleOvateLeafCard(noise, fu, v, leafStamps, cover, shade);
                color = leafRamp.sampleBanded(shade, ctx.colors);
                color.a = static_cast<uint8_t>(std::clamp(cover, 0.f, 1.f) * 255.f);
            } else {
                color = {40, 78, 36, 255};
            }
            img->setPixel(x, y,
                          image::ImageData::Colorf{color.r / 255.f, color.g / 255.f, color.b / 255.f,
                                                   color.a / 255.f});
        }
    }
    return img;
}

/**
 * tex.flower — stem green on the left, soft cream petal + warm centre on the right.
 * Tint in examples for red / blue / white / yellow Flower01-03 accents.
 */
std::unique_ptr<image::ImageData> genFlower(const Params &params, std::string &error) {
    const auto ctx = TextureGenContext::fromParams(params);
    if (ctx.width > 4096 || ctx.height > 4096) {
        error = "texture size too large (max 4096)";
        return nullptr;
    }
    auto img = std::make_unique<image::ImageData>(ctx.width, ctx.height, "RGBA8");
    NoiseField noise;
    noise.seed = ctx.seed;
    if (ctx.seamless) {
        noise.periodX = std::max(1, int(ctx.scale));
        noise.periodY = noise.periodX;
    }

    ColorRamp stemRamp;
    stemRamp.add(0.00f, 28, 72, 28);
    stemRamp.add(0.55f, 48, 110, 42);
    stemRamp.add(1.00f, 34, 86, 32);

    ColorRamp petalRamp;
    petalRamp.add(0.00f, 210, 200, 205);
    petalRamp.add(0.40f, 245, 236, 240);
    petalRamp.add(0.70f, 255, 250, 252);
    petalRamp.add(1.00f, 255, 236, 180);  // warm centre

    const float invW = 1.f / float(std::max(1, ctx.width - 1));
    const float invH = 1.f / float(std::max(1, ctx.height - 1));
    for (int y = 0; y < ctx.height; ++y) {
        for (int x = 0; x < ctx.width; ++x) {
            const float u = float(x) * invW;
            const float v = float(y) * invH;
            Rgba8 color;
            if (u < 0.32f) {
                const float su = u / 0.32f;
                const float h =
                    std::clamp(0.45f + 0.35f * noise.fbm(su * 2.f, v * 4.f, 3) +
                                   0.2f * std::sin(v * 18.f),
                               0.f, 1.f);
                color   = stemRamp.sampleBanded(h, ctx.colors);
                color.a = 255;
            } else {
                // Petal local coords: elliptical falloff with soft tip.
                const float pu = (u - 0.36f) / 0.62f;
                const float pv = v;
                const float dx = (pu - 0.5f) * 2.f;
                const float dy = (pv - 0.5f) * 2.f;
                const float halfW = 0.72f * (1.f - 0.35f * std::max(0.f, dy));
                const float d =
                    (dx * dx) / std::max(1e-4f, halfW * halfW) + (dy * dy) / 0.95f;
                const float cover = 1.f - smoothstep(0.72f, 1.05f, d);
                const float vein = std::pow(
                    std::fabs(std::sin(dy * 6.f + noise.valueNoise(pu * 2.f, pv * 2.f) * 2.f)),
                    3.f);
                const float centre = 1.f - smoothstep(0.05f, 0.42f, std::sqrt(dx * dx + dy * dy));
                const float h = std::clamp(0.35f + (1.f - vein) * 0.25f + centre * 0.45f +
                                               noise.fbm(pu * 3.f, pv * 3.f, 2) * 0.1f,
                                           0.f, 1.f);
                color   = petalRamp.sampleBanded(h, ctx.colors);
                color.a = static_cast<uint8_t>(std::clamp(cover, 0.f, 1.f) * 255.f);
            }
            img->setPixel(x, y,
                          image::ImageData::Colorf{color.r / 255.f, color.g / 255.f, color.b / 255.f,
                                                   color.a / 255.f});
        }
    }
    return img;
}

}  // namespace

const std::vector<TextureRecipeDef> &builtinTextureDefs() {
    static const std::vector<TextureRecipeDef> defs = buildDefs();
    return defs;
}

void TextureRecipeRegistry::registerBuiltins() {
    if (builtinsRegistered_) return;
    registerPrototypeTextureRecipes(*this);
    auto cloudDescriptor = [](std::string id, std::string name, bool shadow) {
        RecipeDescriptor schema = RecipeDescriptor::grid(std::move(id), std::move(name), "Atmosphere", 1, 1);
        schema.params.push_back(ParamDescriptor::floating("worldScale", "World Scale", 96.f, 1.f, 4096.f, 1.f));
        schema.params.push_back(ParamDescriptor::floating("cloudCoverage", "Coverage", 0.55f, 0.f, 1.f, 0.01f));
        schema.params.push_back(ParamDescriptor::floating("cloudSoftness", "Softness", 0.12f, 0.001f, 1.f, 0.01f));
        schema.params.push_back(ParamDescriptor::floating("cloudDetail", "Detail", 0.5f, 0.f, 2.f, 0.01f));
        schema.params.push_back(ParamDescriptor::floating("windSpeed", "Wind Speed", 4.f, 0.f, 256.f, 0.1f));
        schema.params.push_back(ParamDescriptor::floating("windAngle", "Wind Angle", 0.f, -6.2832f, 6.2832f, 0.01f));
        schema.params.push_back(ParamDescriptor::integer("octaves", "Octaves", 4, 1, 12));
        schema.params.push_back(ParamDescriptor::boolean("seamless", "Seamless", true));
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
    {
        RecipeDescriptor atlas = RecipeDescriptor::grid("tex.tree_atlas", "Tree Atlas", "Nature", 1, 1);
        atlas.params.push_back(ParamDescriptor::floating("scale", "Scale", 4.f, 0.1f, 64.f, 0.1f));
        atlas.params.push_back(ParamDescriptor::integer("colors", "Color Bands", 6, 2, 32));
        atlas.params.push_back(ParamDescriptor::boolean("seamless", "Seamless", true));
        registerRecipe(std::move(atlas), genTreeAtlas);
    }
    {
        RecipeDescriptor foliage = RecipeDescriptor::grid("tex.foliage", "Foliage", "Nature", 1, 1);
        foliage.params.push_back(ParamDescriptor::floating("scale", "Scale", 4.f, 0.1f, 64.f, 0.1f));
        foliage.params.push_back(ParamDescriptor::integer("colors", "Color Bands", 6, 2, 32));
        foliage.params.push_back(ParamDescriptor::boolean("seamless", "Seamless", true));
        registerRecipe(std::move(foliage), genFoliage);
    }
    {
        RecipeDescriptor flower = RecipeDescriptor::grid("tex.flower", "Flower", "Nature", 1, 1);
        flower.params.push_back(ParamDescriptor::floating("scale", "Scale", 4.f, 0.1f, 64.f, 0.1f));
        flower.params.push_back(ParamDescriptor::integer("colors", "Color Bands", 6, 2, 32));
        flower.params.push_back(ParamDescriptor::boolean("seamless", "Seamless", true));
        registerRecipe(std::move(flower), genFlower);
    }
    for (const TextureRecipeDef &def : builtinTextureDefs()) {
        registerRecipe(makeTextureRecipeDescriptor(def), [def](const Params &params, std::string &error) {
            return makeFromHeightFn(params, error, def);
        });
    }
    builtinsRegistered_ = true;
}

}  // namespace eve::procgen
