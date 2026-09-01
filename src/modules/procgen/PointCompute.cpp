#include "procgen/PointCompute.h"

#include "common/Module.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace eve::procgen {
namespace {

constexpr int kPointFloats   = 11;
constexpr int kWorkgroupSize = 64;
constexpr int kMaxTransforms = 4;

bool forcedFailure(const char* stage) {
    const char* requested = std::getenv("EVENGINE_POINT_COMPUTE_FAIL");
    return requested && std::string(requested) == stage;
}

const char* transformKernel() {
#ifdef EVENGINE_WEBGPU
    return R"(
struct Data { values: array<f32> };
struct Push { data: array<vec4f, 8> };
@group(0) @binding(0) var<storage, read_write> points: Data;
@group(0) @binding(8) var<uniform> push: Push;
fn parameter(index: u32) -> f32 {
    let value = push.data[index / 4u];
    switch index % 4u {
        case 0u: { return value.x; }
        case 1u: { return value.y; }
        case 2u: { return value.z; }
        default: { return value.w; }
    }
}
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= u32(parameter(0u))) { return; }
    let base = gid.x * 11u;
    for (var operation = 0u; operation < min(u32(parameter(1u)), 4u); operation++) {
        let offset = 2u + operation * 7u;
        let sx = parameter(offset + 3u);
        let sy = parameter(offset + 4u);
        let sz = parameter(offset + 5u);
        let yaw = parameter(offset + 6u);
        let radians = yaw * 0.017453292519943295;
        let c = cos(radians);
        let s = sin(radians);
        let x = points.values[base] * sx;
        let z = points.values[base + 2u] * sz;
        points.values[base] = x * c - z * s + parameter(offset);
        points.values[base + 1u] = points.values[base + 1u] * sy + parameter(offset + 1u);
        points.values[base + 2u] = x * s + z * c + parameter(offset + 2u);
        var nx = points.values[base + 3u] / select(1.0, sx, abs(sx) > 0.000001);
        var ny = points.values[base + 4u] / select(1.0, sy, abs(sy) > 0.000001);
        var nz = points.values[base + 5u] / select(1.0, sz, abs(sz) > 0.000001);
        let rotated = vec3f(nx * c - nz * s, ny, nx * s + nz * c);
        let normal = select(rotated, normalize(rotated), dot(rotated, rotated) > 0.0);
        points.values[base + 3u] = normal.x;
        points.values[base + 4u] = normal.y;
        points.values[base + 5u] = normal.z;
        points.values[base + 6u] += yaw;
        points.values[base + 7u] *= sx;
        points.values[base + 8u] *= sy;
        points.values[base + 9u] *= sz;
    }
}
)";
#else
    return R"(#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Data { float values[]; } points;
layout(push_constant) uniform Push { float data[32]; } push;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(push.data[0])) return;
    uint base = i * 11u;
    for (int operation = 0; operation < min(int(push.data[1]), 4); ++operation) {
        int offset = 2 + operation * 7;
        float sx = push.data[offset + 3], sy = push.data[offset + 4], sz = push.data[offset + 5];
        float yaw = push.data[offset + 6];
        float radians = yaw * 0.017453292519943295;
        float c = cos(radians), s = sin(radians);
        float x = points.values[base] * sx;
        float z = points.values[base + 2u] * sz;
        points.values[base] = x * c - z * s + push.data[offset];
        points.values[base + 1u] = points.values[base + 1u] * sy + push.data[offset + 1];
        points.values[base + 2u] = x * s + z * c + push.data[offset + 2];
        float nx = points.values[base + 3u] / (abs(sx) > 0.000001 ? sx : 1.0);
        float ny = points.values[base + 4u] / (abs(sy) > 0.000001 ? sy : 1.0);
        float nz = points.values[base + 5u] / (abs(sz) > 0.000001 ? sz : 1.0);
        vec3 normal = vec3(nx * c - nz * s, ny, nx * s + nz * c);
        float normalLength = length(normal);
        if (normalLength > 0.0) normal /= normalLength;
        points.values[base + 3u] = normal.x;
        points.values[base + 4u] = normal.y;
        points.values[base + 5u] = normal.z;
        points.values[base + 6u] += yaw;
        points.values[base + 7u] *= sx;
        points.values[base + 8u] *= sy;
        points.values[base + 9u] *= sz;
    }
}
)";
#endif
}

}  // namespace

