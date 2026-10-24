#include "procgen/texture/EverydayPatterns.h"
#include <algorithm>
#include <cmath>

namespace eve::procgen::detail {
namespace {
constexpr float tau = 6.28318530718f;
float           noise(float u, float v, int x, int y, uint32_t seed) {
    NoiseField n;
    n.seed    = seed;
    n.periodX = x;
    n.periodY = y;
    return n.valueNoise(u * float(x), v * float(y));
}
float smooth(float lo, float hi, float x) {
    const float t = std::clamp((x - lo) / (hi - lo), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
struct Cell {
    float distance, edge;
};
Cell cell(float u, float v, int count, uint32_t seed) {
    NoiseField n;
    n.seed    = seed;
    n.periodX = n.periodY = count;
    const float x = u * float(count), y = v * float(count);
    const int   cx = int(std::floor(x)), cy = int(std::floor(y));
    float       first = 100.f, second = 100.f;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const int   ix = cx + dx, iy = cy + dy;
            const float px    = float(ix) + .15f + .7f * n.hash01(ix, iy);
            NoiseField  other = n;
            other.seed ^= 0x9e3779b9u;
            const float py = float(iy) + .15f + .7f * other.hash01(ix, iy);
            const float d  = (x - px) * (x - px) + (y - py) * (y - py);
            if (d < first) {
                second = first;
                first  = d;
            } else
                second = std::min(second, d);
        }
    return {std::sqrt(first), std::sqrt(second) - std::sqrt(first)};
}
}  // namespace
EverydaySample sampleEveryday(EverydayMaterial family, float u, float v, uint32_t seed, int repeats,
                              [[maybe_unused]] int bandwidth) {
    const int   fine = repeats * 48, medium = repeats * 12;
    const float a = noise(u, v, 4, 4, seed), b = noise(u, v, medium, medium, seed + 13);
    const float c     = noise(u, v, fine, fine, seed + 71);
    const float d     = noise(u, v, std::max(2, fine / 2), std::max(2, fine / 2), seed + 133);
    const float micro = .6f * c + .4f * d;
    switch (family) {
        case EverydayMaterial::Wood: {
            const float x      = std::sin(3.14159265f * (u - .25f));
            const float y      = std::sin(3.14159265f * (v - .5f)) * .65f;
            const float radius = std::sqrt(x * x + y * y + .03f);
            const float warp  = .9f * (noise(u, v, 4, 6, seed + 3) - .5f) + .16f * (noise(u, v, 12, 8, seed + 4) - .5f);
            const float phase = radius * float(repeats * 9) + warp * 1.7f;
            NoiseField  growth;
            growth.seed           = seed + 55;
            growth.periodY        = 6;
            const int   ringIndex = int(std::floor(phase));
            const float position  = phase - float(ringIndex);
            const float width     = .04f + .10f * growth.hash01(ringIndex, 0);
            const float latewood  = 1.f - smooth(width, width + .10f, position);
            // Vessels follow the same deformed growth coordinate as the annual rings.
            const float vessel   = growth.valueNoise(phase * 9.f, v * 6.f);
            const float pore     = std::pow(std::max(0.f, 1.f - vessel * 2.1f), 3.f);
            const float ringTone = growth.valueNoise(phase, v * 6.f);
            return {.68f - .14f * latewood - .25f * pore + .35f * (ringTone - .5f) + .10f * (a - .5f),
                    .5f - .008f * latewood - .32f * pore + .012f * (micro - .5f), .36f + .42f * pore + .12f * latewood};
        }
        case EverydayMaterial::Cloth: {
            const int   threads = std::max(2, (std::min(repeats * 10, std::max(2, fine / 2)) / 2) * 2);
            const float x = u * float(threads), y = v * float(threads);
            const float sx = .5f + .5f * std::cos(tau * x), sy = .5f + .5f * std::cos(tau * y);
            // Alternating over/under yarns, rounded crowns and small twisted fibers.
            const float over  = .5f + .5f * std::cos(3.14159265f * x) * std::cos(3.14159265f * y);
            const float warp  = std::pow(sx, .6f) * (.32f + .68f * over);
            const float weft  = std::pow(sy, .6f) * (1.f - .68f * over);
            const float crown = std::max(warp, weft);
            const float fiber = noise(u, v, fine, fine, seed + 19);
            return {.38f + .33f * crown + .12f * (fiber - .5f), .5f + .46f * (crown - .5f) + .035f * (fiber - .5f),
                    .65f + .30f * (1.f - crown)};
        }
        case EverydayMaterial::Metal:
        case EverydayMaterial::Aluminum: {
            const bool     aluminum  = family == EverydayMaterial::Aluminum;
            const int      lines     = repeats * (aluminum ? 90 : 70);
            const uint32_t grainSeed = seed + (aluminum ? 207u : 43u);
            const float    groove =
                .6f * noise(u, v, 8, lines, grainSeed) + .4f * noise(u, v, 24, lines / 2, grainSeed + 7);
            const float scratch = std::pow(std::max(0.f, 1.f - groove * 2.2f), 3.f);
            return {.5f + (aluminum ? .025f : .045f) * (groove - .5f), .5f - (aluminum ? .045f : .075f) * scratch,
                    .28f + .40f * groove};
        }
        case EverydayMaterial::Steel:
            // Polished steel has broad clean reflections; brushed grooves belong to metal/Al.
            return {.5f + .018f * (a - .5f), .5f + .0015f * (micro - .5f), .25f + .12f * b};
        case EverydayMaterial::Paper: {
            // One short randomly oriented cellulose fiber per jittered lattice cell.
            const int  count = std::min(std::max(3, repeats * 12), std::max(3, fine / 2));
            NoiseField hashes;
            hashes.seed    = seed + 91;
            hashes.periodX = hashes.periodY = count;
            const float px = u * float(count), py = v * float(count);
            const int   cx = int(std::floor(px)), cy = int(std::floor(py));
            float       fibers = 0.f;
            for (int j = -1; j <= 1; ++j)
                for (int i = -1; i <= 1; ++i) {
                    const int   ix = cx + i, iy = cy + j;
                    const float h = hashes.hash01(ix, iy), angle = tau * h;
                    const float dx = px - float(ix) - h, dy = py - float(iy) - hashes.hash01(ix + 19, iy + 31);
                    const float along  = dx * std::cos(angle) + dy * std::sin(angle);
                    const float across = -dx * std::sin(angle) + dy * std::cos(angle);
                    const float fiber =
                        (1.f - smooth(.025f, .11f, std::abs(across))) * (1.f - smooth(.20f, .48f, std::abs(along)));
                    fibers = std::max(fibers, fiber);
                }
            return {.64f + .10f * (a - .5f) + .12f * (micro - .5f) - .18f * fibers,
                    .5f + .05f * (micro - .5f) + .035f * fibers, .65f + .22f * micro};
        }
        case EverydayMaterial::Marble: {
            // Seeded flowing veins with finite, tapering forks. No closed noise contours.
            NoiseField n;
            n.seed             = seed + 311;
            const float flow   = v + .08f * (noise(u, v, 8, 8, seed + 19) - .5f);
            const float across = u + v + .06f * (noise(u, v, 12, 12, seed + 20) - .5f);
            const int   veins  = std::clamp(repeats, 2, 12);
            float       core = 0.f, halo = 0.f, forks = 0.f;
            for (int i = 0; i < veins; ++i) {
                const float phase  = tau * n.hash01(i, 1);
                const float center = (float(i) + .2f + .6f * n.hash01(i, 2)) / float(veins) +
                                     .15f * std::sin(tau * flow + phase) +
                                     .045f * std::sin(tau * 3.f * flow + phase * 1.7f);
                const float width    = .004f + .008f * noise(u, v, 8, 8, seed + uint32_t(i));
                const float distance = std::abs((across - center) - std::floor(across - center + .5f));
                const float strength = .65f + .35f * n.hash01(i, 3);
                core                 = std::max(core, (1.f - smooth(width, width + .006f, distance)) * strength);
                halo                 = std::max(halo, 1.f - smooth(width, .045f, distance));
                for (int j = 0; j < 3; ++j) {
                    const float start     = n.hash01(i, j + 10);
                    const float t         = (flow - start) - std::floor(flow - start);
                    const float length    = .18f + .24f * n.hash01(i, j + 20);
                    const float support   = 1.f - smooth(length * .7f, length, t);
                    const float direction = n.hash01(i, j + 30) > .5f ? 1.f : -1.f;
                    const float branch    = center + direction * t * (.35f + .45f * n.hash01(i, j + 40)) +
                                         .012f * std::sin(tau * 5.f * t + phase) * smooth(0.f, .06f, t);
                    const float d = std::abs((across - branch) - std::floor(across - branch + .5f));
                    forks         = std::max(forks, (1.f - smooth(.0015f, .005f, d)) * support);
                }
            }
            const float cloud = .6f * noise(u, v, repeats * 3, repeats * 3, seed + 45) +
                                .4f * noise(u, v, repeats * 7, repeats * 7, seed + 46);
            return {.68f - .30f * core - .16f * forks - .08f * halo - .32f * cloud, .5f + .003f * (micro - .5f),
                    .25f + .12f * core};
        }
        case EverydayMaterial::Cement: {
            // Aggregate and mortar vary above and below the mean; pores are sparse accents.
            const float aggregate = .6f * noise(u, v, repeats * 10, repeats * 10, seed + 51) +
                                    .4f * noise(u, v, repeats * 22, repeats * 22, seed + 52);
            const Cell  pores = cell(u, v, std::max(3, repeats * 3), seed + 7);
            const float pore  = (1.f - smooth(.025f, .12f, pores.distance)) * smooth(.50f, .78f, a);
            return {.52f + .28f * (a - .5f) + .32f * (aggregate - .5f) - .20f * pore,
                    .5f + .65f * (aggregate - .5f) + .12f * (micro - .5f) - .10f * pore, .62f + .26f * aggregate};
        }
        case EverydayMaterial::CastIron: {
            const float grains = .6f * noise(u, v, repeats * 12, repeats * 12, seed + 53) +
                                 .4f * noise(u, v, repeats * 24, repeats * 24, seed + 54);
            const Cell  crust  = cell(u, v, std::max(3, repeats * 8), seed + 37);
            const float raised = 1.f - smooth(.10f, .42f, crust.distance);
            return {.5f + .24f * (a - .5f) + .34f * (grains - .5f) + .10f * raised,
                    .5f + .65f * (grains - .5f) + .22f * (raised - .3f), .65f + .30f * grains};
        }
        case EverydayMaterial::Rubber: {
            const float grain = .6f * noise(u, v, repeats * 16, repeats * 16, seed + 61) + .4f * micro;
            return {.5f + .04f * (a - .5f) + .04f * (grain - .5f), .5f + .42f * (grain - .5f), .65f + .3f * grain};
        }
        case EverydayMaterial::Plastic:
            return {.5f + .025f * (a - .5f), .5f + .032f * (b - .5f) + .012f * (micro - .5f), .35f + .25f * b};
        case EverydayMaterial::Paint: {
            const float peel = .65f * noise(u, v, repeats * 14, repeats * 14, seed + 81) +
                               .35f * noise(u, v, repeats * 28, repeats * 28, seed + 82);
            return {.5f + .025f * (a - .5f), .5f + .42f * (peel - .5f) + .06f * (micro - .5f), .35f + .35f * peel};
        }
        case EverydayMaterial::Glass:
            return {.5f + .008f * (a - .5f), .5f + .0005f * (micro - .5f), .5f + .03f * micro};
    }
    return {};
}
}  // namespace eve::procgen::detail
