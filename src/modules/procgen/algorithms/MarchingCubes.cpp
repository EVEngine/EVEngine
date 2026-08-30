#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/algorithms/HexTerrain.h"
#include "procgen/algorithms/PrototypeKit.h"
#include "procgen/algorithms/RockMesh.h"
#include "procgen/algorithms/SkyscraperMesh.h"
#include "procgen/algorithms/TreeMesh.h"
#include "procgen/algorithms/BushMesh.h"
#include "procgen/algorithms/LinearStructure.h"
#include "procgen/algorithms/LSystemMesh.h"
#include "procgen/urban/UrbanOutput.h"
#include "procgen/algorithms/CastleMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>

namespace eve::procgen {
namespace {

#include "procgen/algorithms/MarchingCubesTables.inc"

using eve::procgen::mc_tables::kEdgeTable;
using eve::procgen::mc_tables::kTriTable;

// Edge endpoints for the 12 cube edges (corner index pairs).
constexpr int kEdgeCorners[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
};

// Corner offsets in unit cube (x,y,z).
constexpr float kCornerOffset[8][3] = {
    {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1},
};

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline void normalize3(float &x, float &y, float &z) {
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len > 1e-8f) {
        x /= len;
        y /= len;
        z /= len;
    } else {
        x = 0.f;
        y = 1.f;
        z = 0.f;
    }
}

inline float fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }

inline float grad3(uint32_t h, float x, float y, float z) {
    const uint32_t g = h & 15u;
    const float u = g < 8 ? x : y;
    const float v = g < 4 ? y : (g == 12 || g == 14 ? x : z);
    return ((g & 1u) ? -u : u) + ((g & 2u) ? -v : v);
}

float valueNoise3(float x, float y, float z, uint32_t seed) {
    const int xi = int(std::floor(x));
    const int yi = int(std::floor(y));
    const int zi = int(std::floor(z));
    const float xf = x - float(xi);
    const float yf = y - float(yi);
    const float zf = z - float(zi);
    const float u = fade(xf);
    const float v = fade(yf);
    const float w = fade(zf);

    auto h = [&](int ix, int iy, int iz) -> uint32_t {
        return uint32_t(ix) * 374761393u + uint32_t(iy) * 668265263u + uint32_t(iz) * 1274126177u +
               seed * 2246822519u;
    };

    const float n000 = grad3(h(xi, yi, zi), xf, yf, zf);
    const float n100 = grad3(h(xi + 1, yi, zi), xf - 1, yf, zf);
    const float n010 = grad3(h(xi, yi + 1, zi), xf, yf - 1, zf);
    const float n110 = grad3(h(xi + 1, yi + 1, zi), xf - 1, yf - 1, zf);
    const float n001 = grad3(h(xi, yi, zi + 1), xf, yf, zf - 1);
    const float n101 = grad3(h(xi + 1, yi, zi + 1), xf - 1, yf, zf - 1);
    const float n011 = grad3(h(xi, yi + 1, zi + 1), xf, yf - 1, zf - 1);
    const float n111 = grad3(h(xi + 1, yi + 1, zi + 1), xf - 1, yf - 1, zf - 1);

    const float x00 = lerp(n000, n100, u);
    const float x10 = lerp(n010, n110, u);
    const float x01 = lerp(n001, n101, u);
    const float x11 = lerp(n011, n111, u);
    const float y0 = lerp(x00, x10, v);
    const float y1 = lerp(x01, x11, v);
    return lerp(y0, y1, w);  // roughly [-1,1]
}

float fbm3(float x, float y, float z, uint32_t seed, int octaves) {
    float sum = 0.f;
    float amp = 0.5f;
    float freq = 1.f;
    float norm = 0.f;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise3(x * freq, y * freq, z * freq, seed + uint32_t(i) * 1013u) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.f;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 mul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 normalized(Vec3 v) {
    const float len = std::sqrt(dot(v, v));
    return len > 1e-8f ? mul(v, 1.f / len) : Vec3{0.f, 1.f, 0.f};
}

struct Triangle {
    uint32_t a, b, c;
};

uint64_t edgeKey(uint32_t a, uint32_t b) {
    if (a > b) std::swap(a, b);
    return (uint64_t(a) << 32u) | uint64_t(b);
}

uint32_t midpoint(uint32_t a, uint32_t b, std::vector<Vec3> &vertices,
                  std::map<uint64_t, uint32_t> &cache) {
    const uint64_t key = edgeKey(a, b);
    const auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    const uint32_t index = uint32_t(vertices.size());
    vertices.push_back(normalized(add(vertices[a], vertices[b])));
    cache.emplace(key, index);
    return index;
}

void addPlanetVertex(MeshBuild &out, Vec3 p, float radius) {
    p = normalized(p);
    constexpr float kPi = 3.14159265358979323846f;
    const float u = std::atan2(p.z, p.x) / (2.f * kPi) + 0.5f;
    const float v = std::asin(std::clamp(p.y, -1.f, 1.f)) / kPi + 0.5f;
    out.addVertex(p.x * radius, p.y * radius, p.z * radius, p.x, p.y, p.z, u, v);
}

}  // namespace

