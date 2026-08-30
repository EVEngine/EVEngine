#include "spritestack/SpriteStack.h"

#include "common/Exception.h"
#include "image/ImageData.h"
#include "model3d/ModelData.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <string>
#include <utility>
#include <vector>

struct aiMesh;

namespace eve::spritestack {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float clampf(float x, float lo, float hi) { return std::min(hi, std::max(lo, x)); }

struct Projection {
    int u = 0;  // model axis -> image column
    int v = 0;  // model axis -> image row
    int d = 0;  // model axis -> slice depth
};

Projection projectionForAxis(const std::string &axis) {
    if (axis == "x") return {1, 2, 0};
    if (axis == "y") return {0, 2, 1};  // top-down layers
    return {0, 1, 2};                   // vertical bread slices (default)
}

struct Tri2D {
    glm::vec2 px[3]{};
    float depth[3]{};
    image::ImageData::Colorf color{1.f, 1.f, 1.f, 1.f};
};

/**
 * Rasterize a layer as the solid's exact cross-section at depth `dc`.
 *
 * For every pixel, a ray along the slice axis is tested against each triangle
 * (2D point-in-triangle in the projected plane). All hit depths are collected
 * and deduplicated (shared edges/vertices hit by two adjacent triangles report
 * the exact same interpolated depth), then the number of distinct surfaces
 * strictly above the plane decides even-odd parity: odd = inside the solid.
 * The closest surface above the plane (the one a top-down viewer sees)
 * supplies the pixel color, so shading follows the mesh normals.
 */
void rasterizeCrossSection(const std::vector<Tri2D> &tris, float dc, int w, int h,
                           uint8_t *rgba) {
    const size_t pixelCount = size_t(w) * size_t(h);
    std::vector<std::vector<float>> hits(pixelCount);
    std::vector<float> bestY(pixelCount, 1e30f);
    std::vector<uint32_t> bestRGB(pixelCount, 0xffffffu);

    for (const auto &tri : tris) {
        const glm::vec2 &a = tri.px[0];
        const glm::vec2 &b = tri.px[1];
        const glm::vec2 &c = tri.px[2];
        int x0 = int(std::floor(std::min(a.x, std::min(b.x, c.x))));
        int x1 = int(std::ceil(std::max(a.x, std::max(b.x, c.x))));
        int y0 = int(std::floor(std::min(a.y, std::min(b.y, c.y))));
        int y1 = int(std::ceil(std::max(a.y, std::max(b.y, c.y))));
        x0 = std::max(0, x0);
        y0 = std::max(0, y0);
        x1 = std::min(w - 1, x1);
        y1 = std::min(h - 1, y1);

        const glm::vec2 e0 = b - a;
        const glm::vec2 e1 = c - a;
        const float denom = e0.x * e1.y - e0.y * e1.x;
        if (std::fabs(denom) < 1e-12f) continue;

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const glm::vec2 p(float(x) + 0.5f, float(y) + 0.5f);
                const glm::vec2 d = p - a;
                const float u = (d.x * e1.y - d.y * e1.x) / denom;
                const float v = (e0.x * d.y - e0.y * d.x) / denom;
                if (u < -1e-4f || v < -1e-4f || u + v > 1.f + 1e-4f) continue;
                const float w0 = 1.f - u - v;
                const float depthAt = w0 * tri.depth[0] + u * tri.depth[1] + v * tri.depth[2];
                if (depthAt <= dc) continue;

                const size_t i = size_t(y) * size_t(w) + size_t(x);
                hits[i].push_back(depthAt);
                if (depthAt < bestY[i]) {
                    bestY[i] = depthAt;
                    bestRGB[i] = (uint32_t(clampf(tri.color.r, 0.f, 1.f) * 255.f) << 16) |
                                 (uint32_t(clampf(tri.color.g, 0.f, 1.f) * 255.f) << 8) |
                                 uint32_t(clampf(tri.color.b, 0.f, 1.f) * 255.f);
                }
            }
        }
    }

    std::vector<float> uniqueHits;
    for (size_t i = 0; i < pixelCount; ++i) {
        if (hits[i].empty()) continue;
        std::sort(hits[i].begin(), hits[i].end());
        uniqueHits.clear();
        for (float d : hits[i]) {
            // Shared edges/vertices report the same surface with slightly
            // different interpolated depths (float weight sums); group them.
            if (uniqueHits.empty() ||
                d - uniqueHits.back() > 1e-5f * std::max(1.f, std::fabs(d)))
                uniqueHits.push_back(d);
        }
        if (uniqueHits.size() % 2 == 0) continue;  // even = outside the solid
        uint8_t *px = rgba + i * 4;
        px[0] = uint8_t(bestRGB[i] >> 16);
        px[1] = uint8_t(bestRGB[i] >> 8);
        px[2] = uint8_t(bestRGB[i]);
        px[3] = 255;
    }
}

