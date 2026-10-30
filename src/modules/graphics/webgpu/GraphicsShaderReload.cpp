#include "graphics/webgpu/Graphics.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <utility>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

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

    std::string preparationError;
    device.PushErrorScope(wgpu::ErrorFilter::Validation);
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
        preparationError = error.what();
    } catch (...) {
        preparationError = "unknown pipeline preparation error";
    }

    bool            done            = false;
    wgpu::ErrorType validationError = wgpu::ErrorType::Unknown;
    std::string     validationMessage;
    device.PopErrorScope(wgpu::CallbackMode::AllowProcessEvents, [&](wgpu::PopErrorScopeStatus status,
                                                                     wgpu::ErrorType type, wgpu::StringView message) {
        validationError = status == wgpu::PopErrorScopeStatus::Success ? type : wgpu::ErrorType::Unknown;
        if (message.data) {
            const auto length = message.length == WGPU_STRLEN ? std::strlen(message.data) : message.length;
            validationMessage.assign(message.data, length);
        }
        done = true;
    });
    while (!done) {
#if defined(__EMSCRIPTEN__)
        emscripten_sleep(0);
#endif
        wgpuInstanceProcessEvents(instance.Get());
    }
    const bool missingPipeline =
        candidate.isMesh3D ? !candidate.mesh3dPipeline || (!candidate.isHair3D && !candidate.mesh3dXrayPipeline)
                           : !candidate.swapchainPipeline || !candidate.offscreenPipeline;
    if (!preparationError.empty() || validationError != wgpu::ErrorType::NoError || missingPipeline)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed,
                                                       !preparationError.empty() ? preparationError
                                                       : !validationMessage.empty()
                                                           ? validationMessage
                                                           : "WebGPU rejected replacement pipeline",
                                                       "pipeline", {}, "graphics.webgpu.shader_reload"));

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
