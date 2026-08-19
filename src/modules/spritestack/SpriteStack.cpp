#include "spritestack/SpriteStack.h"

#include "common/Exception.h"
#include "common/ECS.h"
#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "model3d/ModelData.h"
#include "spritestack/shaders/sprite_stack_frag_spv.inc"
#include "spritestack/shaders/sprite_stack_vert_spv.inc"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
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
            if (am->mColors && am->mColors[0]) {
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
// SpriteStack3D
// ---------------------------------------------------------------------------

namespace {

std::vector<uint32_t> copySpv(const uint32_t *data, size_t count) {
    return std::vector<uint32_t>(data, data + count);
}

glm::mat4 billboardModel(const glm::vec3 &center, float width, float height,
                         const glm::vec3 &eye) {
    glm::vec3 d = eye - center;
    d.y = 0.f;
    if (glm::length(d) < 1e-4f) d = glm::vec3(0.f, 0.f, 1.f);
    d = glm::normalize(d);
    const glm::vec3 up(0.f, 1.f, 0.f);
    const glm::vec3 right = glm::normalize(glm::cross(up, d));
    const glm::vec3 forward = glm::normalize(glm::cross(right, up));
    glm::mat4 m(1.f);
    m[0] = glm::vec4(right * width, 0.f);
    m[1] = glm::vec4(up * height, 0.f);
    m[2] = glm::vec4(forward, 0.f);
    m[3] = glm::vec4(center, 1.f);
    return m;
}

graphics::Camera3D *findActiveCamera3D() {
    if (ecs::current()->getManager<graphics::Camera3D>() == nullptr) return nullptr;
    auto camView = ecs::View<graphics::Camera3D, graphics::Camera3D::Data>();
    for (auto it = camView.begin(); it != camView.end(); ++it) {
        auto [data] = *it;
        if (!data->active || !data->entity) continue;
        return data->entity;
    }
    return nullptr;
}

}  // namespace

void SpriteStack3D::setLayerCount(int count) {
    if (count < 0) throw eve::Exception("SpriteStack3D.setLayerCount: count must be >= 0");
    layerCount_ = count;
    layers_.assign(size_t(count), Layer{});
    bumpVersion();
}

int SpriteStack3D::getLayerCount() const { return layerCount_; }

void SpriteStack3D::setLayerTexture(graphics::Texture *texture, int index) {
    if (index < 0 || index >= layerCount_)
        throw eve::Exception("SpriteStack3D.setLayerTexture: index %d out of range [0,%d)", index,
                             layerCount_);
    layers_[size_t(index)] = Layer{texture, glm::vec4(0.f, 0.f, 1.f, 1.f)};
    bumpVersion();
}

graphics::Texture *SpriteStack3D::getLayerTexture(int index) const {
    if (index < 0 || index >= layerCount_) return nullptr;
    return layers_[size_t(index)].texture;
}

void SpriteStack3D::setLayerImage(graphics::Graphics *gfx, image::ImageData *img, int index) {
    if (!gfx) throw eve::Exception("SpriteStack3D.setLayerImage: null graphics");
    if (!img) throw eve::Exception("SpriteStack3D.setLayerImage: null ImageData");
    setLayerTexture(gfx->newTextureFromImageData(img, false, false), index);
}

void SpriteStack3D::setLayerFile(graphics::Graphics *gfx, const std::string &path, int index) {
    if (!gfx) throw eve::Exception("SpriteStack3D.setLayerFile: null graphics");
    setLayerTexture(gfx->newTextureFromFile(path), index);
}

void SpriteStack3D::setLayersFromAtlas(graphics::Graphics *gfx, graphics::Texture *atlas,
                                       int layerCount) {
    if (!gfx) throw eve::Exception("SpriteStack3D.setLayersFromAtlas: null graphics");
    if (!atlas) throw eve::Exception("SpriteStack3D.setLayersFromAtlas: null atlas");
    if (layerCount <= 0) throw eve::Exception("SpriteStack3D.setLayersFromAtlas: count must be > 0");
    setLayerCount(layerCount);
    for (int i = 0; i < layerCount; ++i) {
        const float u0 = float(i) / float(layerCount);
        const float u1 = float(i + 1) / float(layerCount);
        layers_[size_t(i)] = Layer{atlas, glm::vec4(u0, 0.f, u1, 1.f)};
    }
    bumpVersion();
}

void SpriteStack3D::setThickness(float thickness) {
    if (thickness <= 0.f) throw eve::Exception("SpriteStack3D.setThickness: must be > 0");
    thickness_ = thickness;
    bumpVersion();
}

float SpriteStack3D::getThickness() const { return thickness_; }

void SpriteStack3D::setSize(float width, float height) {
    if (width <= 0.f || height <= 0.f)
        throw eve::Exception("SpriteStack3D.setSize: size must be > 0");
    width_ = width;
    height_ = height;
    bumpVersion();
}

float SpriteStack3D::getWidth() const { return width_; }
float SpriteStack3D::getHeight() const { return height_; }

void SpriteStack3D::setPosition(float x, float y, float z) {
    x_ = x;
    y_ = y;
    z_ = z;
    bumpVersion();
}

void SpriteStack3D::setYaw(float yaw) {
    yaw_ = yaw;
    bumpVersion();
}

void SpriteStack3D::setTint(float r, float g, float b, float a) {
    tintR_ = r;
    tintG_ = g;
    tintB_ = b;
    tintA_ = a;
    bumpVersion();
}

void SpriteStack3D::setAlphaCutoff(float cutoff) { alphaCutoff_ = clampf(cutoff, 0.f, 1.f); }

void SpriteStack3D::setVisible(bool visible) {
    visible_ = visible;
    bumpVersion();
}

bool SpriteStack3D::getVisible() const { return visible_; }

void SpriteStack3D::setMode(const std::string &mode) {
    if (mode != "vertical" && mode != "horizontal")
        throw eve::Exception("SpriteStack3D.setMode: unknown mode '%s' (vertical|horizontal)",
                             mode.c_str());
    mode_ = mode;
    bumpVersion();
}

std::string SpriteStack3D::getMode() const { return mode_; }

void SpriteStack3D::setShadowEnabled(bool enabled) {
    shadowEnabled_ = enabled;
    bumpVersion();
}

bool SpriteStack3D::getShadowEnabled() const { return shadowEnabled_; }

void SpriteStack3D::setShadowOpacity(float opacity) {
    shadowOpacity_ = clampf(opacity, 0.f, 1.f);
    bumpVersion();
}

void SpriteStack3D::setShadowLight(float dx, float dy, float dz) {
    shadowLightX_ = dx;
    shadowLightY_ = dy;
    shadowLightZ_ = dz;
    bumpVersion();
}

void SpriteStack3D::setShadowPlaneY(float y) {
    shadowPlaneY_ = y;
    bumpVersion();
}

void SpriteStack3D::setOutline(float width, float r, float g, float b) {
    outlineWidth_ = std::max(0.f, width);
    outlineR_ = r;
    outlineG_ = g;
    outlineB_ = b;
    bumpVersion();
}

float SpriteStack3D::getOutlineWidth() const { return outlineWidth_; }

void SpriteStack3D::setOutlineColor(float r, float g, float b) {
    outlineR_ = r;
    outlineG_ = g;
    outlineB_ = b;
    bumpVersion();
}

void SpriteStack3D::ensureResources(graphics::Graphics *gfx) const {
    if (quad_ && shader_) return;
    if (!quad_) {
        const float posXYZ[] = {-0.5f, 0.5f, 0.f, 0.5f, 0.5f, 0.f,
                                0.5f, -0.5f, 0.f, -0.5f, -0.5f, 0.f};
        const float nrmXYZ[] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
                                0.f, 0.f, 1.f, 0.f, 0.f, 1.f};
        const float uvST[] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
        const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
        quad_ = gfx->newMeshFromArrays(posXYZ, nrmXYZ, uvST, 4, indices, 6);
    }
    if (!shader_) {
        auto vert = copySpv(sprite_stack_vert_spv, sprite_stack_vert_spv_count);
        auto frag = copySpv(sprite_stack_frag_spv, sprite_stack_frag_spv_count);
        shader_ = gfx->newHairShaderFromSpv(vert, frag);
        if (!shader_ || !shader_->gpuHandle)
            throw eve::Exception("SpriteStack3D.render: failed to create slice shader");
        shader_->declareVec4("uvRect");
        shader_->declareFloat("alphaCutoff");
        shader_->sendVec4("uvRect", 0.f, 0.f, 1.f, 1.f);
        shader_->sendFloat("alphaCutoff", alphaCutoff_);
    }
}

