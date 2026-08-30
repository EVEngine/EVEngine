#pragma once

#include "procgen/PointSet.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::procgen {

/** @brief Backend-neutral GPU kernels used by PointGraph with deterministic CPU fallback. */
class PointCompute {
public:
    /** @brief One transform operation in a fused point-compute dispatch. */
    struct Transform {
        float x = 0.f, y = 0.f, z = 0.f;
        float yaw    = 0.f;
        float scaleX = 1.f, scaleY = 1.f, scaleZ = 1.f;
    };

    PointCompute();
    ~PointCompute();
    PointCompute(const PointCompute&)            = delete;
    PointCompute& operator=(const PointCompute&) = delete;

    /**
     * @brief Transform attributed points through the active Vulkan or WebGPU compute backend.
     * @return True when GPU execution completed; false leaves output untouched and describes
     * the fallback reason in getError().
     */
    bool transform(const PointSet& input, PointSet& output, float translateX, float translateY, float translateZ,
                   float yawDegrees, float scaleX, float scaleY, float scaleZ);

    /**
     * @brief Execute one to four transforms in one upload, dispatch and readback.
     * @return True when GPU execution completed; false leaves output untouched.
     */
    bool transformChain(const PointSet& input, PointSet& output, const std::vector<Transform>& transforms);

    /** @brief Return the last GPU unavailability, compilation, dispatch, or readback error. */
    const std::string& getError() const { return error_; }
    /** @brief Return successful host-to-GPU upload count for this executor. */
    uint64_t getUploadCount() const;
    /** @brief Return successful compute dispatch count for this executor. */
    uint64_t getDispatchCount() const;
    /** @brief Return successful GPU-to-host readback count for this executor. */
    uint64_t getReadbackCount() const;
    /** @brief Return executions that reused existing GPU buffers. */
    uint64_t getBufferReuseCount() const;
    /** @brief Return peak bytes reserved in each reusable GPU point buffer. */
    uint64_t getPeakBufferBytes() const;
    /** @brief Return logical transform count in the most recent successful dispatch. */
    int getLastFusedTransformCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string           error_;
};

}  // namespace eve::procgen
