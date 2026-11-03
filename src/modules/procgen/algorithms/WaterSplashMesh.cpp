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

// Nested crowns matching the reference silhouette: tall thin spire, then
// progressively shorter and wider outward crowns that flare at the lip.
constexpr LayerSpec kDefaultLayers[] = {
    {1.00f, 0.10f, 0.045f, 0.15f},  // central jet
    {0.72f, 0.14f, 0.38f, 0.55f},   // inner crown
    {0.48f, 0.20f, 0.68f, 0.70f},   // middle crown
    {0.28f, 0.28f, 1.00f, 0.85f},   // outer ripple crown
};

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

float hash01(uint32_t n) {
    n ^= n >> 16;
    n *= 0x7feb352du;
    n ^= n >> 15;
    n *= 0x846ca68bu;
    n ^= n >> 16;
    return float(n & 0x00ffffffu) / float(0x01000000u);
}

// Deterministic angular noise for jagged crown lips (no RNG stream needed mid-shell).
float rimNoise(uint32_t seed, int layer, int radial, int sides) {
    const float a = float(radial) / float(sides);
    const float n1 = hash01(seed + uint32_t(layer * 131u + radial * 17u + 3u));
    const float n2 = hash01(seed + uint32_t(layer * 97u + ((radial * 3) % sides) * 19u + 11u));
    const float n3 = hash01(seed + uint32_t(layer * 53u + ((radial * 5) % sides) * 23u + 29u));
    const float wave = 0.55f * std::sin(a * 2.f * kPi * 7.f + float(layer)) +
                       0.30f * std::sin(a * 2.f * kPi * 13.f - float(layer) * 0.7f) +
                       0.15f * std::sin(a * 2.f * kPi * 3.f + n1 * 6.f);
    return clampf(0.5f + 0.5f * wave + 0.35f * (n2 - 0.5f) + 0.20f * (n3 - 0.5f), 0.f, 1.f);
}

float easeOutCubic(float t) {
    const float u = 1.f - t;
    return 1.f - u * u * u;
}

float radiusAt(const LayerSpec &spec, float halfW, float t) {
    const float blend = (1.f - spec.flareBias) * t + spec.flareBias * easeOutCubic(t);
    return halfW * (spec.baseRadius + (spec.topRadius - spec.baseRadius) * blend);
}

