#include "procgen/algorithms/WaterSplashMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace eve::procgen {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct LayerSpec {
    float heightScale;  // relative to overall height
    float baseRadius;   // relative to half-width
    float topRadius;    // relative to half-width
    float flareBias;    // 0 = linear, 1 = ease-out flare
};

// Clean clay-sheet crowns (reference guide mesh): continuous nested flares.
// Jagged water lips / foam live in the semi-transparent material, not here.
constexpr LayerSpec kDefaultLayers[] = {
    {1.00f, 0.10f, 0.045f, 0.15f},  // central jet
    {0.72f, 0.14f, 0.38f, 0.55f},   // inner crown
    {0.48f, 0.20f, 0.68f, 0.70f},   // middle crown
    {0.28f, 0.28f, 1.00f, 0.85f},   // outer ripple crown
};

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

float easeOutCubic(float t) {
    const float u = 1.f - t;
    return 1.f - u * u * u;
}

float radiusAt(const LayerSpec &spec, float halfW, float t) {
    const float blend = (1.f - spec.flareBias) * t + spec.flareBias * easeOutCubic(t);
    return halfW * (spec.baseRadius + (spec.topRadius - spec.baseRadius) * blend);
}

void addFlaredSheet(MeshBuild &out, const LayerSpec &spec, float height, float halfW, int radialSegments,
                    int heightSegments) {
    const int   sides  = radialSegments;
    const int   rings  = heightSegments;
    const float layerH = height * spec.heightScale;
    if (layerH < 1e-4f || halfW < 1e-4f) return;

    const uint32_t base = uint32_t(out.getVertexCount());
    for (int y = 0; y <= rings; ++y) {
        const float v = float(y) / float(rings);
        for (int x = 0; x < sides; ++x) {
            const float u      = float(x) / float(sides);
            const float angle  = u * 2.f * kPi;
            const float radius = radiusAt(spec, halfW, v);
            // Mild outward lip curl so crowns read as open sheets, not tubes.
            const float lip  = easeOutCubic(clampf((v - 0.72f) / 0.28f, 0.f, 1.f));
            const float curl = lip * halfW * 0.035f;
            const float px   = std::cos(angle) * (radius + curl);
            const float py   = layerH * v;
            const float pz   = std::sin(angle) * (radius + curl);

            const float dr   = (spec.topRadius - spec.baseRadius) * halfW;
            float       nx   = std::cos(angle);
            float       ny   = -dr / std::max(layerH, 1e-3f);
            float       nz   = std::sin(angle);
            const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= nlen;
            ny /= nlen;
            nz /= nlen;

            // UV.v = 0 at ground, 1 at lip — the splash material drives alpha/foam from this.
            out.addVertex(px, py, pz, nx, ny, nz, u, v);
        }
    }

    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < sides; ++x) {
            const int      nx = (x + 1) % sides;
            const uint32_t a  = base + uint32_t(y * sides + x);
            const uint32_t b  = base + uint32_t(y * sides + nx);
            const uint32_t c  = base + uint32_t((y + 1) * sides + x);
            const uint32_t d  = base + uint32_t((y + 1) * sides + nx);
            // Double-sided sheets so the interior of each crown stays visible.
            out.addTriangle(a, b, c);
            out.addTriangle(b, d, c);
            out.addTriangle(c, b, a);
            out.addTriangle(c, d, b);
        }
    }
}

}  // namespace

bool generateWaterSplashMesh(const Params &params, MeshBuild &out, std::string &error) {
    const float height = std::max(0.2f, params.getFloat("height", 2.4f));
    const float width  = std::max(0.3f, params.getFloat("width", 3.2f));
    const float halfW  = width * 0.5f;
    const int   layers = std::clamp(params.getInt("layers", 4), 2, 4);
    // onlyLayer < 0 emits every active layer; 0..layers-1 emits a single crown for
    // staggered VFX playback without rebuilding the shared silhouette table.
    const int   onlyLayer = params.getInt("onlyLayer", -1);
    const int   sides     = std::clamp(params.getInt("radialSegments", 32), 8, 64);
    const int   rings     = std::clamp(params.getInt("heightSegments", 12), 3, 32);
    const float flare     = clampf(params.getFloat("flare", 1.0f), 0.35f, 1.75f);

    if (onlyLayer >= layers) {
        error = "mesh.water_splash: onlyLayer must be < layers";
        return false;
    }

    const uint32_t seed = params.getSeed();
    out.clear();
    out.setActiveGroup("splash");

    for (int i = 0; i < layers; ++i) {
        if (onlyLayer >= 0 && i != onlyLayer) continue;
        LayerSpec spec = kDefaultLayers[i];
        if (i > 0) {
            spec.topRadius *= flare;
            spec.flareBias = clampf(spec.flareBias * (0.65f + 0.35f * flare), 0.f, 1.f);
        }
        const std::string group = i == 0 ? "spire" : ("crown" + std::to_string(i));
        out.setActiveGroup(group);
        addFlaredSheet(out, spec, height, halfW, sides, rings);
    }

    out.setMeta("recipe", "mesh.water_splash");
    out.setMeta("seed", std::to_string(seed));
    out.setMeta("layers", std::to_string(layers));
    out.setMeta("sheet", "clay");
    if (out.empty()) {
        error = "mesh.water_splash: generated an empty mesh";
        return false;
    }
    return true;
}

}  // namespace eve::procgen
