#include <algorithm>
#include <array>
#include "graphics/vulkan/Graphics.h"
#include "graphics/vulkan/GraphicsInternal.h"

namespace eve::graphics::vulkan {
eve::Result<void> Graphics::configureMeshShaderSurface(Shader& shader, BlendMode blend, bool depthWrite,
                                                       bool doubleSided) {
    auto found = std::find_if(ownedGpuShaders.begin(), ownedGpuShaders.end(),
                              [&](const auto& g) { return g->owner == &shader; });
    if (found == ownedGpuShaders.end() || !(*found)->isMesh3D || swapchainPassOpen || offscreen3DPassOpen)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Expected an owned mesh shader outside frame submission"));
    auto&                       gpu = **found;
    std::array<vk::Pipeline, 3> prepared{};
    try {
        prepared[0] =
            createMesh3DStylePipeline(shader.vertexSpirv(), shader.fragmentSpirv(), gpu.pipelineLayout,
                                      activeScenePass(), activeSceneSamples(), blend, depthWrite, doubleSided);
        prepared[1] = createMesh3DStylePipeline(shader.vertexSpirv(), shader.fragmentSpirv(), gpu.pipelineLayout,
                                                offscreen3DRenderPass, vk::SampleCountFlagBits::e1, blend, depthWrite,
                                                doubleSided);
        prepared[2] = createMesh3DStylePipeline(shader.vertexSpirv(), shader.fragmentSpirv(), gpu.pipelineLayout,
                                                hdrOffscreen3DRenderPass, vk::SampleCountFlagBits::e1, blend,
                                                depthWrite, doubleSided);
        waitForSharedGpuResources();
    } catch (const std::exception& e) {
        for (auto pipeline : prepared)
            if (pipeline) device->destroyPipeline(pipeline);
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, e.what()));
    }
    for (auto pipeline : {gpu.mesh3dPipeline, gpu.mesh3dOffscreenPipeline, gpu.mesh3dHdrOffscreenPipeline})
        if (pipeline) device->destroyPipeline(pipeline);
    gpu.mesh3dPipeline             = prepared[0];
    gpu.mesh3dOffscreenPipeline    = prepared[1];
    gpu.mesh3dHdrOffscreenPipeline = prepared[2];
    shader.meshBlend               = blend;
    shader.meshDepthWrite          = depthWrite;
    shader.meshDoubleSided         = doubleSided;
    lastMesh3dPipeline             = nullptr;
    return eve::Result<void>::success();
}
}  // namespace eve::graphics::vulkan
