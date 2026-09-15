#include "graphics/HairCards.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/HairShader.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Shader.h"

#include <cmath>

namespace eve::graphics::hair {
namespace {

void pushVertex(CardMeshData &mesh, float x, float y, float z, float nx, float ny, float nz,
                float u, float v) {
    mesh.positions.push_back(x);
    mesh.positions.push_back(y);
    mesh.positions.push_back(z);
    mesh.normals.push_back(nx);
    mesh.normals.push_back(ny);
    mesh.normals.push_back(nz);
    mesh.uvs.push_back(u);
    mesh.uvs.push_back(v);
}

void pushQuad(CardMeshData &mesh, float ax, float ay, float az, float bx, float by, float bz,
              float cx, float cy, float cz, float dx, float dy, float dz, float nx, float ny,
              float nz) {
    const uint32_t base = static_cast<uint32_t>(mesh.positions.size() / 3);
    pushVertex(mesh, ax, ay, az, nx, ny, nz, 0.f, 0.f);
    pushVertex(mesh, bx, by, bz, nx, ny, nz, 1.f, 0.f);
    pushVertex(mesh, cx, cy, cz, nx, ny, nz, 1.f, 1.f);
    pushVertex(mesh, dx, dy, dz, nx, ny, nz, 0.f, 1.f);
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 0);
    mesh.indices.push_back(base + 2);
    mesh.indices.push_back(base + 3);
}

void normalize3(float &x, float &y, float &z) {
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len <= 1e-8f) {
        x = 0.f;
        y = 1.f;
        z = 0.f;
        return;
    }
    x /= len;
    y /= len;
    z /= len;
}

void cross3(float ax, float ay, float az, float bx, float by, float bz, float &ox, float &oy,
            float &oz) {
    ox = ay * bz - az * by;
    oy = az * bx - ax * bz;
    oz = ax * by - ay * bx;
}

}  // namespace

CardMeshData buildCard(float width, float height) {
    CardMeshData mesh;
    const float w = width > 1e-4f ? width : 1e-4f;
    const float h = height > 1e-4f ? height : 1e-4f;
    const float hw = 0.5f * w;
    pushQuad(mesh, -hw, 0.f, 0.f, hw, 0.f, 0.f, hw, h, 0.f, -hw, h, 0.f, 0.f, 0.f, 1.f);
    return mesh;
}

CardMeshData buildCardsAlongPolyline(const float *pointsXYZ, int pointCount, float width) {
    CardMeshData mesh;
    if (!pointsXYZ || pointCount < 2) return mesh;
    const float w = width > 1e-4f ? width : 1e-4f;
    const float hw = 0.5f * w;
    for (int i = 0; i + 1 < pointCount; ++i) {
        const float *a = pointsXYZ + i * 3;
        const float *b = pointsXYZ + (i + 1) * 3;
        float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
        normalize3(dx, dy, dz);
        float sx = 0.f, sy = 0.f, sz = 0.f;
        cross3(dx, dy, dz, 0.f, 1.f, 0.f, sx, sy, sz);
        if (sx * sx + sy * sy + sz * sz < 1e-8f)
            cross3(dx, dy, dz, 1.f, 0.f, 0.f, sx, sy, sz);
        normalize3(sx, sy, sz);
        float nx = 0.f, ny = 0.f, nz = 0.f;
        cross3(sx, sy, sz, dx, dy, dz, nx, ny, nz);
        normalize3(nx, ny, nz);
        pushQuad(mesh, a[0] - sx * hw, a[1] - sy * hw, a[2] - sz * hw, a[0] + sx * hw,
                 a[1] + sy * hw, a[2] + sz * hw, b[0] + sx * hw, b[1] + sy * hw, b[2] + sz * hw,
                 b[0] - sx * hw, b[1] - sy * hw, b[2] - sz * hw, nx, ny, nz);
    }
    return mesh;
}

CardMeshData buildCardsFromRoots(const CardRoot *roots, int rootCount) {
    CardMeshData mesh;
    if (!roots || rootCount <= 0) return mesh;
    for (int i = 0; i < rootCount; ++i) {
        const CardRoot &r = roots[i];
        float dx = r.dirX, dy = r.dirY, dz = r.dirZ;
        normalize3(dx, dy, dz);
        const float len = r.length > 1e-4f ? r.length : 1e-4f;
        const float tip[3] = {r.x + dx * len, r.y + dy * len, r.z + dz * len};
        const float pts[6] = {r.x, r.y, r.z, tip[0], tip[1], tip[2]};
        appendCardMesh(mesh, buildCardsAlongPolyline(pts, 2, r.width));
    }
    return mesh;
}

void appendCardMesh(CardMeshData &dst, const CardMeshData &src) {
    const uint32_t base = static_cast<uint32_t>(dst.positions.size() / 3);
    dst.positions.insert(dst.positions.end(), src.positions.begin(), src.positions.end());
    dst.normals.insert(dst.normals.end(), src.normals.begin(), src.normals.end());
    dst.uvs.insert(dst.uvs.end(), src.uvs.begin(), src.uvs.end());
    for (uint32_t idx : src.indices) dst.indices.push_back(base + idx);
}

Mesh *uploadCardMesh(Graphics *gfx, const CardMeshData &data) {
    if (!gfx) throw eve::Exception("hair::uploadCardMesh: null graphics");
    if (data.positions.empty() || data.indices.empty()) return nullptr;
    const int vertexCount = static_cast<int>(data.positions.size() / 3);
    return gfx->newMeshFromArrays(data.positions.data(), data.normals.data(), data.uvs.data(),
                                  vertexCount, data.indices.data(),
                                  static_cast<int>(data.indices.size()));
}

Mesh *newCardMesh(Graphics *gfx, float width, float height) {
    return uploadCardMesh(gfx, buildCard(width, height));
}

void applyCardDefaults(Material &mat) {
    mat.setHair(true);
    mat.setShadingModel("hair");
    mat.setSurfaceMode("transparent");
    mat.setBlendMode("alpha");
    mat.setDoubleSided(true);
    mat.setDepthWrite(false);
    mat.setCastShadow(false);
    mat.setAlphaCutoff(0.15f);
    if (mat.getSortPriority() == 0) mat.setSortPriority(10);
}

Material *makeCardMaterial(Graphics *gfx, Texture *albedo, Shader *hairShader) {
    if (!gfx) throw eve::Exception("hair::makeCardMaterial: null graphics");
    auto *mat = new Material();
    applyCardDefaults(*mat);
    if (albedo) mat->setAlbedoTexture(albedo);
    Shader *shader = hairShader ? hairShader : createShader(gfx);
    mat->setShader(shader);
    return mat;
}

void configureCardLod(Renderable3D *renderable, Mesh *nearCards, Mesh *farProxy,
                      float switchDistance) {
    if (!renderable) throw eve::Exception("hair::configureCardLod: null renderable");
    if (!nearCards) throw eve::Exception("hair::configureCardLod: null near mesh");
    const float dist = switchDistance > 0.f ? switchDistance : 8.f;
    renderable->setMeshLod(0, nearCards, 0.f);
    if (farProxy) renderable->setMeshLod(1, farProxy, dist);
    renderable->setHair(true);
}

}  // namespace eve::graphics::hair
