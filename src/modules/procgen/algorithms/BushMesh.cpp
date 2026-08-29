#include "procgen/algorithms/BushMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace eve::procgen {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Shared with the tree atlas: the left half is bark/stems, the right half foliage.
// A bush is almost entirely foliage, so lobes/cards sample the right half while
// the few emergent twigs sample the left half.
constexpr float kFoliageUMin = 0.55f;
constexpr float kFoliageUMax = 1.0f;
constexpr float kBarkUMin    = 0.0f;
constexpr float kBarkUMax    = 0.45f;

struct V3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

V3    add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3    sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3    mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3    cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3    norm(V3 a) {
    const float n = std::sqrt(std::max(1e-12f, dot(a, a)));
    return mul(a, 1.f / n);
}

float randomRange(std::mt19937 &rng, float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }

float random01(std::mt19937 &rng) { return randomRange(rng, 0.f, 1.f); }

void basisFor(V3 axis, V3 &right, V3 &forward) {
    axis            = norm(axis);
    const V3 helper = std::fabs(axis.y) < 0.92f ? V3{0.f, 1.f, 0.f} : V3{1.f, 0.f, 0.f};
    right           = norm(cross(helper, axis));
    forward         = norm(cross(axis, right));
}

// Closed ellipsoid. The per-vertex normal is the analytic ellipsoid normal so the
// bush shades smoothly under directional light. UVs fall inside [uMin, uMax].
void addEllipsoidBlob(MeshBuild &out, V3 c, V3 r, int rings, int sides, float uMin, float uMax,
                      float phase = 0.f) {
    const uint32_t base = uint32_t(out.getVertexCount());
    for (int y = 0; y <= rings; ++y) {
        const float v   = float(y) / float(rings);
        const float phi = v * kPi;
        for (int x = 0; x < sides; ++x) {
            const float u     = float(x) / float(sides);
            const float theta = u * 2.f * kPi;
            const V3 n = {std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
            // Two low-frequency harmonics break the unmistakable primitive-sphere
            // silhouette without producing noisy, faceted normals. Keep both poles
            // fixed so neighbouring lobes still close cleanly.
            const float equator = std::sin(phi);
            const float swell = 1.f + equator * (0.075f * std::sin(theta * 3.f + phase) +
                                                 0.035f * std::sin(theta * 5.f - phase * 0.7f));
            const V3 p = {c.x + n.x * r.x * swell, c.y + n.y * r.y, c.z + n.z * r.z * swell};
            // Ellipsoid normal = N/R divided by its length (finite since radii > 0).
            const V3 normal = norm({n.x / r.x, n.y / r.y, n.z / r.z});
            out.addVertex(p.x, p.y, p.z, normal.x, normal.y, normal.z, uMin + u * (uMax - uMin), v);
        }
    }
    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < sides; ++x) {
            const int      nx = (x + 1) % sides;
            const uint32_t a  = base + uint32_t(y * sides + x);
            const uint32_t b  = base + uint32_t(y * sides + nx);
            const uint32_t c  = base + uint32_t((y + 1) * sides + x);
            const uint32_t d  = base + uint32_t((y + 1) * sides + nx);
            out.addTriangle(a, c, b);
            out.addTriangle(b, c, d);
        }
    }
}

// Open tapered cylinder (a woody twig). Vertices use the bark UV half.
void addTwig(MeshBuild &out, V3 a, V3 b, float r0, float r1, int sides, float uMin, float uMax) {
    const V3 axis = norm(sub(b, a));
    V3       right, forward;
    basisFor(axis, right, forward);
    const uint32_t base = uint32_t(out.getVertexCount());
    for (int ring = 0; ring < 2; ++ring) {
        const V3    center = ring ? b : a;
        const float radius = ring ? r1 : r0;
        for (int i = 0; i < sides; ++i) {
            const float t     = float(i) / float(sides);
            const float angle = t * 2.f * kPi;
            const V3    radial = add(mul(right, std::cos(angle)), mul(forward, std::sin(angle)));
            const V3    p      = add(center, mul(radial, radius));
            out.addVertex(p.x, p.y, p.z, radial.x, radial.y, radial.z, uMin + t * (uMax - uMin), float(ring));
        }
    }
    for (int i = 0; i < sides; ++i) {
        const uint32_t n  = uint32_t((i + 1) % sides);
        const uint32_t i0 = base + uint32_t(i), i1 = base + n;
        const uint32_t i2 = base + uint32_t(sides) + uint32_t(i);
        const uint32_t i3 = base + uint32_t(sides) + n;
        out.addTriangle(i0, i2, i1);
        out.addTriangle(i1, i2, i3);
    }
}

