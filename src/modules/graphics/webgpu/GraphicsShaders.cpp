#include "common/FrameCapture.h"
#include "graphics/AntiAliasing.h"
#include "graphics/Batcher.h"
#include "graphics/ClusteredLight.h"
#include "graphics/CubemapPrefilter.h"
#include "graphics/GBuffer.h"
#include "graphics/Light.h"
#include "graphics/Mesh.h"
#include "graphics/PrimitiveDrawList.h"
#include "graphics/PrimitiveTessellator.h"
#include "graphics/ReflectionProbeUpload.h"
#include "graphics/RenderControl.h"
#include "graphics/Shader.h"
#include "graphics/Shadow.h"
#include "graphics/Texture.h"
#include "graphics/TextureSampler.h"
#include "graphics/shaders/ReflectionProbeWgsl.h"
#include "graphics/webgpu/BindGroupLayoutBuilder.h"
#include "graphics/webgpu/Canvas.h"
#include "graphics/webgpu/Graphics.h"
#include "graphics/webgpu/GraphicsPipelineSupport.h"
#include "graphics/webgpu/InitFlow.h"
#include "graphics/webgpu/PipelineBuilder.h"
#include "graphics/webgpu/wgsl_shaders.h"

#include "common/Exception.h"
#include "common/Resource.h"
#include "common/config.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "image/Image.h"
#include "image/ImageData.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <glm/gtc/constants.hpp>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace eve::graphics::webgpu {
using namespace detail;

