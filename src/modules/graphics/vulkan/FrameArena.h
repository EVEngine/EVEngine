#pragma once

#include "vkbuilder/frame_arena.hpp"

namespace eve::graphics::vulkan {

/** @brief Engine alias for the VKBuilder per-frame bump allocator. */
using FrameArena = vkb::FrameArena;

}  // namespace eve::graphics::vulkan
