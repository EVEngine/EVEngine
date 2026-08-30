#include "gpgpu/vulkan/VulkanSequence.h"
#include "gpgpu/vulkan/VulkanComputeShader.h"
#include "gpgpu/vulkan/VulkanGpuBuffer.h"
#include "gpgpu/vulkan/VulkanUtil.h"

#include "common/Exception.h"
#include "graphics/vulkan/Graphics.h"

#include <algorithm>

namespace eve::gpgpu {

bool VulkanSequence::ready() const { return vkg && static_cast<VkDevice>(vkg->getDevice().instance); }

namespace {

using buf = vk::BufferUsageFlagBits;
using pfb = vk::MemoryPropertyFlagBits;

/** Make writes from one recorded command visible to later commands in the CB. */
void fullMemoryBarrier(VulkanSequence *seq) {
    vk::MemoryBarrier mb{vk::AccessFlagBits::eMemoryWrite | vk::AccessFlagBits::eMemoryRead,
                         vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite};
    seq->commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,
                                       vk::PipelineStageFlagBits::eAllCommands,
                                       vk::DependencyFlags{}, 1, &mb, 0, nullptr, 0, nullptr);
}

}  // namespace

VulkanSequence *vulkanSequenceCreate() { return new VulkanSequence(); }

void VulkanSequence::ensureReady() {
    if (ready()) return;
    auto *g = requireVulkanGraphics();
    vkg = g;
    queue = computeQueue(g);
    pool = computeCommandPool(g);
    if (!queue) throw Exception("Gpgpu.Sequence: no compute/graphics queue");
}

void VulkanSequence::ensureCommandBuffer() {
    if (recording && commandBuffer) return;
    if (status == SequenceStatus::Submitted)
        throw Exception("Gpgpu.Sequence.begin: previous async submission is still pending");
    ensureReady();
    auto &device = vkg->getDevice();
    if (!fenceReady) {
        fence = device->createFence(vk::FenceCreateInfo{});
        fenceReady = true;
    }
    vk::CommandBufferAllocateInfo cbai{pool, vk::CommandBufferLevel::ePrimary, 1};
    commandBuffer = device->allocateCommandBuffers(cbai)[0];
    commandBuffer.begin(vk::CommandBufferBeginInfo{});
    recording = true;
    status      = SequenceStatus::Recording;
    stagingUsed = 0;
    usedShaders.clear();
}

void VulkanSequence::destroy() {
    if (status == SequenceStatus::Submitted) {
        try {
            (void)vulkanSequenceWait(this);
        } catch (...) {
        }
    }
    auto *dev = vkg ? &vkg->getDevice() : nullptr;
    if (dev && static_cast<VkDevice>(dev->instance)) {
        if (commandBuffer) {
            (*dev)->freeCommandBuffers(pool, commandBuffer);
            commandBuffer = vk::CommandBuffer{};
        }
        if (submittedCommandBuffer) {
            (*dev)->freeCommandBuffers(pool, submittedCommandBuffer);
            submittedCommandBuffer = vk::CommandBuffer{};
        }
        if (fenceReady) {
            (*dev)->destroyFence(fence, dev->allocation_callbacks);
            fence = nullptr;
            fenceReady = false;
        }
    }
    recording = false;
    status    = SequenceStatus::Idle;
    stagingPool.clear();
    vkg = nullptr;
    queue = vk::Queue{};
    pool = vk::CommandPool{};
}

void vulkanSequenceDestroy(VulkanSequence *seq) {
    if (!seq) return;
    seq->destroy();
    delete seq;
}

void vulkanSequenceBegin(VulkanSequence *seq) {
    if (!seq) throw Exception("Gpgpu.Sequence: null sequence");
    seq->ensureReady();
    seq->ensureCommandBuffer();
    seq->stagingUsed = 0;
}

void vulkanSequenceRecordUpload(VulkanSequence *seq, GpuBuffer *dst,
                                const void *src, uint64_t nbytes,
                                uint64_t dstOffset) {
    if (!seq || !dst || !src || nbytes == 0) return;
    seq->ensureCommandBuffer();
    auto *vkbDst = dynamic_cast<VulkanGpuBuffer *>(dst);
    if (!vkbDst || !vkbDst->buffer_)
        throw Exception("Gpgpu.Sequence.recordUpload: destination is not a Vulkan storage buffer");
    if (dstOffset + nbytes > uint64_t(vkbDst->size_))
        throw Exception("Gpgpu.Sequence.recordUpload: out of range");

    auto &device = seq->vkg->getDevice();
    vkb::GenericBuffer *staging = nullptr;
    for (size_t i = seq->stagingUsed; i < seq->stagingPool.size(); ++i) {
        if (seq->stagingPool[i].capacity >= vk::DeviceSize(nbytes)) {
            staging = &seq->stagingPool[i];
            std::swap(seq->stagingPool[seq->stagingUsed], seq->stagingPool[i]);
            break;
        }
    }
    if (!staging) {
        seq->stagingPool.emplace_back(device, buf::eTransferSrc, vk::DeviceSize(nbytes),
                                      pfb::eHostVisible | pfb::eHostCoherent);
        staging = &seq->stagingPool.back();
    }
    ++seq->stagingUsed;
    staging->updateLocal(vkb::FrameSlot::gpuIdle(), src, vk::DeviceSize(nbytes));

    vk::BufferCopy bc{0, vk::DeviceSize(dstOffset), vk::DeviceSize(nbytes)};
    seq->commandBuffer.copyBuffer(staging->buffer, vkbDst->buffer_, bc);
    fullMemoryBarrier(seq);
}

