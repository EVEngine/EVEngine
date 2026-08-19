// Vulkan backend implementation — 3D frame, shadow and G-buffer passes.
//
// Split out of the original single 5100-line Graphics.cpp so several
// agents can work on backend concerns (lifecycle / pipelines / 2D / 3D /
// mesh) without touching the same translation unit. This file only
// defines members of vulkan::Graphics; see GraphicsInternal.h for the
// shared implementation helpers.

#include "graphics/AmbientOcclusion.h"
#include "graphics/AntiAliasing.h"
#include "graphics/GlobalIllumination.h"
#include "graphics/Light.h"
#include "graphics/Outline.h"
#include "graphics/RenderControl.h"
#include "graphics/vulkan/Canvas.h"
#include "graphics/vulkan/Graphics.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "common/Exception.h"
#include "common/StartupTiming.h"
#include "common/config.h"
#include "filesystem/Filesystem.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"

#include <memory>


#include <assimp/mesh.h>
#include <assimp/matrix3x3.h>
#include <assimp/matrix4x4.h>
#include <assimp/vector3.h>
#include <glm/gtc/matrix_transform.hpp>

#include "graphics/shaders/mesh3d_vert_spv.inc"
#include "graphics/shaders/mesh3d_frag_spv.inc"
#include "graphics/shaders/voxel_rect_vert_spv.inc"
#include "graphics/shaders/voxel_rect_frag_spv.inc"
#include "graphics/vulkan/GraphicsInternal.h"

