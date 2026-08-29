#include "graphics/Grass.h"

#include "common/Exception.h"
#include "data/ByteData.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/TextureSampler.h"
#include "graphics/shaders/mesh3d_grass_frag_spv.inc"
#include "graphics/shaders/mesh3d_grass_vert_spv.inc"
#include "graphics/shaders/GrassWgsl.h"
#include "image/Image.h"
#include "image/ImageData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <utility>

namespace eve::graphics::grass {
namespace {

const std::array<const char *, 18> kParamSlots = {
    "time",        "frameDuration", "grassWidth",         "grassHeight", "alphaCutoff",
    "alwaysDark",  "lightGreenX",   "lightGreenY",        "lightGreenZ", "darkGreenX",
    "darkGreenY",  "darkGreenZ",    "frameCount",         "atlasCols",   "atlasRows",
    "grassVariantCount", "leafVariantCount", "leafRowOffset"};

std::vector<uint32_t> copySpv(const uint32_t *data, size_t count) {
    return std::vector<uint32_t>(data, data + count);
}

float radicalInverse(uint32_t n, uint32_t base) {
    const float inv = 1.f / float(base);
    float f = inv;
    float val = 0.f;
    while (n > 0) {
        val += float(n % base) * f;
        n /= base;
        f *= inv;
    }
    return val;
}

float wrap01(float x) {
    x = x - std::floor(x);
    return x < 0.f ? x + 1.f : x;
}

uint32_t mixSeed(uint32_t seed, uint32_t i) {
    seed ^= 0x9e3779b9u + (i << 6) + (i >> 2);
    seed *= 0x85ebca6bu;
    return seed ^ (seed >> 13);
}

struct Triangle {
    uint32_t i0 = 0, i1 = 0, i2 = 0;
    float area = 0.f;
    glm::vec3 n{0.f, 1.f, 0.f};
};

glm::vec3 readPos(const float *pos, int i) {
    return glm::vec3(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]);
}

glm::vec3 readNrm(const float *nrm, int i) {
    if (!nrm) return glm::vec3(0.f, 1.f, 0.f);
    return glm::vec3(nrm[i * 3], nrm[i * 3 + 1], nrm[i * 3 + 2]);
}

bool buildTriangles(const float *posXYZ, const float *nrmXYZ, int vertexCount,
                    const uint32_t *indices, int indexCount, float minSlopeDot,
                    std::vector<Triangle> &tris, std::vector<float> &cdf) {
    tris.clear();
    cdf.clear();
    if (!posXYZ || !indices || vertexCount < 3 || indexCount < 3) return false;
    if (indexCount % 3 != 0) return false;

    float total = 0.f;
    for (int t = 0; t + 2 < indexCount; t += 3) {
        const uint32_t i0 = indices[t];
        const uint32_t i1 = indices[t + 1];
        const uint32_t i2 = indices[t + 2];
        if (int(i0) >= vertexCount || int(i1) >= vertexCount || int(i2) >= vertexCount) continue;
        const glm::vec3 a = readPos(posXYZ, int(i0));
        const glm::vec3 b = readPos(posXYZ, int(i1));
        const glm::vec3 c = readPos(posXYZ, int(i2));
        glm::vec3 n = glm::cross(b - a, c - a);
        const float twiceArea = glm::length(n);
        if (twiceArea < 1e-10f) continue;
        n /= twiceArea;
        if (nrmXYZ) {
            glm::vec3 ns = readNrm(nrmXYZ, int(i0)) + readNrm(nrmXYZ, int(i1)) + readNrm(nrmXYZ, int(i2));
            if (glm::dot(ns, ns) > 1e-8f) n = glm::normalize(ns);
        }
        if (n.y < minSlopeDot) continue;
        Triangle tri;
        tri.i0 = i0;
        tri.i1 = i1;
        tri.i2 = i2;
        tri.area = 0.5f * twiceArea;
        tri.n = n;
        total += tri.area;
        tris.push_back(tri);
        cdf.push_back(total);
    }
    return total > 1e-12f && !tris.empty();
}

int pickTriangle(const std::vector<float> &cdf, float u) {
    const float target = u * cdf.back();
    auto it = std::lower_bound(cdf.begin(), cdf.end(), target);
    int idx = int(it - cdf.begin());
    if (idx >= int(cdf.size())) idx = int(cdf.size()) - 1;
    return idx;
}

void sampleOnTriangle(const float *posXYZ, const Triangle &tri, float u, float v, glm::vec3 &p) {
    if (u + v > 1.f) {
        u = 1.f - u;
        v = 1.f - v;
    }
    const glm::vec3 a = readPos(posXYZ, int(tri.i0));
    const glm::vec3 b = readPos(posXYZ, int(tri.i1));
    const glm::vec3 c = readPos(posXYZ, int(tri.i2));
    p = a + u * (b - a) + v * (c - a);
}

struct GridKey {
    int x = 0, y = 0, z = 0;
    bool operator==(const GridKey &o) const { return x == o.x && y == o.y && z == o.z; }
};

struct GridKeyHash {
    size_t operator()(const GridKey &k) const {
        return size_t(k.x) * 73856093u ^ size_t(k.y) * 19349663u ^ size_t(k.z) * 83492791u;
    }
};

GridKey toKey(const glm::vec3 &p, float cell) {
    return {int(std::floor(p.x / cell)), int(std::floor(p.y / cell)), int(std::floor(p.z / cell))};
}

bool tooClose(const glm::vec3 &p, float radius,
              const std::vector<Point> &accepted,
              const std::unordered_map<GridKey, std::vector<int>, GridKeyHash> &grid, float cell) {
    const GridKey c = toKey(p, cell);
    const float r2 = radius * radius;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                GridKey k{c.x + dx, c.y + dy, c.z + dz};
                auto it = grid.find(k);
                if (it == grid.end()) continue;
                for (int idx : it->second) {
                    const glm::vec3 d = accepted[size_t(idx)].position - p;
                    if (glm::dot(d, d) < r2) return true;
                }
            }
        }
    }
    return false;
}

