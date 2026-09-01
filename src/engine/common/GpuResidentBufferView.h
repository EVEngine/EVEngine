#pragma once

#include <cstdint>

namespace eve {

/** @brief Backend identity for a transient, non-owning resident GPU buffer view. */
enum class GpuResidentBackend : uint8_t { Unknown, Vulkan, WebGpu };

/** @brief Portable alignment required for resident storage-buffer slice offsets. */
constexpr uint64_t kGpuResidentStorageOffsetAlignment = 256;

/**
 * @brief Transient backend buffer slice shared between sibling GPU modules.
 * @ownership Non-owning; nativeHandle is never retained as an owning resource.
 * @lifetime The producing buffer outlives all frame commands that consume this view.
 * @thread Create and consume on the render/submission thread.
 */
struct GpuResidentBufferView {
    GpuResidentBackend backend      = GpuResidentBackend::Unknown;
    uint64_t           nativeHandle = 0;
    uint64_t           offsetBytes  = 0;
    uint64_t           sizeBytes    = 0;
    uint32_t           strideBytes  = 0;
};

}  // namespace eve
