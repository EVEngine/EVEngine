#include "gpgpu/Sequence.h"

#include "common/Exception.h"
#include "common/config.h"  // EVENGINE_WEBGPU platform macro (generated)

#ifdef EVENGINE_WEBGPU

#include "gpgpu/webgpu/WebGpuGpgpu.h"

namespace eve::gpgpu {

struct Sequence::Impl {
    WebGpuSequence* sequence = webgpuSequenceCreate();
};

Sequence::Sequence() : impl_(new Impl()) {}
Sequence::~Sequence() {
    webgpuSequenceDestroy(impl_->sequence);
    delete impl_;
}

bool Sequence::isAvailable() const { return webgpuSequenceReady(impl_->sequence); }

void Sequence::begin() { webgpuSequenceBegin(impl_->sequence); }

void Sequence::recordUpload(GpuBuffer *dst, const void *src, uint64_t nbytes,
                            uint64_t dstOffset) {
    webgpuSequenceRecordUpload(impl_->sequence, dst, src, nbytes, dstOffset);
}

void Sequence::recordDownload(GpuBuffer *src, GpuBuffer *staging, uint64_t nbytes,
                              uint64_t srcOffset) {
    webgpuSequenceRecordDownload(impl_->sequence, src, staging, nbytes, srcOffset);
}

void Sequence::recordDispatch(ComputeShader *shader, int groupsX, int groupsY,
                              int groupsZ) {
    webgpuSequenceRecordDispatch(impl_->sequence, shader, groupsX, groupsY, groupsZ);
}

void Sequence::submit() { webgpuSequenceSubmit(impl_->sequence); }

SequenceStatus Sequence::submitAsync() { return webgpuSequenceSubmitAsync(impl_->sequence); }
SequenceStatus Sequence::poll() { return webgpuSequencePoll(impl_->sequence); }
SequenceStatus Sequence::wait() { return webgpuSequenceWait(impl_->sequence); }
SequenceStatus Sequence::getStatus() const { return webgpuSequenceStatus(impl_->sequence); }

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

SequenceStatus Sequence::submitAsync() { return vulkanSequenceSubmitAsync(impl_->v); }
SequenceStatus Sequence::poll() { return vulkanSequencePoll(impl_->v); }
SequenceStatus Sequence::wait() { return vulkanSequenceWait(impl_->v); }
SequenceStatus Sequence::getStatus() const { return vulkanSequenceStatus(impl_->v); }

}  // namespace eve::gpgpu

#endif

namespace eve::gpgpu {

std::string Sequence::getStatusName() const {
    switch (getStatus()) {
        case SequenceStatus::Idle: return "idle";
        case SequenceStatus::Recording: return "recording";
        case SequenceStatus::Submitted: return "submitted";
        case SequenceStatus::Complete: return "complete";
        case SequenceStatus::Failed: return "failed";
    }
    return "failed";
}

}  // namespace eve::gpgpu