float hash01(uint32_t id) {
    float h = std::fmod(float(id) * 0.61803398875f, 1.f);
    if (h < 0.f) h += 1.f;
    return h;
}

float clampf(float x, float lo, float hi) { return std::min(hi, std::max(lo, x)); }

float coverEdge(float distPx, float radiusPx) {
    // 1 inside the stroke, 0 outside, ~1px antialiased fringe for nearest upscale.
    return clampf(radiusPx + 0.65f - distPx, 0.f, 1.f);
}

void stampOver(std::vector<uint8_t> &rgba, int w, int h, int x, int y, float luma, float a) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    a = clampf(a, 0.f, 1.f);
    luma = clampf(luma, 0.f, 1.f);
    if (a < 0.02f) return;
    const size_t i = (size_t(y) * size_t(w) + size_t(x)) * 4u;
    const float oa = float(rgba[i + 3]) / 255.f;
    const float or_ = float(rgba[i + 0]) / 255.f;
    const float og = float(rgba[i + 1]) / 255.f;
    const float ob = float(rgba[i + 2]) / 255.f;
    const float outA = a + oa * (1.f - a);
    if (outA < 1e-4f) return;
    const float nr = (luma * a + or_ * oa * (1.f - a)) / outA;
    const float ng = (luma * a + og * oa * (1.f - a)) / outA;
    const float nb = (luma * a + ob * oa * (1.f - a)) / outA;
    rgba[i + 0] = uint8_t(std::round(clampf(nr, 0.f, 1.f) * 255.f));
    rgba[i + 1] = uint8_t(std::round(clampf(ng, 0.f, 1.f) * 255.f));
    rgba[i + 2] = uint8_t(std::round(clampf(nb, 0.f, 1.f) * 255.f));
    rgba[i + 3] = uint8_t(std::round(clampf(outA, 0.f, 1.f) * 255.f));
}

