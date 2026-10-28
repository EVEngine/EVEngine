#include "graphics/webgpu/BindGroupLayoutBuilder.h"

#include "common/Exception.h"

namespace eve::graphics::webgpu {

wgpu::BindGroupLayout BindGroupLayoutBuilder::build(const wgpu::Device &device,
                                                    const char *label) const {
    if (!device) throw Exception("BindGroupLayoutBuilder::build: null device");

    wgpu::BindGroupLayoutDescriptor d{};
    d.label = label;
    d.entryCount = entries_.size();
    d.entries = entries_.data();
    return device.CreateBindGroupLayout(&d);
}

}  // namespace eve::graphics::webgpu