#pragma once

#include "vkbuilder.hpp"

#include <algorithm>
#include <cstdint>

namespace vkb {

/**
 * @brief Per-frame GPU allocation arena (host-visible coherent).
 *
 * One arena per swapchain frame slot. `reset()` at frame start; `alloc()`
 * hands out monotonically increasing offsets until capacity. There is no
 * free/realloc inside a frame: everything written here lives until the slot
 * wraps around, which is exactly the lifetime the frame's command buffer needs.
 */
class FrameArena {
public:
  ~FrameArena() {
    if (mapped_ && device_.instance) buf_.unmap();
    mapped_ = nullptr;
  }

  FrameArena() = default;
  FrameArena(const FrameArena &) = delete;
  FrameArena &operator=(const FrameArena &) = delete;
  FrameArena(FrameArena &&o) noexcept { *this = std::move(o); }
  FrameArena &operator=(FrameArena &&o) noexcept {
    if (this != &o) {
      if (mapped_ && device_.instance) buf_.unmap();
      mapped_ = o.mapped_;
      o.mapped_ = nullptr;
      device_ = o.device_;
      buf_ = std::move(o.buf_);
      capacity_ = o.capacity_;
      head_ = o.head_;
      o.capacity_ = 0;
      o.head_ = 0;
    }
    return *this;
  }

  struct Alloc {
    vk::DeviceSize offset = 0;
    vk::DeviceSize size = 0;
    void *mapped = nullptr;
  };

  /// Allocate or grow (before any recording) so `bytes` fit.
  bool ensure(Device &device, vk::DeviceSize bytes, vk::BufferUsageFlags usage) {
    if (capacity_ >= bytes) return true;
    if (!device.instance) return false;

    const vk::DeviceSize grow = (capacity_ > 0) ? capacity_ : 1;
    vk::DeviceSize target = bytes;
    while (target < bytes) target += grow;
    target = std::max<vk::DeviceSize>(target, 1 << 20);

    if (mapped_ && device_.instance) buf_.unmap();
    mapped_ = nullptr;
    GenericBuffer next(device, usage, target,
                       vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    buf_ = std::move(next);
    device_ = device;
    capacity_ = target;
    head_ = 0;
    mapped_ = buf_.map();
    return true;
  }

  /// Allocate `bytes` (aligned). Returns empty Alloc on capacity overflow.
  Alloc alloc(vk::DeviceSize bytes, vk::DeviceSize align = 16) {
    Alloc out{};
    if (bytes == 0) return out;
    const vk::DeviceSize aligned = ((head_ + align - 1) / align) * align;
    if (aligned + bytes > capacity_) return out;
    head_ = aligned + bytes;
    out.offset = aligned;
    out.size = bytes;
    out.mapped = mapped_ ? static_cast<char *>(mapped_) + aligned : nullptr;
    return out;
  }

  void reset() { head_ = 0; }

  vk::Buffer buffer() const { return buf_.buffer; }
  vk::DeviceSize capacity() const { return capacity_; }
  vk::DeviceSize used() const { return head_; }

private:
  GenericBuffer buf_;
  Device device_;
  vk::DeviceSize capacity_ = 0;
  vk::DeviceSize head_ = 0;
  void *mapped_ = nullptr;
};

}  // namespace vkb