void stampBlade(std::vector<uint8_t> &rgba, int atlasW, int atlasH, int ox, int frameW, int frameH,
                float baseU, float height, float lean, float baseHalf, float tipHalf, float luma0,
                float luma1) {
    const float fw = float(frameW);
    const float fh = float(frameH);
    height = clampf(height, 0.12f, 1.f);
    for (int y = 0; y < frameH; ++y) {
        for (int x = 0; x < frameW; ++x) {
            const float u = (float(x) + 0.5f) / fw;
            const float v = 1.f - (float(y) + 0.5f) / fh;
            if (v < -0.02f || v > height + 0.04f) continue;
            const float t = clampf(v / height, 0.f, 1.f);
            const float cx = baseU + lean * t * t;
            const float half = baseHalf * (1.f - t * 0.82f) + tipHalf * t;
            const float dist = std::abs(u - cx) * fw;
            float cov = coverEdge(dist, half * fw);
            cov *= 1.f - clampf((v - height) / 0.03f, 0.f, 1.f);
            cov *= clampf((v + 0.02f) / 0.05f, 0.f, 1.f);
            if (cov > 0.5f) cov = 1.f;
            else if (cov < 0.25f) cov = 0.f;
            if (cov <= 0.f) continue;
            const float side = (u - cx) * fw;  // +right
            float luma = luma0 + (luma1 - luma0) * t;
            if (side > 0.15f) luma *= 0.78f;  // 1px-ish self-shadow on the right
            stampOver(rgba, atlasW, atlasH, ox + x, y, luma, cov);
        }
    }
}

struct BladeDesc {
    float u;
    float height;
    float lean;
    float baseHalf;
    float tipHalf;
    float luma0;
    float luma1;
};

void stampTuft(std::vector<uint8_t> &rgba, int atlasW, int atlasH, int ox, int frameW, int frameH,
               float wind) {
    // Tiny root pad only — a solid mound turns overlapping cards into a flat lime slab.
    const float fw = float(frameW);
    const float fh = float(frameH);
    for (int y = 0; y < frameH; ++y) {
        for (int x = 0; x < frameW; ++x) {
            const float u = (float(x) + 0.5f) / fw;
            const float v = 1.f - (float(y) + 0.5f) / fh;
            if (v > 0.16f) continue;
            const float cx = 0.50f + wind * 0.02f;
            const float hw = 0.16f * (1.f - v / 0.16f);
            float cov = coverEdge(std::abs(u - cx) * fw, hw * fw);
            if (cov > 0.5f) cov = 1.f;
            else if (cov < 0.25f) cov = 0.f;
            if (cov > 0.f) stampOver(rgba, atlasW, atlasH, ox + x, y, 0.72f, cov);
        }
    }

    // 7 separated blades with gaps so later cards show through. Outline then fill.
    const BladeDesc blades[] = {
        {0.50f, 0.98f, 0.02f, 0.070f, 0.018f, 0.86f, 1.00f}, {0.38f, 0.90f, -0.14f, 0.062f, 0.016f, 0.82f, 0.97f},
        {0.62f, 0.92f, 0.16f, 0.062f, 0.016f, 0.82f, 0.97f}, {0.28f, 0.74f, -0.26f, 0.056f, 0.016f, 0.78f, 0.93f},
        {0.72f, 0.76f, 0.28f, 0.056f, 0.016f, 0.78f, 0.93f}, {0.44f, 0.84f, -0.06f, 0.050f, 0.014f, 0.84f, 0.98f},
        {0.56f, 0.86f, 0.08f, 0.050f, 0.014f, 0.84f, 0.98f},
    };
    const float outline = 1.15f / fw;
    for (const BladeDesc &b : blades) {
        const float lean = b.lean + wind * 0.32f;
        const float u = b.u + wind * 0.03f;
        stampBlade(rgba, atlasW, atlasH, ox, frameW, frameH, u, b.height, lean, b.baseHalf + outline,
                   b.tipHalf + outline * 0.5f, 0.42f, 0.55f);
        stampBlade(rgba, atlasW, atlasH, ox, frameW, frameH, u, b.height, lean, b.baseHalf, b.tipHalf,
                   b.luma0, b.luma1);
    }
}

}  // namespace

int paramCount() { return int(kParamSlots.size()); }

std::string paramName(int index) {
    if (index < 0 || index >= int(kParamSlots.size())) return {};
    return kParamSlots[size_t(index)];
}

