#include "graphics/webgpu/GraphicsPipelineSupport.h"

namespace eve::graphics::webgpu::detail {
wgpu::Sampler createLinearSampler(wgpu::Device& dev) {
    WGPUSamplerDescriptor d{};
    d.label         = sv("eve_linear");
    d.addressModeU  = WGPUAddressMode_ClampToEdge;
    d.addressModeV  = WGPUAddressMode_ClampToEdge;
    d.addressModeW  = WGPUAddressMode_ClampToEdge;
    d.magFilter     = WGPUFilterMode_Linear;
    d.minFilter     = WGPUFilterMode_Linear;
    d.mipmapFilter  = WGPUMipmapFilterMode_Linear;
    d.lodMinClamp   = 0.f;
    d.lodMaxClamp   = 1000.f;
    d.maxAnisotropy = 1;
    return dev.CreateSampler(reinterpret_cast<const wgpu::SamplerDescriptor*>(&d));
}
WGPUShaderModuleDescriptor mdDesc(const std::string& code) {
    static WGPUShaderSourceWGSL wd{};
    wd.chain.sType = WGPUSType_ShaderSourceWGSL;
    wd.code        = sv(code.c_str());
    WGPUShaderModuleDescriptor md{};
    md.nextInChain = &wd.chain;
    return md;
}

void fillVertexLayout(WGPUVertexBufferLayout& layout, uint64_t stride, const WGPUVertexAttribute* attrs,
                      uint32_t attrCount) {
    layout.arrayStride    = stride;
    layout.stepMode       = WGPUVertexStepMode_Vertex;
    layout.attributeCount = attrCount;
    layout.attributes     = attrs;
}

WGPUBlendState alphaBlend() {
    WGPUBlendState b{};
    b.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    b.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    b.color.operation = WGPUBlendOperation_Add;
    b.alpha.srcFactor = WGPUBlendFactor_One;
    b.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    b.alpha.operation = WGPUBlendOperation_Add;
    return b;
}

WGPUBlendState noBlend() {
    WGPUBlendState b{};
    b.color.srcFactor = WGPUBlendFactor_One;
    b.color.dstFactor = WGPUBlendFactor_Zero;
    b.color.operation = WGPUBlendOperation_Add;
    b.alpha.srcFactor = WGPUBlendFactor_One;
    b.alpha.dstFactor = WGPUBlendFactor_Zero;
    b.alpha.operation = WGPUBlendOperation_Add;
    return b;
}

WGPUBlendState additiveBlend() {
    WGPUBlendState b{};
    b.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    b.color.dstFactor = WGPUBlendFactor_One;
    b.color.operation = WGPUBlendOperation_Add;
    b.alpha.srcFactor = WGPUBlendFactor_One;
    b.alpha.dstFactor = WGPUBlendFactor_One;
    b.alpha.operation = WGPUBlendOperation_Add;
    return b;
}

WGPUBlendState premultipliedBlend() {
    WGPUBlendState b  = alphaBlend();
    b.color.srcFactor = WGPUBlendFactor_One;
    return b;
}

WGPUBlendState multiplyBlend() {
    WGPUBlendState b  = alphaBlend();
    b.color.srcFactor = WGPUBlendFactor_Dst;
    return b;
}

size_t meshPipelineIndex(BlendMode blend, bool depthWrite, bool doubleSided) {
    return size_t(blend) * 4u + (depthWrite ? 2u : 0u) + (doubleSided ? 1u : 0u);
}

WGPUBlendState blendState(BlendMode mode) {
    switch (mode) {
        case BlendMode::Additive: return additiveBlend();
        case BlendMode::Premultiplied: return premultipliedBlend();
        case BlendMode::Multiply: return multiplyBlend();
        case BlendMode::Opaque: return noBlend();
        case BlendMode::Alpha:
        default: return alphaBlend();
    }
}
void fillMeshAttributes(WGPUVertexAttribute (&attrs)[6]) {
    attrs[0].format         = WGPUVertexFormat_Float32x3;
    attrs[0].offset         = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format         = WGPUVertexFormat_Float32x3;
    attrs[1].offset         = 12;
    attrs[1].shaderLocation = 1;
    attrs[2].format         = WGPUVertexFormat_Float32x2;
    attrs[2].offset         = 24;
    attrs[2].shaderLocation = 2;
    attrs[3].format         = WGPUVertexFormat_Uint16x4;
    attrs[3].offset         = 32;
    attrs[3].shaderLocation = 3;
    attrs[4].format         = WGPUVertexFormat_Float32x4;
    attrs[4].offset         = 40;
    attrs[4].shaderLocation = 4;
    attrs[5].format         = WGPUVertexFormat_Float32x4;
    attrs[5].offset         = 56;
    attrs[5].shaderLocation = 5;
}
}  // namespace eve::graphics::webgpu::detail
