#include "procgen/algorithms/TreeMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace eve::procgen {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct V3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

V3    add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3    sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3    mul(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float distanceSquared(V3 a, V3 b) { return dot(sub(a, b), sub(a, b)); }
V3    cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3    norm(V3 a) {
    const float n = std::sqrt(std::max(1e-12f, dot(a, a)));
    return mul(a, 1.f / n);
}

float randomRange(std::mt19937& rng, float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }

float hash01(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return float(value & 0x00ffffffu) / float(0x01000000u);
}

void basisFor(V3 axis, V3& right, V3& forward) {
    axis            = norm(axis);
    const V3 helper = std::fabs(axis.y) < 0.92f ? V3{0.f, 1.f, 0.f} : V3{1.f, 0.f, 0.f};
    right           = norm(cross(helper, axis));
    forward         = norm(cross(axis, right));
}

void addTaperedCylinder(MeshBuild& out, V3 a, V3 b, float r0, float r1, int sides) {
    const V3 axis = norm(sub(b, a));
    V3       right, forward;
    basisFor(axis, right, forward);
    const uint32_t base = uint32_t(out.getVertexCount());
    for (int ring = 0; ring < 2; ++ring) {
        const V3    center = ring ? b : a;
        const float radius = ring ? r1 : r0;
        for (int i = 0; i < sides; ++i) {
            const float t      = float(i) / float(sides);
            const float angle  = t * 2.f * kPi;
            const V3    radial = add(mul(right, std::cos(angle)), mul(forward, std::sin(angle)));
            const V3    p      = add(center, mul(radial, radius));
            // Left side of the tree atlas is bark; foliage uses the right side.
            out.addVertex(p.x, p.y, p.z, radial.x, radial.y, radial.z, t * 0.45f, float(ring));
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

void addLeafCard(MeshBuild& out, V3 c, V3 direction, float size, float twist) {
    V3 right, up;
    basisFor(norm(direction), right, up);
    right            = add(mul(right, std::cos(twist)), mul(up, std::sin(twist)));
    up               = norm(cross(norm(direction), right));
    const V3       r = mul(right, size * 0.5f), u = mul(up, size);
    const V3       normal    = norm(cross(right, up));
    const uint32_t base      = uint32_t(out.getVertexCount());
    const V3       points[4] = {sub(sub(c, r), u), add(sub(c, u), r), add(add(c, r), u), add(sub(c, r), u)};
    const float    uv[4][2]  = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; ++i)
        out.addVertex(points[i].x, points[i].y, points[i].z, normal.x, normal.y, normal.z, 0.55f + uv[i][0] * 0.45f,
                      uv[i][1]);
    out.addTriangle(base, base + 1, base + 2);
    out.addTriangle(base, base + 2, base + 3);
    out.addTriangle(base + 2, base + 1, base);
    out.addTriangle(base + 3, base + 2, base);
}

void addCanopyBlob(MeshBuild& out, V3 center, V3 radius, int rings, int sides) {
    const uint32_t base = uint32_t(out.getVertexCount());
    for (int y = 0; y <= rings; ++y) {
        const float v   = float(y) / float(rings);
        const float phi = v * kPi;
        for (int x = 0; x < sides; ++x) {
            const float u     = float(x) / float(sides);
            const float theta = u * 2.f * kPi;
            const V3    n     = {std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
            const V3    p     = {center.x + n.x * radius.x, center.y + n.y * radius.y, center.z + n.z * radius.z};
            out.addVertex(p.x, p.y, p.z, n.x, n.y, n.z, 0.55f + u * 0.45f, v);
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

struct Tip {
    V3    p;
    V3    dir;
    float scale;
};

struct StemPath {
    std::vector<V3> points;
    V3              endDirection;
};

StemPath makeCurvedStem(V3 start, V3 direction, V3 bendAxis, float length, float curve, float curveBack,
                        float verticalAcceleration, int segments) {
    StemPath path;
    path.points.reserve(size_t(segments + 1));
    direction = norm(direction);
    bendAxis  = norm(sub(bendAxis, mul(direction, dot(bendAxis, direction))));
    const V3 up{0.f, 1.f, 0.f};
    for (int i = 0; i <= segments; ++i) {
        const float t = float(i) / float(segments);
        // Both offsets have zero slope at the branch base, avoiding an immediate kink.
        const float primary = curve * t * t;
        const float back    = curveBack * std::sin(2.f * kPi * t) * t * (1.f - t);
        const V3 p = add(start, add(mul(direction, length * t), add(mul(bendAxis, length * (primary + back)),
                                                                    mul(up, length * verticalAcceleration * t * t))));
        path.points.push_back(p);
    }
    path.endDirection = norm(sub(path.points.back(), path.points[path.points.size() - 2]));
    return path;
}

void addStem(MeshBuild& out, const StemPath& path, float r0, float r1, int sides) {
    const int segmentCount = int(path.points.size()) - 1;
    for (int i = 0; i < segmentCount; ++i) {
        const float t0 = float(i) / float(segmentCount);
        const float t1 = float(i + 1) / float(segmentCount);
        const float a  = std::pow(t0, 1.25f);
        const float b  = std::pow(t1, 1.25f);
        addTaperedCylinder(out, path.points[size_t(i)], path.points[size_t(i + 1)], r0 + (r1 - r0) * a,
                           r0 + (r1 - r0) * b, sides);
    }
}

void samplePath(const StemPath& path, float t, V3& point, V3& direction) {
    const int   segmentCount = int(path.points.size()) - 1;
    const float x            = std::clamp(t, 0.f, 1.f) * float(segmentCount);
    const int   i            = std::min(int(x), segmentCount - 1);
    const float local        = x - float(i);
    point     = add(path.points[size_t(i)], mul(sub(path.points[size_t(i + 1)], path.points[size_t(i)]), local));
    direction = norm(sub(path.points[size_t(i + 1)], path.points[size_t(i)]));
}

struct ColonyNode {
    V3    p;
    V3    dir;
    V3    heading{0.f, 1.f, 0.f};
    int   parent          = -1;
    int   descendants     = 1;
    int   children        = 0;
    int   depth           = 0;
    float remainingLength = 0.f;
    float vigor           = 1.f;
    float budHeight       = 0.f;
};

struct ColonizationSettings {
    float height;
    float foliageStart;
    float crownRadius;
    float trunkRadius;
    float tropism;
    float droop;
    float inertia;
    int   attractorCount;
    int   iterations;
    float influenceRadius;
    float killRadius;
    float step;
    float maxTurnAngle;
    float maxCumulativeAngle;
    float lengthFalloff;
    float radiusFalloff;
    float lowerLeafCoverage;
    float upperLeafCoverage;
    int   maxChildren;
    int   sides;
};

void growSpaceColonizedTree(MeshBuild& out, const StemPath& trunk, std::mt19937& rng,
                            const ColonizationSettings& settings, std::vector<Tip>& foliageAnchors) {
    V3 crownCenter, ignoredDirection;
    samplePath(trunk, settings.foliageStart + (1.f - settings.foliageStart) * 0.53f, crownCenter, ignoredDirection);
    const float crownHalfHeight = settings.height * (1.f - settings.foliageStart) * 0.52f;

    std::vector<V3> attractors;
    attractors.reserve(size_t(settings.attractorCount));
    while (int(attractors.size()) < settings.attractorCount) {
        const V3 q{randomRange(rng, -1.f, 1.f), randomRange(rng, -1.f, 1.f), randomRange(rng, -1.f, 1.f)};
        if (dot(q, q) > 1.f) continue;
        // A modest upward bias avoids a perfectly symmetric, balloon-like crown.
        attractors.push_back(add(crownCenter, {q.x * settings.crownRadius,
                                               q.y * crownHalfHeight + 0.12f * crownHalfHeight * (1.f - q.y * q.y),
                                               q.z * settings.crownRadius}));
    }

    std::vector<ColonyNode> nodes;
    // Multiple trunk buds provide the primary scaffold. Colonization then shapes and
    // splits those limbs; starting from a single bud produces a shrub-like fan.
    const int seedCount = 9;
    for (int i = 0; i < seedCount; ++i) {
        V3          p, dir;
        const float t =
            settings.foliageStart + (1.f - settings.foliageStart) * (0.08f + 0.78f * float(i) / float(seedCount - 1));
        samplePath(trunk, t, p, dir);
        const float crownHeight = float(i) / float(seedCount - 1);
        const float lengthScale = 1.f - settings.lengthFalloff * crownHeight;
        const float vigor       = 1.f - settings.radiusFalloff * crownHeight;
        nodes.push_back({p, dir, dir, -1, 1, 0, 0, settings.crownRadius * 1.35f * lengthScale, vigor, crownHeight});
    }

    std::vector<uint8_t> alive(size_t(settings.attractorCount), 1);
    const float          influenceSquared  = settings.influenceRadius * settings.influenceRadius;
    const float          killSquared       = settings.killRadius * settings.killRadius;
    const float          separationSquared = settings.step * settings.step * 0.22f;
    const float          maxTurn           = settings.maxTurnAngle * kPi / 180.f;
    const float          maxTurnCos        = std::cos(maxTurn);
    const float          maxTurnSin        = std::sin(maxTurn);
    const float          cumulativeTurn    = settings.maxCumulativeAngle * kPi / 180.f;
    const float          cumulativeTurnCos = std::cos(cumulativeTurn);
    const float          cumulativeTurnSin = std::sin(cumulativeTurn);
    for (int iteration = 0; iteration < settings.iterations; ++iteration) {
        const size_t     nodeCount = nodes.size();
        std::vector<V3>  directionSums(nodeCount);
        std::vector<int> directionCounts(nodeCount, 0);
        int              liveAttractors = 0;
        for (size_t ai = 0; ai < attractors.size(); ++ai) {
            if (!alive[ai]) continue;
            ++liveAttractors;
            size_t nearest = 0;
            float  best    = influenceSquared;
            bool   found   = false;
            for (size_t ni = 0; ni < nodeCount; ++ni) {
                const V3    toward = sub(attractors[ai], nodes[ni].p);
                const float d      = dot(toward, toward);
                if (d < killSquared) {
                    alive[ai] = 0;
                    found     = false;
                    break;
                }
                // Once a shoot has left the trunk, attraction from behind may not
                // reverse it. This is the main distinction between a branch and a vine.
                if (nodes[ni].depth > 0) {
                    const V3 attractionDirection = norm(toward);
                    if (dot(attractionDirection, nodes[ni].dir) < -0.08f ||
                        dot(attractionDirection, nodes[ni].heading) < 0.04f)
                        continue;
                }
                if (d < best) {
                    best    = d;
                    nearest = ni;
                    found   = true;
                }
            }
            if (found) {
                directionSums[nearest] = add(directionSums[nearest], norm(sub(attractors[ai], nodes[nearest].p)));
                ++directionCounts[nearest];
            }
        }
        if (liveAttractors == 0) break;

        std::vector<ColonyNode> additions;
        for (size_t ni = 0; ni < nodeCount; ++ni) {
            if (directionCounts[ni] == 0 || nodes[ni].children >= settings.maxChildren ||
                nodes[ni].remainingLength < settings.step)
                continue;
            V3 direction = norm(
                add(mul(directionSums[ni], 1.f / float(directionCounts[ni])), mul(nodes[ni].dir, settings.inertia)));
            const float vertical = settings.tropism - settings.droop * std::max(0.f, 1.f - direction.y);
            direction            = norm(add(direction, {0.f, vertical, 0.f}));
            // Clamp curvature per growth step. Linear blending alone can still turn
            // sharply when the remaining attractors move to the other side of a tip.
            const float alignment = std::clamp(dot(nodes[ni].dir, direction), -1.f, 1.f);
            if (nodes[ni].depth > 0 && alignment < maxTurnCos) {
                V3 tangent = sub(direction, mul(nodes[ni].dir, alignment));
                if (dot(tangent, tangent) > 1e-8f)
                    direction = norm(add(mul(nodes[ni].dir, maxTurnCos), mul(norm(tangent), maxTurnSin)));
                else
                    direction = nodes[ni].dir;
            }
            if (nodes[ni].depth > 0) {
                const float headingAlignment = std::clamp(dot(nodes[ni].heading, direction), -1.f, 1.f);
                if (headingAlignment < cumulativeTurnCos) {
                    V3 tangent = sub(direction, mul(nodes[ni].heading, headingAlignment));
                    if (dot(tangent, tangent) > 1e-8f)
                        direction =
                            norm(add(mul(nodes[ni].heading, cumulativeTurnCos), mul(norm(tangent), cumulativeTurnSin)));
                    else
                        direction = nodes[ni].heading;
                }
            } else if (direction.y < -0.05f) {
                // Primary limbs may spread horizontally, but do not launch downward.
                direction = norm({direction.x, -0.05f, direction.z});
            }
            const V3 candidate = add(nodes[ni].p, mul(direction, settings.step));
            bool     separated = true;
            for (size_t oi = 0; oi < nodes.size() && separated; ++oi)
                separated = distanceSquared(candidate, nodes[oi].p) >= separationSquared;
            for (size_t oi = 0; oi < additions.size() && separated; ++oi)
                separated = distanceSquared(candidate, additions[oi].p) >= separationSquared;
            if (separated) {
                const V3 heading = nodes[ni].depth == 0 ? direction : nodes[ni].heading;
                additions.push_back({candidate, direction, heading, int(ni), 1, 0, nodes[ni].depth + 1,
                                     nodes[ni].remainingLength - settings.step, nodes[ni].vigor, nodes[ni].budHeight});
                ++nodes[ni].children;
            }
        }
        if (additions.empty()) break;
        nodes.insert(nodes.end(), additions.begin(), additions.end());
    }

    for (int i = int(nodes.size()) - 1; i >= 0; --i) {
        const int parent = nodes[size_t(i)].parent;
        if (parent >= 0) {
            nodes[size_t(parent)].descendants += nodes[size_t(i)].descendants;
        }
    }
    const float twigRadius = settings.trunkRadius * 0.055f;
    for (size_t i = 0; i < nodes.size(); ++i) {
        const ColonyNode& node = nodes[i];
        if (node.parent < 0) continue;
        const ColonyNode& parent = nodes[size_t(node.parent)];
        const float       r0     = std::min(settings.trunkRadius * 0.34f * parent.vigor,
                                            twigRadius * parent.vigor * (1.f + std::sqrt(float(parent.descendants))));
        const float r1 = std::min(r0 * 0.92f, twigRadius * node.vigor * (1.f + std::sqrt(float(node.descendants))));
        addTaperedCylinder(out, parent.p, node.p, r0, r1, std::max(3, settings.sides - 2));
        if (node.children == 0) {
            foliageAnchors.push_back({node.p, node.dir, 0.42f + 0.22f * node.vigor});
        } else if (node.depth >= 2 && (node.depth % 2) == 0) {
            // Older lower limbs carry foliage along the branch, whereas young upper
            // shoots keep most foliage near their tips.
            const float coverage =
                settings.lowerLeafCoverage + (settings.upperLeafCoverage - settings.lowerLeafCoverage) * node.budHeight;
            if (hash01(uint32_t(i) * 747796405u + 2891336453u) < coverage)
                foliageAnchors.push_back({node.p, node.dir, 0.48f + 0.20f * (1.f - node.budHeight)});
        }
    }
}

}  // namespace

bool generateTreeMesh(const Params& params, MeshBuild& out, std::string& error) {
    const std::string style           = params.getString("style", "lowpoly");
    const std::string leafMode        = params.getString("leafMode", "cards");
    const std::string branchAlgorithm = params.getString("branchAlgorithm", "weberPenn");
    if (style != "lowpoly" && style != "realistic") {
        error = "mesh.tree: style must be lowpoly|realistic";
        return false;
    }
    if (leafMode != "cards" && leafMode != "canopy" && leafMode != "none") {
        error = "mesh.tree: leafMode must be cards|canopy|none";
        return false;
    }
    if (branchAlgorithm != "weberPenn" && branchAlgorithm != "spaceColonization") {
        error = "mesh.tree: branchAlgorithm must be weberPenn|spaceColonization";
        return false;
    }

    const bool  realistic            = style == "realistic";
    const float height               = std::max(0.5f, params.getFloat("height", 6.f));
    const float trunkRadius          = std::max(0.02f, params.getFloat("trunkRadius", height * 0.055f));
    const float crownRadius          = std::max(0.1f, params.getFloat("crownRadius", height * 0.34f));
    const float leafSize             = std::max(0.02f, params.getFloat("leafSize", height * 0.075f));
    const float density              = std::clamp(params.getFloat("leafDensity", 0.65f), 0.f, 1.f);
    const float foliageStart         = std::clamp(params.getFloat("foliageStart", 0.35f), 0.1f, 0.9f);
    const int   branchLevels         = std::clamp(params.getInt("branchLevels", realistic ? 3 : 2), 1, 5);
    const int   branchCount          = std::clamp(params.getInt("branchCount", realistic ? 9 : 6), 2, 20);
    const int   sides                = std::clamp(params.getInt("radialSegments", realistic ? 10 : 6), 3, 24);
    const int   curveSegments        = std::clamp(params.getInt("curveSegments", realistic ? 9 : 5), 2, 20);
    const float trunkCurve           = std::clamp(params.getFloat("trunkCurve", 0.10f), 0.f, 0.45f);
    const float curveBack            = std::clamp(params.getFloat("curveBack", 0.16f), -0.5f, 0.5f);
    const float branchCurve          = std::clamp(params.getFloat("branchCurve", 0.13f), 0.f, 0.5f);
    const float branchAngle          = std::clamp(params.getFloat("branchAngle", 62.f), 5.f, 88.f);
    const float angleVariation       = std::clamp(params.getFloat("branchAngleVariation", 12.f), 0.f, 40.f);
    const float phyllotaxis          = params.getFloat("phyllotaxis", 137.5f) * kPi / 180.f;
    const float tropism              = std::clamp(params.getFloat("tropism", 0.22f), -0.5f, 0.8f);
    const float droop                = std::clamp(params.getFloat("droop", 0.18f), 0.f, 0.8f);
    const float apicalDominance      = std::clamp(params.getFloat("apicalDominance", 0.62f), 0.f, 1.f);
    const int   attractorCount       = std::clamp(params.getInt("attractorCount", realistic ? 180 : 80), 12, 1200);
    const int colonizationIterations = std::clamp(params.getInt("colonizationIterations", realistic ? 46 : 30), 4, 160);
    const float influenceRadius      = std::max(0.05f, params.getFloat("influenceRadius", crownRadius * 1.08f));
    const float killRadius           = std::max(0.01f, params.getFloat("killRadius", crownRadius * 0.13f));
    const float growthStep = std::max(0.01f, params.getFloat("growthStep", crownRadius * (realistic ? 0.105f : 0.14f)));
    const float branchInertia       = std::clamp(params.getFloat("branchInertia", 1.20f), 0.f, 4.f);
    const float maxTurnAngle        = std::clamp(params.getFloat("maxTurnAngle", realistic ? 16.f : 22.f), 2.f, 60.f);
    const float maxCumulativeAngle  = std::clamp(params.getFloat("maxCumulativeAngle", 58.f), 10.f, 85.f);
    const float branchLengthFalloff = std::clamp(params.getFloat("branchLengthFalloff", 0.58f), 0.f, 0.9f);
    const float branchRadiusFalloff = std::clamp(params.getFloat("branchRadiusFalloff", 0.50f), 0.f, 0.9f);
    const float lowerLeafCoverage   = std::clamp(params.getFloat("lowerLeafCoverage", 0.72f), 0.f, 1.f);
    const float upperLeafCoverage   = std::clamp(params.getFloat("upperLeafCoverage", 0.18f), 0.f, 1.f);
    const int   maxChildren         = std::clamp(params.getInt("maxChildren", 2), 1, 4);
    std::mt19937 rng(params.getSeed());
    out.clear();

    const V3       root{0.f, 0.f, 0.f};
    const float    trunkAzimuth = randomRange(rng, 0.f, 2.f * kPi);
    const V3       trunkBend{std::cos(trunkAzimuth), 0.f, std::sin(trunkAzimuth)};
    const StemPath trunk =
        makeCurvedStem(root, {0.f, 1.f, 0.f}, trunkBend, height, trunkCurve * randomRange(rng, 0.72f, 1.28f),
                       curveBack * randomRange(rng, 0.72f, 1.28f), 0.f, curveSegments + 2);
    addStem(out, trunk, trunkRadius, trunkRadius * 0.16f, sides);

    std::vector<Tip> tips;
    std::vector<Tip> foliageAnchors;
    if (branchAlgorithm == "spaceColonization") {
        const ColonizationSettings colonization{
            .height             = height,
            .foliageStart       = foliageStart,
            .crownRadius        = crownRadius,
            .trunkRadius        = trunkRadius,
            .tropism            = tropism,
            .droop              = droop * 0.45f,
            .inertia            = branchInertia,
            .attractorCount     = attractorCount,
            .iterations         = colonizationIterations,
            .influenceRadius    = influenceRadius,
            .killRadius         = killRadius,
            .step               = growthStep,
            .maxTurnAngle       = maxTurnAngle,
            .maxCumulativeAngle = maxCumulativeAngle,
            .lengthFalloff      = branchLengthFalloff,
            .radiusFalloff      = branchRadiusFalloff,
            .lowerLeafCoverage  = lowerLeafCoverage,
            .upperLeafCoverage  = upperLeafCoverage,
            .maxChildren        = maxChildren,
            .sides              = sides,
        };
        growSpaceColonizedTree(out, trunk, rng, colonization, foliageAnchors);
    } else {
        for (int level = 0; level < branchLevels; ++level) {
            const int        count      = std::max(2, branchCount - level * 2);
            const float      levelScale = std::pow(0.62f, float(level));
            std::vector<Tip> next;
            for (int i = 0; i < count; ++i) {
                const float h     = foliageStart + (1.f - foliageStart) * (float(i) + 0.35f) / float(count);
                const float angle = float(i) * phyllotaxis + randomRange(rng, -0.20f, 0.20f) + level;
                V3          start;
                V3          parentDirection;
                if (level == 0) {
                    samplePath(trunk, h, start, parentDirection);
                } else {
                    const Tip& parent = tips[size_t(i) % tips.size()];
                    start             = parent.p;
                    parentDirection   = parent.dir;
                }
                V3 ringRight, ringForward;
                basisFor(parentDirection, ringRight, ringForward);
                const V3    radial = add(mul(ringRight, std::cos(angle)), mul(ringForward, std::sin(angle)));
                const float levelAngle =
                    branchAngle - float(level) * 7.f + randomRange(rng, -angleVariation, angleVariation);
                const float radians = std::clamp(levelAngle, 5.f, 88.f) * kPi / 180.f;
                V3          dir = norm(add(mul(parentDirection, std::cos(radians)), mul(radial, std::sin(radians))));
                dir             = norm(add(dir, mul(V3{0.f, 1.f, 0.f}, apicalDominance * 0.24f * h)));
                const float relativeHeight =
                    std::clamp((start.y / height - foliageStart) / (1.f - foliageStart), 0.f, 1.f);
                const float lengthTaper = 1.f - branchLengthFalloff * relativeHeight;
                const float radiusTaper = 1.f - branchRadiusFalloff * relativeHeight;
                const float length      = crownRadius * randomRange(rng, 0.72f, 1.18f) * lengthTaper * levelScale;
                const float r           = trunkRadius * (0.36f * radiusTaper) * levelScale;
                V3          bendRight, bendForward;
                basisFor(dir, bendRight, bendForward);
                const V3 bendAxis =
                    norm(add(mul(bendRight, std::cos(angle + 1.1f)), mul(bendForward, std::sin(angle + 1.1f))));
                const float    flexibility          = 1.f + float(level) * 0.38f;
                const float    downWeight           = droop * (0.35f + 0.65f * (1.f - h)) * flexibility;
                const float    verticalAcceleration = tropism * (0.55f + apicalDominance * h) - downWeight;
                const StemPath stem = makeCurvedStem(start, dir, bendAxis, length,
                                                     branchCurve * flexibility * randomRange(rng, 0.65f, 1.35f),
                                                     curveBack * 0.55f * flexibility * randomRange(rng, 0.55f, 1.25f),
                                                     verticalAcceleration, std::max(2, curveSegments - level));
                addStem(out, stem, r, r * 0.22f, std::max(3, sides - level * 2));
                const float    coverage = lowerLeafCoverage + (upperLeafCoverage - lowerLeafCoverage) * relativeHeight;
                const uint32_t coverageKey = params.getSeed() ^ uint32_t(level * 131 + i * 977);
                if (hash01(coverageKey) < coverage && stem.points.size() > 3) {
                    const size_t middle          = stem.points.size() / 2;
                    const V3     middleDirection = norm(sub(stem.points[middle], stem.points[middle - 1]));
                    foliageAnchors.push_back(
                        {stem.points[middle], middleDirection, levelScale * (0.72f + 0.28f * (1.f - relativeHeight))});
                }
                next.push_back({stem.points.back(), stem.endDirection, levelScale});
            }
            foliageAnchors.insert(foliageAnchors.end(), next.begin(), next.end());
            tips.swap(next);
        }
    }

    if (leafMode == "cards") {
        const int perTip = int(std::round((realistic ? 12.f : 6.f) * density));
        for (const Tip& tip : foliageAnchors) {
            for (int i = 0; i < perTip; ++i) {
                V3       c = add(tip.p, {randomRange(rng, -1.f, 1.f) * crownRadius * 0.24f * tip.scale,
                                         randomRange(rng, -0.25f, 0.55f) * crownRadius * tip.scale,
                                         randomRange(rng, -1.f, 1.f) * crownRadius * 0.24f * tip.scale});
                const V3 face =
                    norm({randomRange(rng, -1.f, 1.f), randomRange(rng, -0.2f, 0.8f), randomRange(rng, -1.f, 1.f)});
                addLeafCard(out, c, face, leafSize * randomRange(rng, 0.72f, 1.25f), randomRange(rng, 0.f, kPi));
            }
        }
    } else if (leafMode == "canopy" && density > 0.f) {
        // A canopy lobe belongs to a branch: center it just behind the terminal
        // point so the branch visibly penetrates the foliage instead of ending in air.
        int wanted = std::max(1, int(std::round(float(foliageAnchors.size()) * (0.28f + density * 0.62f))));
        if (branchAlgorithm == "spaceColonization") wanted = int(foliageAnchors.size());
        const int stride  = std::max(1, int(foliageAnchors.size()) / wanted);
        int       emitted = 0;
        for (int i = int(foliageAnchors.size()) - 1; i >= 0 && emitted < wanted; i -= stride) {
            const Tip&  tip     = foliageAnchors[size_t(i)];
            const float lobeMin = branchAlgorithm == "spaceColonization" ? 0.14f : 0.27f;
            const float lobeMax = branchAlgorithm == "spaceColonization" ? 0.22f : 0.40f;
            const float r       = crownRadius * randomRange(rng, lobeMin, lobeMax) * (0.72f + 0.28f * density) *
                                  (0.58f + 0.42f * tip.scale);
            V3          c       = sub(tip.p, mul(tip.dir, r * 0.22f));
            c.y += r * randomRange(rng, 0.08f, 0.22f);
            addCanopyBlob(out, c, {r, r * randomRange(rng, 0.72f, 1.15f), r}, realistic ? 8 : 4, realistic ? 12 : 7);
            ++emitted;
        }
    }

    out.setMeta("recipe", "mesh.tree");
    out.setMeta("style", style);
    out.setMeta("leafMode", leafMode);
    out.setMeta("branchAlgorithm", branchAlgorithm);
    out.setMeta("seed", std::to_string(params.getSeed()));
    if (out.empty()) {
        error = "mesh.tree: generated an empty mesh";
        return false;
    }
    return true;
}

}  // namespace eve::procgen