void bindDefaults(Shader *shader) {
    if (!shader) throw eve::Exception("grass::bindDefaults: null shader");
    shader->declareFloat("time");
    shader->declareFloat("frameDuration");
    shader->declareFloat("grassWidth");
    shader->declareFloat("grassHeight");
    shader->declareFloat("alphaCutoff");
    shader->declareFloat("alwaysDark");
    shader->declareVec3("lightGreen");
    shader->declareVec3("darkGreen");
    shader->declareFloat("frameCount");
    shader->declareFloat("atlasCols");
    shader->declareFloat("atlasRows");
    shader->declareFloat("grassVariantCount");
    shader->declareFloat("leafVariantCount");
    shader->declareFloat("leafRowOffset");
    shader->sendFloat("time", 0.f);
    shader->sendFloat("frameDuration", 0.12f);
    shader->sendFloat("grassWidth", 0.62f);
    shader->sendFloat("grassHeight", 0.95f);
    shader->sendFloat("alphaCutoff", 0.35f);
    shader->sendFloat("alwaysDark", 0.f);
    shader->sendVec3("lightGreen", 0.58f, 0.84f, 0.26f);
    shader->sendVec3("darkGreen", 0.10f, 0.28f, 0.12f);
    shader->sendFloat("frameCount", 4.f);
    shader->sendFloat("atlasCols", 2.f);
    shader->sendFloat("atlasRows", 2.f);
    shader->sendFloat("grassVariantCount", 1.f);
    shader->sendFloat("leafVariantCount", 1.f);
    shader->sendFloat("leafRowOffset", 0.f);
}

void bindAtlasLayout(Shader *shader, const PackedAtlasInfo &info) {
    if (!shader) throw eve::Exception("grass::bindAtlasLayout: null shader");
    shader->sendFloat("frameCount", float(std::max(info.frames, 1)));
    shader->sendFloat("atlasCols", float(std::max(info.atlasCols, 1)));
    shader->sendFloat("atlasRows", float(std::max(info.atlasRows, 1)));
    shader->sendFloat("grassVariantCount", float(std::max(info.grassVariants, 1)));
    shader->sendFloat("leafVariantCount", float(std::max(info.leafVariants, 1)));
    shader->sendFloat("leafRowOffset", float(std::max(info.leafRowOffset, 0)));
}

void bindLayer(Shader *shader, bool alwaysDark) {
    if (!shader) throw eve::Exception("grass::bindLayer: null shader");
    shader->sendFloat("alwaysDark", alwaysDark ? 1.f : 0.f);
}

void setTime(Shader *shader, float seconds) {
    if (!shader) throw eve::Exception("grass::setTime: null shader");
    shader->sendFloat("time", seconds);
}

void setFrameDuration(Shader *shader, float seconds) {
    if (!shader) throw eve::Exception("grass::setFrameDuration: null shader");
    shader->sendFloat("frameDuration", seconds > 1e-4f ? seconds : 1e-4f);
}

Shader *createShader(Graphics *gfx) {
    if (!gfx) throw eve::Exception("grass::createShader: null graphics");
    Shader *sh = nullptr;
    if (gfx->getBackendName() == "webgpu") {
        sh = gfx->newMeshShaderFromWgsl(shaders::kGrassVertWgsl, shaders::kGrassFragWgsl);
    } else {
        auto vert = copySpv(mesh3d_grass_vert_spv, mesh3d_grass_vert_spv_count);
        auto frag = copySpv(mesh3d_grass_frag_spv, mesh3d_grass_frag_spv_count);
        sh = gfx->newMeshShaderFromSpv(vert, frag);
    }
    if (!sh || !sh->gpuHandle)
        throw eve::Exception("grass::createShader: failed to create grass shader");
    bindDefaults(sh);
    return sh;
}

int swayFrame(float time, float frameDuration, uint32_t instanceId, int frameCount) {
    if (frameCount < 1) frameCount = 1;
    const float dur = frameDuration > 1e-4f ? frameDuration : 1e-4f;
    const float t = time / dur + hash01(instanceId) * float(frameCount);
    int f = int(std::floor(t)) % frameCount;
    if (f < 0) f += frameCount;
    return f;
}

int swayAtlasWidth(int frameW, int frames) { return std::max(frameW, 1) * std::max(frames, 1); }
int swayAtlasHeight(int frameH) { return std::max(frameH, 1); }

void makeSwayAtlasRGBA(int frameW, int frameH, int frames, std::vector<uint8_t> &rgbaOut) {
    frameW = std::max(frameW, 8);
    frameH = std::max(frameH, 8);
    frames = std::max(frames, 1);
    const int w = swayAtlasWidth(frameW, frames);
    const int h = swayAtlasHeight(frameH);
    rgbaOut.assign(size_t(w * h * 4), 0);

    for (int f = 0; f < frames; ++f) {
        const float wind = (float(f) - 1.5f) / 1.5f;  // -1 .. +1 across 4 frames
        stampTuft(rgbaOut, w, h, f * frameW, frameW, frameH, wind);
    }
}