void SpriteStack3D::collectSlices(const SpriteStack3D &stack, const glm::vec3 &eye,
                                  std::vector<SliceDraw> &out) {
    if (!stack.visible_ || stack.layerCount_ <= 0) return;
    const glm::mat4 yawM = glm::rotate(glm::mat4(1.f), stack.yaw_, glm::vec3(0.f, 1.f, 0.f));
    const float half = 0.5f * float(stack.layerCount_ - 1);
    for (int i = 0; i < stack.layerCount_; ++i) {
        graphics::Texture *tex = stack.layers_[size_t(i)].texture;
        if (!tex) continue;
        const float offset = stack.thickness_ * (float(i) - half);
        glm::vec3 center(stack.x_, stack.y_, stack.z_);
        if (stack.mode_ == "horizontal") {
            center.y += offset;
        } else {
            const glm::vec4 off = yawM * glm::vec4(0.f, 0.f, offset, 0.f);
            center += glm::vec3(off);
        }
        SliceDraw s;
        const glm::vec3 d = center - eye;
        s.distSq = glm::dot(d, d);
        if (stack.mode_ == "horizontal") {
            // Horizontal top-down layer: rotate image by yaw, keep the quad
            // level so a 3/4 camera sees true volume.
            s.model = glm::translate(glm::mat4(1.f), center);
            s.model = glm::rotate(s.model, stack.yaw_, glm::vec3(0.f, 1.f, 0.f));
            s.model = glm::rotate(s.model, -kPi * 0.5f, glm::vec3(1.f, 0.f, 0.f));
            s.model = glm::scale(s.model, glm::vec3(stack.width_, stack.height_, 1.f));
        } else {
            s.model = billboardModel(center, stack.width_, stack.height_, eye);
        }
        s.texture = tex;
        s.uv = stack.layers_[size_t(i)].uv;
        out.push_back(s);
    }
}

