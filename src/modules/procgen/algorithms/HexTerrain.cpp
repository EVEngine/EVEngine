#include "procgen/algorithms/HexTerrain.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

namespace eve::procgen {
namespace {

enum Biome : int {
    DeepOcean = 0,
    Ocean,
    Coast,
    Grassland,
    Hills,
    Mountain,
    Forest,
    Swamp,
    Rainforest,
    Ice,
    Cliff,
    River,
};

struct Cell {
    float elevation = 0.f;
    float moisture = 0.f;
    float temperature = 0.f;
    float river = 0.f;
    int biome = Ocean;
    int secondary = Ocean;
    float blend = 0.f;
    uint8_t riverEdges = 0;
    bool lake = false;
};

uint32_t hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}

float valueNoise(float x, float y, uint32_t seed) {
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    const float fx = x - float(ix);
    const float fy = y - float(iy);
    const float sx = fx * fx * (3.f - 2.f * fx);
    const float sy = fy * fy * (3.f - 2.f * fy);
    auto sample = [seed](int px, int py) {
        const uint32_t h = hash(uint32_t(px) * 0x9e3779b9u ^ uint32_t(py) * 0x85ebca6bu ^ seed);
        return float(h & 0xffffu) / 32767.5f - 1.f;
    };
    const float a = std::lerp(sample(ix, iy), sample(ix + 1, iy), sx);
    const float b = std::lerp(sample(ix, iy + 1), sample(ix + 1, iy + 1), sx);
    return std::lerp(a, b, sy);
}

float fbm(float x, float y, uint32_t seed) {
    float value = 0.f, amplitude = 0.55f, frequency = 1.f, norm = 0.f;
    for (int octave = 0; octave < 5; ++octave) {
        value += valueNoise(x * frequency, y * frequency, seed + uint32_t(octave) * 1013u) * amplitude;
        norm += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.03f;
    }
    return value / norm;
}

int indexOf(int q, int r, int width, int height) {
    return q < 0 || r < 0 || q >= width || r >= height ? -1 : q + r * width;
}

std::array<int, 6> neighbours(int q, int r, int width, int height) {
    static constexpr int dq[6] = {1, 0, -1, -1, 0, 1};
    const int parity = q & 1;
    const int dr[6] = {parity, 1, parity, parity - 1, -1, parity - 1};
    std::array<int, 6> result{};
    for (int i = 0; i < 6; ++i) result[i] = indexOf(q + dq[i], r + dr[i], width, height);
    return result;
}

void addRiverArm(MeshBuild& out, float cx, float y, float cz, float radius, int edge,
                 float coverage, uint32_t shapeSeed) {
    const float angle = 1.0471975512f * (float(edge) + 0.5f);
    const float dx = std::cos(angle), dz = std::sin(angle);
    const float ex = cx + dx * radius * 0.94f;
    const float ez = cz + dz * radius * 0.94f;
    const float bend = (float(hash(shapeSeed ^ uint32_t(edge) * 0x9e3779b9u) & 0xffffu) /
                        65535.f * 2.f - 1.f) * radius * 0.30f;
    const float mx = cx + dx * radius * 0.48f - dz * bend;
    const float mz = cz + dz * radius * 0.48f + dx * bend;
    const float u = float(River) + 0.04f;
    const float v = float(River) + std::clamp(coverage, 0.f, 0.999f);
    const uint32_t base = uint32_t(out.getVertexCount());
    constexpr int curveSegments = 6;
    for (int segment = 0; segment <= curveSegments; ++segment) {
        const float t = float(segment) / float(curveSegments);
        const float omt = 1.f - t;
        const float x = omt * omt * cx + 2.f * omt * t * mx + t * t * ex;
        const float z = omt * omt * cz + 2.f * omt * t * mz + t * t * ez;
        float tx = 2.f * omt * (mx - cx) + 2.f * t * (ex - mx);
        float tz = 2.f * omt * (mz - cz) + 2.f * t * (ez - mz);
        const float invLength = 1.f / std::max(0.0001f, std::sqrt(tx * tx + tz * tz));
        tx *= invLength;
        tz *= invLength;
        const float halfWidth = radius * (0.102f + coverage * 0.064f) *
                                (0.82f + 0.18f * std::sin(t * 3.1415926536f));
        const float px = -tz, pz = tx;
        out.addVertex(x + px * halfWidth, y, z + pz * halfWidth,
                      0.f, 1.f, 0.f, u, v);
        out.addVertex(x - px * halfWidth, y, z - pz * halfWidth,
                      0.f, 1.f, 0.f, u, v);
    }
    for (int segment = 0; segment < curveSegments; ++segment) {
        const uint32_t a = base + uint32_t(segment * 2);
        out.addTriangle(a, a + 1u, a + 3u);
        out.addTriangle(a, a + 3u, a + 2u);
    }
}

void addRiverJunction(MeshBuild& out, float cx, float y, float cz, float radius,
                      float coverage) {
    constexpr int segments = 12;
    const float junctionRadius = radius * (0.105f + coverage * 0.045f);
    const float u = float(River) + 0.04f;
    const float v = float(River) + std::clamp(coverage, 0.f, 0.999f);
    const uint32_t center = uint32_t(out.getVertexCount());
    out.addVertex(cx, y, cz, 0.f, 1.f, 0.f, u, v);
    for (int k = 0; k < segments; ++k) {
        const float angle = 6.2831853072f * float(k) / float(segments);
        out.addVertex(cx + std::cos(angle) * junctionRadius, y,
                      cz + std::sin(angle) * junctionRadius, 0.f, 1.f, 0.f, u, v);
    }
    for (int k = 0; k < segments; ++k)
        out.addTriangle(center, center + 1u + uint32_t(k),
                        center + 1u + uint32_t((k + 1) % segments));
}

void addShoreBand(MeshBuild& out, float cx, float y, float cz, float radius, int edge) {
    const float a0 = 1.0471975512f * float(edge);
    const float a1 = 1.0471975512f * float((edge + 1) % 6);
    constexpr float innerScale = 0.89f;
    const float u = float(Coast) + 0.92f;
    const float v = float(Ocean) + 0.15f;
    const uint32_t base = uint32_t(out.getVertexCount());
    out.addVertex(cx + std::cos(a0) * radius, y, cz + std::sin(a0) * radius,
                  0.f, 1.f, 0.f, u, v);
    out.addVertex(cx + std::cos(a1) * radius, y, cz + std::sin(a1) * radius,
                  0.f, 1.f, 0.f, u, v);
    out.addVertex(cx + std::cos(a1) * radius * innerScale, y,
                  cz + std::sin(a1) * radius * innerScale, 0.f, 1.f, 0.f, u, v);
    out.addVertex(cx + std::cos(a0) * radius * innerScale, y,
                  cz + std::sin(a0) * radius * innerScale, 0.f, 1.f, 0.f, u, v);
    out.addTriangle(base, base + 1u, base + 2u);
    out.addTriangle(base, base + 2u, base + 3u);
}

void classify(Cell& c, float sea) {
    if (c.elevation < sea - 0.22f) c.biome = DeepOcean;
    else if (c.temperature < 0.16f) c.biome = Ice;
    else if (c.elevation < sea - 0.045f) c.biome = Ocean;
    else if (c.elevation < sea + 0.025f) c.biome = Coast;
    else if (c.elevation > sea + 0.55f) c.biome = Mountain;
    else if (c.elevation > sea + 0.35f) c.biome = Hills;
    else if (c.moisture > 0.68f && c.elevation < sea + 0.16f) c.biome = Swamp;
    else if (c.moisture > 0.64f && c.temperature > 0.55f) c.biome = Rainforest;
    else if (c.moisture > 0.56f) c.biome = Forest;
    else c.biome = Grassland;
    c.secondary = c.biome;
}

float surfaceHeight(const Cell& c, float sea, float heightScale) {
    float y = (c.elevation - sea) * heightScale;
    if (c.lake) return y - 0.035f * heightScale;
    if (c.biome == DeepOcean || c.biome == Ocean)
        return std::min(y, -0.08f * heightScale);

    const float land = std::max(0.f, c.elevation - sea);
    if (c.biome == Hills) y += (0.08f + land * 0.16f) * heightScale;
    if (c.biome == Mountain) y += (0.22f + land * 0.38f) * heightScale;
    if (c.biome == Ice && land > 0.28f) y += land * 0.22f * heightScale;
    if (c.river > 0.f) y -= (0.045f + 0.035f * c.river) * heightScale;
    return y;
}

void addCone(MeshBuild& out, float cx, float baseY, float cz, float radius, float height,
             int biome, int segments = 6, uint32_t shapeSeed = 0) {
    const uint32_t base = uint32_t(out.getVertexCount());
    // 0.92 marks procedural decoration vertices for optional wind animation.
    const float u = float(biome) + 0.92f;
    const float v = float(biome);
    for (int k = 0; k < segments; ++k) {
        const float a = 6.2831853072f * float(k) / float(segments);
        const float nx = std::cos(a), nz = std::sin(a);
        const float radial = shapeSeed == 0 ? 1.f :
            0.78f + float(hash(shapeSeed + uint32_t(k) * 0x85ebca6bu) & 0xffffu) /
                        65535.f * 0.38f;
        out.addVertex(cx + nx * radius * radial, baseY, cz + nz * radius * radial,
                      nx * 0.82f, radius / std::max(height, 0.01f), nz * 0.82f, u, v);
    }
    const float apexX = shapeSeed == 0 ? cx : cx +
        (float(hash(shapeSeed ^ 0x41c64e6du) & 0xffffu) / 65535.f - 0.5f) * radius * 0.34f;
    const float apexZ = shapeSeed == 0 ? cz : cz +
        (float(hash(shapeSeed ^ 0xc2b2ae35u) & 0xffffu) / 65535.f - 0.5f) * radius * 0.34f;
    if (shapeSeed != 0) {
        const uint32_t shoulder = uint32_t(out.getVertexCount());
        for (int k = 0; k < segments; ++k) {
            const float a = 6.2831853072f * (float(k) + 0.16f) / float(segments);
            const float nx = std::cos(a), nz = std::sin(a);
            const float radial = 0.43f +
                float(hash(shapeSeed ^ 0xa511e9b3u ^ uint32_t(k) * 0x27d4eb2du) & 0xffffu) /
                    65535.f * 0.22f;
            out.addVertex(std::lerp(cx, apexX, 0.34f) + nx * radius * radial,
                          baseY + height * (0.43f + radial * 0.16f),
                          std::lerp(cz, apexZ, 0.34f) + nz * radius * radial,
                          nx * 0.88f, 0.34f, nz * 0.88f, u, v);
        }
        const uint32_t apex = uint32_t(out.getVertexCount());
        out.addVertex(apexX, baseY + height, apexZ, 0.f, 1.f, 0.f, u, v);
        for (int k = 0; k < segments; ++k) {
            const uint32_t next = uint32_t((k + 1) % segments);
            const uint32_t lower = base + uint32_t(k);
            const uint32_t lowerNext = base + next;
            const uint32_t upper = shoulder + uint32_t(k);
            const uint32_t upperNext = shoulder + next;
            out.addTriangle(lower, lowerNext, upperNext);
            out.addTriangle(lower, upperNext, upper);
            out.addTriangle(upper, upperNext, apex);
        }
        return;
    }
    const uint32_t apex = uint32_t(out.getVertexCount());
    out.addVertex(apexX, baseY + height, apexZ, 0.f, 1.f, 0.f, u, v);
    for (int k = 0; k < segments; ++k)
        out.addTriangle(base + uint32_t(k), base + uint32_t((k + 1) % segments), apex);
}

void addTrunk(MeshBuild& out, float cx, float y, float cz, float radius, float height) {
    constexpr int segments = 5;
    const uint32_t base = uint32_t(out.getVertexCount());
    const float u = float(Cliff) + 0.12f;
    const float v = float(Forest);
    for (int level = 0; level < 2; ++level) for (int k = 0; k < segments; ++k) {
        const float a = 6.2831853072f * float(k) / float(segments);
        const float nx = std::cos(a), nz = std::sin(a);
        out.addVertex(cx + nx * radius, y + float(level) * height, cz + nz * radius,
                      nx, 0.f, nz, u, v);
    }
    for (int k = 0; k < segments; ++k) {
        const uint32_t next = uint32_t((k + 1) % segments);
        out.addTriangle(base + uint32_t(k), base + next, base + uint32_t(segments + k));
        out.addTriangle(base + next, base + uint32_t(segments) + next,
                        base + uint32_t(segments + k));
    }
}

void addCrownBlob(MeshBuild& out, float cx, float y, float cz, float radius, float height,
                  int biome, int segments = 7) {
    const float u = float(biome) + 0.92f;
    const float v = float(biome);
    const uint32_t bottom = uint32_t(out.getVertexCount());
    out.addVertex(cx, y, cz, 0.f, -1.f, 0.f, u, v);
    const uint32_t ring = uint32_t(out.getVertexCount());
    for (int k = 0; k < segments; ++k) {
        const float a = 6.2831853072f * float(k) / float(segments);
        const float nx = std::cos(a), nz = std::sin(a);
        out.addVertex(cx + nx * radius, y + height * 0.43f, cz + nz * radius,
                      nx, 0.25f, nz, u, v);
    }
    const uint32_t top = uint32_t(out.getVertexCount());
    out.addVertex(cx, y + height, cz, 0.f, 1.f, 0.f, u, v);
    for (int k = 0; k < segments; ++k) {
        const uint32_t a = ring + uint32_t(k);
        const uint32_t b = ring + uint32_t((k + 1) % segments);
        out.addTriangle(bottom, b, a);
        out.addTriangle(a, b, top);
    }
}

void addTree(MeshBuild& out, float x, float y, float z, float scale, int biome,
             bool broadleaf) {
    addTrunk(out, x, y, z, scale * 0.075f, scale * 0.48f);
    if (broadleaf) {
        const float broad = biome == Rainforest ? 0.46f : 0.39f;
        addCrownBlob(out, x, y + scale * 0.34f, z, scale * broad, scale * 0.78f,
                     biome, biome == Rainforest ? 8 : 7);
        if (biome == Rainforest)
            addCrownBlob(out, x + scale * 0.13f, y + scale * 0.55f, z - scale * 0.09f,
                         scale * 0.31f, scale * 0.62f, biome, 7);
    } else if (biome == Rainforest) {
        addCone(out, x, y + scale * 0.34f, z, scale * 0.38f, scale * 0.72f, biome, 7);
        addCone(out, x, y + scale * 0.68f, z, scale * 0.28f, scale * 0.55f, biome, 7);
    } else {
        addCone(out, x, y + scale * 0.30f, z, scale * 0.32f, scale * 0.64f, biome, 6);
        addCone(out, x, y + scale * 0.58f, z, scale * 0.24f, scale * 0.56f, biome, 6);
    }
}

void addTop(MeshBuild& out, float cx, float cz, float y, float radius, const Cell& c) {
    const uint32_t center = uint32_t(out.getVertexCount());
    const float u = float(c.biome) + std::clamp(c.blend, 0.f, 0.999f);
    const float v = float(c.secondary) + std::clamp(c.river, 0.f, 0.999f);
    out.addVertex(cx, y, cz, 0.f, 1.f, 0.f, u, v);
    for (int k = 0; k < 6; ++k) {
        const float a = 1.0471975512f * float(k);
        out.addVertex(cx + std::cos(a) * radius, y, cz + std::sin(a) * radius,
                      0.f, 1.f, 0.f, u, v);
    }
    for (int k = 0; k < 6; ++k)
        out.addTriangle(center, center + 1u + uint32_t(k), center + 1u + uint32_t((k + 1) % 6));
}

void addCliff(MeshBuild& out, float cx, float cz, float radius, int edge, float top, float bottom,
              const Cell& c, uint32_t shapeSeed) {
    const float a0 = 1.0471975512f * float(edge);
    const float a1 = 1.0471975512f * float((edge + 1) % 6);
    const float x0 = cx + std::cos(a0) * radius, z0 = cz + std::sin(a0) * radius;
    const float x1 = cx + std::cos(a1) * radius, z1 = cz + std::sin(a1) * radius;
    const float nx = std::sin((a0 + a1) * 0.5f), nz = -std::cos((a0 + a1) * 0.5f);
    const float relief = std::abs(top - bottom);
    const bool rockFace = relief > radius * 0.42f;
    const float u = rockFace ? float(Cliff) + std::min(0.999f, relief)
                             : float(c.biome) + std::clamp(c.blend, 0.f, 0.49f);
    const float v = rockFace ? float(c.biome)
                             : float(c.secondary) + std::clamp(c.river, 0.f, 0.999f);
    constexpr int wallSegments = 5;
    std::array<float, wallSegments + 1> topX{}, topY{}, topZ{}, bottomX{}, bottomY{}, bottomZ{};
    for (int segment = 0; segment <= wallSegments; ++segment) {
        const float t = float(segment) / float(wallSegments);
        const float straightX = std::lerp(x0, x1, t);
        const float straightZ = std::lerp(z0, z1, t);
        const uint32_t h = hash(shapeSeed + uint32_t(segment) * 0x9e3779b9u);
        const float endMask = segment == 0 || segment == wallSegments ? 0.f : 1.f;
        const float lipScale = rockFace ? 0.26f : 0.12f;
        const float footScale = rockFace ? 0.34f : 0.16f;
        const float lipOffset = (float(h & 0xffffu) / 65535.f - 0.42f) *
                                radius * lipScale * endMask;
        const float footOffset = (float((h >> 16) & 0xffffu) / 65535.f - 0.5f) *
                                 radius * footScale * endMask;
        topX[size_t(segment)] = straightX + nx * lipOffset;
        topY[size_t(segment)] = top + (rockFace ?
            (float(hash(h ^ 0x165667b1u) & 0xffffu) / 65535.f - 0.38f) *
                radius * 0.22f * endMask : 0.f);
        topZ[size_t(segment)] = straightZ + nz * lipOffset;
        bottomX[size_t(segment)] = straightX + nx * footOffset;
        bottomY[size_t(segment)] = bottom + (rockFace ?
            (float(hash(h ^ 0xd3a2646cu) & 0xffffu) / 65535.f) *
                std::min(relief * 0.16f, radius * 0.16f) * endMask : 0.f);
        bottomZ[size_t(segment)] = straightZ + nz * footOffset;
    }
    for (int segment = 0; segment < wallSegments; ++segment) {
        const uint32_t h = hash(shapeSeed ^ uint32_t(segment) * 0x85ebca6bu);
        const float shadeTurn = (float(h & 0xffffu) / 65535.f - 0.5f) * 0.34f;
        const float snx = nx + std::cos(a1) * shadeTurn;
        const float snz = nz + std::sin(a1) * shadeTurn;
        const uint32_t base = uint32_t(out.getVertexCount());
        out.addVertex(topX[size_t(segment)], topY[size_t(segment)], topZ[size_t(segment)], snx, 0.f, snz, u, v);
        out.addVertex(topX[size_t(segment + 1)], topY[size_t(segment + 1)], topZ[size_t(segment + 1)], snx, 0.f, snz, u, v);
        out.addVertex(bottomX[size_t(segment + 1)], bottomY[size_t(segment + 1)], bottomZ[size_t(segment + 1)], snx, 0.f, snz, u, v);
        out.addVertex(bottomX[size_t(segment)], bottomY[size_t(segment)], bottomZ[size_t(segment)], snx, 0.f, snz, u, v);
        out.addTriangle(base, base + 1u, base + 2u);
        out.addTriangle(base, base + 2u, base + 3u);
    }
}

}  // namespace

