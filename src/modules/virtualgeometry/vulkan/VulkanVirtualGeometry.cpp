#include "virtualgeometry/vulkan/VulkanVirtualGeometry.h"
#include "virtualgeometry/vulkan/VirtualGeometryShaders.h"
#include "virtualgeometry/VirtualGeometryAsset.h"
#include "virtualgeometry/VirtualGeometryBackend.h"

#include "gpgpu/ComputeShader.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/vulkan/VulkanGpgpu.h"
#include "gpgpu/vulkan/VulkanUtil.h"

#include <cstring>
#include <vector>

namespace eve::virtualgeometry {

namespace vulkan {

struct VgState {
    gpgpu::ComputeShader *cull = nullptr;
    gpgpu::ComputeShader *raster = nullptr;
    gpgpu::GpuBuffer *positions = nullptr;
    gpgpu::GpuBuffer *triangles = nullptr;
    gpgpu::GpuBuffer *clusters = nullptr;
    gpgpu::GpuBuffer *visible = nullptr;
    gpgpu::GpuBuffer *pixels = nullptr;
    gpgpu::GpuBuffer *uniforms = nullptr;
    gpgpu::GpuBuffer *stats = nullptr;

    int viewW = 1, viewH = 1;
    int pixelCapacity = 0;
    int visibleCapacity = 0;
    int lastVisible = 0;
};

namespace {

gpgpu::ComputeShader *makeShader(const std::vector<uint32_t> &spv) {
    return gpgpu::vulkanNewShaderFromSpirv(spv);
}

void bindStandard(gpgpu::ComputeShader *s, VgState *st) {
    if (st->positions) s->bindBuffer(0, st->positions);
    if (st->triangles) s->bindBuffer(1, st->triangles);
    if (st->clusters) s->bindBuffer(2, st->clusters);
    if (st->visible) s->bindBuffer(3, st->visible);
    if (st->pixels) s->bindBuffer(4, st->pixels);
    if (st->uniforms) s->bindBuffer(5, st->uniforms);
    if (st->stats) s->bindBuffer(6, st->stats);
}

void ensurePixels(VgState *st, int w, int h) {
    int need = w * h;
    if (need > st->pixelCapacity) {
        delete st->pixels;
        st->pixels = gpgpu::vulkanNewBuffer(need * (int)sizeof(uint32_t), "storage");
        st->pixelCapacity = need;
        bindStandard(st->cull, st);
        bindStandard(st->raster, st);
    }
}

}  // namespace

}  // namespace vulkan

// ---- eve::virtualgeometry backend interface ----

void vgCreate(VgBackend &be) {
    auto *st = new vulkan::VgState();
    be.state = st;
    try {
        st->cull = vulkan::makeShader(
            std::vector<uint32_t>(vg_cull_compute_spv, vg_cull_compute_spv + vg_cull_compute_spv_count));
        st->raster = vulkan::makeShader(
            std::vector<uint32_t>(vg_raster_compute_spv, vg_raster_compute_spv + vg_raster_compute_spv_count));
    } catch (...) {
        delete st;
        be.state = nullptr;
        throw;
    }
}

void vgDestroy(VgBackend &be) {
    auto *st = static_cast<vulkan::VgState *>(be.state);
    if (!st) return;
    delete st->cull;
    delete st->raster;
    delete st->positions;
    delete st->triangles;
    delete st->clusters;
    delete st->visible;
    delete st->pixels;
    delete st->uniforms;
    delete st->stats;
    delete st;
    be.state = nullptr;
}

void vgUpload(VgBackend &be, const VirtualGeometryAsset &asset) {
    auto *st = static_cast<vulkan::VgState *>(be.state);
    if (!st) return;

    delete st->positions;
    delete st->triangles;
    delete st->clusters;

    st->positions = gpgpu::vulkanNewBuffer((int)(asset.positions.size() * sizeof(float)), "storage");
    st->positions->uploadBytes(asset.positions.data(), asset.positions.size() * sizeof(float));

    st->triangles = gpgpu::vulkanNewBuffer((int)(asset.triangles.size() * sizeof(uint32_t)), "storage");
    st->triangles->uploadBytes(asset.triangles.data(), asset.triangles.size() * sizeof(uint32_t));

    // Cluster table: 4 uvec4 per cluster (see VgGpuCluster layout).
    std::vector<uint32_t> packed;
    packed.reserve(asset.clusters.size() * 16);
    auto fb = [](float f) {
        union { float f; uint32_t u; } x;
        x.f = f;
        return x.u;
    };
    for (const auto &c : asset.clusters) {
        packed.push_back(fb(c.cx)); packed.push_back(fb(c.cy));
        packed.push_back(fb(c.cz)); packed.push_back(fb(c.r));
        packed.push_back(c.triStart); packed.push_back(c.triCount);
        packed.push_back(c.lodLevel); packed.push_back(c.parent);
        packed.push_back(fb(c.errorR)); packed.push_back(fb(c.errorRScreen));
        packed.push_back(c.childCount); packed.push_back(0);
        for (int k = 0; k < 4; ++k) packed.push_back(c.children[k]);
    }
    st->clusters = gpgpu::vulkanNewBuffer((int)(packed.size() * sizeof(uint32_t)), "storage");
    st->clusters->uploadBytes(packed.data(), packed.size() * sizeof(uint32_t));

    // Per-frame buffers (uniforms / visible / stats / pixels) must exist before
    // we bind the standard layout so no binding is left null.
    if (!st->uniforms)
        st->uniforms = gpgpu::vulkanNewBuffer((int)sizeof(VgUniforms), "storage");
    if (!st->visible)
        st->visible = gpgpu::vulkanNewBuffer((st->visibleCapacity + 1) * (int)sizeof(uint32_t),
                                             "storage");
    if (!st->stats)
        st->stats = gpgpu::vulkanNewBuffer(4 * (int)sizeof(uint32_t), "storage");
    vulkan::ensurePixels(st, st->viewW, st->viewH);

    vulkan::bindStandard(st->cull, st);
    vulkan::bindStandard(st->raster, st);
}

void vgUploadUniforms(VgBackend &be, const VgUniforms &u) {
    auto *st = static_cast<vulkan::VgState *>(be.state);
    if (!st) return;
    if (!st->uniforms)
        st->uniforms = gpgpu::vulkanNewBuffer((int)sizeof(VgUniforms), "storage");
    st->uniforms->uploadBytes(&u, sizeof(VgUniforms));
    st->cull->bindBuffer(5, st->uniforms);
    st->raster->bindBuffer(5, st->uniforms);
}

void vgReset(VgBackend &be, int visibleCapacity) {
    auto *st = static_cast<vulkan::VgState *>(be.state);
    if (!st || visibleCapacity <= 0) return;
    if (st->visibleCapacity == visibleCapacity && st->visible && st->stats) return;
    st->visibleCapacity = visibleCapacity;
    delete st->visible;
    delete st->stats;
    // [counter, data...]
    st->visible = gpgpu::vulkanNewBuffer((visibleCapacity + 1) * (int)sizeof(uint32_t), "storage");
    st->stats = gpgpu::vulkanNewBuffer(4 * (int)sizeof(uint32_t), "storage");
    st->cull->bindBuffer(3, st->visible);
    st->cull->bindBuffer(6, st->stats);
    st->raster->bindBuffer(3, st->visible);
    st->raster->bindBuffer(6, st->stats);
}

int vgUpdate(VgBackend &be, int clusterCount, int visibleCapacity, int viewW, int viewH) {
    auto *st = static_cast<vulkan::VgState *>(be.state);
    if (!st || !st->cull || !st->raster || !st->visible) return 0;
    st->visible->fillFloat32(0.f);
    st->stats->fillFloat32(0.f);
    if (viewW * viewH > 0) vulkan::ensurePixels(st, viewW, viewH);
    st->viewW = viewW;
    st->viewH = viewH;
    float allOnes = 0.f;
    std::memcpy(&allOnes, "\xff\xff\xff\xff", 4);
    st->pixels->fillFloat32(allOnes);
    int cullGroups = (clusterCount + 63) / 64;
    gpgpu::vulkanDispatch(st->cull, cullGroups, 1, 1);
    int rasterThreads = visibleCapacity * 124;
    int rasterGroups = (rasterThreads + 127) / 128;
    gpgpu::vulkanDispatch(st->raster, rasterGroups, 1, 1);

    uint32_t counter = 0;
    st->visible->downloadBytes(&counter, sizeof(counter), 0);
    st->lastVisible = (int)counter;
    return st->lastVisible;
}

bool vgReadPixels(VgBackend &be, std::vector<uint32_t> &out) {
    auto *st = static_cast<vulkan::VgState *>(be.state);
    if (!st || !st->pixels || st->viewW <= 0 || st->viewH <= 0) return false;
    size_t n = (size_t)st->viewW * (size_t)st->viewH;
    out.resize(n);
    st->pixels->downloadBytes(out.data(), n * sizeof(uint32_t), 0);
    return true;
}

}  // namespace eve::virtualgeometry