SpriteStack3D::SliceDraw SpriteStack3D::makeShadowDraw(const SpriteStack3D &stack,
                                                       const SliceDraw &s,
                                                       const glm::vec3 &eye) {
    SliceDraw out = s;
    const glm::vec3 light =
        glm::normalize(glm::vec3(stack.shadowLightX_, stack.shadowLightY_, stack.shadowLightZ_));
    const glm::vec3 center(s.model[3]);
    // Lift the shadow above the ground so the depth test (strict LESS) cannot
    // reject it as coplanar with the ground surface.
    const float planeY = stack.shadowPlaneY_ + 0.02f;
    const float t = (planeY - center.y) / light.y;
    const glm::vec3 c = center + light * t;
    out.model = glm::translate(glm::mat4(1.f), c);
    out.model = glm::rotate(out.model, -kPi * 0.5f, glm::vec3(1.f, 0.f, 0.f));
    out.model = glm::scale(out.model, glm::vec3(stack.width_, stack.height_, 1.f));
    const glm::vec3 d = c - eye;
    out.distSq = glm::dot(d, d);
    return out;
}

SpriteStack3D::SliceDraw SpriteStack3D::makeOutlineDraw(const SpriteStack3D &stack,
                                                        const SliceDraw &s,
                                                        const glm::vec3 &eye) {
    SliceDraw out = s;
    const glm::vec3 center(s.model[3]);
    glm::vec3 away = center - eye;
    if (glm::length(away) < 1e-6f) away = glm::vec3(0.f, 0.f, 1.f);
    away = glm::normalize(away);
    // Push the outline slightly away from the camera so the slice draw (same
    // plane) still passes the strict-LESS depth test over it and only the rim
    // stays visible.
    out.model[3] = glm::vec4(center + away * 0.02f, 1.f);
    const float k = stack.outlineWidth_;
    out.model = out.model * glm::scale(glm::mat4(1.f), glm::vec3(1.f + 2.f * k, 1.f + 2.f * k, 1.f));
    return out;
}

std::vector<SpriteStack3D *> &SpriteStack3D::gbufferStacks() {
    static std::vector<SpriteStack3D *> stacks;
    return stacks;
}

void SpriteStack3D::registerGbufferDrawer() {
    static bool registered = false;
    if (registered) return;
    registered = true;
    eve::graphics::RenderSystem3D::addGBufferExtraDrawer(
        [](eve::graphics::Graphics &gfx, const eve::graphics::Camera3D::Data &cam,
           const glm::mat4 &viewProj, float /*aspect*/) {
            drawGbufferStacks(gfx, viewProj, cam.eyeX, cam.eyeY, cam.eyeZ, cam.nearZ, cam.farZ);
        });
}

