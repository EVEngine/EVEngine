#include "procgen/algorithms/RockMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace eve::procgen {
namespace {

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

struct Tri {
    uint32_t a = 0, b = 0, c = 0;
};

struct ShapeProfile {
    const char *name;
    float axisX, axisY, axisZ;
    float power;
    float skewX, skewZ;
    int cuts;
    float cutDepth;
};

Vec3 normalize(Vec3 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 1e-8f) return {0.f, 1.f, 0.f};
    return {v.x / len, v.y / len, v.z / len};
}

Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

inline float fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }
inline float mix(float a, float b, float t) { return a + (b - a) * t; }

float grad3(uint32_t h, float x, float y, float z) {
    const uint32_t g = h & 15u;
    const float u = g < 8 ? x : y;
    const float v = g < 4 ? y : (g == 12 || g == 14 ? x : z);
    return ((g & 1u) ? -u : u) + ((g & 2u) ? -v : v);
}

float noise3(float x, float y, float z, uint32_t seed) {
    const int xi = int(std::floor(x)), yi = int(std::floor(y)), zi = int(std::floor(z));
    const float xf = x - float(xi), yf = y - float(yi), zf = z - float(zi);
    const float u = fade(xf), v = fade(yf), w = fade(zf);
    auto hash = [&](int ix, int iy, int iz) {
        return uint32_t(ix) * 374761393u + uint32_t(iy) * 668265263u +
               uint32_t(iz) * 1274126177u + seed * 2246822519u;
    };
    const float n000 = grad3(hash(xi, yi, zi), xf, yf, zf);
    const float n100 = grad3(hash(xi + 1, yi, zi), xf - 1.f, yf, zf);
    const float n010 = grad3(hash(xi, yi + 1, zi), xf, yf - 1.f, zf);
    const float n110 = grad3(hash(xi + 1, yi + 1, zi), xf - 1.f, yf - 1.f, zf);
    const float n001 = grad3(hash(xi, yi, zi + 1), xf, yf, zf - 1.f);
    const float n101 = grad3(hash(xi + 1, yi, zi + 1), xf - 1.f, yf, zf - 1.f);
    const float n011 = grad3(hash(xi, yi + 1, zi + 1), xf, yf - 1.f, zf - 1.f);
    const float n111 = grad3(hash(xi + 1, yi + 1, zi + 1), xf - 1.f, yf - 1.f, zf - 1.f);
    return mix(mix(mix(n000, n100, u), mix(n010, n110, u), v),
               mix(mix(n001, n101, u), mix(n011, n111, u), v), w);
}

