#pragma once

#include "graphics/webgpu/wgpu_types.h"

#include <vector>

namespace eve::graphics::webgpu {

/**
 * @brief Fluent builder for wgpu::BindGroupLayout.
 *
 * Builds the C++ wrapper descriptor directly (whose zero-init already matches
 * the WebGPU defaults) instead of a hand-written WGPUBindGroupLayoutEntry array
 * + reinterpret_cast. Mirrors vk-bootstrap's DescriptorSetBuilder.
 */
class BindGroupLayoutBuilder {
public:
    /** @brief Default: empty layout with no entries. */
    BindGroupLayoutBuilder() = default;

    /** @brief Append a buffer binding (uniform / storage / read-only storage). */
    BindGroupLayoutBuilder &buffer(uint32_t binding, wgpu::ShaderStage visibility,
                                   wgpu::BufferBindingType type, bool dynamicOffset,
                                   uint64_t minBindingSize) {
        wgpu::BindGroupLayoutEntry e{};
        e.binding = binding;
        e.visibility = visibility;
        e.buffer = {nullptr, type, dynamicOffset, minBindingSize};
        entries_.push_back(std::move(e));
        return *this;
    }

    /** @brief Append a sampled-texture binding. */
    BindGroupLayoutBuilder &texture(uint32_t binding, wgpu::ShaderStage visibility,
                                    wgpu::TextureSampleType sampleType,
                                    wgpu::TextureViewDimension dim, bool multisampled = false) {
        wgpu::BindGroupLayoutEntry e{};
        e.binding = binding;
        e.visibility = visibility;
        e.texture = {nullptr, sampleType, dim, multisampled};
        entries_.push_back(std::move(e));
        return *this;
    }

    /** @brief Append a sampler binding (filtering or comparison). */
    BindGroupLayoutBuilder &sampler(uint32_t binding, wgpu::ShaderStage visibility,
                                    wgpu::SamplerBindingType type) {
        wgpu::BindGroupLayoutEntry e{};
        e.binding = binding;
        e.visibility = visibility;
        e.sampler = {nullptr, type};
        entries_.push_back(std::move(e));
        return *this;
    }

    /** @brief Number of entries accumulated so far. */
    size_t entryCount() const { return entries_.size(); }

    /** @brief Create the bind group layout. Throws on null device. */
    wgpu::BindGroupLayout build(const wgpu::Device &device, const char *label) const;

private:
    std::vector<wgpu::BindGroupLayoutEntry> entries_;
};

}  // namespace eve::graphics::webgpu