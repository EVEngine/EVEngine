#include "physics/cloth/LayeredClothGPU.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include "common/Exception.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "physics/cloth/LayeredClothGpuKernel.inc"
namespace eve::physics {
namespace {
void finite3(float x, float y, float z) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        throw Exception("LayeredClothGPU: nonfinite vector");
}
}  // namespace
struct LayeredClothGPU::Impl {
    gpgpu::Gpgpu&                                    compute;
    graphics::Graphics&                              graphics;
    std::array<std::unique_ptr<gpgpu::GpuBuffer>, 8> buffers;
    std::unique_ptr<gpgpu::ComputeShader>            kernel;
    std::unique_ptr<gpgpu::Sequence>                 sequence;
    graphics::Mesh*       mesh = nullptr;  // Borrowed Graphics-owned; released during teardown.
    std::vector<float>    initial, adjacency;
    std::array<float, 32> params{};
    int                   count = 0, triangles = 0, buckets = 1, capacity = 0, substeps = 2, iterations = 8;
    bool                  contacts = true, self = false, failed = false;
    Impl(gpgpu::Gpgpu& c, graphics::Graphics& g) : compute(c), graphics(g) {}
    void bind() {
        for (int i = 0; i < 8; ++i) kernel->bindBuffer(i, buffers[static_cast<size_t>(i)].get());
        for (int i = 0; i < 32; ++i) kernel->setFloat(i, params[static_cast<size_t>(i)]);
    }
    void dispatch(int mode, int work, bool swap = false) {
        kernel->setFloat(7, float(mode));
        sequence->recordDispatch(kernel.get(), (work + 63) / 64);
        if (swap) {
            std::swap(buffers[0], buffers[1]);
            kernel->bindBuffer(0, buffers[0].get());
            kernel->bindBuffer(1, buffers[1].get());
        }
    }
    // Packing is also a graphics-queue retirement point. No readback is recorded.
    void packSubmit() {
        bind();
        sequence->begin();
        dispatch(5, count);
        sequence->submit();
    }
    ~Impl() {
        // Teardown synchronization is resource retirement, not a simulation step.
        // Providers/device must still be alive (public lifetime contract).
        try {
            if (kernel && sequence && buffers[7]) packSubmit();
        } catch (...) {
        }
        if (mesh) {
            try {
                if (graphics.releaseMesh(mesh)) delete mesh;
            } catch (...) {
            }
            mesh = nullptr;
        }
        sequence.reset();
        kernel.reset();
    }
};
LayeredClothGPU::LayeredClothGPU(gpgpu::Gpgpu& compute, graphics::Graphics& graphics, int layers, int cols, int rows,
                                 float spacing, float gap, float x, float y, float z) {
    finite3(x, y, z);
    if (layers < 1 || layers > 8 || cols < 2 || rows < 2 || cols > 128 || rows > 128 || !std::isfinite(spacing) ||
        spacing <= 0 || !std::isfinite(gap) || gap <= 0 || !std::isfinite(x + float(cols) * spacing) ||
        !std::isfinite(y + float(layers) * gap) || !std::isfinite(z + float(rows) * spacing))
        throw Exception("LayeredClothGPU: invalid grid or coordinates");
    if (!compute.isAvailable()) throw Exception("LayeredClothGPU: Vulkan compute is unavailable");
    auto p       = std::make_unique<Impl>(compute, graphics);
    p->count     = layers * cols * rows;
    p->triangles = layers * (cols - 1) * (rows - 1) * 2;
    while (p->buckets < p->count * 2) p->buckets *= 2;
    p->capacity                    = (p->count + p->triangles) * 32;
    const std::array<int, 8> bytes = {
        p->count * 16,    p->count * 16, p->count * 16, p->count * 12 * 16, p->buckets * 4, p->capacity * 32, 32,
        p->count * 2 * 88};
    // Refuse huge per-buffer allocations before touching the device.
    for (int size : bytes)
        if (size > 128 * 1024 * 1024) throw Exception("LayeredClothGPU: grid exceeds resident budget");
    for (int i = 0; i < 8; ++i)
        p->buffers[size_t(i)].reset(compute.newBuffer(bytes[size_t(i)], i == 7 ? "storage_vertex" : "storage"));
    if (p->buffers[7]->residentView().backend != GpuResidentBackend::Vulkan)
        throw Exception("LayeredClothGPU: resident drawing requires Vulkan");
    p->kernel.reset(compute.newShader(detail::layeredClothKernel));
    p->sequence.reset(compute.newSequence());
    p->initial.resize(size_t(p->count) * 4);
    p->adjacency.assign(size_t(p->count) * 12 * 4, 0.f);
    std::vector<float>    positions(size_t(p->count) * 2 * 3, 0.f);
    std::vector<uint32_t> indices;
    const int             dr[12] = {0, 0, 1, -1, 1, 1, -1, -1, 0, 0, 2, -2};
    const int             dc[12] = {1, -1, 0, 0, 1, -1, 1, -1, 2, -2, 0, 0};
    for (int l = 0; l < layers; ++l)
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                int    i             = (l * rows + r) * cols + c;
                size_t base          = size_t(i) * 4;
                p->initial[base]     = x + float(c) * spacing;
                p->initial[base + 1] = y + float(l) * gap;
                p->initial[base + 2] = z + float(r) * spacing;
                p->initial[base + 3] = r == 0 ? 0.f : 1.f;
                for (int side = 0; side < 2; ++side)
                    for (int k = 0; k < 3; ++k)
                        positions[size_t(i + side * p->count) * 3 + size_t(k)] = p->initial[base + size_t(k)];
                for (int k = 0; k < 12; ++k) {
                    int    rr = r + dr[k], cc = c + dc[k];
                    size_t edge        = (size_t(i) * 12 + size_t(k)) * 4;
                    p->adjacency[edge] = -1;
                    if (rr < 0 || cc < 0 || rr >= rows || cc >= cols) continue;
                    p->adjacency[edge]     = float((l * rows + rr) * cols + cc);
                    p->adjacency[edge + 1] = spacing * std::sqrt(float(dr[k] * dr[k] + dc[k] * dc[k]));
                    p->adjacency[edge + 2] = k < 8 ? 1e-7f : 1e-4f;
                }
                if (r + 1 < rows && c + 1 < cols)
                    for (const auto& tri :
                         {std::array<int, 3>{i, i + cols, i + 1}, std::array<int, 3>{i + cols, i + cols + 1, i + 1}}) {
                        for (int v : tri) indices.push_back(uint32_t(v));
                        for (auto it = tri.rbegin(); it != tri.rend(); ++it)
                            indices.push_back(uint32_t(*it + p->count));
                    }
            }
    p->mesh       = graphics.newMeshFromArrays(positions.data(), nullptr, nullptr, p->count * 2, indices.data(),
                                               int(indices.size()));
    p->params[1]  = float(p->count);
    p->params[2]  = float(cols);
    p->params[3]  = float(rows);
    p->params[4]  = float(layers);
    p->params[5]  = spacing;
    p->params[6]  = std::min(gap * 0.8f, spacing * 0.3f);
    p->params[10] = -9.8f;
    p->params[15] = 0.005f;
    p->params[17] = 0.2f;
    p->params[19] = float(p->buckets);
    p->params[20] = float(p->capacity);
    p->params[21] = float(p->triangles);
    p->params[22] = 1;
    p->params[23] = 0.2f;
    p->params[24] = spacing * 2;
    p->params[27] = -3;
    impl_         = std::move(p);
    reset();
}
LayeredClothGPU::~LayeredClothGPU() = default;
void LayeredClothGPU::reset() {
    auto& p = *impl_;
    p.bind();
    p.sequence->begin();
    for (int i = 0; i < 3; ++i)
        p.sequence->recordUpload(p.buffers[size_t(i)].get(), p.initial.data(), p.initial.size() * 4);
    p.sequence->recordUpload(p.buffers[3].get(), p.adjacency.data(), p.adjacency.size() * 4);
    const std::array<unsigned, 8> zeros{};
    p.sequence->recordUpload(p.buffers[6].get(), zeros.data(), 32);
    p.dispatch(5, p.count);
    p.sequence->submit();
    p.failed = false;
}
Result<void> LayeredClothGPU::update(float dt) {
    auto& p = *impl_;
    if (!std::isfinite(dt) || dt < 0 || dt > 0.05f || (dt > 0 && dt < 1e-6f))
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "dt must be zero or finite in [1e-6,0.05]", "physics.layeredGpu.update"));
    if (p.failed)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "GPU solver requires reset after failure",
                                                       "physics.layeredGpu.update"));
    if (dt == 0) return Result<void>::success();
    try {
        p.params[0]  = dt / float(p.substeps);
        p.params[18] = p.self ? 1.f : 0.f;
        p.bind();
        p.sequence->begin();
        p.kernel->setFloat(26, 1.f);
        p.dispatch(2, p.buckets);
        p.kernel->setFloat(26, 0.f);
        for (int s = 0; s < p.substeps; ++s) {
            p.dispatch(0, p.count, true);
            for (int it = 0; it < p.iterations; ++it) {
                p.dispatch(1, p.count, true);
                if (p.contacts || p.self) {
                    // contact() distinguishes self/inter through this parameter.
                    p.kernel->setFloat(16, p.contacts ? 1.f : 0.f);
                    p.kernel->setFloat(26, 0.f);
                    p.dispatch(2, p.buckets);
                    p.dispatch(3, std::max(p.count, p.triangles));
                    p.dispatch(6, p.count);
                    p.dispatch(4, p.count, true);
                }
            }
        }
        p.dispatch(5, p.count);
        p.sequence->submit();
        return Result<void>::success();
    } catch (const std::exception& e) {
        p.failed = true;
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, e.what(), "physics.layeredGpu.update"));
    }
}
Result<void> LayeredClothGPU::draw() {
    auto& p = *impl_;
    if (p.failed)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "GPU solver requires reset after failure",
                                                       "physics.layeredGpu.draw"));
    auto view        = p.buffers[7]->residentView();
    view.strideBytes = 88;
    return p.graphics.drawMeshResidentVertices(*p.mesh, view);
}
void LayeredClothGPU::setSolver(int substeps, int iterations) {
    if (substeps < 1 || substeps > 16 || iterations < 1 || iterations > 64)
        throw Exception("LayeredClothGPU: invalid solver budget");
    impl_->substeps   = substeps;
    impl_->iterations = iterations;
}
void LayeredClothGPU::setGravity(float x, float y, float z) {
    finite3(x, y, z);
    impl_->params[9]  = x;
    impl_->params[10] = y;
    impl_->params[11] = z;
}
void LayeredClothGPU::setWind(float x, float y, float z) {
    finite3(x, y, z);
    impl_->params[12] = x;
    impl_->params[13] = y;
    impl_->params[14] = z;
}
void LayeredClothGPU::setInterCollision(bool on) { impl_->contacts = on; }
void LayeredClothGPU::setSelfCollision(bool on) { impl_->self = on; }
void LayeredClothGPU::setContact(float thickness, float friction) {
    if (!std::isfinite(thickness) || thickness <= 0 || !std::isfinite(friction) || friction < 0 || friction > 1)
        throw Exception("LayeredClothGPU: invalid contact material");
    impl_->params[6]  = thickness;
    impl_->params[17] = friction;
}
void LayeredClothGPU::setFloor(float y) {
    if (!std::isfinite(y)) throw Exception("LayeredClothGPU: invalid floor");
    impl_->params[27] = y;
}
void LayeredClothGPU::setParticleInverseMass(int index, float mass) {
    auto& p = *impl_;
    if (index < 0 || index >= p.count || !std::isfinite(mass) || mass < 0)
        throw Exception("LayeredClothGPU: invalid particle mass");
    p.bind();
    p.sequence->begin();
    p.sequence->recordUpload(p.buffers[2].get(), &mass, 4, uint64_t(index) * 16 + 12);
    p.dispatch(5, p.count);
    p.sequence->submit();
}
int                LayeredClothGPU::getParticleCount() const { return impl_->count; }
std::vector<float> LayeredClothGPU::readPositions() {
    auto& p       = *impl_;
    auto  staging = std::unique_ptr<gpgpu::GpuBuffer>(p.compute.newBuffer(p.count * 16, "staging"));
    p.bind();
    p.sequence->begin();
    p.dispatch(5, p.count);
    p.sequence->recordDownload(p.buffers[0].get(), staging.get(), uint64_t(p.count) * 16);
    p.sequence->submit();
    std::vector<float> result(size_t(p.count) * 4);
    staging->downloadBytes(result.data(), result.size() * 4);
    return result;
}
std::array<unsigned, 4> LayeredClothGPU::readDiagnostics() {
    auto& p       = *impl_;
    auto  staging = std::unique_ptr<gpgpu::GpuBuffer>(p.compute.newBuffer(16, "staging"));
    p.bind();
    p.sequence->begin();
    p.dispatch(5, p.count);
    p.sequence->recordDownload(p.buffers[6].get(), staging.get(), 16);
    p.sequence->submit();
    std::array<unsigned, 4> result{};
    staging->downloadBytes(result.data(), 16);
    return result;
}
}  // namespace eve::physics