namespace {
template <class Build>
std::string checkedShaderBuild(wgpu::Device& device, wgpu::Instance& instance, Build&& build) {
    device.PushErrorScope(wgpu::ErrorFilter::Validation);
    std::string preparationError;
    try {
        build();
    } catch (const std::exception& error) {
        preparationError = error.what();
    } catch (...) {
        preparationError = "unknown pipeline preparation error";
    }
    bool                      done   = false;
    wgpu::ErrorType           type   = wgpu::ErrorType::Unknown;
    wgpu::PopErrorScopeStatus status = wgpu::PopErrorScopeStatus::Error;
    std::string               message;
    device.PopErrorScope(wgpu::CallbackMode::AllowProcessEvents, [&](wgpu::PopErrorScopeStatus result,
                                                                     wgpu::ErrorType error, wgpu::StringView detail) {
        status = result;
        type   = error;
        if (detail.data) {
            const auto length = detail.length == WGPU_STRLEN ? std::strlen(detail.data) : detail.length;
            message.assign(detail.data, length);
        }
        done = true;
    });
    while (!done) {
#if defined(__EMSCRIPTEN__)
        emscripten_sleep(0);
#endif
        wgpuInstanceProcessEvents(instance.Get());
    }
    if (!preparationError.empty()) return preparationError;
    if (status != wgpu::PopErrorScopeStatus::Success || type != wgpu::ErrorType::NoError)
        return message.empty() ? "WebGPU rejected shader pipeline" : message;
    return {};
}

}  // namespace
namespace {

wgpu::RenderPipeline buildPipelineFromWgsl(wgpu::Device& dev, wgpu::PipelineLayout layout, WGPUTextureFormat format,
                                           const std::string& vert, const std::string& frag, bool depth, bool blend,
                                           bool mesh3d, bool hair, bool shadow, bool gbuffer, uint32_t sampleCount,
                                           bool depthWrite = true) {
    WGPURenderPipelineDescriptor pd{};
    pd.label  = sv("eve_custom_shader");
    pd.layout = layout.Get();

    WGPUVertexAttribute    attrs[6]{};
    WGPUVertexBufferLayout vb{};
    if (mesh3d || shadow || gbuffer) {
        fillMeshAttributes(attrs);
        attrs[0].format         = WGPUVertexFormat_Float32x3;
        attrs[0].offset         = 0;
        attrs[0].shaderLocation = 0;
        attrs[1].format         = WGPUVertexFormat_Float32x3;
        attrs[1].offset         = 12;
        attrs[1].shaderLocation = 1;
        attrs[2].format         = WGPUVertexFormat_Float32x2;
        attrs[2].offset         = 24;
        attrs[2].shaderLocation = 2;
        vb.arrayStride          = sizeof(MeshVertex);
        vb.stepMode             = WGPUVertexStepMode_Vertex;
        vb.attributeCount       = 6;
        vb.attributes           = attrs;
    } else {
        attrs[0].format         = WGPUVertexFormat_Float32x2;
        attrs[0].offset         = 0;
        attrs[0].shaderLocation = 0;
        attrs[1].format         = WGPUVertexFormat_Float32x4;
        attrs[1].offset         = 8;
        attrs[1].shaderLocation = 1;
        attrs[2].format         = WGPUVertexFormat_Float32x2;
        attrs[2].offset         = 24;
        attrs[2].shaderLocation = 2;
        vb.arrayStride          = 32;
        vb.stepMode             = WGPUVertexStepMode_Vertex;
        vb.attributeCount       = 3;
        vb.attributes           = attrs;
    }
    pd.vertex.bufferCount = 1;
    pd.vertex.buffers     = &vb;

    // Keep both modules alive until CreateRenderPipeline has consumed the
    // descriptor. Storing only their raw handles in pd and destroying the
    // wrappers at the end of these branches leaves dangling handles for Dawn.
    wgpu::ShaderModule vertexModule;
    wgpu::ShaderModule fragmentModule;
    if (!vert.empty()) {
        WGPUShaderModuleDescriptor md = mdDesc(vert);
        vertexModule         = dev.CreateShaderModule(reinterpret_cast<const wgpu::ShaderModuleDescriptor*>(&md));
        pd.vertex.module     = vertexModule.Get();
        pd.vertex.entryPoint = sv("vs_main");
    } else {
        // Default textured vertex shader.
        WGPUShaderModuleDescriptor md = mdDesc(kTexturedVertWgsl);
        vertexModule         = dev.CreateShaderModule(reinterpret_cast<const wgpu::ShaderModuleDescriptor*>(&md));
        pd.vertex.module     = vertexModule.Get();
        pd.vertex.entryPoint = sv("vs_main");
    }

    // The descriptor borrows these until CreateRenderPipeline returns.
    WGPUFragmentState    fs{};
    WGPUColorTargetState target{};
    if (!frag.empty()) {
        WGPUShaderModuleDescriptor md = mdDesc(frag);
        fragmentModule   = dev.CreateShaderModule(reinterpret_cast<const wgpu::ShaderModuleDescriptor*>(&md));
        fs.module        = fragmentModule.Get();
        fs.entryPoint    = sv("fs_main");
        fs.targetCount   = 1;
        target.format    = format;
        target.writeMask = WGPUColorWriteMask_All;
        if (blend) {
            static WGPUBlendState bs = alphaBlend();
            target.blend             = &bs;
        }
        fs.targets  = &target;
        pd.fragment = &fs;
    } else {
        pd.fragment = nullptr;
    }

    pd.primitive.topology         = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace        = WGPUFrontFace_CCW;
    pd.primitive.cullMode         = hair ? WGPUCullMode_None : WGPUCullMode_None;
    pd.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;

    if (depth && !shadow) {
        static WGPUDepthStencilState ds{};
        ds.format            = WGPUTextureFormat_Depth32Float;
        ds.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        ds.depthCompare      = hair ? WGPUCompareFunction_Less : WGPUCompareFunction_LessEqual;
        pd.depthStencil      = &ds;
    }
    pd.multisample.count = sampleCount ? sampleCount : 1;
    // mask=0 (zero-init) silently discards all fragments; use all-samples.
    pd.multisample.mask = 0xFFFFFFFFu;
    return dev.CreateRenderPipeline(reinterpret_cast<const wgpu::RenderPipelineDescriptor*>(&pd));
}

}  // namespace

Shader* Graphics::newShaderFromSpv(const std::vector<uint32_t>& vertSpv, const std::vector<uint32_t>& fragSpv) {
    if (!device) throw Exception("newShaderFromSpv: device not initialized");
    // Native Dawn can consume Vulkan SPIR-V directly; the browser build cannot
    // (browsers only accept WGSL). The WebGPU backend therefore requires WGSL
    // for custom shaders; SPIR-V input is rejected with a clear message.
    throw Exception(
        "newShaderFromSpv: SPIR-V custom shaders are not supported on the "
        "WebGPU backend (browsers accept WGSL only). Recompile the shader "
        "with the WebGPU toolchain (glslc+tint) and use newShaderFromWgsl.");
}

