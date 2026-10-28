#include "common/Exception.h"
#include "graphics/PrimitiveDrawList.h"
#include "graphics/PrimitiveTessellator.h"
#include "graphics/webgpu/Graphics.h"
#include "graphics/webgpu/PipelineBuilder.h"

#include <algorithm>

namespace eve::graphics::webgpu {
namespace {

wgpu::ShaderModule primitiveShader(wgpu::Device &device, const char *source) {
    wgpu::ShaderSourceWGSL wgsl{};
    wgsl.code = source;
    wgpu::ShaderModuleDescriptor descriptor{};
    descriptor.nextInChain = &wgsl;
    return device.CreateShaderModule(&descriptor);
}

}  // namespace

wgpu::RenderPipeline Graphics::getPrimitive3DPipeline(PrimitiveDepthMode depth, BlendMode blend, PrimitiveCullMode cull,
                                                      WGPUTextureFormat format, uint32_t sampleCount) {
    const uint64_t key = static_cast<uint64_t>(depth) | (static_cast<uint64_t>(blend) << 2u) |
                         (static_cast<uint64_t>(cull) << 5u) | (static_cast<uint64_t>(uint32_t(format)) << 8u) |
                         (static_cast<uint64_t>(sampleCount) << 40u);
    if (auto found = primitive3DPipelines.find(key); found != primitive3DPipelines.end()) return found->second;

    static constexpr const char *kPrimitiveVert = R"wgsl(
struct VSIn { @location(0) clipPosition: vec4f, @location(1) color: vec4f };
struct VSOut { @builtin(position) position: vec4f, @location(0) color: vec4f };
@vertex fn vs_main(input: VSIn) -> VSOut {
    var output: VSOut;
    output.position = input.clipPosition;
    // Match the Vulkan-convention clip coordinates used by the shared resolver.
    output.position.y = -output.position.y;
    output.color = input.color;
    return output;
}
)wgsl";
    static constexpr const char *kPrimitiveFrag = R"wgsl(
@fragment fn fs_main(@location(0) color: vec4f) -> @location(0) vec4f { return color; }
)wgsl";
    wgpu::VertexAttribute        attributes[2]{};
    attributes[0].format         = wgpu::VertexFormat::Float32x4;
    attributes[0].offset         = offsetof(Primitive3DVertex, clipPosition);
    attributes[0].shaderLocation = 0;
    attributes[1].format         = wgpu::VertexFormat::Float32x4;
    attributes[1].offset         = offsetof(Primitive3DVertex, color);
    attributes[1].shaderLocation = 1;
    PipelineBuilder builder;
    builder.label("eve_primitive3d")
        .vertexLayout(sizeof(Primitive3DVertex), wgpu::VertexStepMode::Vertex, attributes, 2)
        .shader(primitiveShader(device, kPrimitiveVert), "vs_main", primitiveShader(device, kPrimitiveFrag), "fs_main")
        .colorTarget(format, blend)
        .depth(WGPUTextureFormat_Depth32Float,
               depth == PrimitiveDepthMode::Ignore ? static_cast<wgpu::CompareFunction>(WGPUCompareFunction_Always)
                                                   : wgpu::CompareFunction::LessEqual,
               depth == PrimitiveDepthMode::TestAndWrite)
        .sampleCount(sampleCount)
        .frontFace(wgpu::FrontFace::CCW)
        .cull(cull == PrimitiveCullMode::Back
                  ? wgpu::CullMode::Back
                  : (cull == PrimitiveCullMode::Front ? wgpu::CullMode::Front
                                                      : static_cast<wgpu::CullMode>(WGPUCullMode_None)));
    wgpu::RenderPipeline pipeline = builder.build(device);
    primitive3DPipelines.emplace(key, pipeline);
    return pipeline;
}

void Graphics::drawPrimitiveScene(const PrimitiveSceneCanvas3D &canvas) {
    if (!frame3DStarted) throw Exception("drawPrimitiveScene: call begin3DFrame first");
    const ResolvedPrimitiveTriangles resolved = resolvePrimitiveStrokes3D(canvas);
    if (resolved.vertices.empty()) return;

    const std::size_t firstSequence = primitive3DDraws.size();
    primitive3DDraws.reserve(firstSequence + resolved.batches3D.size());
    for (const ResolvedPrimitiveBatch3D &batch : resolved.batches3D) {
        Primitive3DDraw draw;
        draw.paint        = batch.paint;
        draw.averageDepth = batch.averageDepth;
        draw.sequence     = firstSequence + batch.sequence;
        draw.vertices.reserve(batch.vertexCount);
        for (std::size_t vertex = batch.firstVertex; vertex < batch.firstVertex + batch.vertexCount; ++vertex)
            draw.vertices.push_back({resolved.vertices[vertex].clipPosition, resolved.vertices[vertex].color});
        primitive3DDraws.push_back(std::move(draw));
    }
}

void Graphics::flushPrimitive3D(wgpu::RenderPassEncoder pass, WGPUTextureFormat format, uint32_t sampleCount) {
    if (primitive3DDraws.empty()) return;
    std::stable_sort(primitive3DDraws.begin(), primitive3DDraws.end(),
                     [](const Primitive3DDraw &a, const Primitive3DDraw &b) {
                         const bool ignoreA = a.paint.depth == PrimitiveDepthMode::Ignore;
                         const bool ignoreB = b.paint.depth == PrimitiveDepthMode::Ignore;
                         if (ignoreA != ignoreB) return !ignoreA;
                         if (a.paint.layer != b.paint.layer) return a.paint.layer < b.paint.layer;
                         const bool transparentA = a.paint.blend != BlendMode::Opaque || a.paint.color.a < 1.f;
                         const bool transparentB = b.paint.blend != BlendMode::Opaque || b.paint.color.a < 1.f;
                         if (transparentA != transparentB) return !transparentA;
                         if (transparentA && a.averageDepth != b.averageDepth) return a.averageDepth > b.averageDepth;
                         return a.sequence < b.sequence;
                     });
    auto       &arena      = currentVertexArena();
    std::size_t totalBytes = 0;
    for (const Primitive3DDraw &draw : primitive3DDraws) totalBytes += draw.vertices.size() * sizeof(Primitive3DVertex);
    ensureVertexArena(arena, arena.used + totalBytes);
    std::vector<Primitive3DVertex> vertices;
    vertices.reserve(totalBytes / sizeof(Primitive3DVertex));
    for (const Primitive3DDraw &draw : primitive3DDraws)
        vertices.insert(vertices.end(), draw.vertices.begin(), draw.vertices.end());
    const uint64_t offset = arena.alloc(totalBytes);
    queue.WriteBuffer(arena.buffer, offset, vertices.data(), totalBytes);
    pass.SetVertexBuffer(0, arena.buffer, offset, totalBytes);
    uint32_t firstVertex = 0;
    for (const Primitive3DDraw &draw : primitive3DDraws) {
        if (draw.vertices.empty()) continue;
        pass.SetPipeline(
            getPrimitive3DPipeline(draw.paint.depth, draw.paint.blend, draw.paint.cull, format, sampleCount));
        pass.Draw(static_cast<uint32_t>(draw.vertices.size()), 1, firstVertex, 0);
        firstVertex += static_cast<uint32_t>(draw.vertices.size());
    }
    primitive3DDraws.clear();
}

}  // namespace eve::graphics::webgpu