void SpriteStack3D::drawGbufferStacks(eve::graphics::Graphics &gfx, const glm::mat4 &viewProj,
                                      float eyeX, float eyeY, float eyeZ, float nearZ,
                                      float farZ) {
    const glm::vec3 eye(eyeX, eyeY, eyeZ);
    std::vector<SliceDraw> slices;
    for (SpriteStack3D *st : gbufferStacks()) {
        if (!st || !st->visible_ || st->layerCount_ <= 0) continue;
        st->ensureResources(&gfx);
        if (!st->quad_) continue;
        slices.clear();
        collectSlices(*st, eye, slices);
        for (const auto &s : slices) {
            const glm::mat4 mvp = viewProj * s.model;
            gfx.drawMeshGBufferAlpha(st->quad_, mvp, s.model, nearZ, farZ, s.texture,
                                     st->tintR_, st->tintG_, st->tintB_);
        }
    }
}

void SpriteStack3D::setGbufferEnabled(bool enabled) {
    gbufferEnabled_ = enabled;
    auto &stacks = gbufferStacks();
    auto it = std::find(stacks.begin(), stacks.end(), this);
    if (enabled && it == stacks.end()) stacks.push_back(this);
    if (!enabled && it != stacks.end()) stacks.erase(it);
    registerGbufferDrawer();
}

bool SpriteStack3D::getGbufferEnabled() const { return gbufferEnabled_; }

std::vector<SpriteStack3D *> &SpriteStack3D::shadowCasterStacks() {
    static std::vector<SpriteStack3D *> stacks;
    return stacks;
}

void SpriteStack3D::registerShadowDrawer() {
    static bool registered = false;
    if (registered) return;
    registered = true;
    eve::graphics::RenderSystem3D::addShadowExtraDrawer(
        [](eve::graphics::Graphics &gfx, const glm::mat4 &lightVP,
           const eve::graphics::Camera3D::Data &cam) {
            drawShadowCasterStacks(gfx, lightVP, cam.eyeX, cam.eyeY, cam.eyeZ);
        });
}

void SpriteStack3D::drawShadowCasterStacks(eve::graphics::Graphics &gfx,
                                           const glm::mat4 &lightVP, float eyeX, float eyeY,
                                           float eyeZ) {
    const glm::vec3 eye(eyeX, eyeY, eyeZ);
    std::vector<SliceDraw> slices;
    for (SpriteStack3D *st : shadowCasterStacks()) {
        if (!st || !st->visible_ || st->layerCount_ <= 0) continue;
        st->ensureResources(&gfx);
        if (!st->quad_) continue;
        slices.clear();
        collectSlices(*st, eye, slices);
        for (const auto &s : slices) {
            // Camera-facing cards cast their silhouette into the light view;
            // the alpha-cutout shadow pipeline keeps the shape instead of a
            // solid quad.
            gfx.drawMeshShadowAlpha(st->quad_, lightVP * s.model, s.texture);
        }
    }
}

void SpriteStack3D::setCastShadow(bool cast) {
    castShadow_ = cast;
    auto &stacks = shadowCasterStacks();
    auto it = std::find(stacks.begin(), stacks.end(), this);
    if (cast && it == stacks.end()) stacks.push_back(this);
    if (!cast && it != stacks.end()) stacks.erase(it);
    registerShadowDrawer();
}

bool SpriteStack3D::getCastShadow() const { return castShadow_; }

SpriteStack3D::~SpriteStack3D() {
    auto &gb = gbufferStacks();
    auto it = std::find(gb.begin(), gb.end(), this);
    if (it != gb.end()) gb.erase(it);
    auto &sc = shadowCasterStacks();
    it = std::find(sc.begin(), sc.end(), this);
    if (it != sc.end()) sc.erase(it);
}