Texture *createSwayAtlas(Graphics *gfx, int frameW, int frameH, int frames) {
    if (!gfx) throw eve::Exception("grass::createSwayAtlas: null graphics");
    std::vector<uint8_t> rgba;
    makeSwayAtlasRGBA(frameW, frameH, frames, rgba);
    TextureCreateInfo info;
    info.sampler = TextureSampler::nearest();
    return gfx->newTexture(swayAtlasWidth(frameW, frames), swayAtlasHeight(frameH), rgba.data(),
                           info);
}

namespace {

bool readWholeFile(const std::string &path, std::vector<char> &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return in.good() || !out.empty();
}

void maskToTintable(std::vector<uint8_t> &rgba) {
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        const uint8_t luma = std::max(rgba[i], std::max(rgba[i + 1], rgba[i + 2]));
        const uint8_t a = std::max(luma, rgba[i + 3]);
        rgba[i + 0] = 255;
        rgba[i + 1] = 255;
        rgba[i + 2] = 255;
        rgba[i + 3] = a;
    }
}

bool loadSwayMaskPng(const std::string &path, std::vector<uint8_t> &rgba, int &w, int &h) {
    std::vector<char> raw;
    if (!readWholeFile(path, raw)) return false;
    eve::image::Image *image = eve::image::Image::create();
    if (!image) return false;
    eve::data::ByteData bytes(raw.data(), raw.size());
    std::unique_ptr<eve::image::ImageData> img(image->newImageData(&bytes));
    if (!img) return false;
    w = img->getWidth();
    h = img->getHeight();
    if (w < 2 || h < 2) return false;
    const size_t n = size_t(w) * size_t(h) * 4u;
    if (img->getSize() < n) return false;
    rgba.resize(n);
    std::memcpy(rgba.data(), img->getData(), n);
    maskToTintable(rgba);
    return true;
}

void blitRgba(std::vector<uint8_t> &dst, int dw, int dh, int dx, int dy,
              const std::vector<uint8_t> &src, int sw, int sh) {
    for (int y = 0; y < sh; ++y) {
        const int ty = dy + y;
        if (ty < 0 || ty >= dh) continue;
        for (int x = 0; x < sw; ++x) {
            const int tx = dx + x;
            if (tx < 0 || tx >= dw) continue;
            const size_t si = (size_t(y) * size_t(sw) + size_t(x)) * 4u;
            const size_t di = (size_t(ty) * size_t(dw) + size_t(tx)) * 4u;
            dst[di + 0] = src[si + 0];
            dst[di + 1] = src[si + 1];
            dst[di + 2] = src[si + 2];
            dst[di + 3] = src[si + 3];
        }
    }
}

}  // namespace

void packSwayAtlasRGBA(const std::vector<std::string> &grassFiles,
                       const std::vector<std::string> &leafFiles, std::vector<uint8_t> &rgbaOut,
                       PackedAtlasInfo &info) {
    if (grassFiles.empty()) throw eve::Exception("grass::packSwayAtlasRGBA: no grass atlas files");

    struct Loaded {
        std::vector<uint8_t> rgba;
        int w = 0;
        int h = 0;
    };
    auto loadOne = [](const std::string &path) {
        Loaded img;
        if (!loadSwayMaskPng(path, img.rgba, img.w, img.h))
            throw eve::Exception("grass::packSwayAtlasRGBA: failed to load '%s'", path.c_str());
        return img;
    };

    std::vector<Loaded> grass;
    grass.reserve(grassFiles.size());
    for (const auto &p : grassFiles) grass.push_back(loadOne(p));
    std::vector<Loaded> leaf;
    leaf.reserve(leafFiles.size());
    for (const auto &p : leafFiles) leaf.push_back(loadOne(p));

    const int tw = grass.front().w;
    const int th = grass.front().h;
    auto checkSize = [tw, th](const Loaded &img, const char *kind) {
        if (img.w != tw || img.h != th)
            throw eve::Exception("grass::packSwayAtlasRGBA: %s atlas size mismatch (%dx%d vs %dx%d)",
                                 kind, img.w, img.h, tw, th);
    };
    for (const auto &img : grass) checkSize(img, "grass");
    for (const auto &img : leaf) checkSize(img, "leaf");

    const int nGrass = int(grass.size());
    const int nLeaf = int(leaf.size());
    const int nX = std::max(nGrass, std::max(nLeaf, 1));
    const int nY = nLeaf > 0 ? 2 : 1;
    info.frames = 4;
    info.grassVariants = nGrass;
    info.leafVariants = nLeaf > 0 ? nLeaf : 1;
    info.leafRowOffset = nLeaf > 0 ? 2 : 0;
    info.atlasCols = nX * 2;
    info.atlasRows = nY * 2;
    info.width = nX * tw;
    info.height = nY * th;
    rgbaOut.assign(size_t(info.width) * size_t(info.height) * 4u, 0);

    for (int i = 0; i < nGrass; ++i)
        blitRgba(rgbaOut, info.width, info.height, i * tw, 0, grass[size_t(i)].rgba, tw, th);
    for (int i = 0; i < nLeaf; ++i)
        blitRgba(rgbaOut, info.width, info.height, i * tw, th, leaf[size_t(i)].rgba, tw, th);
}

