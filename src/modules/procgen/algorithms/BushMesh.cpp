#include "procgen/algorithms/BushMesh.h"
#include "procgen/algorithms/FoliageCluster.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace eve::procgen {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// tex.foliage atlas layout:
//   u in [0, 0.22] brown bark for twigs (same look as tree trunks),
//   u in [0.24, 0.50] opaque leafy fill for ellipsoid blobs,
//   u in [0.52, 1] 2×3 = 6 leaf-card panels (each 8–16 blue-noise ovate stamps).
constexpr float kBarkUMin     = 0.00f;
constexpr float kBarkUMax     = 0.22f;
constexpr float kBlobUMin     = 0.24f;
constexpr float kBlobUMax     = 0.50f;
constexpr float kCardAtlasU0  = 0.52f;
constexpr float kCardAtlasU1  = 1.00f;
constexpr int   kLeafCardCols = 2;
constexpr int   kLeafCardRows = 3;
constexpr int   kLeafCardPanels = kLeafCardCols * kLeafCardRows;

struct V3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

struct FoliageLobe {
    V3 center;
    V3 radius;
    float yaw = 0.f;
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
                      float phase = 0.f, float irregularity = 0.55f) {
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
            const float swell = 1.f + equator * irregularity *
                                          (0.14f * std::sin(theta * 3.f + phase) +
                                           0.065f * std::sin(theta * 5.f - phase * 0.7f));
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
            out.addTriangle(a, b, c);
            out.addTriangle(b, d, c);
        }
    }
}

struct Shoot {
    std::vector<V3> centers;
    float           radius, tipRadius;
};

V3 shootPoint(const Shoot &shoot, float t) {
    const float at   = std::clamp(t, 0.f, 1.f) * float(shoot.centers.size() - 1);
    const int   edge = std::min(int(at), int(shoot.centers.size()) - 2);
    return add(shoot.centers[size_t(edge)],
               mul(sub(shoot.centers[size_t(edge + 1)], shoot.centers[size_t(edge)]), at - float(edge)));
}

float shootRadius(const Shoot &shoot, float t) {
    return shoot.radius + (shoot.tipRadius - shoot.radius) * std::pow(t, 0.85f);
}

