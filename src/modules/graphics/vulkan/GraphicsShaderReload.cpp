#include "graphics/vulkan/Graphics.h"

#include <algorithm>
#include <exception>
#include <utility>

#include "common/Diagnostic.h"
#include "common/Exception.h"
#include "graphics/Shader.h"
#include "graphics/shaders/mesh3d_hair_vert_spv.inc"
#include "graphics/shaders/mesh3d_vert_spv.inc"
#include "graphics/shaders/textured_vert_spv.inc"
#include "graphics/vulkan/GraphicsInternal.h"

namespace eve::graphics::vulkan {
namespace {

Result<void> reloadFailure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<void>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                   "graphics.vulkan.shader_reload"));
}

void destroyCandidate(vkb::Device &device, GpuShader &candidate) {
    if (candidate.swapchainPipeline) device->destroyPipeline(candidate.swapchainPipeline);
    if (candidate.offscreenPipeline) device->destroyPipeline(candidate.offscreenPipeline);
    if (candidate.mesh3dPipeline) device->destroyPipeline(candidate.mesh3dPipeline);
    if (candidate.mesh3dXrayPipeline) device->destroyPipeline(candidate.mesh3dXrayPipeline);
}

}  // namespace

Result<void> Graphics::replaceShaderFromSpv(Shader &shader,
                                            const std::vector<uint32_t> &vertSpv,
                                            const std::vector<uint32_t> &fragSpv) {
    auto shaderIt = std::find_if(ownedShaders.begin(), ownedShaders.end(),
                                 [&](const std::unique_ptr<Shader> &owned) {
                                     return owned.get() == &shader;
                                 });
    if (shaderIt == ownedShaders.end() || !shader.gpuHandle)
        return reloadFailure(DiagnosticCode::StaleHandle,
                             "shader is not a live resource owned by this Graphics instance",
                             "shader");
    if (fragSpv.empty())
        return reloadFailure(DiagnosticCode::InvalidArgument,
                             "fragment SPIR-V must not be empty", "fragSpv");
    if (fragSpv.front() != 0x07230203 ||
        (!vertSpv.empty() && vertSpv.front() != 0x07230203))
        return reloadFailure(DiagnosticCode::ParseError, "SPIR-V magic mismatch", "source");

    auto *current = static_cast<GpuShader *>(shader.gpuHandle);
    auto gpuIt = std::find_if(ownedGpuShaders.begin(), ownedGpuShaders.end(),
                              [&](const std::unique_ptr<GpuShader> &owned) {
                                  return owned.get() == current;
                              });
    if (gpuIt == ownedGpuShaders.end())
        return reloadFailure(DiagnosticCode::StaleHandle,
                             "shader GPU resource is no longer owned by this Graphics instance",
                             "shader.gpuHandle");

    std::vector<uint32_t> vert = vertSpv;
    if (vert.empty()) {
        if (!current->isMesh3D)
            vert.assign(textured_vert_spv, textured_vert_spv + textured_vert_spv_count);
        else if (current->isHair3D)
            vert.assign(mesh3d_hair_vert_spv,
                        mesh3d_hair_vert_spv + mesh3d_hair_vert_spv_count);
        else
            vert.assign(mesh3d_vert_spv, mesh3d_vert_spv + mesh3d_vert_spv_count);
    }

    GpuShader candidate;
    candidate.isMesh3D = current->isMesh3D;
    candidate.isHair3D = current->isHair3D;
    candidate.pipelineLayout = current->pipelineLayout;
    candidate.owner = &shader;
    try {
        if (!candidate.isMesh3D) {
            candidate.swapchainPipeline =
                createTexturedStylePipeline(vert, fragSpv, renderpass, candidate.pipelineLayout);
            if (offscreenRenderPass)
                candidate.offscreenPipeline = createTexturedStylePipeline(
                    vert, fragSpv, offscreenRenderPass, candidate.pipelineLayout);
        } else if (candidate.isHair3D) {
            candidate.mesh3dPipeline = createMesh3DHairPipeline(
                vert, fragSpv, candidate.pipelineLayout, activeScenePass(), activeSceneSamples());
        } else {
            candidate.mesh3dPipeline = createMesh3DStylePipeline(
                vert, fragSpv, candidate.pipelineLayout, activeScenePass(), activeSceneSamples());
            candidate.mesh3dXrayPipeline = createMesh3DXrayPipeline(
                vert, fragSpv, candidate.pipelineLayout, activeScenePass(), activeSceneSamples());
        }
    } catch (const std::exception &error) {
        destroyCandidate(device, candidate);
        return reloadFailure(DiagnosticCode::Failed,
                             std::string("failed to prepare replacement pipeline: ") + error.what(),
                             "pipeline");
    } catch (...) {
        destroyCandidate(device, candidate);
        return reloadFailure(DiagnosticCode::Failed,
                             "failed to prepare replacement pipeline", "pipeline");
    }

    waitForSharedGpuResources();
    if (current->swapchainPipeline) device->destroyPipeline(current->swapchainPipeline);
    if (current->offscreenPipeline) device->destroyPipeline(current->offscreenPipeline);
    if (current->mesh3dPipeline) device->destroyPipeline(current->mesh3dPipeline);
    if (current->mesh3dXrayPipeline) device->destroyPipeline(current->mesh3dXrayPipeline);
    current->swapchainPipeline = candidate.swapchainPipeline;
    current->offscreenPipeline = candidate.offscreenPipeline;
    current->mesh3dPipeline = candidate.mesh3dPipeline;
    current->mesh3dXrayPipeline = candidate.mesh3dXrayPipeline;
    shader.setSpirv(std::move(vert), fragSpv);
    return Result<void>::success();
}