bool generateHexPlanetMesh(const Params &params, MeshBuild &out, std::string &error) {
    const int subdivisions = params.getInt("subdivisions", 2);
    const float radius = params.getFloat("radius", 1.f);
    const float inset = params.getFloat("tileInset", 0.06f);
    if (subdivisions < 0 || subdivisions > 7) {
        error = "mesh.hexplanet: subdivisions must be in [0, 7]";
        return false;
    }
    if (!(radius > 0.f)) {
        error = "mesh.hexplanet: radius must be positive";
        return false;
    }
    if (inset < 0.f || inset >= 0.5f) {
        error = "mesh.hexplanet: tileInset must be in [0, 0.5)";
        return false;
    }

    const float phi = (1.f + std::sqrt(5.f)) * 0.5f;
    std::vector<Vec3> vertices = {
        {-1, phi, 0}, {1, phi, 0}, {-1, -phi, 0}, {1, -phi, 0},
        {0, -1, phi}, {0, 1, phi}, {0, -1, -phi}, {0, 1, -phi},
        {phi, 0, -1}, {phi, 0, 1}, {-phi, 0, -1}, {-phi, 0, 1},
    };
    for (Vec3 &v : vertices) v = normalized(v);
    std::vector<Triangle> faces = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
    };
    for (int level = 0; level < subdivisions; ++level) {
        std::map<uint64_t, uint32_t> cache;
        std::vector<Triangle> next;
        next.reserve(faces.size() * 4u);
        for (const Triangle &f : faces) {
            const uint32_t ab = midpoint(f.a, f.b, vertices, cache);
            const uint32_t bc = midpoint(f.b, f.c, vertices, cache);
            const uint32_t ca = midpoint(f.c, f.a, vertices, cache);
            next.insert(next.end(), {{f.a, ab, ca}, {f.b, bc, ab}, {f.c, ca, bc}, {ab, bc, ca}});
        }
        faces.swap(next);
    }

    std::vector<std::vector<uint32_t>> incident(vertices.size());
    std::vector<Vec3> faceCenters;
    faceCenters.reserve(faces.size());
    for (uint32_t i = 0; i < faces.size(); ++i) {
        const Triangle &f = faces[i];
        faceCenters.push_back(normalized(add(add(vertices[f.a], vertices[f.b]), vertices[f.c])));
        incident[f.a].push_back(i);
        incident[f.b].push_back(i);
        incident[f.c].push_back(i);
    }

    out.clear();
    int pentagons = 0, hexagons = 0;
    for (uint32_t cell = 0; cell < vertices.size(); ++cell) {
        const Vec3 center = vertices[cell];
        const Vec3 reference = std::fabs(center.y) < 0.9f ? normalized(cross({0, 1, 0}, center))
                                                            : normalized(cross({1, 0, 0}, center));
        const Vec3 tangent = cross(center, reference);
        auto &ring = incident[cell];
        std::sort(ring.begin(), ring.end(), [&](uint32_t lhs, uint32_t rhs) {
            const Vec3 a = sub(faceCenters[lhs], mul(center, dot(faceCenters[lhs], center)));
            const Vec3 b = sub(faceCenters[rhs], mul(center, dot(faceCenters[rhs], center)));
            return std::atan2(dot(a, tangent), dot(a, reference)) <
                   std::atan2(dot(b, tangent), dot(b, reference));
        });
        if (ring.size() == 5) ++pentagons;
        else if (ring.size() == 6) ++hexagons;

        const uint32_t base = uint32_t(out.getVertexCount());
        addPlanetVertex(out, center, radius);
        for (uint32_t faceIndex : ring) {
            // Pull dual corners toward this cell's center. Re-normalizing keeps every tile spherical.
            addPlanetVertex(out, normalized(add(mul(faceCenters[faceIndex], 1.f - inset),
                                               mul(center, inset))), radius);
        }
        for (uint32_t i = 0; i < ring.size(); ++i) {
            out.addTriangle(base, base + 1u + i, base + 1u + (i + 1u) % uint32_t(ring.size()));
        }
    }
    out.setMeta("algorithm", "mesh.hexplanet");
    out.setMeta("cells", std::to_string(vertices.size()));
    out.setMeta("pentagons", std::to_string(pentagons));
    out.setMeta("hexagons", std::to_string(hexagons));
    out.setMeta("subdivisions", std::to_string(subdivisions));
    return true;
}