// Continuous transported-frame sweep, shared rings, seam-safe arc-length UVs.
// Each child starts at a sampled parent centre with a smaller local radius.
Shoot addShoot(MeshBuild &out, V3 start, V3 end, V3 departure, float r0, float r1, int segments, int sides, float curve,
               float phase) {
    Shoot       shoot{{}, r0, r1};
    const float length  = std::sqrt(dot(sub(end, start), sub(end, start)));
    const V3    control = add(start, mul(norm(departure), length * (0.28f + curve * 0.6f)));
    for (int i = 0; i <= segments; ++i) {
        const float t = float(i) / float(segments), q = 1.f - t;
        shoot.centers.push_back(add(add(mul(start, q * q), mul(control, 2.f * q * t)), mul(end, t * t)));
    }
    const int          stride = sides + 1;
    std::vector<V3>    positions(size_t((segments + 1) * stride));
    std::vector<float> arc(size_t(segments + 1), 0.f);
    V3                 right{}, forward{};
    for (int i = 0; i <= segments; ++i) {
        const V3 before      = shoot.centers[size_t(std::max(0, i - 1))];
        const V3 after       = shoot.centers[size_t(std::min(segments, i + 1))];
        const V3 axis        = norm(sub(after, before));
        const V3 transported = sub(right, mul(axis, dot(right, axis)));
        if (dot(transported, transported) < 1e-8f)
            basisFor(axis, right, forward);
        else {
            right   = norm(transported);
            forward = norm(cross(axis, right));
        }
        if (i > 0) {
            const V3 delta = sub(shoot.centers[size_t(i)], shoot.centers[size_t(i - 1)]);
            arc[size_t(i)] = arc[size_t(i - 1)] + std::sqrt(dot(delta, delta));
        }
        const float t = float(i) / float(segments);
        for (int j = 0; j <= sides; ++j) {
            const float angle  = float(j % sides) / float(sides) * 2.f * kPi;
            const V3    radial = add(mul(right, std::cos(angle)), mul(forward, std::sin(angle)));
            const float relief = 1.f + 0.045f * std::sin(angle * 3.f + phase + t * 5.f) +
                                 0.025f * std::cos(angle * 5.f - phase + t * 8.f);
            const float collar = 1.f + 0.16f * std::exp(-t * 16.f);
            positions[size_t(i * stride + j)] =
                add(shoot.centers[size_t(i)], mul(radial, shootRadius(shoot, t) * relief * collar));
        }
    }
    const uint32_t base = uint32_t(out.getVertexCount());
    for (int i = 0; i <= segments; ++i) {
        for (int j = 0; j <= sides; ++j) {
            const int col    = j % sides;
            const V3  around = sub(positions[size_t(i * stride + (col + 1) % sides)],
                                   positions[size_t(i * stride + (col + sides - 1) % sides)]);
            const V3  along  = sub(positions[size_t(std::min(segments, i + 1) * stride + col)],
                                   positions[size_t(std::max(0, i - 1) * stride + col)]);
            const V3  n      = norm(cross(norm(around), norm(along)));
            const V3  p      = positions[size_t(i * stride + j)];
            out.addVertex(p.x, p.y, p.z, n.x, n.y, n.z,
                          kBarkUMin + 0.003f + float(j) / float(sides) * (kBarkUMax - kBarkUMin - 0.006f),
                          arc[size_t(i)] / std::max(arc.back(), 1e-5f));
        }
    }
    for (int i = 0; i < segments; ++i) {
        for (int j = 0; j < sides; ++j) {
            const uint32_t a = base + uint32_t(i * stride + j), b = a + 1;
            const uint32_t c = a + uint32_t(stride), d = c + 1;
            out.addTriangle(a, b, c);
            out.addTriangle(b, d, c);
        }
    }
    // End caps use separate axial normals; the side rings stay smooth.
    for (int cap = 0; cap < 2; ++cap) {
        const int      ring   = cap ? segments : 0;
        const V3       n      = norm(cap ? sub(shoot.centers.back(), shoot.centers[size_t(segments - 1)])
                                         : sub(shoot.centers[0], shoot.centers[1]));
        const V3       center = shoot.centers[size_t(ring)];
        const uint32_t first  = uint32_t(out.getVertexCount());
        out.addVertex(center.x, center.y, center.z, n.x, n.y, n.z, 0.11f, float(cap));
        for (int j = 0; j < sides; ++j) {
            const V3 p = positions[size_t(ring * stride + j)];
            out.addVertex(p.x, p.y, p.z, n.x, n.y, n.z, 0.11f, float(cap));
        }
        for (int j = 0; j < sides; ++j) {
            const uint32_t a = first + 1 + uint32_t(j), b = first + 1 + uint32_t((j + 1) % sides);
            if (cap)
                out.addTriangle(first, a, b);
            else
                out.addTriangle(first, b, a);
        }
    }
    return shoot;
}

// Leaf card: transparent quad sampling one of six atlas panels. Each panel holds
// 8–16 solid ovate leaves placed with blue-noise spacing + random rotation.
void addLeafCard(MeshBuild &out, std::mt19937 &rng, V3 c, V3 direction, float size) {
    V3 right, up;
    basisFor(norm(direction), right, up);
    const float twist = randomRange(rng, 0.f, 2.f * kPi);
    right             = add(mul(right, std::cos(twist)), mul(up, std::sin(twist)));
    up                = norm(cross(norm(direction), right));
    const V3 normal   = norm(cross(right, up));
    // Quad card 25% larger than the base leafSize.
    const float card  = size * 1.25f;
    const float halfW = card * randomRange(rng, 0.55f, 0.72f);
    const float halfH = card * randomRange(rng, 0.55f, 0.72f);
    const V3    r     = mul(right, halfW);
    const V3    h     = mul(up, halfH);
    const V3    center = add(c, mul(normal, size * 0.04f));
    const V3    points[4] = {sub(sub(center, r), h), add(sub(center, h), r), add(add(center, r), h),
                             add(sub(center, r), h)};

    // Randomly assign one of the six pre-baked leaf-card panels.
    const int   panel  = int(randomRange(rng, 0.f, float(kLeafCardPanels))) % kLeafCardPanels;
    const int   col    = panel % kLeafCardCols;
    const int   row    = panel / kLeafCardCols;
    const float inset  = 0.04f;
    const float cellWU = (kCardAtlasU1 - kCardAtlasU0) / float(kLeafCardCols);
    const float cellWV = 1.f / float(kLeafCardRows);
    const float u0     = kCardAtlasU0 + (float(col) + inset) * cellWU;
    const float u1     = kCardAtlasU0 + (float(col) + 1.f - inset) * cellWU;
    const float v0     = (float(row) + inset) * cellWV;
    const float v1     = (float(row) + 1.f - inset) * cellWV;
    const bool  flipU  = random01(rng) > 0.5f;
    const bool  flipV  = random01(rng) > 0.5f;
    const float uv[4][2] = {
        {flipU ? u1 : u0, flipV ? v1 : v0},
        {flipU ? u0 : u1, flipV ? v1 : v0},
        {flipU ? u0 : u1, flipV ? v0 : v1},
        {flipU ? u1 : u0, flipV ? v0 : v1},
    };
    const uint32_t base = uint32_t(out.getVertexCount());
    for (int i = 0; i < 4; ++i) {
        out.addVertex(points[i].x, points[i].y, points[i].z, normal.x, normal.y, normal.z, uv[i][0],
                      uv[i][1]);
    }
    out.addTriangle(base, base + 1, base + 2);
    out.addTriangle(base, base + 2, base + 3);
    out.addTriangle(base + 2, base + 1, base);
    out.addTriangle(base + 3, base + 2, base);
}

}  // namespace