Shader* Graphics::newShaderFromSpvFile(const std::string& vertPath, const std::string& fragPath) {
    throw Exception(
        "newShaderFromSpvFile: SPIR-V custom shaders are not supported on the "
        "WebGPU backend. Use WGSL shaders instead.");
}

bool Graphics::releaseShader(Shader* shader) {
    if (!shader || !shader->gpuHandle) return false;

    auto* gpu   = static_cast<GpuShader*>(shader->gpuHandle);
    auto  gpuIt = std::find_if(ownedGpuShaders.begin(), ownedGpuShaders.end(),
                               [&](const std::unique_ptr<GpuShader>& g) { return g.get() == gpu; });
    if (gpuIt == ownedGpuShaders.end()) return false;

    auto shIt = std::find_if(ownedShaders.begin(), ownedShaders.end(),
                             [&](const std::unique_ptr<Shader>& s) { return s.get() == shader; });
    if (shIt == ownedShaders.end()) return false;

    shader->gpuHandle = nullptr;
    ownedGpuShaders.erase(gpuIt);
    // Transfer the CPU facade to the caller instead of destroying it.
    (void)shIt->release();
    ownedShaders.erase(shIt);
    return true;
}

Shader* Graphics::newShader(const std::string& vertGlsl, const std::string& fragGlsl) {
    (void)vertGlsl;
    (void)fragGlsl;
    throw Exception(
        "newShader: runtime GLSL compilation is not available on the WebGPU "
        "backend (browser WGSL only). Ship pre-compiled WGSL shaders.");
}

Shader* Graphics::newMeshShaderFromSpv(const std::vector<uint32_t>& vertSpv, const std::vector<uint32_t>& fragSpv) {
    (void)vertSpv;
    (void)fragSpv;
    throw Exception(
        "newMeshShaderFromSpv: SPIR-V custom mesh shaders are not supported on the "
        "WebGPU backend. Use WGSL shaders instead.");
}

Shader* Graphics::newMeshShaderFromWgsl(const std::string& vertWgsl, const std::string& fragWgsl) {
    if (!device) throw Exception("newMeshShaderFromWgsl: device not initialized");
    if (!mesh3dSetLayout) throw Exception("newMeshShaderFromWgsl: mesh3d layout missing");

    std::string vert = vertWgsl.empty() ? kMesh3DVertWgsl : vertWgsl;
    std::string frag = fragWgsl.empty() ? kMesh3DFragWgsl : fragWgsl;

    auto gpu      = std::make_unique<GpuShader>();
    gpu->isMesh3D = true;
    gpu->wgslVert = vert;
    gpu->wgslFrag = frag;
    auto error    = checkedShaderBuild(device, instance, [&] {
        gpu->mesh3dPipeline = buildPipelineFromWgsl(device, mesh3dPipelineLayout, sceneColorFormat, vert, frag,
                                                       /*depth*/ true, /*blend*/ false, /*mesh3d*/ true, /*hair*/ false,
                                                       /*shadow*/ false, /*gbuffer*/ false,
                                                       /*sampleCount*/ sceneColorSamples);
    });
    if (!error.empty() || !gpu->mesh3dPipeline)
        throw Exception("newMeshShaderFromWgsl: %s", error.empty() ? "pipeline allocation failed" : error.c_str());
    // X-ray variant: depth test/write off + alpha blend so occluded silhouettes
    // paint over the building (the shader discards visible fragments itself).
    error = checkedShaderBuild(device, instance, [&] {
        gpu->mesh3dXrayPipeline = buildPipelineFromWgsl(device, mesh3dPipelineLayout, sceneColorFormat, vert, frag,
                                                        /*depth*/ false, /*blend*/ true, /*mesh3d*/ true,
                                                        /*hair*/ false, /*shadow*/ false, /*gbuffer*/ false,
                                                        /*sampleCount*/ sceneColorSamples);
    });
    if (!error.empty() || !gpu->mesh3dXrayPipeline)
        throw Exception("newMeshShaderFromWgsl: %s", error.empty() ? "xray pipeline allocation failed" : error.c_str());

    auto sh = std::make_unique<Shader>();
    sh->setKind(Shader::Kind::eMesh3D);
    sh->gpuHandle = gpu.get();

    Shader* raw = sh.get();
    ownedShaders.push_back(std::move(sh));
    ownedGpuShaders.push_back(std::move(gpu));
    return raw;
}

