#include "gpgpu/Sequence.h"

#include "common/Exception.h"

#ifdef EVENGINE_WEBGPU

namespace eve::gpgpu {

struct Sequence::Impl {};

Sequence::Sequence() : impl_(new Impl()) {}
Sequence::~Sequence() { delete impl_; }

bool Sequence::isAvailable() const { return false; }

void Sequence::begin() {
    throw Exception("Gpgpu.Sequence: requires the Vulkan backend");
}

void Sequence::recordUpload(GpuBuffer *, const void *, uint64_t, uint64_t) {
    throw Exception("Gpgpu.Sequence.recordUpload: requires the Vulkan backend");
}

void Sequence::recordDownload(GpuBuffer *, GpuBuffer *, uint64_t, uint64_t) {
    throw Exception("Gpgpu.Sequence.recordDownload: requires the Vulkan backend");
}

void Sequence::recordDispatch(ComputeShader *, int, int, int) {
    throw Exception("Gpgpu.Sequence.recordDispatch: requires the Vulkan backend");
}

void Sequence::submit() {
    throw Exception("Gpgpu.Sequence.submit: requires the Vulkan backend");
}

}  // namespace eve::gpgpu

#else

#include "gpgpu/vulkan/VulkanSequence.h"

namespace eve::gpgpu {

struct Sequence::Impl {
    VulkanSequence *v = vulkanSequenceCreate();
};

Sequence::Sequence() : impl_(new Impl()) {}
Sequence::~Sequence() {
    vulkanSequenceDestroy(impl_->v);
    delete impl_;
}

bool Sequence::isAvailable() const {
    try {
        impl_->v->ensureReady();
    } catch (...) {
        return false;
    }
    return impl_->v->ready();
}

void Sequence::begin() { vulkanSequenceBegin(impl_->v); }

void Sequence::recordUpload(GpuBuffer *dst, const void *src, uint64_t nbytes,
                            uint64_t dstOffset) {
    vulkanSequenceRecordUpload(impl_->v, dst, src, nbytes, dstOffset);
}

void Sequence::recordDownload(GpuBuffer *src, GpuBuffer *staging, uint64_t nbytes,
                              uint64_t srcOffset) {
    vulkanSequenceRecordDownload(impl_->v, src, staging, nbytes, srcOffset);
}

void Sequence::recordDispatch(ComputeShader *shader, int groupsX, int groupsY,
                              int groupsZ) {
    vulkanSequenceRecordDispatch(impl_->v, shader, groupsX, groupsY, groupsZ);
}

void Sequence::submit() { vulkanSequenceSubmit(impl_->v); }

}  // namespace eve::gpgpu

#endif
