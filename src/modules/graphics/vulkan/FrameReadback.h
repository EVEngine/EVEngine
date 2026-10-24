#pragma once

#include <utility>
#include "vkbuilder.hpp"

namespace eve::graphics::vulkan::detail {
/**
 * @internal
 * @brief Shared raw-copy kernel for live pixels, legacy exports and immutable captures.
 * @thread Graphics queue recording thread; caller owns resources until completion.
 */
inline void recordFrameReadback(vk::CommandBuffer cb, vk::Image source, vk::Buffer destination, vk::Extent2D extent,
                                vk::ImageAspectFlags aspect, uint32_t layer, vk::ImageLayout originalLayout) {
    vk::ImageMemoryBarrier barrier{};
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image               = source;
    barrier.subresourceRange    = {aspect, 0, 1, layer, 1};
    barrier.oldLayout           = originalLayout;
    barrier.newLayout           = vk::ImageLayout::eTransferSrcOptimal;
    const bool present          = originalLayout == vk::ImageLayout::ePresentSrcKHR;
    barrier.srcAccessMask       = present ? vk::AccessFlagBits::eColorAttachmentWrite
                                          : vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
    barrier.dstAccessMask       = vk::AccessFlagBits::eTransferRead;
    cb.pipelineBarrier(
        present ? vk::PipelineStageFlagBits::eColorAttachmentOutput : vk::PipelineStageFlagBits::eAllCommands,
        vk::PipelineStageFlagBits::eTransfer, {}, 0, nullptr, 0, nullptr, 1, &barrier);
    vk::BufferImageCopy region{};
    region.imageSubresource = {aspect, 0, layer, 1};
    region.imageExtent      = vk::Extent3D{extent.width, extent.height, 1};
    cb.copyImageToBuffer(source, vk::ImageLayout::eTransferSrcOptimal, destination, region);
    // Fence completion orders execution; explicitly make transfer writes visible
    // to host reads as in Khronos' CPU readback synchronization contract.
    vk::BufferMemoryBarrier hostRead{};
    hostRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostRead.srcAccessMask       = vk::AccessFlagBits::eTransferWrite;
    hostRead.dstAccessMask       = vk::AccessFlagBits::eHostRead;
    hostRead.buffer              = destination;
    hostRead.offset              = 0;
    hostRead.size                = VK_WHOLE_SIZE;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {}, 0, nullptr, 1,
                       &hostRead, 0, nullptr);
    std::swap(barrier.oldLayout, barrier.newLayout);
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
    barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
    cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                       present ? vk::PipelineStageFlagBits::eBottomOfPipe : vk::PipelineStageFlagBits::eAllCommands, {},
                       0, nullptr, 0, nullptr, 1, &barrier);
}
}  // namespace eve::graphics::vulkan::detail