bool marchingCubes(const float *density, int nx, int ny, int nz, float isolevel, MeshBuild &out,
                   std::string *error) {
    if (!density) {
        if (error) *error = "marchingCubes: null density";
        return false;
    }
    if (nx < 2 || ny < 2 || nz < 2) {
        if (error) *error = "marchingCubes: volume must be at least 2x2x2";
        return false;
    }

    out.clear();
    out.reserve((nx * ny * nz) / 2, (nx * ny * nz) * 3);

    auto at = [&](int x, int y, int z) -> float {
        return density[size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny)];
    };

    // World-space mapping: unit cube centered at origin spanning [-0.5, 0.5]^3.
    const float sx = 1.f / float(nx - 1);
    const float sy = 1.f / float(ny - 1);
    const float sz = 1.f / float(nz - 1);

    for (int z = 0; z < nz - 1; ++z) {
        for (int y = 0; y < ny - 1; ++y) {
            for (int x = 0; x < nx - 1; ++x) {
                float val[8];
                int cubeIndex = 0;
                for (int i = 0; i < 8; ++i) {
                    const int cx = x + int(kCornerOffset[i][0]);
                    const int cy = y + int(kCornerOffset[i][1]);
                    const int cz = z + int(kCornerOffset[i][2]);
                    val[i] = at(cx, cy, cz);
                    if (val[i] < isolevel) cubeIndex |= (1 << i);
                }
                const int edges = kEdgeTable[cubeIndex];
                if (edges == 0) continue;

                float vertList[12][3];
                for (int e = 0; e < 12; ++e) {
                    if (!(edges & (1 << e))) continue;
                    const int a = kEdgeCorners[e][0];
                    const int b = kEdgeCorners[e][1];
                    const float va = val[a];
                    const float vb = val[b];
                    float t = (isolevel - va) / (vb - va + 1e-12f);
                    t = std::clamp(t, 0.f, 1.f);
                    const float px =
                        (float(x) + lerp(kCornerOffset[a][0], kCornerOffset[b][0], t)) * sx - 0.5f;
                    const float py =
                        (float(y) + lerp(kCornerOffset[a][1], kCornerOffset[b][1], t)) * sy - 0.5f;
                    const float pz =
                        (float(z) + lerp(kCornerOffset[a][2], kCornerOffset[b][2], t)) * sz - 0.5f;
                    vertList[e][0] = px;
                    vertList[e][1] = py;
                    vertList[e][2] = pz;
                }

                for (int i = 0; kTriTable[cubeIndex][i] != -1; i += 3) {
                    const int e0 = kTriTable[cubeIndex][i];
                    const int e1 = kTriTable[cubeIndex][i + 1];
                    const int e2 = kTriTable[cubeIndex][i + 2];
                    const float *p0 = vertList[e0];
                    const float *p1 = vertList[e1];
                    const float *p2 = vertList[e2];

                    float ax = p1[0] - p0[0], ay = p1[1] - p0[1], az = p1[2] - p0[2];
                    float bx = p2[0] - p0[0], by = p2[1] - p0[1], bz = p2[2] - p0[2];
                    float nxn = ay * bz - az * by;
                    float nyn = az * bx - ax * bz;
                    float nzn = ax * by - ay * bx;
                    normalize3(nxn, nyn, nzn);

                    // Flip so normals point toward empty (lower density / outside).
                    // With cubeIndex bits for val < isolevel, winding already tends outward.
                    const uint32_t base = uint32_t(out.getVertexCount());
                    const float u0 = p0[0] + 0.5f, v0 = p0[1] + 0.5f;
                    const float u1 = p1[0] + 0.5f, v1 = p1[1] + 0.5f;
                    const float u2 = p2[0] + 0.5f, v2 = p2[1] + 0.5f;
                    out.addVertex(p0[0], p0[1], p0[2], nxn, nyn, nzn, u0, v0);
                    out.addVertex(p1[0], p1[1], p1[2], nxn, nyn, nzn, u1, v1);
                    out.addVertex(p2[0], p2[1], p2[2], nxn, nyn, nzn, u2, v2);
                    out.addTriangle(base, base + 1, base + 2);
                }
            }
        }
    }

    out.setMeta("algorithm", "mesh.marchingcubes");
    return true;
}