void vulkanSequenceRecordDownload(VulkanSequence *seq, GpuBuffer *src,
                                  GpuBuffer *staging, uint64_t nbytes,
                                  uint64_t srcOffset) {
    if (!seq || !src || !staging || nbytes == 0) return;
    seq->ensureCommandBuffer();
    auto *vkbSrc = dynamic_cast<VulkanGpuBuffer *>(src);
    auto *vkbStaging = dynamic_cast<VulkanGpuBuffer *>(staging);
    if (!vkbSrc || !vkbSrc->buffer_)
        throw Exception("Gpgpu.Sequence.recordDownload: source is not a Vulkan storage buffer");
    if (!vkbStaging || !vkbStaging->buffer_ || !vkbStaging->hostVisible_)
        throw Exception("Gpgpu.Sequence.recordDownload: target must be a \"staging\" buffer");
    if (srcOffset + nbytes > uint64_t(vkbSrc->size_))
        throw Exception("Gpgpu.Sequence.recordDownload: out of range");
    if (nbytes > uint64_t(vkbStaging->size_))
        throw Exception("Gpgpu.Sequence.recordDownload: staging buffer too small");

    vk::BufferCopy bc{vk::DeviceSize(srcOffset), 0, vk::DeviceSize(nbytes)};
    seq->commandBuffer.copyBuffer(vkbSrc->buffer_, vkbStaging->buffer_, bc);
    fullMemoryBarrier(seq);
}

void vulkanSequenceRecordDispatch(VulkanSequence *seq, ComputeShader *shader,
                                  int groupsX, int groupsY, int groupsZ) {
    if (!seq || !shader) return;
    seq->ensureCommandBuffer();
    auto *vs = dynamic_cast<VulkanComputeShader *>(shader);
    if (!vs || !vs->pipeline_) return;
    if (groupsX <= 0) groupsX = 1;
    if (groupsY <= 0) groupsY = 1;
    if (groupsZ <= 0) groupsZ = 1;

    auto &device = seq->vkg->getDevice();
    vs->flushDescriptors(device);
    if (std::find(seq->usedShaders.begin(), seq->usedShaders.end(), vs) == seq->usedShaders.end()) {
        vs->beginSequence();
        seq->usedShaders.push_back(vs);
    }

    seq->commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, vs->pipeline_);
    if (vs->descriptorSet_) {
        seq->commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                              vs->pipelineLayout_, 0, vs->descriptorSet_,
                                              nullptr);
    }
    seq->commandBuffer.pushConstants(vs->pipelineLayout_, vk::ShaderStageFlagBits::eCompute,
                                     0, ComputeShader::kPushConstantBytes,
                                     vs->pushConstantData());
    seq->commandBuffer.dispatch(uint32_t(groupsX), uint32_t(groupsY), uint32_t(groupsZ));
    fullMemoryBarrier(seq);
}

void vulkanSequenceSubmit(VulkanSequence *seq) {
    const SequenceStatus submitted = vulkanSequenceSubmitAsync(seq);
    if (submitted == SequenceStatus::Failed || vulkanSequenceWait(seq) != SequenceStatus::Complete)
        throw Exception("Gpgpu.Sequence.submit: GPU submission failed");
}

SequenceStatus vulkanSequenceSubmitAsync(VulkanSequence *seq) {
    if (!seq) return SequenceStatus::Failed;
    seq->ensureReady();
    if (!seq->recording || !seq->commandBuffer)
        throw Exception("Gpgpu.Sequence.submit: nothing recorded");
    auto &device = seq->vkg->getDevice();

    vk::CommandBuffer cb = seq->commandBuffer;
    seq->commandBuffer = vk::CommandBuffer{};
    seq->recording = false;
    cb.end();

    device->resetFences(seq->fence);
    vk::SubmitInfo submit;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cb;
    seq->queue.submit(submit, seq->fence);
    seq->submittedCommandBuffer = cb;
    seq->status                 = SequenceStatus::Submitted;
    return seq->status;
}

namespace {

SequenceStatus retireSubmission(VulkanSequence *seq, SequenceStatus terminal) {
    auto &device = seq->vkg->getDevice();
    for (ComputeShader *shader : seq->usedShaders) {
        auto *vs = dynamic_cast<VulkanComputeShader *>(shader);
        if (vs) {
            vs->endSequence();
            vs->releasePendingDescriptors(device);
        }
    }
    seq->usedShaders.clear();
    if (seq->submittedCommandBuffer) {
        device->freeCommandBuffers(seq->pool, seq->submittedCommandBuffer);
        seq->submittedCommandBuffer = vk::CommandBuffer{};
    }
    seq->status = terminal;
    return terminal;
}

}  // namespace

SequenceStatus vulkanSequencePoll(VulkanSequence *seq) {
    if (!seq) return SequenceStatus::Failed;
    if (seq->status != SequenceStatus::Submitted) return seq->status;
    const vk::Result result = seq->vkg->getDevice()->getFenceStatus(seq->fence);
    if (result == vk::Result::eNotReady) return SequenceStatus::Submitted;
    return retireSubmission(seq, result == vk::Result::eSuccess ? SequenceStatus::Complete : SequenceStatus::Failed);
}

SequenceStatus vulkanSequenceWait(VulkanSequence *seq) {
    if (!seq) return SequenceStatus::Failed;
    if (seq->status != SequenceStatus::Submitted) return seq->status;
    const vk::Result result = seq->vkg->getDevice()->waitForFences(1, &seq->fence, VK_TRUE, UINT64_MAX);
    return retireSubmission(seq, result == vk::Result::eSuccess ? SequenceStatus::Complete : SequenceStatus::Failed);
}

SequenceStatus vulkanSequenceStatus(const VulkanSequence *seq) { return seq ? seq->status : SequenceStatus::Failed; }

}  // namespace eve::gpgpu
