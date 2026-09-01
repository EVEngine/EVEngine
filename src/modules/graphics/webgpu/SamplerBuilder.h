#pragma once

#include "graphics/webgpu/wgpu_types.h"
#include "graphics/TextureSampler.h"

#include <cstdint>

namespace eve::graphics::webgpu {

/**
 * @brief Fluent builder for wgpu::Sampler.
 *
 * The constructor already writes WebGPU-legal defaults (clamp-to-edge, linear,
 * no mips, anisotropy 1), so a sampler is always valid without extra calls —
 * this mirrors vk-bootstrap's "builder default is a valid value" principle and
 * removes the hand-written WGPUSamplerDescriptor zero-init + reinterpret_cast
 * that made default-mask bugs possible in Graphics.cpp.
 */
class SamplerBuilder {
public:
    /** @brief Defaults: clamp-to-edge, linear mag/min, no mip sampling, aniso 1. */
    SamplerBuilder() = default;

    /** @brief Start from an engine TextureSampler (mirrors Graphics::makeSampler). */
    static SamplerBuilder fromEngine(const eve::graphics::TextureSampler &s, uint32_t mipLevels,
                                     float maxDeviceAnisotropy);

    SamplerBuilder &label(const char *l) {
        label_ = l;
        return *this;
    }
    SamplerBuilder &address(wgpu::AddressMode u, wgpu::AddressMode v,
                            wgpu::AddressMode w = wgpu::AddressMode::ClampToEdge) {
        addrU_ = u;
        addrV_ = v;
        addrW_ = w;
        return *this;
    }
    SamplerBuilder &filter(wgpu::FilterMode mag, wgpu::FilterMode min) {
        mag_ = mag;
        min_ = min;
        return *this;
    }
    SamplerBuilder &mipmap(wgpu::MipmapFilterMode mip, float lodMin = 0.f, float lodMax = 1000.f) {
        mip_ = mip;
        lodMin_ = lodMin;
        lodMax_ = lodMax;
        return *this;
    }
    SamplerBuilder &compare(wgpu::CompareFunction cmp) {
        compare_ = cmp;
        hasCompare_ = true;
        return *this;
    }
    SamplerBuilder &anisotropy(float maxAniso) {
        aniso_ = maxAniso > 1.f ? maxAniso : 1.f;
        return *this;
    }

    /** @brief Create the sampler. Throws eve::graphics::Exception on null device. */
    wgpu::Sampler build(const wgpu::Device &device) const;

private:
    const char *label_ = "eve_sampler";
    wgpu::AddressMode addrU_ = wgpu::AddressMode::ClampToEdge;
    wgpu::AddressMode addrV_ = wgpu::AddressMode::ClampToEdge;
    wgpu::AddressMode addrW_ = wgpu::AddressMode::ClampToEdge;
    wgpu::FilterMode mag_ = wgpu::FilterMode::Linear;
    wgpu::FilterMode min_ = wgpu::FilterMode::Linear;
    wgpu::MipmapFilterMode mip_ = wgpu::MipmapFilterMode::Nearest;
    float lodMin_ = 0.f;
    float lodMax_ = 0.f;
    float aniso_ = 1.f;
    bool hasCompare_ = false;
    wgpu::CompareFunction compare_ = wgpu::CompareFunction::Always;
};

}  // namespace eve::graphics::webgpu