#include "virtualgeometry/VirtualGeometryRenderer.h"
#include "virtualgeometry/Builder.h"

#include "common/Exception.h"
#include "data/ByteData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace eve::virtualgeometry {
namespace {

// Minimal column-major mat4 helpers (GLM is not linked into this module).
struct Mat4 {
    float m[16];
    static Mat4 identity() {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f;
        return r;
    }
    static Mat4 mul(const Mat4 &a, const Mat4 &b) {
        Mat4 r{};
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                float sum = 0;
                for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = sum;
            }
        return r;
    }
};

}  // namespace

VirtualGeometryRenderer::VirtualGeometryRenderer() { vgCreate(backend_); }

VirtualGeometryRenderer::~VirtualGeometryRenderer() { vgDestroy(backend_); }

bool VirtualGeometryRenderer::build(const VirtualGeometryBuilder::MeshInput &in) {
    VirtualGeometryBuilder builder;
    bool ok = builder.build(in, builderOptions_, asset_);
    if (!ok) return false;
    vgReset(backend_, visibleCapacity_);  // size visible/stats before upload
    vgUpload(backend_, asset_);
    return true;
}

bool VirtualGeometryRenderer::build(const float *positions, int vertexCount,
                                    const std::uint32_t *indices, int indexCount) {
    VirtualGeometryBuilder::MeshInput in;
    in.vertexCount = vertexCount;
    in.positions = positions;
    in.indices = indices;
    in.indexCount = indexCount;
    return build(in);
}