void SpriteStack3D::render(graphics::Graphics *gfx, graphics::Camera3D *camera) const {
    if (!gfx) throw eve::Exception("SpriteStack3D.render: null graphics");
    if (!visible_ || layerCount_ <= 0) return;
    if (!camera) camera = findActiveCamera3D();
    if (!camera) return;

    ensureResources(gfx);
    if (!quad_ || !shader_) return;

    auto cd = camera->data();
    const glm::vec3 eye(cd->eyeX, cd->eyeY, cd->eyeZ);
    const glm::vec3 target(cd->targetX, cd->targetY, cd->targetZ);
    const glm::vec3 up(cd->upX, cd->upY, cd->upZ);
    const float aspect = gfx->getHeight() > 0 ? float(gfx->getWidth()) / float(gfx->getHeight()) : 1.f;
    const glm::mat4 viewM = glm::lookAtRH(eye, target, up);
    const float fovRad = cd->fovYDeg * 0.017453292519943295f;
    const glm::mat4 projM =
        eve::graphics::perspectiveVulkanRH_ZO(fovRad, aspect, cd->nearZ, cd->farZ);
    gfx->setMesh3DViewProj(projM * viewM);
    gfx->setMesh3DView(viewM);
    gfx->setMesh3DClip(cd->nearZ, cd->farZ);
    gfx->setMesh3DCameraPos(eye);
    gfx->setMesh3DEnv(cd->envMap, cd->envIntensity);
    shader_->sendFloat("alphaCutoff", alphaCutoff_);

    std::vector<SliceDraw> slices;
    slices.reserve(size_t(layerCount_));
    collectSlices(*this, eye, slices);
    if (slices.empty()) return;

    // Far-to-near: alpha-blended slices need back-to-front draw order.
    std::sort(slices.begin(), slices.end(),
              [](const SliceDraw &a, const SliceDraw &b) { return a.distSq > b.distSq; });

    // Projected contact shadows: each slice silhouette squashed onto the ground
    // plane along the light direction, painted as a dark alpha blob.
    if (shadowEnabled_ && shadowLightY_ < -1e-4f) {
        const eve::graphics::Color shadowColor(0.f, 0.f, 0.f, shadowOpacity_);
        for (const auto &s : slices) {
            const SliceDraw shadow = makeShadowDraw(*this, s, eye);
            shader_->sendVec4("uvRect", s.uv.x, s.uv.y, s.uv.z, s.uv.w);
            gfx->drawMeshShader(quad_, shadow.model, shadow.texture, shadowColor, shader_);
        }
    }

    // Stylized rim outline: expanded dark silhouettes behind the stack.
    if (outlineWidth_ > 0.f) {
        const eve::graphics::Color outlineColor(outlineR_, outlineG_, outlineB_, 1.f);
        for (const auto &s : slices) {
            const SliceDraw outline = makeOutlineDraw(*this, s, eye);
            shader_->sendVec4("uvRect", s.uv.x, s.uv.y, s.uv.z, s.uv.w);
            gfx->drawMeshShader(quad_, outline.model, outline.texture, outlineColor, shader_);
        }
    }

    const glm::vec4 tint(tintR_, tintG_, tintB_, tintA_);
    for (const auto &s : slices) {
        shader_->sendVec4("uvRect", s.uv.x, s.uv.y, s.uv.z, s.uv.w);
        gfx->drawMeshShader(quad_, s.model, s.texture,
                            eve::graphics::Color(tint.r, tint.g, tint.b, tint.a),
                            shader_);
    }
}

// ---------------------------------------------------------------------------
// SpriteStackBatch
// ---------------------------------------------------------------------------

namespace {

uint32_t packTint(float r, float g, float b, float a) {
    return (uint32_t(clampf(r, 0.f, 1.f) * 255.f) << 24) |
           (uint32_t(clampf(g, 0.f, 1.f) * 255.f) << 16) |
           (uint32_t(clampf(b, 0.f, 1.f) * 255.f) << 8) |
           uint32_t(clampf(a, 0.f, 1.f) * 255.f);
}

eve::graphics::Color unpackTint(uint32_t t) {
    return eve::graphics::Color(float((t >> 24) & 0xff) / 255.f,
                                float((t >> 16) & 0xff) / 255.f,
                                float((t >> 8) & 0xff) / 255.f, float(t & 0xff) / 255.f);
}

}  // namespace

void SpriteStackBatch::add(SpriteStack3D *stack) {
    if (!stack) throw eve::Exception("SpriteStackBatch.add: null stack");
    if (std::find(stacks_.begin(), stacks_.end(), stack) == stacks_.end()) {
        stacks_.push_back(stack);
        forceRebuild_ = true;
    }
}

void SpriteStackBatch::remove(SpriteStack3D *stack) {
    auto it = std::find(stacks_.begin(), stacks_.end(), stack);
    if (it != stacks_.end()) {
        stacks_.erase(it);
        forceRebuild_ = true;
    }
}

void SpriteStackBatch::clear() {
    stacks_.clear();
    groups_.clear();
    forceRebuild_ = true;
}

int SpriteStackBatch::getStackCount() const { return int(stacks_.size()); }

void SpriteStackBatch::ensureShader(graphics::Graphics *gfx) {
    if (shader_) return;
    auto vert = copySpv(sprite_stack_vert_spv, sprite_stack_vert_spv_count);
    auto frag = copySpv(sprite_stack_frag_spv, sprite_stack_frag_spv_count);
    shader_ = gfx->newHairShaderFromSpv(vert, frag);
    if (!shader_ || !shader_->gpuHandle)
        throw eve::Exception("SpriteStackBatch.render: failed to create slice shader");
    shader_->declareVec4("uvRect");
    shader_->declareFloat("alphaCutoff");
    shader_->sendVec4("uvRect", 0.f, 0.f, 1.f, 1.f);
    shader_->sendFloat("alphaCutoff", 0.05f);
}

