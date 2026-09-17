#include "procgen/algorithms/FlowerMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace eve::procgen {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Atlas: left strip = stem green, right = petal face (masked alpha).
constexpr float kStemUMin   = 0.02f;
constexpr float kStemUMax   = 0.28f;
constexpr float kPetalUMin  = 0.38f;
constexpr float kPetalUMax  = 0.98f;

struct V3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

V3    add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3    sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3    mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3    cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
V3 norm(V3 a) {
    const float n = std::sqrt(std::max(1e-12f, dot(a, a)));
    return mul(a, 1.f / n);
}

float randomRange(std::mt19937 &rng, float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}

void basisFor(V3 axis, V3 &right, V3 &forward) {
    axis            = norm(axis);
    const V3 helper = std::fabs(axis.y) < 0.92f ? V3{0.f, 1.f, 0.f} : V3{1.f, 0.f, 0.f};
    right           = norm(cross(helper, axis));
    forward         = norm(cross(axis, right));
}

void addStem(MeshBuild &out, float height, float radius, int sides) {
    const V3       a{0.f, 0.f, 0.f};
    const V3       b{0.f, height, 0.f};
    const uint32_t base = uint32_t(out.getVertexCount());
    for (int ring = 0; ring < 2; ++ring) {
        const V3    center = ring ? b : a;
        const float r      = ring ? radius * 0.55f : radius;
        for (int i = 0; i < sides; ++i) {
            const float t     = float(i) / float(sides);
            const float angle = t * 2.f * kPi;
            const V3    radial{std::cos(angle), 0.f, std::sin(angle)};
            const V3    p = add(center, mul(radial, r));
            out.addVertex(p.x, p.y, p.z, radial.x, radial.y, radial.z,
                          kStemUMin + t * (kStemUMax - kStemUMin), float(ring));
        }
    }
    for (int i = 0; i < sides; ++i) {
        const uint32_t n  = uint32_t((i + 1) % sides);
        const uint32_t i0 = base + uint32_t(i);
        const uint32_t i1 = base + n;
        const uint32_t i2 = base + uint32_t(sides) + uint32_t(i);
        const uint32_t i3 = base + uint32_t(sides) + n;
        out.addTriangle(i0, i2, i1);
        out.addTriangle(i1, i2, i3);
    }
}

/** Rounded petal card — pointed tip outward, base toward flower centre. */
void addPetal(MeshBuild &out, V3 center, V3 outward, float length, float width, float roll) {
    V3 right, up;
    basisFor(outward, right, up);
    // Tip points along outward; width along right; slight cup along up.
    const float c = std::cos(roll), s = std::sin(roll);
    const V3    axisX = add(mul(right, c), mul(up, s));
    const V3    axisY = outward;
    const V3    axisZ = norm(cross(axisX, axisY));

    // Six-point lanceolate outline in petal local space (base at -0.5, tip at +0.5).
    constexpr float kOutlineX[6] = {0.f, -0.46f, -0.38f, 0.f, 0.38f, 0.46f};
    constexpr float kOutlineY[6] = {-0.48f, -0.18f, 0.22f, 0.50f, 0.22f, -0.18f};

    const uint32_t base = uint32_t(out.getVertexCount());
    for (int i = 0; i < 6; ++i) {
        const V3 p = add(center, add(mul(axisX, kOutlineX[i] * width),
                                     add(mul(axisY, kOutlineY[i] * length),
                                         mul(axisZ, (0.5f - std::fabs(kOutlineY[i])) * width * 0.12f))));
        const V3 n = axisZ;
        const float u = kPetalUMin + (0.5f + kOutlineX[i] * 0.5f) * (kPetalUMax - kPetalUMin);
        const float v = 0.5f + kOutlineY[i] * 0.5f;
        out.addVertex(p.x, p.y, p.z, n.x, n.y, n.z, u, v);
    }
    for (int i = 1; i < 5; ++i) {
        out.addTriangle(base, base + uint32_t(i), base + uint32_t(i + 1));
        out.addTriangle(base, base + uint32_t(i + 1), base + uint32_t(i));  // double-sided
    }
}

void addCentre(MeshBuild &out, V3 c, float radius, int rings, int sides) {
    const uint32_t base = uint32_t(out.getVertexCount());
    for (int y = 0; y <= rings; ++y) {
        const float v   = float(y) / float(rings);
        const float phi = v * kPi * 0.55f;  // hemisphere facing up
        for (int x = 0; x < sides; ++x) {
            const float u     = float(x) / float(sides);
            const float theta = u * 2.f * kPi;
            const V3 n = {std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
            const V3 p = {c.x + n.x * radius, c.y + n.y * radius, c.z + n.z * radius};
            // Sample near petal centre (warm yellow in tex.flower).
            out.addVertex(p.x, p.y, p.z, n.x, n.y, n.z, 0.68f + u * 0.12f, 0.45f + v * 0.2f);
        }
    }
    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < sides; ++x) {
            const int      nx = (x + 1) % sides;
            const uint32_t a  = base + uint32_t(y * sides + x);
            const uint32_t b  = base + uint32_t(y * sides + nx);
            const uint32_t c0 = base + uint32_t((y + 1) * sides + x);
            const uint32_t d  = base + uint32_t((y + 1) * sides + nx);
            out.addTriangle(a, c0, b);
            out.addTriangle(b, c0, d);
        }
    }
}

}  // namespace

bool generateFlowerMesh(const Params &params, MeshBuild &out, std::string &error) {
    const float height = std::clamp(params.getFloat("height", 0.55f), 0.15f, 2.5f);
    const float petalLength =
        std::clamp(params.getFloat("petalLength", height * 0.38f), 0.05f, 1.2f);
    const float petalWidth =
        std::clamp(params.getFloat("petalWidth", petalLength * 0.55f), 0.03f, 1.0f);
    const int   petals = std::clamp(params.getInt("petals", 6), 3, 12);
    const float stemRadius =
        std::clamp(params.getFloat("stemRadius", height * 0.035f), 0.005f, 0.12f);
    const float openAngle =
        std::clamp(params.getFloat("openAngle", 58.f), 20.f, 85.f) * kPi / 180.f;
    const int sides = std::clamp(params.getInt("sides", 6), 4, 16);

    std::mt19937 rng(params.getSeed());
    out.clear();

    addStem(out, height, stemRadius, sides);

    const V3 head{0.f, height, 0.f};
    addCentre(out, head, petalWidth * 0.28f, 3, std::max(5, sides));

    for (int i = 0; i < petals; ++i) {
        const float yaw = (float(i) + randomRange(rng, -0.08f, 0.08f)) * (2.f * kPi / float(petals));
        const float pitch = openAngle + randomRange(rng, -0.12f, 0.12f);
        const V3    outward = norm({std::sin(pitch) * std::cos(yaw), std::cos(pitch),
                                    std::sin(pitch) * std::sin(yaw)});
        const V3    base = add(head, mul(outward, petalWidth * 0.08f));
        const float len  = petalLength * randomRange(rng, 0.88f, 1.12f);
        const float wid  = petalWidth * randomRange(rng, 0.85f, 1.1f);
        addPetal(out, base, outward, len, wid, randomRange(rng, -0.25f, 0.25f));
    }

    out.setMeta("recipe", "mesh.flower");
    out.setMeta("petals", std::to_string(petals));
    out.setMeta("seed", std::to_string(params.getSeed()));
    if (out.empty()) {
        error = "mesh.flower: generated an empty mesh";
        return false;
    }
    return true;
}

}  // namespace eve::procgen