std::vector<image::ImageData *> sliceArrays(const float *pos, const float *nrm, const float *rgb,
                                            int vertexCount, const uint32_t *indices, int indexCount,
                                            const SliceOptions &opt) {
    if (!pos || vertexCount < 3 || !indices || indexCount < 3)
        throw eve::Exception("SpriteStack.sliceMesh: invalid mesh arrays");
    if (opt.layerCount <= 0) throw eve::Exception("SpriteStack.sliceMesh: layerCount must be > 0");
    if (opt.imageW <= 0 || opt.imageH <= 0)
        throw eve::Exception("SpriteStack.sliceMesh: image size must be > 0");

    const Projection proj = projectionForAxis(opt.axis);

    glm::vec3 mn(1e30f), mx(-1e30f);
    for (int i = 0; i < vertexCount; ++i) {
        const glm::vec3 p(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]);
        mn = glm::min(mn, p);
        mx = glm::max(mx, p);
    }
    if (mn.x > mx.x) throw eve::Exception("SpriteStack.sliceMesh: empty mesh AABB");

    const float d0 = mn[proj.d];
    const float d1 = opt.thickness > 0.f ? d0 + opt.thickness * float(opt.layerCount - 1)
                                         : mx[proj.d];
    const float slab = opt.thickness > 0.f ? opt.thickness : (d1 - d0) / float(opt.layerCount);
    if (slab <= 0.f) throw eve::Exception("SpriteStack.sliceMesh: zero slice thickness");

    const float uMin = mn[proj.u] - opt.padding * std::max(1.f, mx[proj.u] - mn[proj.u]);
    const float uMax = mx[proj.u] + opt.padding * std::max(1.f, mx[proj.u] - mn[proj.u]);
    const float vMin = mn[proj.v] - opt.padding * std::max(1.f, mx[proj.v] - mn[proj.v]);
    const float vMax = mx[proj.v] + opt.padding * std::max(1.f, mx[proj.v] - mn[proj.v]);
    const float uSpan = uMax - uMin;
    const float vSpan = vMax - vMin;
    if (uSpan <= 0.f || vSpan <= 0.f)
        throw eve::Exception("SpriteStack.sliceMesh: degenerate projected AABB");

    const glm::vec3 viewDir =
        proj.d == 0 ? glm::vec3(1.f, 0.f, 0.f)
                    : (proj.d == 1 ? glm::vec3(0.f, 1.f, 0.f) : glm::vec3(0.f, 0.f, 1.f));

    std::vector<image::ImageData *> layers;
    layers.reserve(size_t(opt.layerCount));
    const int triangleCount = indexCount / 3;
    std::vector<Tri2D> tris;
    tris.reserve(size_t(triangleCount));

    for (int t = 0; t < triangleCount; ++t) {
        const uint32_t i0 = indices[t * 3];
        const uint32_t i1 = indices[t * 3 + 1];
        const uint32_t i2 = indices[t * 3 + 2];
        if (int(i0) >= vertexCount || int(i1) >= vertexCount || int(i2) >= vertexCount) continue;

        const glm::vec3 a(pos[i0 * 3], pos[i0 * 3 + 1], pos[i0 * 3 + 2]);
        const glm::vec3 b(pos[i1 * 3], pos[i1 * 3 + 1], pos[i1 * 3 + 2]);
        const glm::vec3 c(pos[i2 * 3], pos[i2 * 3 + 1], pos[i2 * 3 + 2]);

        image::ImageData::Colorf color{opt.tintR, opt.tintG, opt.tintB, 1.f};
        if (rgb) {
            const float rr = (rgb[i0 * 3] + rgb[i1 * 3] + rgb[i2 * 3]) / 3.f;
            const float gg = (rgb[i0 * 3 + 1] + rgb[i1 * 3 + 1] + rgb[i2 * 3 + 1]) / 3.f;
            const float bb = (rgb[i0 * 3 + 2] + rgb[i1 * 3 + 2] + rgb[i2 * 3 + 2]) / 3.f;
            color.r *= rr;
            color.g *= gg;
            color.b *= bb;
        }
        if (opt.shade) {
            glm::vec3 n = glm::cross(b - a, c - a);
            if (glm::dot(n, n) > 1e-12f) {
                n = glm::normalize(n);
                const float s = 0.72f + 0.28f * std::abs(glm::dot(n, viewDir));
                color.r *= s;
                color.g *= s;
                color.b *= s;
            }
        }

        Tri2D tri;
        tri.px[0] = glm::vec2((a[proj.u] - uMin) / uSpan * float(opt.imageW - 1),
                              (1.f - (a[proj.v] - vMin) / vSpan) * float(opt.imageH - 1));
        tri.px[1] = glm::vec2((b[proj.u] - uMin) / uSpan * float(opt.imageW - 1),
                              (1.f - (b[proj.v] - vMin) / vSpan) * float(opt.imageH - 1));
        tri.px[2] = glm::vec2((c[proj.u] - uMin) / uSpan * float(opt.imageW - 1),
                              (1.f - (c[proj.v] - vMin) / vSpan) * float(opt.imageH - 1));
        tri.depth[0] = a[proj.d];
        tri.depth[1] = b[proj.d];
        tri.depth[2] = c[proj.d];
        tri.color = color;
        tris.push_back(tri);
    }

    for (int li = 0; li < opt.layerCount; ++li) {
        auto *img = new image::ImageData(opt.imageW, opt.imageH, "RGBA8");
        auto *data = static_cast<uint8_t *>(img->getData());
        std::memset(data, 0, size_t(opt.imageW) * size_t(opt.imageH) * 4);
        const float dc = d0 + (float(li) + 0.5f) * slab;
        rasterizeCrossSection(tris, dc, opt.imageW, opt.imageH, data);
        layers.push_back(img);
    }
    return layers;
}