void SpriteStackBatch::render(graphics::Graphics *gfx, graphics::Camera3D *camera) {
    if (!gfx) throw eve::Exception("SpriteStackBatch.render: null graphics");
    if (stacks_.empty()) return;
    if (!camera) camera = findActiveCamera3D();
    if (!camera) return;

    ensureShader(gfx);
    if (!shader_) return;

    auto cd = camera->data();
    const glm::vec3 eye(cd->eyeX, cd->eyeY, cd->eyeZ);
    const glm::vec3 target(cd->targetX, cd->targetY, cd->targetZ);
    const glm::vec3 up(cd->upX, cd->upY, cd->upZ);
    const float aspect = gfx->getHeight() > 0 ? float(gfx->getWidth()) / float(gfx->getHeight()) : 1.f;
    const glm::mat4 viewM = glm::lookAtRH(eye, target, up);
    const float fovRad = cd->fovYDeg * 0.017453292519943295f;
    const glm::mat4 projM =
        eve::graphics::perspectiveVulkanRH_ZO(fovRad, aspect, cd->nearZ, cd->farZ);
    gfx->setMesh3DViewProj(projM * viewM);
    gfx->setMesh3DView(viewM);
    gfx->setMesh3DClip(cd->nearZ, cd->farZ);
    gfx->setMesh3DCameraPos(eye);
    gfx->setMesh3DEnv(cd->envMap, cd->envIntensity);
    shader_->sendFloat("alphaCutoff", 0.05f);
    shader_->sendVec4("uvRect", 0.f, 0.f, 1.f, 1.f);  // UVs are baked into the mesh

    struct GroupBuild {
        GroupKey key{};
        struct ColoredSlice {
            SpriteStack3D::SliceDraw draw;
            eve::graphics::Color color{1.f, 1.f, 1.f, 1.f};
        };
        std::vector<ColoredSlice> slices;
        uint64_t stamp = 0;
        float nearest = 1e30f;
        eve::graphics::Color color{1.f, 1.f, 1.f, 1.f};
    };
    std::unordered_map<GroupKey, GroupBuild, GroupKeyHash> builds;
    for (SpriteStack3D *st : stacks_) {
        if (!st || !st->visible_) continue;
        std::vector<SpriteStack3D::SliceDraw> tmp;
        SpriteStack3D::collectSlices(*st, eye, tmp);
        const uint64_t stamp = st->getVersion();
        std::vector<GroupBuild::ColoredSlice> colored;
        const eve::graphics::Color baseColor(st->tintR_, st->tintG_, st->tintB_, st->tintA_);
        for (const auto &s : tmp) colored.push_back({s, baseColor});
        if (st->shadowEnabled_ && st->shadowLightY_ < -1e-4f) {
            const eve::graphics::Color shadowColor(0.f, 0.f, 0.f, st->shadowOpacity_);
            for (const auto &s : tmp)
                colored.push_back(
                    {SpriteStack3D::makeShadowDraw(*st, s, eye), shadowColor});
        }
        if (st->outlineWidth_ > 0.f) {
            const eve::graphics::Color outlineColor(st->outlineR_, st->outlineG_, st->outlineB_,
                                                    1.f);
            for (const auto &s : tmp)
                colored.push_back(
                    {SpriteStack3D::makeOutlineDraw(*st, s, eye), outlineColor});
        }
        for (const auto &cs : colored) {
            GroupKey key{cs.draw.texture,
                         packTint(cs.color.r, cs.color.g, cs.color.b, cs.color.a)};
            GroupBuild &b = builds[key];
            b.key = key;
            b.color = unpackTint(key.tint);
            b.slices.push_back(cs);
            b.stamp = std::max(b.stamp, stamp);
            b.nearest = std::min(b.nearest, cs.draw.distSq);
        }
    }
    if (builds.empty()) return;

    // Draw far groups first; inside each group slices are baked far-to-near.
    std::vector<GroupBuild *> order;
    order.reserve(builds.size());
    for (auto &kv : builds) order.push_back(&kv.second);
    std::sort(order.begin(), order.end(),
              [](const GroupBuild *a, const GroupBuild *b) { return a->nearest > b->nearest; });

    const glm::vec3 corners[4] = {{-0.5f, 0.5f, 0.f}, {0.5f, 0.5f, 0.f},
                                  {0.5f, -0.5f, 0.f}, {-0.5f, -0.5f, 0.f}};
    const glm::vec2 quadUVs[4] = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}};
    std::vector<float> pos, nrm, uv;
    std::vector<uint32_t> idx;

    for (GroupBuild *gb : order) {
        Group &g = groups_[gb->key];
        std::sort(gb->slices.begin(), gb->slices.end(),
                  [](const GroupBuild::ColoredSlice &a, const GroupBuild::ColoredSlice &b) {
                      return a.draw.distSq > b.draw.distSq;
                  });
        const int sliceCount = int(gb->slices.size());
        const int vc = sliceCount * 4;
        const int ic = sliceCount * 6;

        pos.resize(size_t(vc) * 3);
        nrm.resize(size_t(vc) * 3);
        uv.resize(size_t(vc) * 2);
        idx.resize(size_t(ic));
        for (int si = 0; si < sliceCount; ++si) {
            const SpriteStack3D::SliceDraw &s = gb->slices[size_t(si)].draw;
            for (int c = 0; c < 4; ++c) {
                const glm::vec4 p = s.model * glm::vec4(corners[c], 1.f);
                const size_t vi = size_t(si * 4 + c);
                pos[vi * 3 + 0] = p.x;
                pos[vi * 3 + 1] = p.y;
                pos[vi * 3 + 2] = p.z;
                nrm[vi * 3 + 0] = 0.f;
                nrm[vi * 3 + 1] = 1.f;
                nrm[vi * 3 + 2] = 0.f;
                uv[vi * 2 + 0] = s.uv.x + quadUVs[c].x * (s.uv.z - s.uv.x);
                uv[vi * 2 + 1] = s.uv.y + quadUVs[c].y * (s.uv.w - s.uv.y);
            }
            const uint32_t base = uint32_t(si * 4);
            const size_t di = size_t(si) * 6;
            idx[di + 0] = base;
            idx[di + 1] = base + 1;
            idx[di + 2] = base + 2;
            idx[di + 3] = base;
            idx[di + 4] = base + 2;
            idx[di + 5] = base + 3;
        }

        if (forceRebuild_ || !g.mesh) {
            if (g.mesh) {
                gfx->updateMeshVertices(g.mesh, pos.data(), nrm.data(), uv.data(), vc, idx.data(),
                                        ic);
            } else {
                g.mesh =
                    gfx->newMeshFromArrays(pos.data(), nrm.data(), uv.data(), vc, idx.data(), ic);
            }
            g.vertexCapacity = vc;
            g.indexCapacity = ic;
        } else if (g.stamp != gb->stamp) {
            gfx->updateMeshVertices(g.mesh, pos.data(), nrm.data(), uv.data(), vc, idx.data(), ic);
        }
        g.stamp = gb->stamp;
        gfx->drawMeshShader(g.mesh, glm::mat4(1.f), gb->key.texture, gb->color, shader_);
    }
    forceRebuild_ = false;
}

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

