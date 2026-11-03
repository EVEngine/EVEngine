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

// Nested continuous crowns: tall central jet, then shorter wider flared skirts.
constexpr LayerSpec kDefaultLayers[] = {
    {1.00f, 0.085f, 0.055f, 0.20f},  // central jet
    {0.78f, 0.12f, 0.42f, 0.62f},    // inner crown
    {0.52f, 0.18f, 0.72f, 0.78f},    // middle crown
    {0.30f, 0.26f, 1.00f, 0.90f},    // outer ripple crown
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

float easeOutCubic(float t) {
    const float u = 1.f - t;
    return 1.f - u * u * u;
}

float easeInOut(float t) {
    return t < 0.5f ? 2.f * t * t : 1.f - std::pow(-2.f * t + 2.f, 2.f) * 0.5f;
}

float radiusAt(const LayerSpec &spec, float halfW, float t) {
    const float blend = (1.f - spec.flareBias) * t + spec.flareBias * easeOutCubic(t);
    return halfW * (spec.baseRadius + (spec.topRadius - spec.baseRadius) * blend);
}

// Angular crown profile in [0,1]: high = taller jagged tip (foam peak).
float rimProfile(uint32_t seed, int layer, int radial, int sides) {
    const float a = float(radial) / float(sides);
    const float n1 = hash01(seed + uint32_t(layer * 131u + radial * 17u + 3u));
    const float n2 = hash01(seed + uint32_t(layer * 97u + ((radial * 3) % sides) * 19u + 11u));
    const float n3 = hash01(seed + uint32_t(layer * 53u + ((radial * 5) % sides) * 23u + 29u));
    const float wave = 0.48f * std::sin(a * 2.f * kPi * 9.f + float(layer) * 0.9f) +
                       0.28f * std::sin(a * 2.f * kPi * 17.f - float(layer) * 1.3f) +
                       0.16f * std::sin(a * 2.f * kPi * 5.f + n1 * 5.f) +
                       0.08f * std::sin(a * 2.f * kPi * 23.f + n2 * 3.f);
    return clampf(0.5f + 0.5f * wave + 0.28f * (n3 - 0.5f), 0.f, 1.f);
}

// Vertical streak noise used for foam streaks on the body (reference look).
float streakNoise(uint32_t seed, int layer, int radial, float v) {
    const float a = float(radial);
    const float n = hash01(seed + uint32_t(layer * 71u + radial * 13u + 41u));
    const float s =
        0.55f * std::sin(a * 0.73f + v * 14.f + float(layer)) + 0.45f * std::sin(a * 1.91f - v * 9.f + n * 6.f);
    return clampf(0.5f + 0.5f * s, 0.f, 1.f);
}

void pushColor(std::vector<float> &colors, float r, float g, float b, float a) {
    colors.push_back(r);
    colors.push_back(g);
    colors.push_back(b);
    colors.push_back(a);
}

void addDoubleQuad(MeshBuild &out, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    out.addTriangle(a, b, c);
    out.addTriangle(b, d, c);
    out.addTriangle(c, b, a);
    out.addTriangle(c, d, b);
}

void addFlaredShell(MeshBuild &out, std::vector<float> &colors, uint32_t seed, int layerIndex,
                    const LayerSpec &spec, float height, float halfW, int radialSegments, int heightSegments,
                    float jaggedness, float foamAmount, float bodyR, float bodyG, float bodyB, float foamR,
                    float foamG, float foamB) {
    const int sides = radialSegments;
    const int rings = heightSegments;
    const float layerH = height * spec.heightScale;
    if (layerH < 1e-4f || halfW < 1e-4f) return;

    std::vector<float> rim(size_t(sides), 0.5f);
    for (int i = 0; i < sides; ++i) rim[size_t(i)] = rimProfile(seed, layerIndex, i, sides);

    const uint32_t base = uint32_t(out.getVertexCount());
    std::vector<uint32_t> indexOf(size_t((rings + 1) * sides), 0);

    for (int y = 0; y <= rings; ++y) {
        const float v = float(y) / float(rings);
        for (int x = 0; x < sides; ++x) {
            const float peak = rim[size_t(x)];
            // Continuous sheet: jaggedness only pulls the top lip, not the whole column.
            const float rimPull = jaggedness * (0.08f + 0.34f * peak);
            const float t = v * (1.f - rimPull * easeInOut(v));
            const float angle = (float(x) / float(sides)) * 2.f * kPi;
            float radius = radiusAt(spec, halfW, t);
            // Peaks flare a little farther outward (crown teeth).
            radius += halfW * jaggedness * 0.055f * peak * easeOutCubic(v);
            const float lip = easeOutCubic(clampf((v - 0.70f) / 0.30f, 0.f, 1.f));
            const float curl = lip * halfW * (0.035f + 0.05f * jaggedness * peak);
            const float px = std::cos(angle) * (radius + curl);
            const float py = layerH * (t + jaggedness * 0.12f * peak * std::pow(v, 3.f));
            const float pz = std::sin(angle) * (radius + curl);

            const float dr = (spec.topRadius - spec.baseRadius) * halfW;
            float nx = std::cos(angle);
            float ny = -dr / std::max(layerH, 1e-3f);
            float nz = std::sin(angle);
            const float nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= nlen;
            ny /= nlen;
            nz /= nlen;

            out.addVertex(px, py, pz, nx, ny, nz, float(x) / float(sides), v);

            // Foam sits on the upper lip and rides vertical streaks (浪花).
            const float lipFoam = easeOutCubic(clampf((v - 0.58f) / 0.42f, 0.f, 1.f));
            const float peakFoam = lipFoam * (0.25f + 0.75f * peak);
            const float streak =
                streakNoise(seed, layerIndex, x, v) * easeOutCubic(clampf((v - 0.35f) / 0.65f, 0.f, 1.f));
            const float foamMask = clampf(foamAmount * (0.72f * peakFoam + 0.55f * streak * lipFoam), 0.f, 1.f);
            pushColor(colors, bodyR + (foamR - bodyR) * foamMask, bodyG + (foamG - bodyG) * foamMask,
                      bodyB + (foamB - bodyB) * foamMask, 1.f);

            indexOf[size_t(y * sides + x)] = base + uint32_t(y * sides + x);
        }
    }

    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < sides; ++x) {
            const int nx = (x + 1) % sides;
            addDoubleQuad(out, indexOf[size_t(y * sides + x)], indexOf[size_t(y * sides + nx)],
                          indexOf[size_t((y + 1) * sides + x)], indexOf[size_t((y + 1) * sides + nx)]);
        }
    }

    // White foam lip ribbon along the jagged rim — the missing 浪花 crest.
    if (foamAmount > 0.02f) {
        const uint32_t lipBase = uint32_t(out.getVertexCount());
        const float lipWidth = halfW * (0.028f + 0.04f * foamAmount);
        const float lipLift = layerH * 0.018f;
        for (int x = 0; x < sides; ++x) {
            const float peak = rim[size_t(x)];
            const float angle = (float(x) / float(sides)) * 2.f * kPi;
            const float v = 1.f;
            const float rimPull = jaggedness * (0.08f + 0.34f * peak);
            const float t = 1.f - rimPull;
            float radius = radiusAt(spec, halfW, t) + halfW * jaggedness * 0.055f * peak;
            radius += halfW * 0.04f;
            const float cx = std::cos(angle);
            const float cz = std::sin(angle);
            const float py = layerH * (t + jaggedness * 0.12f * peak) + lipLift * (0.4f + 0.6f * peak);
            const float inner = std::max(0.f, radius - lipWidth * (0.55f + 0.45f * peak));
            const float outer = radius + lipWidth * (0.85f + 0.55f * peak);
            out.addVertex(cx * inner, py, cz * inner, 0.f, 1.f, 0.f, float(x) / float(sides), v);
            out.addVertex(cx * outer, py + lipLift * peak, cz * outer, 0.f, 1.f, 0.f, float(x) / float(sides),
                          v + 0.05f);
            // Near-pure white on the crest; slightly softer on low troughs.
            const float whiteness = clampf(0.55f + 0.45f * peak * foamAmount, 0.f, 1.f);
            const float fr = bodyR + (foamR - bodyR) * whiteness;
            const float fg = bodyG + (foamG - bodyG) * whiteness;
            const float fb = bodyB + (foamB - bodyB) * whiteness;
            pushColor(colors, fr, fg, fb, 1.f);
            pushColor(colors, foamR, foamG, foamB, 1.f);
        }
        for (int x = 0; x < sides; ++x) {
            const uint32_t n = uint32_t((x + 1) % sides);
            const uint32_t a = lipBase + uint32_t(x) * 2u;
            const uint32_t b = lipBase + uint32_t(x) * 2u + 1u;
            const uint32_t c = lipBase + n * 2u;
            const uint32_t d = lipBase + n * 2u + 1u;
            addDoubleQuad(out, a, b, c, d);
        }
    }

    // Detached white spray blades on the tallest peaks.
    if (foamAmount > 0.05f && jaggedness > 0.05f) {
        for (int x = 0; x < sides; ++x) {
            const float peak = rim[size_t(x)];
            if (peak < 0.62f) continue;
            if ((hash01(seed + uint32_t(layerIndex * 200u + x * 17u)) * foamAmount) < 0.35f) continue;

            const float angle = (float(x) / float(sides)) * 2.f * kPi;
            const float rimPull = jaggedness * (0.08f + 0.34f * peak);
            const float t = 1.f - rimPull;
            float radius = radiusAt(spec, halfW, t) + halfW * jaggedness * 0.055f * peak + halfW * 0.05f;
            const float cx = std::cos(angle);
            const float cz = std::sin(angle);
            const float py = layerH * (t + jaggedness * 0.12f * peak);
            const float bladeH = layerH * (0.06f + 0.10f * peak) * foamAmount;
            const float bladeW = halfW * (0.018f + 0.022f * peak);
            const float ox = -cz;
            const float oz = cx;

            const uint32_t b0 = uint32_t(out.getVertexCount());
            out.addVertex(cx * radius - ox * bladeW, py, cz * radius - oz * bladeW, cx, 0.2f, cz, 0.f, 1.f);
            out.addVertex(cx * radius + ox * bladeW, py, cz * radius + oz * bladeW, cx, 0.2f, cz, 1.f, 1.f);
            out.addVertex(cx * (radius + halfW * 0.02f), py + bladeH, cz * (radius + halfW * 0.02f), cx, 0.6f, cz,
                          0.5f, 0.f);
            pushColor(colors, foamR, foamG, foamB, 1.f);
            pushColor(colors, foamR, foamG, foamB, 1.f);
            pushColor(colors, foamR, foamG, foamB, 1.f);
            out.addTriangle(b0, b0 + 1, b0 + 2);
            out.addTriangle(b0 + 2, b0 + 1, b0);
        }
    }
}

}  // namespace

