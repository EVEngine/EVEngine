#include "procgen/PointCompute.h"

#include "common/Module.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <vector>

namespace eve::procgen {
namespace {

constexpr int kPointFloats   = 11;
constexpr int kWorkgroupSize = 64;

const char* transformKernel() {
#ifdef EVENGINE_WEBGPU
    return R"(
struct Data { values: array<f32> };
struct Push { data: array<vec4f, 8> };
@group(0) @binding(0) var<storage, read_write> points: Data;
@group(0) @binding(8) var<uniform> push: Push;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) gid: vec3u) {
    if (gid.x >= u32(push.data[0].x)) { return; }
    let base = gid.x * 11u;
    let sx = push.data[1].x;
    let sy = push.data[1].y;
    let sz = push.data[1].z;
    let c = push.data[2].x;
    let s = push.data[2].y;
    let x = points.values[base] * sx;
    let z = points.values[base + 2u] * sz;
    points.values[base] = x * c - z * s + push.data[0].y;
    points.values[base + 1u] = points.values[base + 1u] * sy + push.data[0].z;
    points.values[base + 2u] = x * s + z * c + push.data[0].w;
    var nx = points.values[base + 3u] / select(1.0, sx, abs(sx) > 0.000001);
    var ny = points.values[base + 4u] / select(1.0, sy, abs(sy) > 0.000001);
    var nz = points.values[base + 5u] / select(1.0, sz, abs(sz) > 0.000001);
    let rotated = vec3f(nx * c - nz * s, ny, nx * s + nz * c);
    let lengthSquared = dot(rotated, rotated);
    let normal = select(rotated, normalize(rotated), lengthSquared > 0.0);
    points.values[base + 3u] = normal.x;
    points.values[base + 4u] = normal.y;
    points.values[base + 5u] = normal.z;
    points.values[base + 6u] += push.data[2].z;
    points.values[base + 7u] *= sx;
    points.values[base + 8u] *= sy;
    points.values[base + 9u] *= sz;
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
    float sx = push.data[4], sy = push.data[5], sz = push.data[6];
    float c = push.data[8], s = push.data[9];
    float x = points.values[base] * sx;
    float z = points.values[base + 2u] * sz;
    points.values[base] = x * c - z * s + push.data[1];
    points.values[base + 1u] = points.values[base + 1u] * sy + push.data[2];
    points.values[base + 2u] = x * s + z * c + push.data[3];
    float nx = points.values[base + 3u] / (abs(sx) > 0.000001 ? sx : 1.0);
    float ny = points.values[base + 4u] / (abs(sy) > 0.000001 ? sy : 1.0);
    float nz = points.values[base + 5u] / (abs(sz) > 0.000001 ? sz : 1.0);
    vec3 normal = vec3(nx * c - nz * s, ny, nx * s + nz * c);
    float normalLength = length(normal);
    if (normalLength > 0.0) normal /= normalLength;
    points.values[base + 3u] = normal.x;
    points.values[base + 4u] = normal.y;
    points.values[base + 5u] = normal.z;
    points.values[base + 6u] += push.data[10];
    points.values[base + 7u] *= sx;
    points.values[base + 8u] *= sy;
    points.values[base + 9u] *= sz;
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
    error_.clear();
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
        if (byteSize > impl_->capacityBytes) {
            int capacity = std::max(byteSize, impl_->capacityBytes + impl_->capacityBytes / 2);
            impl_->storage.reset(gpu->newBuffer(capacity, "storage"));
            impl_->staging.reset(gpu->newBuffer(capacity, "staging"));
            impl_->capacityBytes = capacity;
        }
        if (!impl_->storage || !impl_->staging || !impl_->shader || !impl_->sequence ||
            !impl_->sequence->isAvailable()) {
            error_ = "compute resources unavailable";
            return false;
        }
        impl_->shader->bindBuffer(0, impl_->storage.get());
        constexpr float degreesToRadians = 0.017453292519943295f;
        const float     radians          = yawDegrees * degreesToRadians;
        impl_->shader->setFloat(0, float(input.getCount()));
        impl_->shader->setFloat(1, translateX);
        impl_->shader->setFloat(2, translateY);
        impl_->shader->setFloat(3, translateZ);
        impl_->shader->setFloat(4, scaleX);
        impl_->shader->setFloat(5, scaleY);
        impl_->shader->setFloat(6, scaleZ);
        impl_->shader->setFloat(8, std::cos(radians));
        impl_->shader->setFloat(9, std::sin(radians));
        impl_->shader->setFloat(10, yawDegrees);
        impl_->sequence->begin();
        impl_->sequence->recordUpload(impl_->storage.get(), packed.data(), uint64_t(byteSize));
        impl_->sequence->recordDispatch(impl_->shader.get(), (input.getCount() + kWorkgroupSize - 1) / kWorkgroupSize);
        impl_->sequence->recordDownload(impl_->storage.get(), impl_->staging.get(), uint64_t(byteSize));
        impl_->sequence->submit();
        impl_->staging->downloadBytes(packed.data(), uint64_t(byteSize));

        output = input;
        for (int index = 0; index < output.getCount(); ++index) {
            ProcgenPoint& point  = output.points()[size_t(index)];
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

}  // namespace eve::procgen