bool generateHexTerrainMesh(const Params& params, MeshBuild& out, std::string& error) {
    const int width = params.getInt("width", 32);
    const int height = params.getInt("height", 24);
    const int riverCount = params.getInt("riverCount", 8);
    const uint32_t seed = uint32_t(params.getInt("seed", 1));
    const float radius = params.getFloat("radius", 1.f);
    const float sea = params.getFloat("seaLevel", 0.43f);
    const float heightScale = params.getFloat("heightScale", 4.f);
    const bool decorations = params.getBool("decorations", true);
    const float vegetationDensity = params.getFloat("vegetationDensity", 1.f);
    if (width < 2 || height < 2 || width > 256 || height > 256) {
        error = "mesh.hexterrain: width and height must be in [2, 256]";
        return false;
    }
    if (!(radius > 0.f) || !(heightScale > 0.f) || sea < 0.f || sea > 1.f || riverCount < 0 ||
        vegetationDensity < 0.f || vegetationDensity > 2.f) {
        error = "mesh.hexterrain: invalid radius, heightScale, seaLevel or riverCount";
        return false;
    }

    std::vector<Cell> cells(size_t(width * height));
    for (int r = 0; r < height; ++r) for (int q = 0; q < width; ++q) {
        Cell& c = cells[size_t(q + r * width)];
        const float nx = (float(q) / float(width - 1)) * 2.f - 1.f;
        const float ny = (float(r) / float(height - 1)) * 2.f - 1.f;
        const float continental = 1.f - std::pow(std::min(1.f, std::sqrt(nx * nx + ny * ny)), 1.7f);
        c.elevation = std::clamp(0.12f + continental * 0.78f + fbm(q * 0.085f, r * 0.085f, seed) * 0.22f, 0.f, 1.f);
        c.moisture = std::clamp(0.52f + fbm(q * 0.11f, r * 0.11f, seed ^ 0x51f15e5du) * 0.42f, 0.f, 1.f);
        const float latitude = std::abs(ny);
        c.temperature = std::clamp(1.f - latitude * 0.92f - std::max(0.f, c.elevation - sea) * 0.48f +
                                       fbm(q * 0.06f, r * 0.06f, seed ^ 0xa53a9d1bu) * 0.1f, 0.f, 1.f);
        classify(c, sea);
    }

    std::mt19937 rng(seed ? seed : 1u);
    std::vector<int> riverSources;
    for (int river = 0; river < riverCount; ++river) {
        std::vector<uint8_t> visited(size_t(width * height), 0);
        int current = -1;
        int lastEdge = -1;
        float bestSourceScore = -std::numeric_limits<float>::max();
        for (int attempt = 0; attempt < width * height; ++attempt) {
            const int candidate = int(rng() % uint32_t(width * height));
            if (cells[size_t(candidate)].elevation <= sea + 0.34f) continue;
            float sourceDistance = float(width + height);
            const int candidateQ = candidate % width;
            const int candidateR = candidate / width;
            for (int source : riverSources) {
                const int dq = candidateQ - source % width;
                const int dr = candidateR - source / width;
                sourceDistance = std::min(sourceDistance, std::sqrt(float(dq * dq + dr * dr)));
            }
            const float sourceScore = cells[size_t(candidate)].elevation + sourceDistance * 0.014f;
            if (sourceScore > bestSourceScore) {
                bestSourceScore = sourceScore;
                current = candidate;
            }
        }
        if (current >= 0) riverSources.push_back(current);
        for (int step = 0; current >= 0 && step < width + height; ++step) {
            Cell& c = cells[size_t(current)];
            if (c.elevation <= sea + 0.02f) break;
            visited[size_t(current)] = 1;
            c.river = std::max(c.river, std::min(0.95f, 0.35f + float(step) * 0.025f));
            c.secondary = River;
            int next = -1;
            int nextEdge = -1;
            float bestScore = std::numeric_limits<float>::max();
            const int q = current % width, r = current / width;
            const auto ns = neighbours(q, r, width, height);
            for (int edge = 0; edge < 6; ++edge) {
                const int n = ns[size_t(edge)];
                if (n >= 0 && !visited[size_t(n)]) {
                    float score = cells[size_t(n)].elevation;
                    const float routeNoise = float(hash(seed ^ uint32_t(current) * 0x9e3779b9u ^
                                                       uint32_t(edge) * 0x85ebca6bu) & 0xffffu) /
                                                 65535.f;
                    score += (routeNoise - 0.5f) * 0.055f;
                    if (lastEdge >= 0) {
                        const int delta = (edge - lastEdge + 6) % 6;
                        const int turnSign = ((step / 3 + river) & 1) == 0 ? 1 : -1;
                        const int preferredEdge = (lastEdge + turnSign + 6) % 6;
                        if (edge == lastEdge) score += 0.045f;
                        if (delta == 3) score += 0.30f;
                        if (edge == preferredEdge) score -= 0.060f;
                    }
                    if (cells[size_t(n)].river > 0.f) score -= 0.018f;
                    if (score >= bestScore) continue;
                    bestScore = score;
                    next = n;
                    nextEdge = edge;
                }
            }
            if (next < 0) break;
            if (cells[size_t(next)].elevation >= c.elevation + 0.075f && step >= 5 &&
                c.elevation > sea + 0.10f) {
                c.lake = true;
                c.biome = Ocean;
                c.secondary = River;
                c.river = std::max(c.river, 0.72f);
                break;
            }
            if (cells[size_t(next)].elevation >= c.elevation)
                cells[size_t(next)].elevation = std::max(sea + 0.01f, c.elevation - 0.018f);
            c.riverEdges |= uint8_t(1u << uint32_t(nextEdge));
            cells[size_t(next)].riverEdges |= uint8_t(1u << uint32_t((nextEdge + 3) % 6));
            current = next;
            lastEdge = nextEdge;
        }
    }

    for (int r = 0; r < height; ++r) for (int q = 0; q < width; ++q) {
        Cell& c = cells[size_t(q + r * width)];
        const auto ns = neighbours(q, r, width, height);
        float bestDifference = 0.f;
        for (int n : ns) if (n >= 0 && cells[size_t(n)].biome != c.biome) {
            const float difference = std::abs(c.elevation - cells[size_t(n)].elevation);
            if (difference >= bestDifference) {
                bestDifference = difference;
                c.secondary = cells[size_t(n)].biome;
                c.blend = std::clamp(0.18f + (1.f - difference) * 0.32f, 0.f, 0.49f);
            }
        }
        if (c.river > 0.f) c.secondary = River;
    }

    out.clear();
    out.reserve(width * height * 18, width * height * 30);
    std::array<int, 12> biomeCounts{};
    int cliffEdgeCount = 0;
    int riverCellCount = 0;
    int lakeCellCount = 0;
    const float xStep = radius * 1.5f, zStep = radius * 1.7320508076f;
    for (int r = 0; r < height; ++r) for (int q = 0; q < width; ++q) {
        const int idx = q + r * width;
        const Cell& c = cells[size_t(idx)];
        const float cx = float(q) * xStep;
        const float cz = (float(r) + float(q & 1) * 0.5f) * zStep;
        const float y = surfaceHeight(c, sea, heightScale);
        const auto ns = neighbours(q, r, width, height);
        ++biomeCounts[size_t(std::clamp(c.biome, 0, 11))];
        if (c.riverEdges != 0) ++riverCellCount;
        if (c.lake) ++lakeCellCount;
        addTop(out, cx, cz, y, radius, c);
        if (c.riverEdges != 0) {
            addRiverJunction(out, cx, y + radius * 0.013f, cz, radius, c.river);
            for (int edge = 0; edge < 6; ++edge)
                if ((c.riverEdges & uint8_t(1u << uint32_t(edge))) != 0)
                {
                    const int neighbourIndex = ns[size_t(edge)];
                    const int safeNeighbour = neighbourIndex >= 0 ? neighbourIndex : idx;
                    const uint32_t lo = uint32_t(std::min(idx, safeNeighbour));
                    const uint32_t hi = uint32_t(std::max(idx, safeNeighbour));
                    addRiverArm(out, cx, y + radius * 0.012f, cz, radius, edge, c.river,
                                hash(seed ^ lo * 0x27d4eb2du ^ hi * 0x85ebca6bu));
                }
        }
        for (int edge = 0; edge < 6; ++edge) {
            float neighbourY = -sea * heightScale;
            if (ns[size_t(edge)] >= 0) {
                const Cell& nc = cells[size_t(ns[size_t(edge)])];
                neighbourY = surfaceHeight(nc, sea, heightScale);
            }
            if (y > neighbourY + 0.025f) {
                addCliff(out, cx, cz, radius, edge, y, neighbourY, c,
                         hash(seed ^ uint32_t(idx) * 0x9e3779b9u ^ uint32_t(edge)));
                ++cliffEdgeCount;
            }

            if (c.biome == Coast && ns[size_t(edge)] >= 0 &&
                cells[size_t(ns[size_t(edge)])].biome <= Ocean)
                addShoreBand(out, cx, y + radius * 0.014f, cz, radius, edge);
        }

        if (decorations && (c.biome == Forest || c.biome == Rainforest || c.biome == Swamp)) {
            const uint32_t cellSeed = hash(seed ^ uint32_t(idx) * 0x9e3779b9u);
            const int baseCount = c.biome == Rainforest ? 4 : (c.biome == Forest ? 3 : 2);
            const int count = int(std::round(float(baseCount) * vegetationDensity));
            for (int tree = 0; tree < count; ++tree) {
                const uint32_t h = hash(cellSeed + uint32_t(tree) * 0x85ebca6bu);
                const float a = float(h & 0xffffu) / 65535.f * 6.2831853072f;
                const float d = (0.16f + float((h >> 16) & 0xffu) / 255.f * 0.36f) * radius;
                const float variation = 0.82f + float((h >> 24) & 0xffu) / 255.f * 0.38f;
                const float scale = radius * variation * (c.biome == Swamp ? 0.75f : 1.25f);
                const int treeBiome = c.biome == Swamp ? Forest : c.biome;
                const bool broadleaf = c.biome != Forest || ((h >> 12) & 1u) != 0;
                addTree(out, cx + std::cos(a) * d, y, cz + std::sin(a) * d, scale,
                        treeBiome, broadleaf);
            }
        } else if (decorations && c.biome == Mountain) {
            const uint32_t mountainSeed = hash(seed ^ uint32_t(idx) * 0x27d4eb2du);
            const float mainHeight = radius *
                (1.20f + float(mountainSeed & 0xffu) / 255.f * 0.58f);
            const float mainRadius = radius *
                (0.48f + float((mountainSeed >> 8) & 0xffu) / 255.f * 0.12f);
            const float angle = float((mountainSeed >> 16) & 0xffffu) / 65535.f *
                                6.2831853072f;
            addCone(out, cx - std::cos(angle) * radius * 0.08f, y - radius * 0.04f,
                    cz - std::sin(angle) * radius * 0.08f, mainRadius, mainHeight,
                    Mountain, 8, mountainSeed);
            addCone(out, cx + std::cos(angle) * radius * 0.28f, y - radius * 0.025f,
                    cz + std::sin(angle) * radius * 0.28f, radius * 0.31f,
                    mainHeight * 0.64f, Mountain, 7, mountainSeed ^ 0x68bc21ebu);
            if (((mountainSeed >> 30) & 1u) != 0)
                addCone(out, cx + std::cos(angle + 2.1f) * radius * 0.25f,
                        y - radius * 0.02f,
                        cz + std::sin(angle + 2.1f) * radius * 0.25f,
                        radius * 0.24f, mainHeight * 0.48f, Mountain, 6,
                        mountainSeed ^ 0x02e5be93u);
        }
    }
    out.setMeta("algorithm", "mesh.hexterrain");
    out.setMeta("uvEncoding", "u=primaryBiome+blend,v=secondaryBiome+riverCoverage");
    out.setMeta("biomeCount", "12");
    out.setMeta("shoreGeometry", "edge-bands");
    out.setMeta("hydrology", "drainage-rivers+basin-lakes+confluences");
    out.setMeta("riverGeometry", "seeded-quadratic-ribbons");
    out.setMeta("cliffGeometry", "seeded-segmented-rock-walls");
    out.setMeta("mountainGeometry", "seeded-offset-three-ring-peaks");
    static constexpr const char* biomeNames[10] = {
        "deepOcean", "ocean", "coast", "grassland", "hills",
        "mountain", "forest", "swamp", "rainforest", "ice",
    };
    for (int biome = 0; biome < 10; ++biome)
        out.setMeta(std::string("cells.") + biomeNames[biome],
                    std::to_string(biomeCounts[size_t(biome)]));
    out.setMeta("cells.river", std::to_string(riverCellCount));
    out.setMeta("cells.lake", std::to_string(lakeCellCount));
    out.setMeta("edges.cliff", std::to_string(cliffEdgeCount));
    return !out.empty();
}

}  // namespace eve::procgen