bool generateWaterSplashMesh(const Params &params, MeshBuild &out, std::string &error) {
    const float height = std::max(0.2f, params.getFloat("height", 2.4f));
    const float width = std::max(0.3f, params.getFloat("width", 3.2f));
    const float halfW = width * 0.5f;
    const int layers = std::clamp(params.getInt("layers", 4), 2, 4);
    // onlyLayer < 0 emits every active layer; 0..layers-1 emits a single crown for
    // staggered VFX playback without rebuilding the shared silhouette table.
    const int onlyLayer = params.getInt("onlyLayer", -1);
    const int sides = std::clamp(params.getInt("radialSegments", 36), 8, 64);
    const int rings = std::clamp(params.getInt("heightSegments", 14), 3, 32);
    const float jagged = clampf(params.getFloat("jaggedness", 0.72f), 0.f, 1.f);
    const float foam = clampf(params.getFloat("foam", 1.0f), 0.f, 1.f);
    const float flare = clampf(params.getFloat("flare", 1.05f), 0.35f, 1.75f);

    const float bodyR = clampf(params.getFloat("bodyR", 0.18f), 0.f, 1.f);
    const float bodyG = clampf(params.getFloat("bodyG", 0.72f), 0.f, 1.f);
    const float bodyB = clampf(params.getFloat("bodyB", 0.78f), 0.f, 1.f);
    const float foamR = clampf(params.getFloat("foamR", 1.00f), 0.f, 1.f);
    const float foamG = clampf(params.getFloat("foamG", 1.00f), 0.f, 1.f);
    const float foamB = clampf(params.getFloat("foamB", 1.00f), 0.f, 1.f);

    if (onlyLayer >= layers) {
        error = "mesh.water_splash: onlyLayer must be < layers";
        return false;
    }

    const uint32_t seed = params.getSeed();
    out.clear();
    out.setActiveGroup("splash");
    std::vector<float> colors;
    colors.reserve(size_t(layers) * size_t(sides) * size_t(rings + 3) * 4u);

    for (int i = 0; i < layers; ++i) {
        if (onlyLayer >= 0 && i != onlyLayer) continue;
        LayerSpec spec = kDefaultLayers[i];
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
