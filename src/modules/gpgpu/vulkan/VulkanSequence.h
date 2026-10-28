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
/** @brief VulkanSequence public API. */
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

    /** @brief Ready. */
    bool ready() const;

    /** @brief Ensure ready. */
    void ensureReady();
    /** @brief Ensure command buffer. */
    void ensureCommandBuffer();
    /** @brief Destroys destroy. */
    void destroy();
};

/** @brief Vulkan sequence create. */
VulkanSequence *vulkanSequenceCreate();
/** @brief Vulkan sequence begin. */
void vulkanSequenceBegin(VulkanSequence *seq);
/** @brief Vulkan sequence record upload. */
void vulkanSequenceRecordUpload(VulkanSequence *seq, GpuBuffer *dst,
                                const void *src, uint64_t nbytes,
                                uint64_t dstOffset);
/** @brief Vulkan sequence record download. */
void vulkanSequenceRecordDownload(VulkanSequence *seq, GpuBuffer *src,
                                  GpuBuffer *staging, uint64_t nbytes,
                                  uint64_t srcOffset);
/** @brief Vulkan sequence record dispatch. */
void vulkanSequenceRecordDispatch(VulkanSequence *seq, ComputeShader *shader,
                                  int groupsX, int groupsY, int groupsZ);
/** @brief Vulkan sequence submit. */
void vulkanSequenceSubmit(VulkanSequence *seq);
/** @brief Vulkan sequence submit async. */
SequenceStatus  vulkanSequenceSubmitAsync(VulkanSequence *seq);
/** @brief Vulkan sequence poll. */
SequenceStatus  vulkanSequencePoll(VulkanSequence *seq);
/** @brief Vulkan sequence wait. */
SequenceStatus  vulkanSequenceWait(VulkanSequence *seq);
/** @brief Vulkan sequence status. */
SequenceStatus  vulkanSequenceStatus(const VulkanSequence *seq);
/** @brief Vulkan sequence destroy. */
void vulkanSequenceDestroy(VulkanSequence *seq);

}  // namespace eve::gpgpu