Texture *createSwayAtlasFromFiles(Graphics *gfx, const std::vector<std::string> &grassFiles,
                                  const std::vector<std::string> &leafFiles,
                                  PackedAtlasInfo *infoOut) {
    if (!gfx) throw eve::Exception("grass::createSwayAtlasFromFiles: null graphics");
    PackedAtlasInfo info;
    std::vector<uint8_t> rgba;
    packSwayAtlasRGBA(grassFiles, leafFiles, rgba, info);
    if (infoOut) *infoOut = info;
    TextureCreateInfo texInfo;
    texInfo.sampler = TextureSampler::linear();
    return gfx->newTexture(info.width, info.height, rgba.data(), texInfo);
}

std::vector<Point> sampleHalton(const float *posXYZ, const float *nrmXYZ, int vertexCount,
                                const uint32_t *indices, int indexCount, int count, uint32_t seed,
                                float minSlopeDot) {
    std::vector<Point> out;
    if (count <= 0) return out;
    std::vector<Triangle> tris;
    std::vector<float> cdf;
    if (!buildTriangles(posXYZ, nrmXYZ, vertexCount, indices, indexCount, minSlopeDot, tris, cdf))
        return out;

    out.reserve(size_t(count));
    for (int i = 0; i < count; ++i) {
        const uint32_t n = mixSeed(seed, uint32_t(i + 1));
        const float uTri = wrap01(radicalInverse(n, 2));
        const float u = wrap01(radicalInverse(n, 3));
        const float v = wrap01(radicalInverse(n, 5));
        const Triangle &tri = tris[size_t(pickTriangle(cdf, uTri))];
        Point p;
        sampleOnTriangle(posXYZ, tri, u, v, p.position);
        p.normal = tri.n;
        p.id = uint32_t(i);
        p.scale = 0.85f + 0.3f * hash01(p.id + seed);
        out.push_back(p);
    }
    return out;
}

std::vector<Point> samplePoisson(const float *posXYZ, const float *nrmXYZ, int vertexCount,
                                 const uint32_t *indices, int indexCount,
                                 const SampleParams &params) {
    std::vector<Point> accepted;
    if (params.maxPoints <= 0 || params.radius <= 1e-6f) return accepted;

    std::vector<Triangle> tris;
    std::vector<float> cdf;
    if (!buildTriangles(posXYZ, nrmXYZ, vertexCount, indices, indexCount, params.minSlopeDot, tris,
                        cdf))
        return accepted;

    const float cell = params.radius;
    std::unordered_map<GridKey, std::vector<int>, GridKeyHash> grid;
    const int attempts = std::max(params.maxPoints * 24, params.maxPoints + 16);

    for (int i = 0; i < attempts && int(accepted.size()) < params.maxPoints; ++i) {
        const uint32_t n = mixSeed(params.seed, uint32_t(i + 1));
        const float uTri = wrap01(radicalInverse(n, 2));
        const float u = wrap01(radicalInverse(n, 3));
        const float v = wrap01(radicalInverse(n, 5));
        const Triangle &tri = tris[size_t(pickTriangle(cdf, uTri))];
        glm::vec3 pos;
        sampleOnTriangle(posXYZ, tri, u, v, pos);
        if (tooClose(pos, params.radius, accepted, grid, cell)) continue;

        Point p;
        p.position = pos;
        p.normal = tri.n;
        p.id = uint32_t(accepted.size());
        p.scale = 0.85f + 0.3f * hash01(p.id + params.seed);
        const int idx = int(accepted.size());
        accepted.push_back(p);
        grid[toKey(pos, cell)].push_back(idx);
    }
    return accepted;
}

