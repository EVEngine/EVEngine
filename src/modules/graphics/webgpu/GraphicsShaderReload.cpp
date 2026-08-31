#include "graphics/webgpu/Graphics.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "common/Diagnostic.h"
#include "graphics/Shader.h"

namespace eve::graphics::webgpu {
namespace {

Result<void> reloadFailure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                   "graphics.webgpu.shader_reload"));
}

}  // namespace

Result<void> Graphics::replaceShaderFromSpv(Shader &, const std::vector<uint32_t> &,
                                            const std::vector<uint32_t> &) {
    return reloadFailure(DiagnosticCode::Unsupported,
                         "SPIR-V shader replacement is unavailable on the WebGPU backend",
                         "source");
}

Result<void> Graphics::replaceShaderFromWgsl(Shader &shader, const std::string &vertWgsl,
                                             const std::string &fragWgsl) {
    auto shaderIt = std::find_if(ownedShaders.begin(), ownedShaders.end(),
                                 [&](const std::unique_ptr<Shader> &owned) {
                                     return owned.get() == &shader;
                                 });
    if (shaderIt == ownedShaders.end() || !shader.gpuHandle)
        return reloadFailure(DiagnosticCode::StaleHandle,
                             "shader is not a live resource owned by this Graphics instance",
                             "shader");
    if (fragWgsl.empty())
        return reloadFailure(DiagnosticCode::InvalidArgument,
                             "fragment WGSL must not be empty", "fragWgsl");

    auto *current = static_cast<GpuShader *>(shader.gpuHandle);
    auto gpuIt = std::find_if(ownedGpuShaders.begin(), ownedGpuShaders.end(),
                              [&](const std::unique_ptr<GpuShader> &owned) {
                                  return owned.get() == current;
                              });
    if (gpuIt == ownedGpuShaders.end())
        return reloadFailure(DiagnosticCode::StaleHandle,
                             "shader GPU resource is no longer owned by this Graphics instance",
                             "shader.gpuHandle");

    GpuShader candidate;
    candidate.isMesh3D = current->isMesh3D;
    candidate.isHair3D = current->isHair3D;
    candidate.pipelineLayout = current->pipelineLayout;
    candidate.setLayout = current->setLayout;
    candidate.wgslVert = vertWgsl.empty() ? current->wgslVert : vertWgsl;
    candidate.wgslFrag = fragWgsl;
    if (candidate.wgslVert.empty())
        return reloadFailure(DiagnosticCode::PreconditionViolation,
                             "shader has no reusable default vertex WGSL", "vertWgsl");

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
                &candidate, sceneColorFormat, true, true, candidate.isHair3D, false, false,
                mesh3dPipelineLayout);
            if (!candidate.isHair3D)
                candidate.mesh3dXrayPipeline = createPipelineForShader(
                    &candidate, sceneColorFormat, false, true, false, false, false,
                    mesh3dPipelineLayout);
        }
    } catch (const std::exception &error) {
        return reloadFailure(DiagnosticCode::Failed,
                             std::string("failed to prepare replacement pipeline: ") + error.what(),
                             "pipeline");
    } catch (...) {
        return reloadFailure(DiagnosticCode::Failed,
                             "failed to prepare replacement pipeline", "pipeline");
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
    return reloadFailure(DiagnosticCode::Unsupported,
                         "runtime GLSL replacement is unavailable on the WebGPU backend",
                         "source");
}

}  // namespace eve::graphics::webgpu
