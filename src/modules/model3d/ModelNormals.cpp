#include "model3d/ModelData.h"

#include "common/Diagnostic.h"
#include "common/Result.h"
#include "image/ImageData.h"

#include <assimp/mesh.h>
#include <assimp/vector3.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace eve {
namespace model3d {
namespace {

eve::Diagnostic invalidArg(std::string message, std::string path) {
    return eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message),
                                  std::move(path));
}

eve::Diagnostic unsupported(std::string message, std::string path) {
    return eve::Diagnostic::error(eve::DiagnosticCode::Unsupported, std::move(message),
                                  std::move(path));
}

aiVector3D unitOrUp(float x, float y, float z) {
    const float length = std::sqrt(x * x + y * y + z * z);
    if (!std::isfinite(length) || length <= 1e-12f)
        return aiVector3D(0.f, 0.f, 1.f);
    return aiVector3D(x / length, y / length, z / length);
}

bool ensureNormals(aiMesh *mesh) {
    if (!mesh || mesh->mNumVertices == 0 || !mesh->mVertices)
        return false;
    if (mesh->mNormals)
        return true;
    mesh->mNormals = new aiVector3D[mesh->mNumVertices];
    for (unsigned i = 0; i < mesh->mNumVertices; ++i)
        mesh->mNormals[i] = aiVector3D(0.f, 0.f, 1.f);
    return true;
}