BillboardMesh buildBillboards(const std::vector<Point> &points, float width, float height,
                              bool alwaysDark) {
    (void)width;
    (void)height;
    BillboardMesh mesh;
    const size_t n = points.size();
    mesh.posXYZ.reserve(n * 4 * 3);
    mesh.nrmXYZ.reserve(n * 4 * 3);
    mesh.uvST.reserve(n * 4 * 2);
    mesh.indices.reserve(n * 6);

    // Local corners: (0,0) bottom-left, (1,0) bottom-right, (1,1) top-right, (0,1) top-left.
    // Root is the bottom-center of the rectangle, i.e. UV (0.5, 0).
    const float cu[4] = {0.f, 1.f, 1.f, 0.f};
    const float cv[4] = {0.f, 0.f, 1.f, 1.f};
    const uint32_t corners[6] = {0, 1, 2, 0, 2, 3};

    for (size_t i = 0; i < n; ++i) {
        const Point &p = points[i];
        const uint32_t base = uint32_t(i * 4);
        for (int c = 0; c < 4; ++c) {
            mesh.posXYZ.push_back(p.position.x);
            mesh.posXYZ.push_back(p.position.y);
            mesh.posXYZ.push_back(p.position.z);
            mesh.nrmXYZ.push_back(float(p.id));
            mesh.nrmXYZ.push_back(p.scale > 1e-3f ? p.scale : 1.f);
            mesh.nrmXYZ.push_back(alwaysDark ? 1.f : 0.f);
            mesh.uvST.push_back(cu[c]);
            mesh.uvST.push_back(cv[c]);
        }
        for (uint32_t k : corners) mesh.indices.push_back(base + k);
    }
    return mesh;
}

void makePlane(float sizeX, float sizeZ, int segX, int segZ, std::vector<float> &posXYZ,
               std::vector<float> &nrmXYZ, std::vector<uint32_t> &indices) {
    segX = std::max(segX, 1);
    segZ = std::max(segZ, 1);
    sizeX = std::max(sizeX, 1e-3f);
    sizeZ = std::max(sizeZ, 1e-3f);
    const int nx = segX + 1;
    const int nz = segZ + 1;
    posXYZ.clear();
    nrmXYZ.clear();
    indices.clear();
    posXYZ.reserve(size_t(nx * nz * 3));
    nrmXYZ.reserve(size_t(nx * nz * 3));
    indices.reserve(size_t(segX * segZ * 6));

    for (int z = 0; z < nz; ++z) {
        for (int x = 0; x < nx; ++x) {
            const float px = (float(x) / float(segX) - 0.5f) * sizeX;
            const float pz = (float(z) / float(segZ) - 0.5f) * sizeZ;
            posXYZ.push_back(px);
            posXYZ.push_back(0.f);
            posXYZ.push_back(pz);
            nrmXYZ.push_back(0.f);
            nrmXYZ.push_back(1.f);
            nrmXYZ.push_back(0.f);
        }
    }
    for (int z = 0; z < segZ; ++z) {
        for (int x = 0; x < segX; ++x) {
            const uint32_t i0 = uint32_t(z * nx + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + uint32_t(nx);
            const uint32_t i3 = i2 + 1;
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i1);
            indices.push_back(i1);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }
}

}  // namespace eve::graphics::grass