eve::Result<void> Graphics::configureMeshShaderSurface(Shader& shader, BlendMode blend, bool depthWrite,
                                                       bool doubleSided) {
    auto found = std::find_if(ownedGpuShaders.begin(), ownedGpuShaders.end(),
                              [&](const auto& gpu) { return gpu.get() == shader.gpuHandle; });
    if (found == ownedGpuShaders.end() || !(*found)->isMesh3D)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "Expected an owned WebGPU mesh shader"));
    auto&                gpu        = **found;
    const bool           alphaBlend = blend == BlendMode::Alpha;
    wgpu::RenderPipeline candidate;
    auto                 error = checkedShaderBuild(device, instance, [&] {
        candidate =
            buildPipelineFromWgsl(device, mesh3dPipelineLayout, sceneColorFormat, gpu.wgslVert, gpu.wgslFrag, true,
                                                  alphaBlend, true, doubleSided, false, false, sceneColorSamples, depthWrite);
    });
    if (!error.empty() || !candidate)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, error.empty() ? "WebGPU rejected mesh surface pipeline" : error));
    gpu.mesh3dPipeline     = std::move(candidate);
    shader.meshBlend       = blend;
    shader.meshDepthWrite  = depthWrite;
    shader.meshDoubleSided = doubleSided;
    return eve::Result<void>::success();
}

Shader* Graphics::newMeshShader(const std::string& vertGlsl, const std::string& fragGlsl) {
    (void)vertGlsl;
    (void)fragGlsl;
    throw Exception(
        "newMeshShader: runtime GLSL compilation is not available on the WebGPU "
        "backend. Ship pre-compiled WGSL shaders.");
}

Shader* Graphics::newShaderFromWgsl(const std::string& vertWgsl, const std::string& fragWgsl) {
    if (!device) throw Exception("newShaderFromWgsl: device not initialized");
    if (fragWgsl.empty()) throw Exception("newShaderFromWgsl: empty fragment WGSL");
    if (!tex2DPipelineLayout) throw Exception("newShaderFromWgsl: 2D pipeline layout missing");

    std::string vert = vertWgsl.empty() ? kTexturedVertWgsl : vertWgsl;

    auto gpu      = std::make_unique<GpuShader>();
    gpu->isMesh3D = false;
    gpu->wgslVert = vert;
    gpu->wgslFrag = fragWgsl;
    // 2D custom shader: alpha blend, no depth; one pipeline per target format
    // (swapchain surface vs RGBA8Unorm offscreen canvas).
    auto error = checkedShaderBuild(device, instance, [&] {
        gpu->swapchainPipeline = createPipelineForShader(gpu.get(), wgpu::TextureFormat(surfaceFormat),
                                                         /*depth*/ false, /*mesh3d*/ false,
                                                         /*hair*/ false, /*shadow*/ false,
                                                         /*gbuffer*/ false, tex2DPipelineLayout);
    });
    if (!error.empty() || !gpu->swapchainPipeline)
        throw Exception("newShaderFromWgsl: %s", error.empty() ? "pipeline allocation failed" : error.c_str());
    error = checkedShaderBuild(device, instance, [&] {
        gpu->offscreenPipeline = createPipelineForShader(gpu.get(), wgpu::TextureFormat::RGBA8Unorm,
                                                         /*depth*/ false, /*mesh3d*/ false,
                                                         /*hair*/ false, /*shadow*/ false,
                                                         /*gbuffer*/ false, tex2DPipelineLayout);
        gpu->swapchainOpaquePipeline =
            buildPipelineFromWgsl(device, tex2DPipelineLayout, WGPUTextureFormat(surfaceFormat), vert, fragWgsl, false,
                                  false, false, false, false, false, 1u);
        gpu->offscreenOpaquePipeline =
            buildPipelineFromWgsl(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA8Unorm, vert, fragWgsl, false,
                                  false, false, false, false, false, 1u);
        gpu->hdrOffscreenPipeline = createPipelineForShader(gpu.get(), wgpu::TextureFormat::RGBA16Float, false, false,
                                                            false, false, false, tex2DPipelineLayout);
        gpu->hdrOffscreenOpaquePipeline =
            buildPipelineFromWgsl(device, tex2DPipelineLayout, WGPUTextureFormat_RGBA16Float, vert, fragWgsl, false,
                                  false, false, false, false, false, 1u);
    });
    if (!error.empty() || !gpu->offscreenPipeline || !gpu->swapchainOpaquePipeline || !gpu->offscreenOpaquePipeline ||
        !gpu->hdrOffscreenPipeline || !gpu->hdrOffscreenOpaquePipeline)
        throw Exception("newShaderFromWgsl: %s", error.empty() ? "pipeline allocation failed" : error.c_str());

    auto sh = std::make_unique<Shader>();
    sh->setKind(Shader::Kind::eSprite2D);
    sh->gpuHandle = gpu.get();

    Shader* raw = sh.get();
    ownedShaders.push_back(std::move(sh));
    ownedGpuShaders.push_back(std::move(gpu));
    return raw;
}

