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
#include "graphics/vulkan/ShaderResourceReload.h"

namespace eve::graphics::vulkan {
namespace {

void destroyCandidate(vkb::Device &device, GpuShader &candidate) {
    if (candidate.swapchainPipeline) device->destroyPipeline(candidate.swapchainPipeline);
    if (candidate.swapchainOpaquePipeline) device->destroyPipeline(candidate.swapchainOpaquePipeline);
    if (candidate.offscreenPipeline) device->destroyPipeline(candidate.offscreenPipeline);
    if (candidate.offscreenOpaquePipeline) device->destroyPipeline(candidate.offscreenOpaquePipeline);
    if (candidate.hdrOffscreenPipeline) device->destroyPipeline(candidate.hdrOffscreenPipeline);
    if (candidate.hdrOffscreenOpaquePipeline) device->destroyPipeline(candidate.hdrOffscreenOpaquePipeline);
    if (candidate.mesh3dPipeline) device->destroyPipeline(candidate.mesh3dPipeline);
    if (candidate.mesh3dOffscreenPipeline) device->destroyPipeline(candidate.mesh3dOffscreenPipeline);
    if (candidate.mesh3dHdrOffscreenPipeline) device->destroyPipeline(candidate.mesh3dHdrOffscreenPipeline);
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
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "shader is not a live resource owned by this Graphics instance", "shader", {},
                                                   "graphics.vulkan.shader_reload"));
    if (fragSpv.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "fragment SPIR-V must not be empty", "fragSpv", {},
                                                   "graphics.vulkan.shader_reload"));
    if (fragSpv.front() != 0x07230203 ||
        (!vertSpv.empty() && vertSpv.front() != 0x07230203))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "SPIR-V magic mismatch", "source", {},
                                                   "graphics.vulkan.shader_reload"));

    auto *current = static_cast<GpuShader *>(shader.gpuHandle);
    auto  resourceValidation = validateMeshResourceShaderReload(*current, vertSpv, fragSpv);
    if (!resourceValidation) return resourceValidation;
    auto gpuIt = std::find_if(ownedGpuShaders.begin(), ownedGpuShaders.end(),
                              [&](const std::unique_ptr<GpuShader> &owned) {
                                  return owned.get() == current;
                              });
    if (gpuIt == ownedGpuShaders.end())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "shader GPU resource is no longer owned by this Graphics instance", "shader.gpuHandle", {},
                                                   "graphics.vulkan.shader_reload"));

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
            // Every render pass the shader owns a pipeline for must be replaced: the
            // renderer binds the opaque variant for opaque batches, so rebuilding only
            // the blended pipelines would keep drawing the previous fragment stage there.
            candidate.swapchainPipeline =
                createTexturedStylePipeline(vert, fragSpv, renderpass, candidate.pipelineLayout);
            candidate.swapchainOpaquePipeline = createTexturedStylePipeline(
                vert, fragSpv, renderpass, candidate.pipelineLayout, BlendMode::Opaque);
            if (offscreenRenderPass) {
                candidate.offscreenPipeline = createTexturedStylePipeline(
                    vert, fragSpv, offscreenRenderPass, candidate.pipelineLayout);
                candidate.offscreenOpaquePipeline = createTexturedStylePipeline(
                    vert, fragSpv, offscreenRenderPass, candidate.pipelineLayout, BlendMode::Opaque);
            }
            if (hdrOffscreenRenderPass) {
                candidate.hdrOffscreenPipeline = createTexturedStylePipeline(
                    vert, fragSpv, hdrOffscreenRenderPass, candidate.pipelineLayout);
                candidate.hdrOffscreenOpaquePipeline = createTexturedStylePipeline(
                    vert, fragSpv, hdrOffscreenRenderPass, candidate.pipelineLayout, BlendMode::Opaque);
            }
        } else if (candidate.isHair3D) {
            candidate.mesh3dPipeline = createMesh3DHairPipeline(
                vert, fragSpv, candidate.pipelineLayout, activeScenePass(), activeSceneSamples());
        } else {
            candidate.mesh3dPipeline = createMesh3DStylePipeline(
                vert, fragSpv, candidate.pipelineLayout, activeScenePass(), activeSceneSamples(), shader.meshBlend,
                shader.meshDepthWrite, shader.meshDoubleSided, shader.meshRasterState());
            candidate.mesh3dOffscreenPipeline = createMesh3DStylePipeline(
                vert, fragSpv, candidate.pipelineLayout, offscreen3DRenderPass, vk::SampleCountFlagBits::e1,
                shader.meshBlend, shader.meshDepthWrite, shader.meshDoubleSided, shader.meshRasterState());
            candidate.mesh3dHdrOffscreenPipeline = createMesh3DStylePipeline(
                vert, fragSpv, candidate.pipelineLayout, hdrOffscreen3DRenderPass, vk::SampleCountFlagBits::e1,
                shader.meshBlend, shader.meshDepthWrite, shader.meshDoubleSided, shader.meshRasterState());
            candidate.mesh3dXrayPipeline = createMesh3DXrayPipeline(
                vert, fragSpv, candidate.pipelineLayout, activeScenePass(), activeSceneSamples());
        }
    } catch (const std::exception &error) {
        destroyCandidate(device, candidate);
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, std::move(std::string("failed to prepare replacement pipeline: ") + error.what()), "pipeline", {},
                                                   "graphics.vulkan.shader_reload"));
    } catch (...) {
        destroyCandidate(device, candidate);
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "failed to prepare replacement pipeline", "pipeline", {},
                                                   "graphics.vulkan.shader_reload"));
    }

    waitForSharedGpuResources();
    if (current->swapchainPipeline) device->destroyPipeline(current->swapchainPipeline);
    if (current->swapchainOpaquePipeline) device->destroyPipeline(current->swapchainOpaquePipeline);
    if (current->offscreenPipeline) device->destroyPipeline(current->offscreenPipeline);
    if (current->offscreenOpaquePipeline) device->destroyPipeline(current->offscreenOpaquePipeline);
    if (current->hdrOffscreenPipeline) device->destroyPipeline(current->hdrOffscreenPipeline);
    if (current->hdrOffscreenOpaquePipeline) device->destroyPipeline(current->hdrOffscreenOpaquePipeline);
    if (current->mesh3dPipeline) device->destroyPipeline(current->mesh3dPipeline);
    if (current->mesh3dXrayPipeline) device->destroyPipeline(current->mesh3dXrayPipeline);
    if (current->isMesh3D && !current->isHair3D) {
        if (current->mesh3dOffscreenPipeline) device->destroyPipeline(current->mesh3dOffscreenPipeline);
        if (current->mesh3dHdrOffscreenPipeline) device->destroyPipeline(current->mesh3dHdrOffscreenPipeline);
        current->mesh3dOffscreenPipeline    = candidate.mesh3dOffscreenPipeline;
        current->mesh3dHdrOffscreenPipeline = candidate.mesh3dHdrOffscreenPipeline;
    }
    current->swapchainPipeline = candidate.swapchainPipeline;
    current->swapchainOpaquePipeline = candidate.swapchainOpaquePipeline;
    current->offscreenPipeline = candidate.offscreenPipeline;
    current->offscreenOpaquePipeline = candidate.offscreenOpaquePipeline;
    current->hdrOffscreenPipeline = candidate.hdrOffscreenPipeline;
    current->hdrOffscreenOpaquePipeline = candidate.hdrOffscreenOpaquePipeline;
    current->mesh3dPipeline = candidate.mesh3dPipeline;
    current->mesh3dXrayPipeline = candidate.mesh3dXrayPipeline;
    shader.setSpirv(std::move(vert), fragSpv);
    return Result<void>::success();
}