namespace eve::graphics {

GrassField::GrassField(Graphics *gfx) : gfx_(gfx) {
    if (!gfx_) throw eve::Exception("GrassField: null graphics");
}

void GrassField::bake(const float *posXYZ, const float *nrmXYZ, int vertexCount,
                      const uint32_t *indices, int indexCount, const BakeParams &params) {
    if (!gfx_) throw eve::Exception("GrassField::bake: null graphics");

    grass::SampleParams denseP;
    denseP.radius = params.denseRadius;
    denseP.maxPoints = params.maxDense;
    denseP.seed = params.seed;
    denseP.minSlopeDot = params.minSlopeDot;

    grass::SampleParams sparseP = denseP;
    sparseP.radius = params.sparseRadius;
    sparseP.maxPoints = params.maxSparse;
    sparseP.seed = params.seed * 7477u + 13u;

    const auto densePts =
        grass::samplePoisson(posXYZ, nrmXYZ, vertexCount, indices, indexCount, denseP);
    const auto sparsePts =
        grass::samplePoisson(posXYZ, nrmXYZ, vertexCount, indices, indexCount, sparseP);
    denseCount_ = int(densePts.size());
    sparseCount_ = int(sparsePts.size());

    const auto denseMesh = grass::buildBillboards(densePts, params.width, params.height, false);
    const auto sparseMesh = grass::buildBillboards(sparsePts, params.width, params.height, true);

    if (!shader_) shader_ = grass::createShader(gfx_);
    grass::PackedAtlasInfo layout;
    if (!params.grassAtlasFiles.empty()) {
        atlas_ = grass::createSwayAtlasFromFiles(gfx_, params.grassAtlasFiles, params.leafAtlasFiles,
                                                 &layout);
    } else {
        if (!atlas_)
            atlas_ = grass::createSwayAtlas(gfx_, params.atlasFrameW, params.atlasFrameH,
                                            params.atlasFrames);
        layout.frames = std::max(params.atlasFrames, 1);
        layout.atlasCols = layout.frames;
        layout.atlasRows = 1;
        layout.grassVariants = 1;
        layout.leafVariants = 1;
        layout.leafRowOffset = 0;
    }

    grass::bindDefaults(shader_);
    grass::bindAtlasLayout(shader_, layout);
    shader_->sendFloat("grassWidth", params.width);
    shader_->sendFloat("grassHeight", params.height);
    shader_->sendFloat("frameDuration", frameDuration_);
    shader_->sendFloat("time", time_);

    denseMesh_ = nullptr;
    sparseMesh_ = nullptr;
    if (!denseMesh.indices.empty())
        denseMesh_ = gfx_->newMeshFromArrays(denseMesh.posXYZ.data(), denseMesh.nrmXYZ.data(),
                                             denseMesh.uvST.data(), int(denseMesh.posXYZ.size() / 3),
                                             denseMesh.indices.data(), int(denseMesh.indices.size()));
    if (!sparseMesh.indices.empty())
        sparseMesh_ =
            gfx_->newMeshFromArrays(sparseMesh.posXYZ.data(), sparseMesh.nrmXYZ.data(),
                                    sparseMesh.uvST.data(), int(sparseMesh.posXYZ.size() / 3),
                                    sparseMesh.indices.data(), int(sparseMesh.indices.size()));
}

void GrassField::bakePlane(float sizeX, float sizeZ, int segX, int segZ) {
    bakePlane(sizeX, sizeZ, segX, segZ, BakeParams{});
}

void GrassField::bakePlane(float sizeX, float sizeZ, int segX, int segZ, const BakeParams &params) {
    std::vector<float> pos, nrm;
    std::vector<uint32_t> idx;
    grass::makePlane(sizeX, sizeZ, segX, segZ, pos, nrm, idx);
    bake(pos.data(), nrm.data(), int(pos.size() / 3), idx.data(), int(idx.size()), params);
}

void GrassField::update(float dt) {
    time_ += dt;
    if (shader_) grass::setTime(shader_, time_);
}

void GrassField::setTime(float seconds) {
    time_ = seconds;
    if (shader_) grass::setTime(shader_, time_);
}

void GrassField::setFrameDuration(float seconds) {
    frameDuration_ = seconds > 1e-4f ? seconds : 1e-4f;
    if (shader_) grass::setFrameDuration(shader_, frameDuration_);
}

void GrassField::draw() { draw(glm::mat4(1.f)); }

void GrassField::draw(const glm::mat4 &model) {
    if (!gfx_ || !shader_ || !atlas_) return;
    const Color tint(1.f, 1.f, 1.f, 1.f);
    grass::setTime(shader_, time_);
    grass::setFrameDuration(shader_, frameDuration_);
    if (denseMesh_) {
        grass::bindLayer(shader_, false);
        gfx_->drawMeshShader(denseMesh_, model, atlas_, tint, shader_);
    }
    if (sparseMesh_) {
        grass::bindLayer(shader_, true);
        gfx_->drawMeshShader(sparseMesh_, model, atlas_, tint, shader_);
    }
}

}  // namespace eve::graphics