struct PointCompute::Impl {
    std::vector<float>                         packed;
    std::unique_ptr<eve::gpgpu::GpuBuffer>     storage;
    std::unique_ptr<eve::gpgpu::GpuBuffer>     staging;
    std::unique_ptr<eve::gpgpu::ComputeShader> shader;
    std::unique_ptr<eve::gpgpu::Sequence>      sequence;
    int                                        capacityBytes = 0;
    uint64_t                                   uploadCount             = 0;
    uint64_t                                   dispatchCount           = 0;
    uint64_t                                   readbackCount           = 0;
    uint64_t                                   bufferReuseCount        = 0;
    uint64_t                                   peakBufferBytes         = 0;
    int                                        lastFusedTransformCount = 0;

    void reset() {
        sequence.reset();
        shader.reset();
        staging.reset();
        storage.reset();
        capacityBytes = 0;
    }
};

PointCompute::PointCompute() : impl_(std::make_unique<Impl>()) {}
PointCompute::~PointCompute() = default;

bool PointCompute::transform(const PointSet& input, PointSet& output, float translateX, float translateY,
                             float translateZ, float yawDegrees, float scaleX, float scaleY, float scaleZ) {
    return transformChain(input, output, {{translateX, translateY, translateZ, yawDegrees, scaleX, scaleY, scaleZ}});
}

