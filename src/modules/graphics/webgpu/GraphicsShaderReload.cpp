#include "graphics/webgpu/Graphics.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "common/Diagnostic.h"
#include "graphics/Shader.h"

namespace eve::graphics::webgpu {
namespace {

}  // namespace

Result<void> Graphics::replaceShaderFromSpv(Shader &, const std::vector<uint32_t> &,
                                            const std::vector<uint32_t> &) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "SPIR-V shader replacement is unavailable on the WebGPU backend", "source", {},
                                                   "graphics.webgpu.shader_reload"));
}

Result<void> Graphics::replaceShaderFromWgsl(Shader &shader, const std::string &vertWgsl,
                                             const std::string &fragWgsl) {
    auto shaderIt = std::find_if(ownedShaders.begin(), ownedShaders.end(),
                                 [&](const std::unique_ptr<Shader> &owned) {
                                     return owned.get() == &shader;
                                 });
    if (shaderIt == ownedShaders.end() || !shader.gpuHandle)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "shader is not a live resource owned by this Graphics instance", "shader", {},
                                                   "graphics.webgpu.shader_reload"));
    if (fragWgsl.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "fragment WGSL must not be empty", "fragWgsl", {},
                                                   "graphics.webgpu.shader_reload"));

    auto *current = static_cast<GpuShader *>(shader.gpuHandle);
    auto gpuIt = std::find_if(ownedGpuShaders.begin(), ownedGpuShaders.end(),
                              [&](const std::unique_ptr<GpuShader> &owned) {
                                  return owned.get() == current;
                              });
    if (gpuIt == ownedGpuShaders.end())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "shader GPU resource is no longer owned by this Graphics instance", "shader.gpuHandle", {},
                                                   "graphics.webgpu.shader_reload"));

    GpuShader candidate;
    candidate.isMesh3D = current->isMesh3D;
    candidate.isHair3D = current->isHair3D;
    candidate.pipelineLayout = current->pipelineLayout;
    candidate.setLayout = current->setLayout;
    candidate.wgslVert = vertWgsl.empty() ? current->wgslVert : vertWgsl;
    candidate.wgslFrag = fragWgsl;
    if (candidate.wgslVert.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "shader has no reusable default vertex WGSL", "vertWgsl", {},
                                                   "graphics.webgpu.shader_reload"));

    try {
        if (!candidate.isMesh3D) {
            candidate.swapchainPipeline = createPipelineForShader(
                &candidate, wgpu::TextureFormat(surfaceFormat), false, false, false, false,
                false, tex2DPipelineLayout);
            candidate.offscreenPipeline = createPipelineForShader(
                &candidate, wgpu::TextureFormat::RGBA8Unorm, false, false, false, false,
                false, tex2DPipelineLayout);
        } else {
            candidate.mesh3dPipeline = createPipelineForShader(
                &candidate, static_cast<wgpu::TextureFormat>(sceneColorFormat), true, true,
                candidate.isHair3D, false, false, mesh3dPipelineLayout);
            if (!candidate.isHair3D)
                candidate.mesh3dXrayPipeline = createPipelineForShader(
                    &candidate, static_cast<wgpu::TextureFormat>(sceneColorFormat), false, true,
                    false, false, false, mesh3dPipelineLayout);
        }
    } catch (const std::exception &error) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, std::move(std::string("failed to prepare replacement pipeline: ") + error.what()), "pipeline", {},
                                                   "graphics.webgpu.shader_reload"));
    } catch (...) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "failed to prepare replacement pipeline", "pipeline", {},
                                                   "graphics.webgpu.shader_reload"));
    }

    current->swapchainPipeline = std::move(candidate.swapchainPipeline);
    current->offscreenPipeline = std::move(candidate.offscreenPipeline);
    current->mesh3dPipeline = std::move(candidate.mesh3dPipeline);
    current->mesh3dXrayPipeline = std::move(candidate.mesh3dXrayPipeline);
    current->wgslVert = std::move(candidate.wgslVert);
    current->wgslFrag = std::move(candidate.wgslFrag);
    return Result<void>::success();
}

Result<void> Graphics::replaceShaderFromGlsl(Shader &, const std::string &,
                                             const std::string &) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "runtime GLSL replacement is unavailable on the WebGPU backend", "source", {},
                                                   "graphics.webgpu.shader_reload"));
}

}  // namespace eve::graphics::webgpu
