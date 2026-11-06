#pragma once

#include "common/GpuResidentBufferView.h"

#include <cstdint>

namespace eve::graphics {

/**
 * @brief GPU-driven API surface without GLM layout records.
 *
 * Layout structs that embed glm types live in `GpuDrivenTypes.h`. This header
 * is safe for high-fanout facades such as `Graphics.h` that only need handles,
 * batch descriptors, and status codes.
 */

constexpr uint32_t kInvalidGpuDrivenSlot = 0xFFFFFFFFu;
constexpr uint32_t kMaxGpuDrivenTextures = 1024;  // sampler2D array slots (desktop limit)
constexpr uint32_t kMaxGpuDrivenCubemaps = 64;    // samplerCube array slots

using eve::GpuResidentBackend;
using eve::GpuResidentBufferView;
using eve::kGpuResidentStorageOffsetAlignment;

struct GpuInstance;
struct GpuVgAssetUpload;

/** @brief One contiguous mesh/material bucket in a sorted resident instance buffer. */
struct GpuResidentInstanceBucket {
    uint32_t firstInstance = 0;
    uint32_t instanceCount = 0;
    uint32_t meshId        = kInvalidGpuDrivenSlot;
    uint32_t materialId    = kInvalidGpuDrivenSlot;
};

/**
 * @brief Direct-render description for a GPU-authored array of GpuInstance records.
 * @ownership buckets and buffer are borrowed for this call; the native buffer remains
 * caller-owned through the frame fence as required by GpuResidentBufferView.
 */
struct GpuResidentInstanceBatch {
    GpuResidentBufferView            buffer;
    const GpuResidentInstanceBucket* buckets       = nullptr;
    uint32_t                         bucketCount   = 0;
    uint32_t                         instanceCount = 0;
};

/** @brief Structured result for direct resident-instance submission. */
enum class GpuResidentSubmitStatus : uint8_t {
    Submitted,
    Unsupported,
    InvalidArgument,
    BackendMismatch,
    ResourceUnavailable,
    CapacityExceeded,
};

}  // namespace eve::graphics
