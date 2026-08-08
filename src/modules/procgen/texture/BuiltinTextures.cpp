#include "procgen/texture/TextureRecipe.h"
#include "procgen/texture/NoiseField.h"
#include "procgen/texture/ColorRamp.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace eve::procgen {
namespace {

float smoothstep(float edge0, float edge1, float x) {
    if (edge0 == edge1) return x < edge0 ? 0.f : 1.f;
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

NoiseField makeNoise(const TextureGenContext &ctx) {
    NoiseField n;
    n.seed = ctx.seed;
    if (ctx.seamless) {
        n.periodX = std::max(1, int(std::lround(ctx.scale)));
        n.periodY = std::max(1, int(std::lround(ctx.scale * float(ctx.height) / float(std::max(1, ctx.width)))));
    }
    return n;
}

void fillHeight(const TextureGenContext &ctx, const NoiseField &n,
                const std::function<float(float, float, const NoiseField &)> &fn,
                std::vector<float> &height) {
    height.resize(size_t(ctx.width * ctx.height));
    for (int y = 0; y < ctx.height; ++y) {
        for (int x = 0; x < ctx.width; ++x) {
            const float u = float(x) / float(ctx.width) * ctx.scale;
            const float v = float(y) / float(ctx.height) * ctx.scale;
            height[size_t(y * ctx.width + x)] = std::clamp(fn(u, v, n), 0.f, 1.f);
        }
    }
}

image::ImageData *makeFromHeightFn(
    const Params &params, std::string &error, const ColorRamp &ramp,
    const std::function<float(float, float, const NoiseField &)> &heightFn) {
    const auto ctx = TextureGenContext::fromParams(params);
    if (ctx.width > 4096 || ctx.height > 4096) {
        error = "texture size too large (max 4096)";
        return nullptr;
    }
    auto *img = new image::ImageData(ctx.width, ctx.height, "RGBA8");
    const NoiseField n = makeNoise(ctx);
    std::vector<float> height;
    fillHeight(ctx, n, heightFn, height);
    paintHeightToImage(*img, height, ctx.width, ctx.height, ramp, ctx.colors, ctx.pixelSize);
    return img;
}

image::ImageData *genSoil(const Params &params, std::string &error) {
    ColorRamp ramp;
    ramp.add(0.00f, 58, 38, 22);
    ramp.add(0.35f, 92, 62, 34);
    ramp.add(0.65f, 120, 84, 48);
    ramp.add(1.00f, 148, 110, 68);
    const auto ctx = TextureGenContext::fromParams(params);
    return makeFromHeightFn(params, error, ramp, [&](float u, float v, const NoiseField &n) {
        float h = n.fbm(u, v, ctx.octaves);
        h       = h * 0.75f + 0.25f * n.valueNoise(u * 3.1f + 2.f, v * 3.1f);
        return h;
    });
}

image::ImageData *genStone(const Params &params, std::string &error) {
    ColorRamp ramp;
    ramp.add(0.00f, 52, 54, 58);
    ramp.add(0.40f, 88, 90, 96);
    ramp.add(0.70f, 130, 132, 138);
    ramp.add(1.00f, 176, 178, 184);
    const auto ctx = TextureGenContext::fromParams(params);
    return makeFromHeightFn(params, error, ramp, [&](float u, float v, const NoiseField &n) {
        float       h     = n.ridged(u, v, ctx.octaves);
        const float crack = std::fabs(n.valueNoise(u * 2.7f, v * 2.7f) - 0.5f) * 2.f;
        h                 = h * 0.7f + (1.f - crack) * 0.3f;
        return h;
    });
}

image::ImageData *genMarble(const Params &params, std::string &error) {
    ColorRamp ramp;
    ramp.add(0.00f, 230, 230, 235);
    ramp.add(0.45f, 200, 200, 210);
    ramp.add(0.55f, 120, 120, 135);
    ramp.add(0.70f, 190, 190, 200);
    ramp.add(1.00f, 245, 245, 248);
    const auto ctx = TextureGenContext::fromParams(params);
    return makeFromHeightFn(params, error, ramp, [&](float u, float v, const NoiseField &n) {
        const float w = n.warp(u, v, ctx.warpAmp * 2.5f, std::max(2, ctx.octaves - 1));
        float vein = std::sin((u + w * 4.f) * 3.14159265f * 2.f) * 0.5f + 0.5f;
        vein       = std::pow(vein, 1.6f);
        return vein * 0.65f + w * 0.35f;
    });
}

image::ImageData *genWater(const Params &params, std::string &error) {
    ColorRamp ramp;
    ramp.add(0.00f, 18, 60, 110);
    ramp.add(0.40f, 28, 96, 150);
    ramp.add(0.70f, 48, 140, 180);
    ramp.add(1.00f, 120, 200, 210);
    const auto ctx = TextureGenContext::fromParams(params);
    return makeFromHeightFn(params, error, ramp, [&](float u, float v, const NoiseField &n) {
        const float phase = float(ctx.seed % 1000u) * 0.01f;
        float       wave =
            0.5f + 0.5f * std::sin((u * 2.2f + v * 0.4f + phase) * 6.28318f +
                                   n.valueNoise(u * 1.3f, v * 1.3f) * 2.f);
        float detail = n.fbm(u * 1.5f, v * 1.5f, ctx.octaves);
        return wave * 0.55f + detail * 0.45f;
    });
}

image::ImageData *genSkyCloud(const Params &params, std::string &error) {
    ColorRamp ramp;
    ramp.add(0.00f, 70, 130, 200);
    ramp.add(0.45f, 110, 170, 220);
    ramp.add(0.70f, 180, 210, 235);
    ramp.add(1.00f, 245, 248, 252);
    const auto ctx = TextureGenContext::fromParams(params);
    return makeFromHeightFn(params, error, ramp, [&](float u, float v, const NoiseField &n) {
        const float sky = std::clamp(1.f - (v / std::max(0.01f, ctx.scale)), 0.f, 1.f);
        float clouds    = n.fbm(u * 0.9f + 3.f, v * 0.9f, ctx.octaves);
        clouds          = smoothstep(0.35f, 0.75f, clouds);
        return sky * 0.45f + clouds * 0.55f;
    });
}

}  // namespace

void TextureRecipeRegistry::registerBuiltins() {
    if (builtinsRegistered_) return;
    registerRecipe("tex.soil", genSoil);
    registerRecipe("tex.stone", genStone);
    registerRecipe("tex.marble", genMarble);
    registerRecipe("tex.water", genWater);
    registerRecipe("tex.sky_cloud", genSkyCloud);
    builtinsRegistered_ = true;
}

}  // namespace eve::procgen