Module_IMPL(SpriteStack, new SpriteStack());

SpriteStack3D *SpriteStack::newStack(graphics::Graphics *gfx) {
    if (!gfx) throw eve::Exception("SpriteStack.newStack: null graphics");
    return new SpriteStack3D();
}

SpriteStackBatch *SpriteStack::newBatch(graphics::Graphics *gfx) {
    if (!gfx) throw eve::Exception("SpriteStack.newBatch: null graphics");
    return new SpriteStackBatch();
}

std::vector<image::ImageData *> SpriteStack::slicePrimitive(const std::string &kind, int layerCount,
                                                            int imageW, int imageH,
                                                            const std::string &axis,
                                                            float thickness) {
    SliceOptions opt;
    opt.layerCount = layerCount;
    opt.imageW = imageW;
    opt.imageH = imageH;
    opt.axis = axis;
    opt.thickness = thickness;
    return slicePrimitiveToLayers(kind, opt);
}

std::vector<image::ImageData *> SpriteStack::sliceModel(model3d::ModelData *model, int layerCount,
                                                        int imageW, int imageH,
                                                        const std::string &axis, float thickness) {
    SliceOptions opt;
    opt.layerCount = layerCount;
    opt.imageW = imageW;
    opt.imageH = imageH;
    opt.axis = axis;
    opt.thickness = thickness;
    return sliceModelToLayers(model, opt);
}

