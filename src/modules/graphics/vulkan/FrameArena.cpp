#include "graphics/vulkan/FrameArena.h"

namespace eve::graphics::vulkan {

FrameArena::~FrameArena() {
    // GenericBuffer releases its memory; unmap the persistent mapping first.
    if (mapped_ && device_.instance) {
        device_->unmapMemory(buf_.memory);
    }
    mapped_ = nullptr;
}

FrameArena &FrameArena::operator=(FrameArena &&o) noexcept {
    if (this != &o) {
        if (mapped_ && device_.instance) device_->unmapMemory(buf_.memory);
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

bool FrameArena::ensure(vkb::Device &device, vk::DeviceSize bytes, vk::BufferUsageFlags usage) {
    if (capacity_ >= bytes) return true;
    if (!device.instance) return false;

    const vk::DeviceSize grow = (capacity_ > 0) ? capacity_ : 1;
    vk::DeviceSize target = bytes;
    while (target < bytes) target += grow;  // grow at least by current capacity
    target = std::max<vk::DeviceSize>(target, 1 << 20);

    if (mapped_ && device_.instance) device_->unmapMemory(buf_.memory);
    mapped_ = nullptr;
    vkb::GenericBuffer next(device, usage, target,
                            vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent);
    buf_ = std::move(next);
    device_ = device;
    capacity_ = target;
    head_ = 0;
    mapped_ = device->mapMemory(buf_.memory, 0, capacity_);
    return true;
}

FrameArena::Alloc FrameArena::alloc(vk::DeviceSize bytes, vk::DeviceSize align) {
    Alloc out{};
    if (bytes == 0) return out;
    const vk::DeviceSize aligned = ((head_ + align - 1) / align) * align;
    if (aligned + bytes > capacity_) return out;  // overflow: caller truncates + counts
    head_ = aligned + bytes;
    out.offset = aligned;
    out.size = bytes;
    out.mapped = mapped_ ? static_cast<char *>(mapped_) + aligned : nullptr;
    return out;
}

void FrameArena::reset() {
    head_ = 0;
}

vk::Buffer FrameArena::buffer() const {
    return buf_.buffer;
}

}  // namespace eve::graphics::vulkan