bool meshAabbCenter(const aiMesh *mesh, aiVector3D *out) {
    if (!mesh || mesh->mNumVertices == 0 || !mesh->mVertices)
        return false;
    aiVector3D lo = mesh->mVertices[0];
    aiVector3D hi = mesh->mVertices[0];
    for (unsigned i = 1; i < mesh->mNumVertices; ++i) {
        const aiVector3D &p = mesh->mVertices[i];
        lo.x = std::min(lo.x, p.x);
        lo.y = std::min(lo.y, p.y);
        lo.z = std::min(lo.z, p.z);
        hi.x = std::max(hi.x, p.x);
        hi.y = std::max(hi.y, p.y);
        hi.z = std::max(hi.z, p.z);
    }
    *out = aiVector3D((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f);
    return true;
}

void applyRadial(aiMesh *mesh, const aiVector3D &origin) {
    for (unsigned i = 0; i < mesh->mNumVertices; ++i) {
        const aiVector3D &p = mesh->mVertices[i];
        mesh->mNormals[i] = unitOrUp(p.x - origin.x, p.y - origin.y, p.z - origin.z);
    }
}

struct Vec2 {
    float x = 0.f;
    float y = 0.f;
};

struct Vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

Vec3 add(const Vec3 &a, const Vec3 &b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(const Vec3 &a, const Vec3 &b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(const Vec3 &a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot3(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross3(const Vec3 &a, const Vec3 &b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 normalize3(const Vec3 &a) {
    const aiVector3D n = unitOrUp(a.x, a.y, a.z);
    return {n.x, n.y, n.z};
}

bool barycentric2(const Vec2 &p, const Vec2 &a, const Vec2 &b, const Vec2 &c, float *wa, float *wb,
                  float *wc) {
    const float den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (!std::isfinite(den) || std::fabs(den) <= 1e-12f)
        return false;
    *wa = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / den;
    *wb = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / den;
    *wc = 1.f - *wa - *wb;
    return true;
}

image::ImageData::Colorf encodeNormal(const Vec3 &n) {
    image::ImageData::Colorf c;
    c.r = std::clamp(n.x * 0.5f + 0.5f, 0.f, 1.f);
    c.g = std::clamp(n.y * 0.5f + 0.5f, 0.f, 1.f);
    c.b = std::clamp(n.z * 0.5f + 0.5f, 0.f, 1.f);
    c.a = 1.f;
    return c;
}

Vec3 toTangent(const Vec3 &objectNormal, const Vec3 &pos0, const Vec3 &pos1, const Vec3 &pos2,
               const Vec2 &uv0, const Vec2 &uv1, const Vec2 &uv2) {
    const Vec3 e1 = sub(pos1, pos0);
    const Vec3 e2 = sub(pos2, pos0);
    const float du1 = uv1.x - uv0.x;
    const float dv1 = uv1.y - uv0.y;
    const float du2 = uv2.x - uv0.x;
    const float dv2 = uv2.y - uv0.y;
    const float det = du1 * dv2 - du2 * dv1;
    Vec3 geometric = normalize3(cross3(e1, e2));
    if (dot3(geometric, objectNormal) < 0.f)
        geometric = scale(geometric, -1.f);
    Vec3 tangent;
    Vec3 bitangent;
    if (!std::isfinite(det) || std::fabs(det) <= 1e-12f) {
        tangent = {1.f, 0.f, 0.f};
        if (std::fabs(dot3(tangent, geometric)) > 0.9f)
            tangent = {0.f, 1.f, 0.f};
        tangent = normalize3(sub(tangent, scale(geometric, dot3(tangent, geometric))));
        bitangent = normalize3(cross3(geometric, tangent));
    } else {
        const float inv = 1.f / det;
        tangent = normalize3(add(scale(e1, dv2 * inv), scale(e2, -dv1 * inv)));
        bitangent = normalize3(add(scale(e1, -du2 * inv), scale(e2, du1 * inv)));
        tangent = normalize3(sub(tangent, scale(geometric, dot3(tangent, geometric))));
        const Vec3 reconstructed = cross3(geometric, tangent);
        if (dot3(reconstructed, bitangent) < 0.f)
            bitangent = scale(reconstructed, -1.f);
        else
            bitangent = reconstructed;
    }
    return {dot3(objectNormal, tangent), dot3(objectNormal, bitangent),
            dot3(objectNormal, geometric)};
}

}  // namespace

eve::Result<void> ModelData::setVertexNormal(int meshIndex, int vertexIndex, float x, float y,
                                             float z) {
    aiMesh *mesh = meshAtMutable(meshIndex);
    if (!mesh || vertexIndex < 0 || static_cast<unsigned>(vertexIndex) >= mesh->mNumVertices)
        return eve::Result<void>::failure(
            invalidArg("invalid mesh or vertex index", "model3d.setVertexNormal"));
    if (!ensureNormals(mesh))
        return eve::Result<void>::failure(
            invalidArg("mesh has no vertices", "model3d.setVertexNormal"));
    mesh->mNormals[vertexIndex] = unitOrUp(x, y, z);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ModelData::applyVertexNormals(int meshIndex, std::string_view kind) {
    aiMesh *mesh = meshAtMutable(meshIndex);
    if (!mesh)
        return eve::Result<void>::failure(
            invalidArg("invalid mesh index", "model3d.applyVertexNormals"));
    aiVector3D origin;
    if (!meshAabbCenter(mesh, &origin))
        return eve::Result<void>::failure(
            invalidArg("mesh has no vertices", "model3d.applyVertexNormals"));
    return applyVertexNormalsFrom(meshIndex, kind, origin.x, origin.y, origin.z);
}

eve::Result<void> ModelData::applyVertexNormalsFrom(int meshIndex, std::string_view kind,
                                                    float originX, float originY, float originZ) {
    aiMesh *mesh = meshAtMutable(meshIndex);
    if (!mesh)
        return eve::Result<void>::failure(
            invalidArg("invalid mesh index", "model3d.applyVertexNormalsFrom"));
    if (kind != "radial")
        return eve::Result<void>::failure(
            unsupported("unknown vertex-normal kind", "model3d.applyVertexNormalsFrom.kind"));
    if (!ensureNormals(mesh))
        return eve::Result<void>::failure(
            invalidArg("mesh has no vertices", "model3d.applyVertexNormalsFrom"));
    applyRadial(mesh, aiVector3D(originX, originY, originZ));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::unique_ptr<image::ImageData>> ModelData::bakeNormalMap(
    int meshIndex, int width, int height, int uvChannel, std::string_view space) const {
    using ImagePtr = std::unique_ptr<image::ImageData>;
    const aiMesh *mesh = meshAt(meshIndex);
    if (!mesh)
        return eve::Result<ImagePtr>::failure(
            invalidArg("invalid mesh index", "model3d.bakeNormalMap"));
    if (width <= 0 || height <= 0)
        return eve::Result<ImagePtr>::failure(
            invalidArg("normal map size must be positive", "model3d.bakeNormalMap.size"));
    if (space != "tangent" && space != "object")
        return eve::Result<ImagePtr>::failure(
            unsupported("space must be \"tangent\" or \"object\"", "model3d.bakeNormalMap.space"));
    if (!mesh->HasNormals())
        return eve::Result<ImagePtr>::failure(unsupported(
            "mesh has no vertex normals; apply or set them before baking",
            "model3d.bakeNormalMap.normals"));
    if (uvChannel < 0 || uvChannel >= AI_MAX_NUMBER_OF_TEXTURECOORDS ||
        !mesh->HasTextureCoords(static_cast<unsigned>(uvChannel)))
        return eve::Result<ImagePtr>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "requested UV channel is not present",
                                   "model3d.bakeNormalMap.channel"));

    for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
        if (mesh->mFaces[f].mNumIndices != 3)
            return eve::Result<ImagePtr>::failure(unsupported(
                "normal-map baking requires triangulated faces", "model3d.bakeNormalMap.triangle"));
    }

    auto image = std::make_unique<image::ImageData>(width, height, "RGBA8");
    image::ImageData::Colorf fill;
    fill.r = 0.5f;
    fill.g = 0.5f;
    fill.b = 1.f;
    fill.a = 1.f;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            image->setPixel(x, y, fill);

    const float widthF = static_cast<float>(width);
    const float heightF = static_cast<float>(height);
    const bool tangentSpace = space == "tangent";

    for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
        const aiFace &face = mesh->mFaces[f];
        const unsigned i0 = face.mIndices[0];
        const unsigned i1 = face.mIndices[1];
        const unsigned i2 = face.mIndices[2];
        const Vec3 p0{mesh->mVertices[i0].x, mesh->mVertices[i0].y, mesh->mVertices[i0].z};
        const Vec3 p1{mesh->mVertices[i1].x, mesh->mVertices[i1].y, mesh->mVertices[i1].z};
        const Vec3 p2{mesh->mVertices[i2].x, mesh->mVertices[i2].y, mesh->mVertices[i2].z};
        const Vec3 n0{mesh->mNormals[i0].x, mesh->mNormals[i0].y, mesh->mNormals[i0].z};
        const Vec3 n1{mesh->mNormals[i1].x, mesh->mNormals[i1].y, mesh->mNormals[i1].z};
        const Vec3 n2{mesh->mNormals[i2].x, mesh->mNormals[i2].y, mesh->mNormals[i2].z};
        const aiVector3D &t0 = mesh->mTextureCoords[uvChannel][i0];
        const aiVector3D &t1 = mesh->mTextureCoords[uvChannel][i1];
        const aiVector3D &t2 = mesh->mTextureCoords[uvChannel][i2];
        const Vec2 uv0{t0.x, t0.y};
        const Vec2 uv1{t1.x, t1.y};
        const Vec2 uv2{t2.x, t2.y};
        const Vec2 pix0{uv0.x * widthF, (1.f - uv0.y) * heightF};
        const Vec2 pix1{uv1.x * widthF, (1.f - uv1.y) * heightF};
        const Vec2 pix2{uv2.x * widthF, (1.f - uv2.y) * heightF};

        const int minX = std::max(0, static_cast<int>(std::floor(std::min({pix0.x, pix1.x, pix2.x}))));
        const int maxX =
            std::min(width - 1, static_cast<int>(std::ceil(std::max({pix0.x, pix1.x, pix2.x}))));
        const int minY = std::max(0, static_cast<int>(std::floor(std::min({pix0.y, pix1.y, pix2.y}))));
        const int maxY =
            std::min(height - 1, static_cast<int>(std::ceil(std::max({pix0.y, pix1.y, pix2.y}))));

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const Vec2 sample{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
                float wa = 0.f, wb = 0.f, wc = 0.f;
                if (!barycentric2(sample, pix0, pix1, pix2, &wa, &wb, &wc))
                    continue;
                constexpr float kEps = -1e-4f;
                if (wa < kEps || wb < kEps || wc < kEps)
                    continue;
                Vec3 object = normalize3(add(add(scale(n0, wa), scale(n1, wb)), scale(n2, wc)));
                const Vec3 encoded =
                    tangentSpace ? toTangent(object, p0, p1, p2, uv0, uv1, uv2) : object;
                image->setPixel(x, y, encodeNormal(encoded));
            }
        }
    }

    return eve::Result<ImagePtr>::success(std::move(image));
}

}  // namespace model3d
}  // namespace eve
