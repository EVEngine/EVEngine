#pragma once

#include "gpgpu/Sequence.h"
#include "vkbuilder.hpp"

#include <cstdint>
#include <vector>

namespace eve::graphics::vulkan {
class Graphics;
}

namespace eve::gpgpu {

class ComputeShader;
class GpuBuffer;

/**
 * @ownership Backend pointers are borrowed from the active Graphics device.
 * @lifetime The active Graphics device, recorded shaders and buffers outlive pending work.
 * @thread All methods except backend completion signaling run on the submitting thread.
 * Vulkan implementation of Sequence: one command buffer per begin()/submit()
 * cycle (allocated from the compute/upload pool, matching executeImmediately),
 * with a persistent pool of host-visible staging buffers for recordUpload().
 * submit() ends the command buffer, submits once with a fence and waits;
 * submitAsync() exposes the same fence through poll()/wait().
 */
struct VulkanSequence {
    graphics::vulkan::Graphics *vkg = nullptr;
    vk::Queue queue{};
    vk::CommandPool pool{};

    // Host-visible staging buffers for recordUpload(); reused across cycles
    // after the previous submission has completed.
    // A buffer is only reused when its capacity fits, so recorded copies never
    // reference a buffer that is destroyed mid-record.
    std::vector<vkb::GenericBuffer> stagingPool;
    size_t stagingUsed = 0;

    vk::Fence fence = nullptr;
    bool fenceReady = false;

    vk::CommandBuffer commandBuffer{};
    vk::CommandBuffer            submittedCommandBuffer{};
    bool recording = false;
    SequenceStatus               status    = SequenceStatus::Idle;
    std::vector<ComputeShader *> usedShaders;  // dispatched during this cycle

    bool ready() const;

    void ensureReady();
    void ensureCommandBuffer();
    void destroy();
};

VulkanSequence *vulkanSequenceCreate();
void vulkanSequenceBegin(VulkanSequence *seq);
void vulkanSequenceRecordUpload(VulkanSequence *seq, GpuBuffer *dst,
                                const void *src, uint64_t nbytes,
                                uint64_t dstOffset);
void vulkanSequenceRecordDownload(VulkanSequence *seq, GpuBuffer *src,
                                  GpuBuffer *staging, uint64_t nbytes,
                                  uint64_t srcOffset);
void vulkanSequenceRecordDispatch(VulkanSequence *seq, ComputeShader *shader,
                                  int groupsX, int groupsY, int groupsZ);
void vulkanSequenceSubmit(VulkanSequence *seq);
SequenceStatus  vulkanSequenceSubmitAsync(VulkanSequence *seq);
SequenceStatus  vulkanSequencePoll(VulkanSequence *seq);
SequenceStatus  vulkanSequenceWait(VulkanSequence *seq);
SequenceStatus  vulkanSequenceStatus(const VulkanSequence *seq);
void vulkanSequenceDestroy(VulkanSequence *seq);

}  // namespace eve::gpgpu