Result<void> Graphics::replaceShaderFromWgsl(Shader &, const std::string &,
                                             const std::string &) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, "WGSL shader replacement is unavailable on the Vulkan backend", "source", {},
                                                   "graphics.vulkan.shader_reload"));
}

Result<void> Graphics::replaceShaderFromGlsl(Shader &shader, const std::string &vertGlsl,
                                             const std::string &fragGlsl) {
    if (fragGlsl.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "fragment GLSL must not be empty", "fragGlsl", {},
                                                   "graphics.vulkan.shader_reload"));
    auto shaderIt = std::find_if(ownedShaders.begin(), ownedShaders.end(),
                                 [&](const std::unique_ptr<Shader> &owned) {
                                     return owned.get() == &shader;
                                 });
    if (shaderIt == ownedShaders.end() || !shader.gpuHandle)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::StaleHandle, "shader is not a live resource owned by this Graphics instance", "shader", {},
                                                   "graphics.vulkan.shader_reload"));
    auto *current = static_cast<GpuShader *>(shader.gpuHandle);
    if (current->isHair3D)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Unsupported, std::move("runtime GLSL replacement does not yet provide the hair vertex "
                             "contract; publish SPIR-V stages instead"), "shader.kind", {},
                                                   "graphics.vulkan.shader_reload"));

    Shader *candidate = nullptr;
    try {
        candidate = shader.getKind() == Shader::Kind::eMesh3D
                        ? newMeshShader(vertGlsl, fragGlsl)
                        : newShader(vertGlsl, fragGlsl);
    } catch (const Exception &error) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, std::move(error.what()), "source", {},
                                                   "graphics.vulkan.shader_reload"));
    } catch (const std::exception &error) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, std::move(error.what()), "compiler", {},
                                                   "graphics.vulkan.shader_reload"));
    } catch (...) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "GLSL compilation failed", "compiler", {},
                                                   "graphics.vulkan.shader_reload"));
    }

    auto replaced = replaceShaderFromSpv(shader, candidate->vertexSpirv(),
                                         candidate->fragmentSpirv());
    const bool released = releaseShader(candidate);
    if (released) delete candidate;
    if (!released && replaced.ok())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation, "temporary compiled shader could not be released", "candidate", {},
                                                   "graphics.vulkan.shader_reload"));
    return replaced;
}

}  // namespace eve::graphics::vulkan