bool fillDensityField(const Params &params, std::vector<float> &density, int &nx, int &ny, int &nz,
                      std::string &error) {
    const int res = params.getInt("resolution",
                                  params.getWidth() > 0 ? params.getWidth() : 24);
    nx = params.getInt("nx", res);
    ny = params.getInt("ny", params.getHeight() > 0 ? params.getHeight() : res);
    nz = params.getInt("nz", params.getInt("depth", res));
    if (nx < 2 || ny < 2 || nz < 2) {
        error = "mesh.marchingcubes: resolution must be at least 2 in each axis";
        return false;
    }
    if (nx > 128 || ny > 128 || nz > 128) {
        error = "mesh.marchingcubes: resolution capped at 128 per axis";
        return false;
    }

    const std::string field = params.getString("field", "sphere");
    const float scale = params.getFloat("scale", 1.f);
    const int octaves = std::max(1, params.getInt("octaves", 3));
    const uint32_t seed = params.getSeed();

    density.assign(size_t(nx) * size_t(ny) * size_t(nz), 0.f);
    for (int z = 0; z < nz; ++z) {
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                const float px = (float(x) / float(nx - 1) - 0.5f) * 2.f;
                const float py = (float(y) / float(ny - 1) - 0.5f) * 2.f;
                const float pz = (float(z) / float(nz - 1) - 0.5f) * 2.f;
                float d = 0.f;
                if (field == "sphere") {
                    const float r = params.getFloat("radius", 0.7f);
                    d = r - std::sqrt(px * px + py * py + pz * pz);
                } else if (field == "rock") {
                    // An ellipsoid SDF whose radius is displaced by low-frequency strata and
                    // higher-frequency erosion.  Quantising the direction before sampling the
                    // strata creates broad, natural fracture planes instead of a noisy sphere.
                    const float radius = params.getFloat("radius", 0.68f);
                    const float flattening =
                        std::clamp(params.getFloat("flattening", 0.22f), 0.f, 0.7f);
                    const float angularity =
                        std::clamp(params.getFloat("angularity", 0.35f), 0.f, 1.f);
                    const float erosion =
                        std::clamp(params.getFloat("erosion", 0.18f), 0.f, 0.45f);
                    const float detailScale = std::max(0.25f, scale);
                    const float sy = std::max(0.3f, 1.f - flattening);
                    const float ex = px;
                    const float ey = py / sy;
                    const float ez = pz;
                    const float len = std::sqrt(ex * ex + ey * ey + ez * ez);
                    const float invLen = len > 1e-5f ? 1.f / len : 0.f;
                    const float steps = 3.f + angularity * 9.f;
                    const float qx = std::round(ex * invLen * steps) / steps;
                    const float qy = std::round(ey * invLen * steps) / steps;
                    const float qz = std::round(ez * invLen * steps) / steps;
                    const float strata = fbm3((qx + 2.3f) * detailScale,
                                              (qy + 4.7f) * detailScale,
                                              (qz + 8.1f) * detailScale, seed, octaves);
                    const float pits = fbm3((px + 7.2f) * detailScale * 2.7f,
                                            (py + 1.9f) * detailScale * 2.7f,
                                            (pz + 5.4f) * detailScale * 2.7f,
                                            seed + 7919u, std::max(2, octaves - 1));
                    const float displacement = strata * (0.08f + angularity * 0.16f) -
                                               std::max(0.f, pits) * erosion;
                    d = radius + displacement - len;
                } else if (field == "torus") {
                    const float R = params.getFloat("majorRadius", 0.55f);
                    const float r = params.getFloat("minorRadius", 0.22f);
                    const float q = std::sqrt(px * px + pz * pz) - R;
                    d = r - std::sqrt(q * q + py * py);
                } else if (field == "terrain") {
                    const float h =
                        fbm3(px * scale + 3.1f, 0.f, pz * scale + 1.7f, seed, octaves) * 0.45f;
                    d = h - py;
                } else if (field == "noise") {
                    const float n =
                        fbm3(px * scale + 2.f, py * scale + 5.f, pz * scale + 9.f, seed, octaves);
                    d = n - params.getFloat("threshold", 0.05f);
                } else {
                    error = "mesh.marchingcubes: unknown field '" + field +
                            "' (use sphere|rock|torus|noise|terrain)";
                    return false;
                }
                // Soft boundary falloff so surfaces close.
                const float margin = 0.92f;
                const float bx = std::max(0.f, std::fabs(px) - margin);
                const float by = std::max(0.f, std::fabs(py) - margin);
                const float bz = std::max(0.f, std::fabs(pz) - margin);
                d -= (bx * bx + by * by + bz * bz) * 4.f;
                density[size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny)] =
                    d;
            }
        }
    }
    return true;
}