void makeBox(std::vector<float> &pos, std::vector<float> &nrm, std::vector<uint32_t> &idx) {
    const float h = 0.5f;
    const glm::vec3 corners[8] = {
        {-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h},
        {-h, -h, h},  {h, -h, h},  {h, h, h},  {-h, h, h},
    };
    const glm::vec3 faces[6] = {
        {0.f, 0.f, -1.f}, {0.f, 0.f, 1.f}, {-1.f, 0.f, 0.f},
        {1.f, 0.f, 0.f},  {0.f, -1.f, 0.f}, {0.f, 1.f, 0.f},
    };
    const int quads[6][4] = {
        {0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7},
        {1, 5, 6, 2}, {4, 5, 1, 0}, {3, 2, 6, 7},
    };
    for (int f = 0; f < 6; ++f) {
        for (int k = 0; k < 4; ++k) {
            const glm::vec3 &p = corners[quads[f][k]];
            pos.insert(pos.end(), {p.x, p.y, p.z});
            nrm.insert(nrm.end(), {faces[f].x, faces[f].y, faces[f].z});
        }
        const uint32_t base = uint32_t(f * 4);
        idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

void makeLathe(const std::string &kind, int slices, std::vector<float> &pos,
               std::vector<float> &nrm, std::vector<uint32_t> &idx) {
    const bool sphere = kind == "sphere";
    const bool cone = kind == "cone";
    const int stacks = sphere ? 12 : 1;
    auto ring = [&](float y, float radius, float nY) {
        // Push `slices` vertices forming one horizontal ring.
        const uint32_t base = uint32_t(pos.size() / 3);
        for (int s = 0; s < slices; ++s) {
            const float a = float(s) / float(slices) * 2.f * kPi;
            const glm::vec3 n(std::cos(a), nY, std::sin(a));
            pos.insert(pos.end(), {std::cos(a) * radius, y, std::sin(a) * radius});
            nrm.insert(nrm.end(), {n.x, n.y, n.z});
        }
        return base;
    };
    auto connectRings = [&](uint32_t r0, uint32_t r1) {
        for (int s = 0; s < slices; ++s) {
            const uint32_t s0 = r0 + uint32_t(s);
            const uint32_t s1 = r0 + uint32_t((s + 1) % slices);
            const uint32_t t0 = r1 + uint32_t(s);
            const uint32_t t1 = r1 + uint32_t((s + 1) % slices);
            idx.insert(idx.end(), {s0, t0, t1, s0, t1, s1});
        }
    };
    auto cap = [&](float y, float radius, float nY) {
        const uint32_t center = uint32_t(pos.size() / 3);
        pos.insert(pos.end(), {0.f, y, 0.f});
        nrm.insert(nrm.end(), {0.f, nY, 0.f});
        for (int s = 0; s < slices; ++s) {
            const float a = float(s) / float(slices) * 2.f * kPi;
            pos.insert(pos.end(), {std::cos(a) * radius, y, std::sin(a) * radius});
            nrm.insert(nrm.end(), {0.f, nY, 0.f});
            const uint32_t ring0 = center + 1;
            const uint32_t a0 = ring0 + uint32_t(s);
            const uint32_t b0 = ring0 + uint32_t((s + 1) % slices);
            if (nY > 0.f)
                idx.insert(idx.end(), {center, b0, a0});
            else
                idx.insert(idx.end(), {center, a0, b0});
        }
    };

    if (sphere) {
        std::vector<uint32_t> rings;
        for (int st = 0; st < stacks; ++st) {
            const float t0 = float(st) / float(stacks);
            const float y0 = std::cos(t0 * kPi) * 0.5f;
            const float r0 = std::sin(t0 * kPi) * 0.5f;
            rings.push_back(ring(y0, r0, -std::sin(t0 * kPi)));
        }
        for (int st = 0; st + 1 < stacks; ++st)
            connectRings(rings[size_t(st)], rings[size_t(st + 1)]);
        return;
    }

    if (cone) {
        const uint32_t apex = uint32_t(pos.size() / 3);
        pos.insert(pos.end(), {0.f, 0.5f, 0.f});
        nrm.insert(nrm.end(), {0.f, 1.f, 0.f});
        for (int s = 0; s < slices; ++s) {
            const float a0 = float(s) / float(slices) * 2.f * kPi;
            const float a1 = float(s + 1) / float(slices) * 2.f * kPi;
            const uint32_t b0 = uint32_t(pos.size() / 3);
            pos.insert(pos.end(), {std::cos(a0) * 0.5f, -0.5f, std::sin(a0) * 0.5f});
            nrm.insert(nrm.end(), {std::cos(a0) * 0.7071f, 0.7071f, std::sin(a0) * 0.7071f});
            pos.insert(pos.end(), {std::cos(a1) * 0.5f, -0.5f, std::sin(a1) * 0.5f});
            nrm.insert(nrm.end(), {std::cos(a1) * 0.7071f, 0.7071f, std::sin(a1) * 0.7071f});
            idx.insert(idx.end(), {apex, b0, b0 + 1});
        }
        cap(-0.5f, 0.5f, -1.f);
        return;
    }

    // cylinder
    const uint32_t bottom = ring(-0.5f, 0.5f, 0.f);
    const uint32_t top = ring(0.5f, 0.5f, 0.f);
    connectRings(bottom, top);
    cap(0.5f, 0.5f, 1.f);
    cap(-0.5f, 0.5f, -1.f);
}

}  // namespace

std::vector<image::ImageData *> sliceMeshToLayers(const SliceInput &input, const SliceOptions &opt) {
    return sliceArrays(input.posXYZ, input.nrmXYZ, input.rgb, input.vertexCount, input.indices,
                       input.indexCount, opt);
}

std::vector<image::ImageData *> sliceModelToLayers(model3d::ModelData *model,
                                                   const SliceOptions &opt) {
    if (!model) throw eve::Exception("SpriteStack.sliceModel: null model");
    std::vector<float> pos, nrm, rgb;
    std::vector<uint32_t> idx;
    const int meshCount = model->getMeshCount();
    for (int m = 0; m < meshCount; ++m) {
        const aiMesh *am = model->getMesh(m);
        if (!am) continue;
        const uint32_t base = uint32_t(pos.size() / 3);
        for (unsigned v = 0; v < am->mNumVertices; ++v) {
            const aiVector3D &p = am->mVertices[v];
            pos.insert(pos.end(), {p.x, p.y, p.z});
            if (am->mNormals) {
                const aiVector3D &n = am->mNormals[v];
                nrm.insert(nrm.end(), {n.x, n.y, n.z});
            }
            if (am->mColors[0] != nullptr) {
                const aiColor4D &c = am->mColors[0][v];
                rgb.insert(rgb.end(), {c.r, c.g, c.b});
            }
        }
        for (unsigned f = 0; f < am->mNumFaces; ++f) {
            const auto &face = am->mFaces[f];
            if (face.mNumIndices != 3) continue;
            idx.insert(idx.end(), {base + face.mIndices[0], base + face.mIndices[1],
                                   base + face.mIndices[2]});
        }
    }
    SliceInput in{};
    in.posXYZ = pos.data();
    in.nrmXYZ = nrm.empty() ? nullptr : nrm.data();
    in.rgb = rgb.empty() ? nullptr : rgb.data();
    in.vertexCount = int(pos.size() / 3);
    in.indices = idx.data();
    in.indexCount = int(idx.size());
    return sliceArrays(in.posXYZ, in.nrmXYZ, in.rgb, in.vertexCount, in.indices, in.indexCount, opt);
}

std::vector<image::ImageData *> slicePrimitiveToLayers(const std::string &kind,
                                                       const SliceOptions &opt) {
    std::vector<float> pos, nrm;
    std::vector<uint32_t> idx;
    if (kind == "box") {
        makeBox(pos, nrm, idx);
    } else if (kind == "cylinder" || kind == "sphere" || kind == "cone") {
        makeLathe(kind, 32, pos, nrm, idx);
    } else {
        throw eve::Exception("SpriteStack.slicePrimitive: unknown kind '%s' (box|cylinder|sphere|cone)",
                             kind.c_str());
    }
    SliceInput in{};
    in.posXYZ = pos.data();
    in.nrmXYZ = nrm.data();
    in.vertexCount = int(pos.size() / 3);
    in.indices = idx.data();
    in.indexCount = int(idx.size());
    return sliceArrays(in.posXYZ, in.nrmXYZ, nullptr, in.vertexCount, in.indices, in.indexCount, opt);
}

// ---------------------------------------------------------------------------
// Legacy 3D renderer removed: SpriteStack is now a pure 2D technique. Keep the
// original implementation excluded temporarily so the slicer above remains a
// reviewable, behavior-preserving move while the 2D renderer lives in its own TU.
}  // namespace eve::spritestack