// A rounded, slightly cupped leaf. The eight-point outline avoids both the
// rectangular-card look and the long triangular spikes of a diamond leaf.
void addLeafCard(MeshBuild &out, std::mt19937 &rng, V3 c, V3 direction, float size, float uMin, float uMax) {
    V3 right, up;
    basisFor(norm(direction), right, up);
    const float twist = randomRange(rng, 0.f, 2.f * kPi);
    right             = add(mul(right, std::cos(twist)), mul(up, std::sin(twist)));
    up                   = norm(cross(norm(direction), right));
    const V3 normal = norm(cross(right, up));
    const float halfWidth = size * randomRange(rng, 0.34f, 0.44f);
    const float halfLength = size * randomRange(rng, 0.58f, 0.72f);
    const uint32_t base = uint32_t(out.getVertexCount());
    const V3 center = add(c, mul(normal, size * 0.06f));
    out.addVertex(center.x, center.y, center.z, normal.x, normal.y, normal.z,
                  (uMin + uMax) * 0.5f, 0.5f);
    constexpr int outline = 8;
    for (int i = 0; i < outline; ++i) {
        const float angle = -0.5f * kPi + float(i) * 2.f * kPi / float(outline);
        const float x = std::cos(angle) * halfWidth;
        const float y = std::sin(angle) * halfLength;
        const V3 point = add(c, add(mul(right, x), mul(up, y)));
        out.addVertex(point.x, point.y, point.z, normal.x, normal.y, normal.z,
                      uMin + (0.5f + x / (2.f * halfWidth)) * (uMax - uMin), 0.5f + y / (2.f * halfLength));
    }
    for (int i = 0; i < outline; ++i) {
        const uint32_t a = base + 1u + uint32_t(i);
        const uint32_t b = base + 1u + uint32_t((i + 1) % outline);
        out.addTriangle(base, a, b);
        out.addTriangle(base, b, a);
    }
}

}  // namespace

