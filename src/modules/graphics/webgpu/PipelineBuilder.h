#pragma once

#include "graphics/webgpu/wgpu_types.h"
#include "graphics/BlendMode.h"

#include <cstdint>
#include <vector>

namespace eve::graphics::webgpu {

/**
 * @brief Fluent builder for wgpu::RenderPipeline.
 *
 * Builds the C++ wrapper RenderPipelineDescriptor directly, whose zero-init
 * already matches the WebGPU defaults (writeMask=All, sampleMask=0xFFFFFFFF),
 * so the "default discards every fragment" traps in Graphics.cpp cannot occur.
 * Mirrors vk-bootstrap's PipelineBuilder.
 */
class PipelineBuilder {
public:
    PipelineBuilder() = default;

    PipelineBuilder &label(const char *l) {
        label_ = l;
        return *this;
    }
    PipelineBuilder &layout(wgpu::PipelineLayout l) {
        layout_ = l;
        return *this;
    }
    PipelineBuilder &vertexLayout(uint64_t stride, wgpu::VertexStepMode stepMode,
                                  const wgpu::VertexAttribute *attrs, size_t attrCount);
    PipelineBuilder &shader(wgpu::ShaderModule vert, const char *vertEntry,
                            wgpu::ShaderModule frag, const char *fragEntry);
    PipelineBuilder &topology(wgpu::PrimitiveTopology t) {
        topology_ = t;
        return *this;
    }
    PipelineBuilder &cull(wgpu::CullMode c) {
        cullMode_ = c;
        return *this;
    }
    PipelineBuilder &frontFace(wgpu::FrontFace f) {
        frontFace_ = f;
        return *this;
    }
    /** @brief Set color target format + blend mode (Alpha default = no blend state override risk). */
    PipelineBuilder &colorTarget(WGPUTextureFormat format, eve::graphics::BlendMode mode);
    PipelineBuilder &depth(WGPUTextureFormat format, wgpu::CompareFunction compare,
                           bool writeEnabled);
    PipelineBuilder &sampleCount(uint32_t count) {
        sampleCount_ = count;
        return *this;
    }

    /** @brief Create the pipeline. Throws on null device. */
    wgpu::RenderPipeline build(const wgpu::Device &device) const;

private:
    const char *label_ = "eve_pipeline";
    wgpu::PipelineLayout layout_;
    std::vector<wgpu::VertexBufferLayout> vertexBuffers_;
    wgpu::ShaderModule vertModule_;
    wgpu::ShaderModule fragModule_;
    const char *vertEntry_ = "vs_main";
    const char *fragEntry_ = "fs_main";
    wgpu::PrimitiveTopology topology_ = wgpu::PrimitiveTopology::TriangleList;
    wgpu::CullMode cullMode_ = wgpu::CullMode::None;
    wgpu::FrontFace frontFace_ = wgpu::FrontFace::CCW;
    bool hasColorTarget_ = false;
    wgpu::TextureFormat colorFormat_ = wgpu::TextureFormat::Undefined;
    eve::graphics::BlendMode blendMode_ = eve::graphics::BlendMode::Alpha;
    bool hasDepth_ = false;
    wgpu::TextureFormat depthFormat_ = wgpu::TextureFormat::Undefined;
    wgpu::CompareFunction depthCompare_ = wgpu::CompareFunction::Less;
    bool depthWrite_ = true;
    uint32_t sampleCount_ = 1;
};

}  // namespace eve::graphics::webgpu