namespace eve::graphics::vulkan {

void Graphics::begin3DFrame() {
    ASSERT(initialized);
    if (!initialized) throw Exception("begin3DFrame: graphics not initialized");
    if (isCanvasActive()) throw Exception("begin3DFrame: cannot start 3D while a Canvas is active");
    if (swapchainPassOpen) {
        // Previous eve_render opened 3D then threw before present().
        try {
            flushToSwapchain();
        } catch (...) {
            abortOpen3DFrame();
        }
    }
    // Soft-fail like flushToSwapchain: on Android/iOS the surface may still be
    // settling after orientation change; throwing would abort the whole script.
    if (!beginPresentCommandBuffer())
        return;
    recordPendingShadowPasses();
    recordPendingGBufferPass();

    // 3D pass clears with backgroundColor (setBackgroundColor), not a stale 2D clear.
    clearColor = backgroundColor;
    createSceneColorResources(int(swapchain.extent.width), int(swapchain.extent.height));
    if (beginSceneColorRenderPass()) {
        // Rebuild scene-pass pipelines to match the active scene pass (MSAA).
        // Must happen AFTER the pass opens: if the MSAA scene pass is unavailable
        // we fall back to the swapchain (e1) render pass below, and rasterizing
        // with an Nx pipeline into an e1 pass is UB that can hang the GPU (TDR).
        ensureScenePassPipelines(activeScenePass(), activeSceneSamples());
    } else {
        // Scene pass unavailable — draw 3D straight into the swapchain. Scene-pass
        // pipelines must be 1x / swapchain-compatible to avoid the samples mismatch.
        ensureScenePassPipelines(renderpass, vk::SampleCountFlagBits::e1);
        beginSwapchainColorPass();
    }

    auto &cb = currentPresentCb();
    setViewportAndScissor(cb, swapchain.extent.width, swapchain.extent.height);

    currentMesh3dFrameSlots().drawIndex = 0;
    currentMesh3dClusteredFrameSlots().drawIndex = 0;
    currentVoxelInstanceFrame().drawIndex = 0;
    swapchainPassOpen = true;
    frameHad3D = true;
    hasPendingClear = false;
}

void Graphics::ensureOffscreen3DResources() {
    auto &device = getDevice();
    if (!offscreen3DRenderPass) {
        offscreen3DRenderPass =
            device.createRenderPass()
                .addSampledColorAttachment(vk::Format::eR8G8B8A8Unorm)
                .addDepthAttachment(depthFormat, vk::AttachmentLoadOp::eClear,
                                    vk::AttachmentStoreOp::eDontCare)
                .addSubpass(vkb::SubpassBuilder()
                                .addAttachmentRef(0, vk::ImageLayout::eColorAttachmentOptimal)
                                .setDepthStencilAttachment(
                                    1, vk::ImageLayout::eDepthStencilAttachmentOptimal))
                .addExternalShaderReadDependencies()
                .build();
        offscreen3DMeshPipeline = createMesh3DStylePipeline(
            embeddedSpirv(mesh3d_vert_spv), embeddedSpirv(mesh3d_frag_spv), mesh3dPipelineLayout,
            offscreen3DRenderPass, vk::SampleCountFlagBits::e1);
    }
    if (!offscreen3DPool) {
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient;
        offscreen3DPool = device->createCommandPool(poolInfo);
        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.commandPool = offscreen3DPool;
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = 1;
        auto bufs = device->allocateCommandBuffers(allocInfo);
        offscreen3DCB = bufs[0];
        vk::FenceCreateInfo fenceInfo{};
        fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;  // first wait passes immediately
        offscreen3DFence = device->createFence(fenceInfo);
    }
}

void Graphics::destroyOffscreen3DResources() {
    auto &device = getDevice();
    if (offscreen3DFence) {
        device->destroyFence(offscreen3DFence);
        offscreen3DFence = nullptr;
    }
    if (offscreen3DPool) {
        device->destroyCommandPool(offscreen3DPool);
        offscreen3DPool = nullptr;
    }
    offscreen3DCB = nullptr;
    if (offscreen3DMeshPipeline) {
        device->destroyPipeline(offscreen3DMeshPipeline);
        offscreen3DMeshPipeline = nullptr;
    }
    if (offscreen3DRenderPass) {
        device->destroyRenderPass(offscreen3DRenderPass);
        offscreen3DRenderPass = {};
    }
}

void Graphics::begin3DFrameToCanvas(Canvas *canvas) {
    ASSERT(initialized);
    if (!initialized) throw Exception("begin3DFrameToCanvas: graphics not initialized");
    if (!canvas) throw Exception("begin3DFrameToCanvas: null canvas");
    auto *oc = dynamic_cast<OffscreenCanvas *>(canvas);
    if (!oc) throw Exception("begin3DFrameToCanvas: not an offscreen canvas");
    if (offscreen3DPassOpen) throw Exception("begin3DFrameToCanvas: already open");
    if (swapchainPassOpen) {
        try {
            flushToSwapchain();
        } catch (...) {
            abortOpen3DFrame();
        }
    }
    clearColor = backgroundColor;
    ensureOffscreen3DResources();
    oc->ensure3D();
    offscreen3DCanvas = oc;

    if (offscreen3DFence) (void)device->waitForFences(1, &offscreen3DFence, VK_TRUE, UINT64_MAX);
    vk::CommandBufferBeginInfo beginInfo{};
    offscreen3DCB.begin(beginInfo);
    std::array<vk::ClearValue, 2> clears{};
    clears[0].color =
        vk::ClearColorValue(std::array<float, 4>{clearColor.r, clearColor.g, clearColor.b, 1.f});
    clears[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
    vk::RenderPassBeginInfo rpBegin{};
    rpBegin.renderPass = offscreen3DRenderPass;
    rpBegin.framebuffer = oc->framebuffer3D();
    rpBegin.renderArea =
        vk::Rect2D{{0, 0}, {uint32_t(oc->getWidth()), uint32_t(oc->getHeight())}};
    rpBegin.clearValueCount = uint32_t(clears.size());
    rpBegin.pClearValues = clears.data();
    oc->colorImage().beginColorAttachment();
    oc->depthImage().beginDepthAttachment();
    offscreen3DCB.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
    setViewportAndScissor(offscreen3DCB, oc->getWidth(), oc->getHeight());

    currentMesh3dFrameSlots().drawIndex = 0;
    currentMesh3dClusteredFrameSlots().drawIndex = 0;
    offscreen3DPassOpen = true;
    frameHad3D = true;
    hasPendingClear = false;
}

void Graphics::end3DFrameToCanvas() {
    if (!offscreen3DPassOpen || !offscreen3DCB) return;
    offscreen3DCB.endRenderPass();
    if (offscreen3DCanvas) {
        offscreen3DCanvas->colorImage().setLayout(offscreen3DCB,
                                                  vk::ImageLayout::eShaderReadOnlyOptimal);
    }
    offscreen3DCB.end();

    // Submit the offscreen render directly to the graphics queue (no present),
    // then wait for it so it is fully drained before any subsequent render or
    // teardown.
    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &offscreen3DCB;
    if (offscreen3DFence) (void)device->resetFences(1, &offscreen3DFence);
    (void)device.getQueue(vkb::QueueType::graphics).submit(1, &submitInfo, offscreen3DFence);
    if (offscreen3DFence)
        (void)device->waitForFences(1, &offscreen3DFence, VK_TRUE, UINT64_MAX);

    offscreen3DPassOpen = false;
    offscreen3DCanvas = nullptr;
}

void Graphics::setMesh3DLight(const glm::vec3 &dir, const glm::vec3 &color) {
    glm::vec3 d = glm::normalize(dir);
    if (glm::length(d) < 1e-6f) d = glm::vec3(0.f, 1.f, 0.f);
    mesh3dFrameUbo.lightDir = glm::vec4(d, mesh3dFrameUbo.lightDir.w);
    // Preserve .w (envIntensity) — filled per-draw in drawMeshShader.
    mesh3dFrameUbo.lightColor = glm::vec4(color, mesh3dFrameUbo.lightColor.w);
}

void Graphics::setMesh3DCameraPos(const glm::vec3 &eye) {
    mesh3dFrameUbo.cameraPos = glm::vec4(eye, mesh3dFrameUbo.cameraPos.w);
}

void Graphics::destroyVoxelRectResources() {
    for (auto &frame : voxelInstanceFrames) {
        for (auto &slot : frame.slots) {
            slot.buffer.release();
            slot.capacityBytes = 0;
        }
        frame.slots.clear();
        frame.drawIndex = 0;
    }
    voxelInstanceFrames.clear();
    voxelRectSets.clear();
    auto destroyBuf = [&](vkb::GenericBuffer &b) { b.release(); };
    if (voxelUnitQuadReady) {
        destroyBuf(voxelUnitQuadVerts);
        destroyBuf(voxelUnitQuadIndices);
        voxelUnitQuadReady = false;
    }
    if (voxelRectPipeline) {
        device->destroyPipeline(voxelRectPipeline);
        voxelRectPipeline = vk::Pipeline{};
    }
    if (voxelRectPipelineLayout) {
        device->destroyPipelineLayout(voxelRectPipelineLayout);
        voxelRectPipelineLayout = vk::PipelineLayout{};
    }
    voxelRectSetLayoutUnique.reset();
    voxelRectSetLayout = vk::DescriptorSetLayout{};
}

void Graphics::createVoxelRectPipeline() {
    if (voxelRectPipeline) return;

    if (!voxelRectPipelineLayout) {
        vkb::DescriptorSetLayoutBuilder layoutBuilder;
        voxelRectSetLayoutUnique =
            layoutBuilder
                .image(0, vk::DescriptorType::eCombinedImageSampler,
                       vk::ShaderStageFlagBits::eFragment, 1)
                .createUnique(device.instance);
        voxelRectSetLayout = *voxelRectSetLayoutUnique;

        const auto pcr = pushConstantRange(vk::ShaderStageFlagBits::eVertex, sizeof(VoxelRectPC));
        voxelRectPipelineLayout = createPipelineLayout(device, voxelRectSetLayout, &pcr);
    }

    voxelRectPipeline = buildVoxelRectPipeline(renderpass, vk::SampleCountFlagBits::e1);
    ensureVoxelUnitQuad();
}

vk::Pipeline Graphics::buildVoxelRectPipeline(const vkb::BuiltRenderPass &rp,
                                              vk::SampleCountFlagBits samples) {
    auto vert = embeddedSpirv(voxel_rect_vert_spv);
    auto frag = embeddedSpirv(voxel_rect_frag_spv);
    ShaderModulePair modules(device, vert, frag);

    vk::VertexInputBindingDescription bindings[3]{};
    bindings[0].binding = 0;
    bindings[0].stride = sizeof(glm::vec2);
    bindings[0].inputRate = vk::VertexInputRate::eVertex;
    bindings[1].binding = 1;
    bindings[1].stride = sizeof(uint32_t);
    bindings[1].inputRate = vk::VertexInputRate::eInstance;
    bindings[2].binding = 2;
    bindings[2].stride = sizeof(uint32_t);
    bindings[2].inputRate = vk::VertexInputRate::eInstance;

    vk::VertexInputAttributeDescription attrs[3]{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = vk::Format::eR32G32Sfloat;
    attrs[0].offset = 0;
    attrs[1].location = 1;
    attrs[1].binding = 1;
    attrs[1].format = vk::Format::eR32Uint;
    attrs[1].offset = 0;
    attrs[2].location = 2;
    attrs[2].binding = 2;
    attrs[2].format = vk::Format::eR32Uint;
    attrs[2].offset = 0;

    vk::PipelineVertexInputStateCreateInfo vi{};
    vi.vertexBindingDescriptionCount = 3;
    vi.pVertexBindingDescriptions = bindings;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;

    // Unit-quad indices 0-2-1 / 0-3-2 are object-space CCW for outward faces.
    // perspectiveVulkanRH_ZO flips clip Y, so those faces are CCW in the
    // framebuffer — CounterClockwise frontFace keeps them. Clockwise + Back
    // (the mesh-pipeline convention) culls every submitted face after the Y
    // flip; CPU 6-dir cull already dropped the true back faces, so the frame
    // collapses to a 1px silhouette.
    return device.createPipeline()
        .useClassicPipeline(modules.vert, modules.frag)
        .setPipelineLayout(voxelRectPipelineLayout)
        .setVertexInputState(vi)
        .setDynamicStatesViewportScissor()
        .setRasterizer(vk::PolygonMode::eFill, false, false, 1.0f, vk::CullModeFlagBits::eBack,
                       vk::FrontFace::eCounterClockwise)
        .setMultisampler(false, samples)
        .setDepthStencil(true, true, vk::CompareOp::eLess)
        .setColorAttachmentCount(1)
        .build(rp);
}

void Graphics::ensureVoxelUnitQuad() {
    if (voxelUnitQuadReady) return;
    const float corners[8] = {0.f, 0.f, 1.f, 0.f, 1.f, 1.f, 0.f, 1.f};
    voxelUnitQuadVerts.allocate(frameToken(), device, vk::BufferUsageFlagBits::eVertexBuffer, sizeof(corners),
                                vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent);
    voxelUnitQuadVerts.updateLocal(frameToken(), corners, sizeof(corners));
    const uint32_t indices[6] = {0, 2, 1, 0, 3, 2};
    voxelUnitQuadIndices.allocate(frameToken(), device, vk::BufferUsageFlagBits::eIndexBuffer, sizeof(indices),
                                  vk::MemoryPropertyFlagBits::eHostVisible |
                                      vk::MemoryPropertyFlagBits::eHostCoherent);
    voxelUnitQuadIndices.updateLocal(frameToken(), indices, sizeof(indices));
    voxelUnitQuadReady = true;
}

vkb::BoundSet Graphics::voxelRectSetFor(GpuTexture *gpuTex) {
    ASSERT(gpuTex != nullptr);
    auto it = voxelRectSets.find(gpuTex);
    if (it != voxelRectSets.end()) return it->second;

    vk::DescriptorSetAllocateInfo alloc{};
    alloc.descriptorPool = descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &voxelRectSetLayout;
    vkb::UnboundSet unbound{device->allocateDescriptorSets(alloc).front()};

    vkb::DescriptorSetUpdater updater(4, 4, 0);
    updater.beginDescriptorSet(unbound)
        .beginImages(0, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(gpuTex->sampler, gpuTex->imageView()))
        .update(device.instance);

    vkb::BoundSet bound = std::move(unbound).publish();
    voxelRectSets.emplace(gpuTex, bound);
    return bound;
}

void Graphics::drawVoxelFaceInstances(const uint32_t *packed, int count, float originX,
                                      float originY, float originZ, const std::string &faceDir,
                                      Texture *atlas, int tilesPerRow, const uint32_t *ao) {
    ASSERT(initialized);
    if (!initialized) throw Exception("drawVoxelFaceInstances: graphics not initialized");
    if (!swapchainPassOpen) throw Exception("drawVoxelFaceInstances: call begin3DFrame first");
    if (!voxelRectPipeline) createVoxelRectPipeline();
    if (!packed || count <= 0) return;

    int face = -1;
    if (faceDir == "posX" || faceDir == "+x")
        face = 0;
    else if (faceDir == "negX" || faceDir == "-x")
        face = 1;
    else if (faceDir == "posY" || faceDir == "+y")
        face = 2;
    else if (faceDir == "negY" || faceDir == "-y")
        face = 3;
    else if (faceDir == "posZ" || faceDir == "+z")
        face = 4;
    else if (faceDir == "negZ" || faceDir == "-z")
        face = 5;
    else
        throw Exception("drawVoxelFaceInstances: unknown faceDir '%s'", faceDir.c_str());

    Texture *tex = atlas ? atlas : whiteTexture;
    if (!tex || !tex->gpuHandle) throw Exception("drawVoxelFaceInstances: missing texture");
    auto *gpuTex = static_cast<GpuTexture *>(tex->gpuHandle);

    ensureVoxelUnitQuad();

    const vk::DeviceSize bytes = vk::DeviceSize(count) * sizeof(uint32_t);
    auto &vframe = currentVoxelInstanceFrame();
    const size_t slotIndex = vframe.drawIndex++;
    while (vframe.slots.size() <= slotIndex) vframe.slots.emplace_back();
    auto &slot = vframe.slots[slotIndex];
    if (slot.capacityBytes < size_t(bytes) || !slot.buffer.buffer) {
        const size_t alloc = std::max(size_t(bytes), slot.capacityBytes ? slot.capacityBytes * 2 : size_t(bytes));
        slot.buffer.allocate(frameToken(), device, vk::BufferUsageFlagBits::eVertexBuffer, vk::DeviceSize(alloc),
                             vk::MemoryPropertyFlagBits::eHostVisible |
                                 vk::MemoryPropertyFlagBits::eHostCoherent);
        slot.capacityBytes = alloc;
    }
    slot.buffer.updateLocal(frameToken(), packed, size_t(bytes));

    // Ambient-occlusion words (null → full bright). Kept parallel to packed.
    const uint32_t defaultAO = 0xFFu;  // all four corners AO=3
    std::vector<uint32_t> aoDefaults;
    if (!ao) {
        aoDefaults.assign(size_t(count), defaultAO);
        ao = aoDefaults.data();
    }
    const vk::DeviceSize aoBytes = vk::DeviceSize(count) * sizeof(uint32_t);
    if (slot.aoCapacityBytes < size_t(aoBytes) || !slot.aoBuffer.buffer) {
        const size_t alloc = std::max(size_t(aoBytes),
                                      slot.aoCapacityBytes ? slot.aoCapacityBytes * 2
                                                           : size_t(aoBytes));
        slot.aoBuffer.allocate(frameToken(), device, vk::BufferUsageFlagBits::eVertexBuffer,
                               vk::DeviceSize(alloc),
                               vk::MemoryPropertyFlagBits::eHostVisible |
                                   vk::MemoryPropertyFlagBits::eHostCoherent);
        slot.aoCapacityBytes = alloc;
    }
    slot.aoBuffer.updateLocal(frameToken(), ao, size_t(aoBytes));

    VoxelRectPC pc{};
    pc.viewProj = mesh3dFrameUbo.mvp;
    pc.chunkOrigin = glm::vec4(originX, originY, originZ, float(face));
    pc.atlasInfo = glm::vec4(float(std::max(1, tilesPerRow)), 0.f, 0.f, 0.f);
    pc.tint = mesh3dFrameUbo.tint;

    auto &cb = currentPresentCb();
    vk::DescriptorSet set = voxelRectSetFor(gpuTex);
    cb.bindPipeline(vk::PipelineBindPoint::eGraphics, voxelRectPipeline);
    cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, voxelRectPipelineLayout, 0, 1, &set, 0,
                          nullptr);
    cb.pushConstants(voxelRectPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(VoxelRectPC),
                     &pc);

    vk::Buffer vbufs[3] = {voxelUnitQuadVerts.buffer, slot.buffer.buffer, slot.aoBuffer.buffer};
    vk::DeviceSize offsets[3] = {0, 0, 0};
    cb.bindVertexBuffers(0, 3, vbufs, offsets);
    cb.bindIndexBuffer(voxelUnitQuadIndices.buffer, 0, vk::IndexType::eUint32);
    cb.drawIndexed(6, uint32_t(count), 0, 0, 0);
}

void Graphics::setMesh3DNormalTexture(Texture *normal) { mesh3dNormalTexture = normal; }

void Graphics::setMesh3DHeightTexture(Texture *height) { mesh3dHeightTexture = height; }

void Graphics::setMesh3DSceneDepth(Texture *depth) { mesh3dSceneDepthTexture = depth; }

void Graphics::setMesh3DEnv(Texture *cube, float intensity) {
    mesh3dEnvTexture = cube;
    mesh3dEnvIntensity = intensity < 0.f ? 0.f : intensity;
    if (!cube) mesh3dEnvIntensity = 0.f;
}

void Graphics::setMesh3DShadows(const ShadowUpload &upload) { mesh3dShadows = upload; }

void Graphics::setMesh3DShadowReceive(bool receive) { mesh3dShadowReceive = receive; }

void Graphics::beginShadowPass(int cascadeIndex) {
    ASSERT(initialized);
    if (!shadowPipeline) createShadowResources();
    if (cascadeIndex < 0 || cascadeIndex >= ShadowConfig::kCascades) {
        throw Exception("beginShadowPass: cascadeIndex out of range");
    }
    shadowPassCascade = cascadeIndex;
    shadowPassDraws.clear();
}

void Graphics::drawMeshShadow(Mesh *mesh, const glm::mat4 &lightMVP) {
    if (shadowPassCascade < 0) throw Exception("drawMeshShadow: call beginShadowPass first");
    if (!mesh || !mesh->gpuHandle) throw Exception("drawMeshShadow: null mesh");
    shadowPassDraws.push_back(ShadowDraw{mesh, lightMVP});
}

void Graphics::drawMeshShadowAlpha(Mesh *mesh, const glm::mat4 &lightMVP, Texture *albedo) {
    if (shadowPassCascade < 0) throw Exception("drawMeshShadowAlpha: call beginShadowPass first");
    if (!mesh || !mesh->gpuHandle) throw Exception("drawMeshShadowAlpha: null mesh");
    ShadowDraw d;
    d.mesh = mesh;
    d.mvp = lightMVP;
    d.albedo = albedo;
    d.alphaTest = true;
    shadowPassDraws.push_back(d);
}

void Graphics::endShadowPass() {
    if (shadowPassCascade < 0) throw Exception("endShadowPass: no active shadow pass");
    if (!shadowPipeline || !shadowRenderPass) {
        shadowPassCascade = -1;
        shadowPassDraws.clear();
        return;
    }
    const int cascade = shadowPassCascade;
    shadowCascadeDraws[cascade] = std::move(shadowPassDraws);
    shadowPassDraws.clear();
    shadowPassCascade = -1;
    shadowPendingMask |= (1u << cascade);
    // GPU work is recorded into the swapchain command buffer in
    // beginSwapchainRenderPass() so it shares the frame's submit and uses the
    // ping-pong copy for this frame slot.
}

void Graphics::beginGBufferPass(int width, int height) {
    ASSERT(initialized);
    if (!initialized) throw Exception("beginGBufferPass: graphics not initialized");
    if (width <= 0 || height <= 0) throw Exception("beginGBufferPass: invalid size");
    if (!texSetLayout || !descriptorPool)
        throw Exception("beginGBufferPass: textured descriptor layout not ready");
    createGBufferResources(width, height);
    gbufferPassActive = true;
    gbufferPassDraws.clear();
}

void Graphics::drawMeshGBuffer(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model, float nearZ,
                               float farZ, Texture *albedo, float tintR, float tintG, float tintB) {
    if (!gbufferPassActive) throw Exception("drawMeshGBuffer: call beginGBufferPass first");
    if (!mesh || !mesh->gpuHandle) throw Exception("drawMeshGBuffer: null mesh");
    GBufferDraw d{};
    d.mesh = mesh;
    d.albedo = albedo;
    d.push.mvp = mvp;
    d.push.modelR0 = glm::vec4(model[0][0], model[1][0], model[2][0], model[3][0]);
    d.push.modelR1 = glm::vec4(model[0][1], model[1][1], model[2][1], model[3][1]);
    d.push.modelR2 = glm::vec4(model[0][2], model[1][2], model[2][2], model[3][2]);
    auto u8 = [](float x) -> uint32_t {
        return uint32_t(std::lround(std::clamp(x, 0.f, 1.f) * 255.f));
    };
    const uint32_t packedTint = u8(tintR) | (u8(tintG) << 8) | (u8(tintB) << 16) | (255u << 24);
    d.push.clip = glm::vec4(nearZ, farZ, glm::uintBitsToFloat(packedTint), 0.f);
    gbufferPassDraws.push_back(d);
}

void Graphics::drawMeshGBufferAlpha(Mesh *mesh, const glm::mat4 &mvp, const glm::mat4 &model,
                                    float nearZ, float farZ, Texture *albedo, float tintR,
                                    float tintG, float tintB) {
    if (!gbufferPassActive) throw Exception("drawMeshGBufferAlpha: call beginGBufferPass first");
    if (!mesh || !mesh->gpuHandle) throw Exception("drawMeshGBufferAlpha: null mesh");
    GBufferDraw d{};
    d.mesh = mesh;
    d.albedo = albedo;
    d.alphaTest = true;
    d.push.mvp = mvp;
    d.push.modelR0 = glm::vec4(model[0][0], model[1][0], model[2][0], model[3][0]);
    d.push.modelR1 = glm::vec4(model[0][1], model[1][1], model[2][1], model[3][1]);
    d.push.modelR2 = glm::vec4(model[0][2], model[1][2], model[2][2], model[3][2]);
    auto u8 = [](float x) -> uint32_t {
        return uint32_t(std::lround(std::clamp(x, 0.f, 1.f) * 255.f));
    };
    const uint32_t packedTint = u8(tintR) | (u8(tintG) << 8) | (u8(tintB) << 16) | (255u << 24);
    d.push.clip = glm::vec4(nearZ, farZ, glm::uintBitsToFloat(packedTint), 0.f);
    gbufferPassDraws.push_back(d);
}

void Graphics::endGBufferPass() {
    if (!gbufferPassActive) throw Exception("endGBufferPass: no active gbuffer pass");
    gbufferPassActive = false;

    auto *slot = currentGBufferSlot();
    if (!gbufferPipeline || !gbufferRenderPass || !slot || !slot->framebuffer) {
        gbufferPassDraws.clear();
        gbufferPending = false;
        if (renderControl_) renderControl_->getGBuffer()->clear();
        return;
    }

    gbufferPending = true;
    if (renderControl_) {
        Texture *albedo = &slot->albedoTex;
        renderControl_->getGBuffer()->setTargets(gbufferWidth, gbufferHeight, &slot->depthColorTex,
                                                 &slot->normalTex, albedo, &slot->depthTex);
    }
}

void Graphics::ensureDefaultEnvCubemap() {
    if (defaultEnvCubemap) return;
    const uint8_t black[4] = {0, 0, 0, 255};
    std::vector<uint8_t> faces(6u * 4u);
    for (int i = 0; i < 6; ++i) {
        faces[size_t(i) * 4u + 0] = black[0];
        faces[size_t(i) * 4u + 1] = black[1];
        faces[size_t(i) * 4u + 2] = black[2];
        faces[size_t(i) * 4u + 3] = black[3];
    }
    defaultEnvCubemap = newCubemap(1, faces.data());
}

void Graphics::setMesh3DMaterial(float metallic, float roughness) {
    mesh3dMetallic = metallic;
    mesh3dRoughness = roughness;
    mesh3dFrameUbo.ambient.w = metallic;
    mesh3dFrameUbo.cameraPos.w = roughness;
}

void Graphics::setMesh3DTexCellBomb(float cellScale, float strength, float rotAmount) {
    mesh3dTexBombScale = cellScale > 1e-3f ? cellScale : 1e-3f;
    mesh3dTexBombStrength = strength < 0.f ? 0.f : (strength > 1.f ? 1.f : strength);
    mesh3dTexBombRot = rotAmount < 0.f ? 0.f : (rotAmount > 1.f ? 1.f : rotAmount);
    mesh3dFrameUbo.texBomb =
        glm::vec4(mesh3dTexBombScale, mesh3dTexBombStrength, mesh3dTexBombRot, 0.f);
}

void Graphics::setMesh3DParallax(float scale, float minLayers, float maxLayers) {
    mesh3dParallaxScale = scale < 0.f ? 0.f : (scale > 0.25f ? 0.25f : scale);
    float minL = minLayers < 1.f ? 1.f : minLayers;
    float maxL = maxLayers < minL ? minL : maxLayers;
    if (maxL > 64.f) maxL = 64.f;
    mesh3dParallaxMinLayers = minL;
    mesh3dParallaxMaxLayers = maxL;
    mesh3dFrameUbo.parallax =
        glm::vec4(mesh3dParallaxScale, mesh3dParallaxMinLayers, mesh3dParallaxMaxLayers, 0.f);
}

void Graphics::setMesh3DLighting(const Lighting3DPack &pack) {
    mesh3dLighting = pack;
    mesh3dFrameUbo.ambient = glm::vec4(glm::vec3(pack.ambient), mesh3dMetallic);    const int n = std::max(0, std::min(pack.count, Lighting3DPack::kMaxLights));
    mesh3dFrameUbo.lightDir.w = float(n);
    int dirI = -1;
    for (int i = 0; i < n; ++i) {
        if (pack.lights[i].posRadius.w <= 0.f) {
            dirI = i;
            break;
        }
    }
    if (dirI >= 0) {
        glm::vec3 d(pack.lights[dirI].posRadius);
        if (glm::length(d) < 1e-6f) d = glm::vec3(0.f, 1.f, 0.f);
        else d = glm::normalize(d);
        mesh3dFrameUbo.lightDir = glm::vec4(d, float(n));
        mesh3dFrameUbo.lightColor =
            glm::vec4(glm::vec3(pack.lights[dirI].color), mesh3dFrameUbo.lightColor.w);
    } else {
        // No directional: zero the legacy primary slot. mesh3d.frag always shades it.
        mesh3dFrameUbo.lightDir = glm::vec4(0.f, 1.f, 0.f, float(n));
        mesh3dFrameUbo.lightColor =
            glm::vec4(0.f, 0.f, 0.f, mesh3dFrameUbo.lightColor.w);
    }
}

void Graphics::setCloudShadows(float strength, float worldCell, float time, float windSpeed,
                               float windAngle, float coverage, float detail) {
    mesh3dFrameUbo.cloud = glm::vec4(std::clamp(strength, 0.f, 1.f), std::max(worldCell, 1e-4f),
                                     time, 0.f);
    mesh3dFrameUbo.cloudWind = glm::vec4(std::cos(windAngle) * windSpeed,
                                         std::sin(windAngle) * windSpeed,
                                         std::clamp(coverage, 0.f, 1.f), std::clamp(detail, 0.f, 1.f));
}

void Graphics::ensureFlatNormalTexture3D() {
    if (flatNormalTexture3D) return;
    const uint8_t px[4] = {128, 128, 255, 255};
    flatNormalTexture3D = newTexture(1, 1, px);
}

void Graphics::ensureFlatHeightTexture3D() {
    if (flatHeightTexture3D) return;
    // Mid-gray height: no relief when scale>0 without a real height map.
    const uint8_t px[4] = {128, 128, 128, 255};
    flatHeightTexture3D = newTexture(1, 1, px);
}

vkb::BoundSet Graphics::mesh3dSetFor(GpuTexture *gpuTex, GpuTexture *normalTex, GpuTexture *envTex,
                                     GpuTexture *heightTex, GpuTexture *depthTex,
                                     Mesh3dFrameSlots &fslots, size_t uboSlot) {
    ASSERT(gpuTex != nullptr);
    ASSERT(normalTex != nullptr);
    ASSERT(envTex != nullptr);
    ASSERT(heightTex != nullptr);
    ASSERT(currentShadowArrayView());
    while (fslots.slots.size() <= uboSlot) {
        fslots.slots.emplace_back();
        auto &s = fslots.slots.back();
        s.ubo.allocate(frameToken(), device, vk::BufferUsageFlagBits::eUniformBuffer, sizeof(Mesh3DUBO),
                       vk::MemoryPropertyFlagBits::eHostVisible |
                           vk::MemoryPropertyFlagBits::eHostCoherent);
        s.shadowUbo.allocate(frameToken(), device, vk::BufferUsageFlagBits::eUniformBuffer, sizeof(ShadowUBO),
                             vk::MemoryPropertyFlagBits::eHostVisible |
                                 vk::MemoryPropertyFlagBits::eHostCoherent);
    }
    auto &slot = fslots.slots[uboSlot];
    Mesh3dSetKey key{gpuTex, normalTex, envTex, heightTex, depthTex};
    auto it = slot.sets.find(key);
    if (it != slot.sets.end()) return it->second;

    vk::DescriptorSetAllocateInfo alloc{};
    alloc.descriptorPool = descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &mesh3dSetLayout;
    vkb::UnboundSet unbound{device->allocateDescriptorSets(alloc).front()};

    vkb::DescriptorSetUpdater updater(12, 12, 0);
    updater.beginDescriptorSet(unbound)
        .beginBuffers(0, 0, vk::DescriptorType::eUniformBuffer)
        .buffer(slot.ubo.buffer, 0, sizeof(Mesh3DUBO))
        .beginImages(1, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(gpuTex->sampler, gpuTex->imageView()))
        .beginImages(2, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(normalTex->sampler, normalTex->imageView()))
        .beginImages(3, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(envTex->sampler, envTex->imageView()))
        .beginBuffers(4, 0, vk::DescriptorType::eUniformBuffer)
        .buffer(slot.shadowUbo.buffer, 0, sizeof(ShadowUBO))
        .beginImages(5, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(shadowSampler, currentShadowArrayView()))
        .beginImages(6, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(heightTex->sampler, heightTex->imageView()))
        .beginImages(7, 0, vk::DescriptorType::eCombinedImageSampler)
        .image(vkb::SampledImage::forLaterSample(depthTex->sampler, depthTex->imageView()))
        .update(device.instance);

    vkb::BoundSet bound = std::move(unbound).publish();
    slot.sets.emplace(key, bound);
    return bound;
}

void Graphics::setMesh3DViewProj(const glm::mat4 &viewProj) {
    mesh3dFrameUbo.mvp = viewProj;
}

void Graphics::setMesh3DView(const glm::mat4 &view) {
    mesh3dFrameUbo.view = view;
}

void Graphics::setMesh3DClip(float nearZ, float farZ) {
    const float n = nearZ > 1e-4f ? nearZ : 0.1f;
    const float f = farZ > n ? farZ : n + 1.f;
    mesh3dFrameUbo.clipInfo = glm::vec4(n, f, mesh3dFrameUbo.clipInfo.z, mesh3dFrameUbo.clipInfo.w);
    mesh3dClustered.clipInfo.x = n;
    mesh3dClustered.clipInfo.y = f;
}


}  // namespace eve::graphics::vulkan