namespace {
// Build a unit icosphere (triangulated) into CPU buffers.
void buildIco(std::vector<float> &positions, std::vector<std::uint32_t> &indices, int subdiv) {
    positions.clear();
    indices.clear();
    struct F { int a, b, c; };
    auto addVtx = [&](float x, float y, float z) {
        float l = std::sqrt(x * x + y * y + z * z);
        positions.push_back(x / l);
        positions.push_back(y / l);
        positions.push_back(z / l);
        return static_cast<int>(positions.size() / 3) - 1;
    };
    auto tri = [&](int a, int b, int c) {
        indices.push_back(static_cast<std::uint32_t>(a));
        indices.push_back(static_cast<std::uint32_t>(b));
        indices.push_back(static_cast<std::uint32_t>(c));
    };
    auto mid = [&](int i0, int i1) {
        return addVtx(0.5f * (positions[3 * i0] + positions[3 * i1]),
                      0.5f * (positions[3 * i0 + 1] + positions[3 * i1 + 1]),
                      0.5f * (positions[3 * i0 + 2] + positions[3 * i1 + 2]));
    };
    const float t = (1.f + std::sqrt(5.f)) * 0.5f;
    std::vector<std::array<float, 3>> base = {
        {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
        {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    int idx[12];
    for (int i = 0; i < 12; ++i) idx[i] = addVtx(base[i][0], base[i][1], base[i][2]);
    int faces[20][3] = {{0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11},
                        {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                        {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},
                        {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1}};
    std::vector<F> cur;
    for (auto &f : faces) cur.push_back({idx[f[0]], idx[f[1]], idx[f[2]]});
    for (int d = 0; d < subdiv; ++d) {
        std::vector<F> nf;
        for (auto &x : cur) {
            int ab = mid(x.a, x.b), ac = mid(x.a, x.c), bc = mid(x.b, x.c);
            nf.push_back({x.a, ab, ac});
            nf.push_back({ab, x.b, bc});
            nf.push_back({ac, bc, x.c});
            nf.push_back({ab, bc, ac});
        }
        cur.swap(nf);
    }
    for (auto &x : cur) tri(x.a, x.b, x.c);
}
}  // namespace

bool VirtualGeometryRenderer::buildIcosphere(int subdiv) {
    std::vector<float> pos;
    std::vector<std::uint32_t> idx;
    buildIco(pos, idx, std::clamp(subdiv, 1, 6));
    return build(pos.data(), static_cast<int>(pos.size() / 3), idx.data(),
                 static_cast<int>(idx.size()));
}

void VirtualGeometryRenderer::setViewport(int width, int height, float fovYDeg, float errorPx) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    uniforms_.params[0] = static_cast<float>(width_);
    uniforms_.params[1] = static_cast<float>(height_);
    uniforms_.params[2] = static_cast<float>(height_) /
                          (2.f * std::tan(fovYDeg * 3.14159265358979323846f / 360.f));
    uniforms_.params[3] = errorPx;
}

void VirtualGeometryRenderer::setCamera(const float view[16], const float proj[16],
                                        const float model[16], const float camPos[3]) {
    // Combined view-projection: clip = P * V * model * pos. The shader uses
    // vgU.viewProj * (vgU.model * pos), so viewProj = P * V.
    Mat4 p;
    for (int i = 0; i < 16; ++i) p.m[i] = proj[i];
    Mat4 v;
    for (int i = 0; i < 16; ++i) v.m[i] = view[i];
    Mat4 vp = Mat4::mul(p, v);
    for (int i = 0; i < 16; ++i) uniforms_.viewProj[i] = vp.m[i];
    for (int i = 0; i < 16; ++i) uniforms_.model[i] = model ? model[i] : Mat4::identity().m[i];
    uniforms_.cameraPos[0] = camPos[0];
    uniforms_.cameraPos[1] = camPos[1];
    uniforms_.cameraPos[2] = camPos[2];
    uniforms_.cameraPos[3] = 0.f;
    updateUniforms();
}

void VirtualGeometryRenderer::setCameraSimple(float camX, float camY, float camZ, float nearZ,
                                              float farZ) {
    const float fovY = (uniforms_.params[2] > 0.f)
                           ? 2.f * std::atan(static_cast<float>(height_) / (2.f * uniforms_.params[2]))
                           : 60.f * 3.14159265358979323846f / 180.f;
    // Identity view (looking down -Z).
    Mat4 view = Mat4::identity();
    // Perspective projection (column-major, D3D/Vulkan clip z in [0,1]).
    Mat4 proj{};
    const float a = 1.f / std::tan(fovY * 0.5f);
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    proj.m[0] = a / aspect;
    proj.m[5] = a;
    proj.m[10] = farZ / (nearZ - farZ);
    proj.m[11] = -1.f;
    proj.m[14] = (nearZ * farZ) / (nearZ - farZ);
    const float camPos[3] = {camX, camY, camZ};
    setCamera(view.m, proj.m, nullptr, camPos);
}

void VirtualGeometryRenderer::setModelYaw(float yaw) { modelYaw_ = yaw; }

void VirtualGeometryRenderer::updateUniforms() {
    uniforms_.misc[0] = static_cast<float>(static_cast<int>(asset_.clusters.size()));
    if (modelYaw_ != 0.f) {
        // Build a Y-rotation model matrix (column-major) and override.
        float c = std::cos(modelYaw_), s = std::sin(modelYaw_);
        Mat4 r = Mat4::identity();
        r.m[0] = c; r.m[2] = s;
        r.m[8] = -s; r.m[10] = c;
        for (int i = 0; i < 16; ++i) uniforms_.model[i] = r.m[i];
    }
    // Recompute per-cluster screen-space error constants (errorR * projScale).
    for (auto &c : asset_.clusters) c.errorRScreen = c.errorR * uniforms_.params[2];
    vgUploadUniforms(backend_, uniforms_);
}

int VirtualGeometryRenderer::update() {
    int clusters = static_cast<int>(asset_.clusters.size());
    if (clusters <= 0 || !backend_.state) return 0;
    lastVisible_ = vgUpdate(backend_, clusters, visibleCapacity_, width_, height_);
    return lastVisible_;
}

bool VirtualGeometryRenderer::resolve(unsigned char *outRgba, int &outW, int &outH) {
    std::vector<std::uint32_t> pix;
    if (!vgReadPixels(backend_, pix)) return false;
    outW = width_;
    outH = height_;
    const int n = outW * outH;
    for (int i = 0; i < n; ++i) {
        std::uint32_t packed = pix[i];
        std::uint32_t depth = packed >> 16;
        std::uint32_t cid = packed & 0xFFFFu;
        if (cid == 0u && depth == 0xFFFFu) {
            outRgba[4 * i + 0] = 4;
            outRgba[4 * i + 1] = 6;
            outRgba[4 * i + 2] = 12;
            outRgba[4 * i + 3] = 255;
            continue;
        }
        // Stable per-cluster color hash.
        std::uint32_t h = cid * 2654435761u;
        float cr = (h >> 16) & 0xFFu, cg = (h >> 8) & 0xFFu, cb = h & 0xFFu;
        float mx = std::max({cr, cg, cb, 1.f});
        float depthF = static_cast<float>(depth) / 65535.f;
        float shade = 0.35f + 0.65f * (1.f - depthF);
        outRgba[4 * i + 0] = static_cast<unsigned char>(cr / mx * 255.f * shade);
        outRgba[4 * i + 1] = static_cast<unsigned char>(cg / mx * 255.f * shade);
        outRgba[4 * i + 2] = static_cast<unsigned char>(cb / mx * 255.f * shade);
        outRgba[4 * i + 3] = 255;
    }
    return true;
}

eve::data::ByteData *VirtualGeometryRenderer::resolveByteData() {
    int w = 1, h = 1;
    auto *bd = new eve::data::ByteData(static_cast<std::size_t>(width_) * height_ * 4);
    if (!bd) return nullptr;
    if (!resolve(static_cast<unsigned char *>(bd->getData()), w, h)) {
        delete bd;
        return nullptr;
    }
    return bd;
}

int VirtualGeometryRenderer::getClusterCount() const {
    return static_cast<int>(asset_.clusters.size());
}

int VirtualGeometryRenderer::getTotalTriangleCount() const {
    return asset_.totalTriangles();
}

int VirtualGeometryRenderer::getLodLevel(int clusterId) const {
    if (clusterId < 0 || static_cast<std::size_t>(clusterId) >= asset_.clusters.size()) return -1;
    return static_cast<int>(asset_.clusters[clusterId].lodLevel);
}

int VirtualGeometryRenderer::getMaxLodLevel() const {
    int m = 0;
    for (const auto &c : asset_.clusters) m = std::max(m, static_cast<int>(c.lodLevel));
    return m;
}

}  // namespace eve::virtualgeometry