bool generateBushMesh(const Params &params, MeshBuild &out, std::string &error) {
    const std::string style    = params.getString("style", "mound");
    const std::string leafMode = params.getString("leafMode", "mixed");
    if (style != "mound" && style != "sphere") {
        error = "mesh.bush: style must be mound|sphere";
        return false;
    }
    if (leafMode != "clusters" && leafMode != "blobs" && leafMode != "cards" && leafMode != "mixed" &&
        leafMode != "none") {
        error = "mesh.bush: leafMode must be clusters|blobs|cards|mixed|none";
        return false;
    }

    const float height = std::max(0.3f, params.getFloat("height", 1.4f));
    const float width  = std::max(0.4f, params.getFloat("width", 2.2f));
    const float halfW  = width * 0.5f;
    const int   blobs        = std::clamp(params.getInt("blobs", 24), 1, 96);
    const int   rings  = std::clamp(params.getInt("rings", 3), 2, 10);
    const int   sides  = std::clamp(params.getInt("radialSegments", 7), 4, 24);
    const float density = std::clamp(params.getFloat("leafDensity", 0.62f), 0.f, 1.f);
    const float leafSize = std::max(0.02f, params.getFloat("leafSize", height * 0.16f));
    const float lobeScale = std::clamp(params.getFloat("lobeScale", 0.68f), 0.35f, 1.25f);
    const float irregularity = std::clamp(params.getFloat("irregularity", 0.62f), 0.f, 1.f);
    const int   twigs    = std::clamp(params.getInt("twigs", 4), 0, 16);
    const float twigLen  = std::max(0.05f, params.getFloat("twigLength", height * 0.30f));
    const float twigScale = std::clamp(twigLen / (height * 0.30f), 0.35f, 1.65f);
    const bool  sphere   = style == "sphere";

    std::mt19937 rng(params.getSeed());
    out.clear();

    // Basal shoots use their own deterministic RNG. Leaf-only controls do not
    // change woody geometry, and each terminal anchor is reached by a shoot.
    std::mt19937 wood(params.getSeed() ^ 0x51ed270bu);
    struct LeafRun {
        Shoot shoot;
        float start;
        float scale;
    };
    std::vector<LeafRun> leafRuns;
    const float          branchR     = std::clamp(params.getFloat("twigRadius", width * 0.012f), 0.001f, width * 0.08f);
    const float          spread      = std::clamp(params.getFloat("stemSpread", 0.22f), 0.02f, 0.45f);
    const float          branchStart = std::clamp(params.getFloat("branchingStart", 0.14f), 0.08f, 0.55f);
    const float          curve       = std::clamp(params.getFloat("branchCurve", 0.3f), 0.f, 0.6f);
    const float          falloff     = std::clamp(params.getFloat("branchRadiusFalloff", 0.5f), 0.3f, 0.7f);
    const int            levels      = std::clamp(params.getInt("branchLevels", 3), 1, 3);
    const int            segments    = std::clamp(params.getInt("branchSegments", 12), 4, 24);
    const int            branchCount = std::clamp(params.getInt("branchCount", 4), 2, 6);
    const float          foliageStart = std::clamp(params.getFloat("foliageStart", 0.10f), 0.05f, 0.5f);
    for (int i = 0; i < twigs; ++i) {
        const float angle      = float(i) * 2.399963f + randomRange(wood, -0.35f, 0.35f);
        const float rootRadius = halfW * spread * std::sqrt(random01(wood));
        const V3    root{std::cos(angle) * rootRadius, 0.f, std::sin(angle) * rootRadius};
        const float reach = halfW * (i == 0 ? 0.06f : randomRange(wood, 0.20f, 0.60f)) * twigScale;
        const V3    tip{std::cos(angle) * reach, height * randomRange(wood, 0.48f, 0.84f) * twigScale,
                     std::sin(angle) * reach};
        const float r0   = branchR * randomRange(wood, 0.8f, 1.3f);
        const Shoot main = addShoot(out, root, tip, {std::cos(angle) * 0.3f, 1.f, std::sin(angle) * 0.3f}, r0,
                                    r0 * 0.12f, segments, sides, curve, angle);
        leafRuns.push_back({main, 0.36f, 1.f});
        const int branches = levels < 2 ? 0 : branchCount - int(random01(wood) * 1.999f);
        for (int j = 0; j < branches; ++j) {
            const float at =
                std::clamp(branchStart + float(j) * (0.65f - branchStart) / float(std::max(1, branches - 1)) +
                               randomRange(wood, -0.035f, 0.035f),
                           0.08f, 0.75f);
            const V3    base      = shootPoint(main, at);
            const V3    tangent   = norm(sub(shootPoint(main, at + 0.03f), shootPoint(main, at)));
            const float yaw       = angle + (j & 1 ? -1.f : 1.f) * randomRange(wood, 0.5f, 1.15f);
            const float reachSide = halfW * randomRange(wood, 0.23f, 0.42f) * (1.f - at * 0.35f) * twigScale;
            const V3    end{base.x + std::cos(yaw) * reachSide,
                         base.y + height * randomRange(wood, 0.12f, 0.28f) * (1.f - at * 0.45f) * twigScale,
                         base.z + std::sin(yaw) * reachSide};
            const float childR = shootRadius(main, at) * falloff;
            const Shoot child = addShoot(out, base, end, tangent, childR, childR * 0.13f, std::max(4, segments * 2 / 3),
                                         std::max(4, sides - 2), curve, yaw);
            leafRuns.push_back({child, 0.20f, 0.85f});
            if (levels < 3) continue;
            for (int k = 0; k < 3; ++k) {
                const float t           = 0.32f + float(k) * 0.22f + randomRange(wood, -0.035f, 0.035f);
                const V3    node        = shootPoint(child, t);
                const float azimuth     = yaw + (k ? -0.8f : 0.8f);
                const float shootLength = halfW * randomRange(wood, 0.09f, 0.22f);
                const V3    bud{node.x + std::cos(azimuth) * shootLength,
                             node.y + height * randomRange(wood, 0.06f, 0.18f),
                             node.z + std::sin(azimuth) * shootLength};
                const V3    dir   = sub(shootPoint(child, t + 0.04f), node);
                const float fineR = shootRadius(child, t) * falloff;
                Shoot       fine  = addShoot(out, node, bud, dir, fineR, fineR * 0.1f, 4, 4, curve, azimuth);
                leafRuns.push_back({std::move(fine), 0.15f, 0.7f});
            }
        }
    }
    std::vector<FoliageLobe> foliageLobes;
    foliageLobes.reserve(size_t(blobs));
    if (leafMode != "none") {
        if (!leafRuns.empty()) {
            // Candidate sites follow the leafy portions of real shoots, including
            // mid-sections. Small overlapping patches form leaf bands rather than
            // one large ball at each endpoint. The budget never changes the wood.
            std::vector<FoliageLobe> candidates;
            for (const auto &run : leafRuns) {
                for (int site = 0; site < 3; ++site) {
                    const float t =
                        run.start + (0.95f - run.start) * (float(site) + randomRange(rng, 0.15f, 0.85f)) / 3.f;
                    const V3 center = shootPoint(run.shoot, t);
                    if (center.y < height * foliageStart) continue;
                    const V3    tangent = sub(shootPoint(run.shoot, std::min(1.f, t + 0.03f)), center);
                    const float r       = halfW * 0.19f * lobeScale * run.scale * randomRange(rng, 0.85f, 1.15f);
                    const float ry      = std::min(r * 0.85f, height * 0.15f * lobeScale);
                    candidates.push_back(
                        {center, {r * 1.25f, ry, r * 0.8f}, std::atan2(-tangent.z, tangent.x) * 180.f / kPi});
                }
            }
            std::shuffle(candidates.begin(), candidates.end(), rng);
            for (const auto &candidate : candidates) {
                bool crowded = false;
                for (const auto &placed : foliageLobes) {
                    const V3    d       = sub(candidate.center, placed.center);
                    const float spacing = std::min(candidate.radius.x, placed.radius.x) * 0.55f;
                    if (dot(d, d) < spacing * spacing) {
                        crowded = true;
                        break;
                    }
                }
                if (crowded) continue;
                foliageLobes.push_back(candidate);
                if (int(foliageLobes.size()) >= blobs) break;
            }
            // An explicitly high foliageStart may exclude every shoot. Keep the
            // woody plant observable and return no floating leaf-only fallback.
        } else {
            for (int i = 0; i < blobs; ++i) {
                const float radial       = halfW * 0.65f * std::sqrt(random01(rng));
                const float theta        = randomRange(rng, 0.f, 2.f * kPi);
                const float heightFactor = 1.f - (radial / halfW) * (radial / halfW);
                const float cy           = height * (sphere ? 0.35f + 0.30f * random01(rng)
                                                            : 0.16f + heightFactor * randomRange(rng, 0.25f, 0.5f));
                const float rx           = halfW * 0.19f * lobeScale * randomRange(rng, 0.85f, 1.15f);
                foliageLobes.push_back(
                    {{std::cos(theta) * radial, cy, std::sin(theta) * radial}, {rx, height * 0.15f * lobeScale, rx}});
            }
        }
    }
    if (leafMode == "blobs" || leafMode == "mixed") {
        for (const auto &lobe : foliageLobes)
            addEllipsoidBlob(out, lobe.center, lobe.radius, rings, sides, kBlobUMin, kBlobUMax,
                             randomRange(rng, 0.f, 2.f * kPi), irregularity);
    }

    // Reuse the tree's geometric leaves and rounded normals. Build a unit
    // sphere, then transform both geometry and normals to each authored lobe.
    if (leafMode == "clusters" && density > 0.f) {
        const float canopyBlend = std::clamp(params.getFloat("canopyNormalBlend", 0.45f), 0.f, 0.8f);
        for (size_t i = 0; i < foliageLobes.size(); ++i) {
            const auto        &lobe = foliageLobes[i];
            FoliageClusterDesc desc;
            desc.seed              = params.getSeed() ^ (uint32_t(i + 1) * 2654435761u);
            desc.radius            = 1.f;
            const float scale      = std::max({lobe.radius.x, lobe.radius.y, lobe.radius.z});
            desc.leafSize          = leafSize / scale;
            desc.leafSpacing       = 0.8f * (1.45f - 0.75f * density);
            desc.planes            = std::clamp(params.getInt("clusterPlanes", 10), 1, 24);
            desc.maxLeavesPerPlane = std::clamp(params.getInt("clusterLeaves", 48), 1, 256);
            MeshBuild cluster;
            addFoliageCluster(cluster, desc);
            const int first = out.getVertexCount();
            if (!out.appendTransformed(&cluster, lobe.center.x, lobe.center.y, lobe.center.z, lobe.yaw, lobe.radius.x,
                                       lobe.radius.y, lobe.radius.z)) {
                error = "mesh.bush: could not append leaf cluster";
                return false;
            }
            // Blend local ellipsoid normals with the whole crown envelope so
            // overlapping patches read as one shrub instead of separate balls.
            auto &normals = out.normals();
            for (int v = first; v < out.getVertexCount(); ++v) {
                const V3 local{out.getNormalX(v), out.getNormalY(v), out.getNormalZ(v)};
                const V3 envelope          = norm({out.getPositionX(v) / (halfW * halfW),
                                                   (out.getPositionY(v) - height * 0.42f) / (height * height * 0.25f),
                                                   out.getPositionZ(v) / (halfW * halfW)});
                const V3 normal            = norm(add(mul(local, 1.f - canopyBlend), mul(envelope, canopyBlend)));
                normals[size_t(v) * 3]     = normal.x;
                normals[size_t(v) * 3 + 1] = normal.y;
                normals[size_t(v) * 3 + 2] = normal.z;
            }
        }
    }

    // Leaf cards across the canopy (+50% count vs prior 14× density packing).
    if ((leafMode == "cards" || leafMode == "mixed") && !foliageLobes.empty() && density > 0.f) {
        const int cards = std::max(1, int(std::round(float(blobs) * 21.f * density)));
        for (int i = 0; i < cards; ++i) {
            const FoliageLobe &lobe = foliageLobes[size_t(i) % foliageLobes.size()];
            const float theta = randomRange(rng, 0.f, 2.f * kPi);
            const float ny = randomRange(rng, -0.45f, 1.f);
            const float radial = std::sqrt(std::max(0.f, 1.f - ny * ny));
            const V3 face = norm({std::cos(theta) * radial, ny, std::sin(theta) * radial});
            const V3 c{lobe.center.x + face.x * lobe.radius.x * 1.05f,
                       lobe.center.y + face.y * lobe.radius.y * 1.05f,
                       lobe.center.z + face.z * lobe.radius.z * 1.05f};
            addLeafCard(out, rng, c, face, leafSize * randomRange(rng, 0.85f, 1.35f));
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