void SpriteStack::expose(ssq::Table &table) {
    auto cls = table.addClass(name, SpriteStack::create, false);
    expose(cls);

    auto stack = table.addClass<SpriteStack3D>(
        "SpriteStack3D",
        std::function<SpriteStack3D *()>([]() -> SpriteStack3D * { return nullptr; }), true);
    stack.addFunc("setLayerCount", &SpriteStack3D::setLayerCount);
    stack.addFunc("getLayerCount", &SpriteStack3D::getLayerCount);
    stack.addFunc("setLayerTexture", &SpriteStack3D::setLayerTexture);
    stack.addFunc("getLayerTexture", &SpriteStack3D::getLayerTexture);
    stack.addFunc("setLayerImage", &SpriteStack3D::setLayerImage);
    stack.addFunc("setLayerFile", &SpriteStack3D::setLayerFile);
    stack.addFunc("setLayersFromAtlas", &SpriteStack3D::setLayersFromAtlas);
    stack.addFunc("setThickness", &SpriteStack3D::setThickness);
    stack.addFunc("getThickness", &SpriteStack3D::getThickness);
    stack.addFunc("setSize", &SpriteStack3D::setSize);
    stack.addFunc("getWidth", &SpriteStack3D::getWidth);
    stack.addFunc("getHeight", &SpriteStack3D::getHeight);
    stack.addFunc("setPosition", &SpriteStack3D::setPosition);
    stack.addFunc("setYaw", &SpriteStack3D::setYaw);
    stack.addFunc("setTint", &SpriteStack3D::setTint);
    stack.addFunc("setAlphaCutoff", &SpriteStack3D::setAlphaCutoff);
    stack.addFunc("setVisible", &SpriteStack3D::setVisible);
    stack.addFunc("getVisible", &SpriteStack3D::getVisible);
    stack.addFunc("setMode", &SpriteStack3D::setMode);
    stack.addFunc("getMode", &SpriteStack3D::getMode);
    stack.addFunc("setShadowEnabled", &SpriteStack3D::setShadowEnabled);
    stack.addFunc("getShadowEnabled", &SpriteStack3D::getShadowEnabled);
    stack.addFunc("setShadowOpacity", &SpriteStack3D::setShadowOpacity);
    stack.addFunc("setShadowLight", &SpriteStack3D::setShadowLight);
    stack.addFunc("setShadowPlaneY", &SpriteStack3D::setShadowPlaneY);
    stack.addFunc("setOutline", &SpriteStack3D::setOutline);
    stack.addFunc("getOutlineWidth", &SpriteStack3D::getOutlineWidth);
    stack.addFunc("setOutlineColor", &SpriteStack3D::setOutlineColor);
    stack.addFunc("setGbufferEnabled", &SpriteStack3D::setGbufferEnabled);
    stack.addFunc("getGbufferEnabled", &SpriteStack3D::getGbufferEnabled);
    stack.addFunc("setCastShadow", &SpriteStack3D::setCastShadow);
    stack.addFunc("getCastShadow", &SpriteStack3D::getCastShadow);
    stack.addFunc(
        "render",
        std::function<void(SpriteStack3D *, graphics::Graphics *)>([](SpriteStack3D *self,
                                                                       graphics::Graphics *gfx) {
            if (self) self->render(gfx);
        }));
    stack.addFunc("renderWithCamera", &SpriteStack3D::render);

    auto batch = table.addClass<SpriteStackBatch>(
        "SpriteStackBatch",
        std::function<SpriteStackBatch *()>([]() -> SpriteStackBatch * { return nullptr; }), true);
    batch.addFunc("add", &SpriteStackBatch::add);
    batch.addFunc("remove", &SpriteStackBatch::remove);
    batch.addFunc("clear", &SpriteStackBatch::clear);
    batch.addFunc("getStackCount", &SpriteStackBatch::getStackCount);
    batch.addFunc(
        "render",
        std::function<void(SpriteStackBatch *, graphics::Graphics *)>([](SpriteStackBatch *self,
                                                                         graphics::Graphics *gfx) {
            if (self) self->render(gfx);
        }));
    batch.addFunc("renderWithCamera", &SpriteStackBatch::render);
}

void SpriteStack::expose(ssq::Class &cls) {
    cls.addFunc("getName", &SpriteStack::getName);
    cls.addFunc("newStack", &SpriteStack::newStack);
    cls.addFunc("newBatch", &SpriteStack::newBatch);
    cls.addFunc("slicePrimitive", &SpriteStack::slicePrimitive);
    cls.addFunc("sliceModel", &SpriteStack::sliceModel);
}

}  // namespace eve::spritestack