float fbm3(float x, float y, float z, uint32_t seed, int octaves) {
    float sum = 0.f, amplitude = 0.5f, frequency = 1.f, norm = 0.f;
    for (int i = 0; i < octaves; ++i) {
        sum += noise3(x * frequency, y * frequency, z * frequency,
                      seed + uint32_t(i) * 1013u) * amplitude;
        norm += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.f;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

uint32_t nextRandom(uint32_t &state) {
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

float random01(uint32_t &state) {
    return float(nextRandom(state) & 0x00ffffffu) / float(0x01000000u);
}

float randomSigned(uint32_t &state) { return random01(state) * 2.f - 1.f; }

bool resolveShape(const std::string &requested, uint32_t seed, ShapeProfile &shape) {
    static constexpr ShapeProfile profiles[] = {
        {"boulder", 1.f, 1.f, 0.94f, 0.92f, 0.02f, -0.01f, 3, 0.13f},
        {"slab", 1.28f, 0.62f, 1.02f, 0.74f, 0.08f, -0.03f, 5, 0.18f},
        {"block", 1.04f, 0.90f, 0.96f, 0.46f, 0.03f, 0.02f, 6, 0.20f},
        {"shard", 0.70f, 1.34f, 0.56f, 0.62f, 0.22f, -0.12f, 7, 0.24f},
    };
    std::string name = requested;
    if (name == "mixed") name = profiles[seed % 4u].name;
    for (const ShapeProfile &candidate : profiles) {
        if (name == candidate.name) {
            shape = candidate;
            return true;
        }
    }
    return false;
}

float shapedCoordinate(float value, float power) {
    return std::copysign(std::pow(std::abs(value), power), value);
}

uint64_t edgeKey(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return (uint64_t(a) << 32u) | uint64_t(b);
}

uint32_t midpoint(uint32_t a, uint32_t b, std::vector<Vec3> &vertices,
                  std::unordered_map<uint64_t, uint32_t> &cache) {
    const uint64_t key = edgeKey(a, b);
    const auto found = cache.find(key);
    if (found != cache.end()) return found->second;
    const Vec3 &va = vertices[a], &vb = vertices[b];
    const uint32_t index = uint32_t(vertices.size());
    vertices.push_back(normalize({(va.x + vb.x) * 0.5f, (va.y + vb.y) * 0.5f,
                                  (va.z + vb.z) * 0.5f}));
    cache.emplace(key, index);
    return index;
}

void createIcosphere(int subdivisions, std::vector<Vec3> &vertices, std::vector<Tri> &triangles) {
    constexpr float phi = 1.6180339887498948482f;
    vertices = {{-1, phi, 0}, {1, phi, 0}, {-1, -phi, 0}, {1, -phi, 0},
                {0, -1, phi}, {0, 1, phi}, {0, -1, -phi}, {0, 1, -phi},
                {phi, 0, -1}, {phi, 0, 1}, {-phi, 0, -1}, {-phi, 0, 1}};
    for (Vec3 &v : vertices) v = normalize(v);
    triangles = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
                 {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                 {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
                 {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};

    for (int level = 0; level < subdivisions; ++level) {
        std::unordered_map<uint64_t, uint32_t> cache;
        std::vector<Tri> next;
        next.reserve(triangles.size() * 4);
        for (const Tri &t : triangles) {
            const uint32_t ab = midpoint(t.a, t.b, vertices, cache);
            const uint32_t bc = midpoint(t.b, t.c, vertices, cache);
            const uint32_t ca = midpoint(t.c, t.a, vertices, cache);
            next.push_back({t.a, ab, ca});
            next.push_back({t.b, bc, ab});
            next.push_back({t.c, ca, bc});
            next.push_back({ab, bc, ca});
        }
        triangles.swap(next);
    }
}

}  // namespace

bool generateRockMesh(const Params &params, MeshBuild &out, std::string &error) {
    const int subdivisions = params.getInt("subdivisions", 3);
    if (subdivisions < 0 || subdivisions > 5) {
        error = "mesh.rock: subdivisions must be in [0,5]";
        return false;
    }
    const float radius = std::max(0.05f, params.getFloat("radius", 0.72f));
    const float flattening = std::clamp(params.getFloat("flattening", 0.22f), 0.f, 0.7f);
    const float angularity = std::clamp(params.getFloat("angularity", 0.38f), 0.f, 1.f);
    const float erosion = std::clamp(params.getFloat("erosion", 0.16f), 0.f, 0.45f);
    const float scale = std::max(0.25f, params.getFloat("scale", 2.4f));
    const int octaves = std::clamp(params.getInt("octaves", 4), 1, 8);
    const uint32_t seed = params.getSeed();
    const std::string requestedShape = params.getString("baseShape", "mixed");
    ShapeProfile shape{};
    if (!resolveShape(requestedShape, seed, shape)) {
        error = "mesh.rock: unknown baseShape '" + requestedShape +
                "' (use mixed|boulder|slab|block|shard)";
        return false;
    }

    uint32_t randomState = seed ^ 0x9e3779b9u;
    const float variation = std::clamp(params.getFloat("variation", 0.42f), 0.f, 1.f);
    shape.axisX *= 1.f + randomSigned(randomState) * 0.18f * variation;
    shape.axisY *= 1.f + randomSigned(randomState) * 0.14f * variation;
    shape.axisZ *= 1.f + randomSigned(randomState) * 0.18f * variation;
    shape.power = std::clamp(shape.power + randomSigned(randomState) * 0.12f * variation,
                             0.32f, 1.1f);
    shape.skewX += randomSigned(randomState) * 0.12f * variation;
    shape.skewZ += randomSigned(randomState) * 0.12f * variation;
    if (params.has("axisX")) shape.axisX = std::max(0.2f, params.getFloat("axisX", shape.axisX));
    if (params.has("axisY")) shape.axisY = std::max(0.2f, params.getFloat("axisY", shape.axisY));
    if (params.has("axisZ")) shape.axisZ = std::max(0.2f, params.getFloat("axisZ", shape.axisZ));
    if (params.has("shapePower"))
        shape.power = std::clamp(params.getFloat("shapePower", shape.power), 0.25f, 1.25f);
    if (params.has("skewX")) shape.skewX = params.getFloat("skewX", shape.skewX);
    if (params.has("skewZ")) shape.skewZ = params.getFloat("skewZ", shape.skewZ);
    const int cutCount = std::clamp(params.getInt("cutCount", shape.cuts), 0, 12);
    const float cutDepth = std::clamp(params.getFloat("cutDepth", shape.cutDepth), 0.f, 0.42f);

    std::vector<Vec3> directions;
    std::vector<Tri> triangles;
    createIcosphere(subdivisions, directions, triangles);
    std::vector<Vec3> positions(directions.size());
    std::vector<Vec3> normals(directions.size());
    const float steps = 3.f + angularity * 9.f;
    for (size_t i = 0; i < directions.size(); ++i) {
        const Vec3 d = directions[i];
        const float qx = std::round(d.x * steps) / steps;
        const float qy = std::round(d.y * steps) / steps;
        const float qz = std::round(d.z * steps) / steps;
        const float strata = fbm3((qx + 2.3f) * scale, (qy + 4.7f) * scale,
                                  (qz + 8.1f) * scale, seed, octaves);
        const float pits = fbm3((d.x + 7.2f) * scale * 2.7f,
                                (d.y + 1.9f) * scale * 2.7f,
                                (d.z + 5.4f) * scale * 2.7f,
                                seed + 7919u, std::max(2, octaves - 1));
        const float displacement = strata * (0.08f + angularity * 0.16f) -
                                   std::max(0.f, pits) * erosion;
        const float r = std::max(radius * 0.45f, radius + displacement);
        Vec3 p = {shapedCoordinate(d.x, shape.power) * r * shape.axisX,
                  shapedCoordinate(d.y, shape.power) * r * shape.axisY * (1.f - flattening),
                  shapedCoordinate(d.z, shape.power) * r * shape.axisZ};
        p.x += p.y * shape.skewX;
        p.z += p.y * shape.skewZ;
        positions[i] = p;
    }

    // Clamp the deformed surface against deterministic planes. This preserves the icosphere's
    // economical topology while creating broad fracture faces and non-round silhouettes.
    for (int cut = 0; cut < cutCount; ++cut) {
        Vec3 plane = normalize({randomSigned(randomState), randomSigned(randomState),
                                randomSigned(randomState)});
        if (cut == 0) plane = normalize({0.2f, -1.f, 0.1f});  // stable resting face
        const float support = radius * std::sqrt(
            plane.x * plane.x * shape.axisX * shape.axisX +
            plane.y * plane.y * shape.axisY * shape.axisY * (1.f - flattening) *
                (1.f - flattening) +
            plane.z * plane.z * shape.axisZ * shape.axisZ);
        const float depth = cutDepth * (0.72f + random01(randomState) * 0.56f);
        const float limit = support * (1.f - depth);
        for (Vec3 &p : positions) {
            const float excess = p.x * plane.x + p.y * plane.y + p.z * plane.z - limit;
            if (excess > 0.f) {
                p.x -= plane.x * excess;
                p.y -= plane.y * excess;
                p.z -= plane.z * excess;
            }
        }
    }

    for (const Tri &t : triangles) {
        const Vec3 &a = positions[t.a], &b = positions[t.b], &c = positions[t.c];
        const Vec3 face = cross({b.x - a.x, b.y - a.y, b.z - a.z},
                                {c.x - a.x, c.y - a.y, c.z - a.z});
        for (uint32_t index : {t.a, t.b, t.c}) {
            normals[index].x += face.x;
            normals[index].y += face.y;
            normals[index].z += face.z;
        }
    }
    for (Vec3 &n : normals) n = normalize(n);

    out.clear();
    out.reserve(int(positions.size()), int(triangles.size() * 3));
    for (size_t i = 0; i < positions.size(); ++i) {
        const Vec3 &p = positions[i], &n = normals[i];
        out.addVertex(p.x, p.y, p.z, n.x, n.y, n.z, p.x + 0.5f, p.y + 0.5f);
    }
    for (const Tri &t : triangles) out.addTriangle(t.a, t.b, t.c);
    out.setMeta("algorithm", "mesh.rock");
    out.setMeta("topology", "icosphere");
    out.setMeta("baseShape", shape.name);
    out.setMeta("cutCount", std::to_string(cutCount));
    out.setMeta("subdivisions", std::to_string(subdivisions));
    return true;
}

}  // namespace eve::procgen
