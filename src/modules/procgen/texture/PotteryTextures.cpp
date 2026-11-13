#include "procgen/texture/PotteryTextures.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace eve::procgen {
namespace {
constexpr float tau = 6.283185307179586f;
enum class Finish { Clay, Glaze, Porcelain, Celadon, Stoneware };

NoiseField layer(const NoiseField& source, int frequency, uint32_t stream) {
    auto noise = source;
    noise.seed ^= stream;
    // Scale the lattice period with frequency, preserving the complete UV tile
    // instead of repeating a tiny noise cell many times around the vessel.
    noise.periodX *= frequency;
    noise.periodY *= frequency;
    return noise;
}
float grain(const NoiseField& source, float u, float v, int frequency, uint32_t stream) {
    const auto noise = layer(source, frequency, stream);
    return noise.valueNoise(u * frequency, v * frequency);
}
float smooth(float low, float high, float x) {
    const float t = std::clamp((x - low) / (high - low), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
struct Cells {
    float pore  = 0.f;
    float crack = 0.f;
};
Cells cells(const NoiseField& source, float u, float v, int frequency) {
    const auto  noise = layer(source, frequency, 0xa511e9b3u);
    const float x = u * frequency, y = v * frequency;
    const int   ix = int(std::floor(x)), iy = int(std::floor(y));
    float       first = 100.f, second = 100.f, pore = 0.f;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            const int   cx = ix + i, cy = iy + j;
            const float sx       = cx + 0.2f + 0.6f * noise.hash01(cx, cy);
            const auto  offset   = layer(source, frequency, 0x63d83595u);
            const float sy       = cy + 0.2f + 0.6f * offset.hash01(cx, cy);
            const float distance = std::hypot(x - sx, y - sy);
            if (distance < first) {
                second = first;
                first  = distance;
            } else
                second = std::min(second, distance);
            const auto  pits      = layer(source, frequency, 0xb5297a4du);
            const float selection = pits.hash01(cx, cy);
            if (selection > 0.58f) {
                const float radius = 0.07f + 0.10f * selection;
                pore               = std::max(pore, 1.f - smooth(radius * 0.25f, radius, distance));
            }
        }
    }
    return {pore, 1.f - smooth(0.012f, 0.055f, second - first)};
}
float surfaceHeight(Finish finish, float u, float v, const NoiseField& noise) {
    const float broad = 0.65f * grain(noise, u, v, 1, 0x9e3779b9u) + 0.35f * grain(noise, u, v, 3, 0x68e31da4u);
    const float fine  = grain(noise, u, v, 24, 0x1b56c4e9u) - 0.5f;
    const float wave  = grain(noise, u, v, 2, 0x7f4a7c15u) - 0.5f;
    const float rings = std::sin(tau * (v * 12.f + wave * 0.12f + noise.hash01(0, 0)));
    if (finish == Finish::Celadon) {
        const auto cell = cells(noise, u + wave * 0.035f, v + wave * 0.035f, 4);
        return std::clamp(0.63f + 0.18f * (broad - 0.5f) + 0.045f * fine - 0.31f * cell.crack + 0.007f * rings, 0.f,
                          1.f);
    }
    const auto cell = cells(noise, u, v, 12);
    switch (finish) {
        case Finish::Clay:
            return std::clamp(0.55f + 0.34f * (broad - 0.5f) + 0.23f * fine - 0.30f * cell.pore + 0.008f * rings, 0.f,
                              1.f);
        case Finish::Glaze: {
            const float flow = grain(noise, u + wave * 0.08f, v, 6, 0x94d049bbu);
            return std::clamp(0.53f + 0.55f * (broad - 0.5f) + 0.20f * (flow - 0.5f) + 0.09f * fine -
                                  0.055f * cell.pore + 0.01f * rings,
                              0.f, 1.f);
        }
        case Finish::Porcelain:
            return std::clamp(0.60f + 0.12f * (broad - 0.5f) + 0.07f * fine - 0.04f * cell.pore + 0.004f * rings, 0.f,
                              1.f);
        case Finish::Stoneware:
            return std::clamp(0.54f + 0.55f * (broad - 0.5f) + 0.26f * fine - 0.25f * cell.pore + 0.012f * rings, 0.f,
                              1.f);
        case Finish::Celadon: break;
    }
    return 0.5f;
}
}  // namespace

std::vector<TextureRecipeDef> potteryTextureDefinitions() {
    struct Surface {
        const char* id;
        Finish      finish;
        Rgba8       low, high;
        float       roughLow, roughHigh, normal, relief;
    };
    const Surface surfaces[] = {
        {"tex.terracotta", Finish::Clay, {74, 30, 17, 255}, {218, 137, 83, 255}, 0.95f, 0.72f, 1.3f, 0.16f},
        {"tex.glazed_ceramic", Finish::Glaze, {8, 23, 55, 255}, {53, 135, 177, 255}, 0.28f, 0.12f, 0.7f, 0.025f},
        {"tex.porcelain", Finish::Porcelain, {210, 211, 204, 255}, {255, 252, 240, 255}, 0.19f, 0.11f, 0.8f, 0.08f},
        {"tex.celadon", Finish::Celadon, {67, 69, 51, 255}, {166, 201, 169, 255}, 0.40f, 0.15f, 0.9f, 0.025f},
        {"tex.stoneware", Finish::Stoneware, {37, 22, 13, 255}, {181, 137, 87, 255}, 0.76f, 0.40f, 1.1f, 0.12f}};
    std::vector<TextureRecipeDef> definitions;
    for (const auto& surface : surfaces) {
        TextureRecipeDef def;
        def.id = surface.id;
        def.albedo.add(0.f, surface.low.r, surface.low.g, surface.low.b);
        def.albedo.add(1.f, surface.high.r, surface.high.g, surface.high.b);
        const auto finish = surface.finish;
        def.height = [finish](float u, float v, const NoiseField& noise) { return surfaceHeight(finish, u, v, noise); };
        def.pbr.roughnessLow   = surface.roughLow;
        def.pbr.roughnessHigh  = surface.roughHigh;
        def.pbr.normalStrength = surface.normal;
        def.pbr.heightStrength = surface.relief;
        def.pbr.aoStrength     = surface.finish == Finish::Clay || surface.finish == Finish::Stoneware ? 0.8f : 0.2f;
        def.pbr.metallic       = 0.f;
        definitions.push_back(std::move(def));
    }
    return definitions;
}
}  // namespace eve::procgen
