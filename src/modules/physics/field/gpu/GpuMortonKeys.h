#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include "common/Export.h"
#include "common/GpuResidentBufferView.h"
#include "common/Result.h"

namespace eve::gpgpu {
class Gpgpu;
class GpuBuffer;
}  // namespace eve::gpgpu
namespace eve::graphics {
class Graphics;
}
namespace eve::physics::field::gpu {

/** @brief Inclusive finite view-relative domain for 10-bit-per-axis Morton keys. */
struct GpuMortonBounds {
    std::array<float, 3> lower{};
    std::array<float, 3> upper{1, 1, 1};
};

/** @brief Spatial key and original source identity. */
struct GpuMortonKey {
    std::uint32_t key         = 0;
    std::uint32_t sourceIndex = 0;
};

/**
 * @brief Optional GPU-resident key and stable radix order stage of a source spatial hierarchy.
 * @ownership Owns three key buffers, scan scratch, admission storage, compute pipelines and command sequence. Source buffers
 * are borrowed for one update only. Destroy before the Graphics/GPGPU providers.
 * @thread Graphics/GPGPU owner thread only, non-reentrant; no callbacks.
 * @remarks Keys are 30-bit Morton codes and can be sorted stably by key. This stage does not build a tree, field,
 * error bound or interaction list. Out-of-domain sources reject the whole candidate without publication.
 */
class EVENGINE_API_DOMAINS GpuMortonKeys {
public:
    /** @brief Release device resources while Graphics remains alive. */
    ~GpuMortonKeys();
    GpuMortonKeys(const GpuMortonKeys&)            = delete;
    GpuMortonKeys& operator=(const GpuMortonKeys&) = delete;

    /**
     * @brief Allocate reusable GPU key storage for a fixed source count and finite inclusive domain.
     * @param compute Borrowed GPGPU provider; must outlive the returned builder.
     * @param graphics Borrowed Vulkan Graphics provider; must outlive the returned builder.
     * @param sourceCount Fixed positive row count, at most one million in this backend.
     * @param bounds Finite inclusive view-relative domain with positive extent on each axis.
     * @return Owning builder or structured input/provider/resource failure.
     * @cost O(N) device allocation and O(1) pipeline setup; amortize across updates.
     */
    [[nodiscard]] static Result<std::unique_ptr<GpuMortonKeys>> prepare(gpgpu::Gpgpu&       compute,
                                                                        graphics::Graphics& graphics,
                                                                        std::size_t         sourceCount,
                                                                        GpuMortonBounds     bounds);

    /**
     * @brief Generate and atomically publish source keys from an already resident vec4 source buffer.
     * @param sourceRows Borrowed N vec4 rows, with xyz positions; must remain alive through this synchronous call.
     * @return Success or structured rejection; the last published key buffer remains selected on failure.
     * @cost O(N) GPU work, one submission and one admission-word readback; no source-row readback.
     */
    [[nodiscard]] Result<void> update(gpgpu::GpuBuffer& sourceRows);

    /**
     * @brief Stable-sort the published 30-bit Morton keys on the GPU, retaining source indices.
     * @return Success or structured failure; the old published keys remain selected on failure.
     * @ownership Uses only owned scratch buffers; no source buffer or host row is retained.
     * @thread Graphics/GPGPU owner thread only, non-reentrant; no callbacks.
     * @cost Thirty fixed stable binary radix passes, each O(N) GPU work and four dispatches;
     * one queue submission and synchronization. No full-row CPU readback.
     * @remarks Requires a successful update; repeated sort without update is O(1).
     */
    [[nodiscard]] Result<void> sort();

    /**
     * @brief Borrow currently published key/index pairs for a same-device consumer.
     * @return Native view or failure when no update has published or Graphics has retired.
     * @lifetime Valid until the next successful update or destruction; never retain across frames.
     * @cost O(1) and no GPU submission.
     */
    [[nodiscard]] Result<GpuResidentBufferView> residentView() const;

    /**
     * @brief Explicitly read back all published keys for validation or tooling.
     * @return Owning N-row snapshot or structured failure.
     * @cost O(N) device-to-host transfer and output storage; do not call per frame.
     */
    [[nodiscard]] Result<std::vector<GpuMortonKey>> snapshot() const;

private:
    struct Impl;
    explicit GpuMortonKeys(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::physics::field::gpu
