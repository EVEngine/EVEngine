#pragma once

#include "procgen/PointSet.h"

#include <memory>
#include <string>

namespace eve::procgen {

/** @brief Backend-neutral GPU kernels used by PointGraph with deterministic CPU fallback. */
class PointCompute {
public:
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

    /** @brief Return the last GPU unavailability, compilation, dispatch, or readback error. */
    const std::string& getError() const { return error_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::string           error_;
};

}  // namespace eve::procgen