bool generateMarchingCubesMesh(const Params &params, MeshBuild &out, std::string &error) {
    std::vector<float> density;
    int nx = 0, ny = 0, nz = 0;
    if (!fillDensityField(params, density, nx, ny, nz, error)) return false;
    const float isolevel = params.getFloat("isolevel", 0.f);
    if (!marchingCubes(density.data(), nx, ny, nz, isolevel, out, &error)) return false;
    out.setMeta("field", params.getString("field", "sphere"));
    if (out.empty()) {
        error = "mesh.marchingcubes: empty mesh (adjust field/isolevel/resolution)";
        return false;
    }
    return true;
}

MeshRecipeRegistry &MeshRecipeRegistry::instance() {
    static MeshRecipeRegistry reg;
    return reg;
}

void MeshRecipeRegistry::registerRecipe(const std::string &id, MeshRecipeFn fn) {
    RecipeDescriptor descriptor;
    descriptor.id = id;
    descriptor.displayName = id;
    registerRecipe(std::move(descriptor), std::move(fn));
}

void MeshRecipeRegistry::registerRecipe(RecipeDescriptor descriptor, MeshRecipeFn fn) {
    const std::string id = descriptor.id;
    recipes_[id] = Entry{std::move(fn), std::move(descriptor)};
}

bool MeshRecipeRegistry::has(const std::string &id) const {
    return recipes_.find(id) != recipes_.end();
}

bool MeshRecipeRegistry::generate(const std::string &id, const Params &params, MeshBuild &out,
                                  std::string &error) const {
    auto it = recipes_.find(id);
    if (it == recipes_.end()) {
        error = "unknown mesh recipe '" + id + "'";
        return false;
    }
    return it->second.fn(params, out, error);
}

const RecipeDescriptor *MeshRecipeRegistry::descriptor(const std::string &id) const {
    const auto it = recipes_.find(id);
    return it == recipes_.end() ? nullptr : &it->second.descriptor;
}

bool MeshRecipeRegistry::applyDefaults(const std::string &id, Params &params) const {
    const RecipeDescriptor *schema = descriptor(id);
    if (!schema) return false;
    schema->applyDefaults(params);
    return true;
}

std::vector<std::string> MeshRecipeRegistry::list() const {
    std::vector<std::string> ids;
    ids.reserve(recipes_.size());
    for (const auto &kv : recipes_) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}

