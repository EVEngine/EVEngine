#pragma once

// Optional GPU description, provided by whichever graphics backend is linked.
//
// The system module reports GPU name / vendor / type / VRAM, but the values
// only exist inside the backend's device object. Routing them through a
// capability keeps system from including graphics (and from carrying a
// Vulkan-vs-WebGPU #ifdef), and leaves the queries answering "unknown" in a
// build with no graphics module at all.

#include "common/Export.h"

#include <string>

namespace eve::caps {

class EVENGINE_API IGpuInfo {
public:
    static constexpr const char* capabilityName = "IGpuInfo";
    virtual ~IGpuInfo() = default;

    /** False until the backend has selected a physical device. */
    virtual bool gpuReady() const = 0;
    virtual std::string gpuName() const = 0;
    virtual std::string gpuVendor() const = 0;
    /** "discrete" | "integrated" | "virtual" | "cpu" | "webgpu" | "other". */
    virtual std::string gpuDeviceType() const = 0;
    virtual int gpuMemoryTotalMB() const = 0;
};

}  // namespace eve::caps
