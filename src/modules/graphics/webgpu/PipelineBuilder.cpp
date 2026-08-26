#include "graphics/webgpu/PipelineBuilder.h"

#include "common/Exception.h"

namespace eve::graphics::webgpu {

PipelineBuilder &PipelineBuilder::vertexLayout(uint64_t stride, wgpu::VertexStepMode stepMode,
                                               const wgpu::VertexAttribute *attrs,
                                               size_t attrCount) {
    wgpu::VertexBufferLayout l{};
    l.arrayStride = stride;
    l.stepMode = stepMode;
    l.attributeCount = attrCount;
    l.attributes = attrs;
    vertexBuffers_.push_back(l);
    return *this;
}

PipelineBuilder &PipelineBuilder::shader(wgpu::ShaderModule vert, const char *vertEntry,
                                         wgpu::ShaderModule frag, const char *fragEntry) {
    vertModule_ = vert;
    fragModule_ = frag;
    vertEntry_ = vertEntry;
    fragEntry_ = fragEntry;
    return *this;
}

PipelineBuilder &PipelineBuilder::colorTarget(WGPUTextureFormat format,
                                              eve::graphics::BlendMode mode) {
    hasColorTarget_ = true;
    colorFormat_ = static_cast<wgpu::TextureFormat>(format);
    blendMode_ = mode;
    return *this;
}

PipelineBuilder &PipelineBuilder::depth(WGPUTextureFormat format, wgpu::CompareFunction compare,
                                        bool writeEnabled) {
    hasDepth_ = true;
    depthFormat_ = static_cast<wgpu::TextureFormat>(format);
    depthCompare_ = compare;
    depthWrite_ = writeEnabled;
    return *this;
}

wgpu::RenderPipeline PipelineBuilder::build(const wgpu::Device &device) const {
    if (!device) throw Exception("PipelineBuilder::build: null device");
    if (!vertModule_) throw Exception("PipelineBuilder::build: no vertex shader");
    if (!hasColorTarget_ && !hasDepth_)
        throw Exception("PipelineBuilder::build: no color or depth target");

    wgpu::RenderPipelineDescriptor d{};
    d.label = label_;
    d.layout = layout_;
    d.multisample.count = sampleCount_;
    // C++ wrapper default mask = 0xFFFFFFFF (all samples); zero-init is safe.

    // Vertex state.
    d.vertex.module = vertModule_;
    d.vertex.entryPoint = vertEntry_;
    d.vertex.bufferCount = static_cast<uint32_t>(vertexBuffers_.size());
    d.vertex.buffers = vertexBuffers_.data();

    // Primitive state.
    d.primitive.topology = topology_;
    d.primitive.frontFace = frontFace_;
    d.primitive.cullMode = cullMode_;
    d.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;

    // Depth/stencil.
    wgpu::DepthStencilState ds{};
    if (hasDepth_) {
        ds.format = depthFormat_;
        ds.depthCompare = depthCompare_;
        ds.depthWriteEnabled = depthWrite_;
        d.depthStencil = &ds;
    }

    // Fragment state.
    wgpu::ColorTargetState target{};
    wgpu::FragmentState fs{};
    wgpu::BlendState blend{};
    if (hasColorTarget_) {
        target.format = colorFormat_;
        // C++ wrapper default writeMask = All; only override blend explicitly.
        target.blend = nullptr;
        switch (blendMode_) {
        case eve::graphics::BlendMode::Alpha:
            blend.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::SrcAlpha,
                           wgpu::BlendFactor::OneMinusSrcAlpha};
            blend.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One,
                           wgpu::BlendFactor::OneMinusSrcAlpha};
            target.blend = &blend;
            break;
        case eve::graphics::BlendMode::Additive:
            blend.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::SrcAlpha,
                           wgpu::BlendFactor::One};
            blend.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One,
                           wgpu::BlendFactor::One};
            target.blend = &blend;
            break;
        case eve::graphics::BlendMode::Premultiplied:
            blend.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One,
                           wgpu::BlendFactor::OneMinusSrcAlpha};
            blend.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One,
                           wgpu::BlendFactor::OneMinusSrcAlpha};
            target.blend = &blend;
            break;
        case eve::graphics::BlendMode::Multiply:
            blend.color = {wgpu::BlendOperation::Add, wgpu::BlendFactor::Dst,
                           wgpu::BlendFactor::OneMinusSrcAlpha};
            blend.alpha = {wgpu::BlendOperation::Add, wgpu::BlendFactor::One,
                           wgpu::BlendFactor::OneMinusSrcAlpha};
            target.blend = &blend;
            break;
        case eve::graphics::BlendMode::Opaque:
        default:
            target.blend = nullptr;
            break;
        }
        fs.module = fragModule_;
        fs.entryPoint = fragEntry_;
        fs.targetCount = 1;
        fs.targets = &target;
        d.fragment = &fs;
    }

    return device.CreateRenderPipeline(&d);
}

}  // namespace eve::graphics::webgpu