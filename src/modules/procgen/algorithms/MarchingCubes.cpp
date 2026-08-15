#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/algorithms/TreeMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

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

}  // namespace

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
                            "' (use sphere|torus|noise|terrain)";
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
    recipes_[id] = std::move(fn);
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
    return it->second(params, out, error);
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
    registerRecipe("mesh.marchingcubes", generateMarchingCubesMesh);
    registerRecipe("mesh.tree", generateTreeMesh);
    builtinsRegistered_ = true;
}

}  // namespace eve::procgen