void addFlaredShell(MeshBuild &out, std::vector<float> &colors, uint32_t seed, int layerIndex,
                    const LayerSpec &spec, float height, float halfW, int radialSegments, int heightSegments,
                    float jaggedness, float foamAmount, float bodyR, float bodyG, float bodyB, float foamR,
                    float foamG, float foamB) {
    const int sides  = radialSegments;
    const int rings  = heightSegments;
    const float layerH = height * spec.heightScale;
    if (layerH < 1e-4f || halfW < 1e-4f) return;

    std::vector<float> topCut(size_t(sides), 1.f);
    for (int i = 0; i < sides; ++i) {
        const float n = rimNoise(seed, layerIndex, i, sides);
        // Keep a readable body; only carve the upper lip into a torn crown.
        topCut[size_t(i)] = clampf(1.f - jaggedness * (0.18f + 0.82f * n), 0.35f, 1.f);
    }

    const uint32_t base = uint32_t(out.getVertexCount());
    std::vector<uint32_t> indexOf(size_t((rings + 1) * sides), 0);

    for (int y = 0; y <= rings; ++y) {
        const float v = float(y) / float(rings);
        for (int x = 0; x < sides; ++x) {
            const float cut = topCut[size_t(x)];
            const float t = v * cut;
            const float angle = (float(x) / float(sides)) * 2.f * kPi;
            const float radius = radiusAt(spec, halfW, t);
            // Slight outward lip curl near the jagged rim (reference crowns flare).
            const float lip = easeOutCubic(clampf((t - 0.72f) / 0.28f, 0.f, 1.f));
            const float curl = lip * halfW * 0.04f * (0.6f + 0.4f * jaggedness);
            const float px = std::cos(angle) * (radius + curl);
            const float py = layerH * t;
            const float pz = std::sin(angle) * (radius + curl);

            // Analytic approximate normal for a flared open shell (outward).
            const float dr = (spec.topRadius - spec.baseRadius) * halfW;
            float nx = std::cos(angle);
            float ny = -dr / std::max(layerH, 1e-3f);
            float nz = std::sin(angle);
            const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= nlen;
            ny /= nlen;
            nz /= nlen;

            const float u = float(x) / float(sides);
            out.addVertex(px, py, pz, nx, ny, nz, u, t);

            // Foam gathers on jagged peaks and near the torn lip.
            const float peak = topCut[size_t(x)];
            const float foamMask =
                foamAmount * clampf((t - (0.55f + 0.25f * (1.f - jaggedness))) / 0.45f, 0.f, 1.f) *
                (0.35f + 0.65f * peak);
            colors.push_back(bodyR + (foamR - bodyR) * foamMask);
            colors.push_back(bodyG + (foamG - bodyG) * foamMask);
            colors.push_back(bodyB + (foamB - bodyB) * foamMask);
            colors.push_back(1.f);

            indexOf[size_t(y * sides + x)] = base + uint32_t(y * sides + x);
        }
    }

    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < sides; ++x) {
            const int nx = (x + 1) % sides;
            const uint32_t a = indexOf[size_t(y * sides + x)];
            const uint32_t b = indexOf[size_t(y * sides + nx)];
            const uint32_t c = indexOf[size_t((y + 1) * sides + x)];
            const uint32_t d = indexOf[size_t((y + 1) * sides + nx)];
            // Double-sided so unlit crowns read from both inside and outside.
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
    const int   sides  = std::clamp(params.getInt("radialSegments", 28), 8, 64);
    const int   rings  = std::clamp(params.getInt("heightSegments", 10), 3, 32);
    const float jagged = clampf(params.getFloat("jaggedness", 0.78f), 0.f, 1.f);
    const float foam   = clampf(params.getFloat("foam", 0.85f), 0.f, 1.f);
    const float flare  = clampf(params.getFloat("flare", 1.0f), 0.35f, 1.75f);

    const float bodyR = clampf(params.getFloat("bodyR", 0.22f), 0.f, 1.f);
    const float bodyG = clampf(params.getFloat("bodyG", 0.78f), 0.f, 1.f);
    const float bodyB = clampf(params.getFloat("bodyB", 0.82f), 0.f, 1.f);
    const float foamR = clampf(params.getFloat("foamR", 0.95f), 0.f, 1.f);
    const float foamG = clampf(params.getFloat("foamG", 0.98f), 0.f, 1.f);
    const float foamB = clampf(params.getFloat("foamB", 1.00f), 0.f, 1.f);

    if (onlyLayer >= layers) {
        error = "mesh.water_splash: onlyLayer must be < layers";
        return false;
    }

    const uint32_t seed = params.getSeed();
    out.clear();
    out.setActiveGroup("splash");
    std::vector<float> colors;
    colors.reserve(size_t(layers) * size_t(sides) * size_t(rings + 1) * 4u);

    for (int i = 0; i < layers; ++i) {
        if (onlyLayer >= 0 && i != onlyLayer) continue;
        LayerSpec spec = kDefaultLayers[i];
        // Flare scales only the crown lips (not the central jet base).
        if (i > 0) {
            spec.topRadius *= flare;
            spec.flareBias = clampf(spec.flareBias * (0.65f + 0.35f * flare), 0.f, 1.f);
        }
        const std::string group = i == 0 ? "spire" : ("crown" + std::to_string(i));
        out.setActiveGroup(group);
        addFlaredShell(out, colors, seed, i, spec, height, halfW, sides, rings, jagged, foam, bodyR, bodyG,
                       bodyB, foamR, foamG, foamB);
    }

    out.setMeta("recipe", "mesh.water_splash");
    out.setMeta("seed", std::to_string(seed));
    out.setMeta("layers", std::to_string(layers));
    if (out.empty()) {
        error = "mesh.water_splash: generated an empty mesh";
        return false;
    }
    if (int(colors.size()) != out.getVertexCount() * 4) {
        error = "mesh.water_splash: vertex color stream length mismatch";
        return false;
    }
    const auto colorResult = out.setVertexColors(std::move(colors));
    if (!colorResult.ok()) {
        error = "mesh.water_splash: failed to write vertex colors (" + colorResult.status().describe() + ")";
        return false;
    }
    return true;
}

}  // namespace eve::procgen