Result<void> Graphics::replaceShaderFromWgsl(Shader &, const std::string &,
                                             const std::string &) {
    return reloadFailure(DiagnosticCode::Unsupported,
                         "WGSL shader replacement is unavailable on the Vulkan backend",
                         "source");
}

Result<void> Graphics::replaceShaderFromGlsl(Shader &shader, const std::string &vertGlsl,
                                             const std::string &fragGlsl) {
#if defined(_WIN32)
    (void)shader;
    (void)vertGlsl;
    (void)fragGlsl;
    return reloadFailure(DiagnosticCode::Unsupported,
                         "runtime GLSL replacement is unavailable on Windows Vulkan; compile "
                         "SPIR-V before publication",
                         "source");
#else
    if (fragGlsl.empty())
        return reloadFailure(DiagnosticCode::InvalidArgument,
                             "fragment GLSL must not be empty", "fragGlsl");
    auto shaderIt = std::find_if(ownedShaders.begin(), ownedShaders.end(),
                                 [&](const std::unique_ptr<Shader> &owned) {
                                     return owned.get() == &shader;
                                 });
    if (shaderIt == ownedShaders.end() || !shader.gpuHandle)
        return reloadFailure(DiagnosticCode::StaleHandle,
                             "shader is not a live resource owned by this Graphics instance",
                             "shader");
    auto *current = static_cast<GpuShader *>(shader.gpuHandle);
    if (current->isHair3D)
        return reloadFailure(DiagnosticCode::Unsupported,
                             "runtime GLSL replacement does not yet provide the hair vertex "
                             "contract; publish SPIR-V stages instead",
                             "shader.kind");

    Shader *candidate = nullptr;
    try {
        candidate = shader.getKind() == Shader::Kind::eMesh3D
                        ? newMeshShader(vertGlsl, fragGlsl)
                        : newShader(vertGlsl, fragGlsl);
    } catch (const Exception &error) {
        return reloadFailure(DiagnosticCode::ParseError, error.what(), "source");
    } catch (const std::exception &error) {
        return reloadFailure(DiagnosticCode::Failed, error.what(), "compiler");
    } catch (...) {
        return reloadFailure(DiagnosticCode::Failed, "GLSL compilation failed", "compiler");
    }

    auto replaced = replaceShaderFromSpv(shader, candidate->vertexSpirv(),
                                         candidate->fragmentSpirv());
    const bool released = releaseShader(candidate);
    if (released) delete candidate;
    if (!released && replaced.ok())
        return reloadFailure(DiagnosticCode::InvariantViolation,
                             "temporary compiled shader could not be released", "candidate");
    return replaced;
#endif
}

}  // namespace eve::graphics::vulkan
