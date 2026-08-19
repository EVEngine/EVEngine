// WebGPU counterpart of graphics/vulkan/GpuInfoProvider.cpp.
//
// WebGPU deliberately does not expose adapter identity to the page, so only the
// device type is meaningful here; the rest stay empty, which is what the system
// module reported on this backend before the capability existed.

#include "common/Capability.h"
#include "common/GpuInfo.h"

#include <string>

namespace eve::graphics::webgpu {
namespace {

class WebGpuGpuInfo : public eve::caps::IGpuInfo {
public:
    bool gpuReady() const override { return true; }
    std::string gpuName() const override { return {}; }
    std::string gpuVendor() const override { return {}; }
    std::string gpuDeviceType() const override { return "webgpu"; }
    int gpuMemoryTotalMB() const override { return 0; }
};

struct Register {
    Register() {
        static WebGpuGpuInfo info;
        eve::cap::provide<eve::caps::IGpuInfo>(&info);
    }
} g_register;

}  // namespace
}  // namespace eve::graphics::webgpu
