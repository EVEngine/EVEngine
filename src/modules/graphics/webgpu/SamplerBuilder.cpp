#include "graphics/webgpu/SamplerBuilder.h"

#include "common/Exception.h"

#include <algorithm>

namespace eve::graphics::webgpu {

SamplerBuilder SamplerBuilder::fromEngine(const eve::graphics::TextureSampler &s,
                                          uint32_t mipLevels, float maxDeviceAnisotropy) {
    SamplerBuilder b;
    b.addrU_ = s.repeatU ? wgpu::AddressMode::Repeat : wgpu::AddressMode::ClampToEdge;
    b.addrV_ = s.repeatV ? wgpu::AddressMode::Repeat : wgpu::AddressMode::ClampToEdge;
    b.addrW_ = s.repeatW ? wgpu::AddressMode::Repeat : wgpu::AddressMode::ClampToEdge;
    b.mag_ = s.mag == FilterMode::Nearest ? wgpu::FilterMode::Nearest : wgpu::FilterMode::Linear;
    b.min_ = s.min == FilterMode::Nearest ? wgpu::FilterMode::Nearest : wgpu::FilterMode::Linear;
    if (s.mipmap == MipmapMode::Disabled || mipLevels <= 1) {
        b.mip_ = wgpu::MipmapFilterMode::Nearest;
        b.lodMin_ = 0.f;
        b.lodMax_ = 0.f;
    } else {
        b.mip_ = s.mipmap == MipmapMode::Nearest ? wgpu::MipmapFilterMode::Nearest
                                                 : wgpu::MipmapFilterMode::Linear;
        b.lodMin_ = s.minLod;
        b.lodMax_ = std::min(std::max(s.maxLod, 0.f), float(mipLevels - 1));
        if (b.lodMax_ < b.lodMin_) b.lodMax_ = b.lodMin_;
    }
    float aniso = s.maxAnisotropy > 1.f ? s.maxAnisotropy : 1.f;
    b.aniso_ = std::min(std::max(aniso, 1.f), std::max(maxDeviceAnisotropy, 1.f));
    return b;
}

wgpu::Sampler SamplerBuilder::build(const wgpu::Device &device) const {
    if (!device) throw Exception("SamplerBuilder::build: null device");

    // Build the C++ wrapper descriptor directly: its zero-init already matches
    // the WebGPU defaults (no reinterpret_cast, no hand-rolled default masks).
    wgpu::SamplerDescriptor d{};
    d.label = label_;
    d.addressModeU = addrU_;
    d.addressModeV = addrV_;
    d.addressModeW = addrW_;
    d.magFilter = mag_;
    d.minFilter = min_;
    d.mipmapFilter = mip_;
    d.lodMinClamp = lodMin_;
    d.lodMaxClamp = lodMax_;
    d.maxAnisotropy = static_cast<uint16_t>(aniso_);
    if (hasCompare_) d.compare = compare_;
    return device.CreateSampler(&d);
}

}  // namespace eve::graphics::webgpu