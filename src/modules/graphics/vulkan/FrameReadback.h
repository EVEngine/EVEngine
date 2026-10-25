#pragma once

#include "vkbuilder.hpp"

namespace eve::graphics::vulkan::detail {

/**
 * @internal
 * @brief Shared raw-copy kernel for live pixels, legacy exports and immutable captures.
 * @thread Graphics queue recording thread; caller owns resources until completion.
 */
inline void recordFrameReadback(vk::CommandBuffer cb, vk::Image source, vk::Buffer destination, vk::Extent2D extent,
                                vk::ImageAspectFlags aspect, uint32_t layer, vk::ImageLayout originalLayout) {
    vkb::recordImageToBufferReadback(cb, source, destination, extent, aspect, layer, originalLayout);
}

}  // namespace eve::graphics::vulkan::detail