void MeshRecipeRegistry::registerBuiltins() {
    if (builtinsRegistered_) return;
    registerPrototypePieceRecipes(*this);
    auto mesh = [](std::string id, std::string name) {
        RecipeDescriptor schema{std::move(id), std::move(name), "Mesh", {}};
        schema.params.push_back(ParamDescriptor::integer("seed", "Seed", 1, 0, 2147483647));
        return schema;
    };
    auto addAdvanced = [](RecipeDescriptor &schema, ParamDescriptor param) {
        param.advanced = true;
        schema.params.push_back(std::move(param));
    };
    RecipeDescriptor marching = mesh("mesh.marchingcubes", "Marching Cubes");
    marching.params.push_back(ParamDescriptor::choice("field", "Density Field", "sphere",
                                                      {"sphere", "rock", "terrain", "torus", "noise"}));
    marching.params.push_back(ParamDescriptor::integer("resolution", "Resolution", 24, 4, 256));
    marching.params.push_back(ParamDescriptor::floating("isolevel", "Iso Level", 0.f, -2.f, 2.f, 0.01f));
    marching.params.push_back(ParamDescriptor::floating("scale", "Noise Scale", 1.f, 0.01f, 128.f, 0.01f));
    marching.params.push_back(ParamDescriptor::integer("octaves", "Octaves", 3, 1, 12));
    addAdvanced(marching, ParamDescriptor::integer("nx", "X Resolution", 24, 2, 512));
    addAdvanced(marching, ParamDescriptor::integer("ny", "Y Resolution", 24, 2, 512));
    addAdvanced(marching, ParamDescriptor::integer("nz", "Z Resolution", 24, 2, 512));
    addAdvanced(marching,
                ParamDescriptor::floating("radius", "Field Radius", 0.7f, 0.01f, 10.f, 0.01f));
    addAdvanced(marching, ParamDescriptor::floating("flattening", "Rock Flattening", 0.22f,
                                                     0.f, 0.7f, 0.01f));
    addAdvanced(marching, ParamDescriptor::floating("angularity", "Rock Angularity", 0.35f,
                                                     0.f, 1.f, 0.01f));
    addAdvanced(marching,
                ParamDescriptor::floating("erosion", "Rock Erosion", 0.18f, 0.f, 0.45f, 0.01f));
    addAdvanced(marching, ParamDescriptor::floating("majorRadius", "Torus Major Radius", 0.55f,
                                                     0.01f, 10.f, 0.01f));
    addAdvanced(marching, ParamDescriptor::floating("minorRadius", "Torus Minor Radius", 0.22f,
                                                     0.01f, 10.f, 0.01f));
    addAdvanced(marching, ParamDescriptor::floating("threshold", "Noise Threshold", 0.05f,
                                                     -1.f, 1.f, 0.01f));
    registerRecipe(std::move(marching), generateMarchingCubesMesh);

    RecipeDescriptor rock = mesh("mesh.rock", "Rock");
    rock.params.push_back(ParamDescriptor::choice("baseShape", "Base Shape", "mixed",
                                                  {"mixed", "round", "flat", "tall", "angular"}));
    rock.params.push_back(ParamDescriptor::integer("subdivisions", "Subdivisions", 3, 0, 6));
    rock.params.push_back(ParamDescriptor::floating("radius", "Radius", 0.72f, 0.05f, 32.f, 0.01f));
    rock.params.push_back(ParamDescriptor::floating("scale", "Scale", 2.4f, 0.25f, 64.f, 0.05f));
    rock.params.push_back(ParamDescriptor::floating("variation", "Variation", 0.42f, 0.f, 1.f, 0.01f));
    rock.params.push_back(ParamDescriptor::floating("angularity", "Angularity", 0.38f, 0.f, 1.f, 0.01f));
    rock.params.push_back(ParamDescriptor::floating("erosion", "Erosion", 0.16f, 0.f, 0.45f, 0.01f));
    addAdvanced(rock,
                ParamDescriptor::floating("flattening", "Flattening", 0.22f, 0.f, 0.7f, 0.01f));
    addAdvanced(rock, ParamDescriptor::integer("octaves", "Octaves", 4, 1, 8));
    registerRecipe(std::move(rock), generateRockMesh);

    RecipeDescriptor planet = mesh("mesh.hexplanet", "Hex Planet");
    planet.params.push_back(ParamDescriptor::floating("radius", "Radius", 1.f, 0.01f, 1000.f, 0.01f));
    planet.params.push_back(ParamDescriptor::integer("subdivisions", "Subdivisions", 2, 0, 7));
    planet.params.push_back(ParamDescriptor::floating("tileInset", "Tile Inset", 0.06f, 0.f, 0.49f, 0.01f));
    registerRecipe(std::move(planet), generateHexPlanetMesh);

    RecipeDescriptor terrain = mesh("mesh.hexterrain", "Hex Terrain World");
    terrain.params.push_back(ParamDescriptor::integer("width", "Width", 32, 2, 256));
    terrain.params.push_back(ParamDescriptor::integer("height", "Height", 24, 2, 256));
    terrain.params.push_back(ParamDescriptor::floating("radius", "Hex Radius", 1.f, 0.05f, 64.f, 0.05f));
    terrain.params.push_back(ParamDescriptor::integer("seed", "Seed", 1, 1, 2147483647));
    terrain.params.push_back(ParamDescriptor::floating("seaLevel", "Sea Level", 0.43f, 0.f, 1.f, 0.01f));
    terrain.params.push_back(ParamDescriptor::floating("heightScale", "Height Scale", 4.f, 0.05f, 128.f, 0.05f));
    terrain.params.push_back(ParamDescriptor::integer("riverCount", "River Count", 8, 0, 128));
    terrain.params.push_back(ParamDescriptor::boolean("decorations", "Terrain Decorations", true));
    terrain.params.push_back(ParamDescriptor::floating("vegetationDensity", "Vegetation Density", 1.f, 0.f, 2.f, 0.05f));
    registerRecipe(std::move(terrain), generateHexTerrainMesh);

    RecipeDescriptor tree = mesh("mesh.tree", "Tree");
    tree.params.push_back(ParamDescriptor::choice("style", "Style", "lowpoly", {"lowpoly", "realistic"}));
    tree.params.push_back(ParamDescriptor::choice("leafMode", "Leaf Mode", "cards", {"cards", "clusters", "none"}));
    tree.params.push_back(ParamDescriptor::choice("branchAlgorithm", "Branch Algorithm", "weberPenn",
                                                  {"weberPenn", "colonization"}));
    tree.params.push_back(ParamDescriptor::floating("height", "Height", 6.f, 0.5f, 100.f, 0.1f));
    tree.params.push_back(ParamDescriptor::floating("trunkRadius", "Trunk Radius", 0.33f, 0.02f, 10.f, 0.01f));
    tree.params.push_back(ParamDescriptor::floating("crownRadius", "Crown Radius", 2.04f, 0.1f, 50.f, 0.05f));
    tree.params.push_back(ParamDescriptor::floating("leafDensity", "Leaf Density", 0.65f, 0.f, 1.f, 0.01f));
    tree.params.push_back(ParamDescriptor::integer("branchLevels", "Branch Levels", 2, 1, 5));
    tree.params.push_back(ParamDescriptor::integer("branchCount", "Branch Count", 6, 2, 20));
    addAdvanced(tree,
                ParamDescriptor::floating("leafSize", "Leaf Size", 0.45f, 0.02f, 10.f, 0.01f));
    addAdvanced(tree, ParamDescriptor::floating("foliageStart", "Foliage Start", 0.35f,
                                                 0.1f, 0.9f, 0.01f));
    addAdvanced(tree, ParamDescriptor::integer("radialSegments", "Radial Segments", 6, 3, 24));
    addAdvanced(tree, ParamDescriptor::integer("curveSegments", "Curve Segments", 5, 2, 20));
    addAdvanced(tree,
                ParamDescriptor::floating("trunkCurve", "Trunk Curve", 0.1f, 0.f, 0.45f, 0.01f));
    addAdvanced(tree,
                ParamDescriptor::floating("curveBack", "Curve Back", 0.16f, -0.5f, 0.5f, 0.01f));
    addAdvanced(tree, ParamDescriptor::floating("branchCurve", "Branch Curve", 0.13f, 0.f,
                                                 0.5f, 0.01f));
    addAdvanced(tree, ParamDescriptor::floating("branchAngle", "Branch Angle", 62.f, 5.f, 88.f,
                                                 1.f));
    addAdvanced(tree, ParamDescriptor::floating("branchAngleVariation", "Angle Variation", 12.f,
                                                 0.f, 40.f, 1.f));
    addAdvanced(tree, ParamDescriptor::floating("phyllotaxis", "Phyllotaxis", 137.5f, 0.f,
                                                 360.f, 0.5f));
    addAdvanced(tree,
                ParamDescriptor::floating("tropism", "Tropism", 0.22f, -0.5f, 0.8f, 0.01f));
    addAdvanced(tree, ParamDescriptor::floating("droop", "Droop", 0.18f, 0.f, 0.8f, 0.01f));
    addAdvanced(tree, ParamDescriptor::floating("apicalDominance", "Apical Dominance", 0.62f,
                                                 0.f, 1.f, 0.01f));
    addAdvanced(tree, ParamDescriptor::integer("attractorCount", "Attractor Count", 80, 12, 1200));
    addAdvanced(tree,
                ParamDescriptor::integer("colonizationIterations", "Growth Iterations", 30, 4, 160));
    addAdvanced(tree, ParamDescriptor::floating("influenceRadius", "Influence Radius", 2.2032f,
                                                 0.05f, 100.f, 0.01f));
    addAdvanced(tree, ParamDescriptor::floating("killRadius", "Kill Radius", 0.2652f, 0.01f,
                                                 100.f, 0.01f));
    addAdvanced(tree,
                ParamDescriptor::floating("growthStep", "Growth Step", 0.2856f, 0.01f, 100.f, 0.01f));
    addAdvanced(tree, ParamDescriptor::floating("branchInertia", "Branch Inertia", 1.2f, 0.f,
                                                 4.f, 0.01f));
    addAdvanced(tree, ParamDescriptor::floating("maxTurnAngle", "Maximum Turn Angle", 22.f, 2.f,
                                                 60.f, 1.f));
    addAdvanced(tree, ParamDescriptor::floating("maxCumulativeAngle", "Maximum Crown Angle", 58.f,
                                                 10.f, 85.f, 1.f));
    addAdvanced(tree, ParamDescriptor::floating("branchLengthFalloff", "Length Falloff", 0.58f,
                                                 0.f, 0.9f, 0.01f));
    addAdvanced(tree, ParamDescriptor::floating("branchRadiusFalloff", "Radius Falloff", 0.5f,
                                                 0.f, 0.9f, 0.01f));
    addAdvanced(tree, ParamDescriptor::floating("lowerLeafCoverage", "Lower Leaf Coverage", 0.72f,
                                                 0.f, 1.f, 0.01f));
    addAdvanced(tree, ParamDescriptor::floating("upperLeafCoverage", "Upper Leaf Coverage", 0.18f,
                                                 0.f, 1.f, 0.01f));
    addAdvanced(tree, ParamDescriptor::integer("maxChildren", "Maximum Children", 2, 1, 4));
    registerRecipe(std::move(tree), generateTreeMesh);

    RecipeDescriptor bush = mesh("mesh.bush", "Bush");
    bush.params.push_back(ParamDescriptor::choice("style", "Style", "mound", {"mound", "upright", "wild"}));
    bush.params.push_back(ParamDescriptor::choice("leafMode", "Leaf Mode", "mixed", {"mixed", "cards", "blobs"}));
    bush.params.push_back(ParamDescriptor::floating("height", "Height", 1.4f, 0.3f, 30.f, 0.05f));
    bush.params.push_back(ParamDescriptor::floating("width", "Width", 2.2f, 0.4f, 30.f, 0.05f));
    bush.params.push_back(ParamDescriptor::integer("blobs", "Foliage Blobs", 9, 1, 40));
    bush.params.push_back(ParamDescriptor::floating("leafDensity", "Leaf Density", 0.62f, 0.f, 1.f, 0.01f));
    addAdvanced(bush, ParamDescriptor::integer("rings", "Rings", 3, 2, 10));
    addAdvanced(bush, ParamDescriptor::integer("radialSegments", "Radial Segments", 7, 4, 24));
    addAdvanced(bush,
                ParamDescriptor::floating("leafSize", "Leaf Size", 0.224f, 0.02f, 10.f, 0.01f));
    addAdvanced(bush, ParamDescriptor::integer("twigs", "Twigs", 4, 0, 16));
    addAdvanced(bush,
                ParamDescriptor::floating("twigLength", "Twig Length", 0.42f, 0.05f, 10.f, 0.01f));
    registerRecipe(std::move(bush), generateBushMesh);

    RecipeDescriptor tower = mesh("mesh.skyscraper", "Skyscraper");
    tower.params.push_back(ParamDescriptor::integer("tiers", "Tiers", 5, 1, 24));
    tower.params.push_back(ParamDescriptor::floating("baseWidth", "Base Width", 10.f, 0.5f, 500.f, 0.1f));
    tower.params.push_back(ParamDescriptor::floating("baseDepth", "Base Depth", 10.f, 0.5f, 500.f, 0.1f));
    tower.params.push_back(ParamDescriptor::floating("tierHeight", "Tier Height", 6.f, 0.5f, 100.f, 0.1f));
    tower.params.push_back(ParamDescriptor::floating("setback", "Setback", 0.08f, 0.f, 0.6f, 0.01f));
    tower.params.push_back(ParamDescriptor::integer("windowCols", "Window Columns", 6, 1, 24));
    tower.params.push_back(ParamDescriptor::integer("windowRows", "Window Rows", 4, 1, 24));
    addAdvanced(tower, ParamDescriptor::floating("windowDepth", "Window Depth", 0.04f, 0.f,
                                                  10.f, 0.01f));
    addAdvanced(tower, ParamDescriptor::floating("spireHeight", "Spire Height", 0.f, 0.f,
                                                  1000.f, 0.1f));
    registerRecipe(std::move(tower), generateSkyscraperMesh);
    registerLinearStructureRecipes(*this);
    registerLSystemRecipes(*this);
    urban::registerUrbanMeshRecipes(*this);
    registerCastleMeshRecipe(*this);
    builtinsRegistered_ = true;
}

}  // namespace eve::procgen
