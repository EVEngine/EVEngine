#include "graphics/PrimitiveDrawList.h"
#include "graphics/PrimitiveTessellator.h"
#include "graphics/vulkan/Canvas.h"
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GraphicsInternal.h"

namespace eve::graphics::vulkan {

void Graphics::drawPrimitiveScene(const PrimitiveSceneCanvas3D &canvas) {
    ASSERT(initialized);
    if (!initialized) throw Exception("drawPrimitiveScene: graphics not initialized");
    const bool offscreen = offscreen3DPassOpen;
    if (!offscreen && gpuDrivenScenePassPending_) gpuDrivenOpenScenePass();
    if (!offscreen && !swapchainPassOpen) throw Exception("drawPrimitiveScene: call begin3DFrame first");

    const ResolvedPrimitiveTriangles resolved = resolvePrimitiveStrokes3D(canvas);
    if (resolved.vertices.empty()) return;

    struct DrawGroup {
        ScenePrimitivePaint paint;
        std::size_t         sequence     = 0;
        float               averageDepth = 0.f;
        std::size_t         firstVertex  = 0;
        std::size_t         vertexCount  = 0;
    };
    std::vector<DrawGroup> groups;
    groups.reserve(resolved.batches3D.size());
    for (const ResolvedPrimitiveBatch3D &batch : resolved.batches3D) {
        DrawGroup group;
        group.paint        = batch.paint;
        group.sequence     = batch.sequence;
        group.averageDepth = batch.averageDepth;
        group.firstVertex  = batch.firstVertex;
        group.vertexCount  = batch.vertexCount;
        groups.push_back(std::move(group));
    }
    std::stable_sort(groups.begin(), groups.end(), [&](const DrawGroup &a, const DrawGroup &b) {
        const ScenePrimitivePaint &paintA  = a.paint;
        const ScenePrimitivePaint &paintB  = b.paint;
        const bool                 ignoreA = paintA.depth == PrimitiveDepthMode::Ignore;
        const bool                 ignoreB = paintB.depth == PrimitiveDepthMode::Ignore;
        if (ignoreA != ignoreB) return !ignoreA;
        if (paintA.layer != paintB.layer) return paintA.layer < paintB.layer;
        const bool transparentA = paintA.blend != BlendMode::Opaque || paintA.color.a < 1.f;
        const bool transparentB = paintB.blend != BlendMode::Opaque || paintB.color.a < 1.f;
        if (transparentA != transparentB) return !transparentA;
        if (transparentA && a.averageDepth != b.averageDepth) return a.averageDepth > b.averageDepth;
        return a.sequence < b.sequence;
    });

    auto &buffers   = offscreen ? offscreenPrimitive3DBufs : currentFrame2DBuffers().primitive3DBufs;
    auto &drawIndex = offscreen ? offscreenPrimitive3DDrawIndex : currentFrame2DBuffers().primitive3DDrawIndex;
    if (drawIndex == buffers.size()) buffers.emplace_back();
    // Earlier submissions still reference their buffers until this frame's fence.
    auto                   &buffer = buffers[drawIndex++];
    const vk::CommandBuffer cb     = offscreen ? offscreen3DCB : currentPresentCb();
    const uint32_t          targetWidth =
        offscreen ? static_cast<uint32_t>(offscreen3DCanvas->getWidth()) : swapchain.extent.width;
    const uint32_t targetHeight =
        offscreen ? static_cast<uint32_t>(offscreen3DCanvas->getHeight()) : swapchain.extent.height;
    auto &pipelines = offscreen
                          ? (offscreen3DHDRActive ? hdrOffscreenPrimitive3DPipelines : offscreenPrimitive3DPipelines)
                          : primitive3DPipelines;
    setViewportAndScissor(cb, targetWidth, targetHeight);
    std::vector<Primitive3DVertex> vertices;
    vertices.reserve(resolved.vertices.size());
    for (const DrawGroup &group : groups) {
        for (std::size_t vertex = group.firstVertex; vertex < group.firstVertex + group.vertexCount; ++vertex)
            vertices.push_back({resolved.vertices[vertex].clipPosition, resolved.vertices[vertex].color});
    }
    buffer.allocate<Primitive3DVertex>(frameToken(), device, vertices);
    const vk::DeviceSize offset = 0;
    cb.bindVertexBuffers(0, 1, buffer, &offset);
    std::uint32_t firstVertex = 0;
    for (std::size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        const DrawGroup           &group             = groups[groupIndex];
        const ScenePrimitivePaint &paint             = group.paint;
        const std::size_t          pipelineIndex     = primitive3DPipelineIndex(paint.depth, paint.blend, paint.cull);
        const vk::Pipeline         primitivePipeline = pipelines[pipelineIndex];
        if (!primitivePipeline) throw Exception("drawPrimitiveScene: primitive pipeline unavailable");
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, primitivePipeline);
        cb.draw(static_cast<uint32_t>(group.vertexCount), 1, firstVertex, 0);
        firstVertex += static_cast<std::uint32_t>(group.vertexCount);
    }
}

}  // namespace eve::graphics::vulkan
