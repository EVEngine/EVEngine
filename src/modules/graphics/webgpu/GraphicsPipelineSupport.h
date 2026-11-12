#pragma once
#include "graphics/webgpu/Graphics.h"

namespace eve::graphics::webgpu::detail {
/** @brief Internal WebGPU pipeline/descriptor helper; render-thread only. */
wgpu::Sampler createLinearSampler(wgpu::Device& dev);
/** @brief Internal WebGPU pipeline/descriptor helper; render-thread only. */
WGPUShaderModuleDescriptor mdDesc(const std::string& code);
/** @brief Internal WebGPU pipeline/descriptor helper; render-thread only. */
void fillVertexLayout(WGPUVertexBufferLayout& layout, uint64_t stride, const WGPUVertexAttribute* attrs,
                      uint32_t attrCount);
/** @brief Internal WebGPU pipeline/descriptor helper; render-thread only. */
WGPUBlendState alphaBlend();
/** @brief Internal WebGPU pipeline/descriptor helper; render-thread only. */
WGPUBlendState noBlend();
/** @brief Internal WebGPU pipeline/descriptor helper; render-thread only. */
WGPUBlendState additiveBlend();
/** @brief Internal WebGPU pipeline/descriptor helper; render-thread only. */
WGPUBlendState premultipliedBlend();
/** @brief Internal WebGPU pipeline/descriptor helper; render-thread only. */
WGPUBlendState multiplyBlend();
/** @brief Internal WebGPU pipeline/descriptor helper; render-thread only. */
size_t meshPipelineIndex(BlendMode blend, bool depthWrite, bool doubleSided);
/** @brief Internal WebGPU pipeline/descriptor helper; render-thread only. */
WGPUBlendState blendState(BlendMode mode);
/** @brief Internal WebGPU pipeline/descriptor helper; render-thread only. */
void fillMeshAttributes(WGPUVertexAttribute (&attrs)[6]);
}  // namespace eve::graphics::webgpu::detail