wgpu::RenderPipeline Graphics::createPipelineForShader(GpuShader* gs, wgpu::TextureFormat format, bool depth,
                                                       bool mesh3d, bool hair, bool shadow, bool gbuffer,
                                                       wgpu::PipelineLayout layout) {
    if (!gs || gs->wgslFrag.empty()) return {};
    // Custom 2D/mesh shaders blend only when the caller asks (x-ray / 2D UI);
    // opaque meshes and shadow/gbuffer variants keep write-through targets.
    const bool     blend   = !depth;
    const uint32_t samples = mesh3d || hair || shadow || gbuffer ? sceneColorSamples : 1u;
    return buildPipelineFromWgsl(device, layout, WGPUTextureFormat(format), gs->wgslVert, gs->wgslFrag, depth, blend,
                                 mesh3d, hair, shadow, gbuffer, samples);
}

Shader* Graphics::newHairShaderFromSpv(const std::vector<uint32_t>& vertSpv, const std::vector<uint32_t>& fragSpv) {
    (void)vertSpv;
    (void)fragSpv;
    throw Exception(
        "newHairShaderFromSpv: SPIR-V hair shaders are not supported on WebGPU; "
        "use newHairShaderFromWgsl");
}

Shader* Graphics::newHairShaderFromWgsl(const std::string& vertWgsl, const std::string& fragWgsl) {
    if (!device) throw Exception("newHairShaderFromWgsl: device not initialized");
    if (fragWgsl.empty()) throw Exception("newHairShaderFromWgsl: empty fragment WGSL");
    auto gpu      = std::make_unique<GpuShader>();
    gpu->isMesh3D = true;
    gpu->isHair3D = true;
    gpu->wgslVert = vertWgsl.empty() ? kMesh3DVertWgsl : vertWgsl;
    gpu->wgslFrag = fragWgsl;
    auto error    = checkedShaderBuild(device, instance, [&] {
        gpu->mesh3dPipeline =
            buildPipelineFromWgsl(device, mesh3dPipelineLayout, sceneColorFormat, gpu->wgslVert, gpu->wgslFrag, true,
                                     true, true, true, false, false, sceneColorSamples);
    });
    if (!error.empty() || !gpu->mesh3dPipeline)
        throw Exception("newHairShaderFromWgsl: %s", error.empty() ? "pipeline allocation failed" : error.c_str());
    auto shader = std::make_unique<Shader>();
    shader->setKind(Shader::Kind::eMesh3D);
    shader->gpuHandle = gpu.get();
    Shader* raw       = shader.get();
    ownedShaders.push_back(std::move(shader));
    ownedGpuShaders.push_back(std::move(gpu));
    return raw;
}


}  // namespace eve::graphics::webgpu