bool PointCompute::transformChain(const PointSet& input, PointSet& output, const std::vector<Transform>& transforms) {
    error_.clear();
    if (transforms.empty() || transforms.size() > kMaxTransforms) {
        error_ = "transform chain must contain one to four operations";
        return false;
    }
    if (input.empty()) {
        output = input;
        return true;
    }
    constexpr size_t bytesPerPoint = size_t(kPointFloats) * sizeof(float);
    if (size_t(input.getCount()) > size_t(std::numeric_limits<int>::max()) / bytesPerPoint) {
        error_ = "point data exceeds GPU buffer address range";
        return false;
    }
    try {
        if (forcedFailure("compile")) {
            error_ = "forced shader compilation failure";
            return false;
        }
        auto* gpu = eve::ModuleManager::getInstance<eve::gpgpu::Gpgpu>("Gpgpu");
        if (!gpu) gpu = eve::gpgpu::Gpgpu::create();
        if (!gpu || !gpu->isAvailable()) {
            error_ = "compute device unavailable";
            return false;
        }

        impl_->packed.resize(size_t(input.getCount()) * kPointFloats);
        std::vector<float>& packed = impl_->packed;
        for (int index = 0; index < input.getCount(); ++index) {
            const ProcgenPoint& point  = input.points()[size_t(index)];
            float*              values = packed.data() + size_t(index) * kPointFloats;
            values[0]                  = point.x;
            values[1]                  = point.y;
            values[2]                  = point.z;
            values[3]                  = point.normalX;
            values[4]                  = point.normalY;
            values[5]                  = point.normalZ;
            values[6]                  = point.yaw;
            values[7]                  = point.scaleX;
            values[8]                  = point.scaleY;
            values[9]                  = point.scaleZ;
            values[10]                 = point.density;
        }
        const int byteSize = int(packed.size() * sizeof(float));
        if (!impl_->shader) impl_->shader.reset(gpu->newShader(transformKernel()));
        if (!impl_->sequence) impl_->sequence.reset(gpu->newSequence());
        const bool reusedBuffers = byteSize <= impl_->capacityBytes;
        if (byteSize > impl_->capacityBytes) {
            if (forcedFailure("allocation")) {
                error_ = "forced GPU allocation failure";
                return false;
            }
            int capacity = std::max(byteSize, impl_->capacityBytes + impl_->capacityBytes / 2);
            impl_->storage.reset(gpu->newBuffer(capacity, "storage"));
            impl_->staging.reset(gpu->newBuffer(capacity, "staging"));
            impl_->capacityBytes = capacity;
            impl_->peakBufferBytes = std::max(impl_->peakBufferBytes, uint64_t(capacity));
        }
        if (!impl_->storage || !impl_->staging || !impl_->shader || !impl_->sequence ||
            !impl_->sequence->isAvailable()) {
            error_ = "compute resources unavailable";
            return false;
        }
        impl_->shader->bindBuffer(0, impl_->storage.get());
        impl_->shader->setFloat(0, float(input.getCount()));
        impl_->shader->setFloat(1, float(transforms.size()));
        for (size_t index = 0; index < transforms.size(); ++index) {
            const Transform& transform = transforms[index];
            const int        offset    = 2 + int(index) * 7;
            impl_->shader->setFloat(offset, transform.x);
            impl_->shader->setFloat(offset + 1, transform.y);
            impl_->shader->setFloat(offset + 2, transform.z);
            impl_->shader->setFloat(offset + 3, transform.scaleX);
            impl_->shader->setFloat(offset + 4, transform.scaleY);
            impl_->shader->setFloat(offset + 5, transform.scaleZ);
            impl_->shader->setFloat(offset + 6, transform.yaw);
        }
        impl_->sequence->begin();
        impl_->sequence->recordUpload(impl_->storage.get(), packed.data(), uint64_t(byteSize));
        impl_->sequence->recordDispatch(impl_->shader.get(), (input.getCount() + kWorkgroupSize - 1) / kWorkgroupSize);
        impl_->sequence->recordDownload(impl_->storage.get(), impl_->staging.get(), uint64_t(byteSize));
        if (forcedFailure("submit")) {
            error_ = "forced GPU submission failure";
            impl_->reset();
            return false;
        }
        impl_->sequence->submit();
        if (forcedFailure("readback")) {
            error_ = "forced GPU readback failure";
            impl_->reset();
            return false;
        }
        impl_->staging->downloadBytes(packed.data(), uint64_t(byteSize));
        ++impl_->uploadCount;
        ++impl_->dispatchCount;
        ++impl_->readbackCount;
        if (reusedBuffers) ++impl_->bufferReuseCount;
        impl_->lastFusedTransformCount = int(transforms.size());

        output = input;
        for (int index = 0; index < output.getCount(); ++index) {
            ProcgenPoint& point  = output.mutablePoint(size_t(index));
            const float*  values = packed.data() + size_t(index) * kPointFloats;
            point.x              = values[0];
            point.y              = values[1];
            point.z              = values[2];
            point.normalX        = values[3];
            point.normalY        = values[4];
            point.normalZ        = values[5];
            point.yaw            = values[6];
            point.scaleX         = values[7];
            point.scaleY         = values[8];
            point.scaleZ         = values[9];
            point.density        = values[10];
        }
        return true;
    } catch (const std::exception& exception) {
        error_ = exception.what();
        impl_->reset();
    } catch (...) {
        error_ = "unknown compute failure";
        impl_->reset();
    }
    return false;
}

uint64_t PointCompute::getUploadCount() const { return impl_->uploadCount; }
uint64_t PointCompute::getDispatchCount() const { return impl_->dispatchCount; }
uint64_t PointCompute::getReadbackCount() const { return impl_->readbackCount; }
uint64_t PointCompute::getBufferReuseCount() const { return impl_->bufferReuseCount; }
uint64_t PointCompute::getPeakBufferBytes() const { return impl_->peakBufferBytes; }
int      PointCompute::getLastFusedTransformCount() const { return impl_->lastFusedTransformCount; }

}  // namespace eve::procgen