bool generateBushMesh(const Params &params, MeshBuild &out, std::string &error) {
    const std::string style    = params.getString("style", "mound");
    const std::string leafMode = params.getString("leafMode", "mixed");
    if (style != "mound" && style != "sphere") {
        error = "mesh.bush: style must be mound|sphere";
        return false;
    }
    if (leafMode != "blobs" && leafMode != "cards" && leafMode != "mixed" && leafMode != "none") {
        error = "mesh.bush: leafMode must be blobs|cards|mixed|none";
        return false;
    }

    const float height = std::max(0.3f, params.getFloat("height", 1.4f));
    const float width  = std::max(0.4f, params.getFloat("width", 2.2f));
    const float halfW  = width * 0.5f;
    const int   blobs  = std::clamp(params.getInt("blobs", 9), 1, 40);
    const int   rings  = std::clamp(params.getInt("rings", 3), 2, 10);
    const int   sides  = std::clamp(params.getInt("radialSegments", 7), 4, 24);
    const float density = std::clamp(params.getFloat("leafDensity", 0.62f), 0.f, 1.f);
    const float leafSize = std::max(0.02f, params.getFloat("leafSize", height * 0.16f));
    const int   twigs    = std::clamp(params.getInt("twigs", 4), 0, 16);
    const float twigLen  = std::max(0.05f, params.getFloat("twigLength", height * 0.30f));
    const float twigScale = std::clamp(twigLen / (height * 0.30f), 0.35f, 1.65f);
    const bool  sphere   = style == "sphere";

    std::mt19937 rng(params.getSeed());
    out.clear();

    // Build the woody skeleton first. Each stem bends outward and forks once;
    // foliage is then placed around these endpoints instead of being scattered
    // independently. The clear lower third keeps the branch structure readable.
    std::vector<V3> crownAnchors;
    const float branchR = width * 0.022f;
    for (int i = 0; i < twigs; ++i) {
        const float angle = float(i) * 2.399963f + randomRange(rng, -0.12f, 0.12f);
        const V3 root{std::cos(angle) * halfW * 0.05f, 0.02f, std::sin(angle) * halfW * 0.05f};
        const V3 joint{std::cos(angle) * halfW * randomRange(rng, 0.22f, 0.34f) * twigScale,
                       height * randomRange(rng, 0.30f, 0.43f) * twigScale,
                       std::sin(angle) * halfW * randomRange(rng, 0.22f, 0.34f) * twigScale};
        const float reach = halfW * randomRange(rng, 0.58f, 0.76f) * twigScale;
        const V3 tip{std::cos(angle) * reach, height * randomRange(rng, 0.58f, 0.78f) * twigScale,
                     std::sin(angle) * reach};
        const float forkAngle = angle + (i & 1 ? 0.38f : -0.38f);
        const V3 forkTip{joint.x + std::cos(forkAngle) * halfW * randomRange(rng, 0.28f, 0.40f) * twigScale,
                         joint.y + height * randomRange(rng, 0.22f, 0.33f) * twigScale,
                         joint.z + std::sin(forkAngle) * halfW * randomRange(rng, 0.28f, 0.40f) * twigScale};
        addTwig(out, root, joint, branchR * 1.35f, branchR, std::max(5, sides - 2), kBarkUMin, kBarkUMax);
        addTwig(out, joint, tip, branchR, branchR * 0.38f, std::max(4, sides - 3), kBarkUMin, kBarkUMax);
        addTwig(out, joint, forkTip, branchR * 0.78f, branchR * 0.28f, std::max(4, sides - 3), kBarkUMin,
                kBarkUMax);
        crownAnchors.push_back(tip);
        crownAnchors.push_back(forkTip);
    }

    // Cluster squashed lobes under a dome silhouette so the bush reads as one
    // rounded mound rather than a set of disconnected balls.
    for (int i = 0; i < blobs; ++i) {
        const float radial = halfW * 0.58f * std::sqrt(random01(rng));
        const float theta  = randomRange(rng, 0.f, 2.f * kPi);
        const float heightFactor = 1.f - (radial / halfW) * (radial / halfW);
        const float cy = height * (sphere ? 0.5f + 0.20f * random01(rng)
                                          : 0.18f + heightFactor * (0.34f + 0.34f * random01(rng)));
        const float rx = halfW * 0.29f * randomRange(rng, 0.82f, 1.16f);
        const float ry = height * (sphere ? 0.21f : 0.22f) * randomRange(rng, 0.65f, 1.02f);
        const float rz = rx * randomRange(rng, 0.80f, 1.20f);
        V3 center{std::cos(theta) * radial, cy, std::sin(theta) * radial};
        if (!crownAnchors.empty() && i < int(crownAnchors.size())) {
            center = crownAnchors[size_t(i)];
            center.x += randomRange(rng, -0.10f, 0.10f) * halfW;
            center.y += randomRange(rng, -0.04f, 0.08f) * height;
            center.z += randomRange(rng, -0.10f, 0.10f) * halfW;
        }
        addEllipsoidBlob(out, center, {rx, ry, rz}, rings, sides, kFoliageUMin, kFoliageUMax,
                         randomRange(rng, 0.f, 2.f * kPi));
    }
    // Always cap the top so the dome has no gap at its peak.
    const float topRx = halfW * 0.18f;
    addEllipsoidBlob(out, {0.f, height * (sphere ? 0.62f : 0.72f), 0.f}, {topRx, height * 0.20f, topRx},
                     rings, sides, kFoliageUMin, kFoliageUMax, randomRange(rng, 0.f, 2.f * kPi));

    // Optional loose leaf cards across the canopy for a fuller look.
    if (leafMode == "cards" || leafMode == "mixed") {
        const int cards = std::max(1, int(std::round(float(blobs) * 8.f * density)));
        for (int i = 0; i < cards; ++i) {
            const float radial = halfW * 0.86f * std::sqrt(random01(rng));
            const float theta  = randomRange(rng, 0.f, 2.f * kPi);
            const float heightFactor = 1.f - (radial / halfW) * (radial / halfW);
            const V3 c{std::cos(theta) * radial,
                       height * (0.20f + heightFactor * randomRange(rng, 0.38f, 0.72f)),
                       std::sin(theta) * radial};
            const V3 face = norm({randomRange(rng, -1.f, 1.f), randomRange(rng, 0.2f, 0.9f),
                                  randomRange(rng, -1.f, 1.f)});
            addLeafCard(out, rng, c, face, leafSize * randomRange(rng, 0.70f, 1.20f), kFoliageUMin, kFoliageUMax);
        }
    }

    out.setMeta("recipe", "mesh.bush");
    out.setMeta("style", style);
    out.setMeta("leafMode", leafMode);
    out.setMeta("seed", std::to_string(params.getSeed()));
    if (out.empty()) {
        error = "mesh.bush: generated an empty mesh";
        return false;
    }
    return true;
}

}  // namespace eve::procgen
